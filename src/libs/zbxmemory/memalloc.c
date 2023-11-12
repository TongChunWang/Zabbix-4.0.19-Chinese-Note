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
#include "mutexs.h"
#include "log.h"

#include "memalloc.h"

/******************************************************************************
 *                                                                            *
 *                     Some information on memory layout                      *
 *                  ---------------------------------------                   *
 *                                                                            *
 *                                                                            *
/******************************************************************************
 * *
 *这个代码块主要目的是实现一个简单的内存管理器，用于管理一块连续的内存区域。内存管理器遵循以下规则：
 *
 *1. 内存块分为已使用和空闲两种状态。
 *2. 空闲内存块存储在双向链表中，链表节点包含指向下一个空闲块和上一个空闲块的指针。
 *3. 当分配一个新的内存块时，首先查找链表中是否有合适大小的空闲块，如果有，则分配该块并将其从链表中移除。如果没有，则创建一个新的内存块，将其插入到链表中。
 *4. 释放已使用的内存块时，将其从链表中移除。
 *
 *整个代码块主要由以下几个函数组成：
 *
 *1. 内存管理初始化函数 `mem_init`：初始化内存管理器，创建一个双向链表用于存储空闲内存块。
 *2. 分配内存函数 `mem_alloc`：根据给定的大小分配内存，首先查找链表中是否有合适大小的空闲块，如果有，则分配该块并将其从链表中移除。如果没有，则创建一个新的内存块，将其插入到链表中。
 *3. 重新分配内存函数 `mem_realloc`：根据给定的大小重新分配内存，先释放原有内存块，然后分配新的内存块。
 *4. 释放内存函数 `mem_free`：释放已使用的内存块。
 *
 *此外，还包括一些辅助函数，如 `ALIGN4`、`ALIGN8`、`ALIGNPTR` 等，用于对指针进行对齐操作。
 ******************************************************************************/
/* 定义内存块结构体，包含一个连续的内存块，可以是空闲或已使用 */
typedef struct
{
    zbx_uint64_t size;         /* 内存块大小 */
    zbx_uint64_t flags;        /* 内存块状态，MEM_FLG_USED表示已使用 */
    void *prev;               /* 指向上一个内存块的指针 */
    void *next;               /* 指向下一个内存块的指针 */
} chunk_t;

/* 内存管理相关函数 */
static void *ALIGN4(void *ptr);
static void *ALIGN8(void *ptr);
static void *ALIGNPTR(void *ptr);

static zbx_uint64_t mem_proper_alloc_size(zbx_uint64_t size);
static int mem_bucket_by_size(zbx_uint64_t size);

static void mem_set_chunk_size(void *chunk, zbx_uint64_t size);
static void mem_set_used_chunk_size(void *chunk, zbx_uint64_t size);

static void *mem_get_prev_chunk(void *chunk);
static void mem_set_prev_chunk(void *chunk, void *prev);
static void *mem_get_next_chunk(void *chunk);
static void mem_set_next_chunk(void *chunk, void *next);
static void **mem_ptr_to_prev_field(void *chunk);
static void **mem_ptr_to_next_field(void *chunk, void **first_chunk);

static void mem_link_chunk(zbx_mem_info_t *info, void *chunk);
static void mem_unlink_chunk(zbx_mem_info_t *info, void *chunk);

static void *__mem_malloc(zbx_mem_info_t *info, zbx_uint64_t size);
static void *__mem_realloc(zbx_mem_info_t *info, void *old, zbx_uint64_t size);
static void __mem_free(zbx_mem_info_t *info, void *ptr);

/* 内存块最小大小和最大大小 */
#define MEM_MIN_SIZE		__UINT64_C(128)
#define MEM_MAX_SIZE		__UINT64_C(0x1000000000)	/* 64 GB */

/* 内存分配最小尺寸，必须是8的倍数且至少为（2 * ZBX_PTR_SIZE）*/
#define MEM_MIN_ALLOC	24

/* 内存块大小分桶，根据大小进行分类管理 */
#define MEM_MIN_BUCKET_SIZE	MEM_MIN_ALLOC
#define MEM_MAX_BUCKET_SIZE	256 /* 从这个大小开始，所有空闲块将被放入同一个桶中 */
#define MEM_BUCKET_COUNT	((MEM_MAX_BUCKET_SIZE - MEM_MIN_BUCKET_SIZE) / 8 + 1)

/* 辅助函数 */
static void *ALIGN4(void *ptr)
{
	return (void *)((uintptr_t)((char *)ptr + 3) & (uintptr_t)~3);
}

/*  aligns the pointer to the nearest multiple of 8 */
static void *ALIGN8(void *ptr)
{
	return (void *)((uintptr_t)((char *)ptr + 7) & (uintptr_t)~7);
}

/*  aligns the pointer to the nearest multiple of ZBX_PTR_SIZE (4 or 8) */
static void *ALIGNPTR(void *ptr)
{
	return (void *)((uintptr_t)((char *)ptr + (ZBX_PTR_SIZE - 1)) & (uintptr_t)~(ZBX_PTR_SIZE - 1));
}

/* 设置内存块大小 */
static void mem_set_chunk_size(void *chunk, zbx_uint64_t size)
{
	*(zbx_uint64_t *)chunk = size | MEM_FLG_USED;
}

/* 设置已使用的内存块大小 */
static void mem_set_used_chunk_size(void *chunk, zbx_uint64_t size)
{
	*(zbx_uint64_t *)chunk = size | MEM_FLG_USED;
}

/* 获取上一个内存块 */
static void *mem_get_prev_chunk(void *chunk)
{
	return (void *)((char *)chunk - sizeof(zbx_uint64_t));
}

/* 设置上一个内存块 */
static void mem_set_prev_chunk(void *chunk, void *prev)
{
	void *ptr = (void *)((char *)chunk - sizeof(zbx_uint64_t));
	*(void **)ptr = prev;
}

/* 获取下一个内存块 */
static void *mem_get_next_chunk(void *chunk)
{
	return (void *)((char *)chunk + sizeof(zbx_uint64_t));
}

/* 设置下一个内存块 */
static void mem_set_next_chunk(void *chunk, void *next)
{
	void *ptr = (void *)((char *)chunk + sizeof(zbx_uint64_t));
	*(void **)ptr = next;
}

/* 获取内存块大小 */
static zbx_uint64_t mem_get_chunk_size(void *chunk)
{
	return *(zbx_uint64_t *)chunk & ~MEM_FLG_USED;
}

/* 检查内存块是否为空闲 */
static bool is_free_chunk(void *chunk)
{
	return !(*(zbx_uint64_t *)chunk & MEM_FLG_USED);
}

/* 内存管理函数 */
static zbx_mem_info_t *mem_info;

