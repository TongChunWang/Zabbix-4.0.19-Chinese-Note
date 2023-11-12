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
#include "mutexs.h"
#include "stats.h"
#include "ipc.h"
#include "procstat.h"

#ifdef ZBX_PROCSTAT_COLLECTOR

/*
 * The process CPU statistics are stored using the following memory layout.
 *
 *  .--------------------------------------.
 *  | header                               |
 *  | ------------------------------------ |
 *  | process cpu utilization queries      |
 *  | and historical data                  |
 *  | ------------------------------------ |
 *  | free space                           |
 *  '--------------------------------------'
 *
 * Because the shared memory can be resized by other processes instead of
 * using pointers (when allocating strings, building single linked lists)
 * the memory offsets from the beginning of shared memory segment are used.
 * 0 offset is interpreted similarly to NULL pointer.
 *
 * Currently integer values are used to store offsets to internally allocated
 * memory which leads to 2GB total size limit.
 *
 * During every data collection cycle collector does the following:
 * 1) acquires list of all processes running on system
 * 2) builds a list of processes monitored by queries
 * 3) reads total cpu utilization snapshot for the monitored processes
 * 4) calculates cpu utilization difference by comparing with previous snapshot
 * 5) updates cpu utilization values for queries.
 * 6) saves the last cpu utilization snapshot
 *
 * Initialisation.
 * * zbx_procstat_init() initialises procstat dshm structure but doesn't allocate memory from the system
/******************************************************************************
 * 
 ******************************************************************************/
/* the main collector data */
extern ZBX_COLLECTOR_DATA	*collector;

/* local reference to the procstat shared memory */
static zbx_dshm_ref_t	procstat_ref;

/* memory allocation within dshm. */
/* 1. Ensure that memory segment has enough free space before allocating space */
/* 2. Check how much of the allocated dshm is actually used by procstat */
/* 3. Change the dshm size when necessary */
typedef struct
{
	/* a linked list of active queries (offset of the first active query) */
	int	queries;

	/* the total size of the allocated queries and strings */
	int	size_allocated;

	/* the total shared memory segment size */
	size_t	size;
}
zbx_procstat_header_t;

/* define some constants for memory alignment and queries */
#define PROCSTAT_NULL_OFFSET		0

#define PROCSTAT_ALIGNED_HEADER_SIZE	ZBX_SIZE_T_ALIGN8(sizeof(zbx_procstat_header_t))

#define PROCSTAT_PTR(base, offset)	((char *)base + offset)

#define PROCSTAT_PTR_NULL(base, offset)									\
		(PROCSTAT_NULL_OFFSET == offset ? NULL : PROCSTAT_PTR(base, offset))

#define PROCSTAT_QUERY_FIRST(base)										\
		(zbx_procstat_query_t*)PROCSTAT_PTR_NULL(base, ((zbx_procstat_header_t *)base)->queries)

#define PROCSTAT_QUERY_NEXT(base, query)									\
		(zbx_procstat_query_t*)PROCSTAT_PTR_NULL(base, query->next)

#define PROCSTAT_OFFSET(base, ptr) ((char *)ptr - (char *)base)

/* define some constants for maximum number of queries and time periods */
#define PROCSTAT_MAX_QUERIES	1024

/* the time period after which inactive queries can be removed */
#define PROCSTAT_MAX_INACTIVITY_PERIOD	SEC_PER_DAY

/* the time interval between compressing (inactive query removal) attempts */
#define PROCSTAT_COMPRESS_PERIOD	SEC_PER_DAY

/* data sample collected every second for the process cpu utilization queries */
typedef struct
{
	zbx_uint64_t	utime;
	zbx_uint64_t	stime;
	zbx_timespec_t	timestamp;
}
zbx_procstat_data_t;

/* process cpu utilization query */
typedef struct
{
	/* the process attributes */
	size_t				procname;
	size_t				username;
	size_t				cmdline;
	zbx_uint64_t			flags;

	/* the index of first (oldest) entry in the history data */
	int				h_first;

	/* the number of entries in the history data */
	int				h_count;

	/* the last access time (request from server) */
	int				last_accessed;

	/* increasing id for every data collection run, used to       */
	/* identify queries that are processed during data collection */
	int				runid;

	/* error code */
	int				error;

	/* offset (from segment beginning) of the next process query */
	int				next;

	/* the cpu utilization history data (ring buffer) */
	zbx_procstat_data_t		h_data[MAX_COLLECTOR_HISTORY];
}
zbx_procstat_query_t;

/* process cpu utilization query data */
typedef struct
{
	/* process attributes */
	const char		*procname;
	const char		*username;
	const char		*cmdline;
	zbx_uint64_t			flags;

	/* error code */
	int				error;

	/* process cpu utilization */
	zbx_uint64_t			utime;
	zbx_uint64_t			stime;

	/* vector of pids matching the process attributes */
	zbx_vector_uint64_t	pids;
}
zbx_procstat_query_data_t;

/* the process cpu utilization snapshot */
static zbx_procstat_util_t	*procstat_snapshot;
/* the number of processes in process cpu utilization snapshot */
static int			procstat_snapshot_num;

/******************************************************************************
 *                                                                            *
 * Function: procstat_dshm_create()                                           *
 *                                                                            *
 * Purpose: create a shared memory segment for procstat data                   *
 *               and allocate memory for the header and the first query       *
 *                                                                            *
 * Parameters: size - [IN] the size of the shared memory segment           *
 *               num_queries - [IN] the number of initial queries            *
 *                                                                            *
 * Return value: NULL if failed, otherwise a pointer to the shared memory segment */
zbx_dshm_ref_t *procstat_dshm_create(size_t size, int num_queries)
{
	/* allocate memory for the header and the first query */
	zbx_dshm_ref_t		ref;
	zbx_procstat_header_t	header;
	zbx_procstat_query_t	query;

	/* init the header and query structures */
	memset(&header, 0, sizeof(header));
	memset(&query, 0, sizeof(query));

	/* set the header size and queries count */
	header.size = size;
	header.size_allocated = sizeof(zbx_procstat_header_t) + sizeof(zbx_procstat_query_t);
	header.queries = num_queries;

	/* allocate memory for the header and the first query */
	ref = zbx_dshm_alloc(sizeof(zbx_procstat_header_t) + sizeof(zbx_procstat_query_t));
	if (ref == NULL)
		return NULL;

	/* initialize the header and query data */
	query.procname = NULL;
	query.username = NULL;
	query.cmdline = NULL;
	query.flags = 0;
	query.h_first = 0;
	query.h_count = 0;
	query.last_accessed = 0;
	query.runid = 0;
	query.error = 0;
	query.next = 0;
	query.h_data[0].utime = 0;
	query.h_data[0].stime = 0;

	/* add the first query to the header */
	zbx_vector_init(&query.pids, 1);
	zbx_vector_append(&query.pids, &query);

	/* set the header and query data in the shared memory segment */
	memcpy(ref, &header, sizeof(header));
	memcpy(ref + sizeof(header), &query, sizeof(query));

	return ref;
}

/******************************************************************************
 *                                                                            *
 * Function: procstat_dshm_realloc()                                         *
 *                                                                            *
 * Purpose: reallocates the shared memory segment for procstat data             *
 *               and updates the header and query data                        *
 *                                                                            *
 * Parameters: ref - [IN] the shared memory segment reference               *
 *             size - [IN] the new size of the shared memory segment       *
 *               num_queries - [IN] the new number of queries               *
 *                                                                            *
 * Return value: NULL if failed, otherwise the updated shared memory segment */
zbx_dshm_ref_t *procstat_dshm_realloc(zbx_dshm_ref_t *ref, size_t size, int num_queries)
{
	zbx_procstat_header_t	*header = (zbx_procstat_header_t *)ref;
	zbx_procstat_query_t	*query = (zbx_procstat_query_t *)(ref + sizeof(zbx_procstat_header_t));

	/* check if the new size is valid */
	if (procstat_dshm_has_enough_space(ref, size - sizeof(zbx_procstat_header_t)))
		return NULL;

	/* update the header size and queries count */
	header->size = size;
	header->size_allocated = sizeof(zbx_procstat_header_t) + sizeof(zbx_procstat_query_t);
	header->queries = num_queries;

	/* update the query data */
	while ((query = zbx_vector_get_next(query)) != NULL)
	{
		query->last_accessed = 0;
		zbx_vector_append(&query->pids, &query);
	}

	return ref;
}

