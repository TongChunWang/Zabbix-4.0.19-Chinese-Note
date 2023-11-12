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
#include "stats.h"
#include "diskdevices.h"

#define ZBX_DEV_PFX	"/dev/"
#define ZBX_DEV_READ	0
#define ZBX_DEV_WRITE	1

static struct statinfo	*si = NULL;
/******************************************************************************
 * *
 *该代码块的主要目的是获取指定磁盘设备的统计信息，并将结果存储在指定的数组中。具体来说，它实现了以下功能：
 *
 *1. 验证输入的 devname 参数不为空。
 *2. 初始化一个 zbx_uint64_t 类型的数组，用于存储磁盘统计信息。
 *3. 分配内存并初始化 statinfo 结构体，其中包含 devstat 信息。
 *4. 根据 devname 参数，遍历 statinfo 结构体中的所有磁盘设备信息。
 *5. 添加每个磁盘的读写操作和字节统计信息到数组中。
 *6. 如果 devname 参数不为空，则跳出循环，返回统计结果。
 *
 *整个代码块的作用是获取指定磁盘设备的统计信息，并将结果存储在指定的数组中，以便后续处理和分析。
 ******************************************************************************/
/* 定义一个函数，获取磁盘统计信息，并将结果存储在指定的数组中 */
int get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
	/* 定义变量 */
	int		i;
	struct devstat	*ds = NULL;
	int		ret = FAIL;
	char		dev[DEVSTAT_NAME_LEN + 10];
	const char	*pd;	/* 指向设备名称去掉"/dev/"前缀的指针，例如 'da0' */

	/* 断言确保 devname 不为空 */
	assert(devname);

	/* 初始化统计数组 */
	for (i = 0; i < ZBX_DSTAT_MAX; i++)
		dstat[i] = (zbx_uint64_t)__UINT64_C(0);

	/* 分配内存，初始化 statinfo 结构体 */
	if (NULL == si)
	{
		si = (struct statinfo *)zbx_malloc(si, sizeof(struct statinfo));
		si->dinfo = (struct devinfo *)zbx_malloc(NULL, sizeof(struct devinfo));
		memset(si->dinfo, 0, sizeof(struct devinfo));
	}

	/* 指向设备名称的指针 */
	pd = devname;

	/* 跳过 ZBX_DEV_PFX 前缀，如果存在 */
	if ('\0' != *devname && 0 == strncmp(pd, ZBX_DEV_PFX, ZBX_CONST_STRLEN(ZBX_DEV_PFX)))
			pd += ZBX_CONST_STRLEN(ZBX_DEV_PFX);

	/* 调用函数获取磁盘统计信息 */
#if DEVSTAT_USER_API_VER >= 5
	if (-1 == devstat_getdevs(NULL, si))
#else
	if (-1 == getdevs(si))
#endif
		return FAIL;

	/* 遍历获取到的磁盘设备信息 */
	for (i = 0; i < si->dinfo->numdevs; i++)
	{
		ds = &si->dinfo->devices[i];

		/* 如果 '*devname' 为空，则统计所有磁盘的总信息 */
		if ('\0' != *devname)
		{
			zbx_snprintf(dev, sizeof(dev), "%s%d", ds->device_name, ds->unit_number);
			if (0 != strcmp(dev, pd))
				continue;
		}

#if DEVSTAT_USER_API_VER >= 5
		dstat[ZBX_DSTAT_R_OPER] += (zbx_uint64_t)ds->operations[DEVSTAT_READ];
		dstat[ZBX_DSTAT_W_OPER] += (zbx_uint64_t)ds->operations[DEVSTAT_WRITE];
		dstat[ZBX_DSTAT_R_BYTE] += (zbx_uint64_t)ds->bytes[DEVSTAT_READ];
		dstat[ZBX_DSTAT_W_BYTE] += (zbx_uint64_t)ds->bytes[DEVSTAT_WRITE];
#else
		dstat[ZBX_DSTAT_R_OPER] += (zbx_uint64_t)ds->num_reads;
		dstat[ZBX_DSTAT_W_OPER] += (zbx_uint64_t)ds->num_writes;
		dstat[ZBX_DSTAT_R_BYTE] += (zbx_uint64_t)ds->bytes_read;
		dstat[ZBX_DSTAT_W_BYTE] += (zbx_uint64_t)ds->bytes_written;
#endif
		ret = SUCCEED;

		if ('\0' != *devname)
			break;
	}

	return ret;
}

/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *整个代码块的主要目的是实现一个名为 vfs_dev_rw 的函数，该函数接受 AGENT_REQUEST 类型的参数，根据请求参数的值读取和写入磁盘设备的数据。函数首先检查请求参数的数量，然后获取设备名称、类型和模式。接下来，根据类型和模式设置相应的统计数据，并返回结果。
 ******************************************************************************/