/* 初始化内存管理 */
void mem_init(void)
{
	mem_info = zbx_calloc(1, sizeof(zbx_mem_info_t));
	mem_info->first_chunk = NULL;
	mem_info->lo_bound = MEM_MIN_SIZE;
	mem_info->hi_bound = MEM_MAX_SIZE;
}

/* 释放内存 */
void mem_free(void *ptr)
{
	if (ptr == NULL)
		return;

	zbx_free(ptr);
}

/* 分配内存 */
void *mem_alloc(zbx_uint64_t size)
{
	if (size == 0)
		return NULL;

	size = mem_proper_alloc_size(size);

	void *ptr = NULL;

	/* 查找合适大小的内存块 */
	ptr = __mem_find_chunk(mem_info, size);

	if (ptr == NULL)
	{
		ptr = __mem_alloc_new_chunk(mem_info, size);
		if (ptr == NULL)
			return NULL;

		/* 将新分配的内存块插入到合适的位置 */
		mem_link_chunk(mem_info, ptr);
	}

	mem_set_chunk_size(ptr, size);

	return ptr;
}

/* 重新分配内存 */
void *mem_realloc(zbx_uint64_t *old_ptr, zbx_uint64_t size)
{
	void *new_ptr;

	if (*old_ptr == NULL)
		return NULL;

	new_ptr = mem_alloc(size);
	if (new_ptr == NULL)
		return NULL;

	memcpy(new_ptr, old_ptr, *old_ptr);

	zbx_free(*old_ptr);

	return new_ptr;
}



/******************************************************************************
 * *
 *这段代码的主要目的是实现一个内存对齐函数 ALIGN8，该函数接受一个 void 类型的指针作为输入，将其内存地址进行对齐，然后返回对齐后的指针。在这里，我们对指针进行内存对齐是为了保证程序的稳定性和性能，因为某些硬件和编译器可能对特定地址有特殊处理。
 *
 *函数 ALIGN8 首先获取输入指针的内存地址，然后计算指针地址加上 7 的值。接下来，我们使用位运算符 & 计算该值与 7 的按位与结果，这样就可以得到一个对齐后的地址。最后，我们将对齐后的地址转换为 void 类型的指针并返回。
/******************************************************************************
 * *
 *整个代码块的主要目的是对传入的 void 指针进行对齐操作，根据指定的对齐值（4或8字节）使指针符合特定的对齐要求。代码通过判断 ZBX_PTR_SIZE 的值来选择使用 ALIGN4 还是 ALIGN8 函数进行对齐，如果 ZBX_PTR_SIZE 不是4或8，则触发错误。
 ******************************************************************************/
/**
 * ALIGNPTR 函数：该函数用于对传入的 void 指针进行对齐操作。
 * 参数：void *ptr，需要进行对齐的 void 指针。
 * 返回值：对齐后的 void 指针。
 * 主要目的：根据指定的对齐值，对 void 指针进行对齐，使其符合特定的对齐要求。
 * 注释：
 * 1. 首先，判断 ZBX_PTR_SIZE 的值，即指定的大小。
 * 2. 如果 ZBX_PTR_SIZE 为 4，那么使用 ALIGN4 函数对 void 指针进行 4 字节对齐。
 * 3. 如果 ZBX_PTR_SIZE 为 8，那么使用 ALIGN8 函数对 void 指针进行 8 字节对齐。
 * 4. 如果 ZBX_PTR_SIZE 既不是 4 也不是 8，那么 assert(0) 触发错误，因为不支持其他对齐值。
 */
static void *ALIGNPTR(void *ptr)
{
	// 判断 ZBX_PTR_SIZE 的值，即指定的大小
	if (4 == ZBX_PTR_SIZE)
		// 如果 ZBX_PTR_SIZE 为 4，那么使用 ALIGN4 函数对 void 指针进行 4 字节对齐
		return ALIGN4(ptr);
	else if (8 == ZBX_PTR_SIZE)
		// 如果 ZBX_PTR_SIZE 为 8，那么使用 ALIGN8 函数对 void 指针进行 8 字节对齐
		return ALIGN8(ptr);
	else
		// 如果 ZBX_PTR_SIZE 既不是 4 也不是 8，触发错误
		assert(0);

	// 以下部分为空，因为已经根据 ZBX_PTR_SIZE 进行了相应的对齐操作，无需其他操作
}

    uintptr_t aligned_address = (uintptr_t)((char *)ptr + 7) & (uintptr_t)~7;

    // 返回对齐后的地址（即 void 类型的指针）
    return (void *)(aligned_address);
}


static void	*ALIGNPTR(void *ptr)
{
	if (4 == ZBX_PTR_SIZE)
		return ALIGN4(ptr);
	if (8 == ZBX_PTR_SIZE)
		return ALIGN8(ptr);
	assert(0);
}

static zbx_uint64_t	mem_proper_alloc_size(zbx_uint64_t size)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是分配内存块。其中，`mem_allocate()`函数用于根据用户请求的内存大小进行分配，如果大小可以被8整除，则分配大小加上7，以8字节为最小单位分配内存；否则，返回最小的内存分配大小。`mem_bucket_by_size()`函数用于根据内存大小分配内存块，如果大小在最小内存块大小和最大内存块大小之间，则返回大小除以8的商加1；否则，返回内存块的数量减1，即分配最后一个内存块。
 ******************************************************************************/
// 定义一个函数，用于分配内存大小
static int mem_allocate(zbx_uint64_t size)
{
	// 判断分配的大小是否大于等于最小的内存分配大小（8字节）
	if (size >= MEM_MIN_ALLOC)
	{
		// 如果大小可以被8整除，则返回大小加上7，以8字节为最小单位分配内存
		return size + ((8 - (size & 7)) & 7);
	}
	// 否则，返回最小的内存分配大小
	else
	{
		return MEM_MIN_ALLOC;
	}
}

// 定义一个静态函数，用于根据内存大小分配内存块
static int mem_bucket_by_size(zbx_uint64_t size)
{
	// 判断大小是否小于最小的内存块大小
	if (size < MEM_MIN_BUCKET_SIZE)
	{
		// 如果大小在最小内存块大小和最大内存块大小之间，则返回大小除以8的商加1
		return (size - MEM_MIN_BUCKET_SIZE) >> 3;
	}
	// 否则，返回内存块的数量减1，即分配最后一个内存块
	else
	{
		return MEM_BUCKET_COUNT - 1;
	}
}


/******************************************************************************
 * *
 *整个代码块的主要目的是设置一个内存块的大小。函数 `mem_set_chunk_size` 接受一个内存块指针和一个大小参数，然后将大小存储到内存块的起始地址处，接着计算起始地址加上一个未知字段（MEM_SIZE_FIELD）的大小再加上大小，将这个地址处的值也设置为大小。
 ******************************************************************************/
