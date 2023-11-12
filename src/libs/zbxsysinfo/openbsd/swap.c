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

static int	get_swap_size(zbx_uint64_t *total, zbx_uint64_t *free, zbx_uint64_t *used, double *pfree, double *pused,
		char **error)
{
    // 定义一个整型数组，用于存储系统调用所需的参数
    int mib[2];
    size_t len;
    struct uvmexp v;

    // 初始化数组第一个元素为 CTL_VM，第二个元素为 VM_UVMEXP
    mib[0] = CTL_VM;
    mib[1] = VM_UVMEXP;

    // 设置系统调用所需的参数长度为 sizeof(v)
    len = sizeof(v);

    // 调用 sysctl 函数获取系统信息，如果失败，则返回错误信息
    if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
    {
        *error = zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno));
        return SYSINFO_RET_FAIL;
    }

    // 获取页面大小（必须是 2 的整数次幂）
    /* int pagesize;	size of a page (PAGE_SIZE): must be power of 2 */

    // 获取交换空间的页面数
    /* int swpages;		number of PAGE_SIZE'ed swap pages */

    // 获取正在使用的交换页面数
    /* int swpginuse;	number of swap pages in use */

    // 如果 total 参数不为空，则计算总的交换空间大小
    if (total)
        *total = (zbx_uint64_t)v.swpages * v.pagesize;

    // 如果 free 参数不为空，则计算免费的交换空间大小
    if (free)
        *free = (zbx_uint64_t)(v.swpages - v.swpginuse) * v.pagesize;

    // 如果 used 参数不为空，则计算已使用的交换空间大小
    if (used)
        *used = (zbx_uint64_t)v.swpginuse * v.pagesize;

    // 如果 pfree 参数不为空，则计算免费交换空间占比
    if (pfree)
        *pfree = v.swpages ? (double)(100.0 * (v.swpages - v.swpginuse)) / v.swpages : 100;

    // 如果 pused 参数不为空，则计算已使用交换空间占比
    if (pused)
        *pused = v.swpages ? (double)(100.0 * v.swpginuse) / v.swpages : 0;

    // 返回成功状态码
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统的交换空间总量，并将结果存储到result指向的结构体中。具体步骤如下：
 *
 *1. 定义一个zbx_uint64_t类型的变量value，用于存储交换空间总量。
 *2. 定义一个字符指针error，用于存储错误信息。
 *3. 调用get_swap_size函数获取交换空间总量，并将返回值判断是否为SYSINFO_RET_OK。
 *4. 如果获取交换空间总量失败，设置错误信息到result指向的结构体中，并返回SYSINFO_RET_FAIL。
 *5. 如果成功获取到交换空间总量，将其存储到result指向的结构体中，并返回SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统的交换空间总量
