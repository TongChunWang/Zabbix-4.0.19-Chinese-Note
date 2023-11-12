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

#if defined(KERNEL_2_4)
#	define INFO_FILE_NAME	"/proc/partitions"
#	define PARSE(line)	if (sscanf(line, ZBX_FS_UI64 ZBX_FS_UI64 " %*d %s " 		\
					ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d "			\
					ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d %*d %*d %*d",	\
				&rdev_major,							\
				&rdev_minor,							\
				name,								\
				&ds[ZBX_DSTAT_R_OPER],						\
				&ds[ZBX_DSTAT_R_SECT],						\
				&ds[ZBX_DSTAT_W_OPER],						\
				&ds[ZBX_DSTAT_W_SECT]						\
				) != 7) continue
#else
#	define INFO_FILE_NAME	"/proc/diskstats"
#	define PARSE(line)	if (sscanf(line, ZBX_FS_UI64 ZBX_FS_UI64 " %s "			\
					ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d "			\
					ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d %*d %*d %*d",	\
				&rdev_major,							\
				&rdev_minor,							\
				name,								\
				&ds[ZBX_DSTAT_R_OPER],						\
				&ds[ZBX_DSTAT_R_SECT],						\
				&ds[ZBX_DSTAT_W_OPER],						\
				&ds[ZBX_DSTAT_W_SECT]						\
				) != 7								\
				&&								\
				/* some disk partitions */					\
				sscanf(line, ZBX_FS_UI64 ZBX_FS_UI64 " %s "			\
					ZBX_FS_UI64 ZBX_FS_UI64					\
					ZBX_FS_UI64 ZBX_FS_UI64,				\
				&rdev_major,							\
				&rdev_minor,							\
				name,								\
				&ds[ZBX_DSTAT_R_OPER],						\
				&ds[ZBX_DSTAT_R_SECT],						\
				&ds[ZBX_DSTAT_W_OPER],						\
				&ds[ZBX_DSTAT_W_SECT]						\
				) != 7								\
				) continue
#endif

/******************************************************************************
 * *
 *这段代码的主要目的是从指定的文件中读取磁盘统计信息，并根据设备名和设备信息进行匹配。代码首先打开文件，然后遍历文件中的每一行，对每一行数据进行解析。如果设备名不为空且不是全部设备，根据设备名和设备信息判断是否匹配。如果匹配成功，将累加读写操作和扇区数量。最后关闭文件并返回匹配成功的状态。
 ******************************************************************************/


int	get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
	// 打开文件
	FILE		*f;
	// 定义临时字符串和设备路径
	char		tmp[MAX_STRING_LEN], name[MAX_STRING_LEN], dev_path[MAX_STRING_LEN];
	// 定义变量
	int		i, ret = FAIL, dev_exists = FAIL;
	zbx_uint64_t	ds[ZBX_DSTAT_MAX], rdev_major, rdev_minor;
	zbx_stat_t 	dev_st;
	int		found = 0;

	// 初始化统计数据
	for (i = 0; i < ZBX_DSTAT_MAX; i++)
		dstat[i] = (zbx_uint64_t)__UINT64_C(0);

	// 如果设备名不为空且不是全部设备，进行以下操作
	if (NULL != devname && '\0' != *devname && 0 != strcmp(devname, "all"))
	{
		*dev_path = '\0';
		// 如果设备名不以ZBX_DEV_PFX开头，则在前面添加ZBX_DEV_PFX
		if (0 != strncmp(devname, ZBX_DEV_PFX, ZBX_CONST_STRLEN(ZBX_DEV_PFX)))
			strscpy(dev_path, ZBX_DEV_PFX);
		// 拼接设备路径和设备名
		strscat(dev_path, devname);

		// 检查设备是否存在
		if (zbx_stat(dev_path, &dev_st) == 0)
			dev_exists = SUCCEED;
	}

	// 打开文件失败，返回失败
	if (NULL == (f = fopen(INFO_FILE_NAME, "r")))
		return FAIL;

	// 遍历文件中的每一行
	while (NULL != fgets(tmp, sizeof(tmp), f))
	{
		// 解析每一行数据
		PARSE(tmp);

		// 如果设备名不为空且不是全部设备，根据设备名和设备信息判断是否匹配
		if (NULL != devname && '\0' != *devname && 0 != strcmp(devname, "all"))
		{
			if (0 != strcmp(name, devname))
			{
				if (SUCCEED != dev_exists
					|| major(dev_st.st_rdev) != rdev_major
					|| minor(dev_st.st_rdev) != rdev_minor)
					continue;
			}
			else
				found = 1;
		}
		// 累加每一行的读写操作和扇区数量
		dstat[ZBX_DSTAT_R_OPER] += ds[ZBX_DSTAT_R_OPER];
		dstat[ZBX_DSTAT_R_SECT] += ds[ZBX_DSTAT_R_SECT];
		dstat[ZBX_DSTAT_W_OPER] += ds[ZBX_DSTAT_W_OPER];
		dstat[ZBX_DSTAT_W_SECT] += ds[ZBX_DSTAT_W_SECT];

		// 更新返回值
		ret = SUCCEED;

		// 如果找到了匹配的设备，跳出循环
		if (1 == found)
			break;
	}
	// 关闭文件
	zbx_fclose(f);

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Comments: Translate device name to the one used internally by kernel. The  *
 *           translation is done based on minor and major device numbers      *
 *           listed in INFO_FILE_NAME . If the names differ it is usually an  *
 *           LVM device which is listed in kernel device mapper.              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个设备名（包含在 devname 变量中）获取该设备对应的内核设备名（存储在 kernel_devname 变量中），并将其复制到指定的缓冲区。如果成功，返回 SUCCEED，否则返回 FAIL。在解析文件内容时，通过比较设备的 major 和 minor 字段来找到匹配的设备。
 ******************************************************************************/
