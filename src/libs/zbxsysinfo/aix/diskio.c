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

#define ZBX_DEV_PFX	"/dev/"

typedef struct
{
	zbx_uint64_t	nread;
	zbx_uint64_t	nwritten;
	zbx_uint64_t	reads;
	zbx_uint64_t	writes;
}
zbx_perfstat_t;
int	get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
	return FAIL;
}
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为get_diskstat的函数，该函数用于获取指定硬盘设备的名字和状态信息。在此函数中，并没有实际执行获取硬盘状态的操作，而是直接返回了一个错误代码，表示函数执行失败。可能的原因是该函数尚未实现获取硬盘状态的功能，或者存在一些必要的参数缺失。
 ******************************************************************************/
// 定义一个函数get_diskstat，接收两个参数：一个字符串指针devname（表示硬盘设备名称），和一个zbx_uint64_t类型的指针dstat（用于存储硬盘状态信息）
/******************************************************************************
 * *
 *整个代码块的主要目的是获取磁盘性能数据。它首先检查是否定义了HAVE_LIBPERFSTAT，如果定义了，则使用该库获取磁盘性能数据。如果未定义，则返回失败。
 *
 *对于有devname参数的情况，它调用perfstat_disk函数获取指定设备的磁盘性能数据。如果没有devname，则调用perfstat_disk_total函数获取系统级别的磁盘性能数据。
 *
 *获取到磁盘性能数据后，计算读取和写入的总量和次数，并将结果存储在zp结构体中。如果获取系统信息成功，返回SYSINFO_RET_OK，否则返回SYSINFO_RET_FAIL，并记录错误信息。
 ******************************************************************************/
static int	get_perfstat_io(const char *devname, zbx_perfstat_t *zp, char **error)
{
    // 如果定义了HAVE_LIBPERFSTAT，则使用该库
#if defined(HAVE_LIBPERFSTAT)
    int	err;

    // 如果devname不为空字符串
    if ('\0' != *devname)
    {
        // 初始化perfstat_id_t结构体的name变量
        perfstat_id_t	name;
        perfstat_disk_t	data;

        // 将devname复制到name结构的name字段中
        strscpy(name.name, devname);

        // 调用perfstat_disk函数获取磁盘性能数据
        if (0 < (err = perfstat_disk(&name, &data, sizeof(data), 1)))
        {
            // 计算读取和写入的总量
            zp->nread = data.rblks * data.bsize;
            zp->nwritten = data.wblks * data.bsize;

            // 计算读取和写入的次数
            zp->reads = data.xrate;
            zp->writes = data.xfers - data.xrate;

            // 返回成功
            return SYSINFO_RET_OK;
        }
    }
    // 如果devname为空字符串，则调用perfstat_disk_total函数获取系统级别的磁盘性能数据
    else
    {
        perfstat_disk_total_t	data;

        // 调用perfstat_disk_total函数获取系统级别的磁盘性能数据
        if (0 < (err = perfstat_disk_total(NULL, &data, sizeof(data), 1)))
        {
            // 计算读取和写入的总量，这里假设每块硬盘的容量为512字节
            zp->nread = data.rblks * 512;
            zp->nwritten = data.wblks * 512;

            // 计算读取和写入的次数
            zp->reads = data.xrate;
            zp->writes = data.xfers - data.xrate;

            // 返回成功
            return SYSINFO_RET_OK;
        }
    }

    // 如果err为0，表示没有错误，但无法获取系统信息
    if (0 == err)
        *error = zbx_strdup(NULL, "Cannot obtain system information.");
    // 否则，表示获取系统信息失败，记录错误信息
    else
        *error = zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno));

    // 返回失败
    return SYSINFO_RET_FAIL;
