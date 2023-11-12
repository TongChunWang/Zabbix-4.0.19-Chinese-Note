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

#include "config.h"

#ifdef HAVE_SIGNAL_H
#	if !defined(_GNU_SOURCE)
#		define _GNU_SOURCE	/* required for getting at program counter */
#	endif
#	include <signal.h>
#endif

#ifdef HAVE_SYS_UCONTEXT_H
#	if !defined(_GNU_SOURCE)
#		define _GNU_SOURCE	/* required for getting at program counter */
#	endif
#	include <sys/ucontext.h>
#endif

#ifdef	HAVE_EXECINFO_H
#	include <execinfo.h>
#endif

#include "common.h"
#include "log.h"

#include "fatal.h"
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个函数，接收一个整数参数sig，返回对应信号的名称字符串。
 * 这里使用了switch语句，列举了常见的信号，当sig匹配某个信号时，直接返回对应的名称。
 * 如果没有匹配的信号，返回"unknown"。
 * 注意：这个函数可能不是通用的，因为不同平台上的信号支持情况不同。
 */
const char *get_signal_name(int sig)
{
	/* 可以使用strsignal()或sys_siglist[]获取信号名称，但这些方法并不通用 */

	/* 不是所有POSIX信号在所有平台上都可用，所以我们只列出我们自己处理的手信号 */

	switch (sig)
	{
		case SIGALRM:	/* 匹配SIGALRM信号，返回名称"SIGALRM" */
			return "SIGALRM";
		case SIGILL:	/* 匹配SIGILL信号，返回名称"SIGILL" */
			return "SIGILL";
		case SIGFPE:	/* 匹配SIGFPE信号，返回名称"SIGFPE" */
			return "SIGFPE";
		case SIGSEGV:	/* 匹配SIGSEGV信号，返回名称"SIGSEGV" */
			return "SIGSEGV";
		case SIGBUS:	/* 匹配SIGBUS信号，返回名称"SIGBUS" */
			return "SIGBUS";
		case SIGQUIT:	/* 匹配SIGQUIT信号，返回名称"SIGQUIT" */
			return "SIGQUIT";
		case SIGHUP:	/* 匹配SIGHUP信号，返回名称"SIGHUP" */
			return "SIGHUP";
		case SIGINT:	/* 匹配SIGINT信号，返回名称"SIGINT" */
			return "SIGINT";
		case SIGTERM:	/* 匹配SIGTERM信号，返回名称"SIGTERM" */
			return "SIGTERM";
		case SIGPIPE:	/* 匹配SIGPIPE信号，返回名称"SIGPIPE" */
			return "SIGPIPE";
		case SIGUSR1:	/* 匹配SIGUSR1信号，返回名称"SIGUSR1" */
			return "SIGUSR1";
		case SIGUSR2:	/* 匹配SIGUSR2信号，返回名称"SIGUSR2" */
			return "SIGUSR2";
		default:	/* 没有匹配的信号，返回"unknown" */
			return "unknown";
	}
}


#if defined(HAVE_SYS_UCONTEXT_H) && (defined(REG_EIP) || defined(REG_RIP))

/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个获取寄存器名称的函数，传入一个整数参数reg，返回对应寄存器的字符串名称。
 * 这里主要目的是根据不同的处理器架构，返回对应的寄存器名称。
 * 代码块分为三个部分：i386、x86_64和未知处理器。
 */
static const char *get_register_name(int reg)
{
	/* 切换到不同的处理器架构 */
	switch (reg)
	{
		/* i386处理器寄存器 */
		case REG_GS:
		case REG_FS:
		case REG_ES:
		case REG_DS:
		case REG_EDI:
		case REG_ESI:
		case REG_EBP:
		case REG_ESP:
		case REG_EBX:
		case REG_EDX:
		case REG_ECX:
		case REG_EAX:
		case REG_EIP:
		case REG_CS:
		case REG_UESP:
		case REG_SS:
			/* x86_64处理器寄存器 */
		case REG_R8:
		case REG_R9:
		case REG_R10:
		case REG_R11:
		case REG_R12:
		case REG_R13:
		case REG_R14:
		case REG_R15:
		case REG_RDI:
		case REG_RSI:
		case REG_RBP:
		case REG_RBX:
		case REG_RDX:
		case REG_RAX:
		case REG_RCX:
		case REG_RSP:
		case REG_RIP:
		case REG_CSGSFS:
		case REG_OLDMASK:
		case REG_CR2:
			/* 未知处理器寄存器 */
		default:
			return "unknown";
	}
}


