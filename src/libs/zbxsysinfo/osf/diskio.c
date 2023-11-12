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
// 定义一个名为 get_diskstat 的函数，该函数接收两个参数：
// 第一个参数是一个指向字符串常量（const char *）的指针，表示硬盘设备名称；
// 第二个参数是一个指向 zbx_uint64_t 类型的指针，用于存储硬盘状态信息。
int get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
	return FAIL;
}

