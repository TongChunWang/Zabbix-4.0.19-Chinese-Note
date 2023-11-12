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
#include "zbxalgo.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_values_num                                         *
 *                                                                            *
 * Purpose: calculates the number of values in queue                          *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 * Return value: The number of values in queue                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算队列中的元素个数。首先，定义一个整型变量values_num用于存储队列中的元素个数。接着，计算队列头指针与队列尾指针之间的距离，即队列中的元素个数。然后，判断values_num是否大于0，如果不大于0，则说明队列中没有元素。如果小于0，则将其加上分配的内存空间大小，以确保结果正确。最后，返回计算得到的队列中的元素个数。
 ******************************************************************************/
// 定义一个函数zbx_queue_ptr_values_num，接收一个zbx_queue_ptr_t类型的指针作为参数
int	zbx_queue_ptr_values_num(zbx_queue_ptr_t *queue)
{
	// 定义一个整型变量values_num，用于存储队列中的元素个数
	int	values_num;

	// 计算队列头指针与队列尾指针之间的距离，即队列中的元素个数
	values_num = queue->head_pos - queue->tail_pos;

	// 判断values_num是否大于0，如果不大于0，则说明队列中没有元素
	if (0 > values_num)
	{
		// 如果values_num小于0，则将其加上分配的内存空间大小，以确保结果正确
		values_num += queue->alloc_num;
	}

	// 返回计算得到的队列中的元素个数
	return values_num;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_reserve                                            *
 *                                                                            *
 * Purpose: reserves space in queue for additional values                     *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *             num   - [IN] the number of additional values to reserve        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：当队列中的元素个数接近或超过队列的分配空间大小时，自动扩容队列，以便继续添加新的元素。在扩容过程中，如果队列中存在未使用的空间，则将头部元素向后移动，为新的元素分配空间。
 ******************************************************************************/
// 定义一个函数zbx_queue_ptr_reserve，接收两个参数，一个是zbx_queue_ptr_t类型的指针queue，另一个是整数类型的num。
void zbx_queue_ptr_reserve(zbx_queue_ptr_t *queue, int num)
{
	// 定义三个整数变量values_num、alloc_num和resize_num，用于后续计算使用。
	int	values_num, alloc_num, resize_num;

	// 调用zbx_queue_ptr_values_num函数获取队列中的元素个数，并将结果存储在变量values_num中。
	values_num = zbx_queue_ptr_values_num(queue);

	// 判断当前队列中的元素个数加上传入的num是否小于等于队列的分配空间大小。如果小于等于，则直接返回，不进行扩容。
	if (values_num + num + 1 <= queue->alloc_num)
		return;

	// 调用MAX函数，获取较大的值，用于后续分配内存时使用。这里是判断传入的num和队列现有的alloc_num哪个更大。
	alloc_num = MAX(queue->alloc_num + num + 1, queue->alloc_num * 1.5);
	// 重新分配队列的内存空间，将队列的指针values指向新分配的内存区域，并确保队列的alloc_num更新为alloc_num。
	queue->values = (void **)zbx_realloc(queue->values, alloc_num * sizeof(*queue->values));

	// 判断队列的尾指针是否大于头指针，如果大于，则说明队列中有未使用的空间，可以进行扩容操作。
	if (queue->tail_pos > queue->head_pos)
	{
		// 计算需要移动的元素个数，即新分配的内存区域大小减去原队列的alloc_num与tail_pos的差值。
		resize_num = alloc_num - queue->alloc_num;
		// 将队列头部的元素向后移动，移动的距离为resize_num，从而为新的元素分配空间。
		memmove(queue->values + queue->tail_pos + resize_num, queue->values + queue->tail_pos,
				(queue->alloc_num - queue->tail_pos) * sizeof(*queue->values));
		// 更新队列的tail_pos，使其指向新的内存区域。
		queue->tail_pos += resize_num;
	}

	// 更新队列的alloc_num为新的分配空间大小。
	queue->alloc_num = alloc_num;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_compact                                            *
 *                                                                            *
 * Purpose: compacts queue by freeing unused space                            *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对队列进行压缩，即将队列中连续的内存空洞合并，以提高内存使用效率。输出结果为压缩后的队列头指针、尾指针和分配的内存空间大小。
 ******************************************************************************/
void	zbx_queue_ptr_compact(zbx_queue_ptr_t *queue)
{
	// 定义两个整型变量，分别用于存储队列中的元素个数和分配的内存空间大小
	int values_num, alloc_num;

	// 计算队列中的元素个数
	values_num = zbx_queue_ptr_values_num(queue);
	// 计算新的分配内存空间大小，等于元素个数加1
	alloc_num = values_num + 1;

	// 如果新的分配内存空间大小等于当前分配的内存空间大小，则直接返回，无需压缩
	if (alloc_num == queue->alloc_num)
		return;

	// 判断队列尾指针是否大于队列头指针，如果大于，说明队列中有连续的内存空洞，可以进行压缩
	if (0 != queue->tail_pos)
	{
		if (queue->tail_pos > queue->head_pos)
		{
			// 移动队列头指针后面的元素到队列头指针位置，移动的元素个数为 alloc_num - tail_pos
			memmove(queue->values + queue->head_pos + 1, queue->values + queue->tail_pos,
					(queue->alloc_num - queue->tail_pos) * sizeof(*queue->values));
			// 将队列尾指针移到头指针后面一个位置
			queue->tail_pos = queue->head_pos + 1;
		}
		else
		{
			// 移动队列中的所有元素到队列头指针位置，移动的元素个数为 values_num
			memmove(queue->values, queue->values + queue->tail_pos, values_num * sizeof(*queue->values));
			// 将队列尾指针重置为0，头指针重置为 values_num
			queue->tail_pos = 0;
			queue->head_pos = values_num;
		}
	}

	// 重新分配内存空间，将内存空间大小调整为 alloc_num
	queue->values = (void **)zbx_realloc(queue->values, alloc_num * sizeof(*queue->values));
	// 更新分配的内存空间大小
	queue->alloc_num = alloc_num;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_create                                             *
 *                                                                            *
 * Purpose: creates queue                                                     *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：创建一个zbx_queue_ptr_t类型的指针，并将其内存区域清零，为后续创建队列做准备。
 ******************************************************************************/
// 定义一个函数，参数是一个指向zbx_queue_ptr_t类型的指针
void zbx_queue_ptr_create(zbx_queue_ptr_t *queue)
{
    // 使用memset函数将指针queue所指向的内存区域清零，长度为queue类型的大小
	memset(queue, 0, sizeof(*queue));
    // 这里的queue是指向zbx_queue_ptr_t类型的指针，所以将其清零，为后续创建队列做准备
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_destroy                                            *
 *                                                                            *
 * Purpose: destroys queue                                                    *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是销毁一个zbx_queue_ptr结构体指针所指向的队列。函数名为zbx_queue_ptr_destroy，接收一个zbx_queue_ptr类型的指针作为参数。在函数内部，首先释放队列中的所有元素内存，然后返回。这个函数的作用是在程序运行过程中，当需要释放某个队列时，可以使用该函数来正确地清理队列资源。
 ******************************************************************************/
// 定义一个函数，用于销毁zbx_queue_ptr结构体指针所指向的队列
void zbx_queue_ptr_destroy(zbx_queue_ptr_t *queue)
{
    // 释放队列中的所有元素内存
    zbx_free(queue->values);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_push                                               *
 *                                                                            *
 * Purpose: pushes value in the queue                                         *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *             elem  - [IN] the value                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：向一个动态数组实现的队列中添加一个新元素。当队列已满时，会自动扩容队列空间，以便继续添加新元素。
 *
 *输出：
 *
 *```c
 *void zbx_queue_ptr_push(zbx_queue_ptr_t *queue, void *value)
 *{
 *    // 为队列分配更多空间，如果分配失败，返回错误码
 *    zbx_queue_ptr_reserve(queue, 1);
 *
 *    // 将新元素 value 添加到队列尾部
 *    queue->values[queue->head_pos++] = value;
 *
 *    // 如果队列头指针已经到达队列末尾，将其重置为0，以便继续添加新元素
 *    if (queue->head_pos == queue->alloc_num)
 *    {
 *        queue->head_pos = 0;
 *    }
 *}
 *```
 ******************************************************************************/
// 定义一个函数，用于向队列中添加一个元素
void zbx_queue_ptr_push(zbx_queue_ptr_t *queue, void *value)
{
    // 为队列分配更多空间，如果分配失败，返回错误码
    zbx_queue_ptr_reserve(queue, 1);

    // 将新元素 value 添加到队列尾部
    queue->values[queue->head_pos++] = value;

    // 如果队列头指针已经到达队列末尾，将其重置为0，以便继续添加新元素
    if (queue->head_pos == queue->alloc_num)
    {
        queue->head_pos = 0;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_pop                                                *
 *                                                                            *
 * Purpose: pops value in the queue                                           *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 * Return value: The first queue element.                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个队列的弹出操作。当调用这个函数时，它会从队列中弹出一个元素并将其存储在传入的void指针所指向的内存空间。如果队列中没有元素，那么返回NULL。在弹出元素后，还会自动调整队列的头尾指针位置，以维持队列的正常状态。
 ******************************************************************************/
// 定义一个函数zbx_queue_ptr_pop，参数是一个指向zbx_queue_ptr_t类型的指针
void *zbx_queue_ptr_pop(zbx_queue_ptr_t *queue)
{
    // 定义一个指向void类型的指针value，用于存储队列中的数据
    void *value;

    // 判断队列的头尾指针是否不相等，如果不相等，说明队列中有元素
    if (queue->tail_pos != queue->head_pos)
    {
        // 从队列尾部获取一个元素，并将其存储在value指针指向的内存空间
        value = queue->values[queue->tail_pos++];

        // 判断队列尾指针是否到达队列分配的空间末尾，如果到达，则重新指向头指针位置
        if (queue->tail_pos == queue->alloc_num)
            queue->tail_pos = 0;

        // 判断队列头指针是否到达队列分配的空间末尾，如果到达，则重新指向头指针位置
        if (queue->head_pos == queue->alloc_num)
            queue->head_pos = 0;
    }
    // 如果队列中没有元素，那么value为NULL
    else
        value = NULL;

    // 返回从队列中弹出的元素
    return value;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_queue_ptr_remove_value                                       *
 *                                                                            *
 * Purpose: removes specified value from queue                                *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *             value - [IN] the value to remove                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除队列中指定的值。该函数接收一个指向队列的指针和一个需要删除的值作为参数。函数首先判断队列是否为空，若为空则直接返回。接着判断队尾指针是否小于队头指针，若小于则说明队列是空的，直接返回。然后从队尾开始遍历队列，查找需要删除的值。找到需要删除的值后，将队头指针向前移动一位，继续查找。队头指针小于等于队尾指针时，说明队列为空，直接返回。最后从队尾位置开始遍历队列，查找需要删除的值。找到需要删除的值后，将队尾指针向前移动一位，队尾指针达到分配空间末尾时，重新指向为0。若队尾指针大于队头指针，则说明队列为空，直接返回。
 ******************************************************************************/
void zbx_queue_ptr_remove_value(zbx_queue_ptr_t *queue, const void *value)
{
	// 定义变量，用于循环计数和起始位置
	int i, start_pos;

	// 判断队列为空，若为空则直接返回
	if (queue->tail_pos == queue->head_pos)
		return;

	// 判断队尾指针是否小于队头指针，若小于则说明队列是空的，直接返回
	if (queue->tail_pos < queue->head_pos)
		start_pos = queue->tail_pos;
	else
		start_pos = 0;

	// 从起始位置开始遍历队列，查找需要删除的值
	for (i = start_pos; i < queue->head_pos; i++)
	{
		// 找到需要删除的值，将队头指针向前移动一位，继续查找
		if (queue->values[i] == value)
		{
			for (; i < queue->head_pos - 1; i++)
				queue->values[i] = queue->values[i + 1];

			// 删除找到的值，队头指针向前移动一位
			queue->head_pos--;
			return;
		}
	}

	// 队尾指针小于等于队头指针，说明队列已遍历完毕，未找到需要删除的值，直接返回
	if (queue->tail_pos <= queue->head_pos)
		return;

	// 从队尾位置开始遍历队列，查找需要删除的值
	for (i = queue->alloc_num - 1; i >= queue->tail_pos; i--)
	{
		// 找到需要删除的值，将队尾指针向前移动一位，继续查找
		if (queue->values[i] == value)
		{
			for (; i > queue->tail_pos; i--)
				queue->values[i] = queue->values[i - 1];

			// 删除找到的值，队尾指针向前移动一位，若队尾指针达到分配空间末尾，则重新指向为0
			if (++queue->tail_pos == queue->alloc_num)
				queue->tail_pos = 0;

			return;
		}
	}
}

