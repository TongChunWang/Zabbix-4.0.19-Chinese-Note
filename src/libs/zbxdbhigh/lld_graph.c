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

#include "lld.h"
#include "db.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxserver.h"

typedef struct
{
	zbx_uint64_t		graphid;
	char			*name;
	char			*name_orig;
	zbx_uint64_t		ymin_itemid;
	zbx_uint64_t		ymax_itemid;
	zbx_vector_ptr_t	gitems;
#define ZBX_FLAG_LLD_GRAPH_UNSET			__UINT64_C(0x00000000)
#define ZBX_FLAG_LLD_GRAPH_DISCOVERED			__UINT64_C(0x00000001)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_NAME			__UINT64_C(0x00000002)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_WIDTH			__UINT64_C(0x00000004)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_HEIGHT		__UINT64_C(0x00000008)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_YAXISMIN		__UINT64_C(0x00000010)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_YAXISMAX		__UINT64_C(0x00000020)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_WORK_PERIOD	__UINT64_C(0x00000040)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_TRIGGERS		__UINT64_C(0x00000080)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_GRAPHTYPE		__UINT64_C(0x00000100)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_LEGEND		__UINT64_C(0x00000200)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_3D		__UINT64_C(0x00000400)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_PERCENT_LEFT		__UINT64_C(0x00000800)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_PERCENT_RIGHT		__UINT64_C(0x00001000)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_YMIN_TYPE		__UINT64_C(0x00002000)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID		__UINT64_C(0x00004000)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_YMAX_TYPE		__UINT64_C(0x00008000)
#define ZBX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID		__UINT64_C(0x00010000)
#define ZBX_FLAG_LLD_GRAPH_UPDATE									\
		(ZBX_FLAG_LLD_GRAPH_UPDATE_NAME | ZBX_FLAG_LLD_GRAPH_UPDATE_WIDTH |			\
		ZBX_FLAG_LLD_GRAPH_UPDATE_HEIGHT | ZBX_FLAG_LLD_GRAPH_UPDATE_YAXISMIN |			\
		ZBX_FLAG_LLD_GRAPH_UPDATE_YAXISMAX | ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_WORK_PERIOD |	\
		ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_TRIGGERS | ZBX_FLAG_LLD_GRAPH_UPDATE_GRAPHTYPE |		\
		ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_LEGEND | ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_3D |		\
		ZBX_FLAG_LLD_GRAPH_UPDATE_PERCENT_LEFT | ZBX_FLAG_LLD_GRAPH_UPDATE_PERCENT_RIGHT |	\
		ZBX_FLAG_LLD_GRAPH_UPDATE_YMIN_TYPE | ZBX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID |		\
		ZBX_FLAG_LLD_GRAPH_UPDATE_YMAX_TYPE | ZBX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID)
	zbx_uint64_t		flags;
}
zbx_lld_graph_t;

typedef struct
{
	zbx_uint64_t		gitemid;
	zbx_uint64_t		itemid;
	char			*color;
	int			sortorder;
	unsigned char		drawtype;
	unsigned char		yaxisside;
	unsigned char		calc_fnc;
	unsigned char		type;
#define ZBX_FLAG_LLD_GITEM_UNSET			__UINT64_C(0x0000)
#define ZBX_FLAG_LLD_GITEM_DISCOVERED			__UINT64_C(0x0001)
#define ZBX_FLAG_LLD_GITEM_UPDATE_ITEMID		__UINT64_C(0x0002)
#define ZBX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE		__UINT64_C(0x0004)
#define ZBX_FLAG_LLD_GITEM_UPDATE_SORTORDER		__UINT64_C(0x0008)
#define ZBX_FLAG_LLD_GITEM_UPDATE_COLOR			__UINT64_C(0x0010)
#define ZBX_FLAG_LLD_GITEM_UPDATE_YAXISSIDE		__UINT64_C(0x0020)
#define ZBX_FLAG_LLD_GITEM_UPDATE_CALC_FNC		__UINT64_C(0x0040)
#define ZBX_FLAG_LLD_GITEM_UPDATE_TYPE			__UINT64_C(0x0080)
#define ZBX_FLAG_LLD_GITEM_UPDATE								\
		(ZBX_FLAG_LLD_GITEM_UPDATE_ITEMID | ZBX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE |	\
		ZBX_FLAG_LLD_GITEM_UPDATE_SORTORDER | ZBX_FLAG_LLD_GITEM_UPDATE_COLOR |		\
		ZBX_FLAG_LLD_GITEM_UPDATE_YAXISSIDE | ZBX_FLAG_LLD_GITEM_UPDATE_CALC_FNC |	\
		ZBX_FLAG_LLD_GITEM_UPDATE_TYPE)
#define ZBX_FLAG_LLD_GITEM_DELETE			__UINT64_C(0x0100)
	zbx_uint64_t		flags;
}
zbx_lld_gitem_t;

typedef struct
{
	zbx_uint64_t	itemid;
	unsigned char	flags;
}
zbx_lld_item_t;

/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数`lld_item_free`，用于释放zbx_lld_item_t类型指针指向的内存空间。当程序运行过程中创建了该类型的对象并将其指针传递给`lld_item_free`函数时，该函数可以将该对象所占用的内存空间释放，以避免内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_item_t类型指针指向的内存空间
static void	lld_item_free(zbx_lld_item_t *item)
{
	// 使用zbx_free函数释放item所指向的内存空间
	zbx_free(item);
}


/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_vector_ptr_t类型指针指向的内存空间。具体来说，遍历vector中的每个元素（zbx_lld_item_t类型），并逐个释放它们所指向的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_vector_ptr_t类型指针指向的内存空间
static void lld_items_free(zbx_vector_ptr_t *items)
/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_lld_gitem_t结构体指针指向的内存空间，其中包括颜色内存空间和gitem本身的内存空间。
 *
 *注释详细说明：
 *1. 定义一个静态函数，命名为lld_gitem_free，参数为一个zbx_lld_gitem_t类型的指针gitem。
/******************************************************************************
 * *
 *整个代码块的主要目的是释放全局数据结构gitems指向的内存空间。通过while循环遍历gitems->values[]数组，依次释放数组中的每个元素（zbx_lld_gitem_t类型）所指向的内存空间。
 ******************************************************************************/
/*
 * 定义一个静态函数 lld_gitems_free，接收一个zbx_vector_ptr_t类型的指针作为参数。
 * 这个函数的主要目的是释放一个全局数据结构 gitems 指向的内存空间。
 * 
 * 参数：
 *   gitems：指向全局数据结构zbx_vector_ptr_t的指针。
 */
static void lld_gitems_free(zbx_vector_ptr_t *gitems)
{
    // 使用一个while循环，当gitems指向的元素个数不为0时，执行以下操作：
    while (0 != gitems->values_num)
    {
        // 类型转换，将gitems->values[]数组中的元素类型从void*转换为zbx_lld_gitem_t*，以便后续操作。
        zbx_lld_gitem_t *gitem = (zbx_lld_gitem_t *)gitems->values[--gitems->values_num];
        
        // 释放gitem所指向的内存空间。
        free(gitem);
    }
}