// 定义一个名为 mem_set_chunk_size 的静态函数，该函数接受两个参数：
// 第一个参数是一个指针（void *chunk），它指向一个内存块；
// 第二个参数是一个 zbx_uint64_t 类型的值，表示该内存块的大小。
static void mem_set_chunk_size(void *chunk, zbx_uint64_t size)
{
	// 将第二个参数 size 存储到第一个参数 chunk 所指向的内存地址处。
	*(zbx_uint64_t *)chunk = size;

	// 计算 chunk 指向的内存地址加上 MEM_SIZE_FIELD 字段大小再加上 size 的值，
	// 将这个地址处的值设置为 size。MEM_SIZE_FIELD 是一个未知的字段，这里假设它是一个整数。
	*(zbx_uint64_t *)((char *)chunk + MEM_SIZE_FIELD + size) = size;
}


/******************************************************************************
 * *
 *这块代码的主要目的是设置内存块的已使用状态和大小。函数`mem_set_used_chunk_size`接收两个参数，一个是内存块的起始地址（`chunk`），另一个是要设置的内存块大小（`size`）。函数内部首先将内存块的第一个字段设置为`MEM_FLG_USED`标志和大小，接着将内存块的第二个字段（偏移量为`MEM_SIZE_FIELD+size`）设置为`MEM_FLG_USED`标志和大小。这样就完成了对内存块的设置。
 ******************************************************************************/
// 定义一个静态函数，用于设置内存块的已使用状态和大小
static void mem_set_used_chunk_size(void *chunk, zbx_uint64_t size)
/******************************************************************************
 * *
 *这块代码的主要目的是获取给定内存块（chunk）的前一个块的指针。函数接受一个void类型的指针作为参数，该指针表示一个内存块。通过计算内存块大小字段的位置，获取内存块的大小，然后计算前一个块的起始地址，并返回该地址。
 ******************************************************************************/
// 定义一个静态函数，用于获取内存块的前一个块的指针
static void *mem_get_prev_chunk(void *chunk)
{
    // 转换为字符指针
    char *char_chunk = (char *)chunk;

    // 计算内存块大小字段的位置
    char *mem_size_field = char_chunk + MEM_SIZE_FIELD;

    // 获取内存块大小
    int mem_size = *(int *)mem_size_field;

    // 计算前一个块的起始地址
    char *prev_chunk = char_chunk - mem_size;

    // 返回前一个块的指针
    return prev_chunk;
}

    *(zbx_uint64_t *)((char *)chunk + MEM_SIZE_FIELD + size) = MEM_FLG_USED | size;
}


static void	*mem_get_prev_chunk(void *chunk)
{
	return *(void **)((char *)chunk + MEM_SIZE_FIELD);
}

/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是设置一个内存块（chunk）的prev指针，以便在释放内存时能够追踪到上一个内存块。
 *
 *整个注释好的代码块如下：
 *
 *```c
 *static void mem_set_prev_chunk(void *chunk, void *prev)
 *{
 *\t// 计算chunk内存地址加上MEM_SIZE_FIELD的偏移量，将其赋值给*(void **)类型的指针
 *\t*(void **)((char *)chunk + MEM_SIZE_FIELD) = prev;
 *}
 *```
 ******************************************************************************/
static void mem_set_prev_chunk(void *chunk, void *prev) // 定义一个名为mem_set_prev_chunk的静态函数，接受两个参数，一个是void类型的chunk，另一个是void类型的prev
{
	// 计算chunk内存地址加上MEM_SIZE_FIELD的偏移量，将其赋值给*(void **)类型的指针
	*(void **)((char *)chunk + MEM_SIZE_FIELD) = prev;
}


/******************************************************************************
 * *
 *这块代码的主要目的是：从一个给定的内存块（chunk）中获取下一个内存块的地址。函数接收一个 void 类型的指针作为参数，返回下一个内存块的地址。在此过程中，首先将 void 类型的指针转换为 char 类型的指针，方便计算偏移量。然后根据内存块大小和指针大小计算下一个内存块的地址偏移量，最后通过指针运算获取下一个内存块的地址。
 ******************************************************************************/
// 定义一个静态函数，用于获取下一个内存块
static void *mem_get_next_chunk(void *chunk)
{
    // 转换为字符指针，方便计算偏移量
    char *char_chunk = (char *)chunk;

    // 计算下一个内存块的地址偏移量，偏移量为内存块大小加上指针大小
    int offset = MEM_SIZE_FIELD + ZBX_PTR_SIZE;

    // 获取下一个内存块的地址
    void *next_chunk = *(void **)((char *)char_chunk + offset);

    // 返回下一个内存块的地址
    return next_chunk;
}


/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数`mem_set_next_chunk`，接收两个参数，一个是当前内存块（chunk），另一个是下一个内存块（next）。函数的主要作用是将下一个内存块的地址存储到当前块的下一个内存块位置。这里使用了指针操作，将下一个内存块的地址存储到指针变量中。
 ******************************************************************************/
// 定义一个静态函数，用于设置内存块的下一个块
static void mem_set_next_chunk(void *chunk, void *next)
{
    // 将下一个块的地址存储到当前块的下一个内存块位置
    *(void **)((char *)chunk + MEM_SIZE_FIELD + ZBX_PTR_SIZE) = next;
}


static void	**mem_ptr_to_prev_field(void *chunk)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是：提供两个函数，分别用于获取下一个内存块的地址和内存块的大小。这两个函数接收一个 void * 类型的参数 chunk，用于表示当前内存块。通过判断 chunk 是否为 NULL，分别返回下一个内存块的地址和大小。
 ******************************************************************************/
// 定义一个函数，用于获取下一个内存块的地址
static void	**mem_ptr_to_next_field(void *chunk, void **first_chunk)
{
    // 判断 chunk 是否不为 NULL，如果不为 NULL，则计算下一个内存块的地址
    if (chunk != NULL)
    {
        // 计算下一个内存块的地址，偏移量为 MEM_SIZE_FIELD（内存块大小）加上 ZBX_PTR_SIZE（指针大小）
        return (void **)((char *)chunk + MEM_SIZE_FIELD + ZBX_PTR_SIZE);
    }
    // 如果 chunk 为 NULL，则返回第一个内存块的地址（即 first_chunk 指向的地址）
    else
    {
        return first_chunk;
    }
}

// 定义一个函数，用于获取内存块的大小
void *mem_size_field(void *chunk)
{
    // 判断 chunk 是否不为 NULL，如果不为 NULL，则返回下一个内存块的大小（即 MEM_SIZE_FIELD 指向的地址）
    if (chunk != NULL)
    {
        return (void *)((char *)chunk + MEM_SIZE_FIELD);
    }
    // 如果 chunk 为 NULL，则返回 NULL
    else
    {
        return NULL;
    }
}



