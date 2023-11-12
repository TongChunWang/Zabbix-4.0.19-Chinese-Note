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

#include <excpt.h>
#include <DbgHelp.h>

#pragma comment(lib, "DbgHelp.lib")

#define STACKWALK_MAX_NAMELEN	4096

#define ZBX_LSHIFT(value, bits)	(((unsigned __int64)value) << bits)

extern const char	*progname;

#ifdef _M_X64

#define ZBX_IMAGE_FILE_MACHINE	IMAGE_FILE_MACHINE_AMD64

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 `print_register` 的静态函数，用于打印寄存器的名称和值。该函数接收两个参数：寄存器名（const char *name）和寄存器值（unsigned __int64 value）。函数使用 `zabbix_log` 函数将寄存器的名称和值以格式化的方式打印到日志中，便于调试和分析。
 ******************************************************************************/
/* 定义一个打印寄存器的函数，接收两个参数：寄存器名（const char *name）和寄存器值（unsigned __int64 value）
/******************************************************************************
 * *
 *整个代码块的主要目的是打印出在程序崩溃时，各个寄存器的值以及程序计数器的值。其中，静态函数 `print_fatal_info` 接受一个 `CONTEXT` 类型的指针作为参数，该结构体包含了处理器上下文信息。在函数内部，首先设置日志级别为 Critical，然后打印出标题 \"====== Fatal information: ======\"。接下来，依次打印出程序计数器、各个寄存器的值，以及一些特定的寄存器值。这些信息有助于定位程序崩溃的原因。
 ******************************************************************************/
// 定义一个静态函数，用于打印致命信息
static void print_fatal_info(CONTEXT *pctx)
{
    // 设置日志级别为 Critical，打印标题 "====== Fatal information: ======"
    zabbix_log(LOG_LEVEL_CRIT, "====== Fatal information: ======");

    // 打印程序计数器值，格式为 "0x%08lx"
    zabbix_log(LOG_LEVEL_CRIT, "Program counter: 0x%08lx", pctx->Rip);

    // 打印注册器信息，开头为 "=== Registers: ==="
    zabbix_log(LOG_LEVEL_CRIT, "=== Registers: ===");

    // 依次打印 r8 到 r15 寄存器的值
    print_register("r8", pctx->R8);
    print_register("r9", pctx->R9);
    print_register("r10", pctx->R10);
    print_register("r11", pctx->R11);
    print_register("r12", pctx->R12);
    print_register("r13", pctx->R13);
    print_register("r14", pctx->R14);
    print_register("r15", pctx->R15);

    // 打印 rdi、rsi、rbp 寄存器的值
    print_register("rdi", pctx->Rdi);
    print_register("rsi", pctx->Rsi);
    print_register("rbp", pctx->Rbp);

    // 打印 rbx、rdx、rax、rcx 寄存器的值
    print_register("rbx", pctx->Rbx);
    print_register("rdx", pctx->Rdx);
    print_register("rax", pctx->Rax);
    print_register("rcx", pctx->Rcx);

    // 打印 rsp、efl、csgsfs 寄存器的值
    print_register("rsp", pctx->Rsp);
    print_register("efl", pctx->EFlags);
    print_register("csgsfs", ZBX_LSHIFT(pctx->SegCs, 24) | ZBX_LSHIFT(pctx->SegGs, 16) | ZBX_LSHIFT(pctx->SegFs, 8));
}

	print_register("r15", pctx->R15);

	print_register("rdi", pctx->Rdi);
	print_register("rsi", pctx->Rsi);
	print_register("rbp", pctx->Rbp);

	print_register("rbx", pctx->Rbx);
	print_register("rdx", pctx->Rdx);
	print_register("rax", pctx->Rax);
	print_register("rcx", pctx->Rcx);

	print_register("rsp", pctx->Rsp);
	print_register("efl", pctx->EFlags);
	print_register("csgsfs", ZBX_LSHIFT(pctx->SegCs, 24) | ZBX_LSHIFT(pctx->SegGs, 16) | ZBX_LSHIFT(pctx->SegFs, 8));
}

#else

#define ZBX_IMAGE_FILE_MACHINE	IMAGE_FILE_MACHINE_I386

/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个名为 print_register 的静态函数，用于输出 register 寄存器的值和相关信息。接收两个参数，一个是字符串指针（name），表示寄存器的名称；另一个是无符号整数（value），表示寄存器的值。通过 zabbix_log 函数记录日志，日志级别为 CRIT（严重），格式化输出字符串，展示寄存器的值、名称和相关信息。
 ******************************************************************************/
// 定义一个名为 print_register 的静态函数，接收两个参数：一个字符串指针（name）和一个无符号整数（value）
/******************************************************************************
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个函数，该函数可以遍历程序的栈帧，并输出栈帧信息。这个函数接收一个`CONTEXT`类型的指针作为参数，该指针表示当前程序的运行状态。代码使用了Windows API函数`StackWalk64`和`Sym`系列函数来获取栈帧信息，并使用`zabbix_log`函数输出结果。整个代码块的功能是实现一个简单的堆栈回溯，以便于调试和分析程序的运行状态。
 ******************************************************************************/
