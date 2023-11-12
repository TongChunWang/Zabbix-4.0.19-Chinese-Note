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

typedef struct
{
	zbx_uint64_t	nread;
	zbx_uint64_t	nwritten;
	zbx_uint64_t	reads;
	zbx_uint64_t	writes;
}
zbx_kstat_t;
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为get_diskstat的函数，该函数用于获取指定硬盘设备的名称为devname的硬盘状态信息，并将结果存储在zbx_uint64_t类型的指针dstat所指向的内存区域。如果函数执行失败，返回一个错误代码。
 ******************************************************************************/
// 定义一个函数get_diskstat，接收两个参数：一个字符串指针devname（表示硬盘设备名称），和一个zbx_uint64_t类型的指针dstat（用于存储硬盘状态信息）
int get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
	return FAIL;
}

/******************************************************************************
 * *
 *这段代码的主要目的是获取内核统计信息中的磁盘I/O数据，并将结果存储在`zbx_kstat_t`结构体中。函数通过遍历内核统计项链表，查找并读取符合条件的内核统计数据。如果name参数不为空，则只处理指定的内核统计项；如果为空，则处理所有IO类型的内核统计项。在获取数据过程中，遇到错误情况会记录错误信息并返回失败。成功获取数据后，返回OK。
 ******************************************************************************/
// 定义一个静态函数，用于获取内核统计信息中的磁盘I/O数据
static int get_kstat_io(const char *name, zbx_kstat_t *zk, char **error)
{
	// 初始化返回值，表示失败
	int ret = SYSINFO_RET_FAIL;
	// 申请一个kstat_ctl结构体的指针
	kstat_ctl_t *kc;
	// 申请一个kstat结构体的指针
	kstat_t *kt;
	// 定义一个kstat_io结构体用于存储读写数据
	kstat_io_t kio;

	// 尝试打开内核统计设施
	if (NULL == (kc = kstat_open()))
	{
		// 打开失败，记录错误信息并返回失败
		*error = zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s", zbx_strerror(errno));
		return ret;
	}

	// 如果name不为空，则进行以下操作：
	if ('\0' != *name)
	{
		// 查找指定的内核统计项
		if (NULL == (kt = kstat_lookup(kc, NULL, -1, (char *)name)))
		{
			// 查找失败，记录错误信息并返回失败
			*error = zbx_dsprintf(NULL, "Cannot look up in kernel statistics facility: %s",
					zbx_strerror(errno));
			goto clean;
		}

		// 检查内核统计项类型是否为IO
		if (KSTAT_TYPE_IO != kt->ks_type)
		{
			// 类型错误，记录错误信息并返回失败
			*error = zbx_strdup(NULL, "Information looked up in kernel statistics facility"
					" is of the wrong type.");
			goto clean;
		}

		// 读取内核统计数据
		if (-1 == kstat_read(kc, kt, &kio))
		{
			// 读取失败，记录错误信息并返回失败
			*error = zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s",
					zbx_strerror(errno));
			goto clean;
		}

		// 存储读写数据到zk结构体中
		zk->nread = kio.nread;
		zk->nwritten = kio.nwritten;
		zk->reads = kio.reads;
		zk->writes = kio.writes;
	}
	else
	{
		// 如果name为空，则清空zk结构体，并遍历所有IO类型的内核统计项
		memset(zk, 0, sizeof(*zk));

		for (kt = kc->kc_chain; NULL != kt; kt = kt->ks_next)
		{
			// 检查内核统计项类型是否为IO且类别为"disk"
			if (KSTAT_TYPE_IO == kt->ks_type && 0 == strcmp("disk", kt->ks_class))
			{
				// 读取内核统计数据
				if (-1 == kstat_read(kc, kt, &kio))
				{
					// 读取失败，记录错误信息并返回失败
					*error = zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s",
							zbx_strerror(errno));
					goto clean;
				}

				// 累计读写数据
				zk->nread += kio.nread;
				zk->nwritten += kio.nwritten;
				zk->reads += kio.reads;
				zk->writes += kio.writes;
			}
		}
	}

	// 成功获取内核统计数据，返回OK
	ret = SYSINFO_RET_OK;

