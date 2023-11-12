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
#include "log.h"

#include "zbxalgo.h"
#include "vectorimpl.h"
/******************************************************************************
 * /
 ******************************************************************************/

// 1. 定义了一系列数据结构，如zbx_uint64_t、char*、void*等；
// 2. 定义了释放内存的函数zbx_ptr_free。

ZBX_VECTOR_IMPL(uint64, zbx_uint64_t)
// 这行代码定义了一个名为zbx_uint64_t的类型，用于表示一个无符号64位整数。同时，它还定义了一个名为ZBX_VECTOR_IMPL的宏，用于实现一个vector数据结构。

ZBX_PTR_VECTOR_IMPL(str, char *)
// 这行代码定义了一个名为char*的类型，用于表示一个字符指针。同时，它还定义了一个名为ZBX_PTR_VECTOR_IMPL的宏，用于实现一个ptr_vector数据结构。

ZBX_PTR_VECTOR_IMPL(ptr, void *)
// 这行代码定义了一个名为void*的类型，用于表示一个通用指针。同时，它还定义了一个名为ZBX_PTR_VECTOR_IMPL的宏，用于实现一个ptr_vector数据结构。

ZBX_VECTOR_IMPL(ptr_pair, zbx_ptr_pair_t)
// 这行代码定义了一个名为zbx_ptr_pair_t的类型，用于表示一个包含两个指针的pair结构。同时，它还定义了一个名为ZBX_VECTOR_IMPL的宏，用于实现一个vector数据结构。

ZBX_VECTOR_IMPL(uint64_pair, zbx_uint64_pair_t)
// 这行代码定义了一个名为zbx_uint64_pair_t的类型，用于表示一个包含两个无符号64位整数的pair结构。同时，它还定义了一个名为ZBX_VECTOR_IMPL的宏，用于实现一个vector数据结构。

void	zbx_ptr_free(void *data)
{
	zbx_free(data);
}
// 这行代码定义了一个名为zbx_ptr_free的函数，它接受一个void*类型的参数data，用于释放该指针指向的内存。函数内部调用zbx_free函数来完成内存释放操作。
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个名为zbx_str_free的函数，用于释放传入的char类型指针data所指向的内存空间。
 *
 *注释详细解释如下：
 *
 *1. `void zbx_str_free(char *data)`：定义一个函数zbx_str_free，参数为一个char类型指针data。这里的void表示该函数不返回任何值，zbx_str_free是函数的名称，后面的括号内是函数的参数，即一个char类型的指针data。
 *
 *2. `zbx_free(data)`：调用zbx_free函数，用于释放data指向的内存空间。这里的zbx_free是一个外部函数，用于动态分配内存的释放。通过这个函数，我们可以确保在程序运行过程中不再使用已经分配的内存，避免内存泄漏。
 *
 *整个代码块的作用是提供一个便捷的函数，用于在不需要使用char类型指针data所指向的内存空间时，将其正确释放。这样可以确保程序在运行过程中对内存的有效管理，提高程序的稳定性和可靠性。
 ******************************************************************************/
// 定义一个函数zbx_str_free，参数为一个char类型指针data
void	zbx_str_free(char *data)
{
    // 使用zbx_free函数释放data指向的内存空间
    zbx_free(data);
}

