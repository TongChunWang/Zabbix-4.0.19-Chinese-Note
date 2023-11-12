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

/* Solaris. */
#if !defined(HAVE_SYSINFO_FREESWAP)
#ifdef HAVE_SYS_SWAP_SWAPTABLE
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统交换区的相关信息，如总的交换区条目数、已使用的交换区大小和空闲的交换区大小。输出结果存储在total和fr指针指向的内存区域。
 ******************************************************************************/
static void get_swapinfo(double *total, double *fr)
{
	// 定义变量，用于存储交换区信息
	int cnt, i, page_size;
	/* 支持大于2Gb的内存 */
	/* int t, f; */
	double t, f;
	struct swaptable *swt;
	struct swapent *ste;
	static char path[256];

	// 获取交换区条目的总数
	cnt = swapctl(SC_GETNSWP, 0);

	// 分配足够的空间来容纳交换区条目及其相关信息
	swt = (struct swaptable *)malloc(sizeof(int) +
		cnt * sizeof(struct swapent));

	// 如果分配内存失败，设置total和fr为0，并返回
	if (swt == NULL)
	{
		*total = 0;
		*fr = 0;
		return;
	}
	swt->swt_n = cnt;

	// 填充ste_path指针：我们不关心路径，所以将它们全部指向同一个缓冲区
	ste = &(swt->swt_ent[0]);
	i = cnt;
	while (--i >= 0)
	{
		ste++->ste_path = path;
	}

	// 获取所有交换区信息
	swapctl(SC_LIST, swt);

	// 遍历结构体并累加各个字段之和
	t = f = 0;
	ste = &(swt->swt_ent[0]);
	i = cnt;
	while (--i >= 0)
	{
		// 不计入正在删除的槽位
		if (!(ste->ste_flags & ST_INDEL) &&
		!(ste->ste_flags & ST_DOINGDEL))
		{
			t += ste->ste_pages;
			f += ste->ste_free;
		}
		ste++;
	}

	// 获取页面大小
	page_size = getpagesize();

	// 填充结果
	*total = page_size * t;
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统交换空间的使用情况，并根据不同的系统类型（Solaris和非Solaris系统）使用不同的方法进行获取。在Solaris系统中，使用sysinfo函数获取交换空间信息；在非Solaris系统中，使用sys_swap_swaptable函数获取交换空间信息。最后，将获取到的交换空间使用率设置为结果并返回。
 ******************************************************************************/
static int	SYSTEM_SWAP_USED(AGENT_RESULT *result)
{
    // 定义一个静态函数，用于获取系统交换空间的使用情况

#ifdef HAVE_SYSINFO_FREESWAP
    // 如果系统支持sysinfo函数并且有freeswap参数，则使用这种方式获取交换空间使用情况
    struct sysinfo	info;

    if (0 == sysinfo(&info))
    {
        // 调用sysinfo函数获取系统信息，如果成功则继续执行

#ifdef HAVE_SYSINFO_MEM_UNIT
        // 如果有mem_unit参数，则使用ui64类型结果
        SET_UI64_RESULT(result, ((zbx_uint64_t)info.totalswap - (zbx_uint64_t)info.freeswap) *
                        (zbx_uint64_t)info.mem_unit);
#else
        // 如果没有mem_unit参数，则直接计算交换空间使用率
        SET_UI64_RESULT(result, info.totalswap - info.freeswap);
#endif
        return SYSINFO_RET_OK;
    }
    else
        // 如果没有获取到系统信息，则返回错误
        return SYSINFO_RET_FAIL;

    /* Solaris */
#else
    // 如果系统不支持sysinfo函数，但是支持sys_swap_swaptable函数
#ifdef HAVE_SYS_SWAP_SWAPTABLE
    double	swaptotal, swapfree;

    // 调用get_swapinfo函数获取交换空间信息
    get_swapinfo(&swaptotal, &swapfree);

    // 计算交换空间使用率并设置结果
    SET_UI64_RESULT(result, swaptotal - swapfree);
    return SYSINFO_RET_OK;
#else
    // 如果系统既不支持sysinfo函数，也不支持get_swapinfo函数，则返回失败
    return SYSINFO_RET_FAIL;
#endif
#endif
}

	SET_UI64_RESULT(result, swaptotal - swapfree);
	return SYSINFO_RET_OK;
#else
	return SYSINFO_RET_FAIL;
#endif
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统内存中的免费空间（free space）并将其存储在 result 指针指向的结构体中。根据不同的操作系统，使用相应的系统函数来获取免费空间。在 Solaris 系统中，使用 sysinfo 函数；在其他系统中，使用 get_swapinfo 函数。最后，将结果存储在 result 指针指向的结构体中，并返回 SYSINFO_RET_OK 表示成功，或返回 SYSINFO_RET_FAIL 表示失败。
 ******************************************************************************/
/* 定义一个名为 SYSTEM_SWAP_FREE 的静态函数，接收一个 AGENT_RESULT 类型的指针作为参数 */
static int	SYSTEM_SWAP_FREE(AGENT_RESULT *result)
{
	/* 定义一个名为 info 的 struct sysinfo 结构体变量 */
	struct sysinfo info;

	/* 判断 sysinfo 函数调用是否成功，如果成功则继续执行 */
	if (0 == sysinfo(&info))
	{
		/* 判断是否支持 HAVE_SYSINFO_MEM_UNIT 宏，如果支持，则计算 free_space */
#ifdef HAVE_SYSINFO_MEM_UNIT
		zbx_uint64_t free_space = (zbx_uint64_t)info.freeswap * (zbx_uint64_t)info.mem_unit;
		SET_UI64_RESULT(result, free_space);
#else
		SET_UI64_RESULT(result, info.freeswap);
#endif
		/* 返回 SYSINFO_RET_OK，表示获取成功 */
		return SYSINFO_RET_OK;
	}
	else
		/* 返回 SYSINFO_RET_FAIL，表示获取失败 */
		return SYSINFO_RET_FAIL;
}

/* 以下注释为 Solaris 系统专用，表示获取 swapinfo 的代码块 */
/* 如果系统支持 HAVE_SYSINFO_MEM_UNIT 宏，则计算 free_space */
#ifdef HAVE_SYSINFO_MEM_UNIT
zbx_uint64_t free_space = (zbx_uint64_t)info.freeswap * (zbx_uint64_t)info.mem_unit;
SET_UI64_RESULT(result, free_space);
#else
/* 如果不支持 HAVE_SYSINFO_MEM_UNIT 宏，则直接使用 info.freeswap */
SET_UI64_RESULT(result, info.freeswap);
#endif


/******************************************************************************
 * *
 *整个代码块的主要目的是计算并返回系统的交换空间总量。根据不同的操作系统，使用不同的方法获取交换空间总量。如果成功获取到交换空间总量，将其设置为结果变量 result 的值，并返回 SYSINFO_RET_OK；如果失败，返回 SYSINFO_RET_FAIL。
 ******************************************************************************/
static int	SYSTEM_SWAP_TOTAL(AGENT_RESULT *result)
{
    // 定义一个名为 SYSTEM_SWAP_TOTAL 的静态函数，接收一个 AGENT_RESULT 类型的指针作为参数

#ifdef HAVE_SYSINFO_TOTALSWAP
    // 如果定义了 HAVE_SYSINFO_TOTALSWAP 符号，则执行以下代码
    struct sysinfo info; // 定义一个 struct sysinfo 类型的变量 info

    if (0 == sysinfo(&info)) // 如果调用 sysinfo 函数成功
    {
        // 定义了 HAVE_SYSINFO_MEM_UNIT 符号，则执行以下代码
#ifdef HAVE_SYSINFO_MEM_UNIT
            SET_UI64_RESULT(result, (zbx_uint64_t)info.totalswap * (zbx_uint64_t)info.mem_unit); // 设置结果变量 result 的值为 info.totalswap 乘以 info.mem_unit
#else
            SET_UI64_RESULT(result, info.totalswap); // 设置结果变量 result 的值为 info.totalswap
#endif
            return SYSINFO_RET_OK; // 返回 SYSINFO_RET_OK，表示成功
    }
    else
        return SYSINFO_RET_FAIL; // 返回 SYSINFO_RET_FAIL，表示失败
/* Solaris */
#else
    // 如果没有定义 HAVE_SYSINFO_TOTALSWAP 符号，则执行以下代码
#ifdef HAVE_SYS_SWAP_SWAPTABLE
        double swaptotal,swapfree; // 定义两个 double 类型的变量 swaptotal 和 swapfree

        get_swapinfo(&swaptotal,&swapfree); // 调用 get_swapinfo 函数获取交换空间信息

        SET_UI64_RESULT(result, swaptotal); // 设置结果变量 result 的值为 swaptotal
        return SYSINFO_RET_OK; // 返回 SYSINFO_RET_OK，表示成功
#else
        return SYSINFO_RET_FAIL; // 返回 SYSINFO_RET_FAIL，表示失败
#endif
#endif
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算交换区的空闲占比。首先，定义两个zbx_uint64_t类型的变量tot_val和free_val，分别用于存储交换区的总大小和空闲大小。然后，通过调用SYSTEM_SWAP_TOTAL和SYSTEM_SWAP_FREE函数获取交换区的总大小和空闲大小，并将结果存储在临时变量result_tmp中。接下来，检查交换区是否存在除以零的情况，如果存在，释放result_tmp变量并返回SYSINFO_RET_FAIL。最后，计算空闲占比，并将结果存储在result变量中，表示函数执行成功。
 ******************************************************************************/
static int	SYSTEM_SWAP_PFREE(AGENT_RESULT *result)
{
	/* 定义一个AGENT_RESULT类型的临时变量result_tmp，用于存储交换区的信息 */
	AGENT_RESULT	result_tmp;
	/* 定义两个zbx_uint64_t类型的变量tot_val和free_val，分别用于存储交换区的总大小和空闲大小 */
	zbx_uint64_t	tot_val = 0;
	zbx_uint64_t	free_val = 0;

	/* 初始化result_tmp变量，为后续操作做准备 */
	init_result(&result_tmp);

	/* 调用SYSTEM_SWAP_TOTAL函数获取交换区的总大小，并将结果存储在result_tmp变量中 */
	if (SYSINFO_RET_OK != SYSTEM_SWAP_TOTAL(&result_tmp) || !(result_tmp.type & AR_UINT64))
		/* 如果获取交换区总大小失败，或者result_tmp变量类型不是zbx_uint64_t，返回SYSINFO_RET_FAIL */
		return SYSINFO_RET_FAIL;
	/* 将result_tmp变量中的zbx_uint64_t值赋给tot_val变量 */
/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统交换空间的利用率，并将结果存储在result结构体中。具体步骤如下：
 *
 *1. 定义一个临时结果结构体result_tmp，用于存储交换空间的相关信息。
 *2. 初始化result_tmp。
 *3. 调用SYSTEM_SWAP_TOTAL函数获取交换空间的总量，并将结果存储在result_tmp中。
 *4. 检查result_tmp中的总量是否为0，如果是，则释放result_tmp内存，并返回SYSINFO_RET_FAIL。
 *5. 调用SYSTEM_SWAP_FREE函数获取交换空间的空闲量，并将结果存储在result_tmp中。
 *6. 释放result_tmp内存。
 *7. 计算交换空间的利用率，即将空闲量占总量的比例，并将结果存储在result中。
 *8. 返回SYSINFO_RET_OK，表示计算成功。
 ******************************************************************************/
// 定义一个静态函数，用于计算系统交换空间的利用率
static int SYSTEM_SWAP_PUSED(AGENT_RESULT *result)
{
    // 定义一个临时结果结构体，用于存储交换空间的相关信息
    AGENT_RESULT	result_tmp;
    // 定义两个zbx_uint64_t类型的变量，分别用于存储交换空间的总量和空闲量
    zbx_uint64_t	tot_val = 0;
    zbx_uint64_t	free_val = 0;

    // 初始化临时结果结构体
    init_result(&result_tmp);

    // 调用SYSTEM_SWAP_TOTAL函数获取交换空间的总量，并将结果存储在result_tmp中
    if (SYSINFO_RET_OK != SYSTEM_SWAP_TOTAL(&result_tmp) || !(result_tmp.type & AR_UINT64))
        // 如果获取总量失败或result_tmp中的类型不是zbx_uint64，返回SYSINFO_RET_FAIL
        return SYSINFO_RET_FAIL;
    tot_val = result_tmp.ui64;

    /* 检查是否存在除以零的情况 */
    if (0 == tot_val)
    {
        // 如果总量为0，释放result_tmp内存，并返回SYSINFO_RET_FAIL
        free_result(&result_tmp);
        return SYSINFO_RET_FAIL;
    }

    // 调用SYSTEM_SWAP_FREE函数获取交换空间的空闲量，并将结果存储在result_tmp中
    if (SYSINFO_RET_OK != SYSTEM_SWAP_FREE(&result_tmp) || !(result_tmp.type & AR_UINT64))
        // 如果获取空闲量失败或result_tmp中的类型不是zbx_uint64，返回SYSINFO_RET_FAIL
        return SYSINFO_RET_FAIL;
    free_val = result_tmp.ui64;

    // 释放result_tmp内存
    free_result(&result_tmp);

    // 计算交换空间的利用率，并将结果存储在result中
    SET_DBL_RESULT(result, 100.0 - (100.0 * (double)free_val) / (double)tot_val);

    // 返回SYSINFO_RET_OK，表示计算成功
    return SYSINFO_RET_OK;
}

{
	AGENT_RESULT	result_tmp;
	zbx_uint64_t	tot_val = 0;
	zbx_uint64_t	free_val = 0;

	init_result(&result_tmp);

	if (SYSINFO_RET_OK != SYSTEM_SWAP_TOTAL(&result_tmp) || !(result_tmp.type & AR_UINT64))
		return SYSINFO_RET_FAIL;
	tot_val = result_tmp.ui64;

	/* Check for division by zero */
	if (0 == tot_val)
	{
		free_result(&result_tmp);
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_OK != SYSTEM_SWAP_FREE(&result_tmp) || !(result_tmp.type & AR_UINT64))
		return SYSINFO_RET_FAIL;
	free_val = result_tmp.ui64;

	free_result(&result_tmp);

	SET_DBL_RESULT(result, 100.0 - (100.0 * (double)free_val) / (double)tot_val);

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个请求结构体（包含交换设备路径和模式参数）和一个结果结构体，根据模式参数的不同，调用相应的函数获取交换空间的相关信息（如自由空间、已用空间、总空间、物理自由空间和物理已用空间），并将结果存储在结果结构体中，最后返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，用于处理系统交换空间的相关操作，传入参数为请求结构体指针和结果结构体指针
int SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针，分别用于存储交换设备路径和模式参数
	char *swapdev, *mode;
	// 定义一个整型变量，用于存储函数返回值
	int ret = SYSINFO_RET_FAIL;

	// 检查传入的请求结构体中的参数数量，如果大于2，则直接返回失败
	if (2 < request->nparam)
		return SYSINFO_RET_FAIL;

	// 从请求结构体中获取交换设备路径参数，存储到swapdev指针中
	swapdev = get_rparam(request, 0);
	// 从请求结构体中获取模式参数，存储到mode指针中
	mode = get_rparam(request, 1);

	// 检查交换设备路径是否不为空，且不是"all"字符串，如果是，则直接返回失败
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
		return SYSINFO_RET_FAIL;

	// 检查模式参数是否为空，或者等于"free"字符串，如果是，则调用SYSTEM_SWAP_FREE函数获取自由空间信息，并返回成功
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "free"))
		ret = SYSTEM_SWAP_FREE(result);
	// 如果模式参数等于"used"字符串，则调用SYSTEM_SWAP_USED函数获取已用空间信息，并返回成功
	else if (0 == strcmp(mode, "used"))
		ret = SYSTEM_SWAP_USED(result);
	// 如果模式参数等于"total"字符串，则调用SYSTEM_SWAP_TOTAL函数获取总空间信息，并返回成功
	else if (0 == strcmp(mode, "total"))
		ret = SYSTEM_SWAP_TOTAL(result);
	// 如果模式参数等于"pfree"字符串，则调用SYSTEM_SWAP_PFREE函数获取物理自由空间信息，并返回成功
	else if (0 == strcmp(mode, "pfree"))
		ret = SYSTEM_SWAP_PFREE(result);
	// 如果模式参数等于"pused"字符串，则调用SYSTEM_SWAP_PUSED函数获取物理已用空间信息，并返回成功
	else if (0 == strcmp(mode, "pused"))
		ret = SYSTEM_SWAP_PUSED(result);
	// 如果模式参数不等于以上几种情况，则直接返回失败
	else
		ret = SYSINFO_RET_FAIL;

	// 返回函数执行结果
	return ret;
}

