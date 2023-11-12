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
int	get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
	return FAIL;
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 get_diskstat 的函数，用于获取指定硬盘设备的状态信息。函数接收两个参数，一个是硬盘设备名称 devname，另一个是用于存储硬盘状态信息的指针 dstat。当函数执行失败时，返回一个错误代码 FAIL。
 ******************************************************************************/
// 定义一个名为 get_diskstat 的函数，接收两个参数：
// 参数1：一个字符串指针 devname，表示硬盘设备名称；
/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定硬盘设备的统计信息（如读取和写入字节数、读取和写入操作次数等），并将这些信息存储在传入的指针变量中。如果找不到符合条件的硬盘设备，则返回错误信息。代码中使用了`sysctl`函数来获取硬盘数量和硬盘统计信息，然后根据设备名筛选并累加相应的统计信息。最后，将结果存储在传入的指针变量中，并返回成功。
 ******************************************************************************/
/* 定义一个获取磁盘统计信息的函数 */
static int get_disk_stats(const char *devname, zbx_uint64_t *rbytes, zbx_uint64_t *wbytes, zbx_uint64_t *roper,
                         zbx_uint64_t *woper, char **error)
{
	/* 定义一些变量 */
	int			ret = SYSINFO_RET_FAIL, mib[2], i, drive_count;
	size_t			len;
	struct diskstats	*stats;

	/* 初始化变量 */
	mib[0] = CTL_HW;
	mib[1] = HW_DISKCOUNT;
	len = sizeof(drive_count);

	/* 获取硬盘数量 */
	if (0 != sysctl(mib, 2, &drive_count, &len, NULL, 0))
	{
		/* 获取硬盘数量失败，返回错误信息 */
		*error = zbx_dsprintf(NULL, "Cannot obtain number of disks: %s", zbx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	/* 计算硬盘统计信息所需内存空间 */
	len = drive_count * sizeof(struct diskstats);

	/* 分配内存空间用于存储硬盘统计信息 */
	stats = zbx_calloc(NULL, drive_count, len);

	/* 获取硬盘统计信息 */
	mib[0] = CTL_HW;
	mib[1] = HW_DISKSTATS;

	if (NULL != rbytes)
		*rbytes = 0;
	if (NULL != wbytes)
		*wbytes = 0;
	if (NULL != roper)
		*roper = 0;
	if (NULL != woper)
		*woper = 0;

	if (0 != sysctl(mib, 2, stats, &len, NULL, 0))
	{
		/* 获取硬盘统计信息失败，释放内存，返回错误信息 */
		zbx_free(stats);
		*error = zbx_dsprintf(NULL, "Cannot obtain disk information: %s", zbx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	/* 遍历每个硬盘，根据设备名筛选并累加相应的统计信息 */
	for (i = 0; i < drive_count; i++)
	{
		if (NULL == devname || '\0' == *devname || 0 == strcmp(devname, "all") ||
		        0 == strcmp(devname, stats[i].ds_name))
		{
			if (NULL != rbytes)
				*rbytes += stats[i].ds_rbytes;
			if (NULL != wbytes)
				*wbytes += stats[i].ds_wbytes;
			if (NULL != roper)
				*roper += stats[i].ds_rxfer;
			if (NULL != woper)
				*woper += stats[i].ds_wxfer;

			/* 找到符合条件的硬盘，设置返回值为成功 */
			ret = SYSINFO_RET_OK;
		}
	}

	/* 释放内存 */
	zbx_free(stats);

	/* 如果返回失败，返回错误信息 */
	if (SYSINFO_RET_FAIL == ret)
	{
		*error = zbx_strdup(NULL, "Cannot find information for this disk device.");
		return SYSINFO_RET_FAIL;
	}

	/* 返回成功 */
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定设备的硬盘读取字节数，并将结果存储在 result 指针指向的 AGENT_RESULT 结构体中。如果获取硬盘读取字节数失败，则设置 result 中的错误信息，并返回操作失败；如果成功，则将硬盘读取字节数返回给调用者。
 ******************************************************************************/
// 定义一个静态函数 VFS_DEV_READ_BYTES，接收两个参数：一个字符串指针 devname，一个 AGENT_RESULT 类型的指针 result。
static int VFS_DEV_READ_BYTES(const char *devname, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储硬盘读取字节数。
	zbx_uint64_t value;
	// 定义一个字符串指针 error，用于存储错误信息。
	char *error;

	// 调用 get_disk_stats 函数，获取指定设备（devname）的硬盘读取字节数，并将结果存储在 value 中。
	if (SYSINFO_RET_OK != get_disk_stats(devname, &value, NULL, NULL, NULL, &error))
	{
		// 如果获取硬盘读取字节数失败，设置 result 中的错误信息为 error。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示操作失败。
		return SYSINFO_RET_FAIL;
	}

	// 设置 result 中的值为目标值（value），即将硬盘读取字节数返回给调用者。
	SET_UI64_RESULT(result, value);

	// 返回 SYSINFO_RET_OK，表示操作成功。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是读取指定设备的磁盘统计信息，并将结果存储在result参数中。函数VFS_DEV_READ_OPERATIONS接收两个参数，一个是设备名称（devname），另一个是用于存储结果的结构体指针（result）。在函数内部，首先调用get_disk_stats函数获取磁盘统计信息，并将结果存储在value变量中。然后将value变量设置为result结构体中的值，最后返回成功或失败的状态码。
 ******************************************************************************/
// 定义一个静态函数，用于读取设备的磁盘统计信息
static int	VFS_DEV_READ_OPERATIONS(const char *devname, AGENT_RESULT *result)
{
	// 定义一个无符号64位整数变量，用于存储磁盘读操作的数值
	zbx_uint64_t	value;
	// 定义一个字符串指针，用于存储错误信息
	char		*error;

	// 调用函数get_disk_stats，获取设备磁盘统计信息，并将结果存储在value变量中
	if (SYSINFO_RET_OK != get_disk_stats(devname, NULL, NULL, &value, NULL, &error))
	{
		// 设置错误信息到result结果结构体中
		SET_MSG_RESULT(result, error);
		// 返回错误码，表示获取磁盘统计信息失败
		return SYSINFO_RET_FAIL;
	}

	// 设置result结果结构体中的value值为获取到的磁盘读操作数值
	SET_UI64_RESULT(result, value);

	// 返回成功码，表示获取磁盘统计信息成功
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定设备的硬盘写入字节数，并将结果存储在传入的 AGENT_RESULT 类型的指针（result）所指向的空间中。如果获取过程中出现错误，则将错误信息设置到 result 指向的空间中，并返回失败。否则，将硬盘写入字节数设置到 result 指向的空间中，并返回成功。
 ******************************************************************************/
// 定义一个静态函数 VFS_DEV_WRITE_BYTES，接收两个参数，一个是设备名称（const char *devname），另一个是 AGENT_RESULT 类型的指针（AGENT_RESULT *result）
static int VFS_DEV_WRITE_BYTES(const char *devname, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储硬盘写入字节数
	zbx_uint64_t value;
	// 定义一个 char 类型的指针变量 error，用于存储错误信息
	char *error;

	// 调用 get_disk_stats 函数获取指定设备（devname）的硬盘写入字节数（value）
	if (SYSINFO_RET_OK != get_disk_stats(devname, NULL, &value, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

// 定义一个静态函数，用于执行VFS设备的写操作
static int	VFS_DEV_WRITE_OPERATIONS(const char *devname, AGENT_RESULT *result)
{
	// 定义一个zbx_uint64_t类型的变量value，用于存储硬盘统计信息
	zbx_uint64_t	value;
	// 定义一个字符指针error，用于存储获取硬盘统计信息时的错误信息
	char		*error;

	// 调用get_disk_stats函数获取指定设备（devname）的硬盘统计信息，并将结果存储在value和error变量中
	if (SYSINFO_RET_OK != get_disk_stats(devname, NULL, NULL, NULL, &value, &error))
	{
		// 如果获取硬盘统计信息失败，设置result的错误信息为error，并返回SYSINFO_RET_FAIL表示操作失败
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 如果获取硬盘统计信息成功，将value存储在result中，并返回SYSINFO_RET_OK表示操作成功
	SET_UI64_RESULT(result, value);

	// 返回SYSINFO_RET_OK，表示VFS设备的写操作成功
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为VFS_DEV_READ的函数，该函数接收两个参数（请求和结果），根据请求中的参数值判断调用相应的操作函数（VFS_DEV_READ_OPERATIONS或VFS_DEV_READ_BYTES），并返回执行结果。如果请求参数不合法，则返回错误信息。
 ******************************************************************************/
// 定义一个函数VFS_DEV_READ，接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result
int VFS_DEV_READ(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义两个字符指针devname和mode，以及一个整型变量ret
    char *devname, *mode;
    int ret;

    // 检查request中的参数数量，如果大于2，则返回错误信息
    if (2 < request->nparam)
    {
        // 设置result的返回信息为"Too many parameters."，并返回SYSINFO_RET_FAIL表示失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 从request中获取第一个参数，即设备名devname
    devname = get_rparam(request, 0);
    // 从request中获取第二个参数，即操作模式mode
    mode = get_rparam(request, 1);

    // 检查mode是否为NULL，或者为空字符串，或者等于"operations"字符串，如果是，则调用VFS_DEV_READ_OPERATIONS函数处理
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "operations"))
        ret = VFS_DEV_READ_OPERATIONS(devname, result);
    // 否则，检查mode是否为"bytes"，如果是，则调用VFS_DEV_READ_BYTES函数处理
    else if (0 == strcmp(mode, "bytes"))
        ret = VFS_DEV_READ_BYTES(devname, result);
    // 如果mode不是"operations"和"bytes"，则返回错误信息
    else
    {
        // 设置result的返回信息为"Invalid second parameter."，并返回SYSINFO_RET_FAIL表示失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 返回ret，表示函数执行成功
    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个AGENT_REQUEST类型的request和一个AGENT_RESULT类型的result，然后根据request中的参数调用相应的函数处理请求。如果请求参数不合法，返回错误信息。
 *
 *注释详细说明了每个步骤，包括函数的定义、参数检查、获取参数、判断参数合法性以及调用相应函数处理请求。最后返回处理结果。
 ******************************************************************************/
// 定义一个函数VFS_DEV_WRITE，接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result
int VFS_DEV_WRITE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义两个字符指针变量devname和mode，以及一个整型变量ret
    char *devname, *mode;
    int ret;

    // 检查request中的参数数量，如果大于2，则返回错误信息，并设置result
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 从request中获取devname和mode字符串
    devname = get_rparam(request, 0);
    mode = get_rparam(request, 1);


	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "operations"))
		ret = VFS_DEV_WRITE_OPERATIONS(devname, result);
	else if (0 == strcmp(mode, "bytes"))
		ret = VFS_DEV_WRITE_BYTES(devname, result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}
