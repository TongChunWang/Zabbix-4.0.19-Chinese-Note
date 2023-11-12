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
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 get_diskstat 的函数，该函数用于获取指定硬盘设备的统计信息，并将统计数据存储在 dstat 指针所指向的 zbx_uint64_t 类型的变量中。如果函数执行失败，返回一个常量值 FAIL。
 ******************************************************************************/
// 定义一个名为 get_diskstat 的函数，该函数接收两个参数：
// 参数1：一个字符串指针 devname，表示硬盘设备名称；
// 参数2：一个 zbx_uint64_t 类型的指针 dstat，用于存储硬盘统计信息。
int get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
    // 返回一个常量值 FAIL，表示函数执行失败。
    return FAIL;
}