// 定义一个静态函数，用于获取设备的内核名称
static int	get_kernel_devname(const char *devname, char *kernel_devname, size_t max_kernel_devname_len)
{
	// 声明一些变量
	FILE		*f;
	char		tmp[MAX_STRING_LEN], name[MAX_STRING_LEN], dev_path[MAX_STRING_LEN];
	int		ret = FAIL;
	zbx_uint64_t	ds[ZBX_DSTAT_MAX], rdev_major, rdev_minor;
	zbx_stat_t	dev_st;

	// 如果传入的设备名长度为0，直接返回失败
	if ('\0' == *devname)
		return ret;

	// 初始化 dev_path 为空字符串
	*dev_path = '\0';
	// 如果设备名不是以 ZBX_DEV_PFX 开头，则在 dev_path 前面添加 ZBX_DEV_PFX
	if (0 != strncmp(devname, ZBX_DEV_PFX, ZBX_CONST_STRLEN(ZBX_DEV_PFX)))
		strscpy(dev_path, ZBX_DEV_PFX);
	// 将 devname 添加到 dev_path 后面
	strscat(dev_path, devname);
	if (zbx_stat(dev_path, &dev_st) < 0 || NULL == (f = fopen(INFO_FILE_NAME, "r")))
		return ret;

	while (NULL != fgets(tmp, sizeof(tmp), f))
	{
		PARSE(tmp);
		if (major(dev_st.st_rdev) != rdev_major || minor(dev_st.st_rdev) != rdev_minor)
			continue;

		zbx_strlcpy(kernel_devname, name, max_kernel_devname_len);
		ret = SUCCEED;
		break;
	}
	zbx_fclose(f);

	return ret;
}

/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为`vfs_dev_rw`的函数，该函数用于读取和写入磁盘统计信息。函数接收三个参数：请求指针、结果指针和读写标志。在函数内部，首先检查参数是否合法，然后获取设备名称、统计类型、模式等信息。接下来，根据类型和模式设置结果，并返回成功。
 ******************************************************************************/
