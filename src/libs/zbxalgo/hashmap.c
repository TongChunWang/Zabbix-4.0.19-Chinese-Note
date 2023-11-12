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

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数`__hashmap_ensure_free_entry`，用于确保hashmap中的免费Entry。该函数接收两个参数，一个是hashmap的结构体指针`hm`，另一个是指向hashmap中的一个slot的指针`slot`。在这个函数中，主要逻辑是检查slot的entries是否已满，如果已满则进行扩容，以确保hashmap中有足够的免费Entry。
 ******************************************************************************/
// 定义静态函数，用于确保hashmap中的免费 Entry
// 参数：hm -> 指向hashmap的结构体指针
//       slot -> 指向hashmap中的一个slot的指针
static void __hashmap_ensure_free_entry(zbx_hashmap_t *hm, ZBX_HASHMAP_SLOT_T *slot);

// 定义常量，用于控制负载因子、slot增长因子、数组增长因子和默认slots数量
#define CRIT_LOAD_FACTOR 5/1         // 临界负载因子，当hashmap的使用率达到此值时，会触发扩容
#define SLOT_GROWTH_FACTOR 3/2     // slot的增长因子，用于控制扩容时slot的数量
#define ARRAY_GROWTH_FACTOR 2     // 数组增长因子，用于控制entries数组的扩容
#define ZBX_HASHMAP_DEFAULT_SLOTS 10 // 默认slots数量

/* 私有hashmap函数 */

static void __hashmap_ensure_free_entry(zbx_hashmap_t *hm, ZBX_HASHMAP_SLOT_T *slot)
{
    // 如果slot的entries为空，则初始化entries数量和分配内存
    if (NULL == slot->entries)
    {
        slot->entries_num = 0;
        slot->entries_alloc = 6;
        slot->entries = (ZBX_HASHMAP_ENTRY_T *)hm->mem_malloc_func(NULL, slot->entries_alloc * sizeof(ZBX_HASHMAP_ENTRY_T));
    }
    // 如果slot的entries已满，则扩容entries数组
    else if (slot->entries_num == slot->entries_alloc)
    {
        slot->entries_alloc = slot->entries_alloc * ARRAY_GROWTH_FACTOR;
        slot->entries = (ZBX_HASHMAP_ENTRY_T *)hm->mem_realloc_func(slot->entries, slot->entries_alloc * sizeof(ZBX_HASHMAP_ENTRY_T));
    }
}


/******************************************************************************
 * *
 *这块代码的主要目的是初始化zbx哈希表的槽位。它接收一个哈希表指针`hm`和一个初始化大小`init_size`作为参数。在代码中，首先判断初始化大小是否大于0，如果大于0，则计算下一个质数作为哈希表的槽位数，然后分配内存存储槽位数据，并将内存初始化为0。如果初始化大小为0，则直接将槽位数和槽位指针初始化为0。整个函数用于确保哈希表在使用前正确初始化其结构和数据。
 ******************************************************************************/
// 定义一个静态函数，用于初始化zbx哈希表的槽位
static void zbx_hashmap_init_slots(zbx_hashmap_t *hm, size_t init_size)
{
	// 定义一个变量，用于存储哈希表中的数据数量
	hm->num_data = 0;

	// 判断初始化大小是否大于0
	if (0 < init_size)
	{
		// 计算下一个质数，作为哈希表的槽位数
		hm->num_slots = next_prime(init_size);

		// 分配内存，存储哈希表的槽位数据
		hm->slots = (ZBX_HASHMAP_SLOT_T *)hm->mem_malloc_func(NULL, hm->num_slots * sizeof(ZBX_HASHMAP_SLOT_T));

		// 将槽位内存初始化为0
		memset(hm->slots, 0, hm->num_slots * sizeof(ZBX_HASHMAP_SLOT_T));
	}
	else
	{
		// 初始化槽位数为0
		hm->num_slots = 0;

		// 初始化槽位指针为NULL
		hm->slots = NULL;
	}
}


/* public hashmap interface */

