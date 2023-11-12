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

#ifdef HAVE_UNIXODBC

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

#include "odbc.h"
#include "log.h"
#include "zbxjson.h"
#include "zbxalgo.h"

struct zbx_odbc_data_source
{
	SQLHENV	henv;
	SQLHDBC	hdbc;
};

struct zbx_odbc_query_result
{
	SQLHSTMT	hstmt;
	SQLSMALLINT	col_num;
	char		**row;
};

/******************************************************************************
 *                                                                            *
 * Function: zbx_odbc_rc_str                                                  *
 *                                                                            *
 * Purpose: get human readable representation of ODBC return code             *
 *                                                                            *
 * Parameters: rc - [IN] ODBC return code                                     *
 *                                                                            *
 * Return value: human readable representation of error code or NULL if the   *
 *               given code is unknown                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是根据传入的 SQLRETURN 类型的 rc 参数，返回对应的错误代码字符串。其中，使用 switch 语句对不同的 rc 值进行分支处理，将对应的错误代码转换为字符串并返回。如果 rc 的值不属于预设的错误代码范围，则返回 NULL 字符串。
 ******************************************************************************/
// 定义一个名为 zbx_odbc_rc_str 的静态常量字符指针变量，该函数用于根据 SQLRETURN 类型的参数 rc 返回对应的错误代码字符串
static const char *zbx_odbc_rc_str(SQLRETURN rc)
{
	// 使用 switch 语句根据 rc 的值进行分支处理
	switch (rc)
	{
		// 如果是 SQL_ERROR，返回 "SQL_ERROR" 字符串
		case SQL_ERROR:
			return "SQL_ERROR";
		// 如果是 SQL_SUCCESS_WITH_INFO，返回 "SQL_SUCCESS_WITH_INFO" 字符串
		case SQL_SUCCESS_WITH_INFO:
			return "SQL_SUCCESS_WITH_INFO";
		// 如果是 SQL_NO_DATA，返回 "SQL_NO_DATA" 字符串
		case SQL_NO_DATA:
			return "SQL_NO_DATA";
		// 如果是 SQL_INVALID_HANDLE，返回 "SQL_INVALID_HANDLE" 字符串
		case SQL_INVALID_HANDLE:
			return "SQL_INVALID_HANDLE";
		// 如果是 SQL_STILL_EXECUTING，返回 "SQL_STILL_EXECUTING" 字符串
		case SQL_STILL_EXECUTING:
			return "SQL_STILL_EXECUTING";
		// 如果是 SQL_NEED_DATA，返回 "SQL_NEED_DATA" 字符串
		case SQL_NEED_DATA:
			return "SQL_NEED_DATA";
		// 如果是 SQL_SUCCESS，返回 "SQL_SUCCESS" 字符串
		case SQL_SUCCESS:
			return "SQL_SUCCESS";
		// 如果 rc 的值不属于以上情况，返回 NULL 字符串
		default:
			return NULL;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_odbc_diag                                                    *
/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个名为 zbx_odbc_diag 的静态函数，参数包括：
// h_type：ODBC错误类型
// h：错误句柄
// rc：SQLRETURN 返回码
// diag：诊断信息指针
// 该函数的主要目的是获取并处理 ODBC 错误信息，并将处理后的诊断信息存储在 diag 指针指向的内存空间中。

static int	zbx_odbc_diag(SQLSMALLINT h_type, SQLHANDLE h, SQLRETURN rc, char **diag)
{
	// 定义一些常量和变量，用于存储调试信息和错误处理
	const char	*__function_name = "zbx_odbc_diag";
	const char	*rc_str = NULL;
	char		*buffer = NULL;
	size_t		alloc = 0, offset = 0;

	// 判断 rc 的值，如果为 SQL_ERROR 或 SQL_SUCCESS_WITH_INFO，则进行以下操作：
	if (SQL_ERROR == rc || SQL_SUCCESS_WITH_INFO == rc)
	{
		// 获取 SQLSTATE 信息，err_code 错误代码，以及错误信息
		SQLCHAR		sql_state[SQL_SQLSTATE_SIZE + 1], err_msg[128];
		SQLINTEGER	err_code = 0;
		SQLSMALLINT	rec_nr = 1;

		// 使用 SQLGetDiagRec 函数获取错误记录，直到返回值为非零
		while (0 != SQL_SUCCEEDED(SQLGetDiagRec(h_type, h, rec_nr++, sql_state, &err_code, err_msg,
				sizeof(err_msg), NULL)))
		{
			// 为 buffer 分配内存，并拼接错误信息
			zbx_chrcpy_alloc(&buffer, &alloc, &offset, (NULL == buffer ? ':' : '|'));
			zbx_snprintf_alloc(&buffer, &alloc, &offset, "[%s][%ld][%s]", sql_state, (long)err_code, err_msg);
		}
	}

	// 如果 rc 为非零，则获取 rc 的描述字符串
	if (0 != SQL_SUCCEEDED(rc))
	{
		// 如果找不到 rc 的描述字符串，则使用默认日志记录
		if (NULL == (rc_str = zbx_odbc_rc_str(rc)))
		{
			zabbix_log(LOG_LEVEL_TRACE, "%s(): [%d (unknown SQLRETURN code)]%s", __function_name,
					(int)rc, ZBX_NULL2EMPTY_STR(buffer));
		}
		// 否则，使用 zbx_odbc_rc_str 函数获取描述字符串，并记录日志
		else
			zabbix_log(LOG_LEVEL_TRACE, "%s(): [%s]%s", __function_name, rc_str, ZBX_NULL2EMPTY_STR(buffer));
	}
	// 如果 rc 为零，则执行以下操作：
	else
	{
		// 如果找不到 rc 的描述字符串，则拼接诊断信息
		if (NULL == (rc_str = zbx_odbc_rc_str(rc)))
		{
			*diag = zbx_dsprintf(*diag, "[%d (unknown SQLRETURN code)]%s",
					(int)rc, ZBX_NULL2EMPTY_STR(buffer));
		}
		// 否则，使用 zbx_odbc_rc_str 函数获取描述字符串，并拼接诊断信息
		else
			*diag = zbx_dsprintf(*diag, "[%s]%s", rc_str, ZBX_NULL2EMPTY_STR(buffer));

		// 记录日志
		zabbix_log(LOG_LEVEL_TRACE, "%s(): %s", __function_name, *diag);
	}

	// 释放 buffer 分配的内存
	zbx_free(buffer);

	// 返回 0 表示成功，否则返回失败
	return 0 != SQL_SUCCEEDED(rc) ? SUCCEED : FAIL;
}

		if (NULL == (rc_str = zbx_odbc_rc_str(rc)))
		{
			*diag = zbx_dsprintf(*diag, "[%d (unknown SQLRETURN code)]%s",
					(int)rc, ZBX_NULL2EMPTY_STR(buffer));
		}
		else
			*diag = zbx_dsprintf(*diag, "[%s]%s", rc_str, ZBX_NULL2EMPTY_STR(buffer));

		zabbix_log(LOG_LEVEL_TRACE, "%s(): %s", __function_name, *diag);
	}

	zbx_free(buffer);

	return 0 != SQL_SUCCEEDED(rc) ? SUCCEED : FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_log_odbc_connection_info                                     *
 *                                                                            *
 * Purpose: log details upon successful connection on behalf of caller        *
 *                                                                            *
 * Parameters: function - [IN] caller function name                           *
 *             hdbc     - [IN] ODBC connection handle                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是获取ODBC连接的驱动程序和数据库的相关信息（如名称、版本号等），并在DEBUG级别日志中输出这些信息。具体来说，代码实现了以下功能：
 *
 *1. 检查日志级别，如果为DEBUG级别，则执行以下操作。
 *2. 定义字符串变量，用于存储驱动程序名称、版本号、数据库名称、数据库版本。
 *3. 获取驱动程序和数据库的相关信息。
 *4. 如果无法获取诊断信息，记录日志并使用默认值替换字符串。
 *5. 输出连接信息，包括函数名、数据库名称、版本号、驱动程序名称、版本号。
 *6. 释放诊断信息内存。
 ******************************************************************************/
// 定义一个静态函数zbx_log_odbc_connection_info，接收两个参数：const char *function（函数名）和SQLHDBC hdbc（ODBC连接句柄）
static void zbx_log_odbc_connection_info(const char *function, SQLHDBC hdbc)
{
	// 检查日志级别，如果为DEBUG级别，则执行以下操作
	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		// 定义一些字符串变量，用于存储驱动程序名称、版本号、数据库名称、数据库版本
		char driver_name[MAX_STRING_LEN + 1], driver_ver[MAX_STRING_LEN + 1],
			db_name[MAX_STRING_LEN + 1], db_ver[MAX_STRING_LEN + 1], *diag = NULL;
		SQLRETURN rc;

		// 获取驱动程序名称
		rc = SQLGetInfo(hdbc, SQL_DRIVER_NAME, driver_name, MAX_STRING_LEN, NULL);
/******************************************************************************
 * *
 *这段代码的主要目的是用于连接到ODBC数据源。它接受五个参数：
 *
 *1. `dsn`：数据源标识符。
 *2. `user`：用户名。
 *3. `pass`：密码。
 *4. `timeout`：连接超时时间。
 *5. `error`：错误信息指针，用于存储连接失败时的错误信息。
 *
 *代码首先记录日志，然后分配内存用于存储数据源结构体。接着分配环境句柄并设置ODBC版本，再分配连接句柄。之后尝试连接到数据源，如果连接成功，记录连接信息并返回数据源；如果连接失败，释放资源并返回错误信息。
 ******************************************************************************/
/*
 * zbx_odbc_connect.c
 * 版权：Zabbix SIA
 * 创建日期：2017-04-20
 * 作者：Alina Timofeeva
 * 电子邮件：alina.timofeeva@zabbix.com
 * 电话：+371 29299907
 * 功能：连接到ODBC数据源
 * 编译器：gcc 4.8.5
 * 编译日期：2017-04-20
 * 文件路径：src/odbc/zbx_odbc_connect.c
 */

#include "zbx_odbc.h"
#include "zbx_log.h"
#include "zbx_pool.h"

zbx_odbc_data_source_t *zbx_odbc_connect(const char *dsn, const char *user, const char *pass, int timeout, char **error)
{
	/* 定义函数名 */
	const char *__function_name = "zbx_odbc_connect";

	/* 初始化诊断信息 */
	char *diag = NULL;

	/* 初始化数据源指针 */
	zbx_odbc_data_source_t *data_source = NULL;

	/* 初始化SQLRETURN变量 */
	SQLRETURN rc;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() dsn:'%s' user:'%s'", __function_name, dsn, user);

	/* 分配内存用于存储数据源结构体 */
	data_source = (zbx_odbc_data_source_t *)zbx_malloc(data_source, sizeof(zbx_odbc_data_source_t));

	/* 分配内存失败则返回NULL */
	if (data_source == NULL)
	{
		*error = zbx_strdup(*error, "Memory allocation failed.");
		goto out;
	}

	/* 分配环境句柄 */
	if (0 != SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &data_source->henv)))
	{
		/* 设置ODBC版本 */
		rc = SQLSetEnvAttr(data_source->henv, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);

		/* 获取诊断信息 */
		if (SUCCEED == zbx_odbc_diag(SQL_HANDLE_ENV, data_source->henv, rc, &diag))
		{
			/* 分配连接句柄 */
			if (SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, data_source->henv, &data_source->hdbc)))
			{
				/* 设置连接超时 */
				rc = SQLSetConnectAttr(data_source->hdbc, (SQLINTEGER)SQL_LOGIN_TIMEOUT,
						(SQLPOINTER)(intptr_t)timeout, (SQLINTEGER)0);

				/* 获取诊断信息 */
				if (SUCCEED == zbx_odbc_diag(SQL_HANDLE_DBC, data_source->hdbc, rc, &diag))
				{
					/* 连接到数据源 */
					rc = SQLConnect(data_source->hdbc, (SQLCHAR *)dsn, SQL_NTS, (SQLCHAR *)user,
							SQL_NTS, (SQLCHAR *)pass, SQL_NTS);

					/* 获取诊断信息 */
					if (SUCCEED == zbx_odbc_diag(SQL_HANDLE_DBC, data_source->hdbc, rc, &diag))
					{
						/* 记录连接信息 */
						zbx_log_odbc_connection_info(__function_name, data_source->hdbc);
						goto out;
					}
					else
					{
						/* 连接失败，释放资源并返回错误信息 */
						*error = zbx_dsprintf(*error, "Cannot connect to ODBC DSN: %s", diag);
						goto out;
					}
				}
				else
				{
					/* 设置连接句柄失败，释放资源并返回错误信息 */
					*error = zbx_dsprintf(*error, "Cannot create ODBC connection handle: %s", diag);
					goto out;
				}
			}
			else
			{
				/* 分配连接句柄失败，释放资源并返回错误信息 */
				*error = zbx_dsprintf(*error, "Cannot allocate ODBC connection handle.");
				goto out;
			}
		}
		else
		{
			/* 设置ODBC版本失败，释放资源并返回错误信息 */
			*error = zbx_dsprintf(*error, "Cannot set ODBC version: %s", diag);
			goto out;
		}
	}
	else
	{
		/* 创建环境句柄失败，释放资源并返回错误信息 */
		*error = zbx_dsprintf(*error, "Cannot create ODBC environment handle.");
		goto out;
	}

