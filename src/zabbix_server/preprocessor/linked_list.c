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
#include "linked_list.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_list_create                                                  *
 *                                                                            *
 * Purpose: create singly linked list                                         *
 *                                                                            *
 * Parameters: list - [IN] the list                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：创建一个空的zbx链表结构体，将链表的头部和尾部的指针初始化为NULL。
 ******************************************************************************/
// 定义一个函数，用于创建一个空的zbx链表结构体
void zbx_list_create(zbx_list_t *queue)
{
	memset(queue, 0, sizeof(*queue));
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_destroy                                                 *
 *                                                                            *
 * Purpose: destroy list                                                      *
 *                                                                            *
 * Parameters: list - [IN] the list                                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_list_destroy(zbx_list_t *list)
{
	while (FAIL != zbx_list_pop(list, NULL))
		;
}

/******************************************************************************
 *                                                                            *
 * Function: list_create_item                                                 *
 *                                                                            *
 * Purpose: allocate memory and initialize a new list item                    *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             value    - [IN] the data to be stored                          *
 *             created  - [OUT] pointer to the created list item              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是创建一个zbx列表项（zbx_list_item_t结构体），并将该列表项的指针传递给调用者。具体来说，这个函数接收三个参数：一个zbx_list_t类型的指针（代表列表），一个void类型的指针（代表要插入的值），以及一个zbx_list_item_t类型的指针（用于存储创建的列表项）。在函数内部，首先分配内存空间创建一个新的zbx_list_item_t结构体，然后初始化其结构，最后将创建的列表项指针传递给调用者。
 ******************************************************************************/
// 定义一个静态函数，用于创建列表项
static void list_create_item(zbx_list_t *list, void *value, zbx_list_item_t **created)
{
	// 定义一个指向列表项的指针变量 item
	zbx_list_item_t *item;

	// 忽略列表指针 list
	ZBX_UNUSED(list);

	// 为新列表项分配内存空间
	item = (zbx_list_item_t *)zbx_malloc(NULL, sizeof(zbx_list_item_t));
	
	// 初始化列表项的结构
	item->next = NULL;
	item->data = value;

	*created = item;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_list_insert_after                                            *
 *                                                                            *
 * Purpose: insert value after specified position in the list                 *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             after    - [IN] specified position (can be NULL to insert at   *
 *                             the end of the list)                           *
 *             value    - [IN] the value to be inserted                       *
 *             inserted - [OUT] pointer to the inserted list item             *
 *                                                                            *
 ******************************************************************************/
// 定义一个函数，用于在给定的列表节点后插入一个新的节点
// 参数：list 是列表的头指针，after 是要插入节点的参考节点，value 是要插入的值，inserted 是一个指针，用于返回新插入的节点
// 返回值：无
void zbx_list_insert_after(zbx_list_t *list, zbx_list_item_t *after, void *value, zbx_list_item_t **inserted)
{
	// 创建一个新的节点 item
	zbx_list_item_t *item;

	list_create_item(list, value, &item);

	if (NULL == after)
		after = list->tail;

	if (NULL != after)
	{
		item->next = after->next;
		after->next = item;
	}
	else
		list->head = item;

	if (after == list->tail)
		list->tail = item;

	if (NULL != inserted)
		*inserted = item;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_list_append                                                  *
 *                                                                            *
 * Purpose: append value to the end of the list                               *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             value    - [IN] the value to append                            *
 *             inserted - [OUT] pointer to the inserted list item             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个名为zbx_list_append的函数，用于在zbx_list类型的列表中添加一个新元素。该函数接收三个参数：指向zbx_list结构的指针list，要添加的值（void类型），以及一个指向zbx_list_item结构的指针指针inserted。在函数内部，调用zbx_list_insert_after函数将在列表的末尾插入一个新的元素，并将插入位置的指针返回给inserted。
 *
 *
 ******************************************************************************/
// 定义一个函数：zbx_list_append，用于在列表末尾添加一个元素
void zbx_list_append(zbx_list_t *list, void *value, zbx_list_item_t **inserted)
{
    // 使用zbx_list_insert_after函数，将在list的末尾插入一个新的元素
    zbx_list_insert_after(list, NULL, value, inserted);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_prepend                                                 *
 *                                                                            *
 * Purpose: prepend value to the beginning of the list                        *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             value    - [IN] the value to prepend                           *
 *             inserted - [OUT] pointer to the inserted list item             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是在zbx_list链表的头部插入一个新元素，并返回插入位置的下一个节点。
 ******************************************************************************/
// 这是一个C语言函数，名为zbx_list_prepend，它属于zbx_list头文件。
// 该函数的作用是在zbx_list链表的头部插入一个新元素。
// 函数的参数如下：
// list：指向zbx_list结构体的指针，用于操作链表。
// value：将要插入链表的 void 类型的值。
// inserted：指向zbx_list_item_t类型的指针，用于返回插入位置的下一个节点。

void	zbx_list_prepend(zbx_list_t *list, void *value, zbx_list_item_t **inserted)
{
	// 定义一个zbx_list_item_t类型的指针item，用于保存新创建的节点。
	zbx_list_item_t *item;

	// 使用list_create_item函数创建一个新的节点，并将value传入，保存到item中。
	list_create_item(list, value, &item);
	item->next = list->head;
	list->head = item;

	if (NULL == list->tail)
		list->tail = item;

	if (NULL != inserted)
		*inserted = item;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_list_pop                                                     *
 *                                                                            *
 * Purpose: removes a value from the beginning of the list                    *
 *                                                                            *
 * Parameters: list  - [IN]  the list                                         *
 *             value - [OUT] the value                                        *
 *                                                                            *
 * Return value: SUCCEED is returned if list is not empty, otherwise, FAIL is *
 *               returned.                                                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_list_pop(zbx_list_t *list, void **value)
{
	zbx_list_item_t	*head;

	if (NULL == list->head)
		return FAIL;

	head = list->head;

	if (NULL != value)
		*value = head->data;

	list->head = list->head->next;
	zbx_free(head);

	if (NULL == list->head)
		list->tail = NULL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_list_peek                                                    *
 *                                                                            *
 * Purpose: get value from the queue without dequeuing                        *
 *                                                                            *
 * Parameters: list  - [IN]  the list                                         *
 *             value - [OUT] the value                                        *
 *                                                                            *
 * Return value: SUCCEED is returned if list is not empty, otherwise, FAIL is *
 *               returned.                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：查询zbx_list_t类型链表的头节点数据，并将查询结果存储到void类型的指针value所指向的内存空间。如果链表为空，返回失败标志FAIL。
 ******************************************************************************/
// 定义一个C语言函数zbx_list_peek，接收两个参数：
// 参数1：指向zbx_list_t结构体的指针list，表示链表的头指针；
// 参数2：void类型的指针value，用于存储链表头的数据。
int zbx_list_peek(const zbx_list_t *list, void **value)
{
	// 判断list->head是否为空，如果不为空，说明链表有数据节点；
	if (NULL != list->head)
	{
		// 将链表头的数据存储到void类型的指针value所指向的内存空间；
		*value = list->head->data;
		// 返回成功标志SUCCEED，表示操作成功；
		return SUCCEED;
	}

	// 如果链表为空，返回失败标志FAIL，表示没有数据可供查询。
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_iterator_init                                           *
 *                                                                            *
 * Purpose: initialize list iterator                                          *
 *                                                                            *
 * Parameters: list     - [IN]  the list                                      *
 *             iterator - [OUT] the iterator to be initialized                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是初始化一个zbx链表的迭代器。函数接收两个参数，一个是zbx链表的结构体指针，另一个是zbx链表迭代器的结构体指针。在函数内部，首先将迭代器的list指针指向传入的链表结构体，然后将迭代器的next指针指向链表的head节点，最后将迭代器的current指针设置为NULL，表示当前迭代器没有指向任何节点。这样就完成了迭代器的初始化工作。
 ******************************************************************************/
// 定义一个函数，用于初始化zbx链表的迭代器
void zbx_list_iterator_init(zbx_list_t *list, zbx_list_iterator_t *iterator)
{
    // 将迭代器的list指针指向传入的list结构体
    iterator->list = list;
    // 将迭代器的next指针指向list的head节点
    iterator->next = list->head;
    // 将迭代器的current指针设置为NULL，表示当前迭代器没有指向任何节点
    iterator->current = NULL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_iterator_next                                           *
 *                                                                            *
 * Purpose: advance list iterator                                             *
 *                                                                            *
 * Parameters: iterator - [IN] the iterator to be advanced                    *
 *                                                                            *
 * Return value: SUCCEED is returned if next list item exists, otherwise,     *
 *               FAIL is returned.                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个迭代器函数zbx_list_iterator_next，用于遍历链表。该函数接收一个zbx_list_iterator_t类型的指针作为参数，判断迭代器是否有下一个节点可供访问。如果有，则更新迭代器的当前节点和下一个节点，并返回成功；如果没有，则返回失败。
 ******************************************************************************/
// 定义一个函数zbx_list_iterator_next，接收一个zbx_list_iterator_t类型的指针作为参数
int zbx_list_iterator_next(zbx_list_iterator_t *iterator)
{
	// 检查迭代器的下一个节点是否为空，如果不为空，说明有下一个节点可以访问
	if (NULL != iterator->next)
	{
		// 更新迭代器的当前节点为下一个节点
		iterator->current = iterator->next;
		// 更新迭代器的下一个节点为当前节点的下一个节点
		iterator->next = iterator->next->next;

		// 返回成功，表示迭代器向前移动成功
		return SUCCEED;
	}

	// 如果迭代器的下一个节点为空，说明已经到达列表的末尾，返回失败
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_iterator_peek                                           *
 *                                                                            *
 * Purpose: get value without removing it from list                           *
 *                                                                            *
 * Parameters: iterator - [IN]  initialized list iterator                     *
 *             value    - [OUT] the value                                     *
 *                                                                            *
 * Return value: SUCCEED is returned if item exists, otherwise, FAIL is       *
 *               returned.                                                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_list_iterator_peek(const zbx_list_iterator_t *iterator, void **value)
{
	if (NULL != iterator->current)
	{
		*value = iterator->current->data;
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_list_iterator_clear                                          *
 *                                                                            *
 * Purpose: clears iterator leaving it in uninitialized state                 *
 *                                                                            *
 * Parameters: iterator - [IN]  list iterator                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：清空一个 zbx_list_iterator_t 类型的指针 iterator 所指向的结构体中的所有成员变量，使其各个成员变量都为 0。这个过程通过使用 memset 函数来实现。
 ******************************************************************************/
// 定义一个函数，名为 zbx_list_iterator_clear，参数是一个指向 zbx_list_iterator_t 类型的指针 iterator。
void zbx_list_iterator_clear(zbx_list_iterator_t *iterator)
{
	// 使用 memset 函数，将 iterator 指向的内存区域清空，清空的内容为 0。
	// 这里清空的目的就是为了将 iterator 指向的 zbx_list_iterator_t 结构体的各个成员变量都设置为 0，从而实现清空迭代器的功能。
	memset(iterator, 0, sizeof(zbx_list_iterator_t));
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_iterator_equal                                          *
 *                                                                            *
 * Purpose: tests if two iterators points at the same list item               *
 *                                                                            *
 * Parameters: iterator1 - [IN] first list iterator                           *
 *             iterator2 - [IN] second list iterator                          *
 *                                                                            *
 * Return value: SUCCEED is returned if both iterator point at the same item, *
 *               FAIL otherwise.                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断两个迭代器是否相等。迭代器相等的情况下，返回SUCCEED；迭代器不等时，返回FAIL。其中，判断迭代器相等的关键是比较两个迭代器的list和current指针。
 ******************************************************************************/
// 定义一个函数zbx_list_iterator_equal，接收两个zbx_list_iterator_t类型的指针作为参数
int zbx_list_iterator_equal(const zbx_list_iterator_t *iterator1, const zbx_list_iterator_t *iterator2)
{
	// 判断两个迭代器的list指针是否相同，如果相同，则继续判断current指针是否相同
	if (iterator1->list == iterator2->list && iterator1->current == iterator2->current)
		// 如果两个迭代器的list和current指针都相同，返回SUCCEED，表示迭代器相同
		return SUCCEED;

	// 如果两个迭代器的list或current指针不同，返回FAIL，表示迭代器不同
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_iterator_isset                                          *
 *                                                                            *
 * Purpose: checks if the iterator points at some list item                   *
 *                                                                            *
 * Parameters: iterator - [IN] list iterator                                  *
 *                                                                            *
 * Return value: SUCCEED is returned if iterator is set, FAIL otherwise.      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断一个zbx_list_iterator_t类型的指针所指向的列表是否为空。如果列表为空，则返回FAIL，否则返回SUCCEED。这个函数用于检查迭代器是否有效，以便在处理列表时避免出现问题。
 ******************************************************************************/
// 定义一个函数zbx_list_iterator_isset，接收一个zbx_list_iterator_t类型的指针作为参数
int zbx_list_iterator_isset(const zbx_list_iterator_t *iterator)
{
    // 判断iterator->list是否为空，如果为空，则返回FAIL，否则返回SUCCEED
    return (NULL == iterator->list ? FAIL : SUCCEED);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_list_iterator_update                                         *
 *                                                                            *
 * Purpose: updates iterator                                                  *
 *                                                                            *
 * Parameters: iterator - [IN] list iterator                                  *
 *                                                                            *
 * Comments: This function must be used after an item has been inserted in    *
 *           list during iteration process.                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是更新一个链表迭代器的下一个节点指针。函数接收一个zbx_list_iterator_t类型的指针作为参数，该类型可能是一个链表迭代器结构体。在函数内部，首先判断传入的迭代器是否有当前节点（current），如果有的话，就更新迭代器的下一个节点（next）为当前节点的下一个节点。这样就可以实现迭代器向前移动一位，准备访问下一个节点。整个函数的实现比较简单，主要是判断和赋值操作。
 ******************************************************************************/
// 定义一个函数，名为 zbx_list_iterator_update，参数是一个 zbx_list_iterator_t 类型的指针 iterator。
void zbx_list_iterator_update(zbx_list_iterator_t *iterator)
{
	// 判断 iterator 指向的当前节点（current）是否不为空
	if (NULL != iterator->current)
		// 更新 iterator 指向的下一个节点，将其设置为当前节点的下一个节点
		iterator->next = iterator->current->next;
}