// 定义一个函数，用于创建一个哈希表
void zbx_hashmap_create(zbx_hashmap_t *hm, size_t init_size)
{
    // 调用zbx_hashmap_create_ext函数来创建哈希表
    zbx_hashmap_create_ext(hm, init_size,
                          // 设置哈希函数
                          ZBX_DEFAULT_UINT64_HASH_FUNC,
                          // 设置比较函数
                          ZBX_DEFAULT_UINT64_COMPARE_FUNC,
                          // 设置内存分配函数
                          ZBX_DEFAULT_MEM_MALLOC_FUNC,
                          // 设置内存重分配函数
                          ZBX_DEFAULT_MEM_REALLOC_FUNC,
                          // 设置内存释放函数
                          ZBX_DEFAULT_MEM_FREE_FUNC);
}

/******************************************************************************
 * *
 *这块代码的主要目的是创建一个扩展的zbx哈希表。它接收一个zbx_hashmap_t类型的指针hm，以及哈希函数、比较函数、内存分配函数、内存重新分配函数和内存释放函数作为参数。然后将这些函数赋值给hm结构体的相应成员，并调用zbx_hashmap_init_slots函数初始化哈希表的槽位。在整个过程中，没有返回值，因此这是一个void类型的函数。
 ******************************************************************************/