out:
	/* 释放分配的内存 */
	zbx_free(data_source);

	/* 释放诊断信息 */
	zbx_free(diag);

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	/* 返回连接成功的数据源，或者释放内存失败时的错误信息 */
	return data_source;
}

				if (SUCCEED == zbx_odbc_diag(SQL_HANDLE_DBC, data_source->hdbc, rc, &diag))
				{
					/* look for user in data source instead of no user */
					if ('\0' == *user)
						user = NULL;

					/* look for password in data source instead of no password */
					if ('\0' == *pass)
						pass = NULL;

					rc = SQLConnect(data_source->hdbc, (SQLCHAR *)dsn, SQL_NTS, (SQLCHAR *)user,
							SQL_NTS, (SQLCHAR *)pass, SQL_NTS);

					if (SUCCEED == zbx_odbc_diag(SQL_HANDLE_DBC, data_source->hdbc, rc, &diag))
					{
						zbx_log_odbc_connection_info(__function_name, data_source->hdbc);
						goto out;
					}

					*error = zbx_dsprintf(*error, "Cannot connect to ODBC DSN: %s", diag);
				}
				else
					*error = zbx_dsprintf(*error, "Cannot set ODBC login timeout: %s", diag);

				SQLFreeHandle(SQL_HANDLE_DBC, data_source->hdbc);
			}
			else
				*error = zbx_dsprintf(*error, "Cannot create ODBC connection handle: %s", diag);
		}
		else
			*error = zbx_dsprintf(*error, "Cannot set ODBC version: %s", diag);

		SQLFreeHandle(SQL_HANDLE_ENV, data_source->henv);
	}
	else
		*error = zbx_strdup(*error, "Cannot create ODBC environment handle.");

	zbx_free(data_source);