#endif	/* defined(HAVE_SYS_UCONTEXT_H) && (defined(REG_EIP) || defined(REG_RIP)) */

void	zbx_backtrace(void)
{
#	define	ZBX_BACKTRACE_SIZE	60
#ifdef	HAVE_EXECINFO_H
	char	**bcktrc_syms;
	void	*bcktrc[ZBX_BACKTRACE_SIZE];
	int	bcktrc_sz, i;

	zabbix_log(LOG_LEVEL_CRIT, "=== Backtrace: ===");

	bcktrc_sz = backtrace(bcktrc, ZBX_BACKTRACE_SIZE);
	bcktrc_syms = backtrace_symbols(bcktrc, bcktrc_sz);

	if (NULL == bcktrc_syms)
	{
		zabbix_log(LOG_LEVEL_CRIT, "error in backtrace_symbols(): %s", zbx_strerror(errno));

		for (i = 0; i < bcktrc_sz; i++)
			zabbix_log(LOG_LEVEL_CRIT, "%d: %p", bcktrc_sz - i - 1, bcktrc[i]);
	}
	else
	{
		for (i = 0; i < bcktrc_sz; i++)
			zabbix_log(LOG_LEVEL_CRIT, "%d: %s", bcktrc_sz - i - 1, bcktrc_syms[i]);

		zbx_free(bcktrc_syms);
	}
#else
	zabbix_log(LOG_LEVEL_CRIT, "backtrace is not available for this platform");
#endif	/* HAVE_EXECINFO_H */
}