// 定义一个函数，用于创建一个扩展的zbx哈希表
void zbx_hashmap_create_ext(zbx_hashmap_t *hm, size_t init_size,
                             zbx_hash_func_t hash_func,
                             zbx_compare_func_t compare_func,
                             zbx_mem_malloc_func_t mem_malloc_func,
                             zbx_mem_realloc_func_t mem_realloc_func,
                             zbx_mem_free_func_t mem_free_func)
{
	// 将传入的哈希函数赋值给hm结构体的hash_func成员
	hm->hash_func = hash_func;

	// 将传入的比较函数赋值给hm结构体的compare_func成员
	hm->compare_func = compare_func;

	// 将传入的内存分配函数赋值给hm结构体的mem_malloc_func成员
	hm->mem_malloc_func = mem_malloc_func;

	// 将传入的内存重新分配函数赋值给hm结构体的mem_realloc_func成员
	hm->mem_realloc_func = mem_realloc_func;

	// 将传入的内存释放函数赋值给hm结构体的mem_free_func成员
	hm->mem_free_func = mem_free_func;

	// 初始化哈希表的槽位，设置初始大小为init_size
	zbx_hashmap_init_slots(hm, init_size);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是销毁一个哈希表。函数 `zbx_hashmap_destroy` 接收一个 `zbx_hashmap_t` 类型的指针作为参数，遍历哈希表的每个槽位，释放其中的内存，然后重置哈希表的各种属性为 NULL。在释放内存后，将哈希表的 slots 指针置空，以防止野指针。最后，将哈希表的各个函数指针重置为 NULL。
 ******************************************************************************/
// 定义一个函数，用于销毁哈希表
void zbx_hashmap_destroy(zbx_hashmap_t *hm)
{
	// 定义一个循环变量 i，用于遍历哈希表的槽位数
	int i;

	// 遍历哈希表的每个槽位
	for (i = 0; i < hm->num_slots; i++)
	{
		// 如果槽位中的 entries 不是空，则释放内存
		if (NULL != hm->slots[i].entries)
			hm->mem_free_func(hm->slots[i].entries);
	}

	// 重置哈希表的数据数量和槽位数
	hm->num_data = 0;
	hm->num_slots = 0;

	// 如果哈希表的 slots 不是空，则释放内存并将其置空
	if (NULL != hm->slots)
	{
		hm->mem_free_func(hm->slots);
		hm->slots = NULL;
	}

	// 重置哈希表的各个函数指针为 NULL
	hm->hash_func = NULL;
	hm->compare_func = NULL;
	hm->mem_malloc_func = NULL;
	hm->mem_realloc_func = NULL;
	hm->mem_free_func = NULL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个哈希映射（hashmap）的获取操作，传入一个键（key），查找并返回对应的值（value）。如果哈希映射为空，或者在映射中没有找到匹配的键，则返回失败（FAIL）。
 ******************************************************************************/
// 定义一个函数 int zbx_hashmap_get(zbx_hashmap_t *hm, zbx_uint64_t key)，这个函数的主要目的是在哈希映射（hashmap）中查找指定的键（key）对应的值（value）。

int	zbx_hashmap_get(zbx_hashmap_t *hm, zbx_uint64_t key)
{
	// 定义一个整型变量 i，用于循环遍历哈希映射的槽（slot）。
	int			i, value = FAIL;
	// 定义一个zbx_hash_t类型的变量 hash，用于存储计算得到的哈希值。
	zbx_hash_t		hash;
	// 定义一个ZBX_HASHMAP_SLOT_T类型的指针变量 slot，指向哈希映射的槽。
	ZBX_HASHMAP_SLOT_T	*slot;

	// 检查哈希映射的槽数量（num_slots）是否为0，如果为0，则直接返回失败（FAIL）。
	if (0 == hm->num_slots)
		return FAIL;

	// 计算键（key）的哈希值（hash），并将结果存储在 hash 变量中。
	hash = hm->hash_func(&key);
	// 根据计算得到的哈希值，找到对应的哈希映射槽。
	slot = &hm->slots[hash % hm->num_slots];

	// 遍历哈希映射槽中的所有键值对（entries），查找与给定键（key）匹配的键值对。
	for (i = 0; i < slot->entries_num; i++)
	{
		// 检查当前遍历到的键值对（slot->entries[i]）的键是否与给定键（key）相同。
		if (0 == hm->compare_func(&slot->entries[i].key, &key))
		{
			// 如果找到匹配的键值对，将该键值对的值（value）赋值给 value 变量，并跳出循环。
			value = slot->entries[i].value;
			break;
		}
	}

	// 遍历结束，如果没有找到匹配的键值对，则返回失败（FAIL），否则返回找到的键值对的值（value）。
	return value;
}

/******************************************************************************
 * *
 *这块代码的主要目的是在哈希映射（hashmap）中设置一个键值对。具体来说，它接收一个键（key）、一个值（value）和一个哈希映射指针（hm），然后在哈希映射中查找是否存在相同的键。如果存在，则更新该键的值；如果不存在，则在哈希映射中添加一个新的键值对。同时，代码还处理了哈希映射扩容的情况。在整个过程中，代码遵循了哈希映射的基本原理，保证了较高的查找效率。
 ******************************************************************************/
// 定义一个函数，用于在哈希映射（hashmap）中设置一个键值对
void zbx_hashmap_set(zbx_hashmap_t *hm, zbx_uint64_t key, int value)
{
	// 定义一些变量
	int			i;
	zbx_hash_t		hash;
	ZBX_HASHMAP_SLOT_T	*slot;

	// 如果哈希映射中还没有槽（slot）
	if (0 == hm->num_slots)
		// 初始化槽
		zbx_hashmap_init_slots(hm, ZBX_HASHMAP_DEFAULT_SLOTS);

	// 计算键（key）的哈希值
	hash = hm->hash_func(&key);
	// 获取对应键值的槽位置
	slot = &hm->slots[hash % hm->num_slots];

	// 遍历当前槽中的键值对
	for (i = 0; i < slot->entries_num; i++)
	{
		// 如果找到相同的键
		if (0 == hm->compare_func(&slot->entries[i].key, &key))
		{
			// 更新值
			slot->entries[i].value = value;
			// 跳出循环
			break;
		}
	}

	// 如果没有找到相同的键
	if (i == slot->entries_num)
	{
		// 确保槽中有空闲位置
		__hashmap_ensure_free_entry(hm, slot);
		// 添加新的键值对
		slot->entries[i].key = key;
		slot->entries[i].value = value;
		// 更新槽中的键值对数量
		slot->entries_num++;
		// 更新哈希映射中的数据数量
		hm->num_data++;

		// 如果哈希映射中的数据数量达到阈值
		if (hm->num_data >= hm->num_slots * CRIT_LOAD_FACTOR)
		{
			// 计算新的槽数量
			int			inc_slots, s;
			ZBX_HASHMAP_SLOT_T	*new_slot;

			inc_slots = next_prime(hm->num_slots * SLOT_GROWTH_FACTOR);

			// 重新分配内存
			hm->slots = (ZBX_HASHMAP_SLOT_T *)hm->mem_realloc_func(hm->slots, inc_slots * sizeof(ZBX_HASHMAP_SLOT_T));
			memset(hm->slots + hm->num_slots, 0, (inc_slots - hm->num_slots) * sizeof(ZBX_HASHMAP_SLOT_T));

			// 迁移键值对
			for (s = 0; s < hm->num_slots; s++)
			{
				slot = &hm->slots[s];

				for (i = 0; i < slot->entries_num; i++)
				{
					hash = hm->hash_func(&slot->entries[i].key);
					new_slot = &hm->slots[hash % inc_slots];

					// 如果新槽与原槽不同
					if (slot != new_slot)
					{
						// 确保新槽中有空闲位置
						__hashmap_ensure_free_entry(hm, new_slot);
						// 移动键值对
						new_slot->entries[new_slot->entries_num] = slot->entries[i];
						new_slot->entries_num++;

						// 移动原槽中的键值对
						slot->entries[i] = slot->entries[slot->entries_num - 1];
						slot->entries_num--;
						i--;
					}
				}
			}

			// 更新哈希映射的槽数量
			hm->num_slots = inc_slots;
		}
	}
}

/******************************************************************************
 * *
 *代码主要目的是从哈希表中删除一个键值对。整个代码块描述如下：
 *
 *1. 定义一个整数变量 i，用于循环遍历哈希表的槽位。
 *2. 计算键 key 的哈希值，并将其存储在 hash 变量中。
 *3. 根据哈希值计算槽位索引，并将槽位地址存储在 slot 指针中。
 *4. 遍历槽位中的键值对，直到找到要删除的键。
 *5. 如果找到要删除的键，将键值对向前移动一个位置，并更新槽位中的键值对数量和哈希表中的数据数量。
 *6. 跳出循环，结束删除操作。
 ******************************************************************************/
// 定义一个函数，用于从哈希表中删除一个键值对
void zbx_hashmap_remove(zbx_hashmap_t *hm, zbx_uint64_t key)
{
	// 定义一个整数变量 i，用于循环遍历哈希表的槽位
	int i;

	// 定义一个哈希变量 hash，用于存储计算出的哈希值
	zbx_hash_t hash;

	// 定义一个指向哈希表槽位的指针，初始值为哈希表第一个槽位地址
	ZBX_HASHMAP_SLOT_T *slot;

	// 判断哈希表是否有空槽位，如果没有，直接返回
	if (0 == hm->num_slots)
		return;

	// 计算键 key 的哈希值，并将其存储在 hash 变量中
	hash = hm->hash_func(&key);

	// 根据哈希值计算槽位索引，并将槽位地址存储在 slot 指针中
	slot = &hm->slots[hash % hm->num_slots];

	// 遍历槽位中的键值对，直到找到要删除的键
	for (i = 0; i < slot->entries_num; i++)
	{
		// 如果找到要删除的键，即键值对中的 key 与传入的 key 相等
		if (0 == hm->compare_func(&slot->entries[i].key, &key))
		{
			// 将要删除的键值对的后一个元素向前移动一个位置
			slot->entries[i] = slot->entries[slot->entries_num - 1];

			// 减一槽位中的键值对数量
			slot->entries_num--;

			// 减一哈希表中的数据数量
			hm->num_data--;

			// 找到要删除的键后，跳出循环
			break;
		}
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是清空一个哈希表（zbx_hashmap_t 类型）的所有数据。函数名为 zbx_hashmap_clear，接收一个指向哈希表的指针作为参数。通过遍历哈希表的每个槽位，将槽位中的 entries_num 置为 0，表示清空该槽位的所有数据。同时，将哈希表的数据数量（num_data）置为 0，表示清空整个哈希表的数据。函数执行完毕后，哈希表将被清空，准备重新使用。
 ******************************************************************************/
// 定义一个函数，用于清空哈希表
void zbx_hashmap_clear(zbx_hashmap_t *hm)
{
	// 定义一个循环变量 i，用于遍历哈希表的槽位数
	int i;

	// 遍历哈希表的每个槽位
	for (i = 0; i < hm->num_slots; i++)
		// 将每个槽位的 entries_num 置为 0，表示清空该槽位的所有数据
		hm->slots[i].entries_num = 0;

	// 将哈希表的数据数量置为 0，表示清空整个哈希表的数据
	hm->num_data = 0;
}