// 定义一个静态函数，用于读取和写入磁盘统计信息
static int vfs_dev_rw(AGENT_REQUEST *request, AGENT_RESULT *result, int rw)
{
    // 定义一个结构体指针，用于存储磁盘设备信息
    ZBX_SINGLE_DISKDEVICE_DATA *device;
    // 定义一些字符串指针，用于存储设备名称、临时字符串等
    char *devname, *tmp, kernel_devname[MAX_STRING_LEN];
    // 定义一些整数变量，用于存储类型、模式等
    int type, mode;
    // 定义一个zbx_uint64_t数组，用于存储磁盘统计信息
    zbx_uint64_t dstats[ZBX_DSTAT_MAX];

    // 检查参数个数是否正确，如果过多则返回错误
    if (3 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数，即设备名称
    devname = get_rparam(request, 0);
    // 获取第二个参数，即统计类型
    tmp = get_rparam(request, 1);

    // 判断第二个参数是否合法，如果不合法则返回错误
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "sps")) /* default parameter */
        type = ZBX_DSTAT_TYPE_SPS;
    else if (0 == strcmp(tmp, "ops"))
        type = ZBX_DSTAT_TYPE_OPS;
    else if (0 == strcmp(tmp, "sectors"))
        type = ZBX_DSTAT_TYPE_SECT;
    else if (0 == strcmp(tmp, "operations"))
        type = ZBX_DSTAT_TYPE_OPER;
    else
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 如果类型为SECT或OPER，则检查是否有第三个参数
    if (type == ZBX_DSTAT_TYPE_SECT || type == ZBX_DSTAT_TYPE_OPER)
    {
        if (request->nparam > 2)
        {
            /* Mode is supported only if type is in: operations, sectors. */
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
            return SYSINFO_RET_FAIL;
        }

		if (SUCCEED != get_diskstat(devname, dstats))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain disk information."));
			return SYSINFO_RET_FAIL;
		}

		if (ZBX_DSTAT_TYPE_SECT == type)
			SET_UI64_RESULT(result, dstats[(ZBX_DEV_READ == rw ? ZBX_DSTAT_R_SECT : ZBX_DSTAT_W_SECT)]);
		else
			SET_UI64_RESULT(result, dstats[(ZBX_DEV_READ == rw ? ZBX_DSTAT_R_OPER : ZBX_DSTAT_W_OPER)]);

		return SYSINFO_RET_OK;
	}
        tmp = get_rparam(request, 2);

        // 判断第三个参数是否合法，如果不合法则返回错误
        if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1")) /* default parameter */
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

    // 检查是否已启动收集器
    if (NULL == collector)
    {
        /* CPU statistics collector and (optionally) disk statistics collector is started only when Zabbix */
        /* agentd is running as a daemon. When Zabbix agent or agentd is started with "-p" or "-t" parameter */
        /* the collectors are not available and keys "vfs.dev.read", "vfs.dev.write" with some parameters */
        /* (e.g. sps, ops) are not supported. */

        SET_MSG_RESULT(result, zbx_strdup(NULL, "This item is available only in daemon mode when collectors are"
            " started."));
        return SYSINFO_RET_FAIL;
    }

    // 获取设备名称或所有设备名称
    if (NULL == devname || '\0' == *devname || 0 == strcmp(devname, "all"))
    {
        *kernel_devname = '\0';
    }

	else if (SUCCEED != get_kernel_devname(devname, kernel_devname, sizeof(kernel_devname)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain device name used internally by the kernel."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (device = collector_diskdevice_get(kernel_devname)))
	{
		if (SUCCEED != get_diskstat(kernel_devname, dstats))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain disk information."));

			return SYSINFO_RET_FAIL;
		}

		if (NULL == (device = collector_diskdevice_add(kernel_devname)))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot add disk device to agent collector."));
			return SYSINFO_RET_FAIL;
		}
	}

	if (ZBX_DSTAT_TYPE_SPS == type)
		SET_DBL_RESULT(result, (ZBX_DEV_READ == rw ? device->r_sps[mode] : device->w_sps[mode]));
	else
		SET_DBL_RESULT(result, (ZBX_DEV_READ == rw ? device->r_ops[mode] : device->w_ops[mode]));

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *这段代码的主要目的是定义一个名为 VFS_DEV_READ 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。接着，调用另一个名为 vfs_dev_rw 的函数，传入三个参数：request、result 和 ZBX_DEV_READ，用于实现虚拟文件系统（VFS）中的读操作。整个函数的返回类型是 int。
 ******************************************************************************/
// 定义一个函数，名为 VFS_DEV_READ，接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 VFS_DEV_WRITE 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。然后调用另一个名为 vfs_dev_rw 的函数，传入三个参数：request、result 和 ZBX_DEV_WRITE，用于处理虚拟文件系统的设备写操作。最后，返回 vfs_dev_rw 函数的执行结果。
 ******************************************************************************/
// 定义一个函数 VFS_DEV_WRITE，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result
int	VFS_DEV_READ(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 vfs_dev_rw 函数，传入三个参数：request、result 和 ZBX_DEV_WRITE
    // 该函数主要用于处理虚拟文件系统的设备写操作
	return vfs_dev_rw(request, result, ZBX_DEV_READ);
}



int	VFS_DEV_WRITE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return vfs_dev_rw(request, result, ZBX_DEV_WRITE);
}