static int SYSTEM_SWAP_TOTAL(AGENT_RESULT *result)
{
	// 定义一个zbx_uint64_t类型的变量value，用于存储交换空间总量
	zbx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_swap_size(&value, NULL, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

    // 设置返回结果的交换空间大小
    SET_UI64_RESULT(result, value);

    // 返回成功码SYSINFO_RET_OK，表示获取交换空间大小成功
    return SYSINFO_RET_OK;
}

static int	SYSTEM_SWAP_FREE(AGENT_RESULT *result)
{
	// 定义一个zbx_uint64_t类型的变量value，用于存储交换空间大小
	zbx_uint64_t	value;
	// 定义一个字符指针error，用于存储错误信息
	char		*error;

	if (SYSINFO_RET_OK != get_swap_size(NULL, &value, NULL, NULL, NULL, &error))
	{
		// 如果获取交换空间大小失败，设置错误信息到result结构体中，并返回SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 成功获取到交换空间大小，将其存储到result结构体中，并返回SYSINFO_RET_OK
	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	SYSTEM_SWAP_USED(AGENT_RESULT *result)
{
	zbx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_swap_size(NULL, NULL, &value, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的交换空间大小，并将结果存储在传入的AGENT_RESULT结构体中。函数首先定义了两个变量value和error，分别用于存储交换空间大小和错误信息。然后调用get_swap_size函数获取交换空间大小，如果获取失败，将错误信息设置到result结构体中，并返回SYSINFO_RET_FAIL。如果获取成功，将交换空间大小设置到result结构体中，并返回SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统的交换空间大小
static int	SYSTEM_SWAP_PFREE(AGENT_RESULT *result)
{
	// 定义一个double类型的变量，用于存储交换空间大小
	double	value;
	// 定义一个字符指针，用于存储错误信息
	char	*error;

	// 判断get_swap_size函数的返回值是否为SYSINFO_RET_OK
	if (SYSINFO_RET_OK != get_swap_size(NULL, NULL, NULL, &value, NULL, &error))
	{
		// 如果返回值为失败，设置错误信息到result结构体中，并返回SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 设置result结构体中的value值为获取到的交换空间大小
	SET_DBL_RESULT(result, value);

	// 函数执行成功，返回SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统交换空间的使用情况，并将结果存储在result结构体中。首先定义了两个变量value和error，分别用于存储交换空间大小和错误信息。然后调用get_swap_size函数获取交换空间大小，如果成功，将交换空间大小设置到result结构体中，并返回SYSINFO_RET_OK表示成功；如果失败，设置错误信息到result结构体中，并返回SYSINFO_RET_FAIL表示失败。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统交换空间使用情况
static int	SYSTEM_SWAP_PUSED(AGENT_RESULT *result)
{
	// 定义一个double类型的变量，用于存储交换空间的大小
	double	value;
	// 定义一个字符指针，用于存储错误信息
	char	*error;

	// 判断get_swap_size函数的返回值是否为SYSINFO_RET_OK，如果不为OK，说明获取交换空间大小失败
	if (SYSINFO_RET_OK != get_swap_size(NULL, NULL, NULL, NULL, &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

int	SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义两个字符指针，分别用于存储交换设备路径和模式参数 */
	char *swapdev, *mode;
	/* 定义一个整型变量，用于存储函数返回值 */
	int ret;

	/* 检查传入的参数个数是否大于2，如果是，则返回错误信息 */
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		/* 返回失败状态码 */
		return SYSINFO_RET_FAIL;
	}

	/* 获取传入的第一个参数（交换设备路径） */
	swapdev = get_rparam(request, 0);
	/* 获取传入的第二个参数（模式） */
	mode = get_rparam(request, 1);

	/* 设置默认参数 */
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		/* 返回失败状态码 */
		return SYSINFO_RET_FAIL;
	}

	/* 设置默认参数 */
	if (NULL == mode || *mode == '\0' || 0 == strcmp(mode, "free"))
		/* 调用 SYSTEM_SWAP_FREE 函数处理交换空间自由度量 */
		ret = SYSTEM_SWAP_FREE(result);
	else if (0 == strcmp(mode, "used"))
		/* 调用 SYSTEM_SWAP_USED 函数处理交换空间已用量 */
		ret = SYSTEM_SWAP_USED(result);
	else if (0 == strcmp(mode, "total"))
		/* 调用 SYSTEM_SWAP_TOTAL 函数处理交换空间总量 */
		ret = SYSTEM_SWAP_TOTAL(result);
	else if (0 == strcmp(mode, "pfree"))
		/* 调用 SYSTEM_SWAP_PFREE 函数处理交换空间空闲页数 */
		ret = SYSTEM_SWAP_PFREE(result);
	else if (0 == strcmp(mode, "pused"))
		/* 调用 SYSTEM_SWAP_PUSED 函数处理交换空间已用页数 */
		ret = SYSTEM_SWAP_PUSED(result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		/* 返回失败状态码 */
		return SYSINFO_RET_FAIL;
	}

	/* 函数执行成功，返回处理结果 */
	return ret;
}

static int	get_swap_io(zbx_uint64_t *icount, zbx_uint64_t *ipages, zbx_uint64_t *ocount, zbx_uint64_t *opages,
		char **error)
{
	// 定义一个整型数组，用于存储系统信息
	int mib[2];
	// 定义一个size_t类型的变量，用于存储系统信息的长度
	size_t len;
	// 定义一个结构体变量，用于存储uvmexp信息
	struct uvmexp v;

	// 初始化mib数组，第一个元素为CTL_VM，第二个元素为VM_UVMEXP
	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;

	// 设置len为sizeof(v)，用于存储系统信息的长度
	len = sizeof(v);

	// 使用sysctl函数获取系统信息，如果失败，则设置错误信息并返回SYSINFO_RET_FAIL
	if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
	{
		*error = zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	/* int swapins;		swapins */
	/* int swapouts;	swapouts */
	/* int pgswapin;	pages swapped in */
	/* int pgswapout;	pages swapped out */

	// 如果操作系统版本小于OpenBSD 201311，则支持交换磁盘I/O计数
#if OpenBSD < 201311		/* swapins and swapouts are not supported starting from OpenBSD 5.4 */
	if (NULL != icount)
		*icount = (zbx_uint64_t)v.swapins;
	if (NULL != ocount)
		*ocount = (zbx_uint64_t)v.swapouts;
#else
	// 如果icount或ocount不为空，则提示不支持该功能
	if (NULL != icount || NULL != ocount)
	{
		*error = zbx_dsprintf(NULL, "Not supported by the system starting from OpenBSD 5.4.");
		return SYSINFO_RET_FAIL;
	}
#endif
	if (NULL != ipages)
		*ipages = (zbx_uint64_t)v.pgswapin;
	if (NULL != opages)
		*opages = (zbx_uint64_t)v.pgswapout;

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是接收客户端请求，根据请求参数获取交换盘空间信息，并将结果返回给客户端。具体来说，该函数接收两个参数：交换设备（swapdev）和模式（mode）。函数首先检查参数数量，如果过多则返回错误。接下来，获取并检查交换设备和模式参数的合法性。根据模式参数的不同，函数会调用 `get_swap_io` 函数获取交换空间信息，并将结果存储在 `value` 变量中。最后，根据函数执行结果，设置返回结果并输出相应的信息。整个函数的返回值为获取交换空间信息的成功与否。
 ******************************************************************************/
// 定义一个函数，用于系统交换盘空间信息
int SYSTEM_SWAP_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量
	int		ret;
	char		*swapdev, *mode, *error;
	zbx_uint64_t	value;

	// 检查传入的参数数量，如果超过2个，返回错误
	if (2 < request->nparam)
	{
		// 设置返回结果，提示参数过多
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（交换设备）
	swapdev = get_rparam(request, 0);
	// 获取第二个参数（模式）
	mode = get_rparam(request, 1);

	// 检查交换设备参数，仅支持 "all" 字符串
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		// 设置返回结果，提示无效的交换设备参数
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 默认参数
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "count"))
	{
		// 获取交换空间信息，并将结果存储在 value 变量中
		ret = get_swap_io(&value, NULL, NULL, NULL, &error);
	}
	else if (0 == strcmp(mode, "pages"))
	{
		// 获取交换空间信息，但不获取具体页面数量
		ret = get_swap_io(NULL, &value, NULL, NULL, &error);
	}
	else
	{
		// 设置返回结果，提示无效的模式参数
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 如果获取成功，设置返回结果并输出交换空间信息
	if (SYSINFO_RET_OK == ret)
		SET_UI64_RESULT(result, value);
	else
		// 否则，设置返回结果并输出错误信息
		SET_MSG_RESULT(result, error);

	// 返回函数执行结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 `SYSTEM_SWAP_OUT` 的函数，该函数用于获取系统交换分区信息。函数接收两个参数：一个是交换分区设备（`swapdev`），另一个是交换分区模式（`mode`）。函数首先检查传入的参数数量是否正确，然后获取交换分区设备和解压分区模式。接下来，根据传入的模式获取相应的交换分区信息，并将结果存储在 `result` 指针指向的结构体中。如果获取交换分区信息成功，设置结果值为 `value`，否则设置错误信息。最后，返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，用于系统交换分区信息
int SYSTEM_SWAP_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量
	int		ret;
	char		*swapdev, *mode, *error;
	zbx_uint64_t	value;

	// 检查传入的参数数量是否正确，如果超过2个，返回错误
	if (2 < request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（交换分区设备）
	swapdev = get_rparam(request, 0);
	// 获取第二个参数（交换分区模式）
	mode = get_rparam(request, 1);

	// 检查第一个参数是否合法（仅支持 "all" 模式）
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 检查第二个参数是否合法（默认值为 "count" 或 "pages"）
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "count"))
	{
		// 获取交换分区信息
		ret = get_swap_io(NULL, NULL, &value, NULL, &error);
	}
	else if (0 == strcmp(mode, "pages"))
	{
		// 获取交换分区信息
		ret = get_swap_io(NULL, NULL, NULL, &value, &error);
	}
	else
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 如果获取交换分区信息成功，设置结果值
	if (SYSINFO_RET_OK == ret)
		SET_UI64_RESULT(result, value);
	else
		// 设置错误信息
		SET_MSG_RESULT(result, error);

	// 返回函数执行结果
	return ret;
}

