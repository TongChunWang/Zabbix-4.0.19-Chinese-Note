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


static void	swap(zbx_binary_heap_t *heap, int index_1, int index_2);

static void	__binary_heap_ensure_free_space(zbx_binary_heap_t *heap);

static int	__binary_heap_bubble_up(zbx_binary_heap_t *heap, int index);
static int	__binary_heap_bubble_down(zbx_binary_heap_t *heap, int index);

#define	ARRAY_GROWTH_FACTOR	3/2

#define	HAS_DIRECT_OPTION(heap)	(0 != (heap->options & ZBX_BINARY_HEAP_OPTION_DIRECT))

/* helper functions */


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个二叉堆（binary heap）的数据结构，其中包括以下几个辅助函数：
 *
 *1. `swap()`：交换两个元素。
 *2. `__binary_heap_ensure_free_space()`：确保二叉堆中有足够的自由空间。
 *3. `__binary_heap_bubble_up()`：将一个元素向上冒泡，使其满足二叉堆的性质。
 *4. `__binary_heap_bubble_down()`：将一个元素向下冒泡，使其满足二叉堆的性质。
 *
 *此外，还定义了一些常量和辅助宏，如`ARRAY_GROWTH_FACTOR`、`HAS_DIRECT_OPTION()`等。这些函数和宏共同构成了二叉堆的数据结构和相关操作。
 ******************************************************************************/
// 定义一个交换函数，用于交换两个元素
static void swap(zbx_binary_heap_t *heap, int index_1, int index_2)
{
	// 定义一个临时变量tmp，用于存储交换的元素
	zbx_binary_heap_elem_t tmp;

	// 交换两个元素
	tmp = heap->elems[index_1];
	heap->elems[index_1] = heap->elems[index_2];
	heap->elems[index_2] = tmp;

	// 如果堆选项中包含直接选项，则更新key_index中的映射关系
	if (HAS_DIRECT_OPTION(heap))
	{
		zbx_hashmap_set(heap->key_index, heap->elems[index_1].key, index_1);
		zbx_hashmap_set(heap->key_index, heap->elems[index_2].key, index_2);
	}
}


/* private binary heap functions */

/******************************************************************************
 * *
 *整个代码块的主要目的是确保二叉堆（zbx_binary_heap）有足够的自由空间来容纳新的元素。当二叉堆中的元素数量接近或达到分配的内存空间时，此函数会被调用。它会检查当前分配的内存空间是否足够，如果不够，它会重新分配更大的内存空间，并确保二叉堆的指针指向新的内存区域。如果在重新分配内存时出现问题，程序将执行 THIS_SHOULD_NEVER_HAPPEN 宏并退出。
 *
 *输出：
 *
 *``````
 ******************************************************************************/
