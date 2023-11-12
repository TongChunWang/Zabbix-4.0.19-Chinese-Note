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

#ifndef _WINDOWS

#include "common.h"
#include "diskdevices.h"
#include "stats.h"
#include "log.h"
#include "mutexs.h"

extern zbx_mutex_t		diskstats_lock;
#define LOCK_DISKSTATS		zbx_mutex_lock(diskstats_lock)
#define UNLOCK_DISKSTATS	zbx_mutex_unlock(diskstats_lock)

/******************************************************************************
 * *
 *整个代码块的主要目的是对磁盘设备的读写数据进行统计，并计算各个时间段的平均速率。具体来说，这段代码做了以下事情：
 *
 *1. 初始化一些变量，包括整型变量i、时间类型数组clock和整型数组index。
 *2. 检查传入的device参数是否为空，确保不为空。
 *3. 增加device的索引。
 *4. 保存当前时间作为device的clock数组的当前元素。
 *5. 将dstat数组中的读取扇区、读取操作、读取字节、写入扇区、写入操作、写入字节分别保存到device的相应数组中。
 *6. 初始化clock数组和index数组。
 *7. 遍历device的clock数组，找到最近的时间戳，并根据最近时间戳更新各个平均值。
 *8. 保存各个平均值的读取、操作和字节速率。
 ******************************************************************************/
static void apply_diskstat(ZBX_SINGLE_DISKDEVICE_DATA *device, time_t now, zbx_uint64_t *dstat)
{
	// 定义一个函数，用于应用磁盘统计数据

	register int	i; // 定义一个整型变量i，用于循环计数
	time_t		clock[ZBX_AVG_COUNT], sec; // 定义一个时间类型数组clock，用于存储各个平均值的时间戳
	int		index[ZBX_AVG_COUNT]; // 定义一个整型数组index，用于存储各个平均值的索引

	// 确保传入的device参数不为空
	assert(device);

	// 增加device的索引
	device->index++;

	// 如果索引达到最大值，重新置为0
	if (MAX_COLLECTOR_HISTORY == device->index)
		device->index = 0;

	// 保存当前时间作为device的clock数组的当前元素
	device->clock[device->index] = now;
	// 保存dstat数组中的读取扇区、读取操作、读取字节、写入扇区、写入操作、写入字节到device的相应数组中
	device->r_sect[device->index] = dstat[ZBX_DSTAT_R_SECT];
	device->r_oper[device->index] = dstat[ZBX_DSTAT_R_OPER];
	device->r_byte[device->index] = dstat[ZBX_DSTAT_R_BYTE];
	device->w_sect[device->index] = dstat[ZBX_DSTAT_W_SECT];
	device->w_oper[device->index] = dstat[ZBX_DSTAT_W_OPER];
	device->w_byte[device->index] = dstat[ZBX_DSTAT_W_BYTE];

	// 初始化clock数组和index数组
	for (i = 0; i < ZBX_AVG_COUNT; i++)
	{
		clock[i] = now + 1;
		index[i] = -1;
	}

	// 遍历device的clock数组，找到最近的时间戳
	for (i = 0; i < MAX_COLLECTOR_HISTORY; i++)
	{
		if (0 == device->clock[i])
			continue;

		// 判断当前时间与各个平均值时间戳的关系，如果满足条件，则更新平均值
		DISKSTAT(1);
		DISKSTAT(5);
		DISKSTAT(15);
	}

	// 保存各个平均值的读取、操作和字节速率
	SAVE_DISKSTAT(1);
	SAVE_DISKSTAT(5);
	SAVE_DISKSTAT(15);
}


/******************************************************************************
 * *
 *这段代码的主要目的是处理磁盘统计数据。首先，获取当前时间，然后调用get_diskstat函数获取磁盘统计数据。如果获取成功，就调用apply_diskstat函数应用这些统计数据。最后，更新设备的数据采集次数。整个过程中，使用了静态函数和指针变量，以及时间戳和磁盘统计数据的相关操作。
 ******************************************************************************/