out:
	zbx_free(diag);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return data_source;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_odbc_data_source_free                                        *
 *                                                                            *
 * Purpose: free resources allocated by successful zbx_odbc_connect() call    *
 *                                                                            *
 * Parameters: data_source - [IN] pointer to data source structure            *
 *                                                                            *
 * Comments: Input parameter data_source must be obtained using               *
 *           zbx_odbc_connect() and must not be NULL.                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放ODBC数据源相关资源，包括断开与数据库的连接、释放数据源句柄、释放环境句柄以及释放数据源结构体内存。在这个函数中，依次调用了SQLDisconnect()、SQLFreeHandle()和zbx_free()函数来完成这些操作。
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是使用ODBC执行给定的SQL查询，并返回查询结果。代码首先分配内存用于存储查询结果的结构体，然后分配SQL语句句柄并执行查询。接着获取查询结果的列数，并根据列数分配内存用于存储每一行的数据。最后，循环获取每一行的数据并将其存储在分配的内存中。如果在执行过程中出现错误，则返回相应的错误信息。整个函数的执行过程都使用了详细的中文注释，以便于理解其功能和逻辑。
 ******************************************************************************/
/*
 * zbx_odbc_select.c
 * 版权：Zabbix SIA
 * 授权：GPL-2.0+
 * 功能：使用ODBC执行SQL查询并返回结果
 */