static void lld_gitem_free(zbx_lld_gitem_t *gitem)
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码的主要目的是从数据库中查询与给定parent_graphid相关的图信息，并将它们存储在一个图结构体数组中。在这个过程中，代码还处理了图的各种属性，如宽度、高度、y轴最小值、y轴最大值等。最后，将图结构体数组和相关属性添加到结果列表中，并对结果列表进行排序。整个函数以调试日志开头和结尾，用于记录函数的执行情况。
 ******************************************************************************/
static void lld_graphs_get(zbx_uint64_t parent_graphid, zbx_vector_ptr_t *graphs, int width, int height,
                          double yaxismin, double yaxismax, unsigned char show_work_period, unsigned char show_triggers,
                          unsigned char graphtype, unsigned char show_legend, unsigned char show_3d, double percent_left,
                          double percent_right, unsigned char ymin_type, unsigned char ymax_type)
{
	/* 定义一个常量，表示函数名 */
	const char *__function_name = "lld_graphs_get";

	/* 声明变量，用于存储数据库查询结果、行数据以及图结构体指针 */
	DB_RESULT	result;
	DB_ROW		row;
	zbx_lld_graph_t	*graph;

	/* 记录日志，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 执行数据库查询，获取与给定parent_graphid相关的图信息 */
	result = DBselect(
			"select g.graphid,g.name,g.width,g.height,g.yaxismin,g.yaxismax,g.show_work_period,"
				"g.show_triggers,g.graphtype,g.show_legend,g.show_3d,g.percent_left,g.percent_right,"
				"g.ymin_type,g.ymin_itemid,g.ymax_type,g.ymax_itemid"
			" from graphs g,graph_discovery gd"
			" where g.graphid=gd.graphid"
				" and gd.parent_graphid=" ZBX_FS_UI64,
			parent_graphid);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 为每个图分配内存 */
		graph = (zbx_lld_graph_t *)zbx_malloc(NULL, sizeof(zbx_lld_graph_t));

		/* 解析行数据，设置图的结构体成员变量 */
		ZBX_STR2UINT64(graph->graphid, row[0]);
		graph->name = zbx_strdup(NULL, row[1]);
		graph->name_orig = NULL;

		graph->flags = ZBX_FLAG_LLD_GRAPH_UNSET;

		/* 检查并更新图的宽度、高度、y轴最小值、y轴最大值、工作周期、触发器、图类型、图例、三维显示、左侧百分比、右侧百分比、y轴最小值类型、y轴最大值类型 */
		if (atoi(row[2]) != width)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_WIDTH;

		if (atoi(row[3]) != height)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_HEIGHT;

		if (atof(row[4]) != yaxismin)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YAXISMIN;

		if (atof(row[5]) != yaxismax)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YAXISMAX;

		if ((unsigned char)atoi(row[6]) != show_work_period)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_WORK_PERIOD;

		if ((unsigned char)atoi(row[7]) != show_triggers)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_TRIGGERS;

		if ((unsigned char)atoi(row[8]) != graphtype)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_GRAPHTYPE;

		if ((unsigned char)atoi(row[9]) != show_legend)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_LEGEND;

		if ((unsigned char)atoi(row[10]) != show_3d)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_SHOW_3D;

		if (atof(row[11]) != percent_left)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_PERCENT_LEFT;

		if (atof(row[12]) != percent_right)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_PERCENT_RIGHT;

		if ((unsigned char)atoi(row[13]) != ymin_type)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YMIN_TYPE;

		ZBX_DBROW2UINT64(graph->ymin_itemid, row[14]);

		if ((unsigned char)atoi(row[15]) != ymax_type)
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YMAX_TYPE;

		ZBX_DBROW2UINT64(graph->ymax_itemid, row[16]);

		/* 初始化图的物品列表 */
		zbx_vector_ptr_create(&graph->gitems);

		/* 将图添加到结果列表中 */
/******************************************************************************
 * *
 *主要目的：从这个函数中，我们可以看出它的主要目的是从数据库中查询与给定parent_graphid相关的graph items，并将它们添加到相应的graph对象中。此外，它还负责处理内存分配和释放，以及排序和日志记录。
 ******************************************************************************/
static void lld_gitems_get(zbx_uint64_t parent_graphid, zbx_vector_ptr_t *gitems_proto,
                           zbx_vector_ptr_t *graphs)
{
    const char *__function_name = "lld_gitems_get";

    int i, index;
    zbx_lld_graph_t *graph;
    zbx_lld_gitem_t *gitem;
    zbx_uint64_t graphid;
    zbx_vector_uint64_t graphids;
    DB_RESULT result;
    DB_ROW row;
    char *sql = NULL;
    size_t sql_alloc = 256, sql_offset = 0;

    // 开启日志记录，记录函数调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个uint64类型的 vector，用于存储graphid
    zbx_vector_uint64_create(&graphids);
    // 将parent_graphid添加到vector中
    zbx_vector_uint64_append(&graphids, parent_graphid);

    // 遍历graphs vector，将每个graph的graphid添加到graphids vector中
    for (i = 0; i < graphs->values_num; i++)
    {
        graph = (zbx_lld_graph_t *)graphs->values[i];
        zbx_vector_uint64_append(&graphids, graph->graphid);
    }

    // 对graphids vector进行排序
    zbx_vector_uint64_sort(&graphids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 分配内存，用于存储SQL语句
    sql = (char *)zbx_malloc(sql, sql_alloc);

    // 构造SQL查询语句，查询graphs_items表中的数据
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                    "select gitemid,graphid,itemid,drawtype,sortorder,color,yaxisside,calc_fnc,type"
                    " from graphs_items"
                    " where");
    // 添加查询条件，筛选出与parent_graphid相关的记录
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid",
                         graphids.values, graphids.values_num);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 释放SQL内存
    zbx_free(sql);

    // 遍历查询结果，解析数据，创建gitem对象
    while (NULL != (row = DBfetch(result)))
    {
        gitem = (zbx_lld_gitem_t *)zbx_malloc(NULL, sizeof(zbx_lld_gitem_t));

        // 解析gitem的数据
        ZBX_STR2UINT64(gitem->gitemid, row[0]);
        ZBX_STR2UINT64(graphid, row[1]);
        ZBX_STR2UINT64(gitem->itemid, row[2]);
        ZBX_STR2UCHAR(gitem->drawtype, row[3]);
        gitem->sortorder = atoi(row[4]);
        gitem->color = zbx_strdup(NULL, row[5]);
        ZBX_STR2UCHAR(gitem->yaxisside, row[6]);
        ZBX_STR2UCHAR(gitem->calc_fnc, row[7]);
        ZBX_STR2UCHAR(gitem->type, row[8]);

        // 初始化gitem对象
        gitem->flags = ZBX_FLAG_LLD_GITEM_UNSET;

        // 如果graphid等于parent_graphid，将gitem添加到gitems_proto vector中
        if (graphid == parent_graphid)
        {
            zbx_vector_ptr_append(gitems_proto, gitem);
        }
        // 如果graphid不在gitems_proto vector中，但能在graphs vector中找到对应的graph，将gitem添加到该graph的gitems vector中
        else if (FAIL != (index = zbx_vector_ptr_bsearch(graphs, &graphid,
                                                         ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
        {
            graph = (zbx_lld_graph_t *)graphs->values[index];

            // 将gitem添加到graph的gitems vector中
            zbx_vector_ptr_append(&graph->gitems, gitem);
        }
        // 如果没有找到对应的graph，释放gitem内存，并进行错误处理
        else
        {
            THIS_SHOULD_NEVER_HAPPEN;
            lld_gitem_free(gitem);
        }
    }
    // 释放查询结果
    DBfree_result(result);

    // 对gitems_proto vector进行排序
    zbx_vector_ptr_sort(gitems_proto, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 对每个graph的gitems vector进行排序
    for (i = 0; i < graphs->values_num; i++)
    {
        graph = (zbx_lld_graph_t *)graphs->values[i];
        zbx_vector_ptr_sort(&graph->gitems, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
    }

    // 释放graphids vector
    zbx_vector_uint64_destroy(&graphids);

    // 结束日志记录
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_gitems_get                                                   *
 *                                                                            *
 * Purpose: retrieve graphs_items which are used by the graph prototype and   *
 *          by selected graphs                                                *
 *                                                                            *
 ******************************************************************************/
static void	lld_gitems_get(zbx_uint64_t parent_graphid, zbx_vector_ptr_t *gitems_proto,
		zbx_vector_ptr_t *graphs)
{
	const char		*__function_name = "lld_gitems_get";

	int			i, index;
	zbx_lld_graph_t		*graph;
	zbx_lld_gitem_t		*gitem;
	zbx_uint64_t		graphid;
	zbx_vector_uint64_t	graphids;
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&graphids);
	zbx_vector_uint64_append(&graphids, parent_graphid);

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (zbx_lld_graph_t *)graphs->values[i];

		zbx_vector_uint64_append(&graphids, graph->graphid);
	}

	zbx_vector_uint64_sort(&graphids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select gitemid,graphid,itemid,drawtype,sortorder,color,yaxisside,calc_fnc,type"
			" from graphs_items"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid",
			graphids.values, graphids.values_num);

	result = DBselect("%s", sql);

	zbx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		gitem = (zbx_lld_gitem_t *)zbx_malloc(NULL, sizeof(zbx_lld_gitem_t));

		ZBX_STR2UINT64(gitem->gitemid, row[0]);
		ZBX_STR2UINT64(graphid, row[1]);
		ZBX_STR2UINT64(gitem->itemid, row[2]);
		ZBX_STR2UCHAR(gitem->drawtype, row[3]);
		gitem->sortorder = atoi(row[4]);
		gitem->color = zbx_strdup(NULL, row[5]);
		ZBX_STR2UCHAR(gitem->yaxisside, row[6]);
		ZBX_STR2UCHAR(gitem->calc_fnc, row[7]);
		ZBX_STR2UCHAR(gitem->type, row[8]);

		gitem->flags = ZBX_FLAG_LLD_GITEM_UNSET;

		if (graphid == parent_graphid)
		{
			zbx_vector_ptr_append(gitems_proto, gitem);
		}
		else if (FAIL != (index = zbx_vector_ptr_bsearch(graphs, &graphid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			graph = (zbx_lld_graph_t *)graphs->values[index];

			zbx_vector_ptr_append(&graph->gitems, gitem);
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			lld_gitem_free(gitem);
		}
	}
	DBfree_result(result);

	zbx_vector_ptr_sort(gitems_proto, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < graphs->values_num; i++)
	{
		graph = (zbx_lld_graph_t *)graphs->values[i];

		zbx_vector_ptr_sort(&graph->gitems, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	zbx_vector_uint64_destroy(&graphids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * 以下是我为您注释的代码块：
 *
 *
 *
 *整个代码块的主要目的是从一个名为`gitems_proto`的vector中获取数据，根据给定的ymin_itemid_proto、ymax_itemid_proto和itemids vector，查询数据库中的items表，并将查询结果中的item添加到一个新的items vector中。最后，释放不再使用的内存并结束函数。
 ******************************************************************************/
static void lld_items_get(const zbx_vector_ptr_t *gitems_proto, zbx_uint64_t ymin_itemid_proto,
                         zbx_uint64_t ymax_itemid_proto, zbx_vector_ptr_t *items)
{
	/* 定义一个常量，表示函数名 */
	const char *__function_name = "lld_items_get";

	/* 声明一些变量 */
	DB_RESULT		result;
	DB_ROW			row;
	const zbx_lld_gitem_t	*gitem;
	zbx_lld_item_t		*item;
	zbx_vector_uint64_t	itemids;
	int			i;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一个uint64类型的 vector */
	zbx_vector_uint64_create(&itemids);

	/* 遍历gitems_proto中的每个元素，将其itemid添加到itemids vector中 */
	for (i = 0; i < gitems_proto->values_num; i++)
	{
		gitem = (zbx_lld_gitem_t *)gitems_proto->values[i];

		zbx_vector_uint64_append(&itemids, gitem->itemid);
	}

	/* 如果ymin_itemid_proto不为0，将其添加到itemids vector中 */
	if (0 != ymin_itemid_proto)
		zbx_vector_uint64_append(&itemids, ymin_itemid_proto);

	/* 如果ymax_itemid_proto不为0，将其添加到itemids vector中 */
	if (0 != ymax_itemid_proto)
		zbx_vector_uint64_append(&itemids, ymax_itemid_proto);

	/* 如果itemids vector不为空，执行以下操作：
	 * 1. 构建SQL查询语句
	 * 2. 根据itemids vector中的itemid查询数据库中的items表
	 * 3. 将查询结果中的item添加到items vector中
	 */
	if (0 != itemids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 256, sql_offset = 0;

		/* 排序itemids vector */
		zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		/* 分配内存并构建SQL查询语句 */
		sql = (char *)zbx_malloc(sql, sql_alloc);

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select itemid,flags"
				" from items"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids.values, itemids.values_num);

		/* 执行SQL查询 */
		result = DBselect("%s", sql);

		/* 释放分配的内存 */
		zbx_free(sql);

		/* 遍历查询结果，将item添加到items vector中 */
		while (NULL != (row = DBfetch(result)))
		{
			item = (zbx_lld_item_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_t));

			ZBX_STR2UINT64(item->itemid, row[0]);
			ZBX_STR2UCHAR(item->flags, row[1]);

			/* 添加item到items vector中 */
			zbx_vector_ptr_append(items, item);
		}
		/* 释放查询结果 */
		DBfree_result(result);

		/* 排序items vector */
		zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	/* 释放itemids vector */
	zbx_vector_uint64_destroy(&itemids);

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

			ZBX_STR2UCHAR(item->flags, row[1]);

			zbx_vector_ptr_append(items, item);
		}
		DBfree_result(result);

		zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	zbx_vector_uint64_destroy(&itemids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graph_by_item                                                *
 *                                                                            *
 * Purpose: finds already existing graph, using an item                       *
 *                                                                            *
 * Return value: upon successful completion return pointer to the graph       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是通过 itemid 查找对应的 lld_graph。该函数接收两个参数：一个指向 zbx_vector_ptr_t 类型的指针 graphs，表示包含多个 lld_graph 的向量；一个 zbx_uint64_t 类型的参数 itemid，表示要查找的 lld_graph 的 itemid。函数返回一个指向 zbx_lld_graph_t 类型的指针，如果找到对应的 lld_graph，否则返回 NULL。
 ******************************************************************************/
// 定义一个函数，用于通过 itemid 查找对应的 lld_graph
static zbx_lld_graph_t *lld_graph_by_item(zbx_vector_ptr_t *graphs, zbx_uint64_t itemid)
{
	// 定义两个循环变量 i 和 j
	int		i, j;
	// 定义一个指向 zbx_lld_graph_t 类型的指针 graph
	zbx_lld_graph_t	*graph;
	// 定义一个指向 zbx_lld_gitem_t 类型的指针 gitem
	zbx_lld_gitem_t	*gitem;

	// 遍历 graphs 中的每个元素
	for (i = 0; i < graphs->values_num; i++)
	{
		// 取出 graphs 中的一个元素，类型为 zbx_lld_graph_t
		graph = (zbx_lld_graph_t *)graphs->values[i];

		// 如果 graph 的 flags 中包含了 ZBX_FLAG_LLD_GRAPH_DISCOVERED 标志，则跳过这个元素，继续查找下一个
		if (0 != (graph->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED))
			continue;

		// 遍历 graph 中的每个 gitem
		for (j = 0; j < graph->gitems.values_num; j++)
		{
			// 取出 graph 中的一个 gitem
			gitem = (zbx_lld_gitem_t *)graph->gitems.values[j];

			// 如果 gitem 的 itemid 等于传入的 itemid，则找到目标 lld_graph，返回它
			if (gitem->itemid == itemid)
				return graph;
		}
	}

/******************************************************************************
 * *
 *这个代码块的主要目的是根据全局物品（gitems）原型和关联的item_links，创建并更新全局物品。具体来说，代码块实现了以下功能：
 *
 *1. 定义静态函数lld_gitems_make，接收5个参数，分别为：gitems_proto（全局物品原型）、gitems（全局物品列表）、items（item_links中的items）、item_links（item_links列表）。
 *2. 初始化函数名和日志记录。
 *3. 遍历gitems_proto中的每个元素，调用lld_item_get函数获取itemid，并将其添加到gitems列表中。
 *4. 在遍历过程中，针对每个全局物品，检查并更新其属性（如drawtype、sortorder、color等），以确保与gitems_proto中的属性保持一致。
 *5. 遍历gitems列表，将每个全局物品的标志位设置为DELETE，表示待删除。
 *6. 设置函数返回值，表示成功。
 *7. 记录日志，表示函数执行结束。
 *8. 返回函数执行结果。
 ******************************************************************************/
// 定义静态函数lld_gitems_make，参数包括：
// static：静态函数，只在当前源文件中可用
// int：返回类型为整型
// lld_gitems_make：函数名
// const zbx_vector_ptr_t *gitems_proto：指向zbx_vector_ptr_t类型对象的指针，表示全局物品（gitems）的原型
// zbx_vector_ptr_t *gitems：指向zbx_vector_ptr_t类型对象的指针，表示全局物品（gitems）
// const zbx_vector_ptr_t *items：指向zbx_vector_ptr_t类型对象的指针，表示item_links中的items
// const zbx_vector_ptr_t *item_links：指向zbx_vector_ptr_t类型对象的指针，表示item_links

static int lld_gitems_make(const zbx_vector_ptr_t *gitems_proto, zbx_vector_ptr_t *gitems,
                          const zbx_vector_ptr_t *items, const zbx_vector_ptr_t *item_links)
{
	// 定义常量字符串，表示函数名
	const char *__function_name = "lld_gitems_make";

	// 定义变量，用于循环遍历gitems_proto中的每个元素
	int i, ret = FAIL;
	// 定义指向zbx_lld_gitem_t类型对象的指针，用于暂存gitems_proto中的元素
	const zbx_lld_gitem_t *gitem_proto;
	// 定义指向zbx_lld_gitem_t类型对象的指针，用于存储创建的新全局物品
	zbx_lld_gitem_t *gitem;
	// 定义变量，用于存储itemid
	zbx_uint64_t itemid;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 遍历gitems_proto中的每个元素
	for (i = 0; i < gitems_proto->values_num; i++)
	{
		// 获取gitems_proto中的当前元素
		gitem_proto = (zbx_lld_gitem_t *)gitems_proto->values[i];

		// 调用函数lld_item_get，获取itemid，若失败则退出循环
		if (SUCCEED != lld_item_get(gitem_proto->itemid, items, item_links, &itemid))
			goto out;

		// 判断当前遍历到的元素是否为gitems中的最后一个元素
		if (i == gitems->values_num)
		{
			// 分配内存，创建一个新的全局物品
			gitem = (zbx_lld_gitem_t *)zbx_malloc(NULL, sizeof(zbx_lld_gitem_t));

			// 初始化新创建的全局物品
			gitem->gitemid = 0;
			gitem->itemid = itemid;
			gitem->drawtype = gitem_proto->drawtype;
			gitem->sortorder = gitem_proto->sortorder;
			gitem->color = zbx_strdup(NULL, gitem_proto->color);
			gitem->yaxisside = gitem_proto->yaxisside;
			gitem->calc_fnc = gitem_proto->calc_fnc;
			gitem->type = gitem_proto->type;

			// 设置全局物品标志位，表示已发现
			gitem->flags = ZBX_FLAG_LLD_GITEM_DISCOVERED;

			// 将新创建的全局物品添加到gitems中
			zbx_vector_ptr_append(gitems, gitem);
		}
		else
		{
			// 获取gitems中的当前元素
			gitem = (zbx_lld_gitem_t *)gitems->values[i];

			// 判断gitem的itemid是否与当前遍历到的itemid相同，若不同则更新itemid
			if (gitem->itemid != itemid)
			{
				gitem->itemid = itemid;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_ITEMID;
			}

			// 判断gitem的drawtype是否与gitem_proto的drawtype不同，若不同则更新drawtype
			if (gitem->drawtype != gitem_proto->drawtype)
			{
				gitem->drawtype = gitem_proto->drawtype;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE;
			}

			// 判断gitem的sortorder是否与gitem_proto的sortorder不同，若不同则更新sortorder
			if (gitem->sortorder != gitem_proto->sortorder)
			{
				gitem->sortorder = gitem_proto->sortorder;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_SORTORDER;
			}

			// 判断gitem的color是否与gitem_proto的color不同，若不同则更新color
			if (0 != strcmp(gitem->color, gitem_proto->color))
			{
				gitem->color = zbx_strdup(gitem->color, gitem_proto->color);
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_COLOR;
			}

			// 判断gitem的yaxisside是否与gitem_proto的yaxisside不同，若不同则更新yaxisside
			if (gitem->yaxisside != gitem_proto->yaxisside)
			{
				gitem->yaxisside = gitem_proto->yaxisside;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_YAXISSIDE;
			}

			// 判断gitem的calc_fnc是否与gitem_proto的calc_fnc不同，若不同则更新calc_fnc
			if (gitem->calc_fnc != gitem_proto->calc_fnc)
			{
				gitem->calc_fnc = gitem_proto->calc_fnc;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_CALC_FNC;
			}

			// 判断gitem的type是否与gitem_proto的type不同，若不同则更新type
			if (gitem->type != gitem_proto->type)
			{
				gitem->type = gitem_proto->type;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_TYPE;
			}

			// 更新gitem的flags，表示已发现
			gitem->flags |= ZBX_FLAG_LLD_GITEM_DISCOVERED;
		}
	}

	// 遍历gitems中的每个元素，将其标志位设置为DELETE，表示待删除
	for (; i < gitems->values_num; i++)
	{
		gitem = (zbx_lld_gitem_t *)gitems->values[i];

		gitem->flags |= ZBX_FLAG_LLD_GITEM_DELETE;
	}

	// 设置返回值，表示成功
	ret = SUCCEED;
out:
	// 记录日志，表示函数结束执行
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

	else
		*itemid = item_proto->itemid;

	return SUCCEED;
}

static int	lld_gitems_make(const zbx_vector_ptr_t *gitems_proto, zbx_vector_ptr_t *gitems,
		const zbx_vector_ptr_t *items, const zbx_vector_ptr_t *item_links)
{
	const char		*__function_name = "lld_gitems_make";

	int			i, ret = FAIL;
	const zbx_lld_gitem_t	*gitem_proto;
	zbx_lld_gitem_t		*gitem;
	zbx_uint64_t		itemid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	for (i = 0; i < gitems_proto->values_num; i++)
	{
		gitem_proto = (zbx_lld_gitem_t *)gitems_proto->values[i];

		if (SUCCEED != lld_item_get(gitem_proto->itemid, items, item_links, &itemid))
			goto out;

		if (i == gitems->values_num)
		{
			gitem = (zbx_lld_gitem_t *)zbx_malloc(NULL, sizeof(zbx_lld_gitem_t));

			gitem->gitemid = 0;
			gitem->itemid = itemid;
			gitem->drawtype = gitem_proto->drawtype;
			gitem->sortorder = gitem_proto->sortorder;
			gitem->color = zbx_strdup(NULL, gitem_proto->color);
			gitem->yaxisside = gitem_proto->yaxisside;
			gitem->calc_fnc = gitem_proto->calc_fnc;
			gitem->type = gitem_proto->type;

			gitem->flags = ZBX_FLAG_LLD_GITEM_DISCOVERED;

			zbx_vector_ptr_append(gitems, gitem);
		}
		else
		{
			gitem = (zbx_lld_gitem_t *)gitems->values[i];

			if (gitem->itemid != itemid)
			{
				gitem->itemid = itemid;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_ITEMID;
			}

			if (gitem->drawtype != gitem_proto->drawtype)
			{
				gitem->drawtype = gitem_proto->drawtype;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_DRAWTYPE;
			}

			if (gitem->sortorder != gitem_proto->sortorder)
			{
				gitem->sortorder = gitem_proto->sortorder;
				gitem->flags |= ZBX_FLAG_LLD_GITEM_UPDATE_SORTORDER;
/******************************************************************************
 * *
 *主要目的：这个函数用于创建或更新一个 LLDP（链路层发现）图表。它根据提供的名称、最小和最大 itemID 以及相关数据结构创建一个新的图表，或者更新已存在的图表。在执行过程中，函数会处理图表名称、itemID 以及其他相关参数。如果创建或更新图表成功，函数将返回 SUCCEED，否则返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于创建或更新一个 LLDP（链路层发现）图表
static void 	lld_graph_make(const zbx_vector_ptr_t *gitems_proto, zbx_vector_ptr_t *graphs, zbx_vector_ptr_t *items,
                const char *name_proto, zbx_uint64_t ymin_itemid_proto, zbx_uint64_t ymax_itemid_proto,
                const zbx_lld_row_t *lld_row)
{
    // 定义一个内部函数名，方便调试
    const char			*__function_name = "lld_graph_make";

    // 定义一个指向 LLDP 图表的指针，初始化为 NULL
    zbx_lld_graph_t			*graph = NULL;
    // 定义一个字符串缓冲区，用于存储图表名称
    char				*buffer = NULL;
    // 定义一个指向 JSON 解析结构的指针
    const struct zbx_json_parse	*jp_row = &lld_row->jp_row;
    // 定义两个变量，分别用于存储 ymin_itemid 和 ymax_itemid
    zbx_uint64_t			ymin_itemid, ymax_itemid;

    // 记录日志，表示进入函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 如果 ymin_itemid_proto 为 0，则将 ymin_itemid 设置为 0
    if (0 == ymin_itemid_proto)
        ymin_itemid = 0;
    // 否则，尝试从 items  vector 中获取 ymin_itemid
    else if (SUCCEED != lld_item_get(ymin_itemid_proto, items, &lld_row->item_links, &ymin_itemid))
        goto out;

    // 如果 ymax_itemid_proto 为 0，则将 ymax_itemid 设置为 0
    if (0 == ymax_itemid_proto)
        ymax_itemid = 0;
    // 否则，尝试从 items vector 中获取 ymax_itemid
    else if (SUCCEED != lld_item_get(ymax_itemid_proto, items, &lld_row->item_links, &ymax_itemid))
        goto out;

    // 如果 graph 不是 NULL，说明已经存在一个图表，我们需要对其进行更新
    if (NULL != (graph = lld_graph_get(graphs, &lld_row->item_links)))
    {
        // 复制一个缓冲区，用于存储图表名称
        buffer = zbx_strdup(buffer, name_proto);
        // 替换图表名称中的宏
        substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_SIMPLE, NULL, 0);
        // 去除缓冲区末尾的空白字符
        zbx_lrtrim(buffer, ZBX_WHITESPACE);

        // 如果当前图表名称与新缓冲区中的名称不同，更新图表名称
        if (0 != strcmp(graph->name, buffer))
        {
            // 保存原始图表名称
            graph->name_orig = graph->name;
            // 更新图表名称
            graph->name = buffer;
            // 设置图表更新标志，表示名称已更新
            graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_NAME;
        }

        // 如果 ymin_itemid 与 graph->ymin_itemid 不同，更新 ymin_itemid
        if (graph->ymin_itemid != ymin_itemid)
        {
            graph->ymin_itemid = ymin_itemid;
            // 设置图表更新标志，表示 ymin_itemid 已更新
            graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID;
        }

        // 如果 ymax_itemid 与 graph->ymax_itemid 不同，更新 ymax_itemid
        if (graph->ymax_itemid != ymax_itemid)
        {
            graph->ymax_itemid = ymax_itemid;
            // 设置图表更新标志，表示 ymax_itemid 已更新
            graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID;
        }
    }
    // 否则，说明需要创建一个新的图表
    else
    {
        // 分配内存，用于存储新图表
        graph = (zbx_lld_graph_t *)zbx_malloc(NULL, sizeof(zbx_lld_graph_t));

        // 初始化图表ID
        graph->graphid = 0;

        // 复制一个缓冲区，用于存储图表名称
        graph->name = zbx_strdup(NULL, name_proto);
        // 保存原始图表名称
        graph->name_orig = NULL;
        // 替换图表名称中的宏
        substitute_lld_macros(&graph->name, jp_row, ZBX_MACRO_SIMPLE, NULL, 0);
        // 去除缓冲区末尾的空白字符
        zbx_lrtrim(graph->name, ZBX_WHITESPACE);

        // 初始化图表的 ymin_itemid 和 ymax_itemid
        graph->ymin_itemid = ymin_itemid;
        graph->ymax_itemid = ymax_itemid;

        // 创建一个指向 items vector 的指针 vector
        zbx_vector_ptr_create(&graph->gitems);

        // 初始化图表更新标志，表示未更新
        graph->flags = ZBX_FLAG_LLD_GRAPH_UNSET;

        // 将新图表添加到 graphs vector 中
        zbx_vector_ptr_append(graphs, graph);
    }

    // 释放缓冲区
    zbx_free(buffer);

    // 如果 failed 调用 lld_gitems_make 函数，表示创建或更新图表失败，返回
    if (SUCCEED != lld_gitems_make(gitems_proto, &graph->gitems, items, &lld_row->item_links))
        return;

    // 设置图表更新标志，表示已发现
    graph->flags |= ZBX_FLAG_LLD_GRAPH_DISCOVERED;

    // 记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(graph->name, buffer))
		{
			graph->name_orig = graph->name;
			graph->name = buffer;
			buffer = NULL;
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_NAME;
		}

		if (graph->ymin_itemid != ymin_itemid)
		{
			graph->ymin_itemid = ymin_itemid;
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YMIN_ITEMID;
		}

		if (graph->ymax_itemid != ymax_itemid)
		{
			graph->ymax_itemid = ymax_itemid;
			graph->flags |= ZBX_FLAG_LLD_GRAPH_UPDATE_YMAX_ITEMID;
		}
	}
	else
	{
		graph = (zbx_lld_graph_t *)zbx_malloc(NULL, sizeof(zbx_lld_graph_t));

		graph->graphid = 0;

		graph->name = zbx_strdup(NULL, name_proto);
		graph->name_orig = NULL;
		substitute_lld_macros(&graph->name, jp_row, ZBX_MACRO_SIMPLE, NULL, 0);
		zbx_lrtrim(graph->name, ZBX_WHITESPACE);

		graph->ymin_itemid = ymin_itemid;
		graph->ymax_itemid = ymax_itemid;

		zbx_vector_ptr_create(&graph->gitems);

		graph->flags = ZBX_FLAG_LLD_GRAPH_UNSET;

		zbx_vector_ptr_append(graphs, graph);
	}

	zbx_free(buffer);

	if (SUCCEED != lld_gitems_make(gitems_proto, &graph->gitems, items, &lld_row->item_links))
		return;

	graph->flags |= ZBX_FLAG_LLD_GRAPH_DISCOVERED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历 lld_rows 中的每个元素，根据 gitems_proto 中的配置信息、items 中的所有 items 信息以及 name_proto、ymin_itemid_proto、ymax_itemid_proto 参数，调用 lld_graph_make 函数生成图表，并将生成的图表添加到 graphs 列表中。最后对 graphs 列表进行排序。
 ******************************************************************************/
// 定义一个名为 lld_graphs_make 的静态函数，参数包括：
// gitems_proto：指向全局变量 zbx_vector_ptr_t 类型的指针，用于存储图表的配置信息
// graphs：指向全局变量 zbx_vector_ptr_t 类型的指针，用于存储生成的图表列表
// items：指向全局变量 zbx_vector_ptr_t 类型的指针，用于存储所有 items 信息
// name_proto：指向字符串常量的指针，表示图表的名称前缀
// ymin_itemid_proto：指向 zbx_uint64_t 类型的指针，表示图表中最小 item 的 ID
// ymax_itemid_proto：指向 zbx_uint64_t 类型的指针，表示图表中最大 item 的 ID
// lld_rows：指向全局变量 zbx_vector_ptr_t 类型的指针，用于存储 lld_rows 信息

static void lld_graphs_make(const zbx_vector_ptr_t *gitems_proto, zbx_vector_ptr_t *graphs, zbx_vector_ptr_t *items,
                           const char *name_proto, zbx_uint64_t ymin_itemid_proto, zbx_uint64_t ymax_itemid_proto,
                           const zbx_vector_ptr_t *lld_rows)
{
	int	i; // 定义一个整型变量 i，用于循环计数

	for (i = 0; i < lld_rows->values_num; i++) // 遍历 lld_rows 中的每个元素
	{
		zbx_lld_row_t	*lld_row = (zbx_lld_row_t *)lld_rows->values[i]; // 获取当前遍历到的 lld_rows 元素

		lld_graph_make(gitems_proto, graphs, items, name_proto, ymin_itemid_proto, ymax_itemid_proto, lld_row); // 调用 lld_graph_make 函数，根据 lld_row 信息生成图表
	}

	zbx_vector_ptr_sort(graphs, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC); // 对生成的图表列表进行排序
}


/******************************************************************************
 *                                                                            *
 * Function: lld_validate_graph_field                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 函数名：lld_validate_graph_field
 * 参数：
 *   zbx_lld_graph_t *graph：图结构指针
 *   char **field：图字段指针
 *   char **field_orig：原始图字段指针
 *   zbx_uint64_t flag：图标记
 *   size_t field_len：字段长度
 *   char **error：错误信息指针
 * 功能：验证图字段
 * 注释：
 *   1. 检查图是否已发现，如果没有，则直接返回
 *   2. 只验证新图或数据发生变化的图
 *   3. 检查字段是否为UTF-8编码，如果不是，则进行替换并记录错误
 *   4. 检查字段长度是否超过规定长度，如果超过，则记录错误
 *   5. 检查图名是否为空，如果为空，则记录错误
 *   6. 如果图已存在，则回滚字段字符串并更新图标记
 *   7. 如果图不存在，则清除图标记
 */
static void lld_validate_graph_field(zbx_lld_graph_t *graph, char **field, char **field_orig, zbx_uint64_t flag,
                                     size_t field_len, char **error)
{
	// 1. 检查图是否已发现，如果没有，则直接返回
	if (0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED))
		return;
/******************************************************************************
 * *
 *这个代码块的主要目的是对一组图形进行验证，确保它们的名称不重复，并在数据库中不存在具有相同名称的图形。以下是代码的详细注释：
 *
 *1. 定义函数`lld_graphs_validate`，接受三个参数：`zbx_uint64_t hostid`（主机ID）、`zbx_vector_ptr_t *graphs`（图形指针数组）和`char **error`（错误字符串指针）。
 *2. 定义一个常量字符串`__function_name`，表示当前函数的名
 ******************************************************************************/
static void lld_graphs_validate(zbx_uint64_t hostid, zbx_vector_ptr_t *graphs, char **error)
{
    const char *__function_name = "lld_graphs_validate";

    int i, j;
    zbx_lld_graph_t *graph, *graph_b;
    zbx_vector_uint64_t graphids;
    zbx_vector_str_t names;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    zbx_vector_uint64_create(&graphids);
    zbx_vector_str_create(&names);		/* list of graph names */

    /* 检查字段的有效性 */

    for (i = 0; i < graphs->values_num; i++)
    {
        graph = (zbx_lld_graph_t *)graphs->values[i];

        lld_validate_graph_field(graph, &graph->name, &graph->name_orig,
                                ZBX_FLAG_LLD_GRAPH_UPDATE_NAME, GRAPH_NAME_LEN, error);
    }

    /* 检查重复的图形名称 */
    for (i = 0; i < graphs->values_num; i++)
    {
        graph = (zbx_lld_graph_t *)graphs->values[i];

        if (0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED))
            continue;

        /* 只有新图形或名称发生变化的图形才会被验证 */
        if (0 != graph->graphid && 0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_UPDATE_NAME))
            continue;

        for (j = 0; j < graphs->values_num; j++)
        {
            graph_b = (zbx_lld_graph_t *)graphs->values[j];

            if (0 == (graph_b->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED) || i == j)
                continue;

            if (0 != strcmp(graph->name, graph_b->name))
                continue;

            *error = zbx_strdcatf(*error, "Cannot %s graph:"
                                " graph with the same name \"%s\" already exists.\
",
                                (0 != graph->graphid ? "update" : "create"), graph->name);

            if (0 != graph->graphid)
            {
                lld_field_str_rollback(&graph->name, &graph->name_orig, &graph->flags,
                                ZBX_FLAG_LLD_GRAPH_UPDATE_NAME);
            }
            else
                graph->flags &= ~ZBX_FLAG_LLD_GRAPH_DISCOVERED;

            break;
        }
    }

    /* 检查数据库中是否已存在重复的图形 */

    for (i = 0; i < graphs->values_num; i++)
    {
        graph = (zbx_lld_graph_t *)graphs->values[i];

        if (0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED))
            continue;

        if (0 != graph->graphid)
        {
            zbx_vector_uint64_append(&graphids, graph->graphid);

            if (0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_UPDATE_NAME))
                continue;
        }

        zbx_vector_str_append(&names, graph->name);
    }

    if (0 != names.values_num)
    {
        DB_RESULT	result;
        DB_ROW		row;
        char		*sql = NULL;
        size_t		sql_alloc = 256, sql_offset = 0;

        sql = (char *)zbx_malloc(sql, sql_alloc);

        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                        "select g.name"
                        " from graphs g,graphs_items gi,items i"
                        " where g.graphid=gi.graphid"
                        " and gi.itemid=i.itemid"
                        " and i.hostid=" ZBX_FS_UI64
                        " and",
                        hostid);
        DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.name",
                                (const char **)names.values, names.values_num);

        if (0 != graphids.values_num)
        {
            zbx_vector_uint64_sort(&graphids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
            zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
            DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.graphid",
                                graphids.values, graphids.values_num);
        }

        result = DBselect("%s", sql);

        while (NULL != (row = DBfetch(result)))
        {
            for (i = 0; i < graphs->values_num; i++)
            {
                graph = (zbx_lld_graph_t *)graphs->values[i];

                if (0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED))
                    continue;

                if (0 == strcmp(graph->name, row[0]))
                {
                    *error = zbx_strdcatf(*error, "Cannot %s graph:"
                                    " graph with the same name \"%s\" already exists.\
",
                                    (0 != graph->graphid ? "update" : "create"), graph->name);

                    if (0 != graph->graphid)
                    {
                        lld_field_str_rollback(&graph->name, &graph->name_orig, &graph->flags,
                                            ZBX_FLAG_LLD_GRAPH_UPDATE_NAME);
                    }
                    else
                        graph->flags &= ~ZBX_FLAG_LLD_GRAPH_DISCOVERED;

                    continue;
                }
            }
        }
        DBfree_result(result);

        zbx_vector_str_destroy(&names);
        zbx_vector_uint64_destroy(&graphids);
    }

    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.graphid",
					graphids.values, graphids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < graphs->values_num; i++)
			{
				graph = (zbx_lld_graph_t *)graphs->values[i];

				if (0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED))
					continue;

				if (0 == strcmp(graph->name, row[0]))
				{
					*error = zbx_strdcatf(*error, "Cannot %s graph:"
							" graph with the same name \"%s\" already exists.\n",
							(0 != graph->graphid ? "update" : "create"), graph->name);

					if (0 != graph->graphid)
					{
						lld_field_str_rollback(&graph->name, &graph->name_orig, &graph->flags,
								ZBX_FLAG_LLD_GRAPH_UPDATE_NAME);
					}
					else
						graph->flags &= ~ZBX_FLAG_LLD_GRAPH_DISCOVERED;

					continue;
				}
			}
		}
		DBfree_result(result);

		zbx_free(sql);
	}

	zbx_vector_str_destroy(&names);
	zbx_vector_uint64_destroy(&graphids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_graphs_save                                                  *
 *                                                                            *
 * Purpose: add or update graphs in database based on discovery rule          *
 *                                                                            *
 * Return value: SUCCEED - if graphs were successfully saved or saving        *
 *                         was not necessary                                  *
 *               FAIL    - graphs cannot be saved                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对给定C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是处理一个名为lld_graphs_save的函数，用于处理一些与图形相关数据的保存操作。代码中遍历了一个指向zbx_lld_graph_t类型的指针数组，根据数组中元素的标志位，进行相应的数据处理，如新增、更新、删除等操作。最后返回一个表示处理结果的整型变量。
 ******************************************************************************/
static int	lld_graphs_save(...)
{
    // 函数名称为 lld_graphs_save，是一个静态函数
    // 参数列表包括：hostid、parent_graphid、graphs、width、height、yaxismin、yaxismax等
    // 返回值为int类型

    int			ret = SUCCEED, i, j, new_graphs = 0, upd_graphs = 0, new_gitems = 0;
    // 定义变量 ret，表示函数返回值，初始化为SUCCEED
    // 定义变量 i、j，用于循环遍历数组
    // 定义变量 new_graphs、upd_graphs、new_gitems，用于统计新增、更新、删除的数据个数

    // ...

    for (i = 0; i < graphs->values_num; i++)
    {
        // 循环遍历数组，i表示当前遍历到的元素索引
        // graphs是一个指向zbx_lld_graph_t类型的指针数组

        graph = (zbx_lld_graph_t *)graphs->values[i];

        // graph是一个指向zbx_lld_graph_t类型的指针，指向当前遍历到的元素

        if (0 == (graph->flags & ZBX_FLAG_LLD_GRAPH_DISCOVERED))
        {
            // 如果当前元素的flags字段中没有ZBX_FLAG_LLD_GRAPH_DISCOVERED标志
            continue;
        }

        // ...
    }

    // ...

    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_update_graphs                                                *
 *                                                                            *
 * Purpose: add or update graphs for discovery item                           *
 *                                                                            *
 * Parameters: hostid  - [IN] host identificator from database                *
 *             agent   - [IN] discovery item identificator from database      *
 *             jp_data - [IN] received data                                   *
 *                                                                            *
 * Return value: SUCCEED - if graphs were successfully added/updated or       *
 *                         adding/updating was not necessary                  *
 *               FAIL    - graphs cannot be added/updated                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
int	lld_update_graphs(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows, char **error)
{
	const char		*__function_name = "lld_update_graphs";

	int			ret = SUCCEED;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_ptr_t	graphs;
	zbx_vector_ptr_t	gitems_proto;
	zbx_vector_ptr_t	items;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_ptr_create(&graphs);		/* list of graphs which were created or will be created or */
						/* updated by the graph prototype */
	zbx_vector_ptr_create(&gitems_proto);	/* list of graphs_items which are used by the graph prototype */
	zbx_vector_ptr_create(&items);		/* list of items which are related to the graph prototype */

	result = DBselect(
			"select distinct g.graphid,g.name,g.width,g.height,g.yaxismin,g.yaxismax,g.show_work_period,"
				"g.show_triggers,g.graphtype,g.show_legend,g.show_3d,g.percent_left,g.percent_right,"
				"g.ymin_type,g.ymin_itemid,g.ymax_type,g.ymax_itemid"
			" from graphs g,graphs_items gi,items i,item_discovery id"
			" where g.graphid=gi.graphid"
				" and gi.itemid=i.itemid"
				" and i.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	parent_graphid, ymin_itemid_proto, ymax_itemid_proto;
		const char	*name_proto;
		int		width, height;
		double		yaxismin, yaxismax, percent_left, percent_right;
		unsigned char	show_work_period, show_triggers, graphtype, show_legend, show_3d,
				ymin_type, ymax_type;

		ZBX_STR2UINT64(parent_graphid, row[0]);
		name_proto = row[1];
		width = atoi(row[2]);
		height = atoi(row[3]);
		yaxismin = atof(row[4]);
		yaxismax = atof(row[5]);
		ZBX_STR2UCHAR(show_work_period, row[6]);
		ZBX_STR2UCHAR(show_triggers, row[7]);
		ZBX_STR2UCHAR(graphtype, row[8]);
		ZBX_STR2UCHAR(show_legend, row[9]);
		ZBX_STR2UCHAR(show_3d, row[10]);
		percent_left = atof(row[11]);
		percent_right = atof(row[12]);
		ZBX_STR2UCHAR(ymin_type, row[13]);
		ZBX_DBROW2UINT64(ymin_itemid_proto, row[14]);
		ZBX_STR2UCHAR(ymax_type, row[15]);
		ZBX_DBROW2UINT64(ymax_itemid_proto, row[16]);

		lld_graphs_get(parent_graphid, &graphs, width, height, yaxismin, yaxismax, show_work_period,
				show_triggers, graphtype, show_legend, show_3d, percent_left, percent_right,
				ymin_type, ymax_type);
		lld_gitems_get(parent_graphid, &gitems_proto, &graphs);
		lld_items_get(&gitems_proto, ymin_itemid_proto, ymax_itemid_proto, &items);

		/* making graphs */

		lld_graphs_make(&gitems_proto, &graphs, &items, name_proto, ymin_itemid_proto, ymax_itemid_proto,
				lld_rows);
		lld_graphs_validate(hostid, &graphs, error);
		ret = lld_graphs_save(hostid, parent_graphid, &graphs, width, height, yaxismin, yaxismax,
				show_work_period, show_triggers, graphtype, show_legend, show_3d, percent_left,
				percent_right, ymin_type, ymax_type);

		lld_items_free(&items);
		lld_gitems_free(&gitems_proto);
		lld_graphs_free(&graphs);
	}
	DBfree_result(result);

	zbx_vector_ptr_destroy(&items);
	zbx_vector_ptr_destroy(&gitems_proto);
	zbx_vector_ptr_destroy(&graphs);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}
/******************************************************************************
 * *
 *这段代码的主要目的是更新图层信息。首先，通过数据库查询获取图层的相关信息，然后创建图层、校验图层信息，最后保存图层到数据库。在整个过程中，涉及到图层、图层项、相关物品的创建、校验和释放等操作。
 ******************************************************************************/
/* 定义函数名和日志级别 */
int	lld_update_graphs(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows, char **error);
const char	*__function_name = "lld_update_graphs";

/* 定义变量，用于存储数据库查询结果、图层、图层项、相关物品等 */
int			ret = SUCCEED;
DB_RESULT		result;
DB_ROW			row;
zbx_vector_ptr_t	graphs;
zbx_vector_ptr_t	gitems_proto;
zbx_vector_ptr_t	items;

/* 打印日志，表示函数开始执行 */
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

/* 创建图层列表、图层项列表、相关物品列表 */
zbx_vector_ptr_create(&graphs);
zbx_vector_ptr_create(&gitems_proto);
zbx_vector_ptr_create(&items);

/* 执行数据库查询，获取图层信息 */
result = DBselect(
    "select distinct g.graphid,g.name,g.width,g.height,g.yaxismin,g.yaxismax,g.show_work_period,"
    "g.show_triggers,g.graphtype,g.show_legend,g.show_3d,g.percent_left,g.percent_right,"
    "g.ymin_type,g.ymin_itemid,g.ymax_type,g.ymax_itemid"
    " from graphs g,graphs_items gi,items i,item_discovery id"
    " where g.graphid=gi.graphid"
    " and gi.itemid=i.itemid"
    " and i.itemid=id.itemid"
    " and id.parent_itemid=" ZBX_FS_UI64,
    lld_ruleid);

/* 遍历查询结果，解析图层信息 */
while (SUCCEED == ret && NULL != (row = DBfetch(result)))
{
    zbx_uint64_t	parent_graphid, ymin_itemid_proto, ymax_itemid_proto;
    const char	*name_proto;
    int		width, height;
    double		yaxismin, yaxismax, percent_left, percent_right;
    unsigned char	show_work_period, show_triggers, graphtype, show_legend, show_3d,
                ymin_type, ymax_type;

    /* 解析图层ID、名称、宽度、高度、坐标范围、是否显示工作周期、是否显示触发器、图层类型、是否显示图例、是否三维、左侧百分比、右侧百分比、坐标范围类型、最小坐标项ID、最大坐标范围类型、最大坐标项ID */
    ZBX_STR2UINT64(parent_graphid, row[0]);
    name_proto = row[1];
    width = atoi(row[2]);
    height = atoi(row[3]);
    yaxismin = atof(row[4]);
    yaxismax = atof(row[5]);
    ZBX_STR2UCHAR(show_work_period, row[6]);
    ZBX_STR2UCHAR(show_triggers, row[7]);
    ZBX_STR2UCHAR(graphtype, row[8]);
    ZBX_STR2UCHAR(show_legend, row[9]);
    ZBX_STR2UCHAR(show_3d, row[10]);
    percent_left = atof(row[11]);
    percent_right = atof(row[12]);
    ZBX_STR2UCHAR(ymin_type, row[13]);
    ZBX_DBROW2UINT64(ymin_itemid_proto, row[14]);
    ZBX_STR2UCHAR(ymax_type, row[15]);
    ZBX_DBROW2UINT64(ymax_itemid_proto, row[16]);

    /* 获取图层信息并创建图层 */
    lld_graphs_get(parent_graphid, &graphs, width, height, yaxismin, yaxismax, show_work_period,
                show_triggers, graphtype, show_legend, show_3d, percent_left, percent_right,
                ymin_type, ymax_type);
    lld_gitems_get(parent_graphid, &gitems_proto, &graphs);
    lld_items_get(&gitems_proto, ymin_itemid_proto, ymax_itemid_proto, &items);

    /* 创建图层并校验 */
    lld_graphs_make(&gitems_proto, &graphs, &items, name_proto, ymin_itemid_proto, ymax_itemid_proto,
                lld_rows);
    lld_graphs_validate(hostid, &graphs, error);

    /* 保存图层信息 */
    ret = lld_graphs_save(hostid, parent_graphid, &graphs, width, height, yaxismin, yaxismax,
                show_work_period, show_triggers, graphtype, show_legend, show_3d, percent_left,
                percent_right, ymin_type, ymax_type);

    /* 释放资源 */
    lld_items_free(&items);
    lld_gitems_free(&gitems_proto);
    lld_graphs_free(&graphs);
}

/* 释放数据库查询结果，并清理变量 */
DBfree_result(result);

/* 打印日志，表示函数执行完毕 */
zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

/* 返回执行结果 */
return ret;

