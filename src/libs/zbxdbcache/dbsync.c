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
#include "dbcache.h"
#include "zbxserver.h"
#include "mutexs.h"

#define ZBX_DBCONFIG_IMPL
#include "dbconfig.h"
#include "dbsync.h"

typedef struct
{
	zbx_hashset_t	strpool;
	ZBX_DC_CONFIG	*cache;
}
zbx_dbsync_env_t;

static zbx_dbsync_env_t	dbsync_env;

/* string pool support */

#define REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

/******************************************************************************
 * c
 */*
 * * 定义一个名为 dbsync_strpool_hash_func 的静态函数，用于计算字符串的哈希值。
 * * 传入的参数为一个指向 void 类型的指针 data，通过转换指针类型为 char* 类型，
 * * 并加上 REFCOUNT_FIELD_SIZE，用于处理字符串哈希计算。
 * * 最后，调用 ZBX_DEFAULT_STRING_HASH_FUNC 函数计算字符串哈希值并返回。
 * */
 *static zbx_hash_t\tdbsync_strpool_hash_func(const void *data)
 *{
 *    // 返回 ZBX_DEFAULT_STRING_HASH_FUNC 函数的调用结果，传入的参数为（char *）data + REFCOUNT_FIELD_SIZE
 *    return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 dbsync_strpool_hash_func 的静态函数，参数为一个指向 void 类型的指针 data
static zbx_hash_t	dbsync_strpool_hash_func(const void *data)
{
    // 返回 ZBX_DEFAULT_STRING_HASH_FUNC 函数的调用结果，传入的参数为（char *）data + REFCOUNT_FIELD_SIZE
    return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

代码块主要目的是：定义一个名为 dbsync_strpool_hash_func 的静态函数，用于计算字符串的哈希值。传入的参数为一个指向 void 类型的指针 data，通过转换指针类型为 char* 类型，并加上 REFCOUNT_FIELD_SIZE，用于处理字符串哈希计算。最后，调用 ZBX_DEFAULT_STRING_HASH_FUNC 函数计算字符串哈希值并返回。

整个注释好的代码块如下：



/******************************************************************************
 * *
 *这块代码的主要目的是定义一个静态函数`dbsync_strpool_compare_func`，用于比较两个字符串对象的大小。函数接收两个参数，分别是两个字符串对象的指针。通过将指针转换为字符指针并跳过REF_COUNT_FIELD_SIZE字节，然后使用`strcmp`函数比较两个字符串的内容是否相同。
 *
 *输出：
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串复制函数dbsync_strdup，该函数接受一个const char *类型的参数str，返回一个char *类型的指针，表示复制的字符串。该函数通过内存池管理来实现字符串的复制，如果字符串已经在内存池中，则直接返回内存池中的指针；否则，将字符串插入到内存池中，并返回内存池中的指针。同时，对字符串进行参考计数，以确保内存池中的字符串不会被重复释放。
 ******************************************************************************/
// 定义一个函数dbsync_strdup，用于实现字符串的复制和内存池管理
static char *dbsync_strdup(const char *str)
{
	// 定义一个指针ptr，用于在内存池中查找或插入字符串
	void *ptr;

	// 在内存池中查找字符串，键为str，偏移量为str - REFCOUNT_FIELD_SIZE
	ptr = zbx_hashset_search(&dbsync_env.strpool, str - REFCOUNT_FIELD_SIZE);

	// 如果查找失败，即ptr为空，说明字符串不在内存池中，需要进行插入操作
	if (NULL == ptr)
	{
		// 在内存池中插入字符串，键为str，偏移量为str - REFCOUNT_FIELD_SIZE，
		// 字符串长度为strlen(str)，额外分配1个字节的内存用于参考计数
		ptr = zbx_hashset_insert_ext(&dbsync_env.strpool, str - REFCOUNT_FIELD_SIZE,
				REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);

		// 初始化参考计数为0
		*(zbx_uint32_t *)ptr = 0;
	}

	// 增加字符串的参考计数
	(*(zbx_uint32_t *)ptr)++;

	// 返回字符串指针，偏移量为REFCOUNT_FIELD_SIZE
	return (char *)ptr + REFCOUNT_FIELD_SIZE;
}

{
	void	*ptr;

	ptr = zbx_hashset_search(&dbsync_env.strpool, str - REFCOUNT_FIELD_SIZE);
/******************************************************************************
 * *
 *整个代码块的主要目的是比较一个字符串表示的uint64值与一个zbx_uint64_t类型的值是否相等。具体实现过程如下：
 *
 *1. 定义一个zbx_uint64_t类型的变量value_ui64，用于存储从value_raw字符串转换而来的uint64数值。
 *2. 使用ZBX_DBROW2UINT64函数将value_raw字符串转换为uint64类型的value_ui64。
 *3. 判断value_ui64与value是否相等，如果相等则返回SUCCEED，否则返回FAIL。
 *
 *输出结果：
 *
 *```
 *int main()
 *{
 *    const char *value_raw = \"123456789\";
 *    zbx_uint64_t value = 123456789;
 *
 *    int result = dbsync_compare_uint64(value_raw, value);
 *
 *    if (result == SUCCEED)
 *    {
 *        printf(\"The value is equal.\
 *\");
 *    }
 *    else
 *    {
 *        printf(\"The value is not equal.\
 *\");
 *    }
 *
 *    return 0;
 *}
 *```
 *
 *在这个例子中，我们将字符串\"123456789\"转换为uint64数值，并与zbx_uint64_t类型的值进行比较。如果两者相等，输出\"The value is equal.\"，否则输出\"The value is not equal.\"。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_uint64，接收两个参数，一个是指向字符串的指针value_raw，另一个是zbx_uint64_t类型的值value。
static int	dbsync_compare_uint64(const char *value_raw, zbx_uint64_t value)
{
	// 定义一个zbx_uint64_t类型的变量value_ui64，用于存储从value_raw字符串转换而来的uint64数值
	zbx_uint64_t	value_ui64;

	// 使用ZBX_DBROW2UINT64函数将value_raw字符串转换为uint64类型的value_ui64
	ZBX_DBROW2UINT64(value_ui64, value_raw);

	// 判断value_ui64与value是否相等，如果相等则返回SUCCEED，否则返回FAIL
	return (value_ui64 == value ? SUCCEED : FAIL);
}


	(*(zbx_uint32_t *)ptr)++;

	return (char *)ptr + REFCOUNT_FIELD_SIZE;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：释放一个已分配内存的字符串，并在字符串池中删除该字符串。当传入的字符串指针不为空时，首先计算原始内存地址，然后判断该字符串的引用计数。如果引用计数为0，说明该字符串已经被释放，可以直接从字符串池中删除。
 ******************************************************************************/
// 定义一个静态函数dbsync_strfree，用于释放字符串内存
static void dbsync_strfree(char *str)
{
    // 判断传入的字符串指针str是否为空，如果不为空，则继续执行后续代码
    if (NULL != str)
    {
        // 计算字符串指针str减去引用计数字段的长度，得到原始内存地址
        void *ptr = str - REFCOUNT_FIELD_SIZE;

        // 判断引用计数是否为0，如果为0，说明该字符串已经被释放，可以直接删除
        if (0 == --(*(zbx_uint32_t *)ptr))
        {
            // 删除字符串池中的字符串，其中ptr作为键
            zbx_hashset_remove_direct(&dbsync_env.strpool, ptr);
        }
    }
}


/* macro valie validators */

/******************************************************************************
 *                                                                            *
 * Function: dbsync_numeric_validator                                         *
 *                                                                            *
 * Purpose: validate numeric value                                            *
 *                                                                            *
 * Parameters: value   - [IN] the value to validate                           *
 *                                                                            *
 * Return value: SUCCEED - the value contains valid numeric value             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查输入的字符串值`value`是否包含小数后缀，如果包含，则返回成功（SUCCEED），否则返回失败（FAIL）。函数内部使用了`is_double_suffix`函数来判断字符串是否包含小数后缀。
 ******************************************************************************/
// 定义一个静态函数dbsync_numeric_validator，参数为一个字符串指针value
static int dbsync_numeric_validator(const char *value)
{
    // 判断value字符串是否包含小数后缀
    if (SUCCEED == is_double_suffix(value, ZBX_FLAG_DOUBLE_SUFFIX))
    {
        // 如果包含小数后缀，返回成功
        return SUCCEED;
    }
    // 如果不包含小数后缀，返回失败
    return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_uint64                                            *
 *                                                                            *
 * Purpose: compares 64 bit unsigned integer with a raw database value        *
 *                                                                            *
 ******************************************************************************/
static int	dbsync_compare_uint64(const char *value_raw, zbx_uint64_t value)
{
	zbx_uint64_t	value_ui64;

	ZBX_DBROW2UINT64(value_ui64, value_raw);

	return (value_ui64 == value ? SUCCEED : FAIL);
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_int                                               *
 *                                                                            *
 * Purpose: compares 32 bit signed integer with a raw database value          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个值，一个是字符串类型的value_raw，另一个是整数类型的value。通过将字符串value_raw转换为整数，然后判断转换后的整数与value是否相等。如果相等，则返回SUCCEED，表示比较成功；否则，返回FAIL，表示比较失败。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_int，接收两个参数：一个const char类型的指针value_raw，一个int类型的值value。
static int dbsync_compare_int(const char *value_raw, int value)
{
    // 将字符串value_raw转换为整数，使用atoi函数
    int num = atoi(value_raw);

    // 判断转换后的整数num与传入的值value是否相等
    if (num == value)
    {
        // 如果相等，返回SUCCEED，表示成功
        return SUCCEED;
    }
    else
    {
        // 如果不相等，返回FAIL，表示失败
        return FAIL;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_uchar                                             *
 *                                                                            *
 * Purpose: compares unsigned character with a raw database value             *
 *                                                                            *
 ******************************************************************************/

/******************************************************************************
 * *
 *这块代码的主要目的是比较两个字符串对应的 unsigned char 类型的值是否相等。其中，`value_raw` 是一个指向字符串的指针，`value` 是一个 unsigned char 类型的值。函数通过 `ZBX_STR2UCHAR` 函数将 `value_raw` 转换为 unsigned char 类型，然后判断转换后的值是否与 `value` 相等，返回相应的结果。如果相等，则返回 SUCCEED，否则返回 FAIL。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_uchar，用于比较两个字符串对应的 unsigned char 类型的值
static int	dbsync_compare_uchar(const char *value_raw, unsigned char value)
{
	// 定义一个 unsigned char 类型的变量 value_uchar，用于存储 value_raw 转换后的 unsigned char 值
	unsigned char	value_uchar;

	// 使用 ZBX_STR2UCHAR 函数将 value_raw 转换为 unsigned char 类型的值，存储在 value_uchar 中
	ZBX_STR2UCHAR(value_uchar, value_raw);

	// 判断 value_uchar 和 value 是否相等，如果相等则返回 SUCCEED，否则返回 FAIL
	return (value_uchar == value ? SUCCEED : FAIL);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数 `dbsync_add_row`，用于向数据同步结构体中的数据表添加一行数据。在函数中，首先创建一个 `zbx_dbsync_row_t` 类型的实例，并设置其行ID、行标签和行数据。然后，根据行标签的不同，更新同步状态计数器。最后，将新创建的行添加到同步结构体的 `rows` 向量中。
 ******************************************************************************/
// 定义一个静态函数，用于向数据同步结构体中的数据表添加一行数据
static void dbsync_add_row(zbx_dbsync_t *sync, zbx_uint64_t rowid, unsigned char tag, const DB_ROW dbrow)
{
	// 定义一个整型变量 i，用于循环操作
	int i;
	// 定义一个指向 zbx_dbsync_row_t 类型的指针，用于存储行的信息
	zbx_dbsync_row_t *row;

	// 分配内存，创建一个新的 zbx_dbsync_row_t 结构体实例
	row = (zbx_dbsync_row_t *)zbx_malloc(NULL, sizeof(zbx_dbsync_row_t));
	// 设置行ID
	row->rowid = rowid;
	// 设置行标签
	row->tag = tag;

	// 如果 dbrow 参数不为空，则分配内存存储行数据
	if (NULL != dbrow)
	{
		// 分配内存，存储行数据
		row->row = (char **)zbx_malloc(NULL, sizeof(char *) * sync->columns_num);

		// 遍历 dbrow 数组，将每个元素转换为字符串，并存储在 row->row 数组中
		for (i = 0; i < sync->columns_num; i++)
		{
			row->row[i] = (NULL == dbrow[i] ? NULL : dbsync_strdup(dbrow[i]));
		}
	}
	// 如果 dbrow 为空，则设置 row->row 为 NULL
	else
	{
		row->row = NULL;
	}

	// 将新创建的行添加到 sync->rows 向量中
	zbx_vector_ptr_append(&sync->rows, row);

	// 根据行标签的不同，更新同步状态计数器
	switch (tag)
	{
		case ZBX_DBSYNC_ROW_ADD:
			sync->add_num++;
			break;
		case ZBX_DBSYNC_ROW_UPDATE:
			sync->update_num++;
			break;
		case ZBX_DBSYNC_ROW_REMOVE:
			sync->remove_num++;
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_add_row                                                   *
 *                                                                            *
 * Purpose: adds a new row to the changeset                                   *
 *                                                                            *
 * Parameter: sync  - [IN] the changeset                                      *
 *            rowid - [IN] the row identifier                                 *
 *            tag   - [IN] the row tag (see ZBX_DBSYNC_ROW_ defines)          *
 *            row   - [IN] the row contents (depending on configuration cache *
 *                         removal logic for the specific object it can be    *
 *                         NULL when used with ZBX_DBSYNC_ROW_REMOVE tag)     *
 *                                                                            *
 ******************************************************************************/
static void	dbsync_add_row(zbx_dbsync_t *sync, zbx_uint64_t rowid, unsigned char tag, const DB_ROW dbrow)
{
	int			i;
	zbx_dbsync_row_t	*row;

	row = (zbx_dbsync_row_t *)zbx_malloc(NULL, sizeof(zbx_dbsync_row_t));
	row->rowid = rowid;
	row->tag = tag;

	if (NULL != dbrow)
	{
		row->row = (char **)zbx_malloc(NULL, sizeof(char *) * sync->columns_num);

		for (i = 0; i < sync->columns_num; i++)
			row->row[i] = (NULL == dbrow[i] ? NULL : dbsync_strdup(dbrow[i]));
	}
	else
		row->row = NULL;

	zbx_vector_ptr_append(&sync->rows, row);

	switch (tag)
	{
		case ZBX_DBSYNC_ROW_ADD:
			sync->add_num++;
			break;
		case ZBX_DBSYNC_ROW_UPDATE:
			sync->update_num++;
			break;
		case ZBX_DBSYNC_ROW_REMOVE:
			sync->remove_num++;
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_prepare                                                   *
 *                                                                            *
 * Purpose: prepares changeset                                                *
 *                                                                            *
 * Parameter: sync             - [IN] the changeset                           *
 *            columns_num      - [IN] the number of columns in the changeset  *
 *            get_hostids_func - [IN] the callback function used to retrieve  *
 *                                    associated hostids (can be NULL if      *
 *                                    user macros are not resolved during     *
 *                                    synchronization process)                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：准备数据库同步操作所需的参数，包括列的数量、处理每一行的函数以及存储每一行数据的内存空间。在这个过程中，使用了动态内存分配函数zbx_malloc()来分配内存。
 ******************************************************************************/
/* 定义一个静态函数，用于准备数据库同步操作 */
static void dbsync_prepare(zbx_dbsync_t *sync, int columns_num, zbx_dbsync_preproc_row_func_t preproc_row_func)
{
	/* 保存传入的参数，columns_num表示列的数量，preproc_row_func是用于处理每一行的函数 */
	sync->columns_num = columns_num;
	sync->preproc_row_func = preproc_row_func;

	/* 为sync结构体分配一块内存，用于存储每一行的数据，columns_num表示行数 */
	sync->row = (char **)zbx_malloc(NULL, sizeof(char *) * columns_num);
	/* 初始化sync->row数组，将每个元素都设置为NULL */
	memset(sync->row, 0, sizeof(char *) * columns_num);
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_check_row_macros                                          *
 *                                                                            *
 * Purpose: checks if the specified column in the row contains user macros    *
 *                                                                            *
 * Parameter: row    - [IN] the row to check                                  *
 *            column - [IN] the column index                                  *
 *                                                                            *
 * Comments: While not definite, this check is used to filter out rows before *
 *           doing more precise (and resource intense) checks.                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查一行字符串中是否包含 \"$\" 符号。如果找到 \"$\" 符号，则返回SUCCEED（成功），否则返回FAIL（失败）。
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为dbsync_check_row_macros，静态类型，用于检查一行字符串中是否包含 "$" 符号。
 * 函数接收两个参数：一个字符指针数组（row）和整数 column。
 * 函数返回两个枚举类型值之一：SUCCEED（成功）或FAIL（失败）。
 */
static int	dbsync_check_row_macros(char **row, int column)
{
	/* 如果传入的行数组不为空，且column索引处的元素包含"$"符号 */
	if (NULL != strstr(row[column], "{$"))
		/* 如果有 "$" 符号，返回SUCCEED（成功） */
		return SUCCEED;

	/* 如果没有找到 "$" 符号，返回FAIL（失败） */
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_preproc_row                                               *
 *                                                                            *
 * Purpose: applies necessary pre-processing before row is compared/used      *
 *                                                                            *
 * Parameter: sync - [IN] the changeset                                       *
 *            row  - [IN/OUT] the data row                                    *
 *                                                                            *
 * Return value: the resulting row                                            *
 *                                                                            *
 ******************************************************************************/
static char	**dbsync_preproc_row(zbx_dbsync_t *sync, char **row)
{
	int	i;
/******************************************************************************
 * *
 *整个代码块的主要目的是对原始行数据进行预处理，并将处理后的数据存储在 `sync->row` 中。同时，将处理后的数据与原始数据不同的部分添加到列数组 `sync->columns` 中。最后返回处理后的行数据。
 ******************************************************************************/
// 判断预处理函数指针是否为空，若为空则直接返回原始行数据
if (NULL == sync->preproc_row_func)
{
    return row; // 如果预处理函数为空，直接返回原始行数据
}

/* 释放上一次预处理分配的资源 */
zbx_vector_ptr_clear_ext(&sync->columns, zbx_ptr_free);

/* 复制原始数据 */
memcpy(sync->row, row, sizeof(char *) * sync->columns_num);

/* 调用预处理函数，并将结果存储在 sync->row 中 */
sync->row = sync->preproc_row_func(sync->row);

/* 遍历列数组，比较原始行数据与预处理后的行数据 */
for (i = 0; i < sync->columns_num; i++)
{
    if (sync->row[i] != row[i]) // 如果预处理后的数据与原始数据不同，则将差异部分添加到列数组中
    {
        zbx_vector_ptr_append(&sync->columns, sync->row[i]);
    }
}

/* 返回预处理后的行数据 */
return sync->row;


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_init_env                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是初始化数据同步环境，包括将传入的配置信息赋值给对应的结构体变量，以及创建一个字符串池用于存储和管理字符串资源。其中，`zbx_dbsync_init_env`函数接收一个`ZBX_DC_CONFIG`类型的指针作为参数，这个指针指向一个配置结构体，里面包含了数据同步所需的各种配置信息。在函数内部，首先将传入的配置信息赋值给`dbsync_env`结构体的`cache`字段，然后创建一个哈希表用于存储字符串池，哈希表的大小为100。`dbsync_strpool_hash_func`和`dbsync_strpool_compare_func`分别是哈希表的哈希函数和比较函数，用于管理字符串池中的字符串资源。
 ******************************************************************************/
// 定义一个函数，用于初始化数据同步环境
void zbx_dbsync_init_env(ZBX_DC_CONFIG *cache)
{
    // 将传入的cache指针赋值给dbsync_env结构的cache字段
    dbsync_env.cache = cache;
    
    // 创建一个哈希表，用于存储字符串池（字符串缓存）
/******************************************************************************
 * *
 *这块代码的主要目的是：销毁一个 C 语言结构体 dbsync_env 中的字符串池（strpool）。
 *
 *函数 zbx_dbsync_free_env 的作用是在程序运行结束时，释放字符串池中所占用的内存资源。这里使用了 zbx_hashset_destroy 函数来完成字符串池的销毁操作。在调用此函数时，将字符串池的指针传递给它，从而确保正确地销毁指定的字符串池。
 ******************************************************************************/
// 定义一个函数，名为 zbx_dbsync_free_env，函数类型为 void
void zbx_dbsync_free_env(void)
{
    // 定义一个指向 dbsync_env.strpool 的指针，用于操作字符串池
    zbx_hashset_t *strpool = &dbsync_env.strpool;

    // 调用 zbx_hashset_destroy 函数，传入字符串池指针，销毁字符串池
    zbx_hashset_destroy(strpool);
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_env_release                                               *
 *                                                                            *
 ******************************************************************************/
void	zbx_dbsync_free_env(void)
{
	zbx_hashset_destroy(&dbsync_env.strpool);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_init                                                  *
 *                                                                            *
 * Purpose: initializes changeset                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是初始化一个zbx_dbsync结构体，根据传入的同步模式（ZBX_DBSYNC_UPDATE或其他）设置相应的状态和数据结构。在同步模式为ZBX_DBSYNC_UPDATE时，还会创建一个用于存储行数据的zbx_vector_ptr对象，并初始化行索引。
 ******************************************************************************/
// 定义一个函数，用于初始化zbx_dbsync结构体
void zbx_dbsync_init(zbx_dbsync_t *sync, unsigned char mode)
{
	// 初始化同步状态的结构体指针
	sync->columns_num = 0;
	// 设置同步模式
	sync->mode = mode;

	// 初始化同步操作的数量
	sync->add_num = 0;
	sync->update_num = 0;
	sync->remove_num = 0;

	// 初始化行数据指针为空
	sync->row = NULL;
	// 初始化预处理行函数为空
	sync->preproc_row_func = NULL;
	// 创建一个用于存储列信息的zbx_vector_ptr对象
	zbx_vector_ptr_create(&sync->columns);

	// 根据同步模式进行不同操作
	if (ZBX_DBSYNC_UPDATE == sync->mode)
	{
		// 创建一个用于存储行数据的zbx_vector_ptr对象
		zbx_vector_ptr_create(&sync->rows);
		// 初始化行索引
		sync->row_index = 0;
	}
	else
	{
		// 初始化数据库操作结果为空
		sync->dbresult = NULL;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_clear                                                 *
 *                                                                            *
 * Purpose: frees resources allocated by changeset                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清除zbx_dbsync_t结构体分配的内存资源。具体来说，它执行以下操作：
 *
 *1. 释放由行预处理分配的列向量资源。
 *2. 释放行预处理分配的内存。
 *3. 如果同步模式为ZBX_DBSYNC_UPDATE，则遍历所有行，释放每行中的字段内存，并释放行结构体内存。
 *4. 释放列向量资源。
 *5. 如果同步模式不是ZBX_DBSYNC_UPDATE，则释放数据库结果集。
 *
 *注意：这段代码仅适用于zbx_dbsync_t结构体的清理操作。
 ******************************************************************************/
void	zbx_dbsync_clear(zbx_dbsync_t *sync)
{
	/* 释放行预处理分配的资源 */
	zbx_vector_ptr_clear_ext(&sync->columns, zbx_ptr_free);
	zbx_vector_ptr_destroy(&sync->columns);

	zbx_free(sync->row);

	// 如果同步模式为ZBX_DBSYNC_UPDATE，则执行以下操作
	if (ZBX_DBSYNC_UPDATE == sync->mode)
	{
		int			i, j;
		zbx_dbsync_row_t	*row;

		for (i = 0; i < sync->rows.values_num; i++)
		{
			row = (zbx_dbsync_row_t *)sync->rows.values[i];

			// 如果row结构体不为空，则遍历其内的每个字段并释放内存
			if (NULL != row->row)
			{
				for (j = 0; j < sync->columns_num; j++)
					dbsync_strfree(row->row[j]);

				zbx_free(row->row);
			}

			// 释放row结构体内存
			zbx_free(row);
		}

		// 释放rows向量内存
		zbx_vector_ptr_destroy(&sync->rows);
	}
	else
	{
		// 如果是其他同步模式，则释放数据库结果集
		DBfree_result(sync->dbresult);
		sync->dbresult = NULL;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_next                                                  *
 *                                                                            *
 * Purpose: gets the next row from the changeset                              *
 *                                                                            *
 * Parameters: sync  - [IN] the changeset                                     *
 *             rowid - [OUT] the row identifier (required for row removal,    *
 *                          optional for new/updated rows)                    *
 *             row   - [OUT] the row data                                     *
 *             tag   - [OUT] the row tag, identifying changes                 *
/******************************************************************************
 * /
 ******************************************************************************/* ******************************************************************************
 * zbx_dbsync_next 函数：从数据库中获取下一行数据
 * 
 * 参数：
 *   sync：zbx_dbsync_t 类型，同步对象
 *   rowid：zbx_uint64_t 类型，用于存储下一行的行ID
 *   row：char*** 类型，用于存储下一行的数据
 *   tag：unsigned char 类型，用于存储下一行的标签
 * 
 * 返回值：
 *   SUCCEED - 成功获取到下一行数据
 *   FAIL    - 没有更多数据可供获取
 * 
 * 注释：
 *   1. 如果同步模式为 ZBX_DBSYNC_UPDATE，则按照更新模式获取下一行数据
 *   2. 如果同步索引已经到达数据末尾，则返回 FAIL，表示没有更多数据
 *   3. 获取下一行数据，并将行ID、行数据和标签存储在传入的指针变量中
 *   4. 如果同步模式为查询模式（非更新模式），则从数据库中查询下一行数据
 *   5. 如果数据库查询失败，返回 FAIL，表示没有更多数据
 *   6. 对查询到的数据进行预处理，将其存储在 row 指针变量中
 *   7. 设置行ID为0，标签为 ZBX_DBSYNC_ROW_ADD
 *   8. 调用者需增加行数据的相关处理（如：插入、更新等）
 * 
 *******************************************************************************/

int	zbx_dbsync_next(zbx_dbsync_t *sync, zbx_uint64_t *rowid, char ***row, unsigned char *tag)
{
	// 如果同步模式为 ZBX_DBSYNC_UPDATE
	if (ZBX_DBSYNC_UPDATE == sync->mode)
	{
		// 获取下一行数据
		zbx_dbsync_row_t	*sync_row;

		// 如果同步索引已经到达数据末尾，则返回 FAIL，表示没有更多数据
		if (sync->row_index == sync->rows.values_num)
			return FAIL;

		// 获取下一行数据
		sync_row = (zbx_dbsync_row_t *)sync->rows.values[sync->row_index++];
		*rowid = sync_row->rowid;
		*row = sync_row->row;
		*tag = sync_row->tag;
	}
/******************************************************************************
 * *
 *这段代码的主要目的是比较主机数据库中的记录和主机缓存中的记录，对于不同的记录类型（新增、更新、删除），将它们添加到同步结果中。整个代码块的核心功能是通过循环遍历数据库查询结果和主机缓存，对比主机信息，并根据不同的情况标记为主机记录或删除记录。最后，将同步结果添加到指定的哈希集中。整个过程完成后，释放资源并返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_hosts(zbx_dbsync_t *sync)
{
	// 声明变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	ZBX_DC_HOST		*host;

	// 数据库查询，查询hosts表中符合条件的记录
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (NULL == (result = DBselect(
			"select hostid,proxy_hostid,host,ipmi_authtype,ipmi_privilege,ipmi_username,"
				"ipmi_password,maintenance_status,maintenance_type,maintenance_from,"
				"errors_from,available,disable_until,snmp_errors_from,"
				"snmp_available,snmp_disable_until,ipmi_errors_from,ipmi_available,"
				"ipmi_disable_until,jmx_errors_from,jmx_available,jmx_disable_until,"
				"status,name,lastaccess,error,snmp_error,ipmi_error,jmx_error,tls_connect,tls_accept"
				",tls_issuer,tls_subject,tls_psk_identity,tls_psk,proxy_address,auto_compress,"
				"maintenanceid"
			" from hosts"
			" where status in (%d,%d,%d,%d)"
				" and flags<>%d",
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
			HOST_STATUS_PROXY_ACTIVE, HOST_STATUS_PROXY_PASSIVE,
			ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		return FAIL;
	}

	// 初始化dbsync
	dbsync_prepare(sync, 38, NULL);
#else
	if (NULL == (result = DBselect(
			"select hostid,proxy_hostid,host,ipmi_authtype,ipmi_privilege,ipmi_username,"
				"ipmi_password,maintenance_status,maintenance_type,maintenance_from,"
				"errors_from,available,disable_until,snmp_errors_from,"
				"snmp_available,snmp_disable_until,ipmi_errors_from,ipmi_available,"
				"ipmi_disable_until,jmx_errors_from,jmx_available,jmx_disable_until,"
				"status,name,lastaccess,error,snmp_error,ipmi_error,jmx_error,tls_connect,tls_accept,"
				"proxy_address,auto_compress,maintenanceid"
			" from hosts"
			" where status in (%d,%d,%d,%d)"
				" and flags<>%d",
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
			HOST_STATUS_PROXY_ACTIVE, HOST_STATUS_PROXY_PASSIVE,
			ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		return FAIL;
	}

	// 初始化dbsync
	dbsync_prepare(sync, 34, NULL);
#endif

	// 如果dbsync模式为初始化
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		// 设置dbsync结果为查询结果
		sync->dbresult = result;
		// 返回成功
		return SUCCEED;
	}

	// 创建一个哈希集用于存储主机ID
	zbx_hashset_create(&ids, dbsync_env.cache->hosts.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果，对比主机信息
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将主机ID插入哈希集中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查询主机缓存
		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&dbsync_env.cache->hosts, &rowid)))
			// 标记为新增主机
			tag = ZBX_DBSYNC_ROW_ADD;
		else if (FAIL == dbsync_compare_host(host, dbrow))
			// 标记为更新主机
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 添加主机到同步结果中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 遍历主机缓存，处理未匹配的主机
	zbx_hashset_iter_reset(&dbsync_env.cache->hosts, &iter);
	while (NULL != (host = (ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		// 查询哈希集中是否有主机ID
		if (NULL == zbx_hashset_search(&ids, &host->hostid))
			// 标记为删除主机
			dbsync_add_row(sync, host->hostid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁哈希集
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}

		return FAIL;

	/* 检查主机名称是否一致 */
	if (FAIL == dbsync_compare_str(dbrow[23], host->name))
		return FAIL;

	/* 检查TLS相关信息是否一致 */
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (FAIL == dbsync_compare_str(dbrow[31], host->tls_issuer))
		return FAIL;

	if (FAIL == dbsync_compare_str(dbrow[32], host->tls_subject))
		return FAIL;

	/* 检查TLS密码是否为空，如果为空，则检查内存中的TLS密码是否为空 */
	if ('\0' == *dbrow[33] || '\0' == *dbrow[34])
	{
		if (NULL != host->tls_dc_psk)
			return FAIL;
	}
	else
	{
		/* 如果TLS密码不为空，则比较TLS密码的相关信息是否一致 */
		if (NULL == host->tls_dc_psk)
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[33], host->tls_dc_psk->tls_psk_identity))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[34], host->tls_dc_psk->tls_psk))
			return FAIL;
	}
#endif

	/* 检查IPMI相关信息是否一致 */
	ipmi_authtype = (signed char)atoi(dbrow[3]);
	ipmi_privilege = (unsigned char)atoi(dbrow[4]);

	/* 检查IPMI主机是否已经存在，如果不存在，则创建一个新的IPMI主机 */
	if (ZBX_IPMI_DEFAULT_AUTHTYPE != ipmi_authtype || ZBX_IPMI_DEFAULT_PRIVILEGE != ipmi_privilege ||
			'\0' != *dbrow[5] || '\0' != *dbrow[6])	/* useipmi */
	{
		if (NULL == (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&dbsync_env.cache->ipmihosts,
				&host->hostid)))
		{
			return FAIL;
		}

		/* 检查IPMI认证类型和权限是否一致 */
		if (ipmihost->ipmi_authtype != ipmi_authtype)
			return FAIL;

		if (ipmihost->ipmi_privilege != ipmi_privilege)
			return FAIL;

		/* 检查IPMI用户名和密码是否一致 */
		if (FAIL == dbsync_compare_str(dbrow[5], ipmihost->ipmi_username))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[6], ipmihost->ipmi_password))
			return FAIL;
	}
	else if (NULL != zbx_hashset_search(&dbsync_env.cache->ipmihosts, &host->hostid))
		return FAIL;

	/* 检查代理相关信息是否一致 */
	if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&dbsync_env.cache->proxies, &host->hostid)))
	{
		/* 检查代理地址是否一致 */
		if (FAIL == dbsync_compare_str(dbrow[31 + ZBX_HOST_TLS_OFFSET], proxy->proxy_address))
			return FAIL;

		/* 检查代理是否支持自动压缩 */
		if (FAIL == dbsync_compare_uchar(dbrow[32 + ZBX_HOST_TLS_OFFSET], proxy->auto_compress))
			return FAIL;
	}

	/* 如果所有比较都成功，则返回SUCCEED，表示主机信息一致 */
	return SUCCEED;
}


#endif
	if (FAIL == dbsync_compare_uchar(dbrow[29], host->tls_connect))
		return FAIL;

	if (FAIL == dbsync_compare_uchar(dbrow[30], host->tls_accept))
		return FAIL;

	/* IPMI hosts */

	ipmi_authtype = (signed char)atoi(dbrow[3]);
	ipmi_privilege = (unsigned char)atoi(dbrow[4]);

	if (ZBX_IPMI_DEFAULT_AUTHTYPE != ipmi_authtype || ZBX_IPMI_DEFAULT_PRIVILEGE != ipmi_privilege ||
			'\0' != *dbrow[5] || '\0' != *dbrow[6])	/* useipmi */
	{
		if (NULL == (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&dbsync_env.cache->ipmihosts,
				&host->hostid)))
		{
			return FAIL;
		}

		if (ipmihost->ipmi_authtype != ipmi_authtype)
			return FAIL;

		if (ipmihost->ipmi_privilege != ipmi_privilege)
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[5], ipmihost->ipmi_username))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[6], ipmihost->ipmi_password))
			return FAIL;
	}
	else if (NULL != zbx_hashset_search(&dbsync_env.cache->ipmihosts, &host->hostid))
		return FAIL;

	/* proxies */
	if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&dbsync_env.cache->proxies, &host->hostid)))
	{
		if (FAIL == dbsync_compare_str(dbrow[31 + ZBX_HOST_TLS_OFFSET], proxy->proxy_address))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[32 + ZBX_HOST_TLS_OFFSET], proxy->auto_compress))
			return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_hosts                                         *
 *                                                                            *
 * Purpose: compares hosts table with cached configuration data               *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_dbsync_compare_hosts(zbx_dbsync_t *sync)
{
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	ZBX_DC_HOST		*host;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (NULL == (result = DBselect(
			"select hostid,proxy_hostid,host,ipmi_authtype,ipmi_privilege,ipmi_username,"
				"ipmi_password,maintenance_status,maintenance_type,maintenance_from,"
				"errors_from,available,disable_until,snmp_errors_from,"
				"snmp_available,snmp_disable_until,ipmi_errors_from,ipmi_available,"
				"ipmi_disable_until,jmx_errors_from,jmx_available,jmx_disable_until,"
				"status,name,lastaccess,error,snmp_error,ipmi_error,jmx_error,tls_connect,tls_accept"
				",tls_issuer,tls_subject,tls_psk_identity,tls_psk,proxy_address,auto_compress,"
				"maintenanceid"
			" from hosts"
			" where status in (%d,%d,%d,%d)"
				" and flags<>%d",
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
			HOST_STATUS_PROXY_ACTIVE, HOST_STATUS_PROXY_PASSIVE,
			ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		return FAIL;
	}

	dbsync_prepare(sync, 38, NULL);
#else
	if (NULL == (result = DBselect(
			"select hostid,proxy_hostid,host,ipmi_authtype,ipmi_privilege,ipmi_username,"
/******************************************************************************
 * *
 *这段代码的主要目的是比较主机库存数据，并将比较结果保存在一个哈希集中。具体来说，它执行以下操作：
 *
 *1. 定义所需的变量，如数据库行、查询结果、哈希集等。
 *2. 定义查询语句，从数据库中获取主机库存数据。
 *3. 判断同步模式，初始化同步数据。
 *4. 创建哈希集，用于存储主机库存记录。
 *5. 遍历查询结果，解析主机库存记录，并将它们添加到哈希集中。
 *6. 重置哈希集迭代器，遍历主机库存记录，检查哈希集中是否包含主机ID。如果不包含，则删除主机库存记录。
 *7. 销毁哈希集，释放查询结果。
 *8. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_host_inventory(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	ZBX_DC_HOST_INVENTORY	*hi;
	const char		*sql;

	// 定义查询语句
	sql = "select hostid,inventory_mode,type,type_full,name,alias,os,os_full,os_short,serialno_a,"
			"serialno_b,tag,asset_tag,macaddress_a,macaddress_b,hardware,hardware_full,software,"
			"software_full,software_app_a,software_app_b,software_app_c,software_app_d,"
			"software_app_e,contact,location,location_lat,location_lon,notes,chassis,model,"
			"hw_arch,vendor,contract_number,installer_name,deployment_status,url_a,url_b,"
			"url_c,host_networks,host_netmask,host_router,oob_ip,oob_netmask,oob_router,"
			"date_hw_purchase,date_hw_install,date_hw_expiry,date_hw_decomm,site_address_a,"
			"site_address_b,site_address_c,site_city,site_state,site_country,site_zip,site_rack,"
			"site_notes,poc_1_name,poc_1_email,poc_1_phone_a,poc_1_phone_b,poc_1_cell,"
			"poc_1_screen,poc_1_notes,poc_2_name,poc_2_email,poc_2_phone_a,poc_2_phone_b,"
			"poc_2_cell,poc_2_screen,poc_2_notes"
			" from host_inventory";

	// 执行查询
	if (NULL == (result = DBselect("%s", sql)))
		return FAIL;

	// 预处理查询
	dbsync_prepare(sync, 72, NULL);

	// 判断同步模式
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		// 设置查询结果
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建哈希集
	zbx_hashset_create(&ids, dbsync_env.cache->host_inventories.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果
	while (NULL != (dbrow = DBfetch(result)))
	{
		// 解析行ID
		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将行ID添加到哈希集中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找主机库存记录
		if (NULL == (hi = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&dbsync_env.cache->host_inventories,
				&rowid)))
		{
			// 新增主机库存记录
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_host_inventory(hi, dbrow))
			// 更新主机库存记录
			tag = ZBX_DBSYNC_ROW_UPDATE;

		if (ZBX_DBSYNC_ROW_NONE != tag)
			// 将记录添加到同步数据中
			dbsync_add_row(sync, rowid, tag, dbrow);

	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->host_inventories, &iter);
	// 遍历主机库存记录
	while (NULL != (hi = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_iter_next(&iter)))
	{
		// 检查哈希集中是否包含主机ID
		if (NULL == zbx_hashset_search(&ids, &hi->hostid))
			// 删除主机库存记录
			dbsync_add_row(sync, hi->hostid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁哈希集
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}

 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的库存信息与内存中的库存信息是否一致。具体来说，就是依次比较数据库中的库存模式和库存字段值是否与内存中的库存模式和库存字段值一致。如果所有字段都比较一致，则返回SUCCEED（成功），否则返回FAIL（失败）。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_host_inventory，接收两个参数，一个是ZBX_DC_HOST_INVENTORY类型的指针hi，另一个是DB_ROW类型的指针dbrow。
static int	dbsync_compare_host_inventory(const ZBX_DC_HOST_INVENTORY *hi, const DB_ROW dbrow)
{
	// 定义一个整型变量i，用于循环使用
	int	i;

	// 判断dbrow[1]（数据库中的库存模式）与hi->inventory_mode（库存模式）是否相同，如果不相同，返回FAIL（失败）
	if (SUCCEED != dbsync_compare_uchar(dbrow[1], hi->inventory_mode))
		return FAIL;

	// 遍历host_inventory_field_count（库存字段计数）次，比较dbrow[2..2+host_inventory_field_count]（数据库中的库存字段值）与hi->values[0..host_inventory_field_count-1]（库存字段值）
	for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
	{
		// 如果dbrow[2+i]与hi->values[i]不相等，返回FAIL（失败）
		if (FAIL == dbsync_compare_str(dbrow[i + 2], hi->values[i]))
			return FAIL;
	}

	// 如果所有库存字段都比较成功，返回SUCCEED（成功）
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_host_inventory                                *
 *                                                                            *
 * Purpose: compares host_inventory table with cached configuration data      *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_dbsync_compare_host_inventory(zbx_dbsync_t *sync)
{
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	ZBX_DC_HOST_INVENTORY	*hi;
	const char		*sql;

	sql = "select hostid,inventory_mode,type,type_full,name,alias,os,os_full,os_short,serialno_a,"
			"serialno_b,tag,asset_tag,macaddress_a,macaddress_b,hardware,hardware_full,software,"
			"software_full,software_app_a,software_app_b,software_app_c,software_app_d,"
			"software_app_e,contact,location,location_lat,location_lon,notes,chassis,model,"
			"hw_arch,vendor,contract_number,installer_name,deployment_status,url_a,url_b,"
			"url_c,host_networks,host_netmask,host_router,oob_ip,oob_netmask,oob_router,"
			"date_hw_purchase,date_hw_install,date_hw_expiry,date_hw_decomm,site_address_a,"
			"site_address_b,site_address_c,site_city,site_state,site_country,site_zip,site_rack,"
			"site_notes,poc_1_name,poc_1_email,poc_1_phone_a,poc_1_phone_b,poc_1_cell,"
			"poc_1_screen,poc_1_notes,poc_2_name,poc_2_email,poc_2_phone_a,poc_2_phone_b,"
			"poc_2_cell,poc_2_screen,poc_2_notes"
			" from host_inventory";

	if (NULL == (result = DBselect("%s", sql)))
		return FAIL;

	dbsync_prepare(sync, 72, NULL);

	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	zbx_hashset_create(&ids, dbsync_env.cache->host_inventories.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		if (NULL == (hi = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&dbsync_env.cache->host_inventories,
				&rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_host_inventory(hi, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);

	}

	zbx_hashset_iter_reset(&dbsync_env.cache->host_inventories, &iter);
	while (NULL != (hi = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &hi->hostid))
			dbsync_add_row(sync, hi->hostid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中hosts_templates表中的数据与缓存中的数据，找出新增和删除的记录，并将这些记录添加到同步数据结构中。具体来说，代码完成了以下任务：
 *
 *1. 从数据库中查询hosts_templates表的数据，并按照hostid排序。
 *2. 初始化同步数据结构。
 *3. 遍历数据库查询结果，将hostid和templateid添加到哈希集中。
 *4. 遍历哈希集，找出在数据库中不存在但在哈希集中存在的记录，将其添加到同步数据结构中。
 *5. 找出在数据库中存在但在哈希集中不存在的记录，将其添加到同步数据结构中。
 *6. 释放数据库查询结果，销毁哈希集。
 *
 *整个过程实现了hosts_templates表的同步操作。
 ******************************************************************************/
int	zbx_dbsync_compare_host_templates(zbx_dbsync_t *sync)
{
	/* 定义变量，这里就不一一解释了，后面会详细解释 */

	if (NULL == (result = DBselect(
			"select hostid,templateid"
			" from hosts_templates"
			" order by hostid")))
	{
		return FAIL;
	}

	dbsync_prepare(sync, 2, NULL);

	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	zbx_hashset_create(&htmpls, 100, ZBX_DEFAULT_UINT64_PAIR_HASH_FUNC, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);

	/* 遍历所有 host->template 链接 */
	zbx_hashset_iter_reset(&dbsync_env.cache->htmpls, &iter);
	while (NULL != (htmpl = (ZBX_DC_HTMPL *)zbx_hashset_iter_next(&iter)))
	{
		ht_local.first = htmpl->hostid;

		for (i = 0; i < htmpl->templateids.values_num; i++)
		{
			ht_local.second = htmpl->templateids.values[i];
			zbx_hashset_insert(&htmpls, &ht_local, sizeof(ht_local));
		}
	}

	/* 添加新行，从索引中移除现有行 */
	while (NULL != (dbrow = DBfetch(result)))
	{
		ZBX_STR2UINT64(ht_local.first, dbrow[0]);
		ZBX_STR2UINT64(ht_local.second, dbrow[1]);

		if (NULL == (ht = (zbx_uint64_pair_t *)zbx_hashset_search(&htmpls, &ht_local)))
			dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_ADD, dbrow);
		else
			zbx_hashset_remove_direct(&htmpls, ht);
	}

	/* 添加删除的行 */
	zbx_hashset_iter_reset(&htmpls, &iter);
	while (NULL != (ht = (zbx_uint64_pair_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_snprintf(hostid_s, sizeof(hostid_s), ZBX_FS_UI64, ht->first);
		zbx_snprintf(templateid_s, sizeof(templateid_s), ZBX_FS_UI64, ht->second);
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	DBfree_result(result);
	zbx_hashset_destroy(&htmpls);

	return SUCCEED;
}

	{
		zbx_snprintf(hostid_s, sizeof(hostid_s), ZBX_FS_UI64, ht->first);
		zbx_snprintf(templateid_s, sizeof(templateid_s), ZBX_FS_UI64, ht->second);
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	DBfree_result(result);
	zbx_hashset_destroy(&htmpls);
/******************************************************************************
 * *
 *整个代码块的主要目的是比较全局宏（gmacro）和数据库中的一行数据（dbrow），具体包括以下几个步骤：
 *
 *1. 比较dbrow[2]（数据库中的一行数据）和gmacro->value（全局宏的值）；
 *2. 如果dbrow[1]（数据库中的一行数据）解析为用户宏失败，直接返回FAIL；
 *3. 判断gmacro->macro（全局宏的宏名）和macro（解析后的用户宏名）是否相等；
 *4. 判断context是否为空，如果为空，且gmacro->context不为空，跳转到out标签处；
 *5. 如果context和gmacro->context相等，返回SUCCEED，表示匹配成功；
 *6. 释放macro和context内存。
 *
 *最终输出结果为：
 *
 *```
 *static int dbsync_compare_global_macro(const ZBX_DC_GMACRO *gmacro, const DB_ROW dbrow)
 *{
 *\tchar *macro = NULL, *context = NULL;
 *\tint ret = FAIL;
 *
 *\t// 判断dbrow[2]和gmacro->value是否相等，如果不相等，返回FAIL
 *\tif (FAIL == dbsync_compare_str(dbrow[2], gmacro->value))
 *\t\treturn FAIL;
 *
 *\t// 解析dbrow[1]为用户宏，存储macro和context
 *\tif (SUCCEED != zbx_user_macro_parse_dyn(dbrow[1], &macro, &context, NULL))
 *\t\treturn FAIL;
 *
 *\t// 判断gmacro->macro和macro是否相等，如果不相等，跳转到out标签处
 *\tif (0 != strcmp(gmacro->macro, macro))
 *\t\tgoto out;
 *
 *\t// 判断context是否为空，如果为空，且gmacro->context不为空，跳转到out标签处
 *\tif (NULL == context)
 *\t{
 *\t\tif (NULL != gmacro->context)
 *\t\t\tgoto out;
 *
 *\t\t// 如果context为空，但gmacro->context为空，说明匹配成功，返回SUCCEED
 *\t\tret = SUCCEED;
 *\t\tgoto out;
 *\t}
 *
 *\t// 判断gmacro->context是否为空，如果为空，跳转到out标签处
 *\tif (NULL == gmacro->context)
 *\t\tgoto out;
 *
 *\t// 判断gmacro->context和context是否相等，如果相等，返回SUCCEED
 *\tif (0 == strcmp(gmacro->context, context))
 *\t\tret = SUCCEED;
 *
 *out:
 *\t// 释放macro和context内存
 *\tzbx_free(macro);
 *\tzbx_free(context);
 *
 *\t// 返回比较结果
 *\treturn ret;
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数，用于比较全局宏（gmacro）和数据库中的一行数据（dbrow）
static int	dbsync_compare_global_macro(const ZBX_DC_GMACRO *gmacro, const DB_ROW dbrow)
{
	// 声明两个字符指针macro和context，以及一个整型变量ret，用于存储比较结果
	char	*macro = NULL, *context = NULL;
	int	ret = FAIL;

	// 判断dbrow[2]（数据库中的一行数据）和gmacro->value（全局宏的值）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_str(dbrow[2], gmacro->value))
		return FAIL;

	// 如果dbrow[1]（数据库中的一行数据）解析为用户宏失败，返回FAIL
	if (SUCCEED != zbx_user_macro_parse_dyn(dbrow[1], &macro, &context, NULL))
		return FAIL;

	// 判断gmacro->macro（全局宏的宏名）和macro（解析后的用户宏名）是否相等，如果不相等，跳转到out标签处
	if (0 != strcmp(gmacro->macro, macro))
		goto out;

	// 判断context是否为空，如果为空，且gmacro->context不为空，跳转到out标签处
	if (NULL == context)
	{
		if (NULL != gmacro->context)
			goto out;

		// 如果context为空，但gmacro->context为空，说明匹配成功，返回SUCCEED
		ret = SUCCEED;
		goto out;
	}

	// 判断gmacro->context是否为空，如果为空，跳转到out标签处
	if (NULL == gmacro->context)
		goto out;

	// 判断gmacro->context和context是否相等，如果相等，返回SUCCEED
	if (0 == strcmp(gmacro->context, context))
		ret = SUCCEED;

out:
	// 释放macro和context内存
	zbx_free(macro);
	zbx_free(context);

	// 返回比较结果
	return ret;
}

			goto out;

		ret = SUCCEED;
		goto out;
	}

	if (NULL == gmacro->context)
		goto out;

	if (0 == strcmp(gmacro->context, context))
		ret = SUCCEED;
out:
	zbx_free(macro);
	zbx_free(context);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_global_macros                                 *
 *                                                                            *
 * Purpose: compares global macros table with cached configuration data       *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较全局宏数据库中的数据与本地缓存的全局宏数据，并根据需要添加、更新或删除全局宏。具体来说，代码实现了以下功能：
 *
 *1. 从数据库中查询全局宏数据。
 *2. 初始化dbsync结构体。
 *3. 遍历数据库查询结果，将全局宏数据存储到哈希集中。
 *4. 遍历哈希集，对比本地缓存的全球
 ******************************************************************************/
int zbx_dbsync_compare_global_macros(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 全局变量，用于存储全局宏的数据
	zbx_hashset_iter_t	iter; // 迭代器，用于遍历全局宏数据
	zbx_uint64_t		rowid; // 记录ID
	ZBX_DC_GMACRO		*macro; // 全局宏结构体指针

	// 从数据库中查询全局宏数据
	if (NULL == (result = DBselect(
			"select globalmacroid,macro,value"
			" from globalmacro")))
	{
		// 如果查询失败，返回错误码
		return FAIL;
	}

	// 初始化dbsync结构体
	dbsync_prepare(sync, 3, NULL);

	// 如果同步模式为初始化，则将查询结果赋值给dbresult
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		// 同步成功，返回0
		return SUCCEED;
	}

	// 创建一个哈希集，用于存储全局宏的ID
	zbx_hashset_create(&ids, dbsync_env.cache->gmacros.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历数据库查询结果，并将全局宏数据存储到哈希集中
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将字符串转换为整数
		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将全局宏ID插入到哈希集中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找哈希集中是否存在该全局宏
		if (NULL == (macro = (ZBX_DC_GMACRO *)zbx_hashset_search(&dbsync_env.cache->gmacros, &rowid)))
			// 如果不存在，标记为新增
			tag = ZBX_DBSYNC_ROW_ADD;
		else if (FAIL == dbsync_compare_global_macro(macro, dbrow))
			// 如果对比失败，标记为更新
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果标记不为空，则将全局宏数据添加到同步结果中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->gmacros, &iter);
	// 遍历哈希集，删除不在数据库中的全局宏
	while (NULL != (macro = (ZBX_DC_GMACRO *)zbx_hashset_iter_next(&iter)))
	{
		// 如果不存在于ids哈希集中，则删除该全局宏
		if (NULL == zbx_hashset_search(&ids, &macro->globalmacroid))
			dbsync_add_row(sync, macro->globalmacroid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁ids哈希集
	zbx_hashset_destroy(&ids);
	// 释放数据库查询结果
	DBfree_result(result);

	// 同步成功，返回0
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_host_macro                                        *
 *                                                                            *
 * Purpose: compares host macro table row with cached configuration data      *
 *                                                                            *
 * Parameter: hmacro - [IN] the cached host macro data                        *
 *            row -    [IN] the database row                                  *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个数据库记录（DB_ROW结构体）中的宏（host_macro）是否匹配。匹配条件包括：数据库记录中的宏值、主机ID和上下文（context）与host_macro中的相应值和上下文相等。如果匹配成功，函数返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_host_macro，接收两个参数，一个是指向ZBX_DC_HMACRO结构体的指针，另一个是指向DB_ROW结构体的指针
static int	dbsync_compare_host_macro(const ZBX_DC_HMACRO *hmacro, const DB_ROW dbrow)
{
	// 定义两个字符指针macro和context，用于存储解析后的宏和上下文
	char	*macro = NULL, *context = NULL;
	// 定义一个整型变量ret，用于存储比较结果
	int	ret = FAIL;

	// 判断dbrow[3]和hmacro->value字符串是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_str(dbrow[3], hmacro->value))
		return FAIL;

	// 判断dbrow[1]和hmacro->hostid整型值是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_uint64(dbrow[1], hmacro->hostid))
		return FAIL;

	// 调用zbx_user_macro_parse_dyn函数解析dbrow[2]字符串，并将结果存储在macro和context指针中
	if (SUCCEED != zbx_user_macro_parse_dyn(dbrow[2], &macro, &context, NULL))
		return FAIL;

	// 判断hmacro->macro和macro字符串是否相等，如果不相等，跳转到out标签处
	if (0 != strcmp(hmacro->macro, macro))
		goto out;

	// 判断context指针是否为空，如果为空，且hmacro->context不为空，跳转到out标签处
	if (NULL == context)
	{
		if (NULL != hmacro->context)
			goto out;

		// 如果context为空，但hmacro->context为空，说明匹配成功，将ret设置为SUCCEED，跳转到out标签处
		ret = SUCCEED;
		goto out;
	}

	// 判断hmacro->context和context字符串是否相等，如果相等，将ret设置为SUCCEED，跳转到out标签处
	if (NULL == hmacro->context)
		goto out;

	if (0 == strcmp(hmacro->context, context))
		ret = SUCCEED;
out:
	// 释放macro和context指针占用的内存
	zbx_free(macro);
	zbx_free(context);

	// 返回比较结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_host_macros                                   *
 *                                                                            *
 * Purpose: compares global macros table with cached configuration data       *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的主机宏和缓存中的主机宏，并将差异应用到同步数据中。具体来说，代码实现了以下功能：
 *
 *1. 从数据库中查询主机宏信息。
 *2. 初始化同步数据结构体。
 *3. 迭代查询结果，将查询到的主机宏ID添加到哈希集合中。
 *4. 查找哈希集合中的主机宏，判断是否需要在同步数据中添加新增、更新或删除操作。
 *5. 迭代哈希集合中的主机宏，判断是否需要在同步数据中添加删除操作。
 *6. 销毁哈希集合。
 *7. 释放查询结果。
 *8. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_host_macros(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 定义一个哈希集合，用于存储主机宏ID
	zbx_hashset_iter_t	iter; // 定义一个迭代器，用于迭代哈希集合
	zbx_uint64_t		rowid; // 存储查询到的主机宏ID
	ZBX_DC_HMACRO		*macro; // 存储主机宏结构体指针

	// 从数据库中查询主机宏信息
	if (NULL == (result = DBselect(
			"select hostmacroid,hostid,macro,value"
			" from hostmacro")))
	{
		// 如果查询失败，返回FAIL
		return FAIL;
	}

	// 初始化dbsync结构体
	dbsync_prepare(sync, 4, NULL);

	// 如果当前同步模式为初始化，将查询结果赋值给dbresult，并返回成功
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个哈希集合，用于存储查询到的主机宏ID
	zbx_hashset_create(&ids, dbsync_env.cache->hmacros.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 迭代查询结果，并将查询到的主机宏ID添加到哈希集合中
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将主机宏ID转换为无符号整数
		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将主机宏ID添加到哈希集合中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找哈希集合中是否有该主机宏
		if (NULL == (macro = (ZBX_DC_HMACRO *)zbx_hashset_search(&dbsync_env.cache->hmacros, &rowid)))
			// 如果没有找到，标记为新增
			tag = ZBX_DBSYNC_ROW_ADD;
		else if (FAIL == dbsync_compare_host_macro(macro, dbrow))
			// 如果有找到且比较失败，标记为更新
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果不是无效行，将主机宏添加到同步数据中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集合迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->hmacros, &iter);
	// 迭代哈希集合中的主机宏，判断是否需要在同步数据中添加删除操作
	while (NULL != (macro = (ZBX_DC_HMACRO *)zbx_hashset_iter_next(&iter)))
	{
		// 查找哈希集合中是否有该主机宏ID
		if (NULL == zbx_hashset_search(&ids, &macro->hostmacroid))
			// 如果没有找到，表示需要删除，添加删除操作到同步数据中
			dbsync_add_row(sync, macro->hostmacroid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁哈希集合
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_interface                                         *
 *                                                                            *
 * Purpose: compares interface table row with cached configuration data       *
 *                                                                            *
 * Parameter: interface - [IN] the cached interface data                      *
 *            row       - [IN] the database row                               *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: User macros used in ip, dns fields will always make compare to   *
 *           fail.                                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的一行数据与接口结构体中的相应成员变量，如果所有成员变量都匹配，则返回SUCCEED，否则返回FAIL。这个函数用于检查数据同步时接口数据的准确性。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_interface，接收两个参数，一个是ZBX_DC_INTERFACE类型的指针，另一个是DB_ROW类型的指针
static int	dbsync_compare_interface(const ZBX_DC_INTERFACE *interface, const DB_ROW dbrow)
{
	// 判断dbrow[1]（数据库中的第2列）的值是否与interface->hostid（接口的hostid）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_uint64(dbrow[1], interface->hostid))
		return FAIL;

	// 判断dbrow[2]（数据库中的第3列）的值是否与interface->type（接口的类型）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[2], interface->type))
		return FAIL;

	// 判断dbrow[3]（数据库中的第4列）的值是否与interface->main（接口的主干）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[3], interface->main))
		return FAIL;

	// 判断dbrow[4]（数据库中的第5列）的值是否与interface->useip（接口的useip）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[4], interface->useip))
		return FAIL;

	// 判断dbrow[8]（数据库中的第9列）的值是否与interface->bulk（接口的bulk）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[8], interface->bulk))
		return FAIL;

	// 判断dbrow[5]（数据库中的第6列）是否包含"{$"字符串，如果包含，返回FAIL
	if (NULL != strstr(dbrow[5], "{$"))
		return FAIL;

	// 判断dbrow[5]（数据库中的第6列）的值是否与interface->ip（接口的ip）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_str(dbrow[5], interface->ip))
		return FAIL;

	// 判断dbrow[6]（数据库中的第7列）是否包含"{$"字符串，如果包含，返回FAIL
	if (NULL != strstr(dbrow[6], "{$"))
		return FAIL;

	// 判断dbrow[6]（数据库中的第7列）的值是否与interface->dns（接口的dns）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_str(dbrow[6], interface->dns))
		return FAIL;

	// 判断dbrow[7]（数据库中的第8列）的值是否与interface->port（接口的port）相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_str(dbrow[7], interface->port))
		return FAIL;

	// 如果以上所有判断都通过，返回SUCCEED
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中查询到的接口信息与缓存中的接口信息，并将差异应用于同步数据。具体来说，代码实现了以下功能：
 *
 *1. 从数据库中查询接口信息，并存储在result变量中。
 *2. 初始化dbsync模块，准备处理数据。
 *3. 判断同步模式，如果为初始化模式，将查询结果赋值给dbresult，并返回成功。
 *4. 创建一个哈希集合，用于存储接口ID。
 *5. 遍历数据库查询结果，将接口ID插入到哈希集合中。
 *6. 查找缓存中的接口，并根据对比结果标记为添加、更新或删除。
 *7. 将标记为添加或更新的接口添加到同步数据中。
 *8. 重置缓存中接口的迭代器，遍历缓存中的接口，检查是否在哈希集合中。
 *9. 如果接口ID不在哈希集合中，将其标记为删除。
 *10. 销毁哈希集合，释放数据库查询结果。
 *11. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_interfaces(zbx_dbsync_t *sync)
{
	// 声明变量
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *```c
 ******************************************************************************/
// 定义一个名为 dbsync_compare_item 的函数，该函数用于比较两个数据项（来自数据库和内存中的数据项）
static int	dbsync_compare_item(const ZBX_DC_ITEM *item, const DB_ROW dbrow)
{
    // 定义一个指向数据项类型的指针
    ZBX_DC_NUMITEM	*numitem;
    ZBX_DC_SNMPITEM	*snmpitem;
    ZBX_DC_IPMIITEM	*ipmiitem;
    ZBX_DC_TRAPITEM	*trapitem;
    ZBX_DC_LOGITEM	*logitem;
    ZBX_DC_DBITEM	*dbitem;
    ZBX_DC_SSHITEM	*sshitem;
    ZBX_DC_TELNETITEM	*telnetitem;
    ZBX_DC_SIMPLEITEM	*simpleitem;
    ZBX_DC_JMXITEM	*jmxitem;
    ZBX_DC_CALCITEM	*calcitem;
    ZBX_DC_DEPENDENTITEM	*depitem;
    ZBX_DC_HOST		*host;
    ZBX_DC_HTTPITEM	*httpitem;
    unsigned char		value_type, type;
    int			history_sec, trends_sec;

    // 比较主机 ID
    if (FAIL == dbsync_compare_uint64(dbrow[1], item->hostid))
        return FAIL;

    // 从内存中查找主机
    if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&dbsync_env.cache->hosts, &item->hostid)))
        return FAIL;

    // 检查是否有更新项
    if (0 != host->update_items)
        return FAIL;

    // 比较状态
    if (FAIL == dbsync_compare_uchar(dbrow[2], item->status))
        return FAIL;

    // 解析数据项类型
    ZBX_STR2UCHAR(type, dbrow[3]);
    // 比较数据项类型
    if (item->type != type)
        return FAIL;

    // 比较端口
    if (FAIL == dbsync_compare_str(dbrow[8], item->port))
        return FAIL;

    // 比较标志
    if (FAIL == dbsync_compare_uchar(dbrow[24], item->flags))
        return FAIL;

    // 比较接口 ID
    if (FAIL == dbsync_compare_uint64(dbrow[25], item->interfaceid))
        return FAIL;

    // 获取历史秒数
    if (SUCCEED != is_time_suffix(dbrow[31], &history_sec, ZBX_LENGTH_UNLIMITED))
        history_sec = ZBX_HK_PERIOD_MAX;

    // 获取趋势秒数
    if (0 != history_sec && ZBX_HK_OPTION_ENABLED == dbsync_env.cache->config->hk.history_global)
        history_sec = dbsync_env.cache->config->hk.history;

    // 比较历史秒数
    if (item->history != (0 != history_sec))
        return FAIL;

    // 比较历史秒数
    if (history_sec != item->history_sec)
        return FAIL;

    // 比较 inventory_link
    if (FAIL == dbsync_compare_uchar(dbrow[33], item->inventory_link))
        return FAIL;

    // 比较值映射 ID
    if (FAIL == dbsync_compare_uint64(dbrow[34], item->valuemapid))
        return FAIL;

    // 解析数据项类型
    ZBX_STR2UCHAR(value_type, dbrow[4]);
    // 比较数据项类型
    if (item->value_type != value_type)
        return FAIL;

    // 比较键
    if (FAIL == dbsync_compare_str(dbrow[5], item->key))
        return FAIL;

    // 比较延迟
    if (FAIL == dbsync_compare_str(dbrow[14], item->delay))
        return FAIL;

    // 比较数值类型
    numitem = (ZBX_DC_NUMITEM *)zbx_hashset_search(&dbsync_env.cache->numitems, &item->itemid);
    if (ITEM_VALUE_TYPE_FLOAT == value_type || ITEM_VALUE_TYPE_UINT64 == value_type)
    {
        // 获取趋势秒数
        if (SUCCEED != is_time_suffix(dbrow[32], &trends_sec, ZBX_LENGTH_UNLIMITED))
            trends_sec = ZBX_HK_PERIOD_MAX;

        // 获取趋势配置
        if (0 != trends_sec && ZBX_HK_OPTION_ENABLED == dbsync_env.cache->config->hk.trends_global)
            trends_sec = dbsync_env.cache->config->hk.trends;

        // 比较趋势配置
        if (numitem->trends != (0 != trends_sec))
            return FAIL;

        // 比较单位
        if (FAIL == dbsync_compare_str(dbrow[35], numitem->units))
            return FAIL;
    }
    else if (NULL != numitem)
        return FAIL;

    // 比较 SNMP 社区
    snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&dbsync_env.cache->snmpitems, &item->itemid);
    if (SUCCEED == is_snmp_type(type))
    {
        // 比较 SNMPv3 安全名
        if (NULL == snmpitem)
            return FAIL;

        // 比较 SNMPv3 安全级别
        if (FAIL == dbsync_compare_uchar(dbrow[6], snmpitem->snmpv3_securitylevel))
            return FAIL;

        // 比较 SNMPv3 认证密码
        if (FAIL == dbsync_compare_str(dbrow[11], snmpitem->snmpv3_authpassphrase))
            return FAIL;

        // 比较 SNMPv3 隐私密码
        if (FAIL == dbsync_compare_str(dbrow[12], snmpitem->snmpv3_privpassphrase))
            return FAIL;

        // 比较 SNMPv3 认证协议
        if (FAIL == dbsync_compare_uchar(dbrow[26], snmpitem->snmpv3_authprotocol))
            return FAIL;

        // 比较 SNMPv3 隐私协议
        if (FAIL == dbsync_compare_uchar(dbrow[27], snmpitem->snmpv3_privprotocol))
            return FAIL;

        // 比较 SNMPv3 上下文名
        if (FAIL == dbsync_compare_str(dbrow[28], snmpitem->snmpv3_contextname))
            return FAIL;

        // 比较 OID
        if (FAIL == dbsync_compare_str(dbrow[7], snmpitem->snmp_oid))
            return FAIL;
    }
    else if (NULL != snmpitem)
        return FAIL;

    // 比较 IPMI 传感器
    ipmiitem = (ZBX_DC_IPMIITEM *)zbx_hashset_search(&dbsync_env.cache->ipmiitems, &item->itemid);
    if (ITEM_TYPE_IPMI == item->type)
    {
        // 比较 IPMI 传感器名称
        if (NULL == ipmiitem)
            return FAIL;

        // 比较 IPMI 传感器名称
        if (FAIL == dbsync_compare_str(dbrow[13], ipmiitem->ipmi_sensor))
            return FAIL;
    }
    else if (NULL != ipmiitem)
        return FAIL;

    // 比较 trap 主机
    trapitem = (ZBX_DC_TRAPITEM *)zbx_hashset_search(&dbsync_env.cache->trapitems, &item->itemid);
    if (ITEM_TYPE_TRAPPER == item->type && '\0' != *dbrow[15])
    {
        // 解析 trap 主机
        zbx_trim_str_list(dbrow[15], ',');

        // 比较 trap 主机
        if (NULL == trapitem)
            return FAIL;

        // 比较 trap 主机
        if (FAIL == dbsync_compare_str(dbrow[15], trapitem->trapper_hosts))
            return FAIL;
    }
    else if (NULL != trapitem)
        return FAIL;

    // 比较日志时间格式
    logitem = (ZBX_DC_LOGITEM *)zbx_hashset_search(&dbsync_env.cache->logitems, &item->itemid);
    if (ITEM_VALUE_TYPE_LOG == item->value_type && '\0' != *dbrow[16])
    {
        // 比较日志时间格式
        if (NULL == logitem)
            return FAIL;

        // 比较日志时间格式
        if (FAIL == dbsync_compare_str(dbrow[16], logitem->logtimefmt))
            return FAIL;
    }
    else if (NULL != logitem)
        return FAIL;

    // 比较数据库连接参数
    dbitem = (ZBX_DC_DBITEM *)zbx_hashset_search(&dbsync_env.cache->dbitems, &item->itemid);
    if (ITEM_TYPE_DB_MONITOR == item->type && '\0' != *dbrow[17])
    {
        // 比较数据库连接参数
        if (NULL == dbitem)
            return FAIL;

        // 比较用户名
        if (FAIL == dbsync_compare_str(dbrow[17], dbitem->params))
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[20], dbitem->username))
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[21], dbitem->password))
            return FAIL;
    }
    else if (NULL != dbitem)
        return FAIL;

    // 比较 SSH 连接参数
    sshitem = (ZBX_DC_SSHITEM *)zbx_hashset_search(&dbsync_env.cache->sshitems, &item->itemid);
    if (ITEM_TYPE_SSH == item->type)
    {
        // 比较认证类型
        if (NULL == sshitem)
            return FAIL;

        // 比较用户名
        if (FAIL == dbsync_compare_uchar(dbrow[19], sshitem->authtype))
            return FAIL;

        // 比较用户名
        if (FAIL == dbsync_compare_str(dbrow[20], sshitem->username))
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[21], sshitem->password))
            return FAIL;

        // 比较公钥
        if (FAIL == dbsync_compare_str(dbrow[22], sshitem->publickey))
            return FAIL;

        // 比较私钥
        if (FAIL == dbsync_compare_str(dbrow[23], sshitem->privatekey))
            return FAIL;

        // 比较参数
        if (FAIL == dbsync_compare_str(dbrow[17], sshitem->params))
            return FAIL;
    }
    else if (NULL != sshitem)
        return FAIL;

    // 比较 Telnet 连接参数
    telnetitem = (ZBX_DC_TELNETITEM *)zbx_hashset_search(&dbsync_env.cache->telnetitems, &item->itemid);
    if (ITEM_TYPE_TELNET == item->type)
    {
        // 比较用户名
        if (NULL == telnetitem)
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[20], telnetitem->username))
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[21], telnetitem->password))
            return FAIL;

        // 比较参数
        if (FAIL == dbsync_compare_str(dbrow[17], telnetitem->params))
            return FAIL;
    }
    else if (NULL != telnetitem)
        return FAIL;

    // 比较简单项参数
    simpleitem = (ZBX_DC_SIMPLEITEM *)zbx_hashset_search(&dbsync_env.cache->simpleitems, &item->itemid);
    if (ITEM_TYPE_SIMPLE == item->type)
    {
        // 比较用户名
        if (NULL == simpleitem)
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[20], simpleitem->username))
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[21], simpleitem->password))
            return FAIL;
    }
    else if (NULL != simpleitem)
        return FAIL;

    // 比较 JMX 连接参数
    jmxitem = (ZBX_DC_JMXITEM *)zbx_hashset_search(&dbsync_env.cache->jmxitems, &item->itemid);
    if (ITEM_TYPE_JMX == item->type)
    {
        // 比较用户名
        if (NULL == jmxitem)
            return FAIL;

        // 比较用户名
        if (FAIL == dbsync_compare_str(dbrow[20], jmxitem->username))
            return FAIL;

        // 比较密码
        if (FAIL == dbsync_compare_str(dbrow[21], jmxitem->password))
            return FAIL;

        // 比较 JMX 服务端地址
        if (FAIL == dbsync_compare_str(dbrow[37], jmxitem->jmx_endpoint))
            return FAIL;
    }
    else if (NULL != jmxitem)
        return FAIL;

    // 比较计算项参数
    calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&dbsync_env.cache->calcitems, &item->itemid);
    if (ITEM_TYPE_CALCULATED == item->type)
    {
        // 比较参数
        if (NULL == calcitem)
            return FAIL;

        // 比较参数
        if (FAIL == dbsync_compare_str(dbrow[17], calcitem->params))
            return FAIL;
    }
    else if (NULL != calcitem)
        return FAIL;

    // 比较依赖项
    depitem = (ZBX_DC_DEPENDENTITEM *)zbx_hashset_search(&dbsync_env.cache->dependentitems, &item->itemid);
    if (ITEM_TYPE_DEPENDENT == item->type)
    {
        // 比较主项 ID
        if (NULL == depitem)
            return FAIL;

        // 比较主项 ID
        if (FAIL == dbsync_compare_uint64(dbrow[38], depitem->master_itemid))
            return FAIL;
    }

	calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&dbsync_env.cache->calcitems, &item->itemid);
	if (ITEM_TYPE_CALCULATED == item->type)
	{
		if (NULL == calcitem)
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[17], calcitem->params))
			return FAIL;
	}
	else if (NULL != calcitem)
		return FAIL;

	depitem = (ZBX_DC_DEPENDENTITEM *)zbx_hashset_search(&dbsync_env.cache->dependentitems, &item->itemid);
	if (ITEM_TYPE_DEPENDENT == item->type)
	{
		if (NULL == depitem)
			return FAIL;

		if (FAIL == dbsync_compare_uint64(dbrow[38], depitem->master_itemid))
			return FAIL;
	}
	else if (NULL != depitem)
		return FAIL;

	httpitem = (ZBX_DC_HTTPITEM *)zbx_hashset_search(&dbsync_env.cache->httpitems, &item->itemid);
	if (ITEM_TYPE_HTTPAGENT == item->type)
	{
		zbx_trim_str_list(dbrow[15], ',');

		if (NULL == httpitem)
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[39], httpitem->timeout))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[40], httpitem->url))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[41], httpitem->query_fields))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[42], httpitem->posts))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[43], httpitem->status_codes))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[44], httpitem->follow_redirects))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[45], httpitem->post_type))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[46], httpitem->http_proxy))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[47], httpitem->headers))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[48], httpitem->retrieve_mode))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[49], httpitem->request_method))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[50], httpitem->output_format))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[51], httpitem->ssl_cert_file))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[52], httpitem->ssl_key_file))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[53], httpitem->ssl_key_password))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[54], httpitem->verify_peer))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[55], httpitem->verify_host))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[19], httpitem->authtype))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[20], httpitem->username))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[21], httpitem->password))
			return FAIL;

		if (FAIL == dbsync_compare_uchar(dbrow[56], httpitem->allow_traps))
			return FAIL;

		if (FAIL == dbsync_compare_str(dbrow[15], httpitem->trapper_hosts))
			return FAIL;
	}
	else if (NULL != httpitem)
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_item_preproc_row                                          *
 *                                                                            *
 * Purpose: applies necessary preprocessing before row is compared/used       *
 *                                                                            *
 * Parameter: row - [IN] the row to preprocess                                *
/******************************************************************************
 * *
 *这个代码块主要目的是处理物品数据，包括以下几个步骤：
 *
 *1. 查询物品表中的数据。
 *2. 预处理物品数据，包括展开用户宏等操作。
 *3. 初始化配置缓存，用于存储物品数据。
 *4. 遍历查询结果，比较物品数据，并根据不同的标志位进行添加、更新或删除操作。
 *5. 释放查询结果，销毁配置缓存。
 *
 *整个函数的输出是一个处理后的变化集（changeset），其中包括需要添加、更新或删除的物品数据。
 ******************************************************************************/
// 定义一个函数，用于检查行是否符合条件
int dbsync_check_row_macros(const DB_ROW *row, int flag)
{
    // 判断行中的标志位是否包含了给定的flag
    if (0 == (row->flags & flag))
        return FAIL;

    return SUCCEED;
}

// 以下是一个处理物品数据的函数
int dbsync_item_preproc_row(zbx_dbsync_t *sync, const DB_ROW *row)
{
    // 定义一些变量
    unsigned int flags = 0;
    zbx_uint64_t hostid = 0;

    // 检查行中的标志位，根据不同的标志位进行相应的处理
    if (SUCCEED == dbsync_check_row_macros(row, 14))
        flags |= ZBX_DBSYNC_ITEM_COLUMN_DELAY;

    if (SUCCEED == dbsync_check_row_macros(row, 31))
        flags |= ZBX_DBSYNC_ITEM_COLUMN_HISTORY;

    if (SUCCEED == dbsync_check_row_macros(row, 32))
        flags |= ZBX_DBSYNC_ITEM_COLUMN_TRENDS;

    // 如果 flags 为0，说明没有需要处理的标志位，直接返回行数据
    if (0 == flags)
        return row;

    // 获取关联的主机ID
    ZBX_STR2UINT64(hostid, row[1]);

    // 展开用户宏
    if (0 != (flags & ZBX_DBSYNC_ITEM_COLUMN_DELAY))
        row[14] = zbx_dc_expand_user_macros(row[14], &hostid, 1, NULL);

    if (0 != (flags & ZBX_DBSYNC_ITEM_COLUMN_HISTORY))
        row[31] = zbx_dc_expand_user_macros(row[31], &hostid, 1, NULL);

    if (0 != (flags & ZBX_DBSYNC_ITEM_COLUMN_TRENDS))
        row[32] = zbx_dc_expand_user_macros(row[32], &hostid, 1, NULL);

    // 返回处理后的行数据
    return row;
}

// 定义一个比较物品的函数
int dbsync_compare_item(ZBX_DC_ITEM *item, const DB_ROW *row)
{
    // 对比物品的各个属性，如果存在差异，则返回FAIL，否则返回SUCCEED
    // 此处省略了对比各个属性的具体逻辑

    return SUCCEED;
}

// 以下是一个同步物品数据的功能
int zbx_dbsync_compare_items(zbx_dbsync_t *sync)
{
    // 获取数据库连接
    DB_ROW dbrow;
    DB_RESULT result;

    // 查询物品表中的数据
    if (NULL == (result = DBselect(
            "select i.itemid,i.hostid,i.status,i.type,i.value_type,i.key_,"
                "i.snmp_community,i.snmp_oid,i.port,i.snmpv3_securityname,i.snmpv3_securitylevel,"
                "i.snmpv3_authpassphrase,i.snmpv3_privpassphrase,i.ipmi_sensor,i.delay,"
                "i.trapper_hosts,i.logtimefmt,i.params,i.state,i.authtype,i.username,i.password,"
                "i.publickey,i.privatekey,i.flags,i.interfaceid,i.snmpv3_authprotocol,"
                "i.snmpv3_privprotocol,i.snmpv3_contextname,i.lastlogsize,i.mtime,"
                "i.history,i.trends,i.inventory_link,i.valuemapid,i.units,i.error,i.jmx_endpoint,"
                "i.master_itemid,i.timeout,i.url,i.query_fields,i.posts,i.status_codes,"
                "i.follow_redirects,i.post_type,i.http_proxy,i.headers,i.retrieve_mode,"
                "i.request_method,i.output_format,i.ssl_cert_file,i.ssl_key_file,i.ssl_key_password,"
                "i.verify_peer,i.verify_host,i.allow_traps"
            " from items i,hosts h"
            " where i.hostid=h.hostid"
            " and h.status in (%d,%d)"
            " and i.flags<>%d",
            HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
            ZBX_FLAG_DISCOVERY_PROTOTYPE)))
    {
        return FAIL;
    }

    // 预处理物品数据
    dbsync_prepare(sync, 57, dbsync_item_preproc_row);

    // 初始化配置缓存
    zbx_hashset_create(&ids, dbsync_env.cache->items.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
                     ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 遍历查询结果，比较物品数据
    while (NULL != (dbrow = DBfetch(result)))
    {
        unsigned char tag = ZBX_DBSYNC_ROW_NONE;

        ZBX_STR2UINT64(rowid, dbrow[0]);
        zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

        row = dbsync_preproc_row(sync, dbrow);

        // 检查行中的标志位，根据不同的标志位进行相应的处理
        if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&dbsync_env.cache->items, &rowid)))
            tag = ZBX_DBSYNC_ROW_ADD;
        else if (FAIL == dbsync_compare_item(item, row))
            tag = ZBX_DBSYNC_ROW_UPDATE;

        if (ZBX_DBSYNC_ROW_NONE != tag)
            dbsync_add_row(sync, rowid, tag, row);
    }

    // 释放查询结果
    zbx_hashset_iter_reset(&dbsync_env.cache->items, &iter);
    while (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_iter_next(&iter)))
    {
        // 检查配置缓存中是否存在物品
        if (NULL == zbx_hashset_search(&ids, &item->itemid))
            dbsync_add_row(sync, item->itemid, ZBX_DBSYNC_ROW_REMOVE, NULL);
    }

    // 销毁配置缓存
    zbx_hashset_destroy(&ids);
    DBfree_result(result);

    // 返回同步结果
    return SUCCEED;
}


		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, row);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->items, &iter);
	while (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &item->itemid))
			dbsync_add_row(sync, item->itemid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_trigger                                           *
 *                                                                            *
 * Purpose: compares triggers table row with cached configuration data        *
 *                                                                            *
 * Parameter: trigger - [IN] the cached trigger                               *
 *            row     - [IN] the database row                                 *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较一个 DB_ROW 类型的指针（从数据库中读取的一条记录）与一个 ZBX_DC_TRIGGER 类型的指针（表示一个触发器）的各个字段是否相等。如果所有字段都相等，返回 SUCCEED，否则返回 FAIL。这个函数用于检查触发器与数据库中的记录是否匹配，以便在数据同步过程中比较和更新触发器。
 ******************************************************************************/
// 定义一个名为 dbsync_compare_trigger 的静态函数，该函数接收两个参数，一个是 ZBX_DC_TRIGGER 类型的指针，另一个是 DB_ROW 类型的指针
static int	dbsync_compare_trigger(const ZBX_DC_TRIGGER *trigger, const DB_ROW dbrow)
{
    // 首先，检查 dbrow[1] 字符串与 trigger->description 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_str(dbrow[1], trigger->description))
        return FAIL;

    // 接着，检查 dbrow[2] 字符串与 trigger->expression 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_str(dbrow[2], trigger->expression))
        return FAIL;

    // 然后，检查 dbrow[4] 字符串与 trigger->priority 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_uchar(dbrow[4], trigger->priority))
        return FAIL;

    // 接下来，检查 dbrow[5] 字符串与 trigger->type 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_uchar(dbrow[5], trigger->type))
        return FAIL;

    // 然后，检查 dbrow[9] 字符串与 trigger->status 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_uchar(dbrow[9], trigger->status))
        return FAIL;

    // 接着，检查 dbrow[10] 字符串与 trigger->recovery_mode 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_uchar(dbrow[10], trigger->recovery_mode))
        return FAIL;

    // 然后，检查 dbrow[11] 字符串与 trigger->recovery_expression 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_str(dbrow[11], trigger->recovery_expression))
        return FAIL;

    // 接下来，检查 dbrow[12] 字符串与 trigger->correlation_mode 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_uchar(dbrow[12], trigger->correlation_mode))
        return FAIL;

    // 然后，检查 dbrow[13] 字符串与 trigger->correlation_tag 是否相等，如果不相等，返回 FAIL
    if (FAIL == dbsync_compare_str(dbrow[13], trigger->correlation_tag))
        return FAIL;

    // 经过以上一系列的检查，如果所有条件都满足，返回 SUCCEED
    return SUCCEED;
}


#define ZBX_DBSYNC_TRIGGER_COLUMN_EXPRESSION		0x01
#define ZBX_DBSYNC_TRIGGER_COLUMN_RECOVERY_EXPRESSION	0x02

/******************************************************************************
 *                                                                            *
 * Function: dbsync_trigger_preproc_row                                       *
 *                                                                            *
 * Purpose: applies necessary preprocessing before row is compared/used       *
 *                                                                            *
 * Parameter: row - [IN] the row to preprocess                                *
 *                                                                            *
 * Return value: the preprocessed row                                         *
 *                                                                            *
 * Comments: The row preprocessing can be used to expand user macros in       *
 *           some columns.                                                    *
 *                                                                            *
 ******************************************************************************/
static char	**dbsync_trigger_preproc_row(char **row)
{
	zbx_vector_uint64_t	hostids, functionids;
	unsigned char		flags = 0;

	/* return the original row if user macros are not used in target columns */
/******************************************************************************
 * *
 *整个代码块的主要目的是检查row中的数据，根据条件设置flags，然后获取相关的hostids，扩展user macro，最后返回处理后的row。具体来说：
 *
 *1. 判断第3列的值是否符合macro1的条件，如果符合，设置flags为ZBX_DBSYNC_TRIGGER_COLUMN_EXPRESSION。
 *2. 判断第11列的值是否符合macro2的条件，如果符合，设置flags为ZBX_DBSYNC_TRIGGER_COLUMN_RECOVERY_EXPRESSION。
 *3. 如果flags为0，说明没有触发条件，直接返回row。
 *4. 获取相关的hostids，创建两个uint64类型的vector（hostids和functionids）用于存储数据。
 *5. 获取row[2]和row[11]对应的functionids。
 *6. 根据functionids获取hostids。
 *7. 扩展row[2]和row[11]的user macro。
 *8. 销毁functionids和hostids的vector。
 *9. 处理完所有逻辑后，返回处理后的row。
 ******************************************************************************/
// 判断第3列的值是否符合macro1的条件，如果符合，将flags设置为ZBX_DBSYNC_TRIGGER_COLUMN_EXPRESSION
if (SUCCEED == dbsync_check_row_macros(row, 2))
{
    flags |= ZBX_DBSYNC_TRIGGER_COLUMN_EXPRESSION;
}

// 判断第11列的值是否符合macro2的条件，如果符合，将flags设置为ZBX_DBSYNC_TRIGGER_COLUMN_RECOVERY_EXPRESSION
if (SUCCEED == dbsync_check_row_macros(row, 11))
{
    flags |= ZBX_DBSYNC_TRIGGER_COLUMN_RECOVERY_EXPRESSION;
}

// 如果flags为0，说明没有触发条件，直接返回row
if (0 == flags)
{
    return row;
}

/* 获取相关的host标识符 */

// 创建一个uint64类型的vector，用于存储hostids
zbx_vector_uint64_create(&hostids);

/******************************************************************************
 * *
 *该代码的主要目的是比较zbx监控系统中的触发器，并在需要时更新或删除触发器。具体来说，代码执行以下操作：
 *
 *1. 从数据库中查询触发器信息，包括触发器ID、描述、表达式、错误、优先级、类型、值、状态、上次更改时间、状态、恢复模式、恢复表达式、关联模式和关联标签。
 *2. 对查询结果进行预处理。
 *3. 初始化数据缓存。
 *4. 遍历查询结果，并将触发器添加到ids哈希集中。
 *5. 查找ids哈希集中的触发器，并与当前触发器进行对比。如果发现差异，则更新或添加触发器。
 *6. 迭代触发器缓存，并处理差异。如果触发器不在ids哈希集中，则将其从缓存中删除。
 *7. 销毁ids哈希集并释放查询结果。
 *8. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_triggers(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	ZBX_DC_TRIGGER		*trigger;
	char			**row;

	// 查询触发器信息
	if (NULL == (result = DBselect(
			"select distinct t.triggerid,t.description,t.expression,t.error,t.priority,t.type,t.value,"
				"t.state,t.lastchange,t.status,t.recovery_mode,t.recovery_expression,"
				"t.correlation_mode,t.correlation_tag"
			" from hosts h,items i,functions f,triggers t"
			" where h.hostid=i.hostid"
				" and i.itemid=f.itemid"
				" and f.triggerid=t.triggerid"
				" and h.status in (%d,%d)"
				" and t.flags<>%d",
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
			ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		return FAIL;
	}

	// 预处理数据
	dbsync_prepare(sync, 14, dbsync_trigger_preproc_row);

	// 初始化数据缓存
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建哈希集
	zbx_hashset_create(&ids, dbsync_env.cache->triggers.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果并处理触发器
	while (NULL != (dbrow = DBfetch(result)))
	{
		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		row = dbsync_preproc_row(sync, dbrow);

		// 查找触发器并对比差异
		if (NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&dbsync_env.cache->triggers, &rowid)))
		{
			dbsync_add_row(sync, rowid, ZBX_DBSYNC_ROW_ADD, row);
		}
		else
		{
			if (FAIL == dbsync_compare_trigger(trigger, row))
				dbsync_add_row(sync, rowid, ZBX_DBSYNC_ROW_UPDATE, row);
		}
	}

	// 迭代触发器缓存并处理差异
	zbx_hashset_iter_reset(&dbsync_env.cache->triggers, &iter);
	while (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
	{
		// 检查触发器是否在ids哈希集中
		if (NULL == zbx_hashset_search(&ids, &trigger->triggerid))
			dbsync_add_row(sync, trigger->triggerid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁ids哈希集
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}

			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
			ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		return FAIL;
	}

	dbsync_prepare(sync, 14, dbsync_trigger_preproc_row);

	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

/******************************************************************************
 * *
 *整个代码块的主要目的是比较zbx_dbsync_compare_trigger_dependency函数接收到的两个触发器依赖关系集合，并在新的集合中添加符合条件的依赖关系，同时删除不再需要的依赖关系。为了实现这个目的，代码首先从数据库中查询触发器依赖关系，然后创建一个依赖关系集合，接着遍历上游和下游依赖关系，将符合条件的依赖关系添加到集合中，最后添加删除行并释放资源。
 ******************************************************************************/
int zbx_dbsync_compare_trigger_dependency(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		deps; // 依赖关系集合
	zbx_hashset_iter_t	iter;
	ZBX_DC_TRIGGER_DEPLIST	*dep_down, *dep_up; // 指向触发器依赖关系链表的指针
	zbx_uint64_pair_t	*dep, dep_local; // 存储依赖关系的数据结构
	char			down_s[MAX_ID_LEN + 1], up_s[MAX_ID_LEN + 1]; // 存储触发器ID的字符串
	char			*del_row[2] = {down_s, up_s}; // 用于存储删除行的字符串指针数组
	int			i;

	// 从数据库中查询触发器依赖关系
	if (NULL == (result = DBselect(
			"select distinct d.triggerid_down,d.triggerid_up"
			" from trigger_depends d,triggers t,hosts h,items i,functions f"
			" where t.triggerid=d.triggerid_down"
				" and t.flags<>%d"
				" and h.hostid=i.hostid"
				" and i.itemid=f.itemid"
				" and f.triggerid=d.triggerid_down"
				" and h.status in (%d,%d)",
				ZBX_FLAG_DISCOVERY_PROTOTYPE, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED)))
	{
		return FAIL;
	}

	// 初始化数据结构
	dbsync_prepare(sync, 2, NULL);

	// 如果是初始化模式，直接返回成功
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建依赖关系集合
	zbx_hashset_create(&deps, 100, ZBX_DEFAULT_UINT64_PAIR_HASH_FUNC, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);

	// 索引主机模板链接
	zbx_hashset_iter_reset(&dbsync_env.cache->trigdeps, &iter);
	while (NULL != (dep_down = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_iter_next(&iter)))
	{
		dep_local.first = dep_down->triggerid;

		// 遍历下游依赖关系
		for (i = 0; i < dep_down->dependencies.values_num; i++)
		{
			dep_up = (ZBX_DC_TRIGGER_DEPLIST *)dep_down->dependencies.values[i];
			dep_local.second = dep_up->triggerid;
			zbx_hashset_insert(&deps, &dep_local, sizeof(dep_local));
		}
	}

	// 添加新行，删除索引中的现有行
	while (NULL != (dbrow = DBfetch(result)))
	{
		ZBX_STR2UINT64(dep_local.first, dbrow[0]);
		ZBX_STR2UINT64(dep_local.second, dbrow[1]);

		// 如果在依赖关系集合中不存在该依赖关系，则添加为新行
		if (NULL == (dep = (zbx_uint64_pair_t *)zbx_hashset_search(&deps, &dep_local)))
			dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_ADD, dbrow);
		else
			// 否则，从依赖关系集合中移除该行
			zbx_hashset_remove_direct(&deps, dep);
	}

	// 添加删除行
	zbx_hashset_iter_reset(&deps, &iter);
	while (NULL != (dep = (zbx_uint64_pair_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_snprintf(down_s, sizeof(down_s), ZBX_FS_UI64, dep->first);
		zbx_snprintf(up_s, sizeof(up_s), ZBX_FS_UI64, dep->second);
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	// 释放资源
	DBfree_result(result);
	zbx_hashset_destroy(&deps);

	return SUCCEED;
}

	{
		sync->dbresult = result;
		return SUCCEED;
	}

	zbx_hashset_create(&deps, 100, ZBX_DEFAULT_UINT64_PAIR_HASH_FUNC, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);

	/* index all host->template links */
	zbx_hashset_iter_reset(&dbsync_env.cache->trigdeps, &iter);
	while (NULL != (dep_down = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_iter_next(&iter)))
	{
		dep_local.first = dep_down->triggerid;

		for (i = 0; i < dep_down->dependencies.values_num; i++)
		{
			dep_up = (ZBX_DC_TRIGGER_DEPLIST *)dep_down->dependencies.values[i];
			dep_local.second = dep_up->triggerid;
			zbx_hashset_insert(&deps, &dep_local, sizeof(dep_local));
		}
	}

	/* add new rows, remove existing rows from index */
	while (NULL != (dbrow = DBfetch(result)))
	{
		ZBX_STR2UINT64(dep_local.first, dbrow[0]);
		ZBX_STR2UINT64(dep_local.second, dbrow[1]);

		if (NULL == (dep = (zbx_uint64_pair_t *)zbx_hashset_search(&deps, &dep_local)))
			dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_ADD, dbrow);
		else
			zbx_hashset_remove_direct(&deps, dep);
	}

	/* add removed rows */
	zbx_hashset_iter_reset(&deps, &iter);
	while (NULL != (dep = (zbx_uint64_pair_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_snprintf(down_s, sizeof(down_s), ZBX_FS_UI64, dep->first);
		zbx_snprintf(up_s, sizeof(up_s), ZBX_FS_UI64, dep->second);
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	DBfree_result(result);
	zbx_hashset_destroy(&deps);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_function                                          *
 *                                                                            *
 * Purpose: compares functions table row with cached configuration data       *
 *                                                                            *
 * Parameter: function - [IN] the cached function                             *
 *            row      - [IN] the database row                                *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较一个 ZBX_DC_FUNCTION 结构体和一个 DB_ROW 结构体中的相应字段是否相等。如果所有字段都相等，函数返回 SUCCEED，否则返回 FAIL。这里使用了四个 dbsync_compare_* 函数来分别比较各个字段。
 ******************************************************************************/
// 定义一个名为 dbsync_compare_function 的静态函数，参数分别为 ZBX_DC_FUNCTION 类型的指针和一个 DB_ROW 类型的指针
static int	dbsync_compare_function(const ZBX_DC_FUNCTION *function, const DB_ROW dbrow)
{
    // 判断 dbrow[0] 是否等于 function->itemid，如果不等于，返回 FAIL
    if (FAIL == dbsync_compare_uint64(dbrow[0], function->itemid))
        return FAIL;

    // 判断 dbrow[4] 是否等于 function->triggerid，如果不等于，返回 FAIL
    if (FAIL == dbsync_compare_uint64(dbrow[4], function->triggerid))
        return FAIL;

    // 判断 dbrow[2] 是否等于 function->function，如果不等于，返回 FAIL
    if (FAIL == dbsync_compare_str(dbrow[2], function->function))
        return FAIL;

    // 判断 dbrow[3] 是否等于 function->parameter，如果不等于，返回 FAIL
    if (FAIL == dbsync_compare_str(dbrow[3], function->parameter))
        return FAIL;

    // 如果以上所有条件都满足，返回 SUCCEED
    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *该代码主要目的是比较数据库中存储的函数和缓存中的函数，根据需要进行添加、更新或删除操作。具体来说，代码实现了以下功能：
 *
 *1. 从数据库中查询符合条件的函数信息。
 *2. 初始化数据同步对象。
 *3. 遍历查询结果，比较函数ID和缓存中的函数ID，根据需要进行添加、更新或删除操作。
 *4. 释放查询结果，销毁哈希集。
 *5. 返回成功。
 ******************************************************************************/
// 定义一个函数，用于比较数据库中存储的函数和缓存中的函数，并根据需要进行添加、更新或删除操作
int zbx_dbsync_compare_functions(zbx_dbsync_t *sync)
{
	// 声明变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	ZBX_DC_FUNCTION		*function;

	// 从数据库中查询符合条件的函数信息
	if (NULL == (result = DBselect(
			"select i.itemid,f.functionid,f.name,f.parameter,t.triggerid"
			" from hosts h,items i,functions f,triggers t"
			" where h.hostid=i.hostid"
				" and i.itemid=f.itemid"
				" and f.triggerid=t.triggerid"
				" and h.status in (%d,%d)"
				" and t.flags<>%d",
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
			ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		// 查询失败，返回错误
		return FAIL;
	}

	// 初始化数据同步对象
	dbsync_prepare(sync, 5, NULL);

	// 如果数据同步模式为初始化
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		// 将查询结果赋值给数据同步对象
		sync->dbresult = result;
		// 返回成功
		return SUCCEED;
	}

	// 创建一个哈希集，用于存储函数ID
	zbx_hashset_create(&ids, dbsync_env.cache->functions.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果，比较函数ID和缓存中的函数ID
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将数据库中的rowid转换为zbx_uint64类型
		ZBX_STR2UINT64(rowid, dbrow[1]);
		// 将rowid插入到哈希集中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找缓存中的函数
		if (NULL == (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&dbsync_env.cache->functions, &rowid)))
			// 标记为新增函数
			tag = ZBX_DBSYNC_ROW_ADD;
		else if (FAIL == dbsync_compare_function(function, dbrow))
			// 标记为更新函数
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果标记不为空，将操作记录添加到数据同步对象中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->functions, &iter);
	// 遍历缓存中的函数，检查是否需要在数据库中删除
	while (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_iter_next(&iter)))
	{
		// 如果哈希集中不存在该函数ID，则在数据库中删除该函数
		if (NULL == zbx_hashset_search(&ids, &function->functionid))
			dbsync_add_row(sync, function->functionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁哈希集
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}

	while (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &function->functionid))
			dbsync_add_row(sync, function->functionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *这块代码的主要目的是比较数据库中的一行数据与给定的表达式是否匹配。函数`dbsync_compare_expression`接收两个参数，分别是表达式指针`expression`和数据库中的一行数据`dbrow`。通过逐个比较`dbrow`中的字段与表达式中的相应字段，判断数据是否匹配。如果所有字段都匹配成功，函数返回`SUCCEED`，表示表达式与数据库数据匹配；否则，返回`FAIL`，表示表达式与数据库数据不匹配。
 ******************************************************************************/
// 定义一个函数，用于比较数据库中的一行数据与给定的表达式是否匹配
static int	dbsync_compare_expression(const ZBX_DC_EXPRESSION *expression, const DB_ROW dbrow)
{
	// 判断数据库中的一行数据的第一个字段（dbrow[0]）是否与表达式的正则表达式（expression->regexp）匹配
	if (FAIL == dbsync_compare_str(dbrow[0], expression->regexp))
		// 如果匹配失败，返回FAIL
		return FAIL;

	// 判断数据库中的一行数据的第三个字段（dbrow[2]）是否与表达式的字符串（expression->expression）匹配
	if (FAIL == dbsync_compare_str(dbrow[2], expression->expression))
		// 如果匹配失败，返回FAIL
		return FAIL;

	// 判断数据库中的一行数据的第四个字段（dbrow[3]）是否与表达式的类型（expression->type）匹配
	if (FAIL == dbsync_compare_uchar(dbrow[3], expression->type))
		// 如果匹配失败，返回FAIL
		return FAIL;

	// 判断数据库中的一行数据的第五个字段（dbrow[4]）是否与表达式的分隔符（expression->delimiter）匹配
	if (*dbrow[4] != expression->delimiter)
		// 如果匹配失败，返回FAIL
		return FAIL;

	// 判断数据库中的一行数据的第六个字段（dbrow[5]）是否与表达式的是否区分大小写（expression->case_sensitive）匹配
	if (FAIL == dbsync_compare_uchar(dbrow[5], expression->case_sensitive))
		// 如果匹配失败，返回FAIL
		return FAIL;

	// 如果所有字段都匹配成功，返回SUCCEED
	return SUCCEED;
/******************************************************************************
 * *
 *这段代码的主要目的是比较两个数据库中的表达式信息，并根据需要进行添加、更新和删除操作。具体来说，代码的功能如下：
 *
 *1. 从数据库中查询表达式信息，并存储在result变量中。
 *2. 初始化dbsync对象。
 *3. 如果dbsync模式为初始化，则将查询结果赋值给dbresult，并返回成功。
 *4. 创建一个表达式ID集合（ids）。
 *5. 遍历查询结果，比较每个表达式与缓存中的表达式，并根据需要进行添加、更新和删除操作。
 *6. 重置表达式迭代器，遍历缓存中的表达式。
 *7. 如果表达式ID不存在于表达式ID集合中，则将其添加到dbsync中。
 *8. 销毁表达式ID集合。
 *9. 释放查询结果。
 *10. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_expressions(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 表达式ID集合
	zbx_hashset_iter_t	iter; // 迭代器
	zbx_uint64_t		rowid; // 行ID
	ZBX_DC_EXPRESSION	*expression; // 表达式结构体指针

	// 从数据库中查询表达式信息
	if (NULL == (result = DBselect(
			"select r.name,e.expressionid,e.expression,e.expression_type,e.exp_delimiter,e.case_sensitive"
			" from regexps r,expressions e"
			" where r.regexpid=e.regexpid")))
	{
		// 查询失败，返回错误
		return FAIL;
	}

	// 初始化dbsync
	dbsync_prepare(sync, 6, NULL);

	// 如果dbsync模式为初始化
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		// 将查询结果赋值给dbresult
		sync->dbresult = result;
		// 返回成功
		return SUCCEED;
	}

	// 创建表达式ID集合
	zbx_hashset_create(&ids, dbsync_env.cache->expressions.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果
	while (NULL != (dbrow = DBfetch(result)))
	{
		// 将行ID转换为unsigned char类型
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将行ID转换为uint64类型
		ZBX_STR2UINT64(rowid, dbrow[1]);
		// 将行ID添加到表达式ID集合中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 如果未找到该表达式，则添加到集合中
		if (NULL == (expression = (ZBX_DC_EXPRESSION *)zbx_hashset_search(&dbsync_env.cache->expressions,
				&rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		// 比较表达式，若不一致则更新
		else if (FAIL == dbsync_compare_expression(expression, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果tag不为空，则将行添加到dbsync中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置dbsync环境中的表达式迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->expressions, &iter);
	// 遍历集合中的表达式
	while (NULL != (expression = (ZBX_DC_EXPRESSION *)zbx_hashset_iter_next(&iter)))
	{
		// 判断表达式ID是否存在于ids集合中，如果不存在，则将其添加到dbsync中
		if (NULL == zbx_hashset_search(&ids, &expression->expressionid))
			dbsync_add_row(sync, expression->expressionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁表达式ID集合
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}


		ZBX_STR2UINT64(rowid, dbrow[1]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		if (NULL == (expression = (ZBX_DC_EXPRESSION *)zbx_hashset_search(&dbsync_env.cache->expressions,
				&rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_expression(expression, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->expressions, &iter);
	while (NULL != (expression = (ZBX_DC_EXPRESSION *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &expression->expressionid))
			dbsync_add_row(sync, expression->expressionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_action                                            *
 *                                                                            *
 * Purpose: compares actions table row with cached configuration data         *
 *                                                                            *
 * Parameter: action - [IN] the cached action                                 *
 *            row    - [IN] the database row                                  *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_dc_action结构体对象（ action 和 dbrow）的内容是否一致。函数dbsync_compare_action接收两个参数，分别是zbx_dc_action_t类型的指针action和DB_ROW类型的指针dbrow。函数内部依次比较dbrow的四个元素（索引分别为1、2、3）与action的相应成员（eventsource、evaltype、formula）是否相等。如果有一个不相等，函数返回FAIL，表示比较失败。如果所有条件都满足，函数返回SUCCEED，表示比较成功。
 ******************************************************************************/
// 定义一个名为 dbsync_compare_action 的静态函数，该函数接收两个参数，一个是 zbx_dc_action_t 类型的指针 action，另一个是 DB_ROW 类型的指针 dbrow
static int	dbsync_compare_action(const zbx_dc_action_t *action, const DB_ROW dbrow)
{
	// 首先，检查 dbrow[1] 是否等于 action->eventsource，如果不相等，返回 FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[1], action->eventsource))
		return FAIL;

	// 接着，检查 dbrow[2] 是否等于 action->evaltype，如果不相等，返回 FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[2], action->evaltype))
		return FAIL;

	// 然后，检查 dbrow[3] 是否等于 action->formula，如果不相等，返回 FAIL
	if (FAIL == dbsync_compare_str(dbrow[3], action->formula))
		return FAIL;

	// 如果以上三个条件都满足，返回 SUCCEED，表示比较成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_actions                                       *
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的action与缓存中的action，并将差异记录到结果集中。具体来说，代码完成了以下操作：
 *
 *1. 查询数据库，获取状态为ACTIVE的action。
 *2. 初始化dbsync环境。
 *3. 遍历数据库查询结果，将action添加到结果集中。
 *4. 遍历缓存的action，将与数据库中的action不同的action添加到结果集中。
 *5. 销毁ids哈希集。
 *6. 释放数据库查询结果。
 *7. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_actions(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	zbx_dc_action_t		*action;

	// 查询数据库，获取状态为ACTIVE的action
	if (NULL == (result = DBselect(
			"select actionid,eventsource,evaltype,formula"
			" from actions"
			" where status=%d",
			ACTION_STATUS_ACTIVE)))
	{
		// 查询失败，返回FAIL
		return FAIL;
	}

	// 初始化dbsync环境
	dbsync_prepare(sync, 4, NULL);

	// 如果dbsync模式为初始化，将查询结果赋值给dbresult，并返回成功
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个哈希集，用于存储action id
	zbx_hashset_create(&ids, dbsync_env.cache->actions.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历数据库查询结果
	while (NULL != (dbrow = DBfetch(result)))
	{
		// 将dbrow中的数据转换为zbx_dc_action_t结构体
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查询缓存中是否存在该action
		if (NULL == (action = (zbx_dc_action_t *)zbx_hashset_search(&dbsync_env.cache->actions, &rowid)))
			// 如果不存在，标记为新增action
			tag = ZBX_DBSYNC_ROW_ADD;
		else if (FAIL == dbsync_compare_action(action, dbrow))
			// 如果不相同，标记为更新action
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果tag不为空，将action添加到结果集中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->actions, &iter);
	// 遍历缓存的action
	while (NULL != (action = (zbx_dc_action_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果ids哈希集中不存在该action，将其标记为删除
		if (NULL == zbx_hashset_search(&ids, &action->actionid))
			dbsync_add_row(sync, action->actionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁ids哈希集
	zbx_hashset_destroy(&ids);
	// 释放数据库查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}

		if (NULL == zbx_hashset_search(&ids, &action->actionid))
			dbsync_add_row(sync, action->actionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_action_op                                         *
 *                                                                            *
 * Purpose: compares action operation class and flushes update row if         *
 *          necessary                                                         *
 *                                                                            *
 * Parameter: sync     - [OUT] the changeset                                  *
 *            actionid - [IN] the action identifier                           *
 *            opflags  - [IN] the action operation class flags                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理数据库同步中的比较操作。当传入的actionid和opflags与已存在的动作信息不同时，将更新动作信息并添加到同步数据表中。
 ******************************************************************************/
/* 定义一个静态函数，用于处理数据库同步中的比较操作 */
static void dbsync_compare_action_op(zbx_dbsync_t *sync, zbx_uint64_t actionid, unsigned char opflags)
{
    /* 定义一个指向zbx_dc_action_t类型的指针，用于存储查询到的动作信息 */
    zbx_dc_action_t *action;

    /* 如果actionid为0，直接返回，表示不需要执行比较操作 */
    if (0 == actionid)
        return;

    /* 查询的动作信息，通过zbx_hashset_search函数在dbsync_env.cache->actions散列表中查找 */
    if (NULL == (action = (zbx_dc_action_t *)zbx_hashset_search(&dbsync_env.cache->actions, &actionid)) ||
        /* 判断opflags是否与查询到的动作信息的opflags相同，如果不相同，说明需要更新动作信息 */
        opflags != action->opflags)
    {
        /* 格式化actionid和opflags的字符串，便于打印日志和报警 */
        char actionid_s[MAX_ID_LEN], opflags_s[MAX_ID_LEN];
        char *row[] = {actionid_s, opflags_s};

        /* 将actionid和opflags转换为字符串，并存储在row数组中 */
        zbx_snprintf(actionid_s, sizeof(actionid_s), ZBX_FS_UI64, actionid);
        zbx_snprintf(opflags_s, sizeof(opflags_s), "%d", opflags);

        /* 向同步数据表中添加一行数据，表示需要更新动作信息 */
        dbsync_add_row(sync, actionid, ZBX_DBSYNC_ROW_UPDATE, (DB_ROW)row);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_action_ops                                    *
 *                                                                            *
 * Purpose: compares actions by operation class                               *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询所有状态为活跃的操作，并根据操作类型设置相应的操作标志。具体流程如下：
 *
 *1. 查询数据库，获取所有状态为活跃的操作，并将查询结果存储在result变量中。
 *2. 预处理数据同步，准备执行操作。
 *3. 遍历查询结果，对于每个操作：
 *   a. 比较动作操作。
 *   b. 更新actionid。
 *   c. 重置opflags为ZBX_ACTION_OPCLASS_NONE。
 *4. 根据操作类型设置opflags。
 *5. 最后再次比较动作操作。
 *6. 释放数据库查询结果。
 *7. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_action_ops(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_uint64_t		rowid, actionid = 0;
	unsigned char		opflags = ZBX_ACTION_OPCLASS_NONE;

	// 查询数据库，获取所有状态为活跃的操作
	if (NULL == (result = DBselect(
			"select a.actionid,o.recovery"
			" from actions a"
			" left join operations o"
				" on a.actionid=o.actionid"
			" where a.status=%d"
			" group by a.actionid,o.recovery"
			" order by a.actionid",
			ACTION_STATUS_ACTIVE)))
	{
		// 如果查询失败，返回FAIL
		return FAIL;
	}

	// 预处理数据同步，准备执行操作
	dbsync_prepare(sync, 2, NULL);

	// 遍历数据库查询结果
	while (NULL != (dbrow = DBfetch(result)))
	{
		// 将字符串转换为无符号整数
		ZBX_STR2UINT64(rowid, dbrow[0]);

		// 如果actionid与rowid不相等，说明找到了新的操作
		if (actionid != rowid)
		{
			// 比较动作操作
			dbsync_compare_action_op(sync, actionid, opflags);
			// 更新actionid
			actionid = rowid;
			// 重置opflags为ZBX_ACTION_OPCLASS_NONE
			opflags = ZBX_ACTION_OPCLASS_NONE;
		}

		// 如果dbrow[1]为空，跳过此行
		if (SUCCEED == DBis_null(dbrow[1]))
			continue;

		// 根据dbrow[1]的值设置opflags
		switch (atoi(dbrow[1]))
		{
			case 0:
				opflags |= ZBX_ACTION_OPCLASS_NORMAL;
				break;
			case 1:
				opflags |= ZBX_ACTION_OPCLASS_RECOVERY;
				break;
			case 2:
				opflags |= ZBX_ACTION_OPCLASS_ACKNOWLEDGE;
				break;
		}
	}

	// 最后再次比较动作操作
	dbsync_compare_action_op(sync, actionid, opflags);

	// 释放数据库查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}



/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_action_condition                                  *
 *                                                                            *
 * Purpose: compares conditions table row with cached configuration data      *
 *                                                                            *
 * Parameter: condition - [IN] the cached action condition                    *
 *            row       - [IN] the database row                               *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的动作条件与zbx_dc_action_condition_t结构体中的条件是否相同。如果所有条件都相同，返回SUCCEED（表示比较成功）；如果有任何一条条件不同，返回FAIL（表示比较失败）。
 ******************************************************************************/
// 定义一个静态函数，用于比较数据库中的动作条件与zbx_dc_action_condition_t结构体中的条件
static int	dbsync_compare_action_condition(const zbx_dc_action_condition_t *condition, const DB_ROW dbrow)
{
	// 判断dbrow[2]（数据库中的条件类型）与condition->conditiontype（zbx_dc_action_condition_t结构体中的条件类型）是否相同
	if (FAIL == dbsync_compare_uchar(dbrow[2], condition->conditiontype))
		// 如果不同，返回FAIL（表示比较失败）
		return FAIL;

	// 判断dbrow[3]（数据库中的操作符）与condition->op（zbx_dc_action_condition_t结构体中的操作符）是否相同
	if (FAIL == dbsync_compare_uchar(dbrow[3], condition->op))
		// 如果不同，返回FAIL（表示比较失败）
		return FAIL;

	// 判断dbrow[4]（数据库中的值）与condition->value（zbx_dc_action_condition_t结构体中的值）是否相同
	if (FAIL == dbsync_compare_str(dbrow[4], condition->value))
		// 如果不同，返回FAIL（表示比较失败）
		return FAIL;

	// 判断dbrow[5]（数据库中的值2）与condition->value2（zbx_dc_action_condition_t结构体中的值2）是否相同
	if (FAIL == dbsync_compare_str(dbrow[5], condition->value2))
		// 如果不同，返回FAIL（表示比较失败）
		return FAIL;

	// 如果以上所有比较都成功，返回SUCCEED（表示比较成功）
	return SUCCEED;
}
/******************************************************************************
 * *
 *这段代码的主要目的是比较动作条件数据库中的数据与缓存中的数据，对于新增、更新和删除的动作条件进行处理。具体来说，它会执行以下操作：
 *
 *1. 从数据库中查询符合条件的动作条件数据。
 *2. 初始化dbsync对象。
 *3. 如果dbsync模式为初始化，将查询结果赋值给dbresult，并返回成功。
 *4. 创建一个哈希集，用于存储动作条件ID。
 *5. 遍历数据库查询结果，将动作条件插入到哈希集中。
 *6. 比较哈希集中的动作条件与数据库查询结果，如果存在差异，则更新动作条件。
 *7. 遍历哈希集中的动作条件，检查是否需要在哈希集中插入、更新或删除。
 *8. 销毁哈希集并释放数据库查询结果。
 *9. 返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_action_conditions(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW				dbrow;
	DB_RESULT			result;
	zbx_hashset_t			ids;
	zbx_hashset_iter_t		iter;
	zbx_uint64_t			rowid;
	zbx_dc_action_condition_t	*condition;

	// 查询数据库，获取符合条件的动作条件数据
	if (NULL == (result = DBselect(
			"select c.conditionid,c.actionid,c.conditiontype,c.operator,c.value,c.value2"
			" from conditions c,actions a"
			" where c.actionid=a.actionid"
				" and a.status=%d",
			ACTION_STATUS_ACTIVE)))
	{
		// 数据库查询失败，返回失败
		return FAIL;
	}

	// 初始化dbsync
	dbsync_prepare(sync, 6, NULL);

	// 如果dbsync模式为初始化
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		// 将查询结果赋值给dbresult
		sync->dbresult = result;
		// 返回成功
		return SUCCEED;
	}

	// 创建一个哈希集，用于存储动作条件ID
	zbx_hashset_create(&ids, dbsync_env.cache->action_conditions.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历数据库查询结果
	while (NULL != (dbrow = DBfetch(result)))
	{
		// 将dbrow中的数据转换为zbx_uint64类型
		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将rowid插入到哈希集中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找哈希集中是否存在该动作条件
		if (NULL == (condition = (zbx_dc_action_condition_t *)zbx_hashset_search(
				&dbsync_env.cache->action_conditions, &rowid)))
		{
			// 如果不存在，标记为新增
			condition = (zbx_dc_action_condition_t *)zbx_hashset_add_new(
				&dbsync_env.cache->action_conditions, (void *)&rowid, sizeof(rowid));
		}
		else if (FAIL == dbsync_compare_action_condition(condition, dbrow))
		{
			// 如果比较失败，标记为更新
			condition->flags |= ZBX_DC_ACTION_CONDITION_UPDATE;
		}

		// 如果标记不为空，将数据添加到dbsync中
		if (ZBX_DBSYNC_ROW_NONE != condition->flags)
			dbsync_add_row(sync, rowid, condition->flags, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->action_conditions, &iter);
	// 遍历哈希集中的动作条件，检查是否需要在哈希集中插入
	while (NULL != (condition = (zbx_dc_action_condition_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果该动作条件在ids哈希集中不存在，标记为删除
		if (NULL == zbx_hashset_search(&ids, &condition->conditionid))
			condition->flags |= ZBX_DC_ACTION_CONDITION_DELETE;
	}

	// 销毁ids哈希集
	zbx_hashset_destroy(&ids);
	// 释放数据库查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}

			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->action_conditions, &iter);
	while (NULL != (condition = (zbx_dc_action_condition_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &condition->conditionid))
			dbsync_add_row(sync, condition->conditionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_trigger_tag                                       *
 *                                                                            *
 * Purpose: compares trigger tags table row with cached configuration data    *
 *                                                                            *
 * Parameter: tag - [IN] the cached trigger tag                               *
 *            row - [IN] the database row                                     *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个表（一个存储触发器信息，另一个存储标签信息）中的数据是否一致。具体来说，它逐个比较了触发器ID、标签名和标签值这三个字段，如果所有字段都相等，则返回 SUCCEED，表示比较成功；否则，返回 FAIL。这个函数用于判断数据是否一致，可以在数据同步或其他相关场景中使用。
 ******************************************************************************/
// 定义一个名为 dbsync_compare_trigger_tag 的静态函数，该函数接收两个参数：
// 一个指向 zbx_dc_trigger_tag_t 结构体的指针，另一个指向 DB_ROW 结构体的指针
static int	dbsync_compare_trigger_tag(const zbx_dc_trigger_tag_t *tag, const DB_ROW dbrow)
{
	// 判断 dbrow[1]（即触发器ID）与 tag->triggerid 是否相等，如果不相等，返回 FAIL
	if (FAIL == dbsync_compare_uint64(dbrow[1], tag->triggerid))
		return FAIL;

/******************************************************************************
 * *
 *整个代码块的主要目的是对比数据库中存在的触发器标签和实际运行中的触发器标签，对于发生变更的标签，将它们添加到同步对象中，以便后续进行数据同步操作。同时，删除不再使用的触发器标签。
 ******************************************************************************/
int zbx_dbsync_compare_trigger_tags(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 用于存储触发器ID的哈希集合
	zbx_hashset_iter_t	iter; // 用于迭代哈希集合的迭代器
	zbx_uint64_t		rowid; // 用于存储行ID的变量
	zbx_dc_trigger_tag_t	*trigger_tag; // 用于存储触发器标签的指针

	// 执行数据库查询，获取符合条件的触发器标签数据
	if (NULL == (result = DBselect(
			"select distinct tt.triggertagid,tt.triggerid,tt.tag,tt.value"
			" from trigger_tag tt,triggers t,hosts h,items i,functions f"
			" where t.triggerid=tt.triggerid"
			" and t.flags<>%d"
			" and h.hostid=i.hostid"
			" and i.itemid=f.itemid"
			" and f.triggerid=tt.triggerid"
			" and h.status in (%d,%d)",
			ZBX_FLAG_DISCOVERY_PROTOTYPE, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED)))
	{
		return FAIL; // 查询失败，返回错误码
	}

	// 初始化数据同步对象
	dbsync_prepare(sync, 4, NULL);

	// 如果同步模式为初始化，则将查询结果赋值给同步对象
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED; // 初始化成功，返回正确码
	}

	// 创建一个哈希集合，用于存储触发器ID
	zbx_hashset_create(&ids, dbsync_env.cache->trigger_tags.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果，并将符合条件的触发器标签添加到哈希集合中
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 检查触发器标签是否已存在，若不存在则添加为新标签，若存在则比较标签值是否发生变更
		if (NULL == (trigger_tag = (zbx_dc_trigger_tag_t *)zbx_hashset_search(&dbsync_env.cache->trigger_tags,
				&rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_trigger_tag(trigger_tag, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果标签类型不为空，则将触发器标签添加到同步对象中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集合迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->trigger_tags, &iter);
	// 遍历哈希集合，将未在查询结果中出现的触发器标签添加到同步对象中
	while (NULL != (trigger_tag = (zbx_dc_trigger_tag_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果触发器标签在查询结果中未出现，则将其添加为删除操作
		if (NULL == zbx_hashset_search(&ids, &trigger_tag->triggertagid))
			dbsync_add_row(sync, trigger_tag->triggertagid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁哈希集合
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回成功码
	return SUCCEED;
}

		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		if (NULL == (trigger_tag = (zbx_dc_trigger_tag_t *)zbx_hashset_search(&dbsync_env.cache->trigger_tags,
				&rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_trigger_tag(trigger_tag, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->trigger_tags, &iter);
	while (NULL != (trigger_tag = (zbx_dc_trigger_tag_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &trigger_tag->triggertagid))
			dbsync_add_row(sync, trigger_tag->triggertagid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_correlation                                       *
 *                                                                            *
 * Purpose: compares correlation table row with cached configuration data     *
 *                                                                            *
 * Parameter: correlation - [IN] the cached correlation rule                  *
 *            row         - [IN] the database row                             *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较一个 DB_ROW 结构体（包含多个字符串字段）与一个 zbx_dc_correlation_t 结构体（包含多个字符串字段）的相应字段是否相等。如果所有字段都相等，则返回 SUCCEED，表示比较成功；否则返回 FAIL，表示比较失败。
 ******************************************************************************/
// 定义一个名为 dbsync_compare_correlation 的静态函数，该函数接收两个参数：
// 一个指向 zbx_dc_correlation_t 结构体的指针 correlation，
// 以及一个指向 DB_ROW 结构体的指针 dbrow。
static int	dbsync_compare_correlation(const zbx_dc_correlation_t *correlation, const DB_ROW dbrow)
{
	// 判断 dbrow[1] 字符串与 correlation->name 字符串是否相等，
	// 如果相等，则继续执行后续代码，否则返回 FAIL。
	if (FAIL == dbsync_compare_str(dbrow[1], correlation->name))
		return FAIL;

	// 判断 dbrow[2] 字符串与 correlation->evaltype 字符串是否相等，
	// 如果相等，则继续执行后续代码，否则返回 FAIL。
	if (FAIL == dbsync_compare_uchar(dbrow[2], correlation->evaltype))
		return FAIL;

	// 判断 dbrow[3] 字符串与 correlation->formula 字符串是否相等，
	// 如果相等，则继续执行后续代码，否则返回 FAIL。
	if (FAIL == dbsync_compare_str(dbrow[3], correlation->formula))
		return FAIL;

	// 如果以上三个条件都满足，则返回 SUCCEED，表示比较成功。
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_correlations                                  *
 *                                                                            *
 * Purpose: compares correlation table with cached configuration data         *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较数据库中的 correlation 与缓存中的 correlation，并将差异应用于 dbsync 结构体。具体来说，它会执行以下操作：
 *
 *1. 从数据库中查询启用状态的 correlation。
 *2. 初始化 dbsync 结构体。
 *3. 遍历查询结果，并将每个 correlation 添加到 ids hash 集合中。
 *4. 遍历 ids hash 集合中的 correlation，将它们与缓存中的 correlation 进行比较。
 *5. 如果发现差异，则将差异应用于 dbsync 结构体中的 row。
 *6. 移除 ids hash 集合中不在数据库中的 correlation。
 *7. 释放查询结果，并销毁 ids hash 集合。
 *8. 返回成功，表示整个过程顺利完成。
 ******************************************************************************/
// 定义一个函数：zbx_dbsync_compare_correlations，传入一个zbx_dbsync_t类型的指针
int zbx_dbsync_compare_correlations(zbx_dbsync_t *sync)
{
	// 声明一些变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 用于存储 correlation 的 hash 集合
	zbx_hashset_iter_t	iter; // 用于迭代 hash 集合的迭代器
	zbx_uint64_t		rowid; // 用于存储 rowid 的变量
	zbx_dc_correlation_t	*correlation; // 用于存储 correlation 结构体的指针

	// 从数据库中查询启用的 correlation
	if (NULL == (result = DBselect(
			"select correlationid,name,evaltype,formula"
			" from correlation"
			" where status=%d",
			ZBX_CORRELATION_ENABLED)))
	{
		// 如果查询失败，返回 FAIL
		return FAIL;
	}

	// 初始化 dbsync 结构体
	dbsync_prepare(sync, 4, NULL);

	// 如果 sync 的模式为 ZBX_DBSYNC_INIT，则将 dbresult 指向查询结果，并返回 SUCCEED
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个 hash 集合用于存储 correlation
	zbx_hashset_create(&ids, dbsync_env.cache->correlations.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果，并将每个 correlation 添加到 hash 集合中
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将 dbrow[0] 转换为 rowid
		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将 rowid 插入到 hash 集合中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 如果 hash 集合中不存在该 correlation，则将其添加到 hash 集合中
		if (NULL == (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&dbsync_env.cache->correlations,
				&rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		// 否则，比较 dbrow 与已存在的 correlation，如果不同，则更新 hash 集合中的 correlation
		else if (FAIL == dbsync_compare_correlation(correlation, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果 tag 不是 ZBX_DBSYNC_ROW_NONE，则将 correlation 添加到 dbsync 的 row 中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置 hash 集合的迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->correlations, &iter);
	// 遍历 hash 集合中的 correlation，如果不在 ids hash 集合中，则将其从 dbsync 中移除
	while (NULL != (correlation = (zbx_dc_correlation_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &correlation->correlationid))
			dbsync_add_row(sync, correlation->correlationid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁 ids hash 集合
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回 SUCCEED，表示整个过程成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_corr_condition                                    *
 *                                                                            *
 * Purpose: compares correlation condition tables dbrow with cached             *
 *          configuration data                                                *
 *                                                                            *
 * Parameter: corr_condition - [IN] the cached correlation condition          *
 *            row            - [IN] the database row                          *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
/******************************************************************************
 * *
 *这段代码的主要目的是比较数据库中存储的关联条件（correlation）和缓存中的关联条件，对于不同的情况执行相应的操作（添加、更新、删除），并将处理后的关联条件添加到同步数据中。整个代码块可以分为以下几个部分：
 *
 *1. 定义变量：声明了一些用于后续操作的数据结构变量，如数据库查询结果、哈希集、迭代器等。
 *
 *2. 从数据库中查询相关联的条件：使用DBselect语句查询数据库中符合条件的关联条件，并将查询结果存储在result变量中。
 *
 *3. 初始化数据同步对象：调用dbsync_prepare函数初始化数据同步对象，为后续操作做好准备。
 *
 *4. 比较缓存中的关联条件与数据库查询结果：遍历数据库查询结果，对比并处理每个关联条件。具体操作如下：
 *
 *   - 查询缓存中是否存在该关联条件；
 *   - 如果缓存中不存在，则将其添加为新的关联条件；
 *   - 如果缓存中存在，但与数据库查询结果不符，则更新缓存中的关联条件；
 *   - 如果关联条件需要删除，则将其添加到同步数据中。
 *
 *5. 释放资源：在完成所有操作后，释放ids哈希集和数据库查询结果。
 *
 *6. 返回成功：表示整个关联条件比较和处理过程顺利完成。
 ******************************************************************************/
int zbx_dbsync_compare_corr_conditions(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	zbx_dc_corr_condition_t	*corr_condition;

	// 从数据库中查询相关联的条件
	if (NULL == (result = DBselect(
			"select cc.corr_conditionid,cc.correlationid,cc.type,cct.tag,cctv.tag,cctv.value,cctv.operator,"
				" ccg.groupid,ccg.operator,cctp.oldtag,cctp.newtag"
			" from correlation c,corr_condition cc"
			" left join corr_condition_tag cct"
				" on cct.corr_conditionid=cc.corr_conditionid"
			" left join corr_condition_tagvalue cctv"
				" on cctv.corr_conditionid=cc.corr_conditionid"
			" left join corr_condition_group ccg"
				" on ccg.corr_conditionid=cc.corr_conditionid"
			" left join corr_condition_tagpair cctp"
				" on cctp.corr_conditionid=cc.corr_conditionid"
			" where c.correlationid=cc.correlationid"
				" and c.status=%d",
			ZBX_CORRELATION_ENABLED)))
	{
		return FAIL;
	}

	// 初始化数据同步对象
	dbsync_prepare(sync, 11, NULL);

	// 如果数据同步模式为初始化
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个哈希集用于存储id
	zbx_hashset_create(&ids, dbsync_env.cache->corr_conditions.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历数据库查询结果，对比并处理每个关联条件
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查询缓存中是否存在该关联条件
		if (NULL == (corr_condition = (zbx_dc_corr_condition_t *)zbx_hashset_search(
				&dbsync_env.cache->corr_conditions, &rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_corr_condition(corr_condition, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果tag不为空，则将关联条件添加到同步数据中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->corr_conditions, &iter);
	// 遍历缓存中的关联条件，检查是否在ids哈希集中
	while (NULL != (corr_condition = (zbx_dc_corr_condition_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果关联条件在ids哈希集中，则将其添加到同步数据中
		if (NULL == zbx_hashset_search(&ids, &corr_condition->corr_conditionid))
			dbsync_add_row(sync, corr_condition->corr_conditionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁ids哈希集
	zbx_hashset_destroy(&ids);
	// 释放数据库查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}

	}

	// 如果上述所有判断均通过，返回SUCCEED，表示数据库行与关联条件匹配
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_corr_conditions                               *
 *                                                                            *
 * Purpose: compares correlation condition tables with cached configuration   *
 *          data                                                              *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_dbsync_compare_corr_conditions(zbx_dbsync_t *sync)
{
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	zbx_dc_corr_condition_t	*corr_condition;

	if (NULL == (result = DBselect(
			"select cc.corr_conditionid,cc.correlationid,cc.type,cct.tag,cctv.tag,cctv.value,cctv.operator,"
				" ccg.groupid,ccg.operator,cctp.oldtag,cctp.newtag"
			" from correlation c,corr_condition cc"
			" left join corr_condition_tag cct"
				" on cct.corr_conditionid=cc.corr_conditionid"
			" left join corr_condition_tagvalue cctv"
				" on cctv.corr_conditionid=cc.corr_conditionid"
			" left join corr_condition_group ccg"
				" on ccg.corr_conditionid=cc.corr_conditionid"
			" left join corr_condition_tagpair cctp"
				" on cctp.corr_conditionid=cc.corr_conditionid"
			" where c.correlationid=cc.correlationid"
				" and c.status=%d",
			ZBX_CORRELATION_ENABLED)))
	{
		return FAIL;
	}

	dbsync_prepare(sync, 11, NULL);

	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	zbx_hashset_create(&ids, dbsync_env.cache->corr_conditions.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		if (NULL == (corr_condition = (zbx_dc_corr_condition_t *)zbx_hashset_search(
				&dbsync_env.cache->corr_conditions, &rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_corr_condition(corr_condition, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->corr_conditions, &iter);
	while (NULL != (corr_condition = (zbx_dc_corr_condition_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &corr_condition->corr_conditionid))
			dbsync_add_row(sync, corr_condition->corr_conditionid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_corr_operation                                    *
 *                                                                            *
 * Purpose: compares correlation operation tables dbrow with cached             *
 *          configuration data                                                *
 *                                                                            *
 * Parameter: corr_operation - [IN] the cached correlation operation          *
 *            row            - [IN] the database row                          *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较数据库中的数据与zbx_dc_corr_operation结构体中的数据是否一致。具体来说，它依次比较了数据库中的第2个字段（dbrow[1]）与zbx_dc_corr_operation结构体中的correlationid字段，以及数据库中的第3个字段（dbrow[2]）与zbx_dc_corr_operation结构体中的type字段。如果这两个比较都成功，即两个字段一致，函数返回SUCCEED；否则，返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于比较数据库中的数据与zbx_dc_corr_operation结构体中的数据是否一致
static int	dbsync_compare_corr_operation(const zbx_dc_corr_operation_t *corr_operation, const DB_ROW dbrow)
{
	// 判断数据库中的第2个字段（dbrow[1]）与zbx_dc_corr_operation结构体中的correlationid字段是否一致
	if (FAIL == dbsync_compare_uint64(dbrow[1], corr_operation->correlationid))
		// 如果不一致，返回FAIL
		return FAIL;

	// 判断数据库中的第3个字段（dbrow[2]）与zbx_dc_corr_operation结构体中的type字段是否一致
	if (FAIL == dbsync_compare_uchar(dbrow[2], corr_operation->type))
		// 如果不一致，返回FAIL
		return FAIL;

	// 如果上述两个比较都成功，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_corr_operations                               *
/******************************************************************************
 * *
 *这段代码的主要目的是比较两个zbx_dc_corr_operation结构体对象之间的差异，并根据差异进行添加、更新和删除操作。整个代码块可以分为以下几个部分：
 *
 *1. 初始化变量：声明了一些用于后续操作的变量，如数据库查询结果、哈希集等。
 *
 *2. 查询数据库：根据给定条件查询符合条件的zbx_dc_corr_operation对象。
 *
 *3. 初始化dbsync对象：准备进行数据同步操作。
 *
 *4. 判断同步模式：如果为初始化模式，则将查询结果赋值给dbresult，否则进行后续操作。
 *
 *5. 创建哈希集：用于存储corr_operationid，方便后续操作。
 *
 *6. 遍历查询结果：对比并处理每个zbx_dc_corr_operation对象，根据差异进行添加、更新和删除操作。
 *
 *7. 重置哈希集迭代器：方便后续遍历哈希集中的对象。
 *
 *8. 遍历哈希集：检查每个zbx_dc_corr_operation对象是否在ids哈希集中，如果在则进行后续操作。
 *
 *9. 销毁哈希集：释放内存。
 *
 *10. 释放查询结果：避免内存泄漏。
 *
 *11. 返回成功：表示整个操作顺利完成。
 ******************************************************************************/
// 定义一个函数，用于比较两个zbx_dc_corr_operation结构体对象的差异
int zbx_dbsync_compare_corr_operations(zbx_dbsync_t *sync)
{
	// 声明一些变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	zbx_dc_corr_operation_t	*corr_operation;

	// 从数据库中查询符合条件的数据
	if (NULL == (result = DBselect(
			"select co.corr_operationid,co.correlationid,co.type"
			" from correlation c,corr_operation co"
			" where c.correlationid=co.correlationid"
				" and c.status=%d",
			ZBX_CORRELATION_ENABLED)))
	{
		// 如果查询失败，返回FAIL
		return FAIL;
	}

	// 初始化dbsync对象
	dbsync_prepare(sync, 3, NULL);

	// 如果同步模式为初始化，则将查询结果赋值给dbresult
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个哈希集，用于存储corr_operationid
	zbx_hashset_create(&ids, dbsync_env.cache->corr_operations.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果，对比并处理每个zbx_dc_corr_operation对象
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将dbrow中的数据转换为zbx_dc_corr_operation结构体对象
		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找哈希集中是否存在该corr_operationid
		if (NULL == (corr_operation = (zbx_dc_corr_operation_t *)zbx_hashset_search(
				&dbsync_env.cache->corr_operations, &rowid)))
		{
			// 如果不存在，则标记为新增操作
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_corr_operation(corr_operation, dbrow))
			// 如果不相等，则标记为更新操作
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果tag不为空，则将dbrow添加到同步操作列表中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->corr_operations, &iter);
	// 遍历哈希集中的每个zbx_dc_corr_operation对象，检查是否在ids哈希集中
	while (NULL != (corr_operation = (zbx_dc_corr_operation_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果不在ids哈希集中，则标记为移除操作
		if (NULL == zbx_hashset_search(&ids, &corr_operation->corr_operationid))
			dbsync_add_row(sync, corr_operation->corr_operationid, ZBX_DBSYNC_ROW_REMOVE, NULL);
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中存储的主机组信息和本地缓存的主机组信息，并将差异应用到本地缓存中。具体来说，代码实现了以下功能：
 *
 *1. 从数据库中查询主机组信息，并存储在`result`变量中。
 *2. 预处理数据库同步操作，为后续的比较和更新操作做好准备。
 *3. 判断同步模式，如果是初始化模式，将数据库查询结果赋值给同步对象的数据结果，并返回成功状态。
 *4. 创建一个哈希集合`ids`，用于存储主机组ID。
 *5. 遍历数据库查询结果，并将主机组ID添加到哈希集合`ids`中。
 *6. 对于哈希集合`ids`中已存在的主机组，检查数据库中的主机组信息是否与本地缓存中的主机组信息匹配。如果不匹配，标记为更新。
 *7. 将待处理的主机组（包括新增、更新和删除）添加到同步操作的待处理行列表中。
 *8. 迭代本地缓存中的主机组，检查是否需要在同步操作中删除。
 *9. 销毁哈希集合`ids`，并释放数据库查询结果。
 *10. 同步操作完成后，返回成功状态。
 ******************************************************************************/
int zbx_dbsync_compare_host_groups(zbx_dbsync_t *sync)
{
	// 声明变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 用于存储主机组ID的哈希集合
	zbx_hashset_iter_t	iter; // 用于迭代哈希集合的迭代器
	zbx_uint64_t		rowid; // 存储从数据库中读取的主机组ID
	zbx_dc_hostgroup_t	*group; // 存储主机组信息的数据结构指针

	// 从数据库中查询主机组信息
	if (NULL == (result = DBselect("select groupid,name from hstgrp")))
		return FAIL; // 如果查询失败，返回错误状态

	// 预处理数据库同步操作
	dbsync_prepare(sync, 2, NULL);

	// 判断同步模式
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result; // 将数据库查询结果赋值给同步对象的数据结果
		return SUCCEED; // 初始化成功，返回成功状态
	}

	// 创建哈希集合用于存储主机组ID
	zbx_hashset_create(&ids, dbsync_env.cache->hostgroups.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历数据库查询结果，并将主机组ID添加到哈希集合中
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]); // 将字符串转换为整数
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid)); // 将主机组ID添加到哈希集合中

		// 检查哈希集合中是否存在该主机组
		if (NULL == (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&dbsync_env.cache->hostgroups, &rowid)))
			tag = ZBX_DBSYNC_ROW_ADD; // 如果不存在，标记为新增
		else if (FAIL == dbsync_compare_host_group(group, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE; // 如果不匹配，标记为更新

		// 如果标记为新增或更新，将主机组添加到同步操作的待处理行列表中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 迭代哈希集合中的主机组，检查是否需要在同步操作中删除
	zbx_hashset_iter_reset(&dbsync_env.cache->hostgroups, &iter);
	while (NULL != (group = (zbx_dc_hostgroup_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果主机组ID不在ids哈希集合中，标记为删除
		if (NULL == zbx_hashset_search(&ids, &group->groupid))
			dbsync_add_row(sync, group->groupid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁ids哈希集合
	zbx_hashset_destroy(&ids);
	// 释放数据库查询结果
	DBfree_result(result);

	// 同步操作完成后，返回成功状态
	return SUCCEED;
}

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_host_groups                                   *
 *                                                                            *
 * Purpose: compares host groups table with cached configuration data         *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_dbsync_compare_host_groups(zbx_dbsync_t *sync)
{
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids;
	zbx_hashset_iter_t	iter;
	zbx_uint64_t		rowid;
	zbx_dc_hostgroup_t	*group;

	if (NULL == (result = DBselect("select groupid,name from hstgrp")))
		return FAIL;

	dbsync_prepare(sync, 2, NULL);

	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	zbx_hashset_create(&ids, dbsync_env.cache->hostgroups.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		if (NULL == (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&dbsync_env.cache->hostgroups, &rowid)))
			tag = ZBX_DBSYNC_ROW_ADD;
		else if (FAIL == dbsync_compare_host_group(group, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->hostgroups, &iter);
	while (NULL != (group = (zbx_dc_hostgroup_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &group->groupid))
			dbsync_add_row(sync, group->groupid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_item_pp_preproc_row                                       *
 *                                                                            *
 * Purpose: applies necessary preprocessing before row is compared/used       *
 *                                                                            *
 * Parameter: row - [IN] the row to preprocess                                *
 *                                                                            *
 * Return value: the preprocessed row of item_preproc table                   *
 *                                                                            *
 * Comments: The row preprocessing can be used to expand user macros in       *
 *           some columns.                                                    *
 *                                                                            *
 ******************************************************************************/
static char	**dbsync_item_pp_preproc_row(char **row)
{
	zbx_uint64_t	hostid;
/******************************************************************************
 * *
 *这块代码的主要目的是检查数据库同步中的行宏是否成功。如果成功，则提取关联的主机标识符，并使用该标识符扩展用户宏。最后，返回处理后的行数据。
 ******************************************************************************/
// 定义一个函数，用于检查数据库同步中的行宏是否成功
if (SUCCEED == dbsync_check_row_macros(row, 3))
{
    // 获取关联的主机标识符
    ZBX_STR2UINT64(hostid, row[5]);

    // 扩展用户宏
    row[3] = zbx_dc_expand_user_macros(row[3], &hostid, 1, NULL);
}

// 返回处理后的行数据
/******************************************************************************
 * *
 *整个代码块的主要目的是比较一个名为 dbrow 的 DB_ROW 结构体数组与一个名为 preproc 的 zbx_dc_preproc_op_t 结构体实例之间的对应字段是否相等。如果所有字段都相等，函数返回 SUCCEED，否则返回 FAIL。
 ******************************************************************************/
// 定义一个名为 dbsync_compare_item_preproc 的静态函数，该函数接收两个参数，一个是 const zbx_dc_preproc_op_t 类型的指针 preproc，另一个是 const DB_ROW 类型的指针 dbrow。
static int	dbsync_compare_item_preproc(const zbx_dc_preproc_op_t *preproc, const DB_ROW dbrow)
{
	// 判断 dbrow 数组的第二个元素（索引为 1）与 preproc 指向的结构体中的 itemid 字段是否相等，如果不相等，返回 FAIL
	if (FAIL == dbsync_compare_uint64(dbrow[1], preproc->itemid))
		return FAIL;

	// 判断 dbrow 数组的第三个元素（索引为 2）与 preproc 指向的结构体中的 type 字段是否相等，如果不相等，返回 FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[2], preproc->type))
		return FAIL;

	// 判断 dbrow 数组的第四个元素（索引为 3）与 preproc 指向的结构体中的 params 字段是否相等，如果不相等，返回 FAIL
	if (FAIL == dbsync_compare_str(dbrow[3], preproc->params))
		return FAIL;

/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的预处理项（item_preproc）和本地缓存的预处理项，对于新增、更新和删除的预处理项进行相应的操作。具体来说，代码实现了以下功能：
 *
 *1. 从数据库中查询所有符合条件的预处理项（item_preproc）及其相关信息。
 *2. 初始化dbsync结构体，为处理数据做好准备。
 *3. 遍历查询结果，将每一行数据添加到hashset中，以便后续比较。
 *4. 比较本地缓存的预处理项和数据库中的预处理项，对于新增、更新和删除的预处理项进行相应的操作。
 *5. 释放查询结果，并销毁hashset。
 *6. 返回执行结果（SUCCEED或FAIL）。
 ******************************************************************************/
int zbx_dbsync_compare_item_preprocs(zbx_dbsync_t *sync)
{
	// 定义所需的变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 用于存储预处理项的集合
	zbx_hashset_iter_t	iter; // 用于迭代hashset的迭代器
	zbx_uint64_t		rowid; // 记录ID
	zbx_dc_preproc_op_t	*preproc; // 预处理项指针
	char			**row; // 用于存储查询结果的指针

	// 执行SQL查询，获取所有item_preprocid及其相关信息
	if (NULL == (result = DBselect(
			"select pp.item_preprocid,pp.itemid,pp.type,pp.params,pp.step,i.hostid"
			" from item_preproc pp,items i,hosts h"
			" where pp.itemid=i.itemid"
				" and i.hostid=h.hostid"
				" and h.status in (%d,%d)"
				" and i.flags<>%d"
			" order by pp.itemid",
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		// 如果查询失败，返回FAIL
		return FAIL;
	}

	// 初始化dbsync结构体，准备处理数据
	dbsync_prepare(sync, 6, dbsync_item_pp_preproc_row);

	// 如果当前模式为ZBX_DBSYNC_INIT，则将查询结果赋值给sync结构体，并返回SUCCEED
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个hashset用于存储查询结果
	zbx_hashset_create(&ids, dbsync_env.cache->hostgroups.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果，并将每一行数据添加到hashset中
	while (NULL != (dbrow = DBfetch(result)))
	{
		// 解析记录ID
		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将记录ID插入hashset中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 获取预处理项信息
		row = dbsync_preproc_row(sync, dbrow);

		// 查询hashset中是否存在该预处理项
		if (NULL == (preproc = (zbx_dc_preproc_op_t *)zbx_hashset_search(&dbsync_env.cache->preprocops,
				&rowid)))
		{
			// 如果不存在，标记为新增
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_item_preproc(preproc, row))
			// 如果不相同，标记为更新
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果标记为新增或更新，将数据添加到sync结构体的row数组中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, row);
	}

	// 重置hashset迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->preprocops, &iter);
	// 遍历hashset中的预处理项，将不在ids集合中的预处理项添加为删除操作
	while (NULL != (preproc = (zbx_dc_preproc_op_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &preproc->item_preprocid))
			// 如果不存在于ids集合中，添加为删除操作
			dbsync_add_row(sync, preproc->item_preprocid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁hashset
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 执行成功，返回SUCCEED
	return SUCCEED;
}

		return SUCCEED;
	}

	zbx_hashset_create(&ids, dbsync_env.cache->hostgroups.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;
		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		row = dbsync_preproc_row(sync, dbrow);

		if (NULL == (preproc = (zbx_dc_preproc_op_t *)zbx_hashset_search(&dbsync_env.cache->preprocops,
				&rowid)))
		{
			tag = ZBX_DBSYNC_ROW_ADD;
		}
		else if (FAIL == dbsync_compare_item_preproc(preproc, row))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, row);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->preprocops, &iter);
	while (NULL != (preproc = (zbx_dc_preproc_op_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &preproc->item_preprocid))
			dbsync_add_row(sync, preproc->item_preprocid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_maintenance                                       *
 *                                                                            *
 * Purpose: compares maintenance table row with cached configuration data     *
 *                                                                            *
 * Parameter: maintenance - [IN] the cached maintenance data                  *
 *            row         - [IN] the database row                             *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的一行数据（dbrow）与维护对象（maintenance）的相应字段是否一致。如果所有字段都一致，返回SUCCEED，表示比较成功；如果有任何字段不一致，返回FAIL，表示比较失败。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_maintenance，接收两个参数，一个是zbx_dc_maintenance_t类型的指针maintenance，另一个是DB_ROW类型的指针dbrow
static int	dbsync_compare_maintenance(const zbx_dc_maintenance_t *maintenance, const DB_ROW dbrow)
{
	// 判断dbrow[1]（第2列数据）与maintenance->type（维护类型）是否相同，如果不相同，返回FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[1], maintenance->type))
		return FAIL;

	// 判断dbrow[2]（第3列数据）与maintenance->active_since（维护活动自从时间）是否相同，如果不相同，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[2], maintenance->active_since))
		return FAIL;

	// 判断dbrow[3]（第4列数据）与maintenance->active_until（维护活动直到时间）是否相同，如果不相同，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[3], maintenance->active_until))
		return FAIL;

	// 判断dbrow[4]（第5列数据）与maintenance->tags_evaltype（标签评估类型）是否相同，如果不相同，返回FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[4], maintenance->tags_evaltype))
		return FAIL;

	// 如果以上所有条件都满足，返回SUCCEED，表示比较成功
	return SUCCEED;
/******************************************************************************
 * *
 *整个代码块的主要目的是比较数据库中的维护信息与本地缓存的维护信息，对于不一致的地方，更新本地缓存。具体来说，代码做了以下事情：
 *
 *1. 查询数据库中的维护信息，如果查询失败，返回错误。
 *2. 初始化dbsync结构体，准备接收数据。
 *3. 如果同步模式为初始化，将查询结果赋值给dbresult，并返回同步成功。
 *4. 创建维护ID集合。
 *5. 遍历查询结果，将维护ID添加到集合中。
 *6. 查找维护对象，比较维护对象和dbrow，根据结果标记为更新或新增。
 *7. 如果tag不为空，添加行记录。
 *8. 重置维护ID集合迭代器，遍历维护ID集合，查找不在查询结果中的维护，标记为删除。
 *9. 销毁维护ID集合，释放查询结果。
 *10. 同步成功，返回SUCCEED。
 ******************************************************************************/
int zbx_dbsync_compare_maintenances(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_t		ids; // 维护ID集合
	zbx_hashset_iter_t	iter; // 迭代器
	zbx_uint64_t		rowid; // 行ID
	zbx_dc_maintenance_t	*maintenance; // 维护对象

	// 从数据库中查询维护信息
	if (NULL == (result = DBselect("select maintenanceid,maintenance_type,active_since,active_till,tags_evaltype"
						" from maintenances")))
	{
		// 查询失败，返回错误
		return FAIL;
	}

	// 初始化dbsync结构体
	dbsync_prepare(sync, 5, NULL);

	// 如果同步模式为初始化
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		// 将查询结果赋值给dbresult
		sync->dbresult = result;
		// 同步成功，返回SUCCEED
		return SUCCEED;
	}

	// 创建维护ID集合
	zbx_hashset_create(&ids, dbsync_env.cache->maintenances.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果
	while (NULL != (dbrow = DBfetch(result)))
	{
		// 将dbrow转换为维护ID
		ZBX_STR2UINT64(rowid, dbrow[0]);
		// 将维护ID添加到集合中
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找维护对象
		maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&dbsync_env.cache->maintenances, &rowid);

		// 如果没有找到维护对象，标记为新增
		if (NULL == maintenance)
			tag = ZBX_DBSYNC_ROW_ADD;
		// 否则，比较维护对象和dbrow，根据结果标记为更新或不变
		else if (FAIL == dbsync_compare_maintenance(maintenance, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果tag不为空，添加行记录
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置维护ID集合迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->maintenances, &iter);
	// 遍历维护ID集合，查找不在查询结果中的维护，标记为删除
	while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &maintenance->maintenanceid))
			dbsync_add_row(sync, maintenance->maintenanceid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁维护ID集合
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 同步成功，返回SUCCEED
	return SUCCEED;
}

		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->maintenances, &iter);
	while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &maintenance->maintenanceid))
			dbsync_add_row(sync, maintenance->maintenanceid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *这块代码的主要目的是比较数据库中的维护标签与zbx_dc_maintenance_tag结构体中的数据是否一致。具体来说，它逐个比较了数据库行中的第3、4、5个元素（分别为操作、标签和值）与结构体中的对应数据，如果所有比较都成功，则返回SUCCEED，表示比较成功；如果有任何一项比较失败，则返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于比较数据库中的维护标签和zbx_dc_maintenance_tag结构体中的数据
static int	dbsync_compare_maintenance_tag(const zbx_dc_maintenance_tag_t *maintenance_tag, const DB_ROW dbrow)
{
	// 判断dbrow[2]（第3个数据库行元素）与maintenance_tag->op（操作）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[2], maintenance_tag->op))
		return FAIL;

	// 判断dbrow[3]（第4个数据库行元素）与maintenance_tag->tag（标签）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_str(dbrow[3], maintenance_tag->tag))
		return FAIL;

	// 判断dbrow[4]（第5个数据库行元素）与maintenance_tag->value（值）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_str(dbrow[4], maintenance_tag->value))
		return FAIL;

	// 如果以上所有条件都满足，返回SUCCEED，表示比较成功
	return SUCCEED;
}

	if (FAIL == dbsync_compare_int(dbrow[2], maintenance_tag->op))
		return FAIL;

	if (FAIL == dbsync_compare_str(dbrow[3], maintenance_tag->tag))
		return FAIL;

	if (FAIL == dbsync_compare_str(dbrow[4], maintenance_tag->value))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_maintenance_tags                              *
 *                                                                            *
 * Purpose: compares maintenances table with cached configuration data        *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对比数据库中的维护标签数据与缓存中的数据，对于不同的数据类型执行相应的操作（添加、更新或删除），并将最终处理后的数据存储到缓存中。
 ******************************************************************************/
int zbx_dbsync_compare_maintenance_tags(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW				dbrow;
	DB_RESULT			result;
	zbx_hashset_t			ids;
	zbx_hashset_iter_t		iter;
	zbx_uint64_t			rowid;
	zbx_dc_maintenance_tag_t	*maintenance_tag;

	// 查询数据库，获取维护标签信息
	if (NULL == (result = DBselect("select maintenancetagid,maintenanceid,operator,tag,value"
						" from maintenance_tag")))
	{
		// 如果查询失败，返回FAIL
		return FAIL;
	}

	// 初始化dbsync
	dbsync_prepare(sync, 5, NULL);

	// 如果同步模式为初始化，则设置dbresult为查询结果，并返回成功
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个哈希集，用于存储维护标签ID
	zbx_hashset_create(&ids, dbsync_env.cache->maintenance_tags.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历数据库查询结果，对比维护标签数据
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将数据库中的行数据转换为行ID
		ZBX_STR2UINT64(rowid, dbrow[0]);
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找缓存中的维护标签
		maintenance_tag = (zbx_dc_maintenance_tag_t *)zbx_hashset_search(&dbsync_env.cache->maintenance_tags,
				&rowid);

		// 对比维护标签数据，判断是否需要更新或添加新数据
		if (NULL == maintenance_tag)
			tag = ZBX_DBSYNC_ROW_ADD;
		else if (FAIL == dbsync_compare_maintenance_tag(maintenance_tag, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果需要更新或添加新数据，则添加行数据
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->maintenance_tags, &iter);
	// 遍历缓存的维护标签，对比数据库中的数据
	while (NULL != (maintenance_tag = (zbx_dc_maintenance_tag_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果哈希集中不存在该维护标签，则将其添加为删除行数据
		if (NULL == zbx_hashset_search(&ids, &maintenance_tag->maintenancetagid))
			dbsync_add_row(sync, maintenance_tag->maintenancetagid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁哈希集
	zbx_hashset_destroy(&ids);
	// 释放数据库查询结果
	DBfree_result(result);

	// 返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_maintenance_period                                *
 *                                                                            *
 * Purpose: compares maintenance_period table row with cached configuration   *
 *          dat                                                               *
 *                                                                            *
 * Parameter: maintenance_period - [IN] the cached maintenance period         *
 *            row                - [IN] the database row                      *
 *                                                                            *
 * Return value: SUCCEED - the row matches configuration data                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较一个数据库中的维护周期记录（dbrow）与一个zbx_dc_maintenance_period_t类型的结构体（period）中的数据是否一致。如果一致，返回SUCCEED，否则返回FAIL。在这个过程中，依次比较了dbrow的八个字段与period中的相应字段。
 ******************************************************************************/
// 定义一个静态函数dbsync_compare_maintenance_period，接收两个参数，一个是zbx_dc_maintenance_period_t类型的period，另一个是DB_ROW类型的dbrow。
static int	dbsync_compare_maintenance_period(const zbx_dc_maintenance_period_t *period, const DB_ROW dbrow)
{
	// 判断dbrow[1]（即第一个字段）与period->type（类型）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_uchar(dbrow[1], period->type))
		return FAIL;

	// 判断dbrow[2]（即第二个字段）与period->every（每次）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[2], period->every))
		return FAIL;

	// 判断dbrow[3]（即第三个字段）与period->month（月份）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[3], period->month))
		return FAIL;

	// 判断dbrow[4]（即第四个字段）与period->dayofweek（星期）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[4], period->dayofweek))
		return FAIL;

	// 判断dbrow[5]（即第五个字段）与period->day（天）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[5], period->day))
		return FAIL;

	// 判断dbrow[6]（即第六个字段）与period->start_time（开始时间）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[6], period->start_time))
		return FAIL;

	// 判断dbrow[7]（即第七个字段）与period->period（周期）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[7], period->period))
		return FAIL;

	// 判断dbrow[8]（即第八个字段）与period->start_date（开始日期）是否相等，如果不相等，返回FAIL
	if (FAIL == dbsync_compare_int(dbrow[8], period->start_date))
		return FAIL;

	// 如果以上所有判断都相等，则返回SUCCEED，表示比较成功
	return SUCCEED;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是对比两个维护周期表（maintenances_windows和timeperiods）中的数据，并将差异应用到另一个维护周期表（dbsync_maintenance_periods）中。具体操作包括以下几个步骤：
 *
 *1. 从数据库中查询两个表的数据，并存储在result中。
 *2. 初始化dbsync，为其提供查询结果。
 *3. 创建一个哈希集ids，用于存储主维护周期ID。
 *4. 遍历查询结果，对比每个周期，并进行以下操作：
 *   - 插入周期ID到ids哈希集。
 *   - 查找对应的主维护周期。
 *   - 如果不存在或对比失败，则将该周期添加到结果集中。
 *5. 遍历ids哈希集中的周期，如果不存在于ids集中，则标记为删除。
 *6. 销毁ids哈希集，释放查询结果，并返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_maintenance_periods(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW				dbrow;
	DB_RESULT			result;
	zbx_hashset_t			ids;
	zbx_hashset_iter_t		iter;
	zbx_uint64_t			rowid;
	zbx_dc_maintenance_period_t	*period;

	// 从数据库中查询数据
	if (NULL == (result = DBselect("select t.timeperiodid,t.timeperiod_type,t.every,t.month,t.dayofweek,t.day,"
						"t.start_time,t.period,t.start_date,m.maintenanceid"
					" from maintenances_windows m,timeperiods t"
					" where t.timeperiodid=m.timeperiodid")))
	{
		// 如果查询失败，返回FAIL
		return FAIL;
	}

	// 初始化dbsync
	dbsync_prepare(sync, 10, NULL);

	// 如果当前模式为ZBX_DBSYNC_INIT，则设置dbresult为查询结果，返回SUCCEED
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个哈希集用于存储主维护周期ID
	zbx_hashset_create(&ids, dbsync_env.cache->maintenance_periods.num_data, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 遍历查询结果，对比每个周期并进行相应操作
	while (NULL != (dbrow = DBfetch(result)))
	{
		unsigned char	tag = ZBX_DBSYNC_ROW_NONE;

		// 将dbrow[0]转换为uint64类型并存储到rowid中
		ZBX_STR2UINT64(rowid, dbrow[0]);

		// 在哈希集中插入rowid
		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

		// 查找对应的主维护周期
		period = (zbx_dc_maintenance_period_t *)zbx_hashset_search(&dbsync_env.cache->maintenance_periods,
				&rowid);

		// 如果没有找到该周期，则标记为新增
		if (NULL == period)
			tag = ZBX_DBSYNC_ROW_ADD;
		// 否则，如果对比失败，则标记为更新
		else if (FAIL == dbsync_compare_maintenance_period(period, dbrow))
			tag = ZBX_DBSYNC_ROW_UPDATE;

		// 如果tag不为空，则将该周期添加到结果集中
		if (ZBX_DBSYNC_ROW_NONE != tag)
			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	// 重置哈希集迭代器
	zbx_hashset_iter_reset(&dbsync_env.cache->maintenance_periods, &iter);
	// 遍历哈希集中的周期，如果不存在于ids集中，则标记为删除
	while (NULL != (period = (zbx_dc_maintenance_period_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &period->timeperiodid))
			dbsync_add_row(sync, period->timeperiodid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	// 销毁ids哈希集
	zbx_hashset_destroy(&ids);
	// 释放查询结果
	DBfree_result(result);

	// 返回SUCCEED，表示整个过程成功
	return SUCCEED;
}

			dbsync_add_row(sync, rowid, tag, dbrow);
	}

	zbx_hashset_iter_reset(&dbsync_env.cache->maintenance_periods, &iter);
	while (NULL != (period = (zbx_dc_maintenance_period_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == zbx_hashset_search(&ids, &period->timeperiodid))
			dbsync_add_row(sync, period->timeperiodid, ZBX_DBSYNC_ROW_REMOVE, NULL);
	}

	zbx_hashset_destroy(&ids);
	DBfree_result(result);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_maintenance_groups                            *
 *                                                                            *
 * Purpose: compares maintenances_groups table with cached configuration data *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较zbx_dbsync_compare_maintenance_groups函数接收的两个数据库查询结果，根据维护对象及其分组信息在两个查询结果中的差异，执行插入和删除操作，以实现数据库表的同步。
 *
 *代码详细注释如下：
 *
 *1. 定义变量，用于存储数据库查询结果、维护对象、分组集合等。
 *2. 查询数据库，获取维护对象及其分组信息。
 *3. 初始化dbsync结构体，准备插入和删除操作。
 *4. 如果同步模式为初始化，则保存查询结果到dbsync结构体。
 *5. 创建分组集合，用于存储维护对象及其分组信息。
 *6. 遍历维护对象，构建分组索引。
 *7. 处理新增和删除操作。
 *8. 处理删除操作。
 *9. 释放资源。
 *
 *整个代码块的核心是处理数据库查询结果中的维护对象及其分组信息的插入和删除操作，以实现数据库表的同步。
 ******************************************************************************/
int	zbx_dbsync_compare_maintenance_groups(zbx_dbsync_t *sync)
{
	/* 定义变量，用于存储数据库查询结果、维护对象、分组集合等 */
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_iter_t	iter;
	zbx_dc_maintenance_t	*maintenance;
	zbx_hashset_t		mgroups;
	int			i;
	zbx_uint64_pair_t	mg_local, *mg;
	char			maintenanceid_s[MAX_ID_LEN + 1], groupid_s[MAX_ID_LEN + 1];
	char			*del_row[2] = {maintenanceid_s, groupid_s};

	/* 查询数据库，获取维护对象及其分组信息 */
	if (NULL == (result = DBselect("select maintenanceid,groupid from maintenances_groups order by maintenanceid")))
		return FAIL;

	/* 初始化dbsync结构体，准备插入和删除操作 */
	dbsync_prepare(sync, 2, NULL);

	/* 如果同步模式为初始化，则保存查询结果到dbsync结构体 */
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	/* 创建分组集合，用于存储维护对象及其分组信息 */
	zbx_hashset_create(&mgroups, 100, ZBX_DEFAULT_UINT64_PAIR_HASH_FUNC, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);

	/* 遍历维护对象，构建分组索引 */
	zbx_hashset_iter_reset(&dbsync_env.cache->maintenances, &iter);
	while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		mg_local.first = maintenance->maintenanceid;

		for (i = 0; i < maintenance->groupids.values_num; i++)
		{
			mg_local.second = maintenance->groupids.values[i];
			zbx_hashset_insert(&mgroups, &mg_local, sizeof(mg_local));
		}
	}

	/* 处理新增和删除操作 */
	while (NULL != (dbrow = DBfetch(result)))
	{
		ZBX_STR2UINT64(mg_local.first, dbrow[0]);
		ZBX_STR2UINT64(mg_local.second, dbrow[1]);

		/* 查找分组索引中的维护对象 */
		if (NULL == (mg = (zbx_uint64_pair_t *)zbx_hashset_search(&mgroups, &mg_local)))
			/* 未找到，表示新增，执行插入操作 */
			dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_ADD, dbrow);
		else
			/* 已找到，表示删除，执行删除操作 */
			zbx_hashset_remove_direct(&mgroups, mg);
	}

	/* 处理删除操作 */
	zbx_hashset_iter_reset(&mgroups, &iter);
	while (NULL != (mg = (zbx_uint64_pair_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_snprintf(maintenanceid_s, sizeof(maintenanceid_s), ZBX_FS_UI64, mg->first);
		zbx_snprintf(groupid_s, sizeof(groupid_s), ZBX_FS_UI64, mg->second);
		/* 构造删除行数据 */
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	/* 释放资源 */
	DBfree_result(result);
	zbx_hashset_destroy(&mgroups);

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dbsync_compare_maintenance_hosts                             *
 *                                                                            *
 * Purpose: compares maintenances_hosts table with cached configuration data  *
 *                                                                            *
 * Parameter: cache - [IN] the configuration cache                            *
 *            sync  - [OUT] the changeset                                     *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较zbx_dbsync_compare_maintenance_hosts函数中的维护关系数据和数据库中的维护关系数据，并在必要时更新数据库。代码首先查询数据库中的维护关系表，然后遍历维护关系，将数据库中的维护关系与哈希集中的维护关系进行比较。对于数据库中新增的维护关系，将其添加到哈希集中；对于已删除的维护关系，将其从哈希集中删除并添加到数据库中的删除行。最后，释放数据库结果集和销毁哈希集，返回成功。
 ******************************************************************************/
int zbx_dbsync_compare_maintenance_hosts(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_iter_t	iter;
	zbx_dc_maintenance_t	*maintenance;
	zbx_hashset_t		mhosts;
	int			i;
	zbx_uint64_pair_t	mh_local, *mh;
	char			maintenanceid_s[MAX_ID_LEN + 1], hostid_s[MAX_ID_LEN + 1];
	char			*del_row[2] = {maintenanceid_s, hostid_s};

	// 查询数据库中的维护关系表
	if (NULL == (result = DBselect("select maintenanceid,hostid from maintenances_hosts order by maintenanceid")))
		return FAIL;

	// 初始化dbsync
	dbsync_prepare(sync, 2, NULL);

	// 如果同步模式为初始化
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建哈希集用于存储维护关系
	zbx_hashset_create(&mhosts, 100, ZBX_DEFAULT_UINT64_PAIR_HASH_FUNC, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);

	// 遍历所有维护关系
	zbx_hashset_iter_reset(&dbsync_env.cache->maintenances, &iter);
	while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		// 保存维护关系的维护ID和主机ID
		mh_local.first = maintenance->maintenanceid;

		for (i = 0; i < maintenance->hostids.values_num; i++)
		{
			mh_local.second = maintenance->hostids.values[i];
			// 将维护关系添加到哈希集中
			zbx_hashset_insert(&mhosts, &mh_local, sizeof(mh_local));
		}
	}

	// 遍历数据库中的维护关系，添加新的行，删除哈希集中的现有行
	while (NULL != (dbrow = DBfetch(result)))
	{
		ZBX_STR2UINT64(mh_local.first, dbrow[0]);
		ZBX_STR2UINT64(mh_local.second, dbrow[1]);

		// 如果在哈希集中找不到该维护关系，则添加新行
		if (NULL == (mh = (zbx_uint64_pair_t *)zbx_hashset_search(&mhosts, &mh_local)))
			dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_ADD, dbrow);
		// 否则，从哈希集中删除该维护关系
		else
			zbx_hashset_remove_direct(&mhosts, mh);
	}

	// 添加已删除的行
	zbx_hashset_iter_reset(&mhosts, &iter);
	while (NULL != (mh = (zbx_uint64_pair_t *)zbx_hashset_iter_next(&iter)))
	{
		// 构造删除行的ID字符串
		zbx_snprintf(maintenanceid_s, sizeof(maintenanceid_s), ZBX_FS_UI64, mh->first);
		zbx_snprintf(hostid_s, sizeof(hostid_s), ZBX_FS_UI64, mh->second);
		// 添加删除行
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	// 释放数据库结果集
	DBfree_result(result);
	// 销毁哈希集
	zbx_hashset_destroy(&mhosts);
/******************************************************************************
 * *
 *这块代码的主要目的是比较数据库中存储的主机组和主机信息，根据需要添加或删除主机组和主机的关联关系。具体来说，代码完成了以下任务：
 *
 *1. 从数据库中查询主机组和主机的信息，并存储在result变量中。
 *2. 初始化dbsync，为后续操作做好准备。
 *3. 创建一个hashset用于存储主机组。
 *4. 遍历所有主机组及其关联的主机，将主机组和主机的关联关系存储在hashset中。
 *5. 处理新的行，判断是否需要添加或删除主机组和主机的关联关系。
 *6. 添加已删除的行，实现在数据库中删除主机组和主机的关联关系。
 *7. 释放资源，清理内存。
 *
 *整个代码块的输出结果为：成功（SUCCEED）。
 ******************************************************************************/
int zbx_dbsync_compare_host_group_hosts(zbx_dbsync_t *sync)
{
	// 定义变量
	DB_ROW			dbrow;
	DB_RESULT		result;
	zbx_hashset_iter_t	iter, iter_hosts;
	zbx_dc_hostgroup_t	*group;
	zbx_hashset_t		groups;
	zbx_uint64_t		*phostid;
	zbx_uint64_pair_t	gh_local, *gh;
	char			groupid_s[MAX_ID_LEN + 1], hostid_s[MAX_ID_LEN + 1];
	char			*del_row[2] = {groupid_s, hostid_s};

	// 从数据库中查询主机组和主机的信息
	if (NULL == (result = DBselect(
			"select hg.groupid,hg.hostid"
			" from hosts_groups hg,hosts h"
			" where hg.hostid=h.hostid"
			" and h.status in (%d,%d)"
			" and h.flags<>%d"
			" order by hg.groupid",
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, ZBX_FLAG_DISCOVERY_PROTOTYPE)))
	{
		return FAIL;
	}

	// 初始化dbsync
	dbsync_prepare(sync, 2, NULL);

	// 如果是初始化模式，将查询结果赋值给dbresult
	if (ZBX_DBSYNC_INIT == sync->mode)
	{
		sync->dbresult = result;
		return SUCCEED;
	}

	// 创建一个hashset用于存储主机组
	zbx_hashset_create(&groups, 100, ZBX_DEFAULT_UINT64_PAIR_HASH_FUNC, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);

	// 遍历所有主机组及其关联的主机
	zbx_hashset_iter_reset(&dbsync_env.cache->hostgroups, &iter);
	while (NULL != (group = (zbx_dc_hostgroup_t *)zbx_hashset_iter_next(&iter)))
	{
		gh_local.first = group->groupid;

		zbx_hashset_iter_reset(&group->hostids, &iter_hosts);
		while (NULL != (phostid = (zbx_uint64_t *)zbx_hashset_iter_next(&iter_hosts)))
		{
			gh_local.second = *phostid;
			zbx_hashset_insert(&groups, &gh_local, sizeof(gh_local));
		}
	}

	// 处理新的行，删除索引中的现有行
	while (NULL != (dbrow = DBfetch(result)))
	{
		ZBX_STR2UINT64(gh_local.first, dbrow[0]);
		ZBX_STR2UINT64(gh_local.second, dbrow[1]);

		// 如果在hashset中找不到该行，则添加为新行
		if (NULL == (gh = (zbx_uint64_pair_t *)zbx_hashset_search(&groups, &gh_local)))
			dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_ADD, dbrow);
		// 否则，从hashset中移除该行
		else
			zbx_hashset_remove_direct(&groups, gh);
	}

	// 添加已删除的行
	zbx_hashset_iter_reset(&groups, &iter);
	while (NULL != (gh = (zbx_uint64_pair_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_snprintf(groupid_s, sizeof(groupid_s), ZBX_FS_UI64, gh->first);
		zbx_snprintf(hostid_s, sizeof(hostid_s), ZBX_FS_UI64, gh->second);
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	// 释放资源
	DBfree_result(result);
	zbx_hashset_destroy(&groups);

	return SUCCEED;
}

			dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_ADD, dbrow);
		else
			zbx_hashset_remove_direct(&groups, gh);
	}

	/* add removed rows */
	zbx_hashset_iter_reset(&groups, &iter);
	while (NULL != (gh = (zbx_uint64_pair_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_snprintf(groupid_s, sizeof(groupid_s), ZBX_FS_UI64, gh->first);
		zbx_snprintf(hostid_s, sizeof(hostid_s), ZBX_FS_UI64, gh->second);
		dbsync_add_row(sync, 0, ZBX_DBSYNC_ROW_REMOVE, del_row);
	}

	DBfree_result(result);
	zbx_hashset_destroy(&groups);

	return SUCCEED;
}