static void print_backtrace(CONTEXT *pctx)
{
    // 定义变量
    SymGetLineFromAddrW64_func_t zbx_SymGetLineFromAddrW64 = NULL;
    SymFromAddr_func_t zbx_SymFromAddr = NULL;
    CONTEXT ctx, ctxcount;
    STACKFRAME64 s, scount;
    PSYMBOL_INFO pSym = NULL;
    HMODULE hModule;
    HANDLE hProcess, hThread;
    DWORD64 offset;
    wchar_t szProcessName[MAX_PATH];
    char *process_name = NULL, *process_path = NULL, *frame = NULL;
    size_t frame_alloc = 0, frame_offset;
    int nframes = 0;

    // 初始化变量
    ctx = *pctx;

    zabbix_log(LOG_LEVEL_CRIT, "=== Backtrace: ===");

    memset(&s, 0, sizeof(s));

    s.AddrPC.Mode = AddrModeFlat;
    s.AddrFrame.Mode = AddrModeFlat;
    s.AddrStack.Mode = AddrModeFlat;

    // 针对x64和x86架构分别设置PC、Frame和Stack的地址
#ifdef _M_X64
    s.AddrPC.Offset = ctx.Rip;
    s.AddrFrame.Offset = ctx.Rbp;
    s.AddrStack.Offset = ctx.Rsp;
#else
    s.AddrPC.Offset = ctx.Eip;
    s.AddrFrame.Offset = ctx.Ebp;
    s.AddrStack.Offset = ctx.Esp;
#endif
    hProcess = GetCurrentProcess();
    hThread = GetCurrentThread();

    // 获取程序文件名
    if (0 != GetModuleFileNameEx(hProcess, NULL, szProcessName, ARRSIZE(szProcessName)))
    {
        char *ptr;
        size_t path_alloc = 0, path_offset = 0;

        process_name = zbx_unicode_to_utf8(szProcessName);

        if (NULL != (ptr = strstr(process_name, progname)))
            zbx_strncpy_alloc(&process_path, &path_alloc, &path_offset, process_name, ptr - process_name);
    }

    // 获取DbgHelp.DLL模块句柄
    if (NULL != (hModule = GetModuleHandle(TEXT("DbgHelp.DLL"))))
    {
        zbx_SymGetLineFromAddrW64 = (SymGetLineFromAddrW64_func_t)GetProcAddress(hModule,
                                "SymGetLineFromAddr64");
        zbx_SymFromAddr = (SymFromAddr_func_t)GetProcAddress(hModule, "SymFromAddr");
    }

    // 初始化符号解析
    if (NULL != zbx_SymFromAddr || NULL != zbx_SymGetLineFromAddrW64)
    {
        SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES);

        if (FALSE != SymInitialize(hProcess, process_path, TRUE))
        {
            pSym = (PSYMBOL_INFO) zbx_malloc(NULL, sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
            memset(pSym, 0, sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
            pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
            pSym->MaxNameLen = MAX_SYM_NAME;
        }
    }

    // 初始化栈回溯
    scount = s;
    ctxcount = ctx;

    /* 获取帧数，ctxcount可能在StackWalk64()调用过程中被修改 */
    while (TRUE == StackWalk64(ZBX_IMAGE_FILE_MACHINE, hProcess, hThread, &scount, &ctxcount, NULL, NULL, NULL,
                             NULL))
    {
        if (0 == scount.AddrReturn.Offset)
            break;
        nframes++;
    }

    // 遍历栈帧并输出
    while (TRUE == StackWalk64(ZBX_IMAGE_FILE_MACHINE, hProcess, hThread, &s, &ctx, NULL, NULL, NULL, NULL))
    {
        frame_offset = 0;
        zbx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, "%d: %s", nframes--,
                        NULL == process_name ? "(unknown)" : process_name);

        if (NULL != pSym)
        {
            DWORD dwDisplacement;
            IMAGEHLP_LINE64 line = {sizeof(IMAGEHLP_LINE64)};

            zbx_chrcpy_alloc(&frame, &frame_alloc, &frame_offset, '(');
            if (NULL != zbx_SymFromAddr &&
                    TRUE == zbx_SymFromAddr(hProcess, s.AddrPC.Offset, &offset, pSym))
            {
                zbx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, "%s+0x%lx", pSym->Name, offset);
            }

            if (NULL != zbx_SymGetLineFromAddrW64 && TRUE == zbx_SymGetLineFromAddrW64(hProcess,
                        s.AddrPC.Offset, &dwDisplacement, &line))
            {
                zbx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, " %s:%d", line.FileName,
                                line.LineNumber);
            }
            zbx_chrcpy_alloc(&frame, &frame_alloc, &frame_offset, ')');
        }

        zabbix_log(LOG_LEVEL_CRIT, "%s [0x%lx]", frame, s.AddrPC.Offset);

        if (0 == s.AddrReturn.Offset)
            break;
    }

    // 清理符号解析
    SymCleanup(hProcess);

    // 释放内存
    zbx_free(frame);
    zbx_free(process_path);
    zbx_free(process_name);
    zbx_free(pSym);
}