/******************************************************************************
 * *
 *这块代码的主要目的是实现一个动态内存分配函数，根据给定的内存大小需求，在特定内存块中寻找合适的块进行分配。如果找不到合适的大小的块，则记录日志。分配到的内存块可以被分为两个块使用，或者直接使用整个块。
 ******************************************************************************/
/* 定义一个静态函数，用于动态分配内存 */
static void *__mem_malloc(zbx_mem_info_t *info, zbx_uint64_t size)
{
	/* 定义变量 */
	int		index;
	void		*chunk;
	zbx_uint64_t	chunk_size;
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个内存重新分配函数__mem_realloc。该函数接收三个参数：
 *
 *1. `info`：内存信息结构体指针，用于存储内存分配的相关信息。
 *2. `old`：指向旧内存地址的指针。
 *3. `size`：新分配内存的大小。
 *
 *该函数的主要功能是根据给定的大小在新分配的内存中查找一个合适的chunk，或者在现有chunk的基础上进行分割和合并操作。具体来说，该函数实现以下功能：
 *
 *1. 计算分配内存的大小。
 *2. 获取旧chunk的地址和大小。
 *3. 判断下一个chunk是否可用，并计算其大小。
 *4. 根据size和下一个chunk的大小进行以下操作：
 *   a. 如果size小于或等于旧chunk的大小，合并或分割旧chunk。
 *   b. 如果下一个chunk可用且足够大，合并下一个chunk。
 *   c. 否则，分配新的内存。
 *5. 拷贝旧chunk的内存到新分配的内存。
 *6. 释放旧的内存。
 *
 *整个代码块的目标是在保证内存使用效率的前提下，实现对内存的重新分配。
 ******************************************************************************/
static void *__mem_realloc(zbx_mem_info_t *info, void *old, zbx_uint64_t size)
{
	// 定义变量
	void		*chunk, *new_chunk, *next_chunk;
	zbx_uint64_t	chunk_size, new_chunk_size;
	int		next_free;

	// 计算分配内存的大小
	size = mem_proper_alloc_size(size);

	// 获取chunk的地址和大小
	chunk = (void *)((char *)old - MEM_SIZE_FIELD);
	chunk_size = CHUNK_SIZE(chunk);

	// 获取下一个chunk的地址和判断是否可用的标志
	next_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + chunk_size + MEM_SIZE_FIELD);
	next_free = (next_chunk < info->hi_bound && FREE_CHUNK(next_chunk));

	// 如果size小于或等于chunk的大小
	if (size <= chunk_size)
	{
		// 如果没有释放足够的内存，不进行重新分配
		// 因为我们可能再次需要更多内存
		if (size > chunk_size / 4)
			return chunk;

		// 如果下一个chunk可用
		if (next_free)
		{
			// 合并下一个chunk

			info->used_size -= chunk_size - size;
			info->free_size += chunk_size - size;

			new_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + size + MEM_SIZE_FIELD);
			new_chunk_size = CHUNK_SIZE(next_chunk) + (chunk_size - size);

			// 断开下一个chunk的链接
			mem_unlink_chunk(info, next_chunk);

			// 设置新chunk的大小
			mem_set_chunk_size(new_chunk, new_chunk_size);
			mem_link_chunk(info, new_chunk);

			// 设置chunk已使用大小
			mem_set_used_chunk_size(chunk, size);
		}
		else
		{
			// 如果下一个chunk不可用，分割当前chunk

			info->used_size -= chunk_size - size;
			info->free_size += chunk_size - size - 2 * MEM_SIZE_FIELD;

			new_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + size + MEM_SIZE_FIELD);
			new_chunk_size = chunk_size - size - 2 * MEM_SIZE_FIELD;

			// 设置新chunk的大小
			mem_set_chunk_size(new_chunk, new_chunk_size);
			mem_link_chunk(info, new_chunk);

			// 设置chunk已使用大小
			mem_set_used_chunk_size(chunk, size);
		}

		return chunk;
	}

	// 如果下一个chunk可用且足够大
	if (next_free && chunk_size + 2 * MEM_SIZE_FIELD + CHUNK_SIZE(next_chunk) >= size)
	{
		info->used_size -= chunk_size;
		info->free_size += chunk_size + 2 * MEM_SIZE_FIELD;

		chunk_size += 2 * MEM_SIZE_FIELD + CHUNK_SIZE(next_chunk);

		// 断开下一个chunk的链接
		mem_unlink_chunk(info, next_chunk);

		/* 要么使用整个下一个chunk，要么分割它 */

		if (chunk_size < size + 2 * MEM_SIZE_FIELD + MEM_MIN_ALLOC)
		{
			info->used_size += chunk_size;
			info->free_size -= chunk_size;

			// 设置chunk已使用大小
			mem_set_used_chunk_size(chunk, chunk_size);
		}
		else
		{
			new_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + size + MEM_SIZE_FIELD);
			new_chunk_size = chunk_size - size - 2 * MEM_SIZE_FIELD;

			// 设置新chunk的大小
			mem_set_chunk_size(new_chunk, new_chunk_size);
			mem_link_chunk(info, new_chunk);

			info->used_size += size;
			info->free_size -= chunk_size;
			info->free_size += new_chunk_size;

			// 设置chunk已使用大小
			mem_set_used_chunk_size(chunk, size);
		}

		return chunk;
	}

	// 如果没有找到合适的chunk，分配新的内存
	if (NULL != (new_chunk = __mem_malloc(info, size)))
	{
		// 拷贝chunk的内存到新分配的内存
		memcpy((char *)new_chunk + MEM_SIZE_FIELD, (char *)chunk + MEM_SIZE_FIELD, chunk_size);

		// 释放旧的内存
		__mem_free(info, old);

		return new_chunk;
	}

	// 如果没有足够的内存分配，退出程序
	else
	{
		void	*tmp = NULL;

		// 检查是否可以释放当前chunk以及下一个chunk
		new_chunk_size = chunk_size;

		if (0 != next_free)
			new_chunk_size += CHUNK_SIZE(next_chunk) + 2 * MEM_SIZE_FIELD;

		if (info->lo_bound < chunk && FREE_CHUNK((char *)chunk - MEM_SIZE_FIELD))
			new_chunk_size += CHUNK_SIZE((char *)chunk - MEM_SIZE_FIELD) + 2 * MEM_SIZE_FIELD;

		if (size > new_chunk_size)
			return NULL;

		tmp = zbx_malloc(tmp, chunk_size);

		// 拷贝chunk的内存到新分配的内存
		memcpy(tmp, (char *)chunk + MEM_SIZE_FIELD, chunk_size);

		// 释放旧的内存
		__mem_free(info, old);

		if (NULL == (new_chunk = __mem_malloc(info, size)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		// 拷贝新分配的内存到new_chunk
		memcpy((char *)new_chunk + MEM_SIZE_FIELD, tmp, chunk_size);

		zbx_free(tmp);

		return new_chunk;
	}
}

		mem_set_chunk_size(new_chunk, new_chunk_size);
		mem_link_chunk(info, new_chunk);

		info->used_size += size;
		info->free_size -= chunk_size;
		info->free_size += new_chunk_size;

		mem_set_used_chunk_size(chunk, size);
	}

	return chunk;
}

