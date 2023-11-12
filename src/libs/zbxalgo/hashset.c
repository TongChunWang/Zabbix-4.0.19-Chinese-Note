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

static void	__hashset_free_entry(zbx_hashset_t *hs, ZBX_HASHSET_ENTRY_T *entry);

/******************************************************************************
 * *
 *这块代码的主要目的是定义一些常量和私有哈希集函数，其中`__hashset_free_entry`函数用于释放哈希集中的entry。整个代码块的功能是初始化哈希集的相关参数和功能。
 *
 *注释详细解释了每个定义的常量及其作用，以及`__hashset_free_entry`函数的实现。这段代码为后续的哈希集操作提供了基础设置。
 ******************************************************************************/
// 定义常量，用于计算哈希集的负载因子和槽增长因子
#define CRIT_LOAD_FACTOR 4/5
#define SLOT_GROWTH_FACTOR 3/2

// 定义默认的槽数量
#define ZBX_HASHSET_DEFAULT_SLOTS 10

/* 私有哈希集函数 */

// 定义一个内部函数，用于释放哈希集中的entry
static void	__hashset_free_entry(zbx_hashset_t *hs, ZBX_HASHSET_ENTRY_T *entry)
{
	// 如果哈希集有清理函数，则调用清理函数处理entry中的数据
	if (NULL != hs->clean_func)
		hs->clean_func(entry->data);

	// 调用哈希集的内存释放函数，释放entry占用的内存
	hs->mem_free_func(entry);
}


/******************************************************************************
 * *
 *这块代码的主要目的是初始化一个zbx哈希集，根据传入的初始化大小分配槽位和内存，计算下一个质数作为槽位数，并将槽位指针数组清零。如果内存分配失败，则返回失败。初始化完成后，返回成功。
 ******************************************************************************/
// 定义一个函数，用于初始化zbx哈希集的槽位
static int zbx_hashset_init_slots(zbx_hashset_t *hs, size_t init_size)
{
	// 定义一个变量，记录哈希集中的数据数量
	hs->num_data = 0;

	// 判断初始化大小是否大于0
	if (0 < init_size)
	{
		// 计算下一个质数，作为哈希集的槽位数
		hs->num_slots = next_prime(init_size);

		// 分配内存，存储哈希集的槽位指针
		if (NULL == (hs->slots = (ZBX_HASHSET_ENTRY_T **)hs->mem_malloc_func(NULL, hs->num_slots * sizeof(ZBX_HASHSET_ENTRY_T *))))
			// 如果内存分配失败，返回失败
			return FAIL;

		// 将哈希集的槽位指针数组清零
		memset(hs->slots, 0, hs->num_slots * sizeof(ZBX_HASHSET_ENTRY_T *));
	}
	else
	{
		// 如果初始化大小为0，则槽位数为0，槽位指针为NULL
		hs->num_slots = 0;
		hs->slots = NULL;
	}

	// 初始化成功，返回成功
	return SUCCEED;
}


/* public hashset interface */
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个哈希集合，通过调用`zbx_hashset_create_ext`函数来实现。该函数接收多个参数，包括哈希集合的结构体指针、初始大小、哈希函数、比较函数以及内存分配、重分配和释放的默认函数。在创建哈希集合时，这些参数将用于初始化和配置哈希集合的结构。
 ******************************************************************************/
// 定义一个函数，用于创建一个哈希集合
void zbx_hashset_create(zbx_hashset_t *hs, size_t init_size,
                         zbx_hash_func_t hash_func,
                         zbx_compare_func_t compare_func)
{
    // 调用zbx_hashset_create_ext函数来创建哈希集合，传入参数如下：
    // hs：指向哈希集合的结构体指针
    // init_size：哈希集合的初始大小
    // hash_func：用于计算哈希值的函数
	zbx_hashset_create_ext(hs, init_size, hash_func, compare_func, NULL,
				ZBX_DEFAULT_MEM_MALLOC_FUNC,
				ZBX_DEFAULT_MEM_REALLOC_FUNC,
				ZBX_DEFAULT_MEM_FREE_FUNC);
    // compare_func：用于比较哈希值的函数
    // NULL：分配内存的额外参数（此处为NULL，使用默认参数）
    // ZBX_DEFAULT_MEM_MALLOC_FUNC：内存分配函数（默认）
    // ZBX_DEFAULT_MEM_REALLOC_FUNC：内存重分配函数（默认）
    // ZBX_DEFAULT_MEM_FREE_FUNC：内存释放函数（默认）
}

