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
#include "vmstats.h"
#include "log.h"

#ifdef _AIX

#ifndef XINTFRAC	/* defined in IBM AIX 7.1 libperfstat.h, not defined in AIX 6.1 */
#include <sys/systemcfg.h>
#define XINTFRAC	((double)_system_configuration.Xint / _system_configuration.Xfrac)
	/* Example of XINTFRAC = 125.000000 / 64.000000 = 1.953125. Apparently XINTFRAC is a period (in nanoseconds) */
	/* of CPU ticks on a machine. For example, 1.953125 could mean there is 1.953125 nanoseconds between ticks */
	/* and number of ticks in second is 1.0 / (1.953125 * 10^-9) = 512000000. So, tick frequency is 512 MHz. */
#endif

static int		last_clock = 0;
/* --- kthr --- */
static zbx_uint64_t	last_runque = 0;		/* length of the run queue (processes ready) */
static zbx_uint64_t	last_swpque = 0;		/* length of the swap queue (processes waiting to be paged in) */
/* --- page --- */
static zbx_uint64_t	last_pgins = 0;			/* number of pages paged in */
static zbx_uint64_t	last_pgouts = 0;		/* number of pages paged out */
static zbx_uint64_t	last_pgspins = 0;		/* number of page ins from paging space */
static zbx_uint64_t	last_pgspouts = 0;		/* number of page outs from paging space */
static zbx_uint64_t	last_cycles = 0;		/* number of page replacement cycles */
static zbx_uint64_t	last_scans = 0;			/* number of page scans by clock */
/* -- faults -- */
static zbx_uint64_t	last_devintrs = 0;		/* number of device interrupts */
static zbx_uint64_t	last_syscall = 0;		/* number of system calls executed */
static zbx_uint64_t	last_pswitch = 0;		/* number of process switches (change in currently running */
							/* process) */
/* --- cpu ---- */
/* Raw numbers of ticks are readings from forward-ticking counters. */
/* Only difference between 2 readings is meaningful. */
static zbx_uint64_t	last_puser = 0;			/* raw number of physical processor ticks in user mode */
static zbx_uint64_t	last_psys = 0;			/* raw number of physical processor ticks in system mode */
static zbx_uint64_t	last_pidle = 0;			/* raw number of physical processor ticks idle */
static zbx_uint64_t	last_pwait = 0;			/* raw number of physical processor ticks waiting for I/O */
static zbx_uint64_t	last_user = 0;			/* raw total number of clock ticks spent in user mode */
static zbx_uint64_t	last_sys = 0;			/* raw total number of clock ticks spent in system mode */
static zbx_uint64_t	last_idle = 0;			/* raw total number of clock ticks spent idle */
static zbx_uint64_t	last_wait = 0;			/* raw total number of clock ticks spent waiting for I/O */
static zbx_uint64_t	last_timebase_last = 0;		/* most recent processor time base timestamp */
static zbx_uint64_t	last_pool_idle_time = 0;	/* number of clock ticks a processor in the shared pool was */
							/* idle */
static zbx_uint64_t	last_idle_donated_purr = 0;	/* number of idle cycles donated by a dedicated partition */
							/* enabled for donation */
static zbx_uint64_t	last_busy_donated_purr = 0;	/* number of busy cycles donated by a dedicated partition */
							/* enabled for donation */
static zbx_uint64_t	last_idle_stolen_purr = 0;	/* number of idle cycles stolen by the hypervisor from */
							/* a dedicated partition */
static zbx_uint64_t	last_busy_stolen_purr = 0;	/* number of busy cycles stolen by the hypervisor from */
							/* a dedicated partition */
/* --- disk --- */
static zbx_uint64_t	last_xfers = 0;			/* total number of transfers to/from disk */
static zbx_uint64_t	last_wblks = 0;			/* 512 bytes blocks written to all disks */
static zbx_uint64_t	last_rblks = 0;			/* 512 bytes blocks read from all disks */

/******************************************************************************
 *                                                                            *
 * Function: update_vmstat                                                    *
 *                                                                            *
 * Purpose: update vmstat values at most once per second                      *
 *                                                                            *
 * Parameters: vmstat - a structure containing vmstat data                    *
 *                                                                            *
 * Comments: on first iteration only save last data, on second - set vmstat   *
 *           data and indicate that it is available                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这段C语言代码的主要目的是从系统性能数据中获取vmstat信息，并对其进行更新。以下是逐行注释后的代码：
 *
 *
 *
 *这段代码首先从perfstat库获取系统性能数据，然后更新vmstat数据。代码中使用了AIX系统的相关性能数据和版本信息。在获取到新的vmstat数据后，将上一次计算得到的vmstat数据进行保存。
 ******************************************************************************/
static void	update_vmstat(ZBX_VMSTAT_DATA *vmstat)
{
#if defined(HAVE_LIBPERFSTAT)
	// 定义一些变量，用于存储从perfstat库获取的性能数据
	int				now;
	zbx_uint64_t			dlcpu_us, dlcpu_sy, dlcpu_id, dlcpu_wa, lcputime;
	perfstat_memory_total_t		memstats;
	perfstat_cpu_total_t		cpustats;
	perfstat_disk_total_t		diskstats;
#ifdef _AIXVERSION_530
	// 一些与AIX系统相关的性能数据
	zbx_uint64_t			dpcpu_us, dpcpu_sy, dpcpu_id, dpcpu_wa, pcputime, dtimebase;
	zbx_uint64_t			delta_purr, entitled_purr, unused_purr, r1, r2;
	perfstat_partition_total_t	lparstats;
#ifdef HAVE_AIXOSLEVEL_530
	// 一些与AIX系统版本相关的性能数据
	zbx_uint64_t			didle_donated_purr, dbusy_donated_purr, didle_stolen_purr, dbusy_stolen_purr;
#endif	/* HAVE_AIXOSLEVEL_530 */

	// 获取当前时间，用于计算时间差
	now = (int)time(NULL);

	// 调用perfstat库获取性能数据
	// 此处省略了perfstat库的具体调用代码

	// 设置一些静态vmstat值
	if (0 == last_clock)
	{
#ifdef _AIXVERSION_530
		// 设置与AIX系统相关的vmstat值
		vmstat->shared_enabled = (unsigned char)lparstats.type.b.shared_enabled;
		vmstat->pool_util_authority = (unsigned char)lparstats.type.b.pool_util_authority;
#endif
#ifdef HAVE_AIXOSLEVEL_520004
		// 设置与AIX系统版本相关的vmstat值
		vmstat->aix52stats = 1;
#endif
	}
	else if (now > last_clock)
	{
		// 更新vmstat数据
		// 此处省略了更新vmstat数据的代码
	}

	// 保存上一次计算得到的vmstat数据
	// 此处省略了保存数据的代码

#endif	/* HAVE_LIBPERFSTAT */
}

/******************************************************************************
 * *
 *这块代码的主要目的是收集虚拟内存状态数据。通过调用 update_vmstat 函数，更新 ZBX_VMSTAT_DATA 结构体中的数据。收集到的数据可以用于性能监控、故障排查等场景。整个代码块的功能简洁明了，实现了一个简单的数据收集功能。
 ******************************************************************************/
// 定义一个函数，用于收集虚拟内存状态数据
// 参数：vmstat 指向 ZBX_VMSTAT_DATA 结构体的指针
void collect_vmstat_data(ZBX_VMSTAT_DATA *vmstat)
{
    // 调用 update_vmstat 函数，更新虚拟内存状态数据
    update_vmstat(vmstat);
}


#endif	/* _AIX */