#else
    // 如果未定义HAVE_LIBPERFSTAT，则返回失败
    *error = zbx_strdup(NULL, "Agent was compiled without support for Perfstat API.");
    return SYSINFO_RET_FAIL;
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定设备的读字节数。函数 VFS_DEV_READ_BYTES 接收一个设备名称（字符串类型）和一个 AGENT_RESULT 类型的指针，用于存储查询结果。函数首先调用 get_perfstat_io 函数获取设备的性能统计信息，并将结果存储在 zp 变量中。然后将 zp 变量中的 nread 值设置为 AGENT_RESULT 结构体的 nread 成员，最后返回操作结果。如果操作失败，则设置错误信息并返回 SYSINFO_RET_FAIL；操作成功则返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_DEV_READ_BYTES，接收两个参数：一个字符串指针 devname，一个 AGENT_RESULT 类型的指针 result。
static int	VFS_DEV_READ_BYTES(const char *devname, AGENT_RESULT *result)
{
	// 定义一个 zbx_perfstat_t 类型的变量 zp，用于存储设备的性能统计信息。
	zbx_perfstat_t	zp;
	// 定义一个字符串指针 error，用于存储可能出现的错误信息。
	char		*error;

	// 调用 get_perfstat_io 函数，获取指定设备（通过 devname 参数）的性能统计信息，并将结果存储在 zp 变量中。
	if (SYSINFO_RET_OK != get_perfstat_io(devname, &zp, &error))
	{
		// 如果 get_perfstat_io 函数执行失败，设置 result 指向的 AGENT_RESULT 结构体的错误信息。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示操作失败。
		return SYSINFO_RET_FAIL;
	}

	// 设置 result 指向的 AGENT_RESULT 结构体的 nread 成员值为 zp.nread。
	SET_UI64_RESULT(result, zp.nread);

	// 返回 SYSINFO_RET_OK，表示操作成功。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是读取指定设备的性能统计数据（读操作次数），并将结果存储在 AGENT_RESULT 结构体中。具体步骤如下：
/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定设备（devname）的写入字节数。函数 VFS_DEV_WRITE_BYTES 接收两个参数，一个是设备名称 devname，另一个是用于存储结果的 AGENT_RESULT 类型的指针 result。首先调用 get_perfstat_io 函数获取设备的性能统计数据，并将结果存储在 zbx_perfstat_t 类型的变量 zp 中。然后将 zp 中的写入字节数设置到 result 中的 nwritten 字段。如果获取性能统计数据失败，则设置 result 中的错误信息并为操作失败返回 SYSINFO_RET_FAIL；如果操作成功，则返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_DEV_WRITE_BYTES，接收两个参数：一个字符串指针 devname，一个 AGENT_RESULT 类型的指针 result。
static int	VFS_DEV_READ_OPERATIONS(const char *devname, AGENT_RESULT *result)
{
	// 定义一个 zbx_perfstat_t 类型的变量 zp，用于存储性能统计数据。
	zbx_perfstat_t	zp;
	// 定义一个字符串指针 error，用于存储错误信息。
	char		*error;

	// 调用 get_perfstat_io 函数获取指定设备（devname）的性能统计数据，并将结果存储在 zp 变量中。
	if (SYSINFO_RET_OK != get_perfstat_io(devname, &zp, &error))
	{
		// 如果获取性能统计数据失败，设置 result 中的错误信息为 error。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示操作失败。
		return SYSINFO_RET_FAIL;

	}

	SET_UI64_RESULT(result, zp.reads);

	return SYSINFO_RET_OK;
}

static int	VFS_DEV_WRITE_BYTES(const char *devname, AGENT_RESULT *result)
{
	zbx_perfstat_t	zp;
	char		*error;

	if (SYSINFO_RET_OK != get_perfstat_io(devname, &zp, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, zp.nwritten);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取指定设备名称的写操作性能数据（如写操作次数），并将结果存储在传入的AGENT_RESULT结构体中。函数VFS_DEV_WRITE_OPERATIONS通过调用get_perfstat_io函数来获取设备的性能数据，如果获取失败，则设置错误信息到结果结构体中，并返回失败码；如果获取成功，则将写操作次数设置到结果结构体中，并返回成功码。
 ******************************************************************************/
// 定义一个静态函数，用于获取设备名称的写操作性能数据
static int	VFS_DEV_WRITE_OPERATIONS(const char *devname, AGENT_RESULT *result)
{
	// 定义一个zbx_perfstat_t类型的变量zp，用于存储设备的性能数据
	zbx_perfstat_t	zp;
	// 定义一个字符指针变量error，用于存储可能出现的错误信息
	char		*error;

	// 调用get_perfstat_io函数，获取设备名称的写操作性能数据
	if (SYSINFO_RET_OK != get_perfstat_io(devname, &zp, &error))
	{
		// 如果获取性能数据失败，设置错误信息到result结果结构体中
		SET_MSG_RESULT(result, error);
		// 返回错误码SYSINFO_RET_FAIL，表示获取性能数据失败
		return SYSINFO_RET_FAIL;
	}

	// 将获取到的写操作次数（zp.writes）设置到result结果结构体中
	SET_UI64_RESULT(result, zp.writes);

	// 返回成功码SYSINFO_RET_OK，表示获取性能数据成功
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个AGENT_REQUEST类型的请求和一个AGENT_RESULT类型的结果，根据请求中的参数调用不同的函数处理设备读取操作，并返回处理结果。
 ******************************************************************************/
// 定义一个函数VFS_DEV_READ，接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result
int VFS_DEV_READ(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符串指针，分别指向设备名称和设备类型
	const char *devname, *type;
	// 定义一个整型变量，用于存储函数返回值
	int ret;

	// 检查传入的参数个数，如果大于2，则返回错误信息
	if (2 < request->nparam)
	{
		// 设置返回结果为“参数过多”的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误码SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 获取传入的第一个参数（设备名称）
	devname = get_rparam(request, 0);

	// 如果设备名称不为空，且等于"all"，则将设备名称设为空字符串
	if (NULL == devname || 0 == strcmp("all", devname))
		devname = "";
	// 如果设备名称以ZBX_DEV_PFX开头，则将ZBX_DEV_PFX的长度添加到设备名称后面
	else if (0 == strncmp(ZBX_DEV_PFX, devname, ZBX_CONST_STRLEN(ZBX_DEV_PFX)))
		devname += ZBX_CONST_STRLEN(ZBX_DEV_PFX);

	// 获取传入的第二个参数（设备类型）
	type = get_rparam(request, 1);

	// 如果设备类型为空，或者等于"operations"，则调用VFS_DEV_READ_OPERATIONS函数处理
	if (NULL == type || '\0' == *type || 0 == strcmp(type, "operations"))
		ret = VFS_DEV_READ_OPERATIONS(devname, result);
	// 如果设备类型为"bytes"，则调用VFS_DEV_READ_BYTES函数处理
	else if (0 == strcmp(type, "bytes"))
		ret = VFS_DEV_READ_BYTES(devname, result);
	// 否则，返回错误信息
	else
	{
		// 设置返回结果为“无效的第二个参数”的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回错误码SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 返回处理结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个AGENT_REQUEST类型的request和一个AGENT_RESULT类型的result作为参数，根据请求的参数执行相应的操作。具体来说，根据传入的设备名称和类型，调用不同的函数处理请求。如果传入的参数不合法，返回错误信息。
 ******************************************************************************/
// 定义一个函数VFS_DEV_WRITE，接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result
int VFS_DEV_WRITE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义两个字符串指针，分别指向设备名称和类型
    const char *devname, *type;
    // 定义一个整型变量，用于存储函数返回值
    int ret;

    // 检查传入的参数个数，如果大于2，则返回错误信息
    if (2 < request->nparam)
    {
        // 设置返回结果为“参数过多”的错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        // 返回失败状态码
        return SYSINFO_RET_FAIL;
    }

    // 获取传入的第一个参数（设备名称）
    devname = get_rparam(request, 0);

    // 如果设备名称不为空，且等于"all"，则清空设备名称
    if (NULL == devname || 0 == strcmp("all", devname))
        devname = "";
    // 如果设备名称以ZBX_DEV_PFX开头，则去掉ZBX_DEV_PFX部分
    else if (0 == strncmp(ZBX_DEV_PFX, devname, ZBX_CONST_STRLEN(ZBX_DEV_PFX)))
        devname += ZBX_CONST_STRLEN(ZBX_DEV_PFX);

    // 获取传入的第二个参数（设备类型）
    type = get_rparam(request, 1);

    // 如果类型不为空，且等于"operations"，则调用VFS_DEV_WRITE_OPERATIONS函数
    if (NULL == type || '\0' == *type || 0 == strcmp(type, "operations"))
        ret = VFS_DEV_WRITE_OPERATIONS(devname, result);
    // 如果类型不为空，且等于"bytes"，则调用VFS_DEV_WRITE_BYTES函数
    else if (0 == strcmp(type, "bytes"))
        ret = VFS_DEV_WRITE_BYTES(devname, result);
    // 如果类型不为空，但不是"operations"或"bytes"，则返回错误信息
    else
    {
        // 设置返回结果为“无效的第二个参数”错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 返回失败状态码
        return SYSINFO_RET_FAIL;
    }

    // 返回处理结果
    return ret;
}

