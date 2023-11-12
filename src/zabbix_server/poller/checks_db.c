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

#include "checks_db.h"

#ifdef HAVE_UNIXODBC

#include "log.h"
#include "../odbc/odbc.h"

/******************************************************************************
 *                                                                            *
 * Function: get_value_db                                                     *
 *                                                                            *
 * Purpose: retrieve data from database                                       *
 *                                                                            *
 * Parameters: item   - [IN] item we are interested in                        *
 *             result - [OUT] check result                                    *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为get_value_db的C语言函数，该函数接收两个参数，一个是DC_ITEM类型的指针，另一个是AGENT_RESULT类型的指针。函数的主要功能是根据给定的物品键和DSN连接到数据库，执行查询并将查询结果转换为文本，最后将结果返回给调用者。如果在执行过程中遇到错误，函数会设置相应的错误信息并返回。
 ******************************************************************************/
// 定义一个C语言函数，名为get_value_db，接收两个参数，一个是DC_ITEM类型的指针，另一个是AGENT_RESULT类型的指针
int get_value_db(DC_ITEM *item, AGENT_RESULT *result)
{
    // 定义一个常量字符串，表示函数名称
    const char *__function_name = "get_value_db";

    // 初始化一个AGENT_REQUEST类型的结构体
    AGENT_REQUEST request;

    // 定义一个指向数据源的字符串指针
    const char *dsn;

    // 定义一个zbx_odbc_data_source_t类型的指针，用于存储数据源
    zbx_odbc_data_source_t *data_source;

    // 定义一个zbx_odbc_query_result_t类型的指针，用于存储查询结果
    zbx_odbc_query_result_t *query_result;

    // 定义一个字符指针，用于存储错误信息
    char *error = NULL;

    // 定义一个函数指针，用于将查询结果转换为文本
    int (*query_result_to_text)(zbx_odbc_query_result_t *query_result, char **text, char **error),
        // 定义一个整型变量，用于存储函数返回值
        ret = NOTSUPPORTED;

    // 记录日志，表示函数的开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() key_orig:'%s' query:'%s'", __function_name, item->key_orig, item->params);

    // 初始化请求结构体
    init_request(&request);

    // 解析物品键，如果解析失败，设置错误信息并退出
    if (SUCCEED != parse_item_key(item->key, &request))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
        goto out;
    }

    // 如果请求的键是"db.odbc.select"或"db.odbc.discovery"，则分别设置对应的查询结果转换函数
    if (0 == strcmp(request.key, "db.odbc.select"))
    {
        query_result_to_text = zbx_odbc_query_result_to_string;
    }
    else if (0 == strcmp(request.key, "db.odbc.discovery"))
    {
        query_result_to_text = zbx_odbc_query_result_to_lld_json;
    }
    else
    {
        // 否则，设置错误信息并退出
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported item key for this item type."));
        goto out;
    }

    // 检查参数数量是否为2
    if (2 != request.nparam)
    {
        // 如果是，设置错误信息并退出
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 忽略第一个参数，仅用于区分相同DSN的查询
    dsn = request.params[1];

    // 如果DSN为空或第一个字符为'\0'，设置错误信息并退出
    if (NULL == dsn || '\0' == *dsn)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    // 连接到数据库，如果成功，继续执行后续操作
    if (NULL != (data_source = zbx_odbc_connect(dsn, item->username, item->password, CONFIG_TIMEOUT, &error)))
    {
        // 如果查询成功，继续执行后续操作
        if (NULL != (query_result = zbx_odbc_select(data_source, item->params, &error)))
        {
            // 将查询结果转换为文本，如果成功，设置返回码和结果文本
            char *text = NULL;

            if (SUCCEED == query_result_to_text(query_result, &text, &error))
            {
                // 设置返回码为SUCCEED，并将结果文本存储在result中
                SET_TEXT_RESULT(result, text);
                ret = SUCCEED;
            }

            // 释放查询结果内存
            zbx_odbc_query_result_free(query_result);
        }

        // 释放数据源内存
        zbx_odbc_data_source_free(data_source);
    }

    // 如果查询失败，设置错误信息并退出
    if (SUCCEED != ret)
        SET_MSG_RESULT(result, error);

out:
    // 释放请求内存
    free_request(&request);

    // 记录日志，表示函数的结束执行
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回函数执行结果
    return ret;
}


#endif	/* HAVE_UNIXODBC */