/******************************************************************************
 * *
 *这块代码的主要目的是创建一个扩展的哈希集合，并设置哈希集合的相关参数，如哈希函数、比较函数、清理函数、内存分配函数、内存重新分配函数和内存释放函数。最后调用`zbx_hashset_init_slots`函数初始化哈希集合的槽位。整个代码块的功能相当于一个完整的哈希集合创建过程。
 ******************************************************************************/
// 定义一个函数，用于创建一个扩展的哈希集合
void zbx_hashset_create_ext(zbx_hashset_t *hs, size_t init_size,
                             zbx_hash_func_t hash_func,
                             zbx_compare_func_t compare_func,
                             zbx_clean_func_t clean_func,
                             zbx_mem_malloc_func_t mem_malloc_func,
                             zbx_mem_realloc_func_t mem_realloc_func,
                             zbx_mem_free_func_t mem_free_func)
{
	// 将传入的哈希函数赋值给哈希集合结构体的hash_func成员
	hs->hash_func = hash_func;

	// 将传入的比较函数赋值给哈希集合结构体的compare_func成员
	hs->compare_func = compare_func;

	// 将传入的清理函数赋值给哈希集合结构体的clean_func成员
	hs->clean_func = clean_func;

	// 将传入的内存分配函数赋值给哈希集合结构体的mem_malloc_func成员
	hs->mem_malloc_func = mem_malloc_func;

	// 将传入的内存重新分配函数赋值给哈希集合结构体的mem_realloc_func成员
	hs->mem_realloc_func = mem_realloc_func;

	// 将传入的内存释放函数赋值给哈希集合结构体的mem_free_func成员
	hs->mem_free_func = mem_free_func;

	// 初始化哈希集合的槽位，槽位数量为init_size
	zbx_hashset_init_slots(hs, init_size);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：用于销毁一个zbx_hashset结构体，将其中存储的数据节点和桶释放，并将相关函数指针设置为NULL。
 ******************************************************************************/
void	zbx_hashset_destroy(zbx_hashset_t *hs) // 定义一个函数，用于销毁zbx_hashset结构体
{
	int			i; // 定义一个整型变量i，用于循环计数
	ZBX_HASHSET_ENTRY_T	*entry, *next_entry; // 定义两个指向zbx_hashset_entry结构体的指针，分别指向当前节点和下一个节点

	for (i = 0; i < hs->num_slots; i++) // 遍历hs->num_slots个桶
	{
		entry = hs->slots[i]; // 获取当前桶的第一个节点

		while (NULL != entry) // 遍历当前节点的所有子节点
		{
			next_entry = entry->next; // 保存下一个节点
			__hashset_free_entry(hs, entry); // 释放当前节点，并将hs指向下一个节点
			entry = next_entry; // 更新当前节点为下一个节点
		}
	}

	hs->num_data = 0; // 清空数据节点数量
	hs->num_slots = 0; // 清空桶数量

	if (NULL != hs->slots) // 如果hs->slots不为空
	{
		hs->mem_free_func(hs->slots); // 释放hs->slots所指向的内存
		hs->slots = NULL; // 将hs->slots置空
	}

	hs->hash_func = NULL; // 将hs->hash_func设置为NULL，表示不使用哈希函数
	hs->compare_func = NULL; // 将hs->compare_func设置为NULL，表示不使用比较函数
	hs->mem_malloc_func = NULL; // 将hs->mem_malloc_func设置为NULL，表示不使用内存分配函数
	hs->mem_realloc_func = NULL; // 将hs->mem_realloc_func设置为NULL，表示不使用内存重新分配函数
	hs->mem_free_func = NULL; // 将hs->mem_free_func设置为NULL，表示不使用内存释放函数
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_hashset_reserve                                              *
 *                                                                            *
 * Purpose: allocation not less than the required number of slots for hashset *
 *                                                                            *
 * Parameters: hs            - [IN] the destination hashset                   *
 *             num_slots_req - [IN] the number of required slots              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是预分配一个哈希集合所需的空间。当哈希集合的核心槽数量不足时，函数会首先初始化核心槽；如果请求的核心槽数量大于当前核心槽数量乘以临界负载因子，函数会进行扩容，并将原核心槽中的元素迁移到新核心槽中。最后，更新哈希集合的核心槽数量。整个过程完成后，返回SUCCEED表示执行成功。
 ******************************************************************************/
// 定义一个函数，用于预分配哈希集合所需的空间，参数为哈希集合指针和所需的核心槽数量
int zbx_hashset_reserve(zbx_hashset_t *hs, int num_slots_req)
{
	// 如果哈希集合目前没有核心槽，则进行初始化
	if (0 == hs->num_slots)
	{
		/* 防止在需要相同数量的核心槽的情况下进行第二次重定位的修正 */
		if (SUCCEED != zbx_hashset_init_slots(hs, MAX(ZBX_HASHSET_DEFAULT_SLOTS,
				num_slots_req * (2 - CRIT_LOAD_FACTOR) + 1)))
		{
			return FAIL;
		}
	}
	// 如果请求的核心槽数量大于等于哈希集合当前的核心槽数量乘以临界负载因子，则进行扩容
	else if (num_slots_req >= hs->num_slots * CRIT_LOAD_FACTOR)
	{
		int			inc_slots, new_slot, slot;
		void			*slots;
		ZBX_HASHSET_ENTRY_T	**prev_next, *curr_entry, *tmp;

		// 计算下一个质数，作为扩容后核心槽的数量
		inc_slots = next_prime(hs->num_slots * SLOT_GROWTH_FACTOR);

		// 如果内存分配失败，返回失败
		if (NULL == (slots = hs->mem_realloc_func(hs->slots, inc_slots * sizeof(ZBX_HASHSET_ENTRY_T *))))
			return FAIL;

		// 更新哈希集合的核心槽指针
		hs->slots = (ZBX_HASHSET_ENTRY_T **)slots;

		// 初始化新核心槽区域为0
		memset(hs->slots + hs->num_slots, 0, (inc_slots - hs->num_slots) * sizeof(ZBX_HASHSET_ENTRY_T *));

		// 遍历原核心槽区域，进行元素迁移
		for (slot = 0; slot < hs->num_slots; slot++)
		{
			prev_next = &hs->slots[slot];
			curr_entry = hs->slots[slot];

			while (NULL != curr_entry)
			{
				// 计算元素在新核心槽区域的位置
				if (slot != (new_slot = curr_entry->hash % inc_slots))
				{
					tmp = curr_entry->next;
					curr_entry->next = hs->slots[new_slot];
					hs->slots[new_slot] = curr_entry;

					*prev_next = tmp;
					curr_entry = tmp;
				}
				else
				{
					prev_next = &curr_entry->next;
					curr_entry = curr_entry->next;
				}
			}
		}

		// 更新哈希集合的核心槽数量
		hs->num_slots = inc_slots;
	}

	// 函数执行成功，返回SUCCEED
	return SUCCEED;
}

// 函数接收三个参数：
//   hs：指向哈希集合的指针。
//   data：要插入的数据指针。
//   size：数据的大小。
// 函数的主要目的是：向哈希集合中插入数据。
void	*zbx_hashset_insert(zbx_hashset_t *hs, const void *data, size_t size)
{
    // 调用 zbx_hashset_insert_ext 函数，该函数用于执行哈希集合的插入操作。
    // 参数说明：
    //   hs：指向哈希集合的指针。
    //   data：要插入的数据指针。
    //   size：数据的大小。
    //   0：表示不需要进行扩容操作。
    return zbx_hashset_insert_ext(hs, data, size, 0);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个哈希集合（zbx_hashset）的操作，具体包括：
 *
 *1. 向哈希集合中插入数据；
 *2. 计算数据的哈希值；
 *3. 查找哈希值对应的槽位；
 *4. 遍历哈希表，查找是否有相同哈希值的项；
 *5. 如果没有找到相同哈希值的项，则分配新的内存空间，拷贝数据，并更新哈希表；
 *6. 返回待插入数据的指针。
 ******************************************************************************/
// 定义一个函数，用于向哈希集合中插入数据
// 参数：
//   hs：哈希集合指针
//   data：待插入的数据指针
//   size：数据大小
//   offset：数据偏移量
// 返回值：
//   如果插入成功，返回待插入数据的指针；如果插入失败，返回NULL
void *zbx_hashset_insert_ext(zbx_hashset_t *hs, const void *data, size_t size, size_t offset)
{
	// 1. 定义一个整型变量slot，用于存储哈希值对应的槽位
	int			slot;
	// 2. 定义一个zbx_hash_t类型的变量hash，用于存储数据对应的哈希值
	zbx_hash_t		hash;
	// 3. 定义一个zbx_hashset_entry_t类型的指针变量entry，用于存储哈希表项
	ZBX_HASHSET_ENTRY_T	*entry;

	// 4. 如果哈希集合中还没有槽位，并且初始化槽位失败，返回NULL
	if (0 == hs->num_slots && SUCCEED != zbx_hashset_init_slots(hs, ZBX_HASHSET_DEFAULT_SLOTS))
		return NULL;

	// 5. 使用哈希函数计算数据对应的哈希值
	hash = hs->hash_func(data);

	// 6. 计算哈希值对应的槽位
	slot = hash % hs->num_slots;
	// 7. 指向对应槽位的哈希表项
	entry = hs->slots[slot];

	// 8. 遍历哈希表，查找是否有相同的哈希值
	while (NULL != entry)
	{
		// 9. 如果找到相同哈希值的项，则退出循环
		if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
			break;

		// 10. 遍历到下一个哈希表项
		entry = entry->next;
	}

	// 11. 如果没有找到相同哈希值的项，则进行以下操作：
	if (NULL == entry)
	{
		// 12. 如果分配内存失败，返回NULL
		if (SUCCEED != zbx_hashset_reserve(hs, hs->num_data + 1))
			return NULL;

		// 13. 重新计算新的槽位
		slot = hash % hs->num_slots;

		// 14. 分配一个新的哈希表项，并初始化
		if (NULL == (entry = (ZBX_HASHSET_ENTRY_T *)hs->mem_malloc_func(NULL, offsetof(ZBX_HASHSET_ENTRY_T, data) + size)))
			return NULL;

		// 15. 拷贝数据到新的哈希表项
		memcpy((char *)entry->data + offset, (const char *)data + offset, size - offset);
		// 16. 设置哈希值和下一个哈希表项
		entry->hash = hash;
		entry->next = hs->slots[slot];
		// 17. 将新的哈希表项添加到对应槽位
		hs->slots[slot] = entry;
		// 18. 更新哈希集合中的数据数量
		hs->num_data++;
	}

	// 19. 返回待插入数据的指针
	return entry->data;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是在一个哈希集中查找给定数据。该函数接收一个指向哈希集的结构体指针和一个指向待查找数据的指针，通过计算数据的哈希值来确定其在哈希集中的位置，然后遍历哈希集中的所有条目，找到匹配的条目并返回其数据。如果没有找到匹配的条目，则返回NULL。
 ******************************************************************************/
// 定义一个函数指针，用于在哈希集中查找给定数据
void *zbx_hashset_search(zbx_hashset_t *hs, const void *data)
{
	// 定义一个整数变量，用于存储哈希值对应的槽位
	int			slot;
	// 定义一个哈希结构体
	zbx_hash_t		hash;
	// 定义一个指向哈希集条目结构的指针
	ZBX_HASHSET_ENTRY_T	*entry;

	// 如果哈希集中的槽位数为0，说明哈希集为空，直接返回NULL
	if (0 == hs->num_slots)
		return NULL;

	// 计算给定数据的哈希值
	hash = hs->hash_func(data);

	// 计算哈希值对应的槽位
	slot = hash % hs->num_slots;
	// 获取槽位对应的哈希集条目
	entry = hs->slots[slot];

	// 遍历哈希集中的所有条目
	while (NULL != entry)
	{
		// 如果当前条目的哈希值等于给定数据的哈希值，并且比较函数返回0，说明找到匹配的条目
		if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
			break;

		// 否则，继续遍历下一个条目
		entry = entry->next;
	}

	// 如果找到匹配的条目，返回该条目的数据；否则返回NULL
	return (NULL != entry ? entry->data : NULL);
}


/******************************************************************************
 *                                                                            *
 * Purpose: remove a hashset entry using comparison with the given data       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个哈希集（zbx_hashset）的删除功能。传入需要删除的数据（const void *data），找到哈希集中与之匹配的节点，并将其从链表中移除，释放内存，减少哈希集中的数据数量。如果找不到匹配的节点，则遍历链表查找相同哈希值的数据并进行删除操作。
 ******************************************************************************/
void	zbx_hashset_remove(zbx_hashset_t *hs, const void *data)
{
	// 定义变量，用于计算哈希值和索引
	int			slot;
	zbx_hash_t		hash;
	ZBX_HASHSET_ENTRY_T	*entry;

	// 如果哈希集中的槽位为0，直接返回
	if (0 == hs->num_slots)
		return;

	// 计算数据的哈希值
	hash = hs->hash_func(data);

	// 利用哈希值计算索引
	slot = hash % hs->num_slots;
	entry = hs->slots[slot];

	// 检查entry是否为空，如果不为空，进行以下操作：
	if (NULL != entry)
	{
		// 判断entry的哈希值和传入的数据是否一致，如果一致，执行以下操作：
		if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
		{
			// 将entry从链表中移除
			hs->slots[slot] = entry->next;
			// 释放entry内存
			__hashset_free_entry(hs, entry);
			// 减少数据数量
			hs->num_data--;
		}
		// 如果entry的哈希值和传入的数据不一致，遍历链表查找相同哈希值的数据
		else
		{
			ZBX_HASHSET_ENTRY_T	*prev_entry;

			prev_entry = entry;
			entry = entry->next;

			while (NULL != entry)
			{
				// 找到相同哈希值的数据，执行以下操作：
				if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
				{
					// 将entry从链表中移除
					prev_entry->next = entry->next;
					// 释放entry内存
					__hashset_free_entry(hs, entry);
					// 减少数据数量
					hs->num_data--;
					// 跳出循环
					break;
				}

				prev_entry = entry;
				entry = entry->next;
			}
		}
	}
}


/******************************************************************************
 *                                                                            *
 * Purpose: remove a hashset entry using a data pointer returned to the user  *
 *          by zbx_hashset_insert[_ext]() and zbx_hashset_search() functions  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从哈希表中删除一个指定的数据。该函数接收两个参数：一个指向哈希表的指针`hs`和一个指向待删除数据的指针`data`。在函数内部，首先判断哈希表是否为空，如果为空则直接返回。接下来计算待删除数据在哈希表中的偏移量，以获取数据对应的哈希表项。然后找到数据对应的槽位，并遍历链表查找待删除数据。如果找到待删除数据，将其从链表中移除，并释放内存。最后减少哈希表中的数据数量。
 ******************************************************************************/
void	zbx_hashset_remove_direct(zbx_hashset_t *hs, const void *data)
{
	// 定义变量，用于存储哈希表槽位、数据entry指针以及迭代器entry指针
	int			slot;
	ZBX_HASHSET_ENTRY_T	*data_entry, *iter_entry;

	// 如果哈希表为空，直接返回
	if (0 == hs->num_slots)
		return;

	// 计算数据在哈希表中的偏移量，以便获取数据entry
	data_entry = (ZBX_HASHSET_ENTRY_T *)((const char *)data - offsetof(ZBX_HASHSET_ENTRY_T, data));

	// 计算数据对应的槽位
	slot = data_entry->hash % hs->num_slots;
	// 获取对应槽位的哈希表项
	iter_entry = hs->slots[slot];

	// 如果迭代器entry不为空
	if (NULL != iter_entry)
	{
		// 如果迭代器entry就是数据entry，直接将其从哈希表中移除
		if (iter_entry == data_entry)
		{
			hs->slots[slot] = data_entry->next;
			// 释放数据entry内存
			__hashset_free_entry(hs, data_entry);
			// 减少哈希表中的数据数量
			hs->num_data--;
		}
		// 如果迭代器entry不是数据entry，则遍历链表查找数据entry
		else
		{
			while (NULL != iter_entry->next)
			{
				// 如果找到数据entry，将其从链表中移除
				if (iter_entry->next == data_entry)
				{
					iter_entry->next = data_entry->next;
					// 释放数据entry内存
					__hashset_free_entry(hs, data_entry);
					// 减少哈希表中的数据数量
					hs->num_data--;
					// 跳出循环
					break;
				}

				// 迭代器向前移动
				iter_entry = iter_entry->next;
			}
		}
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是清空一个zbx_hashset_t类型的哈希集，将哈希集中的所有节点从链表中移除，并释放节点所占用的内存。最后将哈希集的数据数量清零。
 ******************************************************************************/
void	zbx_hashset_clear(zbx_hashset_t *hs) // 定义一个函数zbx_hashset_clear，参数为一个zbx_hashset_t类型的指针hs
{
	int			slot; // 定义一个整型变量slot，用于循环计数
	ZBX_HASHSET_ENTRY_T	*entry; // 定义一个指向ZBX_HASHSET_ENTRY_T类型的指针entry，用于遍历哈希集

	for (slot = 0; slot < hs->num_slots; slot++) // 遍历哈希集的槽位数
	{
		while (NULL != hs->slots[slot]) // 遍历哈希集中的每个链表节点
		{
			entry = hs->slots[slot]; // 获取当前节点
			hs->slots[slot] = entry->next; // 将当前节点从链表中移除
			__hashset_free_entry(hs, entry); // 释放当前节点的内存
		}
	}

	hs->num_data = 0; // 将哈希集的数据数量清零
}


#define	ITER_START	(-1)
#define	ITER_FINISH	(-2)
/******************************************************************************
 * *
 *这块代码的主要目的是重置哈希集合的迭代器。函数接收一个zbx_hashset_t类型的指针和一个zbx_hashset_iter_t类型的指针作为参数。首先，将迭代器的哈希集合指针设置为传入的哈希集合指针hs，然后将迭代器的当前槽位设置为ITER_START，即从头开始迭代。这样，就可以重新开始对哈希集合的迭代过程。
 ******************************************************************************/
// 定义一个函数，名为 zbx_hashset_iter_reset，接收两个参数：
// 参数1：zbx_hashset_t类型的指针hs，指向哈希集合；
// 参数2：zbx_hashset_iter_t类型的指针iter，用于迭代哈希集合。
void zbx_hashset_iter_reset(zbx_hashset_t *hs, zbx_hashset_iter_t *iter)
{
	// 将iter指向的哈希集合设置为hs；
	iter->hashset = hs;
	// 将iter指向的当前槽位设置为ITER_START，即从头开始迭代；
	iter->slot = ITER_START;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个迭代器，用于遍历zbx_hashset中的下一个元素。该函数接收一个zbx_hashset_iter_t类型的指针作为参数，该指针包含了迭代器的状态信息以及zbx_hashset的相关信息。函数通过判断迭代器的状态和zbx_hashset中的元素情况，来实现遍历下一个元素并返回其数据。在遍历过程中，如果到达了zbx_hashset的末尾，则返回NULL表示遍历结束。
 ******************************************************************************/
// 定义一个函数指针，用于迭代zbx_hashset的下一个元素
void *zbx_hashset_iter_next(zbx_hashset_iter_t *iter)
{
	// 判断当前迭代器是否已经到达结尾
	if (ITER_FINISH == iter->slot)
		return NULL;

	// 判断当前迭代器是否处于起始位置，且下一个元素不为空
	if (ITER_START != iter->slot && NULL != iter->entry && NULL != iter->entry->next)
	{
		// 迭代器向前移动一个元素，并返回当前元素的data
		iter->entry = iter->entry->next;
		return iter->entry->data;
	}

	// 进入一个无限循环，用于迭代zbx_hashset中的下一个元素
	while (1)
	{
		// 迭代器slot值加1
		iter->slot++;

		// 判断迭代器是否到达了zbx_hashset的末尾
		if (iter->slot == iter->hashset->num_slots)
		{
			// 将迭代器slot设置为ITER_FINISH，表示到达结尾，并返回NULL
			iter->slot = ITER_FINISH;
			return NULL;
		}

		// 判断当前slot位置的元素是否不为空
		if (NULL != iter->hashset->slots[iter->slot])
		{
			// 迭代器向前移动一个元素，并返回当前元素的data
			iter->entry = iter->hashset->slots[iter->slot];
			return iter->entry->data;
		}
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是删除哈希集合中的一个元素。该函数接收一个指向`zbx_hashset_iter_t`结构体的指针作为参数，该结构体包含哈希集合的相关信息和迭代器的状态。函数首先检查迭代器是否有效，如果无效，则输出错误日志并退出程序。接下来，根据迭代器的状态和哈希集合的元素关系，分别处理两种情况：1）如果迭代器对应的哈希槽的元素就是待删除的元素，直接将其删除；2）如果迭代器对应的哈希槽的元素不是待删除的元素，则遍历到待删除元素的前一个元素，将其删除。在删除过程中，还会释放待删除元素占用的内存，并减少哈希集合中的数据元素数量。最后，更新迭代器的状态，使其指向下一个元素。
 ******************************************************************************/
// 定义一个函数，用于从哈希集合中删除一个元素
void zbx_hashset_iter_remove(zbx_hashset_iter_t *iter)
{
	// 检查迭代器是否有效
	if (ITER_START == iter->slot || ITER_FINISH == iter->slot || NULL == iter->entry)
	{
		// 输出错误日志
		zabbix_log(LOG_LEVEL_CRIT, "removing a hashset entry through a bad iterator");
		// 程序退出，返回失败
		exit(EXIT_FAILURE);
	}

	// 如果当前迭代器对应的哈希槽的元素就是待删除的元素
	if (iter->hashset->slots[iter->slot] == iter->entry)
	{
		// 将待删除元素的下一个元素设置为迭代器对应的哈希槽的下一个元素
		iter->hashset->slots[iter->slot] = iter->entry->next;
		// 释放待删除元素占用的内存
		__hashset_free_entry(iter->hashset, iter->entry);
		// 减少哈希集合中的数据元素数量
		iter->hashset->num_data--;

		// 迭代器对应的哈希槽减1
		iter->slot--;
		// 将迭代器指向的元素设置为NULL
		iter->entry = NULL;
	}
	else
	{
		// 初始化一个临时变量，用于保存待删除元素的前一个元素
		ZBX_HASHSET_ENTRY_T *prev_entry = iter->hashset->slots[iter->slot];

		// 遍历待删除元素的前一个元素
		while (prev_entry->next != iter->entry)
			// 更新prev_entry为待删除元素的前一个元素
			prev_entry = prev_entry->next;

		// 将待删除元素的下一个元素设置为prev_entry的下一个元素
		prev_entry->next = iter->entry->next;
		// 释放待删除元素占用的内存
		__hashset_free_entry(iter->hashset, iter->entry);
		// 减少哈希集合中的数据元素数量
		iter->hashset->num_data--;

		// 将迭代器指向的元素设置为prev_entry
		iter->entry = prev_entry;
	}
}