void	zbx_log_fatal_info(void *context, unsigned int flags)
{
#ifdef	HAVE_SYS_UCONTEXT_H

#if defined(REG_EIP) || defined(REG_RIP)
	ucontext_t	*uctx = (ucontext_t *)context;
#endif

	/* look for GET_PC() macro in sigcontextinfo.h files */
	/* of glibc if you wish to add more CPU architectures */

#	if	defined(REG_EIP)	/* i386 */

#		define ZBX_GET_REG(uctx, reg)	(uctx)->uc_mcontext.gregs[reg]
#		define ZBX_GET_PC(uctx)		ZBX_GET_REG(uctx, REG_EIP)

#	elif	defined(REG_RIP)	/* x86_64 */

#		define ZBX_GET_REG(uctx, reg)	(uctx)->uc_mcontext.gregs[reg]
#		define ZBX_GET_PC(uctx)		ZBX_GET_REG(uctx, REG_RIP)

#	endif

#endif	/* HAVE_SYS_UCONTEXT_H */
	int	i;
	FILE	*fd;

	zabbix_log(LOG_LEVEL_CRIT, "====== Fatal information: ======");

	if (0 != (flags & ZBX_FATAL_LOG_PC_REG_SF))
	{
#ifdef	HAVE_SYS_UCONTEXT_H

#ifdef	ZBX_GET_PC
		/* On 64-bit GNU/Linux ZBX_GET_PC() returns 'greg_t' defined as 'long long int' (8 bytes). */
		/* On 32-bit GNU/Linux it is defined as 'int' (4 bytes). To print registers in a common way we print */
		/* them as 'long int' or 'unsigned long int' which is 8 bytes on 64-bit GNU/Linux and 4 bytes on */
		/* 32-bit system. */

		zabbix_log(LOG_LEVEL_CRIT, "Program counter: %p", (void *)(ZBX_GET_PC(uctx)));
		zabbix_log(LOG_LEVEL_CRIT, "=== Registers: ===");

		for (i = 0; i < NGREG; i++)
		{
			zabbix_log(LOG_LEVEL_CRIT, "%-7s = %16lx = %20lu = %20ld", get_register_name(i),
					(unsigned long int)(ZBX_GET_REG(uctx, i)),
					(unsigned long int)(ZBX_GET_REG(uctx, i)),
/******************************************************************************
 * *
 *整个代码块的主要目的是获取并输出进程的回溯信息。首先定义了一个常量ZBX_BACKTRACE_SIZE表示回溯栈的最大长度。然后判断系统是否支持execinfo.h头文件，如果支持，则调用backtrace()函数获取进程的回溯信息，并调用backtrace_symbols()函数获取回溯信息的符号（函数名）。如果符号获取失败，则输出错误日志和回溯信息的地址。如果符号获取成功，则输出回溯信息的函数名。最后，释放符号数组占用的内存。如果不支持execinfo.h头文件，则输出一条提示信息。
 ******************************************************************************/
void	zbx_backtrace(void)
{
    // 定义一个常量，表示回溯栈的最大长度
#define	ZBX_BACKTRACE_SIZE	60

    // 判断系统是否支持execinfo.h头文件，用于获取进程的回溯信息
#ifdef	HAVE_EXECINFO_H
    char	**bcktrc_syms;
    void	*bcktrc[ZBX_BACKTRACE_SIZE];
    int	bcktrc_sz, i;

    // 记录日志，表示开始获取回溯信息
    zabbix_log(LOG_LEVEL_CRIT, "=== Backtrace: ===");

    // 获取进程的回溯信息，存储在bcktrc数组中，最大长度为ZBX_BACKTRACE_SIZE
    bcktrc_sz = backtrace(bcktrc, ZBX_BACKTRACE_SIZE);

    // 获取回溯信息的符号（函数名），存储在bcktrc_syms数组中
    bcktrc_syms = backtrace_symbols(bcktrc, bcktrc_sz);

    // 检查bcktrc_syms是否为空，如果为空，说明获取符号失败
    if (NULL == bcktrc_syms)
    {
        // 记录日志，表示获取符号失败
        zabbix_log(LOG_LEVEL_CRIT, "error in backtrace_symbols(): %s", zbx_strerror(errno));

        // 遍历回溯信息，输出地址
        for (i = 0; i < bcktrc_sz; i++)
/******************************************************************************
 * *
 *这个代码块的主要目的是在C语言程序中记录 fatal 错误信息。它首先根据不同的架构（i386或x86_64）获取寄存器值，并输出程序计数器（PC）和其他寄存器的值。接下来，它输出栈帧（EBP）的相关信息，包括返回地址和局部变量的值。最后，它根据用户提供的标志位输出内存映射信息。整个代码块以详细的注释说明了各个步骤的操作和意义，方便其他开发人员理解和使用。
 ******************************************************************************/
void zbx_log_fatal_info(void *context, unsigned int flags)
{
    // 定义宏
    #ifdef HAVE_SYS_UCONTEXT_H

    // 定义REG_EIP或REG_RIP为i386或x86_64架构的寄存器名称
    #if defined(REG_EIP) || defined(REG_RIP)
        ucontext_t *uctx = (ucontext_t *)context;
    #endif

    // 查找GET_PC()宏在sigcontextinfo.h文件中的定义
    // 在glibc中添加更多的CPU架构

    #if defined(REG_EIP)
        // 为i386架构定义ZBX_GET_REG和ZBX_GET_PC宏
        #define ZBX_GET_REG(uctx, reg)	(uctx)->uc_mcontext.gregs[reg]
        #define ZBX_GET_PC(uctx)		ZBX_GET_REG(uctx, REG_EIP)

    #elif defined(REG_RIP)
        // 为x86_64架构定义ZBX_GET_REG和ZBX_GET_PC宏
        #define ZBX_GET_REG(uctx, reg)	(uctx)->uc_mcontext.gregs[reg]
        #define ZBX_GET_PC(uctx)		ZBX_GET_REG(uctx, REG_RIP)

    #endif

    // 定义变量
    int i;
    FILE *fd;

    // 输出日志
    zabbix_log(LOG_LEVEL_CRIT, "====== Fatal information: ======");

    // 判断flags中是否包含ZBX_FATAL_LOG_PC_REG_SF标志位
    if (0 != (flags & ZBX_FATAL_LOG_PC_REG_SF))
    {
        // 判断是否包含HAVE_SYS_UCONTEXT_H和ZBX_GET_PC宏
        #ifdef HAVE_SYS_UCONTEXT_H
            #ifdef ZBX_GET_PC
                // 输出程序计数器（PC）
                zabbix_log(LOG_LEVEL_CRIT, "Program counter: %p", (void *)(ZBX_GET_PC(uctx)));

                // 输出所有寄存器值
                for (i = 0; i < NGREG; i++)
                {
                    zabbix_log(LOG_LEVEL_CRIT, "%-7s = %16lx = %20lu = %20ld", get_register_name(i),
                               (unsigned long int)(ZBX_GET_REG(uctx, i)),
                               (unsigned long int)(ZBX_GET_REG(uctx, i)),
                               (long int)(ZBX_GET_REG(uctx, i)));
                }

                // 输出栈帧（EBP）
                for (i = 16; i >= 2; i--)
                {
                    unsigned int offset = (unsigned int)i * ZBX_PTR_SIZE;

                    zabbix_log(LOG_LEVEL_CRIT, "+0x%02x(%%ebp) = ebp + %2d = %08x = %10u = %11d%s",
                               offset, (int)offset,
                               *(unsigned int *)((void *)ZBX_GET_REG(uctx, REG_EBP) + offset),
                               *(unsigned int *)((void *)ZBX_GET_REG(uctx, REG_EBP) + offset),
                               *(int *)((void *)ZBX_GET_REG(uctx, REG_EBP) + offset),
                               i == 2 ? " <--- call arguments" : "");
                }

                // 输出返回地址
                zabbix_log(LOG_LEVEL_CRIT, "+0x%02x(%%ebp) = ebp      = %08x%28s<--- return address",
                           ZBX_PTR_SIZE, (int)ZBX_PTR_SIZE,
                           *(unsigned int *)((void *)ZBX_GET_REG(uctx, REG_EBP)), "");

                // 输出保存的EBP值
                zabbix_log(LOG_LEVEL_CRIT, "     (%%ebp) = ebp      = %08x%28s<--- saved ebp value",
                           *(unsigned int *)((void *)ZBX_GET_REG(uctx, REG_EBP)), "");

                // 输出局部变量和栈帧
                for (i = 1; i <= 16; i++)
                {
                    unsigned int offset = (unsigned int)i * ZBX_PTR_SIZE;

                    zabbix_log(LOG_LEVEL_CRIT, "-0x%02x(%%ebp) = ebp - %2d = %08x = %10u = %11d%s",
                               offset, (int)offset,
                               *(unsigned int *)((void *)ZBX_GET_REG(uctx, REG_EBP) - offset),
                               *(unsigned int *)((void *)ZBX_GET_REG(uctx, REG_EBP) - offset),
                               *(int *)((void *)ZBX_GET_REG(uctx, REG_EBP) - offset),
                               i == 1 ? " <--- local variables" : "");
                }
            #endif /* ZBX_GET_PC */
        #endif /* HAVE_SYS_UCONTEXT_H */
    }

    // 判断是否包含ZBX_FATAL_LOG_BACKTRACE标志位
    if (0 != (flags & ZBX_FATAL_LOG_BACKTRACE))
        zbx_backtrace();

    // 判断是否包含ZBX_FATAL_LOG_MEM_MAP标志位
    if (0 != (flags & ZBX_FATAL_LOG_MEM_MAP))
    {
        // 打开/proc/self/maps文件并读取内容
        if (NULL != (fd = fopen("/proc/self/maps", "r")))
        {
            char line[1024];

            while (NULL != fgets(line, sizeof(line), fd))
            {
                if (line[0] != '\0')
                    line[strlen(line) - 1] = '\0'; /* remove trailing '\
' */

                zabbix_log(LOG_LEVEL_CRIT, "%s", line);
            }

            zbx_fclose(fd);
        }
        else
            zabbix_log(LOG_LEVEL_CRIT, "memory map not available for this platform");
    }

    // 输出提示信息
    zabbix_log(LOG_LEVEL_CRIT, "================================");
    zabbix_log(LOG_LEVEL_CRIT, "Please consider attaching a disassembly listing to your bug report.");
    zabbix_log(LOG_LEVEL_CRIT, "This listing can be produced with, e.g., objdump -DSswx %s.", progname);

    // 输出结束
    zabbix_log(LOG_LEVEL_CRIT, "================================");
}