// 定义一个静态函数，用于处理磁盘统计数据
static void process_diskstat(ZBX_SINGLE_DISKDEVICE_DATA *device)
{
	// 定义一个时间变量，用于存储当前时间
	time_t now;
	// 定义一个数组，用于存储磁盘统计数据
	zbx_uint64_t dstat[ZBX_DSTAT_MAX];

	// 获取当前时间
	now = time(NULL);
	// 调用get_diskstat函数，获取磁盘统计数据，若失败则返回
	if (FAIL == get_diskstat(device->name, dstat))
		return;

	// 调用apply_diskstat函数，应用磁盘统计数据
	apply_diskstat(device, now, dstat);

	// 更新设备的数据采集次数
	device->ticks_since_polled++;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是收集磁盘设备的统计数据，并对长时间未被轮询的设备进行移除。具体步骤如下：
 *
 *1. 定义一个整型变量 i，用于循环遍历磁盘设备。
 *2. 加锁保护 diskstats 数据结构，防止多线程并发访问。
 *3. 重新挂载 diskstat_shm 共享内存，以便后续操作。
 *4. 遍历 diskdevices 结构体中的所有磁盘设备，并对每个设备处理统计数据。
 *5. 如果磁盘设备长时间没有被轮询（ticks_since_polled 大于等于 DISKDEVICE_TTL），则将其从收集器中移除。
 *6. 使用 memcpy 函数将移除的磁盘设备数据复制到后面一个设备的位置。
 *7. 减少收集器中的设备数量。
 *8. 解锁 diskstats，允许其他线程访问。
 ******************************************************************************/
void	collect_stats_diskdevices(void)
{
	// 定义一个整型变量 i，用于循环计数
	int	i;

	// 加锁保护 diskstats 数据结构，防止多线程并发访问
	LOCK_DISKSTATS;

	// 重新挂载 diskstat_shm 共享内存，以便后续操作
	diskstat_shm_reattach();

	// 遍历 diskdevices 结构体中的所有磁盘设备
	for (i = 0; i < diskdevices->count; i++)
	{
		// 处理每个磁盘设备的统计数据
		process_diskstat(&diskdevices->device[i]);

		// 如果磁盘设备长时间没有被轮询，将其从收集器中移除
		if (DISKDEVICE_TTL <= diskdevices->device[i].ticks_since_polled)
		{
			// 如果删除设备后，收集器中的设备数量大于等于 1
			if ((diskdevices->count - 1) > i)
			{
				// 使用 memcpy 函数将磁盘设备数据从原位置复制到后面一个设备的位置
				memcpy(diskdevices->device + i, diskdevices->device + i + 1,
					sizeof(ZBX_SINGLE_DISKDEVICE_DATA) * (diskdevices->count - i));
			}

			// 减少收集器中的设备数量
			diskdevices->count--;

			// 减少循环变量 i，以便继续处理下一个设备
			i--;
		}
	}

	// 解锁 diskstats，允许其他线程访问
	UNLOCK_DISKSTATS;
}


ZBX_SINGLE_DISKDEVICE_DATA	*collector_diskdevice_get(const char *devname)
{
	const char			*__function_name = "collector_diskdevice_get";
	int				i;
	ZBX_SINGLE_DISKDEVICE_DATA	*device = NULL;
/******************************************************************************
 * *
 *整个代码块的主要目的是查找指定的磁盘设备，并对其进行初始化。输出日志用于记录函数调用过程和找到的磁盘设备。
 ******************************************************************************/
# assert(devname); 声明一个字符串指针变量devname，并对其进行非空检查。

# zabbix_log(LOG_LEVEL_DEBUG, "In %s() devname:'%s'", __function_name, devname); 记录函数调用日志，输出函数名和devname。

# LOCK_DISKSTATS; 使用互斥锁保护磁盘统计数据。

# if (0 == DISKDEVICE_COLLECTOR_STARTED(collector)) 判断collector是否已启动，如果未启动，则执行以下操作：

#   diskstat_shm_init(); 初始化磁盘统计共享内存。

# else 否则，即collector已启动，执行以下操作：

#   diskstat_shm_reattach(); 重新连接磁盘统计共享内存。

# for (i = 0; i < diskdevices->count; i++) 遍历磁盘设备数组。

# if (0 == strcmp(devname, diskdevices->device[i].name)) 如果磁盘设备名与devname相同，执行以下操作：

#   device = &diskdevices->device[i]; 保存当前磁盘设备指针。

#   device->ticks_since_polled = 0; 将设备最近一次采集时间重置为0。

#   zabbix_log(LOG_LEVEL_DEBUG, "%s() device '%s' found", __function_name, devname); 记录日志，表示找到指定设备。

#   break; 跳出循环，结束查找。

# UNLOCK_DISKSTATS; 释放互斥锁。

# zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)device); 记录函数退出日志，输出函数名和device指针。

# return device; 返回找到的磁盘设备指针。


ZBX_SINGLE_DISKDEVICE_DATA	*collector_diskdevice_add(const char *devname)
{
	const char			*__function_name = "collector_diskdevice_add";
	ZBX_SINGLE_DISKDEVICE_DATA	*device = NULL;
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的 devname 参数，创建一个新的磁盘设备结构体，并将其添加到 diskdevices 链表中。在此过程中，会对磁盘设备进行一些初始化操作，如设置名称、索引等。最后，处理新的磁盘设备并返回其指针。
 ******************************************************************************/
// 断言，确保 devname 不会为空，如果为空则程序崩溃
assert(devname);

// 记录日志，表示函数开始执行，输出 devname 的值
zabbix_log(LOG_LEVEL_DEBUG, "In %s() devname:'%s'", __function_name, devname);

// 加锁，保护 diskstats 数据结构
LOCK_DISKSTATS;

// 判断 collector 是否已经启动
if (0 == DISKDEVICE_COLLECTOR_STARTED(collector))
{
    // 如果 collector 未启动，则初始化 diskstat_shm
    diskstat_shm_init();
}
else
{
    // 如果 collector 已启动，则重新附加 diskstat_shm
    diskstat_shm_reattach();
}

// 判断 diskdevices 是否已满
if (diskdevices->count == MAX_DISKDEVICES)
{
    // 如果已满，则日志记录并退出
    zabbix_log(LOG_LEVEL_DEBUG, "%s() collector is full", __function_name);
    goto end;
}

// 如果 diskdevices 已经达到最大数量，则扩展 diskstat_shm
if (diskdevices->count == diskdevices->max_diskdev)
    diskstat_shm_extend();

// 分配一个新的设备结构体并初始化
device = &(diskdevices->device[diskdevices->count]);
memset(device, 0, sizeof(ZBX_SINGLE_DISKDEVICE_DATA));
zbx_strlcpy(device->name, devname, sizeof(device->name));
device->index = -1;
device->ticks_since_polled = 0;
(diskdevices->count)++;

// 处理新的磁盘设备
process_diskstat(device);

// 解锁，结束函数
end:
    UNLOCK_DISKSTATS;

    // 记录日志，表示函数执行结束，输出 device 指针
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)device);

    // 返回新分配的 device 指针
    return device;
}


#endif	/* _WINDOWS */