#include "zbx_odbc.h"

zbx_odbc_query_result_t *zbx_odbc_select(const zbx_odbc_data_source_t *data_source, const char *query, char **error)
{
	/* 定义函数名 */
	const char *__function_name = "zbx_odbc_select";

	/* 初始化诊断信息 */
	char *diag = NULL;

	/* 初始化查询结果结构体指针 */
	zbx_odbc_query_result_t *query_result = NULL;

	/* 初始化SQL返回码 */
	SQLRETURN rc;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() query:'%s'", __function_name, query);

	/* 分配内存用于存储查询结果 */
	query_result = (zbx_odbc_query_result_t *)zbx_malloc(query_result, sizeof(zbx_odbc_query_result_t));

	/* 分配SQL语句句柄 */
	rc = SQLAllocHandle(SQL_HANDLE_STMT, data_source->hdbc, &query_result->hstmt);

	/* 获取诊断信息 */
	if (SUCCEED == zbx_odbc_diag(SQL_HANDLE_DBC, data_source->hdbc, rc, &diag))
	{
		/* 执行SQL查询 */
		rc = SQLExecDirect(query_result->hstmt, (SQLCHAR *)query, SQL_NTS);

		/* 获取查询结果的列数 */
		if (SUCCEED == zbx_odbc_diag(SQL_HANDLE_STMT, query_result->hstmt, rc, &diag))
		{
			rc = SQLNumResultCols(query_result->hstmt, &query_result->col_num);

			/* 分配内存用于存储每一行的数据 */
			query_result->row = (char **)zbx_malloc(NULL, sizeof(char *) * (size_t)query_result->col_num);

			/* 循环获取每一行的数据 */
			SQLSMALLINT i;

			for (i = 0; ; i++)
			{
				/* 如果已经获取到所有列的数据，则退出循环 */
				if (i == query_result->col_num)
				{
					zabbix_log(LOG_LEVEL_DEBUG, "selected all %d columns", (int)query_result->col_num);
					break;
				}

				/* 初始化每一行的数据为NULL */
				query_result->row[i] = NULL;
			}
		}
		else
			*error = zbx_dsprintf(*error, "Cannot get number of columns in ODBC result: %s", diag);
	}
	else
		*error = zbx_dsprintf(*error, "Cannot create ODBC statement handle: %s", diag);

	/* 释放资源 */
	SQLFreeHandle(SQL_HANDLE_STMT, query_result->hstmt);

	/* 结束 */
	zbx_free(diag);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	/* 返回查询结果 */
	return query_result;
}

				query_result->row = (char **)zbx_malloc(NULL, sizeof(char *) * (size_t)query_result->col_num);

				for (i = 0; ; i++)
				{
					if (i == query_result->col_num)
					{
						zabbix_log(LOG_LEVEL_DEBUG, "selected all %d columns",
								(int)query_result->col_num);
						goto out;
					}

					query_result->row[i] = NULL;
				}
			}
			else
				*error = zbx_dsprintf(*error, "Cannot get number of columns in ODBC result: %s", diag);
		}
		else
			*error = zbx_dsprintf(*error, "Cannot execute ODBC query: %s", diag);

		SQLFreeHandle(SQL_HANDLE_STMT, query_result->hstmt);
	}
	else
		*error = zbx_dsprintf(*error, "Cannot create ODBC statement handle: %s", diag);

	zbx_free(query_result);