static void __binary_heap_ensure_free_space(zbx_binary_heap_t *heap)
{
    // 定义一个临时变量 tmp_elems_alloc，用于保存 heap->elems_alloc 的值
    int	tmp_elems_alloc = heap->elems_alloc;

    /* 为了防止内存损坏，只在成功分配内存后设置 heap->elems_alloc */
    /* 否则，在共享内存的情况下，其他进程可能读取或写入超过实际分配的内存 */

    // 检查 heap->elems 是否为空
    if (NULL == heap->elems)
    {
        // 如果 heap->elems 为空，则 heap->elems_num 也为 0
		heap->elems_num = 0;
        // 初始化 tmp_elems_alloc 为 32
        tmp_elems_alloc = 32;
    }
    else if (heap->elems_num == heap->elems_alloc)
        // 如果 heap->elems_num 等于 heap->elems_alloc，则更新 tmp_elems_alloc
        tmp_elems_alloc = MAX(heap->elems_alloc + 1, heap->elems_alloc * ARRAY_GROWTH_FACTOR);

    // 检查 heap->elems_alloc 是否与 tmp_elems_alloc 不同
    if (heap->elems_alloc != tmp_elems_alloc)
    {
        // 重新分配内存，并将 heap->elems 指向新的内存区域
        heap->elems = (zbx_binary_heap_elem_t *)heap->mem_realloc_func(heap->elems, tmp_elems_alloc * sizeof(zbx_binary_heap_elem_t));

        // 检查重新分配的内存是否成功
        if (NULL == heap->elems)
        {
            // 如果内存分配失败，执行 THIS_SHOULD_NEVER_HAPPEN 宏，并退出程序
            THIS_SHOULD_NEVER_HAPPEN;
            exit(EXIT_FAILURE);
        }

        // 更新 heap->elems_alloc 为 tmp_elems_alloc
        heap->elems_alloc = tmp_elems_alloc;
    }
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个二叉堆（binary heap）的冒泡排序算法，将堆顶元素（索引为index的元素）调整到正确的位置。该算法通过比较相邻元素的大小，不断将较大的元素向上移动，直到堆顶。在这个过程中，使用了交换操作来实现元素的移动。最后，返回堆顶元素的正确位置。
 ******************************************************************************/
// 定义一个名为__binary_heap_bubble_up的静态函数，传入一个zbx_binary_heap_t类型的指针（堆结构）和一个整数类型的索引（待调整元素的位置）
static int __binary_heap_bubble_up(zbx_binary_heap_t *heap, int index)
{
	// 使用一个while循环，当索引不为0时执行
	while (0 != index)
	{
		// 判断当前元素与父节点元素的大小关系，如果当前元素小于等于父节点元素，说明无需调整，直接跳出循环
		if (heap->compare_func(&heap->elems[(index - 1) / 2], &heap->elems[index]) <= 0)
			break;

		// 交换当前元素与父节点元素的位置
		swap(heap, (index - 1) / 2, index);

		// 更新当前元素的索引为父节点索引
		index = (index - 1) / 2;
	}

	// 返回最终的索引，即堆顶元素的位置
	return index;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个二叉堆的冒泡排序算法。该算法用于在一个二叉堆（最小堆）中查找最小值，并返回最小值所在的索引。在这个过程中，通过不断地比较和交换节点值，使得最小值逐渐移动到堆的根节点。
 ******************************************************************************/
static int __binary_heap_bubble_down(zbx_binary_heap_t *heap, int index)
{
    // 定义一个无限循环，用于实现冒泡排序算法
    while (1)
    {
        int left = 2 * index + 1; // 计算左子节点的索引
        int right = 2 * index + 2; // 计算右子节点的索引

        // 如果左子节点索引大于等于堆中的元素数量，说明已经到达最底层，可以跳出循环
        if (left >= heap->elems_num)
            break;

        // 如果右子节点索引大于等于堆中的元素数量，说明只剩下一个子节点，无需比较
        if (right >= heap->elems_num)
        {
            // 比较当前节点和左子节点的大小，如果当前节点大于左子节点，则交换它们的位置
            if (heap->compare_func(&heap->elems[index], &heap->elems[left]) > 0)
            {
                swap(heap, index, left);
                index = left; // 更新当前节点的索引
            }

            // 已经找到最小值，跳出循环
            break;
        }

        // 比较当前节点、左子节点和右子节点的大小，选出最小值
        if (heap->compare_func(&heap->elems[left], &heap->elems[right]) <= 0)
        {
            // 比较当前节点和左子节点的大小，如果当前节点大于左子节点，则交换它们的位置
            if (heap->compare_func(&heap->elems[index], &heap->elems[left]) > 0)
            {
                swap(heap, index, left);
                index = left; // 更新当前节点的索引
            }
			else
				// 已经找到最小值，跳出循环
				break;
		}
		else
		{
			// 比较当前节点和右子节点的大小，如果当前节点大于右子节点，则交换它们的位置
			if (heap->compare_func(&heap->elems[index], &heap->elems[right]) > 0)
			{
				swap(heap, index, right);
				index = right;
			}
			else
				break;
		}
	}

    // 返回找到的最小值所在的索引
    return index;
}


/* public binary heap interface */
/******************************************************************************
 * *
 *这块代码的主要目的是创建一个二叉堆。其中，`zbx_binary_heap_create`函数是一个封装的外部函数，它调用内部的`zbx_binary_heap_create_ext`函数来完成创建二叉堆的操作。`zbx_binary_heap_create_ext`函数接收三个参数：`heap`是一个指向二叉堆结构的指针，`compare_func`是一个比较函数，用于比较二叉堆中的元素，`options`是一些选项。
 *
 *在`zbx_binary_heap_create_ext`函数中，使用了三个默认的内存分配函数：`ZBX_DEFAULT_MEM_MALLOC_FUNC`、`ZBX_DEFAULT_MEM_REALLOC_FUNC`和`ZBX_DEFAULT_MEM_FREE_FUNC`，分别用于内存分配、重新分配和释放。这些默认函数可以根据实际需求进行替换。
 *
 *整个代码块的主要目的是创建一个二叉堆，并为后续的操作（如插入、删除、查找等）提供基础。
 ******************************************************************************/
// 定义一个函数，用于创建二叉堆
void zbx_binary_heap_create(zbx_binary_heap_t *heap, zbx_compare_func_t compare_func, int options)
{
    // 调用zbx_binary_heap_create_ext函数来真正实现创建二叉堆
    zbx_binary_heap_create_ext(heap, compare_func, options,
                                ZBX_DEFAULT_MEM_MALLOC_FUNC,
                                ZBX_DEFAULT_MEM_REALLOC_FUNC,
                                ZBX_DEFAULT_MEM_FREE_FUNC);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个扩展的zbx二叉堆结构，包括比较函数、选项、内存分配函数等。在这个过程中，初始化了zbx二叉堆的结构体变量，设置了比较函数、选项、内存分配函数，以及根据选项创建了一个哈希表用于直接访问。最后，将内存分配、重新分配和释放函数赋值给堆结构。
 ******************************************************************************/
void zbx_binary_heap_create_ext(zbx_binary_heap_t *heap, zbx_compare_func_t compare_func, int options,
                                 zbx_mem_malloc_func_t mem_malloc_func,
                                 zbx_mem_realloc_func_t mem_realloc_func,
                                 zbx_mem_free_func_t mem_free_func)
{
	heap->elems = NULL;
	heap->elems_num = 0;
	heap->elems_alloc = 0;
	heap->compare_func = compare_func;
	heap->options = options;

	if (HAS_DIRECT_OPTION(heap))
	{
		heap->key_index = (zbx_hashmap_t *)mem_malloc_func(NULL, sizeof(zbx_hashmap_t));
		zbx_hashmap_create_ext(heap->key_index, 512,
					ZBX_DEFAULT_UINT64_HASH_FUNC,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC,
					mem_malloc_func,
					mem_realloc_func,
					mem_free_func);
	}
	else
		heap->key_index = NULL;

	heap->mem_malloc_func = mem_malloc_func;
	heap->mem_realloc_func = mem_realloc_func;
	heap->mem_free_func = mem_free_func;
}
/******************************************************************************
 *整个代码块的主要目的是销毁一个二叉堆。首先检查二叉堆的元素是否为空，如果不为空，则依次释放内存、置空指针和减少元素数量。接着将比较函数指针置空，然后根据直接选项的标志判断是否需要销毁键值对映射，如果需要，则调用相应的函数进行销毁和释放内存。最后，将各种内存分配、重分配和释放函数指针置空。
 ******************************************************************************/
void	zbx_binary_heap_destroy(zbx_binary_heap_t *heap) // 定义一个函数，用于销毁二叉堆
{
	if (NULL != heap->elems) // 如果二叉堆的元素不为空
	{
		heap->mem_free_func(heap->elems); // 调用分配内存的函数，释放内存
		heap->elems = NULL; // 将二叉堆的元素指针置空
		heap->elems_num = 0; // 将二叉堆中的元素数量置为0
		heap->elems_alloc = 0; // 将二叉堆分配的内存大小置为0
	}

	heap->compare_func = NULL; // 将比较函数指针置空

	if (HAS_DIRECT_OPTION(heap)) // 如果二叉堆使用了直接选项
	{
		zbx_hashmap_destroy(heap->key_index); // 销毁键值对映射
		heap->mem_free_func(heap->key_index); // 释放键值对映射的内存
		heap->key_index = NULL; // 将键值对映射指针置空
		heap->options = 0; // 将选项置为0
	}

	heap->mem_malloc_func = NULL; // 将内存分配函数指针置空
	heap->mem_realloc_func = NULL; // 将内存重分配函数指针置空
	heap->mem_free_func = NULL; // 将内存释放函数指针置空
}

/******************************************************************************
 * *
 *这块代码的主要目的是检查给定的二叉堆（zbx_binary_heap_t类型）是否为空。如果堆中没有元素，函数返回成功（SUCCEED）；如果堆中有元素，函数返回失败（FAIL）。
 ******************************************************************************/
// 定义一个函数zbx_binary_heap_empty，接收一个zbx_binary_heap_t类型的指针作为参数
int zbx_binary_heap_empty(zbx_binary_heap_t *heap)
{
    // 判断堆中的元素个数是否为0
    if (0 == heap->elems_num)
    {
        // 如果元素个数为0，返回成功（SUCCEED）
        return SUCCEED;
    }
    // 如果元素个数不为0，返回失败（FAIL）
    else
    {
        return FAIL;
    }
}

/******************************************************************************
 * *
 *这块代码的主要目的是查找zbx二叉堆中的最小值。具体步骤如下：
 *
 *1. 检查zbx二叉堆中的元素数量是否为0，如果为0则说明堆为空。
 *2. 如果堆为空，输出一条日志表示在空堆中查找最小值，并退出程序，表示操作失败。
 *3. 如果堆非空，返回堆中第一个元素，即最小值。
 ******************************************************************************/
zbx_binary_heap_elem_t *zbx_binary_heap_find_min(zbx_binary_heap_t *heap)
{
	// 检查堆中的元素数量是否为0，如果为0则说明堆为空
	if (0 == heap->elems_num)
	{
		// 输出日志，表示在空堆中查找最小值
		zabbix_log(LOG_LEVEL_CRIT, "asking for a minimum in an empty heap");
		// 退出程序，表示操作失败
		exit(EXIT_FAILURE);
	}

	// 返回堆中第一个元素，即最小值
	return &heap->elems[0];
}

/******************************************************************************
 * *
 *代码主要目的是实现一个二叉堆（zbx_binary_heap）的插入操作。当向二叉堆中插入一个新元素时，首先检查堆中是否已存在相同的键，如果存在则输出警告日志并退出程序。接着确保堆有足够的空闲空间，计算新元素在堆中的索引，并将元素插入到正确位置。最后，如果堆包含直接选项，将新元素的键与索引关联存储在哈希映射中。
 ******************************************************************************/
void	zbx_binary_heap_insert(zbx_binary_heap_t *heap, zbx_binary_heap_elem_t *elem)
{
	// 定义一个整数变量 index，用于存储元素在堆中的索引
	int	index;

	// 判断堆是否包含直接选项（DIRECT_OPTION）
	// 如果包含且元素键已存在于堆中，则输出警告日志，并退出程序
	if (HAS_DIRECT_OPTION(heap) && FAIL != zbx_hashmap_get(heap->key_index, elem->key))
	{
		zabbix_log(LOG_LEVEL_CRIT, "inserting a duplicate key into a heap with direct option");
		exit(EXIT_FAILURE);
	}

	// 确保堆有足够的空闲空间
	__binary_heap_ensure_free_space(heap);

	// 计算新元素在堆中的索引
	index = heap->elems_num++;
	heap->elems[index] = *elem;

	// 使用冒泡排序算法将新元素向上移动至正确位置
	index = __binary_heap_bubble_up(heap, index);

	// 如果堆包含直接选项且新元素已到达最后一个位置
	if (HAS_DIRECT_OPTION(heap) && index == heap->elems_num - 1)
		zbx_hashmap_set(heap->key_index, elem->key, index);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：实现一个 `zbx_binary_heap_update_direct` 函数，用于直接更新二叉堆中的元素。首先检查堆是否支持直接更新操作，如果不支持，则输出警告日志并退出程序。接着从哈希表中查找元素所在的索引，如果找到，则将新元素的值赋给堆中的对应元素，并根据索引执行冒泡上升操作。如果没有找到元素，则输出错误日志并退出程序。
 ******************************************************************************/
// 定义一个函数，用于直接更新二叉堆中的元素
void zbx_binary_heap_update_direct(zbx_binary_heap_t *heap, zbx_binary_heap_elem_t *elem)
{
	// 定义一个整数变量 index，用于存储元素在哈希表中的索引
	int index;

	// 检查堆是否支持直接更新操作
	if (!HAS_DIRECT_OPTION(heap))
	{
		// 输出警告日志，并退出程序
		zabbix_log(LOG_LEVEL_CRIT, "direct update operation is not supported for this heap");
		exit(EXIT_FAILURE);
	}

	// 从哈希表中查找元素所在的索引
	if (FAIL != (index = zbx_hashmap_get(heap->key_index, elem->key)))
	{
		// 将新元素的值赋给堆中的对应元素
		heap->elems[index] = *elem;

		// 如果索引等于冒泡上升的索引，则执行冒泡上升操作
		if (index == __binary_heap_bubble_up(heap, index))
			__binary_heap_bubble_down(heap, index);
	}
	else
	{
		// 输出错误日志，并退出程序
		zabbix_log(LOG_LEVEL_CRIT, "element with key " ZBX_FS_UI64 " not found in heap for update", elem->key);
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从二叉堆中删除最小元素。首先判断堆是否为空，若为空则打印日志并退出程序。接着判断堆中是否包含直接选项，如果包含则从 hashmap 中删除最小元素。然后对堆进行减1操作，将最后一个元素赋值给第一个元素，并调用 bubble_down 函数调整第一个元素的下标。最后，如果堆中仍然包含直接选项且第一个元素下标为0，则在 hashmap 中重新插入第一个元素（已调整位置）。
 ******************************************************************************/
// 定义一个名为 zbx_binary_heap_remove_min 的函数，参数为一个 zbx_binary_heap_t 类型的指针
void	zbx_binary_heap_remove_min(zbx_binary_heap_t *heap)
{
	// 定义一个整型变量 index，用于存储待删除元素的索引
	int	index;

	// 判断堆中的元素数量是否为0，如果为0，则表示空堆
	if (0 == heap->elems_num)
	{
		// 打印日志，表示从空堆中删除最小元素
		zabbix_log(LOG_LEVEL_CRIT, "removing a minimum from an empty heap");
		// 退出程序，返回失败状态
		exit(EXIT_FAILURE);
	}

	// 判断堆是否包含直接选项（HAS_DIRECT_OPTION 宏）
	if (HAS_DIRECT_OPTION(heap))
		// 从 hashmap 中删除最小元素对应的键值对
		zbx_hashmap_remove(heap->key_index, heap->elems[0].key);

	// 减1操作，表示删除了一个元素
	if (0 != (--heap->elems_num))
	{
		// 将最后一个元素赋值给第一个元素
		heap->elems[0] = heap->elems[heap->elems_num];
		// 调用 bubble_down 函数，将第一个元素下标调整为正确位置
		index = __binary_heap_bubble_down(heap, 0);

		// 判断堆是否包含直接选项（HAS_DIRECT_OPTION 宏）且第一个元素下标为0
		if (HAS_DIRECT_OPTION(heap) && index == 0)
			// 在 hashmap 中重新插入第一个元素（已调整位置）
			zbx_hashmap_set(heap->key_index, heap->elems[index].key, index);
	}
}

/******************************************************************************
 * *
 *代码块主要目的是实现一个名为 zbx_binary_heap_remove_direct 的函数，该函数用于从二叉堆中删除一个指定键值的元素。函数接收两个参数，一个指向 zbx_binary_heap_t 类型的指针和一个 zbx_uint64_t 类型的键值。在删除过程中，首先检查二叉堆是否支持直接删除操作，如果不支持，则记录日志并退出程序。如果支持，则通过哈希表查找键值对应的元素索引，找到后从哈希表中移除。接着将删除元素的后一个元素向前移动一个位置，并调整堆顶元素。最后，如果删除的是堆顶元素，还需要调整堆顶元素。如果删除的是堆底元素，还需要调整堆底元素。调整完成后，更新哈希表中的键值映射。如果没有找到要删除的元素，记录日志并退出程序。
 ******************************************************************************/
// 定义一个函数，名为 zbx_binary_heap_remove_direct，参数为一个指向 zbx_binary_heap_t 类型的指针和一个 zbx_uint64_t 类型的键值
void zbx_binary_heap_remove_direct(zbx_binary_heap_t *heap, zbx_uint64_t key)
{
	// 定义一个整型变量 index，用于存储元素在数组中的索引
	int index;

	// 检查 heap 是否支持直接删除操作
	if (!HAS_DIRECT_OPTION(heap))
	{
		// 如果不支持直接删除操作，记录日志并退出程序
		zabbix_log(LOG_LEVEL_CRIT, "direct remove operation is not supported for this heap");
		exit(EXIT_FAILURE);
	}

	// 查询键值对应的元素索引
	if (FAIL != (index = zbx_hashmap_get(heap->key_index, key)))
	{
		// 如果找到键值对应的元素，从哈希表中移除
		zbx_hashmap_remove(heap->key_index, key);

		// 如果没有找到，记录日志并退出程序
		if (index != (--heap->elems_num))
		{
			// 把删除元素的后一个元素向前移动一个位置
			heap->elems[index] = heap->elems[heap->elems_num];

			// 如果是堆顶元素，调整堆顶元素
			if (index == __binary_heap_bubble_up(heap, index))
			{
				// 如果是堆底元素，调整堆底元素
				if (index == __binary_heap_bubble_down(heap, index))
				{
					// 更新哈希表中的键值映射
					zbx_hashmap_set(heap->key_index, heap->elems[index].key, index);
				}
			}
		}
	}
	else
	{
		// 如果没有找到要删除的元素，记录日志并退出程序
		zabbix_log(LOG_LEVEL_CRIT, "element with key " ZBX_FS_UI64 " not found in heap for remove", key);
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是清空二叉堆。首先，它设置二叉堆中的元素数量为0，然后判断二叉堆是否包含直接选项。如果包含直接选项，那么清空键值映射（key_index）。这个过程完成后，二叉堆就被清空了。
 ******************************************************************************/
// 定义一个函数，用于清空二叉堆
void zbx_binary_heap_clear(zbx_binary_heap_t *heap)
{

	// 设置二叉堆中的元素数量为0
	heap->elems_num = 0;

	// 判断二叉堆是否包含直接选项（DIRECT_OPTION）
	if (HAS_DIRECT_OPTION(heap))
		// 如果二叉堆包含直接选项，则清空键值映射（key_index）
		zbx_hashmap_clear(heap->key_index);
}