/******************************************************************************
 *                                                                            *
 * Function: procstat_reattach()                                           *
 *                                                                            *
 * Purpose: synchronises the local reference to the procstat shared memory     *
 *               with the global one and updates the header and query data     *
 *                                                                            *
 * Parameters: ref - [IN] the local shared memory segment reference         *
 *               header - [IN] the global header                           *
 *               num_queries - [IN] the global number of queries             *
 *                                                                            *
 * Return value: NULL if failed, otherwise the updated local shared memory segment */
zbx_dshm_ref_t *procstat_reattach(zbx_dshm_ref_t *ref, zbx_procstat_header_t *header, int num_queries)
{
	zbx_procstat_query_t	*query = (zbx_procstat_query_t *)(ref + sizeof(zbx_procstat_header_t));

	/* synchronise the local header with the global one */
	memcpy(ref, header, sizeof(zbx_procstat_header_t));

	/* update the local queries count */
	header->queries = num_queries;

	/* update the query data */
	while ((query = zbx_vector_get_next(query)) != NULL)
	{
		query->last_accessed = 0;
		zbx_vector_append(&query->pids, &query);
	}

	return ref;
}


/******************************************************************************
 *                                                                            *
 * Function: procstat_dshm_used_size                                          *
 *                                                                            *
 * Purpose: calculate the actual shared memory size used by procstat          *
 *                                                                            *
 * Parameters: base - [IN] the procstat shared memory segment                 *
 *                                                                            *
 * Return value: The number of bytes required to store current procstat data. *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个 ProcStat 数据结构的大小。这个函数接收一个 void * 类型的参数 base，通过遍历 base 指向的 ProcStat 数据结构中的所有查询项，计算各个查询项的大小，并按照 8 字节对齐。最后返回计算得到的 ProcStat 数据结构的大小。
 ******************************************************************************/
// 定义一个名为 procstat_dshm_used_size 的静态 size_t 类型函数，接收一个 void * 类型的参数 base。
static size_t	procstat_dshm_used_size(void *base)
{
	// 定义一个指向 zbx_procstat_query_t 类型的指针 query，用于遍历 base 指向的数据结构。
	const zbx_procstat_query_t	*query;
	// 定义一个 size_t 类型的变量 size，用于存储计算得到的 ProcStat 数据结构的大小。
	size_t				size;

	// 如果 base 为空，直接返回 0，表示没有有效数据。
	if (NULL == base)
		return 0;

	// 初始化 size 为 ProcStat 对齐的头部大小。
	size = PROCSTAT_ALIGNED_HEADER_SIZE;

	// 使用一个 for 循环遍历 base 指向的 ProcStat 数据结构中的所有查询项。
	for (query = PROCSTAT_QUERY_FIRST(base); NULL != query; query = PROCSTAT_QUERY_NEXT(base, query))
	{
		// 判断 query 中的 procname 是否为空，如果不为空，则计算其长度并按照 8 字节对齐后加到 size 上。
		if (PROCSTAT_NULL_OFFSET != query->procname)
			size += ZBX_SIZE_T_ALIGN8(strlen(PROCSTAT_PTR(base, query->procname)) + 1);

		// 判断 query 中的 username 是否为空，如果不为空，则计算其长度并按照 8 字节对齐后加到 size 上。
		if (PROCSTAT_NULL_OFFSET != query->username)
			size += ZBX_SIZE_T_ALIGN8(strlen(PROCSTAT_PTR(base, query->username)) + 1);

		// 判断 query 中的 cmdline 是否为空，如果不为空，则计算其长度并按照 8 字节对齐后加到 size 上。
		if (PROCSTAT_NULL_OFFSET != query->cmdline)
			size += ZBX_SIZE_T_ALIGN8(strlen(PROCSTAT_PTR(base, query->cmdline)) + 1);

		// 加上一个 zbx_procstat_query_t 类型的大小。
		size += ZBX_SIZE_T_ALIGN8(sizeof(zbx_procstat_query_t));
	}

	// 返回计算得到的 ProcStat 数据结构的大小。
	return size;
}


/******************************************************************************
 *                                                                            *
 * Function: procstat_queries_num                                             *
 *                                                                            *
 * Purpose: calculate the number of active queries                            *
 *                                                                            *
 * Parameters: base - [IN] the procstat shared memory segment                 *
 *                                                                            *
 * Return value: The number of active queries.                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 procstat_queries_num 的静态函数，该函数接受一个 void* 类型的参数 base。
   该函数的主要目的是计算 base 指针指向的进程统计查询结构体数组中的查询数量。
*/
static int	procstat_queries_num(void *base)
{
	/* 定义一个指向进程统计查询结构体的指针 query，以及一个整型变量 queries_num 用于存储查询数量。
       初始化 queries_num 值为 0。
*/
	const zbx_procstat_query_t	*query;
	int				queries_num;

	/* 检查 base 是否为空，如果为空则直接返回 0，表示没有查询。
       这里使用了 if 语句进行判断，保证了输入参数的有效性。
*/
	if (NULL == base)
		return 0;

	/* 初始化 queries_num 值为 0，以便在后续循环中累加查询数量。
*/
	queries_num = 0;

	/* 使用 for 循环遍历 base 指针指向的进程统计查询结构体数组。
       循环变量 query 从第一个查询开始，每次迭代时查询下一个查询，直到遇到 NULL 结束。
*/
	for (query = PROCSTAT_QUERY_FIRST(base); NULL != query; query = PROCSTAT_QUERY_NEXT(base, query))
		/* 每次迭代时，将当前查询数量加 1，以便最终输出查询总数。
       这里使用了递增操作符 ++，简化了一条独立的语句。
*/
		queries_num++;

	/* 循环结束后，返回 queries_num 即为 base 指针指向的进程统计查询结构体数组中的查询数量。
       这里使用了返回操作符返回 queries_num 值。
*/
	return queries_num;
}


/******************************************************************************
 *                                                                            *
 * Function: procstat_alloc                                                   *
 *                                                                            *
 * Purpose: allocates memory in the shared memory segment,                    *
 *          calls exit() if segment is too small                              *
 *                                                                            *
 * Parameters: base - [IN] the procstat shared memory segment                 *
 *             size - [IN] the number of bytes to allocate                    *
 *                                                                            *
 * Return value: The offset of allocated data from the beginning of segment   *
/******************************************************************************
 * *
 *这段代码的主要目的是分配进程统计数据结构体的内存空间。通过调用`procstat_alloc`函数，传入进程统计数据结构体的基地址和需要分配的内存大小，该函数会按照8字节对齐大小分配内存。在分配过程中，会检查共享内存是否有足够的空间，如果有足够的空间，则更新已分配内存大小并返回分配成功的偏移量。如果分配失败，则会退出程序。
 ******************************************************************************/
/* 定义一个静态函数，用于分配进程统计数据结构体的内存空间。
 * 参数：
 *   base：进程统计数据结构体的基地址。
 *   size：分配的内存空间大小。
 * 返回值：
 *   分配成功的偏移量，失败则返回-1。
 */