out:
	zbx_free(diag);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return query_result;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_odbc_query_result_free                                       *
 *                                                                            *
 * Purpose: free resources allocated by successful zbx_odbc_select() call     *
 *                                                                            *
 * Parameters: query_result - [IN] pointer to query result structure          *
 *                                                                            *
 * Comments: Input parameter query_result must be obtained using              *
 *           zbx_odbc_select() and must not be NULL.                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放与zbx_odbc_query_result结构体相关的内存资源，包括语句句柄、列数据内存、行数据内存以及zbx_odbc_query_result结构体本身。这个函数在处理完查询结果后，对相关资源进行清理，确保程序不会因为未释放的内存而产生内存泄漏。
 ******************************************************************************/
void	zbx_odbc_query_result_free(zbx_odbc_query_result_t *query_result) // 定义一个函数，用于释放zbx_odbc_query_result类型的指针
{
	SQLSMALLINT	i; // 定义一个整型变量i，用于循环计数

	SQLFreeHandle(SQL_HANDLE_STMT, query_result->hstmt); // 释放与查询结果相关的ODBC语句句柄

	for (i = 0; i < query_result->col_num; i++) // 遍历查询结果中的列数量
		zbx_free(query_result->row[i]); // 释放每一列的数据内存

	zbx_free(query_result->row); // 释放查询结果中的行数据内存
	zbx_free(query_result); // 释放整个查询结果结构体的内存
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_odbc_fetch                                                   *
 *                                                                            *
 * Purpose: fetch single row of ODBC query result                             *
 *                                                                            *
 * Parameters: query_result - [IN] pointer to query result structure          *
 *                                                                            *
 * Return value: array of strings or NULL (see Comments)                      *
 *                                                                            *
 * Comments: NULL result can signify both end of rows (which is normal) and   *
 *           failure. There is currently no way to distinguish these cases.   *
 *           There is no need to free strings returned by this function.      *
 *           Lifetime of strings is limited to next call of zbx_odbc_fetch()  *
 *           or zbx_odbc_query_result_free(), caller needs to make a copy if  *
 *           result is needed for longer.                                     *
 *                                                                            *
 ******************************************************************************/
static const char	*const *zbx_odbc_fetch(zbx_odbc_query_result_t *query_result)
{
	const char	*__function_name = "zbx_odbc_fetch";
	char		*diag = NULL;
	SQLRETURN	rc;
	SQLSMALLINT	i;
	const char	*const *row = NULL;
/******************************************************************************
 * *
 *整个代码块的主要目的是从ODBC数据库中查询数据，并将查询结果的每一列数据存储在内存中。当查询结束时，释放内存并返回查询结果的行数据。在整个过程中，还对遇到的错误进行了调试记录。
 ******************************************************************************/
// 定义日志级别，DEBUG表示调试信息
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

// 判断SQL_NO_DATA是否等于SQLFetch函数的返回值
if (SQL_NO_DATA == (rc = SQLFetch(query_result->hstmt)))
{
    /* 结束当前行 */
    goto out;
}

// 判断zbx_odbc_diag函数是否成功执行
if (SUCCEED != zbx_odbc_diag(SQL_HANDLE_STMT, query_result->hstmt, rc, &diag))
{
    // 打印调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "Cannot fetch row: %s", diag);
    // 跳转到out标签处
    goto out;
}

// 遍历查询结果的每一列
for (i = 0; i < query_result->col_num; i++)
{
    size_t		alloc = 0, offset = 0;
    char		buffer[MAX_STRING_LEN + 1];
    SQLLEN		len;

    // 释放上一行的内存
    zbx_free(query_result->row[i]);

    // 获取列数据
    do
    {
        rc = SQLGetData(query_result->hstmt, i + 1, SQL_C_CHAR, buffer, MAX_STRING_LEN, &len);

        // 判断zbx_odbc_diag函数是否成功执行
        if (SUCCEED != zbx_odbc_diag(SQL_HANDLE_STMT, query_result->hstmt, rc, &diag))
        {
            // 打印调试日志
            zabbix_log(LOG_LEVEL_DEBUG, "Cannot get column data: %s", diag);
            // 跳转到out标签处
            goto out;
        }

        // 判断是否为空数据
        if (SQL_NULL_DATA == (int)len)
            break;

        // 分配内存并拷贝数据
        zbx_strcpy_alloc(&query_result->row[i], &alloc, &offset, buffer);
    }
    while (SQL_SUCCESS != rc);

    // 去除字符串末尾的空白字符
    if (NULL != query_result->row[i])
        zbx_rtrim(query_result->row[i], " ");

    // 打印调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "column #%d value:'%s'", (int)i + 1, ZBX_NULL2STR(query_result->row[i]));
}

// 指向查询结果的行数据指针
row = (const char *const *)query_result->row;

out:
    // 释放诊断信息内存
    zbx_free(diag);

    // 打印调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

    // 返回查询结果的行数据
/******************************************************************************
 * *
 *整个代码块的主要目的是将ODBC查询结果的首行数据转换为字符串，并返回给调用者。过程中，首先判断传入的查询结果是否有效，如果有效，则将首行数据复制到指定的字符串空间，并替换无效的UTF-8字符。如果首行数据为空，则返回错误信息。最后，记录函数调用日志并返回函数执行结果。
 ******************************************************************************/
// 定义一个C函数，名为zbx_odbc_query_result_to_string
// 传入参数：zbx_odbc_query_result_t类型的指针query_result，char**类型的指针string，以及char**类型的指针error
// 返回值：int类型，表示函数执行结果，失败（FAIL）或成功（SUCCEED）
int zbx_odbc_query_result_to_string(zbx_odbc_query_result_t *query_result, char **string, char **error)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "zbx_odbc_query_result_to_string";
	// 定义一个指向zbx_odbc_fetch()函数的指针
	const char *const *row;
	// 定义一个整型变量，用于存储函数执行结果
	int ret = FAIL;

	// 记录函数调用日志，表示 debug 级别
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断传入的query_result是否不为空，如果不为空，则执行以下操作
	if (NULL != (row = zbx_odbc_fetch(query_result)))
	{
		// 判断fetch到的数据首行是否不为空，如果不为空，则执行以下操作
		if (NULL != row[0])
		{
			// 将首行数据复制到string指向的内存空间，并替换无效的UTF-8字符
			*string = zbx_strdup(*string, row[0]);
			zbx_replace_invalid_utf8(*string);
			// 更新函数执行结果为成功
			ret = SUCCEED;
		}
		else
		{
			// 如果首行数据为空，则将错误信息赋值给error指向的内存空间
			*error = zbx_strdup(*error, "SQL query returned NULL value.");
		}
	}
	// 如果fetch到的数据为空，则将错误信息赋值给error指向的内存空间
	else
	{
		*error = zbx_strdup(*error, "SQL query returned empty result.");
	}

	// 记录函数调用日志，表示 debug 级别，输出函数执行结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

	const char	*const *row;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (NULL != (row = zbx_odbc_fetch(query_result)))
	{
		if (NULL != row[0])
		{
			*string = zbx_strdup(*string, row[0]);
			zbx_replace_invalid_utf8(*string);
			ret = SUCCEED;
		}
		else
			*error = zbx_strdup(*error, "SQL query returned NULL value.");
	}
	else
		*error = zbx_strdup(*error, "SQL query returned empty result.");

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
/******************************************************************************
 * *
 *主要目的：将ODBC查询结果转换为JSON格式，并将结果存储在`lld_json`指向的字符串中。如果转换过程中出现错误，将错误信息存储在`error`指向的字符串中。
 *
 *整个代码块的功能可以分为以下几个部分：
 *
 *1. 初始化日志和变量。
 *2. 获取查询结果的每一列的列名，并将其转换为大写和检查是否为合法宏字符。
 *3. 将合法的宏名添加到`macros`向量中。
 *4. 初始化JSON结构体并添加数据标签。
 *5. 遍历查询结果的每一行，构造JSON对象，并添加列名和对应的数据。
 *6. 关闭并释放JSON对象。
 *7. 释放`macros`向量。
 *8. 返回转换结果。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_odbc_query_result_to_lld_json";
int LOG_LEVEL_DEBUG = 10;

// 定义结构体和变量
zbx_odbc_query_result_t *query_result;
char **lld_json;
char **error;

struct zbx_json json;
zbx_vector_str_t macros;

int ret = FAIL, i, j;

// 创建并初始化日志
zabbix_log_init();

// 创建并初始化macros向量
zbx_vector_str_create(&macros);
zbx_vector_str_reserve(&macros, query_result->col_num);

// 遍历查询结果的每一列
for (i = 0; i < query_result->col_num; i++)
{
    // 获取列名
    char str[MAX_STRING_LEN];
    SQLRETURN rc;
    SQLSMALLINT len;

    rc = SQLColAttribute(query_result->hstmt, i + 1, SQL_DESC_LABEL, str, sizeof(str), &len, NULL);

    // 判断是否获取到有效列名
    if (SQL_SUCCESS != rc || sizeof(str) <= (size_t)len || '\0' == *str)
    {
        *error = zbx_dsprintf(*error, "Cannot obtain column #%d name.", i + 1);
        goto out;
    }

    // 打印日志
    zabbix_log(LOG_LEVEL_DEBUG, "column #%d name:'%s'", i + 1, str);

    // 转换为大写并检查是否为合法宏字符
    for (j = 0; j < macros.size; j++)
    {
        if (0 == strcmp(macros.values[j], str))
        {
            *error = zbx_dsprintf(*error, "Duplicate macro name: %s.", macros.values[j]);
            goto out;
        }
    }

    // 添加宏到macros向量
    zbx_vector_str_append(&macros, zbx_dsprintf(NULL, "{#%s}", str));
}

// 初始化JSON结构体
zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
zbx_json_addarray(&json, ZBX_PROTO_TAG_DATA);

// 遍历查询结果并构造JSON对象
while (NULL != (row = zbx_odbc_fetch(query_result)))
{
    zbx_json_addobject(&json, NULL);

    // 遍历每一列并添加JSON对象
    for (i = 0; i < query_result->col_num; i++)
    {
        char *value = NULL;

        if (NULL != row[i])
        {
            value = zbx_strdup(value, row[i]);
            zbx_replace_invalid_utf8(value);
        }

        // 添加列名和值到JSON对象
        zbx_json_addstring(&json, macros.values[i], value, ZBX_JSON_TYPE_STRING);
        zbx_free(value);
    }

    // 关闭当前JSON对象
    zbx_json_close(&json);
}

// 关闭并释放JSON对象
zbx_json_close(&json);

// 释放macros向量
zbx_vector_str_clear_ext(&macros, zbx_str_free);

// 释放内存并返回结果
ret = SUCCEED;
goto out;

out:
zbx_vector_str_destroy(&macros);

zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

return ret;


			zbx_json_addstring(&json, macros.values[i], value, ZBX_JSON_TYPE_STRING);
			zbx_free(value);
		}

		zbx_json_close(&json);
	}

	zbx_json_close(&json);

	*lld_json = zbx_strdup(*lld_json, json.buffer);

	zbx_json_free(&json);

	ret = SUCCEED;
out:
	zbx_vector_str_clear_ext(&macros, zbx_str_free);
	zbx_vector_str_destroy(&macros);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

#endif	/* HAVE_UNIXODBC */