static void	*__mem_realloc(zbx_mem_info_t *info, void *old, zbx_uint64_t size)
{
	void		*chunk, *new_chunk, *next_chunk;
	zbx_uint64_t	chunk_size, new_chunk_size;
	int		next_free;

	size = mem_proper_alloc_size(size);

	chunk = (void *)((char *)old - MEM_SIZE_FIELD);
	chunk_size = CHUNK_SIZE(chunk);

	next_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + chunk_size + MEM_SIZE_FIELD);
	next_free = (next_chunk < info->hi_bound && FREE_CHUNK(next_chunk));

	if (size <= chunk_size)
	{
		/* do not reallocate if not much is freed */
		/* we are likely to want more memory again */
		if (size > chunk_size / 4)
			return chunk;

		if (next_free)
		{
			/* merge with next chunk */

			info->used_size -= chunk_size - size;
			info->free_size += chunk_size - size;

			new_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + size + MEM_SIZE_FIELD);
			new_chunk_size = CHUNK_SIZE(next_chunk) + (chunk_size - size);

			mem_unlink_chunk(info, next_chunk);

			mem_set_chunk_size(new_chunk, new_chunk_size);
			mem_link_chunk(info, new_chunk);

			mem_set_used_chunk_size(chunk, size);
		}
		else
		{
			/* split the current one */

			info->used_size -= chunk_size - size;
			info->free_size += chunk_size - size - 2 * MEM_SIZE_FIELD;

			new_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + size + MEM_SIZE_FIELD);
			new_chunk_size = chunk_size - size - 2 * MEM_SIZE_FIELD;

			mem_set_chunk_size(new_chunk, new_chunk_size);
			mem_link_chunk(info, new_chunk);

			mem_set_used_chunk_size(chunk, size);
		}

		return chunk;
	}

	if (next_free && chunk_size + 2 * MEM_SIZE_FIELD + CHUNK_SIZE(next_chunk) >= size)
	{
		info->used_size -= chunk_size;
		info->free_size += chunk_size + 2 * MEM_SIZE_FIELD;

		chunk_size += 2 * MEM_SIZE_FIELD + CHUNK_SIZE(next_chunk);

		mem_unlink_chunk(info, next_chunk);

		/* either use the full next_chunk or split it */

		if (chunk_size < size + 2 * MEM_SIZE_FIELD + MEM_MIN_ALLOC)
		{
			info->used_size += chunk_size;
			info->free_size -= chunk_size;

			mem_set_used_chunk_size(chunk, chunk_size);
		}
		else
		{
			new_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + size + MEM_SIZE_FIELD);
			new_chunk_size = chunk_size - size - 2 * MEM_SIZE_FIELD;
			mem_set_chunk_size(new_chunk, new_chunk_size);
			mem_link_chunk(info, new_chunk);

			info->used_size += size;
			info->free_size -= chunk_size;
			info->free_size += new_chunk_size;

			mem_set_used_chunk_size(chunk, size);
		}

		return chunk;
	}
	else if (NULL != (new_chunk = __mem_malloc(info, size)))
	{
		memcpy((char *)new_chunk + MEM_SIZE_FIELD, (char *)chunk + MEM_SIZE_FIELD, chunk_size);

		__mem_free(info, old);

		return new_chunk;
	}
	else
	{
		void	*tmp = NULL;

		/* check if there would be enough space if the current chunk */
		/* would be freed before allocating a new one                */
		new_chunk_size = chunk_size;

		if (0 != next_free)
			new_chunk_size += CHUNK_SIZE(next_chunk) + 2 * MEM_SIZE_FIELD;

		if (info->lo_bound < chunk && FREE_CHUNK((char *)chunk - MEM_SIZE_FIELD))
			new_chunk_size += CHUNK_SIZE((char *)chunk - MEM_SIZE_FIELD) + 2 * MEM_SIZE_FIELD;

		if (size > new_chunk_size)
			return NULL;

		tmp = zbx_malloc(tmp, chunk_size);

		memcpy(tmp, (char *)chunk + MEM_SIZE_FIELD, chunk_size);

		__mem_free(info, old);

		if (NULL == (new_chunk = __mem_malloc(info, size)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		memcpy((char *)new_chunk + MEM_SIZE_FIELD, tmp, chunk_size);

		zbx_free(tmp);

		return new_chunk;
	}
}

/******************************************************************************
 * *
 *这段代码的主要目的是免费内存块的管理。它接收一个内存块指针`ptr`，然后根据给定的内存块大小`chunk_size`，将该内存块链接到内存池。在此过程中，它会检查相邻的内存块是否可以合并，如果可以合并，它会将相邻的内存块合并到当前内存块中，并更新内存池的使用和免费空间信息。最后，它会将合并后的内存块链接到内存池。
 ******************************************************************************/
static void	__mem_free(zbx_mem_info_t *info, void *ptr)
{
	// 定义所需的指针和变量
	void		*chunk;
	void		*prev_chunk, *next_chunk;
	zbx_uint64_t	chunk_size;
	int		prev_free, next_free;

	// 计算chunk的地址
	chunk = (void *)((char *)ptr - MEM_SIZE_FIELD);
	chunk_size = CHUNK_SIZE(chunk);

	// 更新内存使用信息
	info->used_size -= chunk_size;
	info->free_size += chunk_size;

	/* 检查是否可以与前一个和后一个块合并 */

	next_chunk = (void *)((char *)chunk + MEM_SIZE_FIELD + chunk_size + MEM_SIZE_FIELD);

	// 判断前一个块是否可以合并
	prev_free = (info->lo_bound < chunk && FREE_CHUNK((char *)chunk - MEM_SIZE_FIELD));
	// 判断后一个块是否可以合并
	next_free = (next_chunk < info->hi_bound && FREE_CHUNK(next_chunk));

	if (prev_free && next_free)
	{
		// 如果可以合并，更新免费空间
		info->free_size += 4 * MEM_SIZE_FIELD;

		// 计算前一个块的地址
		prev_chunk = (char *)chunk - MEM_SIZE_FIELD - CHUNK_SIZE((char *)chunk - MEM_SIZE_FIELD) -
				MEM_SIZE_FIELD;

		// 更新块大小
		chunk_size += 4 * MEM_SIZE_FIELD + CHUNK_SIZE(prev_chunk) + CHUNK_SIZE(next_chunk);

		// 拆卸前一个块
		mem_unlink_chunk(info, prev_chunk);
		// 拆卸后一个块
		mem_unlink_chunk(info, next_chunk);

		// 合并块
		chunk = prev_chunk;
		// 设置块大小
		mem_set_chunk_size(chunk, chunk_size);
		// 链接块到内存池
		mem_link_chunk(info, chunk);
	}
	else if (prev_free)
	{
		// 如果只能合并前一个块，更新免费空间
/******************************************************************************
 * *
 *这段代码的主要目的是创建一个私有共享内存区域，并为该内存区域分配一个zbx_mem_info_t结构体，用于存储内存使用情况。同时，将描述和参数存储在内存中，并设置允许内存溢出。最后，将共享内存划分为多个桶，以便后续分配和管理内存。
 ******************************************************************************/
int zbx_mem_create(zbx_mem_info_t **info, zbx_uint64_t size, const char *descr, const char *param, int allow_oom,
                    char **error)
{
    const char		*__function_name = "zbx_mem_create";

    int			shm_id, index, ret = FAIL;
    void			*base;

    // 将传入的descr和param转换为字符串，防止野指针
    descr = ZBX_NULL2STR(descr);
    param = ZBX_NULL2STR(param);

    // 记录日志，打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() param:'%s' size:" ZBX_FS_SIZE_T, __function_name, param,
                    (zbx_fs_size_t)size);

    /* 分配共享内存 */

    // 检查指针大小，保证系统兼容性
    if (4 != ZBX_PTR_SIZE && 8 != ZBX_PTR_SIZE)
    {
        *error = zbx_dsprintf(*error, "failed assumption about pointer size (" ZBX_FS_SIZE_T " not in {4, 8})",
                            (zbx_fs_size_t)ZBX_PTR_SIZE);
        goto out;
    }

    // 检查请求的内存大小是否在合法范围内
    if (!(MEM_MIN_SIZE <= size && size <= MEM_MAX_SIZE))
    {
        *error = zbx_dsprintf(*error, "requested size " ZBX_FS_SIZE_T " not within bounds [" ZBX_FS_UI64
                            " <= size <= " ZBX_FS_UI64 "]", (zbx_fs_size_t)size, MEM_MIN_SIZE, MEM_MAX_SIZE);
        goto out;
    }

    // 分配私有共享内存
    if (-1 == (shm_id = shmget(IPC_PRIVATE, size, 0600)))
    {
        *error = zbx_dsprintf(*error, "cannot get private shared memory of size " ZBX_FS_SIZE_T " for %s: %s",
                            (zbx_fs_size_t)size, descr, zbx_strerror(errno));
        goto out;
    }

    // 映射共享内存
    if ((void *)(-1) == (base = shmat(shm_id, NULL, 0)))
    {
        *error = zbx_dsprintf(*error, "cannot attach shared memory for %s: %s", descr, zbx_strerror(errno));
        goto out;
    }

    // 标记共享内存为待销毁
    if (-1 == shmctl(shm_id, IPC_RMID, NULL))
        zbx_error("cannot mark shared memory %d for destruction: %s", shm_id, zbx_strerror(errno));

    ret = SUCCEED;

    /* 在共享内存中分配zbx_mem_info_t结构体、桶，以及描述 */

    *info = (zbx_mem_info_t *)ALIGN8(base);
    (*info)->shm_id = shm_id;
    (*info)->orig_size = size;
    size -= (char *)(*info + 1) - (char *)base;

    base = (void *)(*info + 1);

    (*info)->buckets = (void **)ALIGNPTR(base);
    memset((*info)->buckets, 0, MEM_BUCKET_COUNT * ZBX_PTR_SIZE);
    size -= (char *)((*info)->buckets + MEM_BUCKET_COUNT) - (char *)base;
    base = (void *)((*info)->buckets + MEM_BUCKET_COUNT);

    zbx_strlcpy((char *)base, descr, size);
    (*info)->mem_descr = (char *)base;
    size -= strlen(descr) + 1;
    base = (void *)((char *)base + strlen(descr) + 1);

    zbx_strlcpy((char *)base, param, size);
    (*info)->mem_param = (char *)base;
    size -= strlen(param) + 1;
    base = (void *)((char *)base + strlen(param) + 1);

    (*info)->allow_oom = allow_oom;

    /* 为后续分配做好准备，创建一个大的内存块 */
    (*info)->lo_bound = ALIGN8(base);
    (*info)->hi_bound = ALIGN8((char *)base + size - 8);

    (*info)->total_size = (zbx_uint64_t)((char *)((*info)->hi_bound) - (char *)((*info)->lo_bound) -
                            2 * MEM_SIZE_FIELD);

    index = mem_bucket_by_size((*info)->total_size);
    (*info)->buckets[index] = (*info)->lo_bound;
    mem_set_chunk_size((*info)->buckets[index], (*info)->total_size);
    mem_set_prev_chunk((*info)->buckets[index], NULL);
    mem_set_next_chunk((*info)->buckets[index], NULL);

    (*info)->used_size = 0;
    (*info)->free_size = (*info)->total_size;

    zabbix_log(LOG_LEVEL_DEBUG, "valid user addresses: [%p, %p] total size: " ZBX_FS_SIZE_T,
                    (void *)((char *)(*info)->lo_bound + MEM_SIZE_FIELD),
                    (void *)((char *)(*info)->hi_bound - MEM_SIZE_FIELD),
                    (zbx_fs_size_t)(*info)->total_size);

    /* 结束 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

    return ret;
}

	base = (void *)(*info + 1);

	(*info)->buckets = (void **)ALIGNPTR(base);
	memset((*info)->buckets, 0, MEM_BUCKET_COUNT * ZBX_PTR_SIZE);
	size -= (char *)((*info)->buckets + MEM_BUCKET_COUNT) - (char *)base;
	base = (void *)((*info)->buckets + MEM_BUCKET_COUNT);

	zbx_strlcpy((char *)base, descr, size);
	(*info)->mem_descr = (char *)base;
	size -= strlen(descr) + 1;
	base = (void *)((char *)base + strlen(descr) + 1);

	zbx_strlcpy((char *)base, param, size);
	(*info)->mem_param = (char *)base;
	size -= strlen(param) + 1;
	base = (void *)((char *)base + strlen(param) + 1);

	(*info)->allow_oom = allow_oom;

	/* prepare shared memory for further allocation by creating one big chunk */
	(*info)->lo_bound = ALIGN8(base);
	(*info)->hi_bound = ALIGN8((char *)base + size - 8);

	(*info)->total_size = (zbx_uint64_t)((char *)((*info)->hi_bound) - (char *)((*info)->lo_bound) -
			2 * MEM_SIZE_FIELD);

	index = mem_bucket_by_size((*info)->total_size);
	(*info)->buckets[index] = (*info)->lo_bound;
	mem_set_chunk_size((*info)->buckets[index], (*info)->total_size);
	mem_set_prev_chunk((*info)->buckets[index], NULL);
	mem_set_next_chunk((*info)->buckets[index], NULL);

	(*info)->used_size = 0;
	(*info)->free_size = (*info)->total_size;

	zabbix_log(LOG_LEVEL_DEBUG, "valid user addresses: [%p, %p] total size: " ZBX_FS_SIZE_T,
			(void *)((char *)(*info)->lo_bound + MEM_SIZE_FIELD),
			(void *)((char *)(*info)->hi_bound - MEM_SIZE_FIELD),
			(zbx_fs_size_t)(*info)->total_size);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是分配内存。该函数接收一个文件名、行号、内存信息结构体、旧的内存地址和要分配的大小作为参数。在分配内存的过程中，会对大小进行校验，如果大小不合法，则会报错并退出。如果内存分配成功，返回分配的内存地址。如果在分配内存过程中出现内存不足的情况，会给出提示信息，并建议调整配置参数。最后，输出调用栈信息并退出程序。
 ******************************************************************************/
// 定义一个函数指针，用于分配内存
void *__zbx_mem_malloc(const char *file, int line, zbx_mem_info_t *info, const void *old, size_t size)
{
    // 定义一些变量
    const char	*__function_name = "__zbx_mem_malloc";
    void		*chunk;

    // 如果传入的old不为NULL，说明要分配的内存已经分配过，报错并退出
    if (NULL != old)
    {
        zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] %s(): allocating already allocated memory",
                    file, line, __function_name);
        exit(EXIT_FAILURE);
    }

    // 检查要分配的大小是否合法，如果大小为0或大于MEM_MAX_SIZE，报错并退出
    if (0 == size || size > MEM_MAX_SIZE)
    {
        zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] %s(): asking for a bad number of bytes (" ZBX_FS_SIZE_T
                    ")", file, line, __function_name, (zbx_fs_size_t)size);
        exit(EXIT_FAILURE);
    }

    // 调用__mem_malloc函数分配内存，并获取内存块指针
    chunk = __mem_malloc(info, size);

    // 如果分配内存失败，且允许内存不足，直接返回NULL
    if (NULL == chunk)
    {
        if (1 == info->allow_oom)
            return NULL;

        // 报错日志
        zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] %s(): out of memory (requested " ZBX_FS_SIZE_T " bytes)",
                    file, line, __function_name, (zbx_fs_size_t)size);

        // 建议调整配置参数
        zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] %s(): please increase %s configuration parameter",
                    file, line, __function_name, info->mem_param);

        // 输出内存分配统计信息
        zbx_mem_dump_stats(LOG_LEVEL_CRIT, info);

        // 输出调用栈信息
        zbx_backtrace();

        // 退出程序
        exit(EXIT_FAILURE);
    }

    // 返回分配的内存地址，并加上MEM_SIZE_FIELD（一个固定值）
    return (void *)((char *)chunk + MEM_SIZE_FIELD);
}