clean:
	// 关闭内核统计设施
	kstat_close(kc);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定设备（通过 devname 参数指定）的读取字节数（nread）的统计信息，并将结果存储在 result 指向的 AGENT_RESULT 结构体中。如果获取设备统计信息失败，则设置结果的错误信息并为 SYSINFO_RET_FAIL，否则设置结果的 nread 成员为设备读取字节数，并返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_DEV_READ_BYTES，接收两个参数：一个字符串指针 devname，一个 AGENT_RESULT 类型的指针 result。
static int	VFS_DEV_READ_BYTES(const char *devname, AGENT_RESULT *result)
{
	// 定义一个 zbx_kstat_t 类型的结构体变量 zk，用于存储统计信息。
	zbx_kstat_t	zk;
	// 定义一个字符串指针 error，用于存储错误信息。
	char		*error;

	// 判断 get_kstat_io 函数的返回值是否为 SYSINFO_RET_OK，如果不是，则表示获取设备统计信息失败。
	if (SYSINFO_RET_OK != get_kstat_io(devname, &zk, &error))
	{
		// 设置 result 指向的 AGENT_RESULT 结构体的错误信息为 error。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示获取设备统计信息失败。
		return SYSINFO_RET_FAIL;
	}

	// 设置 result 指向的 AGENT_RESULT 结构体的 nread 成员为 zk.nread。
	SET_UI64_RESULT(result, zk.nread);

	// 返回 SYSINFO_RET_OK，表示获取设备统计信息成功。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从一个设备名称（devname）中读取设备的读操作次数，并将结果存储在result变量中。为实现这个目的，代码首先定义了一个zbx_kstat_t类型的结构体变量zk用于存储从内核获取的设备统计信息，然后判断是否成功获取到设备统计信息。如果成功，将读操作次数设置到result中，并返回SYSINFO_RET_OK表示成功。如果出现错误，设置错误信息到result的结果中，并返回SYSINFO_RET_FAIL表示失败。
 ******************************************************************************/
// 定义一个静态函数，用于读取设备的读操作次数
static int	VFS_DEV_READ_OPERATIONS(const char *devname, AGENT_RESULT *result)
{
	// 定义一个zbx_kstat_t类型的结构体变量zk，用于存储从内核获取的设备统计信息
	zbx_kstat_t	zk;
	// 定义一个字符指针变量error，用于存储可能出现的错误信息
	char		*error;

	// 判断是否成功从内核获取设备统计信息
	if (SYSINFO_RET_OK != get_kstat_io(devname, &zk, &error))
	{
		// 如果出现错误，设置错误信息到result的结果中，并返回SYSINFO_RET_FAIL表示失败
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 设置result中的读操作次数为zk.reads
	SET_UI64_RESULT(result, zk.reads);

	// 如果没有错误，返回SYSINFO_RET_OK表示成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是处理一个模式函数请求。该函数接收三个参数：一个AGENT_REQUEST结构体指针（包含请求信息）、一个AGENT_RESULT结构体指针（用于存储结果信息）和一个MODE_FUNCTION结构体指针（包含模式函数信息）。函数首先检查请求参数的数量，如果超过2个，则返回错误信息。接着从请求参数中获取设备名和模式，然后遍历模式函数数组，找到匹配的模式。如果找到匹配的模式，就调用对应的函数并传入设备名和结果指针，否则返回错误信息。
 ******************************************************************************/
// 定义一个静态函数，用于处理模式函数请求
static int	VFS_DEV_WRITE_BYTES(const char *devname, AGENT_RESULT *result)
{
	zbx_kstat_t	zk;
	char		*error;

	if (SYSINFO_RET_OK != get_kstat_io(devname, &zk, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, zk.nwritten);

	return SYSINFO_RET_OK;
}

/*
 *1. 定义一个zbx_kstat_t类型的结构体变量zk，用于存储设备统计信息。
 *2. 定义一个字符指针变量error，用于存储错误信息。
 *3. 调用get_kstat_io函数获取设备统计信息，并将结果存储在zk变量中。如果获取失败，设置result的错误信息为error，并返回SYSINFO_RET_FAIL。
 *4. 将zk变量中的writes字段设置为result的writes字段。
 *5. 返回SYSINFO_RET_OK，表示获取设备统计信息成功。
 ******************************************************************************/
// 定义一个静态函数，用于获取设备（devname）的写操作统计信息
static int	VFS_DEV_WRITE_OPERATIONS(const char *devname, AGENT_RESULT *result)
{
	// 定义一个zbx_kstat_t类型的结构体变量zk，用于存储设备统计信息
	zbx_kstat_t	zk;
	// 定义一个字符指针变量error，用于存储错误信息
	char		*error;

	// 判断get_kstat_io函数返回值是否为SYSINFO_RET_OK，如果不是，则表示获取设备统计信息失败
	if (SYSINFO_RET_OK != get_kstat_io(devname, &zk, &error))
	{
		// 设置result的错误信息为error
		SET_MSG_RESULT(result, error);
		// 返回SYSINFO_RET_FAIL，表示获取设备统计信息失败
		return SYSINFO_RET_FAIL;
	}

	// 设置result的writes字段为zk.writes（设备的写操作次数）
	SET_UI64_RESULT(result, zk.writes);

	// 返回SYSINFO_RET_OK，表示获取设备统计信息成功
	return SYSINFO_RET_OK;
}


static int	process_mode_function(AGENT_REQUEST *request, AGENT_RESULT *result, const MODE_FUNCTION *fl)
{
	const char	*devname, *mode;
	int		i;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	devname = get_rparam(request, 0);

	if (NULL == devname || 0 == strcmp("all", devname))
		devname = "";

	mode = get_rparam(request, 1);

	if (NULL == mode || '\0' == *mode)
		mode = "bytes";

	for (i = 0; NULL != fl[i].mode; i++)
	{
		if (0 == strcmp(mode, fl[i].mode))
			return (fl[i].function)(devname, result);
	}

	SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));

	return SYSINFO_RET_FAIL;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 VFS_DEV_READ 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。在该函数中，定义了一个常量数组 MODE_FUNCTION，用于存储模式函数列表。数组中包含两个元素，第一个元素是一个字符串，表示模式字符串；第二个元素是该模式对应的函数入口地址。接着调用 process_mode_function 函数，传入三个参数：request、result 和 fl 数组，用于处理模式函数列表。最终返回 process_mode_function 函数的执行结果。
 ******************************************************************************/
// 定义一个函数，名为 VFS_DEV_READ，接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_DEV_READ(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个常量数组，存储模式函数列表。数组中包含两个元素，第一个元素是一个字符串，表示模式字符串；第二个元素是该模式对应的函数入口地址。
	const MODE_FUNCTION	fl[] =
	{
		// 第一个元素是一个字符串，表示模式字符串，这里为 "bytes"
		{"bytes",	VFS_DEV_READ_BYTES},
		// 第二个元素是该模式对应的函数入口地址，这里为 VFS_DEV_READ_OPERATIONS
		{"operations",	VFS_DEV_READ_OPERATIONS},
		// 第三个元素为 NULL，表示模式列表的结束
		{NULL,		NULL}
	};

	// 调用 process_mode_function 函数，传入三个参数：request、result 和 fl 数组，用于处理模式函数列表。
	return process_mode_function(request, result, fl);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个 VFS_DEV_WRITE 函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。在函数内部，首先定义了一个常量数组 fl，用于存储 VFS 设备的写操作模式。然后，根据 request 中的模式参数，调用 process_mode_function 函数处理相应的操作。最终返回处理结果。
 ******************************************************************************/
// 定义一个函数 VFS_DEV_WRITE，接收两个参数，一个是 AGENT_REQUEST 类型的指针 request，另一个是 AGENT_RESULT 类型的指针 result。
int VFS_DEV_WRITE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个常量数组，存储 VFS 设备的写操作模式
	const MODE_FUNCTION	fl[] =
	{
		// 添加第一个模式，模式字符串为 "bytes"，对应的功能是 VFS_DEV_WRITE_BYTES
		{"bytes", 	VFS_DEV_WRITE_BYTES},
		// 添加第二个模式，模式字符串为 "operations"，对应的功能是 VFS_DEV_WRITE_OPERATIONS
		{"operations", 	VFS_DEV_WRITE_OPERATIONS},
		// 添加第三个模式，模式字符串为 NULL，对应的功能为 NULL
		{NULL,		NULL}
	};

	// 调用 process_mode_function 函数，根据 request 中的模式参数，处理相应的操作
	return process_mode_function(request, result, fl);
}