/* 定义一个静态函数 vfs_dev_rw，用于读取和写入磁盘设备的数据 */
static int	vfs_dev_rw(AGENT_REQUEST *request, AGENT_RESULT *result, int rw)
{
	/* 定义一个结构体指针，用于存储磁盘设备的数据 */
	ZBX_SINGLE_DISKDEVICE_DATA *device;
	/* 定义一个字符数组，用于存储设备名称 */
	char		devname[32], *tmp;
	/* 定义一个整型变量，用于存储设备类型 */
	int		type;
	/* 定义一个整型变量，用于存储模式 */
	int		mode;
	/* 定义一个zbx_uint64_t类型的数组，用于存储磁盘统计数据 */
	zbx_uint64_t	dstats[ZBX_DSTAT_MAX];
	/* 定义一个字符指针，用于存储设备名称去掉前缀 "/dev/" 的部分 */
	char		*pd;			/* pointer to device name without '/dev/' prefix, e.g. 'da0' */

	/* 检查请求参数的数量，如果超过3个，则返回错误 */
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第一个参数，即设备名称 */
	tmp = get_rparam(request, 0);

	/* 如果第一个参数为空或等于"all"，则清空设备名称 */
	if (NULL == tmp || 0 == strcmp(tmp, "all"))
		*devname = '\0';
	else
		strscpy(devname, tmp);

	pd = devname;

	if ('\0' != *pd)
	{
		/* skip prefix ZBX_DEV_PFX, if present */
		if (0 == strncmp(pd, ZBX_DEV_PFX, ZBX_CONST_STRLEN(ZBX_DEV_PFX)))
			pd += ZBX_CONST_STRLEN(ZBX_DEV_PFX);
	}
	tmp = get_rparam(request, 1);

	/* 检查第二个参数是否合法，如果是合法类型，则赋值给type */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "bps"))	/* default parameter */
		type = ZBX_DSTAT_TYPE_BPS;
	else if (0 == strcmp(tmp, "ops"))
		type = ZBX_DSTAT_TYPE_OPS;
	else if (0 == strcmp(tmp, "bytes"))
		type = ZBX_DSTAT_TYPE_BYTE;
	else if (0 == strcmp(tmp, "operations"))
		type = ZBX_DSTAT_TYPE_OPER;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 如果是字节或操作类型，需要额外的一个参数 */
	if (type == ZBX_DSTAT_TYPE_BYTE || type == ZBX_DSTAT_TYPE_OPER)
	{
		if (2 < request->nparam)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			return SYSINFO_RET_FAIL;
		}

		if (FAIL == get_diskstat(pd, dstats))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain disk information."));
			return SYSINFO_RET_FAIL;
		}

		if (ZBX_DSTAT_TYPE_BYTE == type)
			SET_UI64_RESULT(result, dstats[(ZBX_DEV_READ == rw ? ZBX_DSTAT_R_BYTE : ZBX_DSTAT_W_BYTE)]);
		else	/* ZBX_DSTAT_TYPE_OPER */
			SET_UI64_RESULT(result, dstats[(ZBX_DEV_READ == rw ? ZBX_DSTAT_R_OPER : ZBX_DSTAT_W_OPER)]);

		return SYSINFO_RET_OK;
	}
		tmp = get_rparam(request, 2);

		/* 检查第三个参数是否合法，如果是合法模式，则赋值给mode */
		if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))	/* default parameter */
			mode = ZBX_AVG1;
		else if (0 == strcmp(tmp, "avg5"))
			mode = ZBX_AVG5;
		else if (0 == strcmp(tmp, "avg15"))
			mode = ZBX_AVG15;
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
			return SYSINFO_RET_FAIL;
		}


	/* 检查收集器是否已存在 */
	if (NULL == collector)
	{
		/* CPU统计数据收集器以及（可选的）磁盘统计数据收集器仅在Zabbix agentd作为守护进程运行时启动。
		 * 当Zabbix agent或agentd使用“-p”或“-t”参数启动时，不支持以下键：
		 * “vfs.dev.read”，“vfs.dev.write”（带一些参数，如sps，ops）
		 */

		SET_MSG_RESULT(result, zbx_strdup(NULL, "This item is available only in daemon mode when collectors are"
				" started."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (device = collector_diskdevice_get(pd)))
	{
		if (FAIL == get_diskstat(pd, dstats))	/* validate device name */
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain disk information."));
			return SYSINFO_RET_FAIL;
		}

		if (NULL == (device = collector_diskdevice_add(pd)))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot add disk device to agent collector."));
			return SYSINFO_RET_FAIL;
		}
	}

	if (ZBX_DSTAT_TYPE_BPS == type)	/* default parameter */
		SET_DBL_RESULT(result, (ZBX_DEV_READ == rw ? device->r_bps[mode] : device->w_bps[mode]));
	else if (ZBX_DSTAT_TYPE_OPS == type)
		SET_DBL_RESULT(result, (ZBX_DEV_READ == rw ? device->r_ops[mode] : device->w_ops[mode]));

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为VFS_DEV_READ的函数，该函数接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result。接着调用另一个名为vfs_dev_rw的函数，传入三个参数：request、result和ZBX_DEV_READ（表示设备的读操作）。这个函数的主要目的是实现文件系统的读操作，并将结果返回给调用者。
 ******************************************************************************/
// 定义一个函数VFS_DEV_READ，接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 VFS_DEV_WRITE 的函数，该函数用于处理 VFS 设备的写操作。函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。通过调用另一个名为 vfs_dev_rw 的函数来执行实际的读写操作，并根据操作结果返回相应的返回值。如果操作成功，返回 0；如果操作失败，返回 -1。在示例代码中，我们创建了 AGENT_REQUEST 和 AGENT_RESULT 结构体，并调用 VFS_DEV_WRITE 函数进行 VFS 设备写操作。根据返回值判断操作是否成功，并输出结果。
 ******************************************************************************/
// 定义一个函数 VFS_DEV_WRITE，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result
int	VFS_DEV_READ(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return vfs_dev_rw(request, result, ZBX_DEV_READ);
}


// 函数vfs_dev_rw的参数分别为request、result和operation（这里是ZBX_DEV_READ，表示读操作）
// 该函数的主要目的是实现文件系统的读操作，并将结果返回给调用者
int	VFS_DEV_WRITE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return vfs_dev_rw(request, result, ZBX_DEV_WRITE);
}