/******************************************************************************
 * *
 *这块代码的主要目的是实现一个动态内存分配函数，该函数接受一个文件名、行号、内存信息结构体、旧内存地址和所需内存大小作为参数。首先检查请求的内存大小是否合法，如果合法，则使用`__mem_malloc`或`__mem_realloc`分配内存。分配成功后，返回分配的内存地址。如果内存分配失败，输出错误日志，并提示增加配置参数。同时，输出内存使用统计信息和堆栈跟踪信息，最后退出程序。
 ******************************************************************************/
// 定义一个函数指针，该函数用于动态分配内存
void *__zbx_mem_realloc(const char *file, int line, zbx_mem_info_t *info, void *old, size_t size)
{
    // 定义一些变量
    const char	*__function_name = "__zbx_mem_realloc";
    void		*chunk;

    // 检查分配的内存大小是否合法
    if (0 == size || size > MEM_MAX_SIZE)
    {
        // 输出错误日志，并退出程序
        zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] %s(): asking for a bad number of bytes (" ZBX_FS_SIZE_T
                                ")", file, line, __function_name, (zbx_fs_size_t)size);
        exit(EXIT_FAILURE);
    }

    // 判断old是否为空，如果为空，则使用__mem_malloc分配内存；否则，使用__mem_realloc进行内存扩展
    if (NULL == old)
        chunk = __mem_malloc(info, size);
    else
        chunk = __mem_realloc(info, old, size);

    // 检查分配的内存是否成功
    if (NULL == chunk)
    {
        // 如果允许内存不足，则返回NULL
        if (1 == info->allow_oom)
            return NULL;

        // 输出错误日志，并提示增加配置参数
        zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] %s(): out of memory (requested " ZBX_FS_SIZE_T " bytes)",
                                file, line, __function_name, (zbx_fs_size_t)size);
        zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] %s(): please increase %s configuration parameter",
                                file, line, __function_name, info->mem_param);

        // 输出内存使用统计信息
        zbx_mem_dump_stats(LOG_LEVEL_CRIT, info);

        // 输出堆栈跟踪信息
        zbx_backtrace();

        // 退出程序
        exit(EXIT_FAILURE);
    }

    // 返回分配的内存地址
    return (void *)((char *)chunk + MEM_SIZE_FIELD);
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个用于释放内存的函数__zbx_mem_free。函数接收四个参数：文件名、行号、内存信息结构体指针和要释放的内存指针。当传入的指针为NULL时，函数会输出错误日志并退出程序。否则，调用__mem_free函数进行内存释放。
 ******************************************************************************/
