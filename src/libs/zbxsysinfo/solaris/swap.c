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

/******************************************************************************
 *                                                                            *
 * Function: get_swapinfo                                                     *
 *                                                                            *
 * Purpose: get swap usage statistics                                         *
 *                                                                            *
 * Return value: SUCCEED if swap usage statistics retrieved successfully      *
 *               FAIL otherwise                                               *
 *                                                                            *
 * Comments: we try to imitate "swap -l".                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是获取操作系统交换空间的信息，包括总的交换空间大小和空闲交换空间大小。它通过调用系统调用函数`swapctl`来获取交换空间的相关信息，然后对交换空间进行遍历和计算，最后将结果填充到传入的指针变量中。
 *
 *注释中详细说明了每个步骤，包括分配内存空间、获取交换项数量、填充路径指针、获取交换项列表、遍历结构体并计算总交换空间和空闲交换空间等。同时，还对一些关键点进行了错误处理，以确保程序的稳定运行。整个函数的返回值为SUCCEED或FAIL，表示是否成功获取到交换空间信息。
 ******************************************************************************/
/* 定义一个函数，获取交换空间的信息 */
static int get_swapinfo(zbx_uint64_t *total, zbx_uint64_t *free1, char **error)
{
	/* 定义变量 */
	int			i, cnt, cnt2, page_size, ret = FAIL;
	struct swaptable	*swt = NULL;
	struct swapent		*ste;
	static char		path[256];

	/* 获取交换项的总数 */
	if (-1 == (cnt = swapctl(SC_GETNSWP, 0)))
	{
		*error = zbx_dsprintf(NULL, "Cannot obtain number of swap entries: %s", zbx_strerror(errno));
		return FAIL;
	}

	if (0 == cnt)
	{
		*total = *free1 = 0;
		return SUCCEED;
	}

	/* 分配空间用于存储交换项信息 */
	swt = (struct swaptable *)zbx_malloc(swt, sizeof(struct swaptable) + (cnt - 1) * sizeof(struct swapent));

	swt->swt_n = cnt;

	/* 填充ste_path指针：我们不关心路径，所以将它们全部指向同一个缓冲区 */
	ste = &(swt->swt_ent[0]);
	i = cnt;
	while (--i >= 0)
	{
		ste++->ste_path = path;
	}

	/* grab all swap info */
	if (-1 == (cnt2 = swapctl(SC_LIST, swt)))
	{
		*error = zbx_dsprintf(NULL, "Cannot obtain a list of swap entries: %s", zbx_strerror(errno));
		goto finish;
	}

	if (cnt != cnt2)
	{
		*error = zbx_strdup(NULL, "Obtained an unexpected number of swap entries.");
		goto finish;
	}

	/* 遍历结构体并累加字段值 */
	*total = *free1 = 0;
	ste = &(swt->swt_ent[0]);
	i = cnt;
	while (--i >= 0)
	{
		/* 不计入删除中的槽位 */
		if (0 == (ste->ste_flags & (ST_INDEL | ST_DOINGDEL)))
		{
			*total += ste->ste_pages;
			*free1 += ste->ste_free;
		}
		ste++;
	}

	page_size = getpagesize();

	/* 填充结果 */
	*total *= page_size;
	*free1 *= page_size;

	ret = SUCCEED;
finish:
	zbx_free(swt);

	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统的交换空间信息（包括总大小和空闲大小），并将这些信息存储在result结构体中。如果获取交换空间信息的过程中出现错误，则返回失败状态码并记录错误信息。否则，返回成功状态码。
 ******************************************************************************/
/* 定义一个静态函数，用于获取系统的交换空间信息，并将其存储在result结构体中
 * 参数：request 请求结构体指针
 *       result 结果结构体指针
 * 返回值：成功则返回SYSINFO_RET_OK，失败则返回SYSINFO_RET_FAIL
 */
static int SYSTEM_SWAP_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义两个zbx_uint64_t类型的变量total和free1，用于存储交换空间的总大小和空闲大小
	 * 注意：zbx_uint64_t是一个自定义类型，表示无符号64位整数
	 */
	zbx_uint64_t	total, free1;
	/* 定义一个字符串指针error，用于存储错误信息 */
	char		*error;

	/* 调用get_swapinfo函数获取交换空间信息，并将结果存储在total和free1变量中，同时获取错误信息并存储在error变量中
	 * 注意：这里使用了条件语句，如果get_swapinfo函数调用失败，即返回值不等于SUCCEED，则执行以下操作
	 */
	if (SUCCEED != get_swapinfo(&total, &free1, &error))
	{
		/* 设置结果结构体中的错误信息 */
		SET_MSG_RESULT(result, error);
		/* 返回失败状态码 */
		return SYSINFO_RET_FAIL;
	}

	/* 设置结果结构体中的交换空间总大小 */
	SET_UI64_RESULT(result, total);

	/* 调用成功，返回OK状态码 */
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统的交换空间使用情况，计算总的交换空间和空闲的交换空间的差值，并将结果存储在result中。如果获取交换空间信息失败，则设置错误信息到result的结果中，并返回SYSINFO_RET_FAIL表示失败。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统的交换空间使用情况
static int	SYSTEM_SWAP_USED(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个zbx_uint64_t类型的变量，分别用于存储总的交换空间和空闲的交换空间
	zbx_uint64_t	total, free1;
	// 定义一个字符指针，用于存储错误信息
	char		*error;

	// 调用get_swapinfo函数，获取系统的交换空间信息，并将结果存储在total和free1变量中，同时获取错误信息并存储在error变量中
	if (SUCCEED != get_swapinfo(&total, &free1, &error))
	{
		// 如果获取交换空间信息失败，设置错误信息到result的结果中，并返回SYSINFO_RET_FAIL表示失败
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 计算总的交换空间和空闲的交换空间的差值，并将结果存储在result中
	SET_UI64_RESULT(result, total - free1);

	// 如果没有错误，返回SYSINFO_RET_OK表示成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统交换空间的信息，并将获取到的空闲交换空间大小存储在结果对象中。如果获取交换空间信息失败，则设置结果对象的错误信息并返回失败。
 ******************************************************************************/
// 定义一个静态函数 SYSTEM_SWAP_FREE，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
static int SYSTEM_SWAP_FREE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个zbx_uint64_t类型的变量 total 和 free1，用来存储交换空间的总大小和空闲大小。
	zbx_uint64_t	total, free1;
	// 定义一个字符串指针 error，用来存储错误信息。
	char		*error;

	// 调用 get_swapinfo 函数获取交换空间的信息，并将结果存储在 total、free1 和 error 变量中。
	if (SUCCEED != get_swapinfo(&total, &free1, &error))
	{
		// 如果获取交换空间信息失败，设置结果对象的错误信息为 error，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 设置结果对象的 free1 值为获取到的空闲交换空间大小。
	SET_UI64_RESULT(result, free1);

	// 返回 SYSINFO_RET_OK，表示成功。
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统交换盘的使用率，并将结果存储在AGENT_RESULT结构体中。具体步骤如下：
 *
 *1. 定义两个交换空间变量total和free1，以及一个错误指针error。
 *2. 调用get_swapinfo函数获取交换空间信息，并将结果存储在total和free1变量中，同时记录错误信息。
 *3. 如果获取交换空间信息失败，设置错误结果并返回SYSINFO_RET_FAIL。
 *4. 如果总交换空间为0，说明无法计算百分比，设置错误信息并返回SYSINFO_RET_FAIL。
 *5. 计算使用率，将结果存储在result中，并转换为百分比形式。
 *6. 如果没有错误，返回SYSINFO_RET_OK，表示成功。
 ******************************************************************************/
// 定义一个静态函数，用于处理系统交换盘使用情况
static int SYSTEM_SWAP_PUSED(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个变量，分别为总交换空间、空闲交换空间和一个错误指针
	zbx_uint64_t	total, free1;
	char		*error;

	// 调用函数获取交换空间信息，并将结果存储在total和free1变量中，同时记录错误信息
	if (SUCCEED != get_swapinfo(&total, &free1, &error))
	{
		// 如果获取交换空间信息失败，设置错误结果并返回SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 如果总交换空间为0，说明无法计算百分比，设置错误信息并返回SYSINFO_RET_FAIL
	if (0 == total)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	// 计算使用率，将结果存储在result中，并转换为百分比形式
	SET_DBL_RESULT(result, 100.0 * (double)(total - free1) / (double)total);

	// 如果没有错误，返回SYSINFO_RET_OK，表示成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统交换空间的空闲百分比。首先通过 get_swapinfo 函数获取交换空间的总大小和空闲大小，然后判断是否可以计算百分比。如果可以计算，将空闲百分比存储在结果变量中，并返回 SYSINFO_RET_OK，表示操作成功。如果无法计算百分比，设置错误结果并返回 SYSINFO_RET_FAIL。
 ******************************************************************************/
// 定义一个静态函数 SYSTEM_SWAP_PFREE，接收两个参数，一个是 AGENT_REQUEST 类型的请求，另一个是 AGENT_RESULT 类型的结果
static int SYSTEM_SWAP_PFREE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个zbx_uint64_t类型的变量 total 和 free1，用于存储交换空间的总大小和空闲大小
	zbx_uint64_t	total, free1;
	// 定义一个字符串指针 error，用于存储错误信息
	char		*error;

	// 调用 get_swapinfo 函数获取交换空间信息，并将结果存储在 total、free1 和 error 变量中
	if (SUCCEED != get_swapinfo(&total, &free1, &error))
	{
		// 如果获取交换空间信息失败，设置错误结果并返回 SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (0 == total)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, 100.0 * (double)free1 / (double)total);

	return SYSINFO_RET_OK;
}

int	SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个字符指针变量tmp，用于存储请求中的参数
	char	*tmp;
	// 定义一个整型变量ret，用于存储函数调用结果
	int	ret;

	// 检查请求参数的数量，如果大于2，则表示参数过多
	if (2 < request->nparam)
	{
		// 设置结果中的错误信息为"Too many parameters."，并返回SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取第一个参数，并存储在tmp中
	tmp = get_rparam(request, 0);

	// 检查tmp是否不为空，如果不为空，则检查第一个参数是否为"all"（默认参数）
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "all"))	/* default parameter */
	{
		// 如果第一个参数不是"all"，则设置结果中的错误信息为"Invalid first parameter."，并返回SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取第二个参数，并存储在tmp中
	tmp = get_rparam(request, 1);

	// 检查tmp是否为空，或者第二个参数是否为"free"（默认参数）
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "free"))	/* default parameter */
		// 如果第二个参数不是"free"，则调用SYSTEM_SWAP_FREE函数，并将结果存储在result中
		ret = SYSTEM_SWAP_FREE(request, result);
	else if (0 == strcmp(tmp, "total"))
		// 如果第二个参数是"total"，则调用SYSTEM_SWAP_TOTAL函数，并将结果存储在result中
		ret = SYSTEM_SWAP_TOTAL(request, result);
	else if (0 == strcmp(tmp, "used"))
		ret = SYSTEM_SWAP_USED(request, result);
	else if (0 == strcmp(tmp, "pfree"))
		ret = SYSTEM_SWAP_PFREE(request, result);
	else if (0 == strcmp(tmp, "pused"))
		ret = SYSTEM_SWAP_PUSED(request, result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从内核统计设施中获取CPU的交换分区信息（交换入和交换出），并将这些信息累加到相应的指针变量中。如果找不到CPU统计信息，则返回错误信息。
 ******************************************************************************/
// 定义一个静态函数get_swap_io，用于获取交换分区信息
static int get_swap_io(zbx_uint64_t *swapin, zbx_uint64_t *pgswapin, zbx_uint64_t *swapout,
                       zbx_uint64_t *pgswapout, char **error)
{
    // 定义一个kstat_ctl_t类型的指针kc，用于操作内核统计设施
    kstat_ctl_t *kc;
    // 定义一个kstat_t类型的指针k，用于遍历内核统计链表
    kstat_t *k;
    // 定义一个cpu_stat_t类型的指针cpu，用于存储CPU统计信息
    cpu_stat_t *cpu;
    // 定义一个整型变量cpu_count，用于记录找到的CPU数量
    int cpu_count = 0;

    // 尝试打开内核统计设施
    if (NULL == (kc = kstat_open()))
    {
        // 打开失败，返回错误信息
        *error = zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s", zbx_strerror(errno));
        return SYSINFO_RET_FAIL;
    }

    // 遍历内核统计链表，查找CPU统计信息
    for (k = kc->kc_chain; NULL != k; k = k->ks_next)
    {
        // 判断是否找到CPU统计信息
        if (0 == strncmp(k->ks_name, "cpu_stat", 8))
        {
            // 读取CPU统计信息
            if (-1 == kstat_read(kc, k, NULL))
            {
                // 读取失败，返回错误信息
                *error = zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s",
                                       zbx_strerror(errno));
                goto clean;
            }

            // 获取CPU统计信息地址
            cpu = (cpu_stat_t *)k->ks_data;

            // 处理交换入出信息
            if (NULL != swapin)
            {
                // 累加交换入次数
                *swapin += cpu->cpu_vminfo.swapin;
            }

            if (NULL != pgswapin)
            {
                // 累加交换入页面数量
                *pgswapin += cpu->cpu_vminfo.pgswapin;
            }

            if (NULL != swapout)
            {
                // 累加交换出次数
                *swapout += cpu->cpu_vminfo.swapout;
            }

            if (NULL != pgswapout)
            {
                // 累加交换出页面数量
                *pgswapout += cpu->cpu_vminfo.pgswapout;
            }

            // 增加CPU统计信息计数
            cpu_count++;
        }
    }

    // 如果没有找到CPU统计信息，关闭内核统计设施并返回错误信息
    if (0 == cpu_count)
    {
        kstat_close(kc);

        *error = zbx_strdup(NULL, "Cannot find swap information.");
        return SYSINFO_RET_FAIL;
    }
clean:
    // 关闭内核统计设施
    kstat_close(kc);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理系统交换操作的请求和结果。该函数接收两个参数，分别是请求指针和结果指针。首先检查请求参数的数量，如果超过2个，则返回错误。接着获取第一个和第二个参数，判断它们的有效性。如果第一个参数不为空且不是\"all\"，或者第二个参数为空或者等于\"count\"，则调用get_swap_io函数获取交换信息，并设置结果。如果任何一步出现错误，设置错误信息并返回失败。如果所有操作都成功，返回成功。
 ******************************************************************************/
// 定义一个函数，用于处理系统交换操作的请求和结果
int SYSTEM_SWAP_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，包括返回值、临时指针、错误信息以及交换值
	int		ret;
	char		*tmp, *error;
	zbx_uint64_t	value = 0;

	// 检查请求参数的数量，如果超过2个，返回错误
	if (2 < request->nparam)
	{
		// 设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数，并判断是否为空或者不是"all"字符串
	tmp = get_rparam(request, 0);

	// 如果第一个参数不为空且不是"all"，则设置错误信息并返回失败
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "all"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第二个参数，并判断是否为空或者等于"count"
	tmp = get_rparam(request, 1);

	// 如果第二个参数为空或者等于"count"，则调用get_swap_io函数获取交换信息，并设置结果
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "count"))
		ret = get_swap_io(&value, NULL, NULL, NULL, &error);
	else if (0 == strcmp(tmp, "pages"))
		ret = get_swap_io(NULL, &value, NULL, NULL, &error);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_OK == ret)
		SET_UI64_RESULT(result, value);
	else
		SET_MSG_RESULT(result, error);

	return SYSINFO_RET_OK;
}

int	SYSTEM_SWAP_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一些变量，包括返回值、临时指针、错误信息以及交换空间的值
    int		ret;
    char		*tmp, *error;
    zbx_uint64_t	value = 0;

    // 检查传入的请求参数数量，如果超过2个，则返回错误信息
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数，即临时指针指向的内存区域
    tmp = get_rparam(request, 0);

    // 检查第一个参数是否为空，如果不是，且不是字符串 "all"，则返回错误信息
    if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "all"))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第二个参数，即临时指针指向的内存区域
    tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "count"))
		ret = get_swap_io(NULL, NULL, &value, NULL, &error);
	else if (0 == strcmp(tmp, "pages"))
		ret = get_swap_io(NULL, NULL, NULL, &value, &error);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_OK == ret)
		SET_UI64_RESULT(result, value);
	else
		SET_MSG_RESULT(result, error);

	return SYSINFO_RET_OK;
}