static int	procstat_alloc(void *base, size_t size)
{
	/* 将基地址转换为zbx_procstat_header_t类型的指针 */
	zbx_procstat_header_t	*header = (zbx_procstat_header_t *)base;
	int			offset;

	/* 按照8字节对齐大小分配内存 */
	size = ZBX_SIZE_T_ALIGN8(size);

	/* 检查进程统计共享内存是否有足够的空间分配 */
	if (FAIL == procstat_dshm_has_enough_space(header, size))
	{
		/* 这种情况不应该发生，退出程序 */
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	/* 计算新的偏移量 */
	offset = header->size_allocated;

	/* 更新已分配内存大小 */
	header->size_allocated += size;

	/* 返回分配成功的偏移量 */
	return offset;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 procstat_strdup 的函数，该函数接收两个参数，一个是指向进程状态数据的指针 base，另一个是要复制的字符串 str。函数的主要作用是复制字符串，并返回复制后的字符串在内存中的起始地址。如果复制失败，函数返回 NULL。
 ******************************************************************************/
/* 定义一个函数 procstat_strdup，用于复制字符串并返回复制后的字符串在内存中的起始地址。
 * 参数：
 *   base：指向进程状态数据的指针，用于分配内存空间
 *   str：需要复制的字符串
 * 返回值：
 *   成功复制后的字符串在内存中的起始地址，如果失败则返回 NULL
 */
static size_t	procstat_strdup(void *base, const char *str)
{
	/* 定义两个变量 len 和 offset，分别用于存储字符串长度和复制后的字符串在内存中的偏移量 */
	size_t	len, offset;

	/* 检查输入参数，如果 str 为 NULL，则直接返回 NULL */
	if (NULL == str)
		return PROCSTAT_NULL_OFFSET;

	/* 计算字符串的长度，并加上一个空字符 '\0'，以表示字符串的结束 */
	len = strlen(str) + 1;

	/* 调用 procstat_alloc 函数分配内存，并将分配的内存地址存储在 offset 变量中 */
	offset = procstat_alloc(base, len);
		/* 使用 memcpy 函数将原字符串的内容复制到新分配的内存中 */
	memcpy(PROCSTAT_PTR(base, offset), str, len);

	/* 返回复制后的字符串在内存中的起始地址 */
	return offset;
}

 *               does not have enough free space.                             *
 *                                                                            *
 ******************************************************************************/
static size_t	procstat_strdup(void *base, const char *str)
{
	size_t	len, offset;

	if (NULL == str)
		return PROCSTAT_NULL_OFFSET;

	len = strlen(str) + 1;

	offset = procstat_alloc(base, len);
		memcpy(PROCSTAT_PTR(base, offset), str, len);

	return offset;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`procstat_copy_data`的静态函数，该函数用于将一个`zbx_procstat_header`结构体中的查询数据（包括process_name、username和cmdline）从源地址拷贝到目标地址。在这个过程中，函数首先检查传入的源地址是否为NULL，然后遍历源结构体中的查询数据，为每个查询数据分配新的内存空间，并拷贝相应的字段值。最后，将拷贝后的查询数据存储在目标结构体中。
 ******************************************************************************/
static void	procstat_copy_data(void *dst, size_t size_dst, const void *src)
{
	/* 定义一个常量字符串，表示函数名称 */
	const char		*__function_name = "procstat_copy_data";

	/* 定义一些变量 */
	int			offset, *query_offset;
	zbx_procstat_header_t	*hdst = (zbx_procstat_header_t *)dst;
	zbx_procstat_query_t	*qsrc, *qdst = NULL;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 设置目标结构体的size字段 */
	hdst->size = size_dst;

	/* 设置目标结构体的size_allocated字段，这里使用了PROCSTAT_ALIGNED_HEADER_SIZE进行初始化 */
	hdst->size_allocated = PROCSTAT_ALIGNED_HEADER_SIZE;

	/* 设置目标结构体的queries字段为NULL，表示初始状态 */
	hdst->queries = PROCSTAT_NULL_OFFSET;

	/* 如果传入的src不为NULL，则进行以下操作 */
	if (NULL != src)
	{
		/* 定义一个指针，用于存储查询数据的地址 */
		query_offset = &hdst->queries;

		/* 遍历源结构体中的查询数据 */
		for (qsrc = PROCSTAT_QUERY_FIRST(src); NULL != qsrc; qsrc = PROCSTAT_QUERY_NEXT(src, qsrc))
		{
			/* 为新的共享内存段分配足够的空间 */
			offset = procstat_alloc(dst, sizeof(zbx_procstat_query_t));

			/* 创建一个新的查询数据结构体 */
			qdst = (zbx_procstat_query_t *)PROCSTAT_PTR(dst, offset);

			/* 拷贝查询数据 */
			memcpy(qdst, qsrc, sizeof(zbx_procstat_query_t));

			/* 拷贝查询数据的process_name字段 */
			qdst->procname = procstat_strdup(dst, PROCSTAT_PTR_NULL(src, qsrc->procname));

			/* 拷贝查询数据的username字段 */
			qdst->username = procstat_strdup(dst, PROCSTAT_PTR_NULL(src, qsrc->username));

			/* 拷贝查询数据的cmdline字段 */
			qdst->cmdline = procstat_strdup(dst, PROCSTAT_PTR_NULL(src, qsrc->cmdline));

			/* 更新查询数据的地址 */
			*query_offset = offset;
			query_offset = &qdst->next;
		}
	}

	/* 记录日志，表示函数执行结束 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

 * Function: procstat_copy_data                                               *
 *                                                                            *
 * Purpose: copies procstat data to a new shared memory segment               *
 *                                                                            *
 * Parameters: dst      - [OUT] the destination segment                       *
 *             size_dst - [IN] the size of destination segment                *
 *             src      - [IN] the source segment                             *
 *                                                                            *
 ******************************************************************************/
static void	procstat_copy_data(void *dst, size_t size_dst, const void *src)
{
	const char		*__function_name = "procstat_copy_data";

	int			offset, *query_offset;
	zbx_procstat_header_t	*hdst = (zbx_procstat_header_t *)dst;
	zbx_procstat_query_t	*qsrc, *qdst = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	hdst->size = size_dst;
	hdst->size_allocated = PROCSTAT_ALIGNED_HEADER_SIZE;
	hdst->queries = PROCSTAT_NULL_OFFSET;

	if (NULL != src)
	{
		query_offset = &hdst->queries;

		/* copy queries */
		for (qsrc = PROCSTAT_QUERY_FIRST(src); NULL != qsrc; qsrc = PROCSTAT_QUERY_NEXT(src, qsrc))
		{
			/* the new shared memory segment must have enough space */
			offset = procstat_alloc(dst, sizeof(zbx_procstat_query_t));

			qdst = (zbx_procstat_query_t *)PROCSTAT_PTR(dst, offset);

			memcpy(qdst, qsrc, sizeof(zbx_procstat_query_t));

			qdst->procname = procstat_strdup(dst, PROCSTAT_PTR_NULL(src, qsrc->procname));
			qdst->username = procstat_strdup(dst, PROCSTAT_PTR_NULL(src, qsrc->username));
			qdst->cmdline = procstat_strdup(dst, PROCSTAT_PTR_NULL(src, qsrc->cmdline));

			*query_offset = offset;
			query_offset = &qdst->next;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: procstat_running                                                 *
 *                                                                            *
 * Purpose: checks if processor statistics collector is running (at least one *
 *          one process statistics query has been made).                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是检查进程状态信息共享内存区域是否存在。如果存在，返回成功（SUCCEED）；如果不存在，返回失败（FAIL）。
 ******************************************************************************/
/* 定义一个函数：procstat_running，用于检查进程状态信息共享内存区域是否存在
 * 参数：无
 * 返回值：int，SUCCEED（成功）或FAIL（失败）
 */
static int	procstat_running(void)
{
	/* 判断 collector->procstat.shmid 是否为 ZBX_NONEXISTENT_SHMID，即进程状态信息共享内存区域是否存在
	 * 如果不存在，返回 FAIL（失败）
	 */
	if (ZBX_NONEXISTENT_SHMID == collector->procstat.shmid)
		return FAIL;

	/* 如果进程状态信息共享内存区域存在，返回 SUCCEED（成功） */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: procstat_get_query                                               *
 *                                                                            *
 * Purpose: get process statistics query based on process name, user name     *
/******************************************************************************
 * *
 *代码块主要目的是：提供一个`procstat_get_query`函数，用于根据进程名、用户名、命令行和标志查找进程状态查询结构体指针。如果找到匹配的进程状态查询，则返回该指针，否则返回NULL。
 ******************************************************************************/
// 定义一个静态函数，用于根据进程名、用户名、命令行和标志查找进程状态查询结构体指针
static zbx_procstat_query_t *procstat_get_query(void *base, const char *procname, const char *username,
                                                 const char *cmdline, zbx_uint64_t flags)
{
    // 定义一个指向进程状态查询结构体的指针
    zbx_procstat_query_t *query;

    // 检查进程状态查询服务是否运行
    if (SUCCEED != procstat_running())
    {
        // 如果进程状态查询服务未运行，则返回NULL
        return NULL;
    }

    // 遍历进程状态查询结构体链表
    for (query = PROCSTAT_QUERY_FIRST(base); NULL != query; query = PROCSTAT_QUERY_NEXT(base, query))
    {
        // 判断进程名、用户名、命令行和标志是否匹配
        if (0 == zbx_strcmp_null(procname, PROCSTAT_PTR_NULL(base, query->procname)) &&
                0 == zbx_strcmp_null(username, PROCSTAT_PTR_NULL(base, query->username)) &&
/******************************************************************************
 * *
 *这段代码的主要目的是用于向进程统计信息收集器中添加一个新的进程统计记录。代码首先检查收集器中的进程统计信息列表是否为空，然后为新的进程统计信息分配内存空间。接下来，代码为新分配的内存区域初始化进程统计查询结构体，并设置进程名称、用户名、命令行、标志等信息。最后，将新查询结构体添加到进程统计信息列表中。
 ******************************************************************************/
static void	procstat_add(const char *procname, const char *username, const char *cmdline, zbx_uint64_t flags)
{
	const char		*__function_name = "procstat_add";
	char			*errmsg = NULL;
	size_t			size = 0;
	zbx_procstat_query_t	*query;
	zbx_procstat_header_t	*header;
	int			query_offset;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 为新的进程统计信息分配内存空间 */
	// 1. 首先检查收集器中的进程统计信息列表是否为空，如果是，则预留进程头部的空间
	if (0 == collector->procstat.size)
		size += PROCSTAT_ALIGNED_HEADER_SIZE;

	// 2. 为进程属性预留空间
	if (NULL != procname)
		size += ZBX_SIZE_T_ALIGN8(strlen(procname) + 1);

	if (NULL != username)
		size += ZBX_SIZE_T_ALIGN8(strlen(username) + 1);

	if (NULL != cmdline)
		size += ZBX_SIZE_T_ALIGN8(strlen(cmdline) + 1);

	// 3. 调用 procstat_add() 时，共享内存引用已经过验证，无需调用 procstat_reattach()

	// 4. 为查询容器预留空间
	size += ZBX_SIZE_T_ALIGN8(sizeof(zbx_procstat_query_t));

	// 5. 检查 procstat_ref 指向的内存区域是否足够大，如果不够大，则重新计算所需空间
	if (NULL == procstat_ref.addr || FAIL == procstat_dshm_has_enough_space(procstat_ref.addr, size))
	{
		// 6. 重新计算存储现有数据 + 新查询所需的空间
		size += procstat_dshm_used_size(procstat_ref.addr);

		// 7. 如果内存分配失败，打印错误信息并退出程序
		if (FAIL == zbx_dshm_realloc(&collector->procstat, size, &errmsg))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot reallocate memory in process data collector: %s", errmsg);
			zbx_free(errmsg);
			zbx_dshm_unlock(&collector->procstat);

			exit(EXIT_FAILURE);
		}

		// 8. 调用 procstat_copy_data() 函数初始化新分配的内存区域，该函数会在 zbx_dshm_realloc() 回调中调用
		procstat_reattach();
	}

	header = (zbx_procstat_header_t *)procstat_ref.addr;

	query_offset = procstat_alloc(procstat_ref.addr, sizeof(zbx_procstat_query_t));

	// 9. 初始化新分配的查询结构体
	query = (zbx_procstat_query_t *)PROCSTAT_PTR_NULL(procstat_ref.addr, query_offset);

	memset(query, 0, sizeof(zbx_procstat_query_t));

	query->procname = procstat_strdup(procstat_ref.addr, procname);
	query->username = procstat_strdup(procstat_ref.addr, username);
	query->cmdline = procstat_strdup(procstat_ref.addr, cmdline);
	query->flags = flags;
	query->last_accessed = time(NULL);
	query->next = header->queries;
	header->queries = query_offset;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


	/* reserve space for query container */
	size += ZBX_SIZE_T_ALIGN8(sizeof(zbx_procstat_query_t));

	if (NULL == procstat_ref.addr || FAIL == procstat_dshm_has_enough_space(procstat_ref.addr, size))
	{
		/* recalculate the space required to store existing data + new query */
		size += procstat_dshm_used_size(procstat_ref.addr);

		if (FAIL == zbx_dshm_realloc(&collector->procstat, size, &errmsg))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot reallocate memory in process data collector: %s", errmsg);
			zbx_free(errmsg);
			zbx_dshm_unlock(&collector->procstat);

			exit(EXIT_FAILURE);
		}

		/* header initialised in procstat_copy_data() which is called back from zbx_dshm_realloc() */
		procstat_reattach();
	}

	header = (zbx_procstat_header_t *)procstat_ref.addr;

	query_offset = procstat_alloc(procstat_ref.addr, sizeof(zbx_procstat_query_t));

	/* initialize the created query */
	query = (zbx_procstat_query_t *)PROCSTAT_PTR_NULL(procstat_ref.addr, query_offset);

	memset(query, 0, sizeof(zbx_procstat_query_t));

	query->procname = procstat_strdup(procstat_ref.addr, procname);
	query->username = procstat_strdup(procstat_ref.addr, username);
	query->cmdline = procstat_strdup(procstat_ref.addr, cmdline);
	query->flags = flags;
	query->last_accessed = time(NULL);
	query->next = header->queries;
	header->queries = query_offset;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: procstat_free_query_data                                         *
 *                                                                            *
 * Purpose: frees the query data structure used to store queries locally      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放进程状态查询数据结构体所占用的内存。
 *
 *整个代码块的功能概括为：定义一个静态函数 procstat_free_query_data，接收一个 zbx_procstat_query_data_t 类型的指针作为参数。通过这个函数，首先摧毁名为 data->pids 的 uint64 类型向量，然后释放 data 指向的数据内存。这样就完成了对进程状态查询数据结构体的释放操作。
 ******************************************************************************/
// 定义一个静态函数，用于释放进程状态查询数据结构体
static void	// 声明一个名为 procstat_free_query_data 的静态函数
procstat_free_query_data(zbx_procstat_query_data_t *data) // 接收一个 zbx_procstat_query_data_t 类型的指针作为参数
{
	zbx_vector_uint64_destroy(&data->pids); // 摧毁名为 data->pids 的 uint64 类型向量
	zbx_free(data); // 释放 data 指向的数据内存
}


/******************************************************************************
 *                                                                            *
 * Function: procstat_try_compress                                            *
 *                                                                            *
 * Purpose: try to compress (remove inactive queries) the procstat shared     *
 *          memory segment once per day                                       *
 *                                                                            *
 * Parameters: base - [IN] the procstat shared memory segment                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对进程统计数据进行压缩。当收集器运行一定时间后（每秒钟更新一次），检查进程统计数据结构体的内存使用情况，如果内存使用大小小于预设的大小，则重新分配内存以实现数据压缩。如果在内存重新分配过程中出现错误，记录日志并退出程序。
 ******************************************************************************/
static void	procstat_try_compress(void *base)
{
    // 定义一个静态变量，用于记录收集器运行的轮次，每秒钟更新一次
    static int	collector_iteration = 0;

    /* 迭代计数器，表示收集器运行的时间，每秒钟更新一次 */
    /* 由于收集器数据收集每隔1秒进行一次，因此进行近似计算，避免频繁调用time()函数 */
    /* 如果未定义查询，则不进行时间计算 */
    if (0 == (++collector_iteration % PROCSTAT_COMPRESS_PERIOD))
    {
        zbx_procstat_header_t	*header = (zbx_procstat_header_t *)procstat_ref.addr;
        size_t			size;
        char			*errmsg = NULL;

        // 获取进程统计数据结构体的内存使用大小
        size = procstat_dshm_used_size(base);

        // 如果进程统计数据结构体的内存使用大小小于header中的大小，并且失败地进行重新分配内存
        if (size < header->size && FAIL == zbx_dshm_realloc(&collector->procstat, size, &errmsg))
/******************************************************************************
 * *
 *这段代码的主要目的是构建一个本地查询向量，用于存储进程统计数据。在整个代码块中，首先获取进程统计头信息，然后遍历查询链表，对每个查询进行处理。处理过程包括：移除未使用的查询、分配新的查询数据结构、保存查询属性、把查询数据添加到查询向量中、更新运行ID等。最后，压缩进程统计数据并解锁共享内存区域。整个代码块的输出是一个整数，表示查询标志。
 ******************************************************************************/
static int	procstat_build_local_query_vector(zbx_vector_ptr_t *queries_ptr, int runid)
{
	/* 声明一个zbx_procstat_header_t类型的指针header，用于指向进程统计的头信息 */
	zbx_procstat_header_t		*header;
	/* 声明一个time_t类型的变量now，用于存储当前时间 */
	time_t				now;
	/* 声明一个zbx_procstat_query_t类型的指针query，用于遍历进程统计查询 */
	zbx_procstat_query_t		*query;
	/* 声明一个zbx_procstat_query_data_t类型的指针qdata，用于存储查询数据 */
	zbx_procstat_query_data_t	*qdata;
	/* 声明一个int类型的变量flags，用于存储查询标志 */
	int				flags = ZBX_SYSINFO_PROC_NONE, *pNextQuery;

	/* 加锁保护共享内存区域，防止多个进程同时访问 */
	zbx_dshm_lock(&collector->procstat);

	/* 重新挂载进程统计头信息，确保其正确性 */
	procstat_reattach();

	/* 获取进程统计头信息的地址 */
	header = (zbx_procstat_header_t *)procstat_ref.addr;

	/* 如果头信息中的查询为空，说明没有查询，直接退出 */
	if (PROCSTAT_NULL_OFFSET == header->queries)
		goto out;

	/* 设置查询标志为进程ID */
	flags = ZBX_SYSINFO_PROC_PID;

	/* 获取当前时间 */
	now = time(NULL);
	/* 声明一个指向下一个查询的指针，初始值为头信息的查询链表头 */
	pNextQuery = &header->queries;

	/* 遍历进程统计查询 */
	for (query = PROCSTAT_QUERY_FIRST(procstat_ref.addr); NULL != query;
			query = PROCSTAT_QUERY_NEXT(procstat_ref.addr, query))
	{
		/* 移除未使用的查询，释放其占用的内存，但数据仍然分配，直到下一次重新大小调整 */
		if (PROCSTAT_MAX_INACTIVITY_PERIOD < now - query->last_accessed)
		{
			*pNextQuery = query->next;
			continue;
		}

		/* 分配一个新的查询数据结构 */
		qdata = (zbx_procstat_query_data_t *)zbx_malloc(NULL, sizeof(zbx_procstat_query_data_t));
		/* 创建一个uint64类型的向量，用于存储进程ID */
		zbx_vector_uint64_create(&qdata->pids);

		/* 保存查询属性的引用，确保其在调用process_reattach()之前仍然有效 */
		if (NULL != (qdata->procname = PROCSTAT_PTR_NULL(procstat_ref.addr, query->procname)))
			flags |= ZBX_SYSINFO_PROC_NAME;

		if (NULL != (qdata->username = PROCSTAT_PTR_NULL(procstat_ref.addr, query->username)))
			flags |= ZBX_SYSINFO_PROC_USER;

		if (NULL != (qdata->cmdline = PROCSTAT_PTR_NULL(procstat_ref.addr, query->cmdline)))
			flags |= ZBX_SYSINFO_PROC_CMDLINE;

		qdata->flags = query->flags;
		qdata->utime = 0;
		qdata->stime = 0;
		qdata->error = 0;

		/* 把查询数据添加到查询向量中 */
		zbx_vector_ptr_append(queries_ptr, qdata);

		/* 查询的顺序只能由收集器自身更改（当删除旧的查询时），但在统计数据收集过程中，共享内存解锁，其他进程可能在活跃查询列表的末尾插入查询。*/
		/* 用运行ID标记当前数据收集周期正在处理的查询，运行ID在每次数据收集周期结束时递增。我们可以确保本地副本与具有相同运行ID的共享内存中的查询相匹配。*/
		query->runid = runid;

		/* 更新指向下一个查询的指针 */
		pNextQuery = &query->next;
	}

out:
	/* 压缩进程统计数据 */
	procstat_try_compress(procstat_ref.addr);

	/* 解锁共享内存区域 */
	zbx_dshm_unlock(&collector->procstat);

	/* 返回查询标志 */
	return flags;
}

		if (NULL != (qdata->username = PROCSTAT_PTR_NULL(procstat_ref.addr, query->username)))
			flags |= ZBX_SYSINFO_PROC_USER;

		if (NULL != (qdata->cmdline = PROCSTAT_PTR_NULL(procstat_ref.addr, query->cmdline)))
			flags |= ZBX_SYSINFO_PROC_CMDLINE;

		qdata->flags = query->flags;
		qdata->utime = 0;
		qdata->stime = 0;
		qdata->error = 0;

		zbx_vector_ptr_append(queries_ptr, qdata);

		/* The order of queries can be changed only by collector itself (when removing old    */
		/* queries), but during statistics gathering the shared memory is unlocked and other  */
/******************************************************************************
 * *
 *整个代码块的主要目的是计算查询的进程的CPU利用率。具体步骤如下：
 *
 *1. 遍历查询列表，获取每个查询的进程列表。
 *2. 遍历进程列表，分别计算每个进程在当前和上一时刻的CPU利用率。
 *3. 将计算得到的CPU利用率累加到对应的查询数据结构体中。
 *
 *输出：
 *
 *```
 *static void procstat_calculate_cpu_util_for_queries(zbx_vector_ptr_t *queries,
 *                                                 zbx_vector_uint64_t *pids, const zbx_procstat_util_t *stats)
 *{
 *    // 定义一个指向zbx_procstat_query_data_t结构体的指针
 *    zbx_procstat_query_data_t *qdata;
 *    // 定义一个指向zbx_procstat_util_t结构体的指针
 *    zbx_procstat_util_t *putil;
 *    // 定义两个循环变量，分别用于遍历查询和进程列表
 *    int j, i;
 *
 *    // 遍历查询列表
 *    for (j = 0; j < queries->values_num; j++)
 *    {
 *        // 获取查询列表中的一个元素，该元素是一个zbx_procstat_query_data_t结构体指针
 *        qdata = (zbx_procstat_query_data_t *)queries->values[j];
 *
 *        // 遍历进程列表
 *        for (i = 0; i < qdata->pids.values_num; i++)
 *        {
 *            // 定义三个zbx_uint64_t类型的变量，分别用于存储进程的starttime、utime和stime
 *            zbx_uint64_t starttime, utime, stime;
 *            // 定义一个zbx_procstat_util_t类型的变量，用于存储进程的CPU利用率
 *            zbx_procstat_util_t util_local;
 *
 *            // 设置util_local的pid为当前进程的pid
 *            util_local.pid = qdata->pids.values[i];
 *
 *            // 在当前进程利用率快照中查找进程利用率数据
 *            putil = (zbx_procstat_util_t *)zbx_bsearch(&util_local, stats, pids->values_num,
 *                                                       sizeof(zbx_procstat_util_t), procstat_util_compare);
 *
 *            // 如果找不到或查找失败，继续下一个进程
 *            if (NULL == putil || SUCCEED != putil->error)
 *                continue;
 *
 *            // 保存当前进程的utime和stime
 *            utime = putil->utime;
 *            stime = putil->stime;
 *
 *            // 保存当前进程的starttime
 *            starttime = putil->starttime;
 *
 *            // 在上一时刻的进程利用率快照中查找进程利用率数据
 *            putil = (zbx_procstat_util_t *)zbx_bsearch(&util_local, procstat_snapshot, procstat_snapshot_num,
 *                                                       sizeof(zbx_procstat_util_t), procstat_util_compare);
 *
 *            // 如果找不到或查找失败，或者starttime不匹配，继续下一个进程
 *            if (NULL == putil || SUCCEED != putil->error || putil->starttime != starttime)
 *                continue;
 *
 *            // 计算CPU利用率，并将结果累加到qdata中
 *            qdata->utime += utime - putil->utime;
 *            qdata->stime += stime - putil->stime;
 *        }
 *    }
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数，用于计算查询的进程的CPU利用率
static void procstat_calculate_cpu_util_for_queries(zbx_vector_ptr_t *queries,
                                                 zbx_vector_uint64_t *pids, const zbx_procstat_util_t *stats)
{
    // 定义一个指向zbx_procstat_query_data_t结构体的指针
    zbx_procstat_query_data_t *qdata;
    // 定义一个指向zbx_procstat_util_t结构体的指针
    zbx_procstat_util_t *putil;
    // 定义两个循环变量，分别用于遍历查询和进程列表
    int j, i;

    // 遍历查询列表
    for (j = 0; j < queries->values_num; j++)
    {
        // 获取查询列表中的一个元素，该元素是一个zbx_procstat_query_data_t结构体指针
        qdata = (zbx_procstat_query_data_t *)queries->values[j];

        // 遍历进程列表
        for (i = 0; i < qdata->pids.values_num; i++)
        {
            // 定义三个zbx_uint64_t类型的变量，分别用于存储进程的starttime、utime和stime
            zbx_uint64_t starttime, utime, stime;
            // 定义一个zbx_procstat_util_t类型的变量，用于存储进程的CPU利用率
            zbx_procstat_util_t util_local;

            // 设置util_local的pid为当前进程的pid
            util_local.pid = qdata->pids.values[i];

            // 在当前进程利用率快照中查找进程利用率数据
            putil = (zbx_procstat_util_t *)zbx_bsearch(&util_local, stats, pids->values_num,
                                                       sizeof(zbx_procstat_util_t), procstat_util_compare);

            // 如果找不到或查找失败，继续下一个进程
            if (NULL == putil || SUCCEED != putil->error)
                continue;

            // 保存当前进程的utime和stime
            utime = putil->utime;
            stime = putil->stime;

            // 保存当前进程的starttime
            starttime = putil->starttime;

            // 在上一时刻的进程利用率快照中查找进程利用率数据
            putil = (zbx_procstat_util_t *)zbx_bsearch(&util_local, procstat_snapshot, procstat_snapshot_num,
                                                       sizeof(zbx_procstat_util_t), procstat_util_compare);

            // 如果找不到或查找失败，或者starttime不匹配，继续下一个进程
            if (NULL == putil || SUCCEED != putil->error || putil->starttime != starttime)
                continue;

            // 计算CPU利用率，并将结果累加到qdata中
            qdata->utime += utime - putil->utime;
            qdata->stime += stime - putil->stime;
        }
    }
}

		// 获取当前查询项指针
		qdata = (zbx_procstat_query_data_t *)queries->values[i];

		// 获取与当前查询项匹配的进程ID列表
		zbx_proc_get_matching_pids(processes, qdata->procname, qdata->username, qdata->cmdline, qdata->flags,
				&qdata->pids);

		// 累加匹配到的进程ID数量
		pids_num += qdata->pids.values_num;
	}

	// 返回匹配到的进程ID总数
	return pids_num;
/******************************************************************************
 * 
 ******************************************************************************/
/* 静态函数 procstat_get_monitored_pids，接收以下参数：
 * zbx_vector_uint64_t 类型的指针 pids，用于存储进程 ID 列表；
 * zbx_vector_ptr_t 类型的指针 queries，用于存储查询信息；
 * int 类型的变量 pids_num，表示进程 ID 列表的长度。
 *
 * 该函数的主要目的是从 queries  vector 中获取监控的进程 ID 列表，并将它们存储在 pids vector 中。
 * 函数首先为 pids vector 分配足够的空间，然后遍历 queries vector 中的每个元素。
 * 对于每个元素，检查其 qdata 是否包含错误，如果不含错误，则将该元素的进程 ID 复制到 pids vector 中。
 * 最后，对 pids vector 进行排序和去重操作。
 */
static void	procstat_get_monitored_pids(zbx_vector_uint64_t *pids, const zbx_vector_ptr_t *queries, int pids_num)
{
	zbx_procstat_query_data_t	*qdata; // 定义一个指向 zbx_procstat_query_data_t 类型的指针 qdata
	int				i; // 定义一个整型变量 i，用于循环计数

	zbx_vector_uint64_reserve(pids, pids_num); // 为 pids vector 分配足够的空间

	for (i = 0; i < queries->values_num; i++) // 遍历 queries vector 中的每个元素
	{
		qdata = (zbx_procstat_query_data_t *)queries->values[i]; // 获取当前元素的指针

		if (SUCCEED != qdata->error) // 如果 qdata 包含错误
			continue; // 跳过当前元素

		memcpy(pids->values + pids->values_num, qdata->pids.values, // 将 qdata->pids.values 中的进程 ID 复制到 pids vector
				sizeof(zbx_uint64_t) * qdata->pids.values_num);
		pids->values_num += qdata->pids.values_num; // 更新 pids vector 的长度
	}

	zbx_vector_uint64_sort(pids, ZBX_DEFAULT_UINT64_COMPARE_FUNC); // 对 pids vector 进行排序
	zbx_vector_uint64_uniq(pids, ZBX_DEFAULT_UINT64_COMPARE_FUNC); // 对 pids vector 进行去重
}

		qdata = (zbx_procstat_query_data_t *)queries->values[i];

		if (SUCCEED != qdata->error)
			continue;

		memcpy(pids->values + pids->values_num, qdata->pids.values,
				sizeof(zbx_uint64_t) * qdata->pids.values_num);
		pids->values_num += qdata->pids.values_num;
	}

	zbx_vector_uint64_sort(pids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(pids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: procstat_get_cpu_util_snapshot_for_pids                          *
 *                                                                            *
 * Purpose: gets cpu utilization data snapshot for the monitored processes    *
 *                                                                            *
 * Parameters: stats - [OUT] current reading of the per-pid cpu usage         *
 *                               statistics (array, items correspond to pids) *
 *             pids  - [IN]  pids (unique) for which to collect data in this  *
 *                               iteration                                    *
 *                                                                            *
 * Return value: timestamp of the snapshot                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：获取指定进程ID列表的CPU利用率快照，并将快照的时间戳存储在snapshot_timestamp中。为实现这个目的，代码首先遍历进程ID列表，然后调用zbx_proc_get_process_stats函数获取进程统计信息，接着获取当前时间戳，并将时间戳作为快照返回。
 ******************************************************************************/
// 定义一个静态函数，用于获取进程CPU利用率快照的数据结构
static zbx_timespec_t	procstat_get_cpu_util_snapshot_for_pids(zbx_procstat_util_t *stats,
                                                             zbx_vector_uint64_t *pids)
{
    // 定义一个时间戳结构体变量snapshot_timestamp，用于存储快照的时间戳
    zbx_timespec_t	snapshot_timestamp;
    // 定义一个整型变量i，用于循环计数
    int		i;

    // 遍历pids中的进程ID列表
    for (i = 0; i < pids->values_num; i++)
    {
        // 将进程ID赋值给stats数组中的当前元素
        stats[i].pid = pids->values[i];
    }

    // 调用zbx_proc_get_process_stats函数，获取进程统计信息
    zbx_proc_get_process_stats(stats, pids->values_num);

    // 获取当前时间戳
    zbx_timespec(&snapshot_timestamp);

    // 返回快照的时间戳
    return snapshot_timestamp;
}


/******************************************************************************
 *                                                                            *
 * Function: procstat_util_compare                                            *
 *                                                                            *
 * Purpose: compare process utilization data by their pids                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个进程统计数据结构体（zbx_procstat_util_t）的大小，如果两个进程的 pid 字段不相等，则返回 1，表示不相等；如果两个进程的 pid 字段相等，则返回 0，表示等于。这个函数可以用于进程排序或其他需要比较进程统计数据的场景。
 ******************************************************************************/
// 定义一个名为 procstat_util_compare 的静态函数，该函数用于比较两个进程统计数据结构体 zbx_procstat_util_t 的大小
static int	procstat_util_compare(const void *d1, const void *d2)
{
	// 将传入的指针 d1 和 d2 分别转换为 zbx_procstat_util_t 类型的指针 u1 和 u2
	const zbx_procstat_util_t	*u1 = (zbx_procstat_util_t *)d1;
	const zbx_procstat_util_t	*u2 = (zbx_procstat_util_t *)d2;

	// 判断 u1 和 u2 指向的进程统计数据结构体中的 pid 字段是否相等，如果不相等，则返回 1（表示不相等）
	ZBX_RETURN_IF_NOT_EQUAL(u1->pid, u2->pid);

	// 如果 u1 和 u2 指向的进程统计数据结构体中的 pid 字段相等，则返回 0（表示等于）
	return 0;
}


/******************************************************************************
 *                                                                            *
 * Function: procstat_calculate_cpu_util_for_queries                          *
 *                                                                            *
 * Purpose: calculates the cpu utilization for queries since the previous     *
 *          snapshot                                                          *
 *                                                                            *
 * Parameters: queries - [IN/OUT] local, working copy of queries, saving      *
 *                                utime, stime and error                      *
 *             pids    - [IN] pids (unique) for which to collect data in      *
 *                            this iteration                                  *
 *             stats   - [IN] current reading of the per-pid cpu usage        *
 *                            statistics (array, items correspond to pids)    *
 *                                                                            *
 ******************************************************************************/
static void	procstat_calculate_cpu_util_for_queries(zbx_vector_ptr_t *queries,
			zbx_vector_uint64_t *pids, const zbx_procstat_util_t *stats)
{
	zbx_procstat_query_data_t	*qdata;
	zbx_procstat_util_t		*putil;
	int				j, i;

	for (j = 0; j < queries->values_num; j++)
	{
		qdata = (zbx_procstat_query_data_t *)queries->values[j];

		/* sum the cpu utilization for processes that are present in current */
		/* and last process cpu utilization snapshot                         */
		for (i = 0; i < qdata->pids.values_num; i++)
		{
/******************************************************************************
 * *
 *整个代码块的主要目的是更新进程统计查询数据。这段代码接收三个参数：一个指向查询数据的指针、一个运行ID和一个时间戳。它遍历进程统计查询，比较运行ID，然后更新查询数据的历史记录。在更新过程中，它还会检查查询数据的错误状态，如果错误，则跳过此查询。最后，它释放共享内存区域的锁。
 ******************************************************************************/
static void	procstat_update_query_statistics(zbx_vector_ptr_t *queries, int runid,
                                            const zbx_timespec_t *snapshot_timestamp)
{
    // 定义指向查询数据的指针
    zbx_procstat_query_t		*query;
    zbx_procstat_query_data_t	*qdata;
    int				index;
    int				i;

    // 加锁保护共享内存区域
    zbx_dshm_lock(&collector->procstat);

    // 重新挂载进程统计数据
    procstat_reattach();

    // 遍历进程统计查询
    for (query = PROCSTAT_QUERY_FIRST(procstat_ref.addr), i = 0; NULL != query;
            query = PROCSTAT_QUERY_NEXT(procstat_ref.addr, query))
    {
        // 如果运行ID不匹配，跳过此查询
        if (runid != query->runid)
            continue;

        // 如果查询数据已遍历完，报错并退出
        if (i >= queries->values_num)
        {
            THIS_SHOULD_NEVER_HAPPEN;
            break;
        }

        // 获取查询数据
        qdata = (zbx_procstat_query_data_t *)queries->values[i++];

        // 如果查询错误，跳过此查询
        if (SUCCEED != (query->error = qdata->error))
            continue;

        // 查找下一个历史数据槽
        if (0 < query->h_count)
        {
            if (MAX_COLLECTOR_HISTORY <= (index = query->h_first + query->h_count - 1))
                index -= MAX_COLLECTOR_HISTORY;

            // 更新查询数据的用户和系统时间
            qdata->utime += query->h_data[index].utime;
            qdata->stime += query->h_data[index].stime;

            // 查找下一个历史数据槽
            if (MAX_COLLECTOR_HISTORY <= ++index)
                index -= MAX_COLLECTOR_HISTORY;
        }
        else
            index = 0;

        // 如果达到最大历史记录数，更新查询数据
        if (MAX_COLLECTOR_HISTORY == query->h_count)
        {
            if (MAX_COLLECTOR_HISTORY <= ++query->h_first)
                query->h_first = 0;
        }
        else
            query->h_count++;

        // 保存更新后的历史数据
        query->h_data[index].utime = qdata->utime;
        query->h_data[index].stime = qdata->stime;
        query->h_data[index].timestamp = *snapshot_timestamp;
    }

    // 解锁共享内存区域
    zbx_dshm_unlock(&collector->procstat);
}


	for (query = PROCSTAT_QUERY_FIRST(procstat_ref.addr), i = 0; NULL != query;
			query = PROCSTAT_QUERY_NEXT(procstat_ref.addr, query))
	{
		if (runid != query->runid)
			continue;

		if (i >= queries->values_num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			break;
		}

		qdata = (zbx_procstat_query_data_t *)queries->values[i++];

		if (SUCCEED != (query->error = qdata->error))
			continue;

		/* find the next history data slot */
		if (0 < query->h_count)
		{
			if (MAX_COLLECTOR_HISTORY <= (index = query->h_first + query->h_count - 1))
				index -= MAX_COLLECTOR_HISTORY;

			qdata->utime += query->h_data[index].utime;
			qdata->stime += query->h_data[index].stime;

			if (MAX_COLLECTOR_HISTORY <= ++index)
				index -= MAX_COLLECTOR_HISTORY;
		}
		else
			index = 0;

		if (MAX_COLLECTOR_HISTORY == query->h_count)
		{
			if (MAX_COLLECTOR_HISTORY <= ++query->h_first)
				query->h_first = 0;
		}
		else
			query->h_count++;

		query->h_data[index].utime = qdata->utime;
		query->h_data[index].stime = qdata->stime;
		query->h_data[index].timestamp = *snapshot_timestamp;
	}

	zbx_dshm_unlock(&collector->procstat);
}

/*
 * Public API
 */

/******************************************************************************
 *                                                                            *
 * Function: zbx_procstat_collector_started                                   *
 *                                                                            *
 * Purpose: checks if processor statistics collector is enabled (the main     *
 *          collector has been initialized)                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查收集器（collector）是否已经启动。函数 `zbx_procstat_collector_started` 接受一个空参数，并在内部判断 collector 指针是否为空。如果 collector 为空，则表示未启动，返回 FAIL；如果 collector 不为空，表示已启动，返回 SUCCEED。
 ******************************************************************************/
// 定义一个函数：zbx_procstat_collector_started，该函数用于检查收集器（collector）是否已经启动
int zbx_procstat_collector_started(void)
{
    // 判断 collector 指针是否为空（NULL）
    if (NULL == collector)
        // 如果 collector 为空，返回 FAIL（表示失败）
        return FAIL;

    // 如果 collector 不为空，返回 SUCCEED（表示成功）
    return SUCCEED;
}


/******************************************************************************
 * *
 *代码块主要目的是初始化进程数据收集器，创建一个共享内存区域用于存储进程统计数据，并设置相应的互斥锁和数据复制函数。如果初始化失败，打印错误信息并终止程序运行。
 ******************************************************************************/
void zbx_procstat_init(void)
{
    // 定义一个字符指针变量 errmsg，用于存储错误信息
    char *errmsg = NULL;

    // 使用 zbx_dshm_create 函数创建一个共享内存区域，用于存储进程统计数据
    // 参数1：collector->procstat，指向进程统计数据的指针
    // 参数2：0，表示共享内存区域的初始化标志
    // 参数3：ZBX_MUTEX_PROCSTAT，表示共享内存区域的名称
    // 参数4：procstat_copy_data，指向数据复制函数的指针
    // 参数5：&errmsg，用于存储错误信息的指针
    if (SUCCEED != zbx_dshm_create(&collector->procstat, 0, ZBX_MUTEX_PROCSTAT,
                                     procstat_copy_data, &errmsg))
    {
        // 如果zbx_dshm_create函数执行失败，打印错误信息并释放errmsg内存
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize process data collector: %s", errmsg);
        zbx_free(errmsg);
        // 终止程序运行，返回失败退出码
        exit(EXIT_FAILURE);
    }

    // 初始化进程统计数据的引用，将其设置为无效值
    procstat_ref.shmid = ZBX_NONEXISTENT_SHMID;
    procstat_ref.addr = NULL;
}

	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize process data collector: %s", errmsg);
		zbx_free(errmsg);
		exit(EXIT_FAILURE);
	}

	procstat_ref.shmid = ZBX_NONEXISTENT_SHMID;
	procstat_ref.addr = NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_procstat_destroy                                             *
 *                                                                            *
 * Purpose: destroys process statistics collector                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是销毁进程状态数据收集器。函数zbx_procstat_destroy()负责完成这个任务。在函数内部，首先尝试使用zbx_dshm_destroy()函数销毁进程状态数据收集器，如果失败，则记录日志并打印错误信息。接着，设置进程状态数据收集器的共享内存标识符为不存在，地址为空指针，完成销毁过程。
 ******************************************************************************/
void	zbx_procstat_destroy(void) // 定义一个函数，用于销毁进程状态数据收集器
{
	char	*errmsg = NULL; // 声明一个字符指针，用于存储错误信息

	if (SUCCEED != zbx_dshm_destroy(&collector->procstat, &errmsg)) // 调用zbx_dshm_destroy函数，尝试销毁进程状态数据收集器
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot free resources allocated by process data collector: %s", errmsg); // 如果销毁失败，记录日志并打印错误信息
		zbx_free(errmsg); // 释放错误信息内存
	}

	procstat_ref.shmid = ZBX_NONEXISTENT_SHMID; // 设置进程状态数据收集器的共享内存标识符为不存在
	procstat_ref.addr = NULL; // 设置进程状态数据收集器的地址为空指针
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_procstat_get_util`的函数，该函数接收6个参数，分别表示进程名、用户名、命令行、标志、采集周期和类型。函数的主要任务是根据这些参数查询进程的CPU利用率，并将结果存储在`value`指针指向的内存区域中。如果查询过程中出现错误，函数会输出相应的错误信息。
 ******************************************************************************/
// 定义一个函数zbx_procstat_get_util，接收6个参数，分别表示进程名、用户名、命令行、标志、采集周期和类型。
// 函数返回值表示操作是否成功，输出值是一个双精度浮点数，表示CPU利用率，errmsg是一个字符指针，表示错误信息。
int zbx_procstat_get_util(const char *procname, const char *username, const char *cmdline, zbx_uint64_t flags,
                            int period, int type, double *value, char **errmsg)
{
	// 定义变量，包括返回值、当前时间、开始时间等
	int			ret = FAIL, current, start;
	zbx_procstat_query_t	*query;
	zbx_uint64_t		ticks_diff = 0, time_diff;

	// 加锁保护共享内存区域
	zbx_dshm_lock(&collector->procstat);

	// 重新挂载进程状态查询结构体
	procstat_reattach();

	// 查询进程状态
	if (NULL == (query = procstat_get_query(procstat_ref.addr, procname, username, cmdline, flags)))
	{
		// 如果查询数量达到上限，输出错误信息
		if (procstat_queries_num(procstat_ref.addr) == PROCSTAT_MAX_QUERIES)
			*errmsg = zbx_strdup(*errmsg, "Maximum number of queries reached.");
		else
			// 否则，添加新的进程状态查询
			procstat_add(procname, username, cmdline, flags);

		// 结束函数
		goto out;
	}

	// 更新查询的最近访问时间
	query->last_accessed = time(NULL);

	// 如果查询有错误，输出错误信息
	if (0 != query->error)
	{
		*errmsg = zbx_dsprintf(*errmsg, "Cannot read cpu utilization data: %s", zbx_strerror(-query->error));
		goto out;
	}

	// 如果查询的进程历史记录数量大于1，继续执行
	if (1 >= query->h_count)
		goto out;

	// 计算周期范围
	if (period >= query->h_count)
		period = query->h_count - 1;

	// 计算当前时间范围
	if (MAX_COLLECTOR_HISTORY <= (current = query->h_first + query->h_count - 1))
		current -= MAX_COLLECTOR_HISTORY;

	// 计算开始时间范围
	if (0 > (start = current - period))
		start += MAX_COLLECTOR_HISTORY;

	// 如果是用户态CPU利用率，计算差值
	if (0 != (type & ZBX_PROCSTAT_CPU_USER))
		ticks_diff += query->h_data[current].utime - query->h_data[start].utime;

	// 如果是系统态CPU利用率，计算差值
	if (0 != (type & ZBX_PROCSTAT_CPU_SYSTEM))
		ticks_diff += query->h_data[current].stime - query->h_data[start].stime;

	// 计算时间差
	time_diff = (zbx_uint64_t)(query->h_data[current].timestamp.sec - query->h_data[start].timestamp.sec) *
			1000000000 + query->h_data[current].timestamp.ns - query->h_data[start].timestamp.ns;

	/* 1e9 (nanoseconds) * 1e2 (percent) * 1e1 (one digit decimal place) */
	ticks_diff *= __UINT64_C(1000000000000);
#ifdef HAVE_ROUND
	*value = round((double)ticks_diff / (time_diff * sysconf(_SC_CLK_TCK))) / 10;
#else
	*value = (int)((double)ticks_diff / (time_diff * sysconf(_SC_CLK_TCK)) + 0.5) / 10.0;
#endif

	// 更新返回值
	ret = SUCCEED;
out:
	// 解锁共享内存区域
	zbx_dshm_unlock(&collector->procstat);

	return ret;
}

	if (MAX_COLLECTOR_HISTORY <= (current = query->h_first + query->h_count - 1))
		current -= MAX_COLLECTOR_HISTORY;

	if (0 > (start = current - period))
		start += MAX_COLLECTOR_HISTORY;

	if (0 != (type & ZBX_PROCSTAT_CPU_USER))
		ticks_diff += query->h_data[current].utime - query->h_data[start].utime;

	if (0 != (type & ZBX_PROCSTAT_CPU_SYSTEM))
		ticks_diff += query->h_data[current].stime - query->h_data[start].stime;

	time_diff = (zbx_uint64_t)(query->h_data[current].timestamp.sec - query->h_data[start].timestamp.sec) *
			1000000000 + query->h_data[current].timestamp.ns - query->h_data[start].timestamp.ns;

	/* 1e9 (nanoseconds) * 1e2 (percent) * 1e1 (one digit decimal place) */
	ticks_diff *= __UINT64_C(1000000000000);
#ifdef HAVE_ROUND
	*value = round((double)ticks_diff / (time_diff * sysconf(_SC_CLK_TCK))) / 10;
#else
	*value = (int)((double)ticks_diff / (time_diff * sysconf(_SC_CLK_TCK)) + 0.5) / 10.0;
#endif

/******************************************************************************
 * *
 *这块代码的主要目的是收集系统上进程的cpu使用情况统计数据。整个代码块分为以下几个部分：
 *
 *1. 初始化必要的数据结构，如查询vector、进程vector和pid vector。
 *2. 构建本地查询vector，并根据runid递增。
 *3. 从系统中获取所有进程的数据。
 *4. 根据查询vector和进程vector计算pid数量。
 *5. 获取pid的cpu使用情况统计数据。
 *6. 计算每个查询的cpu使用情况。
 *7. 更新查询统计数据。
 *8. 释放不再使用的内存空间。
 *9. 递增runid，为下一轮收集做准备。
 *
 *整个代码块的输出结果是更新了进程的cpu使用情况统计数据。
 ******************************************************************************/
void	zbx_procstat_collect(void)
{
	/* 标识当前收集迭代 */
	静态 int			runid = 1;

	/* 匹配查询的非重复pid数量 */
	int				pids_num = 0;

	/* 指定要获取的过程属性的标志 */
	int				flags;

	/* 本地工作查询副本 */
	zbx_vector_ptr_t		queries;

	/* 系统上所有进程的数据 */
	zbx_vector_ptr_t		processes;

	/* 当前迭代要收集数据的pid列表 */
	zbx_vector_uint64_t		pids;

	/* 每个pid的cpu使用情况统计数据（数组，项对应pid） */
	zbx_procstat_util_t		*stats;

	/* cpu使用情况快照的时间戳 */
	zbx_timespec_t			snapshot_timestamp;

	if (FAIL == zbx_procstat_collector_started() || FAIL == procstat_running())
		goto out;

	zbx_vector_ptr_create(&queries);
	zbx_vector_ptr_create(&processes);
	zbx_vector_uint64_create(&pids);

	if (ZBX_SYSINFO_PROC_NONE == (flags = procstat_build_local_query_vector(&queries, runid)))
		goto clean;

	if (SUCCEED != zbx_proc_get_processes(&processes, flags))
		goto clean;

	pids_num = procstat_scan_query_pids(&queries, &processes);

	procstat_get_monitored_pids(&pids, &queries, pids_num);

	stats = (zbx_procstat_util_t *)zbx_malloc(NULL, sizeof(zbx_procstat_util_t) * pids.values_num);
	snapshot_timestamp = procstat_get_cpu_util_snapshot_for_pids(stats, &pids);

	procstat_calculate_cpu_util_for_queries(&queries, &pids, stats);

	procstat_update_query_statistics(&queries, runid, &snapshot_timestamp);

	/* 替换当前快照为新统计数据 */
	zbx_free(procstat_snapshot);
	procstat_snapshot = stats;
	procstat_snapshot_num = pids.values_num;
clean:
	zbx_vector_uint64_destroy(&pids);

	zbx_proc_free_processes(&processes);
	zbx_vector_ptr_destroy(&processes);

	zbx_vector_ptr_clear_ext(&queries, (zbx_mem_free_func_t)procstat_free_query_data);
	zbx_vector_ptr_destroy(&queries);
out:
	runid++;
}

	zbx_free(procstat_snapshot);
	procstat_snapshot = stats;
	procstat_snapshot_num = pids.values_num;
clean:
	zbx_vector_uint64_destroy(&pids);

	zbx_proc_free_processes(&processes);
	zbx_vector_ptr_destroy(&processes);

	zbx_vector_ptr_clear_ext(&queries, (zbx_mem_free_func_t)procstat_free_query_data);
	zbx_vector_ptr_destroy(&queries);
out:
	runid++;
}

#endif