#endif
	hProcess = GetCurrentProcess();
	hThread = GetCurrentThread();

	if (0 != GetModuleFileNameEx(hProcess, NULL, szProcessName, ARRSIZE(szProcessName)))
	{
		char	*ptr;
		size_t	path_alloc = 0, path_offset = 0;

		process_name = zbx_unicode_to_utf8(szProcessName);

		if (NULL != (ptr = strstr(process_name, progname)))
			zbx_strncpy_alloc(&process_path, &path_alloc, &path_offset, process_name, ptr - process_name);
	}

	if (NULL != (hModule = GetModuleHandle(TEXT("DbgHelp.DLL"))))
	{
		zbx_SymGetLineFromAddrW64 = (SymGetLineFromAddrW64_func_t)GetProcAddress(hModule,
				"SymGetLineFromAddr64");
		zbx_SymFromAddr = (SymFromAddr_func_t)GetProcAddress(hModule, "SymFromAddr");
	}

	if (NULL != zbx_SymFromAddr || NULL != zbx_SymGetLineFromAddrW64)
	{
		SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES);

		if (FALSE != SymInitialize(hProcess, process_path, TRUE))
		{
			pSym = (PSYMBOL_INFO) zbx_malloc(NULL, sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
			memset(pSym, 0, sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
			pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
			pSym->MaxNameLen = MAX_SYM_NAME;
		}
	}

	scount = s;
	ctxcount = ctx;

	/* get number of frames, ctxcount may be modified during StackWalk64() calls */
	while (TRUE == StackWalk64(ZBX_IMAGE_FILE_MACHINE, hProcess, hThread, &scount, &ctxcount, NULL, NULL, NULL,
			NULL))
	{
		if (0 == scount.AddrReturn.Offset)
			break;
		nframes++;
	}

	while (TRUE == StackWalk64(ZBX_IMAGE_FILE_MACHINE, hProcess, hThread, &s, &ctx, NULL, NULL, NULL, NULL))
	{
		frame_offset = 0;
		zbx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, "%d: %s", nframes--,
				NULL == process_name ? "(unknown)" : process_name);

		if (NULL != pSym)
		{
			DWORD		dwDisplacement;
			IMAGEHLP_LINE64	line = {sizeof(IMAGEHLP_LINE64)};

			zbx_chrcpy_alloc(&frame, &frame_alloc, &frame_offset, '(');
			if (NULL != zbx_SymFromAddr &&
					TRUE == zbx_SymFromAddr(hProcess, s.AddrPC.Offset, &offset, pSym))
			{
				zbx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, "%s+0x%lx", pSym->Name, offset);
			}

			if (NULL != zbx_SymGetLineFromAddrW64 && TRUE == zbx_SymGetLineFromAddrW64(hProcess,
					s.AddrPC.Offset, &dwDisplacement, &line))
			{
				zbx_snprintf_alloc(&frame, &frame_alloc, &frame_offset, " %s:%d", line.FileName,
						line.LineNumber);
			}
			zbx_chrcpy_alloc(&frame, &frame_alloc, &frame_offset, ')');
		}

		zabbix_log(LOG_LEVEL_CRIT, "%s [0x%lx]", frame, s.AddrPC.Offset);

		if (0 == s.AddrReturn.Offset)
			break;
	}

	SymCleanup(hProcess);

	zbx_free(frame);
	zbx_free(process_path);
	zbx_free(process_name);
	zbx_free(pSym);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是处理一个未处理的异常，记录日志并进行崩溃处理。具体来说，首先记录异常信息的日志，然后打印致命信息和堆栈跟踪，最后输出一条分割线。整个过程结束后，返回一个继续搜索异常处理程序的值。
 ******************************************************************************/
// 定义一个函数zbx_win_exception_filter，接收两个参数，一个是无符号整数code，另一个是结构体指针ep。
int zbx_win_exception_filter(unsigned int code, struct _EXCEPTION_POINTERS *ep)
{
    // 使用zabbix_log函数记录日志，日志级别为CRIT，表示异常信息。
    // 输出的内容为：未处理的异常代码%x在0x%p地址检测到。程序崩溃中...
    zabbix_log(LOG_LEVEL_CRIT, "Unhandled exception %x detected at 0x%p. Crashing ...", code,
                ep->ExceptionRecord->ExceptionAddress);

    // 调用print_fatal_info函数，传入ep->ContextRecord，打印致命信息。
    print_fatal_info(ep->ContextRecord);

    // 调用print_backtrace函数，传入ep->ContextRecord，打印堆栈跟踪。
    print_backtrace(ep->ContextRecord);

    // 使用zabbix_log函数记录日志，日志级别为CRIT，表示分割线。
    zabbix_log(LOG_LEVEL_CRIT, "================================");

    // 返回值EXCEPTION_CONTINUE_SEARCH，表示继续搜索异常处理程序。
    return EXCEPTION_CONTINUE_SEARCH;
}