// 定义一个函数，用于释放内存
void __zbx_mem_free(const char *file, int line, zbx_mem_info_t *info, void *ptr)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "__zbx_mem_free";

    // 判断传入的指针是否为空，如果为空，则进行以下操作：
    if (NULL == ptr)
    {
        // 输出日志，记录错误信息，并退出程序
/******************************************************************************
 * *
 *整个代码块的主要目的是清除内存管理信息，将内存块重新初始化，以便重新使用。具体操作包括：
 *
 *1. 使用memset清空内存块数组。
 *2. 计算内存块大小，并将其分配给对应的索引。
 *3. 设置内存块的大小、前后相邻块指针为NULL。
 *4. 初始化已使用内存大小为0，总内存大小为原始值。
 *
 *整个函数的执行过程为：进入函数 -> 清除内存块 -> 计算并分配内存块 -> 设置内存块信息 -> 初始化内存使用情况 -> 结束函数。
 ******************************************************************************/
// 定义一个函数void zbx_mem_clear，接收一个zbx_mem_info_t类型的指针作为参数
void	zbx_mem_clear(zbx_mem_info_t *info)
{
	// 定义一个常量字符串，表示函数名称
	const char	*__function_name = "zbx_mem_clear";

	// 定义一个整型变量index，用于后续计算
	int		index;

	// 使用zabbix_log记录调试信息，表示进入函数__function_name()
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 使用memset函数清空info->buckets数组，将其所有元素设置为0
	memset(info->buckets, 0, MEM_BUCKET_COUNT * ZBX_PTR_SIZE);

	// 根据info->total_size计算index，用于后续分配内存块
	index = mem_bucket_by_size(info->total_size);

	// 将info->lo_bound赋值给info->buckets[index]，作为内存块的起始地址
	info->buckets[index] = info->lo_bound;

	// 调用mem_set_chunk_size函数，设置内存块大小为info->total_size
	mem_set_chunk_size(info->buckets[index], info->total_size);

	// 调用mem_set_prev_chunk函数，设置当前内存块的前一个块为NULL
	mem_set_prev_chunk(info->buckets[index], NULL);

	// 调用mem_set_next_chunk函数，设置当前内存块的后一个块为NULL
	mem_set_next_chunk(info->buckets[index], NULL);

	// 初始化info->used_size为0，表示当前未使用内存大小为0
	info->used_size = 0;

	// 初始化info->free_size为info->total_size，表示总内存大小
	info->free_size = info->total_size;

	// 使用zabbix_log记录调试信息，表示结束函数__function_name()
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	info->buckets[index] = info->lo_bound;
	mem_set_chunk_size(info->buckets[index], info->total_size);
	mem_set_prev_chunk(info->buckets[index], NULL);
	mem_set_next_chunk(info->buckets[index], NULL);
	info->used_size = 0;
	info->free_size = info->total_size;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是输出内存使用统计信息。它接收两个参数：一个表示日志级别的整数和一个表示内存信息结构的指针。代码首先打印内存统计信息的标题，然后遍历内存块链表，记录每个内存块的大小，并更新最小和最大内存块大小。接下来，计算已分配的内存大小，并按照内存块数量进行拆分。最后，打印内存总大小、内存块数量、自由内存大小和已使用内存大小，以及内存块大小的分布情况。整个代码块以一个结束日志结尾。
 ******************************************************************************/
void	zbx_mem_dump_stats(int level, zbx_mem_info_t *info)
{
	// 定义变量
	void		*chunk;
	int		index;
	zbx_uint64_t	counter, total, total_free = 0;
	zbx_uint64_t	min_size = __UINT64_C(0xffffffffffffffff), max_size = __UINT64_C(0);

	// 打印日志
	zabbix_log(level, "=== memory statistics for %s ===", info->mem_descr);

	// 遍历内存块
	for (index = 0; index < MEM_BUCKET_COUNT; index++)
	{
		counter = 0;
		chunk = info->buckets[index];

		// 遍历内存块链表
		while (NULL != chunk)
		{
			counter++;
			// 更新最小和最大内存块大小
			min_size = MIN(min_size, CHUNK_SIZE(chunk));
			max_size = MAX(max_size, CHUNK_SIZE(chunk));
			chunk = mem_get_next_chunk(chunk);
		}

		// 如果内存块数量大于0，累加已分配的内存大小
		if (counter > 0)
		{
			total_free += counter;
			zabbix_log(level, "free chunks of size %2s %3d bytes: %8llu",
					index == MEM_BUCKET_COUNT - 1 ? ">=" : "",
					MEM_MIN_BUCKET_SIZE + 8 * index, (unsigned long long)counter);
		}
	}

	// 打印最小和最大内存块大小
	zabbix_log(level, "min chunk size: %10llu bytes", (unsigned long long)min_size);
	zabbix_log(level, "max chunk size: %10llu bytes", (unsigned long long)max_size);

	// 计算总内存大小，并拆分成内存块数量
	total = (info->total_size - info->used_size - info->free_size) / (2 * MEM_SIZE_FIELD) + 1;
	zabbix_log(level, "memory of total size %llu bytes fragmented into %llu chunks",
			(unsigned long long)info->total_size, (unsigned long long)total);
	zabbix_log(level, "of those, %10llu bytes are in %8llu free chunks",
			(unsigned long long)info->free_size, (unsigned long long)total_free);
	zabbix_log(level, "of those, %10llu bytes are in %8llu used chunks",
			(unsigned long long)info->used_size, (unsigned long long)(total - total_free));

	// 打印结束日志
	zabbix_log(level, "================================");
}

/******************************************************************************
 * *
 *整个代码块的主要目的是计算分配给chunks_num个内存块所需的共享内存大小。该函数接收三个参数：chunks_num（表示内存块的数量）、descr（描述字符串）和param（参数字符串）。在计算过程中，函数考虑了各种开销，如内存对齐、zbx_mem_info_t结构体、字符串长度等。最后，函数返回计算出的内存大小。
 ******************************************************************************/
size_t	zbx_mem_required_size(int chunks_num, const char *descr, const char *param)
{
	/* 定义一个常量字符串，表示函数名 */
	const char	*__function_name = "zbx_mem_required_size";

	/* 定义一个size_t类型的变量，用于存储计算后的内存大小 */
	size_t		size = 0;

	/* 打印调试信息，包括函数名、当前size、chunks_num、descr、param */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() size:" ZBX_FS_SIZE_T " chunks_num:%d descr:'%s' param:'%s'",
			__function_name, (zbx_fs_size_t)size, chunks_num, descr, param);

	/* 计算需要的共享内存大小，确保能够分配chunks_num个内存块，总大小为size */
	/* 同时存储descr和param，并保证对齐 */

	size += 7;					/* 确保分配足够的空间以8字节对齐zbx_mem_info_t */
	size += sizeof(zbx_mem_info_t);
	size += ZBX_PTR_SIZE - 1;			/* 确保分配足够的空间以对齐桶指针 */
	size += ZBX_PTR_SIZE * MEM_BUCKET_COUNT;
	size += strlen(descr) + 1;
	size += strlen(param) + 1;
	size += (MEM_SIZE_FIELD - 1) + 8;		/* 确保分配足够的空间以对齐第一个内存块的大小字段 */
	size += (MEM_SIZE_FIELD - 1) + 8;		/* 确保分配足够的空间以对齐正确的大小字段 */

	size += (chunks_num - 1) * MEM_SIZE_FIELD * 2;	/* 每个额外的内存块需要16字节的额外开销 */
	size += chunks_num * (MEM_MIN_ALLOC - 1);	/* 每个内存块的大小至少为MEM_MIN_ALLOC字节 */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() size:" ZBX_FS_SIZE_T, __function_name, (zbx_fs_size_t)size);

	/* 返回计算后的内存大小 */
	return size;
}

