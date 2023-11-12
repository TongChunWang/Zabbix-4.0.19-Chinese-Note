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

#include "zbxdb.h"

#if defined(HAVE_IBM_DB2)
#	include <sqlcli1.h>
#elif defined(HAVE_MYSQL)
#	include "mysql.h"
#	include "errmsg.h"
#	include "mysqld_error.h"
#elif defined(HAVE_ORACLE)
#	include "oci.h"
#elif defined(HAVE_POSTGRESQL)
#	include <libpq-fe.h>
#elif defined(HAVE_SQLITE3)
#	include <sqlite3.h>
#endif

#include "dbschema.h"
#include "log.h"
#if defined(HAVE_SQLITE3)
#	include "mutexs.h"
#endif

struct zbx_db_result
{
#if defined(HAVE_IBM_DB2)
	SQLHANDLE	hstmt;
	SQLSMALLINT	nalloc;
	SQLSMALLINT	ncolumn;
	DB_ROW		values;
	DB_ROW		values_cli;
	SQLINTEGER	*values_len;
#elif defined(HAVE_MYSQL)
	MYSQL_RES	*result;
#elif defined(HAVE_ORACLE)
	OCIStmt		*stmthp;	/* the statement handle for select operations */
	int 		ncolumn;
	DB_ROW		values;
	ub4		*values_alloc;
	OCILobLocator	**clobs;
#elif defined(HAVE_POSTGRESQL)
	PGresult	*pg_result;
	int		row_num;
	int		fld_num;
	int		cursor;
	DB_ROW		values;
#elif defined(HAVE_SQLITE3)
	int		curow;
	char		**data;
	int		nrow;
	int		ncolumn;
	DB_ROW		values;
#endif
};

static int	txn_level = 0;	/* transaction level, nested transactions are not supported */
static int	txn_error = ZBX_DB_OK;	/* failed transaction */
static int	txn_end_error = ZBX_DB_OK;	/* transaction result */

static char	*last_db_strerror = NULL;	/* last database error message */

extern int	CONFIG_LOG_SLOW_QUERIES;

#if defined(HAVE_IBM_DB2)
typedef struct
{
	SQLHANDLE	henv;
	SQLHANDLE	hdbc;
}
zbx_ibm_db2_handle_t;

static zbx_ibm_db2_handle_t	ibm_db2;

/******************************************************************************
 * *
 *整个代码块的主要目的是初始化各种数据库连接，并定义相应的错误处理函数。其中包括：
 *
 *1. 定义了`IBM_DB2server_status()`函数，用于获取IBM DB2服务器的状态。
 *2. 定义了`zbx_ibm_db2_success()`和`zbx_ibm_db2_success_ext()`两个静态函数，用于处理IBM DB2操作的成功和失败。
 *3. 定义了`zbx_ibm_db2_log_errors()`函数，用于处理数据库错误日志。
 *4. 根据是否包含相应库，初始化各种数据库连接，如MySQL、Oracle、PostgreSQL和SQLite3。
 *5. 定义了`OCI_DBserver_status()`函数，用于获取Oracle数据库服务器的状态。
 *6. 定义了`OCI_DBclean_result()`函数，用于清理Oracle数据库操作的结果。
 *7. 定义了`zbx_db_errlog()`函数，用于处理数据库错误日志。
 *
 *这些函数和变量主要用于处理各种数据库操作，并提供错误处理和日志记录功能。
 ******************************************************************************/
/* 定义静态函数，用于获取IBM DB2服务器的状态 */
static int IBM_DB2server_status();

/* 定义静态函数，用于处理IBM DB2操作的成功和失败 */
static int zbx_ibm_db2_success(SQLRETURN ret);
static int zbx_ibm_db2_success_ext(SQLRETURN ret);

/* 定义静态函数，用于处理数据库错误日志 */
static void zbx_ibm_db2_log_errors(SQLSMALLINT htype, SQLHANDLE hndl, zbx_err_codes_t err, const char *context);

/* 定义条件编译，根据是否包含相应库来设置连接指针 */
#elif defined(HAVE_MYSQL)
static MYSQL *conn = NULL;
#elif defined(HAVE_ORACLE)
#include "zbxalgo.h"

/* 定义结构体，用于保存Oracle数据库的连接信息 */
typedef struct
{
	OCIEnv *envhp;
	OCIError *errhp;
	OCISvcCtx *svchp;
	OCIServer *srvhp;
	OCIStmt *stmthp;	/* 用于执行操作的语句句柄 */
	zbx_vector_ptr_t db_results;
}
zbx_oracle_db_handle_t;

/* 定义Oracle数据库连接句柄 */
static zbx_oracle_db_handle_t oracle;

/* 定义函数，用于获取Oracle数据库服务器的状态 */
static ub4	OCI_DBserver_status(void);

#elif defined(HAVE_POSTGRESQL)
static PGconn *conn = NULL;
static unsigned int ZBX_PG_BYTEAOID = 0;
static int ZBX_PG_SVERSION = 0;
char ZBX_PG_ESCAPE_BACKSLASH = 1;

#elif defined(HAVE_SQLITE3)
static sqlite3 *conn = NULL;
static zbx_mutex_t sqlite_access = ZBX_MUTEX_NULL;

#endif

/* 定义函数，用于清理Oracle数据库操作的结果 */
#if defined(HAVE_ORACLE)
static void	OCI_DBclean_result(DB_RESULT result);
#endif

/* 定义函数，用于处理数据库错误日志 */
static void zbx_db_errlog(zbx_err_codes_t zbx_errno, int db_errno, const char *db_error, const char *context)
{
	char *s;

	if (NULL != db_error)
		last_db_strerror = zbx_strdup(last_db_strerror, db_error);
	else
		last_db_strerror = zbx_strdup(last_db_strerror, "");

	switch (zbx_errno)
	{
		case ERR_Z3001:
			s = zbx_dsprintf(NULL, "connection to database '%s' failed: [%d] %s", context, db_errno,
					last_db_strerror);
			break;
		case ERR_Z3002:
			s = zbx_dsprintf(NULL, "cannot create database '%s': [%d] %s", context, db_errno,
					last_db_strerror);
			break;
		case ERR_Z3003:
			s = zbx_strdup(NULL, "no connection to the database");
			break;
		case ERR_Z3004:
			s = zbx_dsprintf(NULL, "cannot close database: [%d] %s", db_errno, last_db_strerror);
			break;
		case ERR_Z3005:
			s = zbx_dsprintf(NULL, "query failed: [%d] %s [%s]", db_errno, last_db_strerror, context);
			break;
		case ERR_Z3006:
			s = zbx_dsprintf(NULL, "fetch failed: [%d] %s", db_errno, last_db_strerror);
			break;
		case ERR_Z3007:
			s = zbx_dsprintf(NULL, "query failed: [%d] %s", db_errno, last_db_strerror);
			break;
		default:
			s = zbx_strdup(NULL, "unknown error");
	}

	zabbix_log(LOG_LEVEL_ERR, "[Z%04d] %s", (int)zbx_errno, s);

	zbx_free(s);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_last_strerr                                               *
 *                                                                            *
 * Purpose: get last error set by database                                    *
 *                                                                            *
 * Return value: last database error message                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 ** 定义一个C语言函数：zbx_db_last_strerr，该函数用于获取上一个数据库操作的错误信息字符串。
/******************************************************************************
 *整个代码块的主要目的是根据传入的Oracle数据库连接状态（status）和错误指针（err），构造并返回相应的错误信息。代码中使用了switch分支语句，根据status的值判断并处理不同的情况。在处理每个情况时，调用OCIErrorGet函数获取错误信息，并使用zbx_snprintf函数构造错误信息字符串。最后，对错误信息进行右trim，去除多余的空格字符，并返回错误信息字符串。
 ******************************************************************************/
// 定义一个静态常量字符指针，用于存储Oracle数据库连接错误信息
const char	*zbx_db_last_strerr(void)
{
	return last_db_strerror;
}

#if defined(HAVE_ORACLE)
static const char	*zbx_oci_error(sword status, sb4 *err)
{
	static char	errbuf[512];
	sb4		errcode, *perrcode;

	perrcode = (NULL == err ? &errcode : err);

	errbuf[0] = '\0';
	*perrcode = 0;

	switch (status)
	{
		case OCI_SUCCESS_WITH_INFO:
			OCIErrorGet((dvoid *)oracle.errhp, (ub4)1, (text *)NULL, perrcode,
					(text *)errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
			break;
		case OCI_NEED_DATA:
			zbx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_NEED_DATA");
			break;
		case OCI_NO_DATA:
			zbx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_NODATA");
			break;
		case OCI_ERROR:
			OCIErrorGet((dvoid *)oracle.errhp, (ub4)1, (text *)NULL, perrcode,
					(text *)errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
			break;
		case OCI_INVALID_HANDLE:
			zbx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_INVALID_HANDLE");
			break;
		case OCI_STILL_EXECUTING:
			zbx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_STILL_EXECUTING");
			break;
		case OCI_CONTINUE:
			zbx_snprintf(errbuf, sizeof(errbuf), "%s", "OCI_CONTINUE");
			break;
	}

	zbx_rtrim(errbuf, ZBX_WHITESPACE);

	return errbuf;
}

/******************************************************************************
 *                                                                            *
 * Function: OCI_handle_sql_error                                             *
 *                                                                            *
 * Purpose: handles Oracle prepare/bind/execute/select operation error        *
 *                                                                            *
 * Parameters: zerrcode   - [IN] the Zabbix errorcode for the failed database *
 *                               operation                                    *
 *             oci_error  - [IN] the return code from failed Oracle operation *
 *             sql        - [IN] the failed sql statement (can be NULL)       *
 *                                                                            *
 * Return value: ZBX_DB_DOWN - database connection is down                    *
 *               ZBX_DB_FAIL - otherwise                                      *
 *                                                                            *
 * Comments: This function logs the error description and checks the          *
 *           database connection status.                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是处理SQL错误。函数OCI_handle_sql_error接收三个参数：错误代码zerrcode、OCI错误ocierror和SQL语句sql。首先，使用zbx_db_errlog函数记录错误日志。然后，根据错误代码errcode判断并处理不同的错误情况。如果遇到ORA-01012或ORA-02396错误，跳转到out标签处。最后，根据OCI_SERVER_NORMAL判断数据库连接状态，并将处理后的错误代码ret返回。
 ******************************************************************************/
// 定义一个函数OCI_handle_sql_error，用于处理SQL错误
static int OCI_handle_sql_error(int zerrcode, sword oci_error, const char *sql)
{
	// 定义一个sb4类型的变量errcode，用于存储数据库错误代码
	sb4 errcode;

	// 定义一个整型变量ret，初始值为ZBX_DB_DOWN
	int ret = ZBX_DB_DOWN;

	// 使用zbx_db_errlog函数记录数据库错误日志，参数包括错误代码zerrcode、oci_error、errcode和SQL语句sql
	zbx_db_errlog(zerrcode, oci_error, zbx_oci_error(oci_error, &errcode), sql);

	/* after ORA-02396 (and consequent ORA-01012) errors the OCI_SERVER_NORMAL server status is still returned */
	// 判断errcode的值，进行错误处理
	switch (errcode)
	{
		case 1012:	/* ORA-01012: not logged on */
		case 2396:	/* ORA-02396: exceeded maximum idle time */
			// 遇到这两种错误，跳转到out标签处
			goto out;
	}

	// 判断OCI_SERVER_NORMAL是否为OCI_DBserver_status()的返回值
	if (OCI_SERVER_NORMAL == OCI_DBserver_status())
		// 如果为OCI_SERVER_NORMAL，将ret设置为ZBX_DB_FAIL
		ret = ZBX_DB_FAIL;

out:
	// 返回处理后的ret值
	return ret;
}


#endif	/* HAVE_ORACLE */

#ifdef HAVE_POSTGRESQL
/******************************************************************************
 * *
 *整个代码块的主要目的是处理PGresult中的错误信息，并将错误信息拼接到一个字符数组（error）中。输出结果为一个格式化后的字符串，其中包括PGresult的状态信息和错误信息。
 ******************************************************************************/
// 定义一个静态函数zbx_postgresql_error，接收两个参数：一个是指向char类型的指针数组的指针（error），另一个是PGresult类型的指针（pg_result）。
static void zbx_postgresql_error(char **error, const PGresult *pg_result)
{
	// 定义一个字符指针变量result_error_msg，用于存储PGresult中的错误信息。
	char *result_error_msg;

	// 定义两个size_t类型的变量error_alloc和error_offset，分别用于存储错误信息的长度和偏移量。
	size_t error_alloc = 0, error_offset = 0;

	// 使用zbx_snprintf_alloc函数将PGresult的状态字符串格式化并存储到error指向的字符数组中，同时更新error_alloc和error_offset。
	zbx_snprintf_alloc(error, &error_alloc, &error_offset, "%s", PQresStatus(PQresultStatus(pg_result)));

	// 获取PGresult中的错误信息，并将其存储在result_error_msg中。
	result_error_msg = PQresultErrorMessage(pg_result);

	// 判断result_error_msg是否不为空，如果不为空，则将result_error_msg拼接到error指向的字符数组中。
	if ('\0' != *result_error_msg)
		zbx_snprintf_alloc(error, &error_alloc, &error_offset, ":%s", result_error_msg);
}

#endif /*HAVE_POSTGRESQL*/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 zbx_db_execute 的静态函数，该函数用于执行数据库操作。函数接收两个参数，第一个参数是一个指向字符串的指针，表示数据库操作的格式化字符串；第二个参数是一个可变参数列表，用于存储格式化字符串中的占位符对应的实际值。函数内部调用另一个名为 zbx_db_vexecute 的函数执行数据库操作，并返回执行结果。
 ******************************************************************************/
// 定义一个名为 zbx_db_execute 的静态函数，该函数接收两个参数，
// 第一个参数是一个指向字符串的指针，第二个参数是一个可变参数列表。
__zbx_attr_format_printf(1, 2)
static int	zbx_db_execute(const char *fmt, ...)
{
	// 定义一个名为 args 的 va_list 结构体变量，用于存储可变参数列表。
	va_list	args;
	// 定义一个名为 ret 的整型变量，用于存储函数执行结果。
	int	ret;

	va_start(args, fmt);
	ret = zbx_db_vexecute(fmt, args);
	va_end(args);

	return ret;
}

// 这是一个C语言代码块，主要目的是定义一个名为zbx_db_select的静态函数，该函数用于在数据库中执行查询操作。
// 函数接收一个const char *类型的参数fmt，这是一个格式化字符串，用于指定查询语句的格式。
// 函数使用...符号表示可变参数列表，用于接收查询语句中的占位符对应的实际值。

__zbx_attr_format_printf(1, 2)
// 使用__zbx_attr_format_printf宏标记这个函数，表示该函数使用格式化字符串输出。
// 1表示格式化字符串的数量为1，2表示格式化字符串的最大长度为2。

static DB_RESULT	zbx_db_select(const char *fmt, ...)
{
	va_list		args;
	DB_RESULT	result;

	// 初始化va_list类型的args变量，用于存储可变参数列表。
	va_start(args, fmt);

	// 调用zbx_db_vselect函数，该函数用于执行格式化查询语句。
	// 参数fmt为格式化字符串，args为可变参数列表。
	result = zbx_db_vselect(fmt, args);

	// 结束可变参数列表的解析。
	va_end(args);

	// 返回查询结果。
	return result;
}

#if defined(HAVE_MYSQL)

/******************************************************************************
 * *
 *整个代码块的主要目的是判断MySQL连接对象产生的错误是否可以恢复。通过switch语句根据不同的错误号进行判断，如果遇到可以恢复的错误，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
static int	is_recoverable_mysql_error(void) // 定义一个函数，用于判断MySQL错误是否可以恢复
{
	switch (mysql_errno(conn)) // 根据连接对象的错误号进行切换
	{
		case CR_CONN_HOST_ERROR: // 连接主机错误
		case CR_SERVER_GONE_ERROR: // 服务器关闭错误
		case CR_CONNECTION_ERROR: // 连接错误
		case CR_SERVER_LOST: // 服务器丢失
		case CR_UNKNOWN_HOST: // 未知主机
		case CR_COMMANDS_OUT_OF_SYNC: // 命令同步错误
		case ER_SERVER_SHUTDOWN: // 服务器关闭
		case ER_ACCESS_DENIED_ERROR: /* 用户名或密码错误 */
		case ER_ILLEGAL_GRANT_FOR_TABLE: /* 用户没有权限 */
		case ER_TABLEACCESS_DENIED_ERROR: /* 用户缺少某些权限 */
		case ER_UNKNOWN_ERROR: // 未知错误
		case ER_UNKNOWN_COM_ERROR: // 未知命令错误
		case ER_LOCK_DEADLOCK: // 锁死锁
		case ER_LOCK_WAIT_TIMEOUT: // 锁等待超时
#ifdef ER_CONNECTION_KILLED
		case ER_CONNECTION_KILLED: // 连接被杀死
#endif
			return SUCCEED; // 如果遇到以上错误，表示可以恢复
	}

	return FAIL; // 如果没有匹配的错误，表示无法恢复
}
#elif defined(HAVE_POSTGRESQL)
/******************************************************************************
 * *
 *这块代码的主要目的是判断PostgreSQL数据库中的错误是否可以恢复。函数`is_recoverable_postgresql_error`接受两个参数，分别是数据库连接指针`pg_conn`和SQL语句执行结果指针`pg_result`。通过检查这两个参数的状态和内容，判断数据库连接是否正常，以及SQL语句执行结果是否有错误。如果数据库连接正常且SQL语句执行结果错误码不是\"40P01\"，则认为错误可以恢复，返回成功；否则，返回失败。
 ******************************************************************************/
// 定义一个静态函数，判断PostgreSQL错误是否可以恢复
static int	is_recoverable_postgresql_error(const PGconn *pg_conn, const PGresult *pg_result)
{
    // 判断数据库连接是否正常
    if (CONNECTION_OK != PQstatus(pg_conn))
        // 如果连接不正常，返回成功（即错误可以恢复）
        return SUCCEED;

    // 判断SQL语句执行结果是否有错误
    if (0 == zbx_strcmp_null(PQresultErrorField(pg_result, PG_DIAG_SQLSTATE), "40P01"))
        // 如果SQL语句执行结果错误码不是"40P01"，则表示错误可以恢复
        return SUCCEED;

    // 如果以上条件都不满足，说明错误无法恢复
    return FAIL;
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_connect                                                   *
 *                                                                            *
 * Purpose: connect to the database                                           *
 *                                                                            *
 * Return value: ZBX_DB_OK - successfully connected                           *
 *               ZBX_DB_DOWN - database is down                               *
 *               ZBX_DB_FAIL - failed to connect                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这是一段用于连接数据库的 C 语言代码，以下是代码的详细注释：
 *
 *
 *
 *整个代码块的主要目的是连接指定的数据库，如果数据库连接失败，则返回错误信息。如果连接成功，则返回0。
 ******************************************************************************/
// 定义 zbx_db_connect 函数，该函数用于连接数据库
int	zbx_db_connect(char *host, char *user, char *password, char *dbname, char *dbschema, char *dbsocket, int port)
{
	int		ret = ZBX_DB_OK, last_txn_error, last_txn_level;
#if defined(HAVE_IBM_DB2)
	char		*connect = NULL;
#elif defined(HAVE_MYSQL)
#if LIBMYSQL_VERSION_ID >= 80000	/* my_bool type is removed in MySQL 8.0 */
	bool		mysql_reconnect = 1;
#else
	my_bool		mysql_reconnect = 1;
#endif
#elif defined(HAVE_ORACLE)
	char		*connect = NULL;
	sword		err = OCI_SUCCESS;
	static ub2	csid = 0;
#elif defined(HAVE_POSTGRESQL)
	int		rc;
	char		*cport = NULL;
	DB_RESULT	result;
	DB_ROW		row;
#elif defined(HAVE_SQLITE3)
	char		*p, *path = NULL;
#endif

#ifndef HAVE_MYSQL
	ZBX_UNUSED(dbsocket);
#endif
	/* Allow executing statements during a connection initialization. Make sure to mark transaction as failed. */
	if (0 != txn_level)
		txn_error = ZBX_DB_DOWN;

	last_txn_error = txn_error;
	last_txn_level = txn_level;

	txn_error = ZBX_DB_OK;
	txn_level = 0;

#if defined(HAVE_IBM_DB2)
	connect = zbx_strdup(connect, "PROTOCOL=TCPIP;");
	if ('\0' != *host)
		connect = zbx_strdcatf(connect, "HOSTNAME=%s;", host);
	if (NULL != dbname && '\0' != *dbname)
		connect = zbx_strdcatf(connect, "DATABASE=%s;", dbname);
	if (0 != port)
		connect = zbx_strdcatf(connect, "PORT=%d;", port);
	if (NULL != user && '\0' != *user)
		connect = zbx_strdcatf(connect, "UID=%s;", user);
	if (NULL != password && '\0' != *password)
		connect = zbx_strdcatf(connect, "PWD=%s;", password);

	memset(&ibm_db2, 0, sizeof(ibm_db2));

	/* allocate an environment handle */
	if (SUCCEED != zbx_ibm_db2_success(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &ibm_db2.henv)))
		ret = ZBX_DB_FAIL;

	/* set attribute to enable application to run as ODBC 3.0 application; */
	/* recommended for pure IBM DB2 CLI, but not required */
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success(SQLSetEnvAttr(ibm_db2.henv, SQL_ATTR_ODBC_VERSION,
			(void *)SQL_OV_ODBC3, 0)))
	{
		ret = ZBX_DB_FAIL;
	}

	/* allocate a database connection handle */
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success(SQLAllocHandle(SQL_HANDLE_DBC, ibm_db2.henv,
			&ibm_db2.hdbc)))
	{
		ret = ZBX_DB_FAIL;
	}

	/* set codepage to utf-8 */
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_CLIENT_CODEPAGE,
			(SQLPOINTER)(SQLUINTEGER)1208, SQL_IS_UINTEGER)))
	{
		ret = ZBX_DB_FAIL;
	}

	/* connect to the database */
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success(SQLDriverConnect(ibm_db2.hdbc, NULL, (SQLCHAR *)connect,
			SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)))
	{
		ret = ZBX_DB_FAIL;
	}

	/* set autocommit on */
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS)))
	{
		ret = ZBX_DB_DOWN;
	}

	/* we do not generate vendor escape clause sequences */
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_NOSCAN,
			(SQLPOINTER)SQL_NOSCAN_ON, SQL_NTS)))
	{
		ret = ZBX_DB_DOWN;
	}

	/* set current schema */
	if (NULL != dbschema && '\0' != *dbschema && ZBX_DB_OK == ret)
	{
		char	*dbschema_esc;

		dbschema_esc = zbx_db_dyn_escape_string(dbschema, ZBX_SIZE_T_MAX, ZBX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
		if (0 < (ret = zbx_db_execute("set current schema='%s'", dbschema_esc)))
			ret = ZBX_DB_OK;
		zbx_free(dbschema_esc);
	}

	zbx_free(connect);

	/* output error information */
	if (ZBX_DB_OK != ret)
	{
		zbx_ibm_db2_log_errors(SQL_HANDLE_ENV, ibm_db2.henv, ERR_Z3001, dbname);
		zbx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3001, dbname);
	}
#elif defined(HAVE_MYSQL)
	ZBX_UNUSED(dbschema);

	if (NULL == (conn = mysql_init(NULL)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot allocate or initialize MYSQL database connection object");
		exit(EXIT_FAILURE);
	}

	if (NULL == mysql_real_connect(conn, host, user, password, dbname, port, dbsocket, CLIENT_MULTI_STATEMENTS))
	{
		zbx_db_errlog(ERR_Z3001, mysql_errno(conn), mysql_error(conn), dbname);
		ret = ZBX_DB_FAIL;
	}

	/* The RECONNECT option setting is placed here, AFTER the connection	*/
	/* is made, due to a bug in MySQL versions prior to 5.1.6 where it	*/
	/* reset the options value to the default, regardless of what it was	*/
	/* set to prior to the connection. MySQL allows changing connection	*/
	/* options on an open connection, so setting it here is safe.		*/

	if (ZBX_DB_OK == ret && 0 != mysql_options(conn, MYSQL_OPT_RECONNECT, &mysql_reconnect))
		zabbix_log(LOG_LEVEL_WARNING, "Cannot set MySQL reconnect option.");

	/* in contrast to "set names utf8" results of this call will survive auto-reconnects */
	if (ZBX_DB_OK == ret && 0 != mysql_set_character_set(conn, "utf8"))
		zabbix_log(LOG_LEVEL_WARNING, "cannot set MySQL character set to \"utf8\"");

	if (ZBX_DB_OK == ret && 0 != mysql_autocommit(conn, 1))
	{
		zbx_db_errlog(ERR_Z3001, mysql_errno(conn), mysql_error(conn), dbname);
		ret = ZBX_DB_FAIL;
	}

	if (ZBX_DB_OK == ret && 0 != mysql_select_db(conn, dbname))
	{
		zbx_db_errlog(ERR_Z3001, mysql_errno(conn), mysql_error(conn), dbname);
		ret = ZBX_DB_FAIL;
	}

	if (ZBX_DB_FAIL == ret && SUCCEED == is_recoverable_mysql_error())
		ret = ZBX_DB_DOWN;

#elif defined(HAVE_ORACLE)
	ZBX_UNUSED(dbschema);

	memset(&oracle, 0, sizeof(oracle));

	zbx_vector_ptr_create(&oracle.db_results);

	/* connection string format: [//]host[:port][/service name] */

	if ('\0' != *host)
	{
		connect = zbx_strdcatf(connect, "//%s", host);
		if (0 != port)
			connect = zbx_strdcatf(connect, ":%d", port);
		if (NULL != dbname && '\0' != *dbname)
			connect = zbx_strdcatf(connect, "/%s", dbname);
	}
	else
		ret = ZBX_DB_FAIL;

	while (ZBX_DB_OK == ret)
	{
		/* initialize environment */
		if (OCI_SUCCESS == (err = OCIEnvNlsCreate((OCIEnv **)&oracle.envhp, (ub4)OCI_DEFAULT, (dvoid *)0,
				(dvoid * (*)(dvoid *,size_t))0, (dvoid * (*)(dvoid *, dvoid *, size_t))0,
				(void (*)(dvoid *, dvoid *))0, (size_t)0, (dvoid **)0, csid, csid)))
		{
			if (0 != csid)
				break;	/* environment with UTF8 character set successfully created */

			/* try to find out the id of UTF8 character set */
			if (0 == (csid = OCINlsCharSetNameToId(oracle.envhp, (const oratext *)"UTF8")))
			{
				zabbix_log(LOG_LEVEL_WARNING, "Cannot find out the ID of \"UTF8\" character set."
						" Relying on current \"NLS_LANG\" settings.");
				break;	/* use default environment with character set derived from NLS_LANG */
			}

			/* get rid of this environment to create a better one on the next iteration */
			OCIHandleFree((dvoid *)oracle.envhp, OCI_HTYPE_ENV);
			oracle.envhp = NULL;
		}
		else
		{
			zbx_db_errlog(ERR_Z3001, err, zbx_oci_error(err, NULL), connect);
			ret = ZBX_DB_FAIL;
		}
	}

	if (ZBX_DB_OK == ret)
	{
		/* allocate an error handle */
		(void)OCIHandleAlloc((dvoid *)oracle.envhp, (dvoid **)&oracle.errhp, OCI_HTYPE_ERROR,
				(size_t)0, (dvoid **)0);

		/* get the session */
		err = OCILogon2(oracle.envhp, oracle.errhp, &oracle.svchp,
				(text *)user, (ub4)(NULL != user ? strlen(user) : 0),
				(text *)password, (ub4)(NULL != password ? strlen(password) : 0),
				(text *)connect, (ub4)strlen(connect),
				OCI_DEFAULT);

		switch (err)
		{
			case OCI_SUCCESS_WITH_INFO:
				zabbix_log(LOG_LEVEL_WARNING, "%s", zbx_oci_error(err, NULL));
				/* break; is not missing here */
			case OCI_SUCCESS:
				err = OCIAttrGet((void *)oracle.svchp, OCI_HTYPE_SVCCTX, (void *)&oracle.srvhp,
						(ub4 *)0, OCI_ATTR_SERVER, oracle.errhp);
		}

		if (OCI_SUCCESS != err)
		{
			zbx_db_errlog(ERR_Z3001, err, zbx_oci_error(err, NULL), connect);
			ret = ZBX_DB_DOWN;
		}
	}

	if (ZBX_DB_OK == ret)
	{
		/* initialize statement handle */
		err = OCIHandleAlloc((dvoid *)oracle.envhp, (dvoid **)&oracle.stmthp, OCI_HTYPE_STMT,
				(size_t)0, (dvoid **)0);

		if (OCI_SUCCESS != err)
		{
			zbx_db_errlog(ERR_Z3001, err, zbx_oci_error(err, NULL), connect);
			ret = ZBX_DB_DOWN;
		}
	}

	if (ZBX_DB_OK == ret)
	{
		if (0 < (ret = zbx_db_execute("alter session set nls_numeric_characters='. '")))
			ret = ZBX_DB_OK;
	}

	zbx_free(connect);
#elif defined(HAVE_POSTGRESQL)
	if (0 != port)
		cport = zbx_dsprintf(cport, "%d", port);

	conn = PQsetdbLogin(host, cport, NULL, NULL, dbname, user, password);

	zbx_free(cport);

	/* check to see that the backend connection was successfully made */
	if (CONNECTION_OK != PQstatus(conn))
	{
		zbx_db_errlog(ERR_Z3001, 0, PQerrorMessage(conn), dbname);
		ret = ZBX_DB_DOWN;
		goto out;
	}

	if (NULL != dbschema && '\0' != *dbschema)
	{
		char	*dbschema_esc;

		dbschema_esc = zbx_db_dyn_escape_string(dbschema, ZBX_SIZE_T_MAX, ZBX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
		if (ZBX_DB_DOWN == (rc = zbx_db_execute("set schema '%s'", dbschema_esc)) || ZBX_DB_FAIL == rc)
			ret = rc;
		zbx_free(dbschema_esc);
	}

	if (ZBX_DB_FAIL == ret || ZBX_DB_DOWN == ret)
		goto out;

	result = zbx_db_select("select oid from pg_type where typname='bytea'");

	if ((DB_RESULT)ZBX_DB_DOWN == result || NULL == result)
	{
		ret = (NULL == result) ? ZBX_DB_FAIL : ZBX_DB_DOWN;
		goto out;
	}

	if (NULL != (row = zbx_db_fetch(result)))
		ZBX_PG_BYTEAOID = atoi(row[0]);
	DBfree_result(result);

	ZBX_PG_SVERSION = PQserverVersion(conn);
	zabbix_log(LOG_LEVEL_DEBUG, "PostgreSQL Server version: %d", ZBX_PG_SVERSION);

	/* disable "nonstandard use of \' in a string literal" warning */
	if (0 < (ret = zbx_db_execute("set escape_string_warning to off")))
		ret = ZBX_DB_OK;

	if (ZBX_DB_OK != ret)
		goto out;

	result = zbx_db_select("show standard_conforming_strings");

	if ((DB_RESULT)ZBX_DB_DOWN == result || NULL == result)
	{
		ret = (NULL == result) ? ZBX_DB_FAIL : ZBX_DB_DOWN;
		goto out;
	}

	if (NULL != (row = zbx_db_fetch(result)))
		ZBX_PG_ESCAPE_BACKSLASH = (0 == strcmp(row[0], "off"));
	DBfree_result(result);

	if (90000 <= ZBX_PG_SVERSION)
	{
		/* change the output format for values of type bytea from hex (the default) to escape */
		if (0 < (ret = zbx_db_execute("set bytea_output=escape")))
			ret = ZBX_DB_OK;
	}
out:
#elif defined(HAVE_SQLITE3)
	ZBX_UNUSED(host);
	ZBX_UNUSED(user);
	ZBX_UNUSED(password);
	ZBX_UNUSED(dbschema);
	ZBX_UNUSED(port);
#ifdef HAVE_FUNCTION_SQLITE3_OPEN_V2
	if (SQLITE_OK != sqlite3_open_v2(dbname, &conn, SQLITE_OPEN_READWRITE, NULL))
#else
	if (SQLITE_OK != sqlite3_open(dbname, &conn))
#endif
	{
		zbx_db_errlog(ERR_Z3001, 0, sqlite3_errmsg(conn), dbname);
		ret = ZBX_DB_DOWN;
		goto out;
	}

	/* do not return SQLITE_BUSY immediately, wait for N ms */
	sqlite3_busy_timeout(conn, SEC_PER_MIN * 1000);

	if (0 < (ret = zbx_db_execute("pragma synchronous=0")))
		ret = ZBX_DB_OK;

	if (ZBX_DB_OK != ret)
		goto out;

	if (0 < (ret = zbx_db_execute("pragma temp_store=2")))
		ret = ZBX_DB_OK;

	if (ZBX_DB_OK != ret)
		goto out;

	path = zbx_strdup(NULL, dbname);

	if (NULL != (p = strrchr(path, '/')))
		*++p = '\0';
	else
		*path = '\0';

	if (0 < (ret = zbx_db_execute("pragma temp_store_directory='%s'", path)))
		ret = ZBX_DB_OK;

	zbx_free(path);
out:
#endif	/* HAVE_SQLITE3 */
	if (ZBX_DB_OK != ret)
		zbx_db_close();

	txn_error = last_txn_error;
	txn_level = last_txn_level;

	return ret;
}


int	zbx_db_init(const char *dbname, const char *const dbschema, char **error)
{
#ifdef HAVE_SQLITE3
	zbx_stat_t	buf;

	if (0 != zbx_stat(dbname, &buf))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot open database file \"%s\": %s", dbname, zbx_strerror(errno));
		zabbix_log(LOG_LEVEL_WARNING, "creating database ...");

		if (SQLITE_OK != sqlite3_open(dbname, &conn))
		{
			zbx_db_errlog(ERR_Z3002, 0, sqlite3_errmsg(conn), dbname);
			*error = zbx_strdup(*error, "cannot open database");
			return FAIL;
		}

		if (SUCCEED != zbx_mutex_create(&sqlite_access, ZBX_MUTEX_SQLITE3, error))
			return FAIL;

		zbx_db_execute("%s", dbschema);
		zbx_db_close();
		return SUCCEED;
	}

	return zbx_mutex_create(&sqlite_access, ZBX_MUTEX_SQLITE3, error);
#else	/* not HAVE_SQLITE3 */
	ZBX_UNUSED(dbname);
	ZBX_UNUSED(dbschema);
	ZBX_UNUSED(error);

	return SUCCEED;
#endif	/* HAVE_SQLITE3 */
}
/******************************************************************************
 * c
 *void\tzbx_db_deinit(void)
 *{
 *    // 使用 #ifdef 预处理指令，检查 HAVE_SQLITE3 符号是否定义
 *    #ifdef HAVE_SQLITE3
 *        // 定义一个名为 sqlite_access 的互斥锁变量
 *        zbx_mutex_destroy(&sqlite_access);
 *    #endif
 *}
 *```
 ******************************************************************************/
// 定义一个名为 zbx_db_deinit 的函数，该函数为 void 类型（无返回值）
void zbx_db_deinit(void)
{
    // 使用 #ifdef 预处理指令，检查 HAVE_SQLITE3 符号是否定义
    #ifdef HAVE_SQLITE3
        // 定义一个名为 sqlite_access 的互斥锁变量
        zbx_mutex_destroy(&sqlite_access);
    #endif
}

void	zbx_db_close(void)
{
#if defined(HAVE_IBM_DB2)
	if (ibm_db2.hdbc)
	{
		SQLDisconnect(ibm_db2.hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, ibm_db2.hdbc);
	}

	if (ibm_db2.henv)
		SQLFreeHandle(SQL_HANDLE_ENV, ibm_db2.henv);

	memset(&ibm_db2, 0, sizeof(ibm_db2));
#elif defined(HAVE_MYSQL)
	if (NULL != conn)
	{
		mysql_close(conn);
		conn = NULL;
	}
#elif defined(HAVE_ORACLE)
	if (0 != oracle.db_results.values_num)
	{
		int	i;

		zabbix_log(LOG_LEVEL_WARNING, "cannot process queries: database is closed");

		for (i = 0; i < oracle.db_results.values_num; i++)
		{
			/* deallocate all handles before environment is deallocated */
			OCI_DBclean_result(oracle.db_results.values[i]);
		}
	}

	/* deallocate statement handle */
	if (NULL != oracle.stmthp)
	{
		OCIHandleFree((dvoid *)oracle.stmthp, OCI_HTYPE_STMT);
		oracle.stmthp = NULL;
	}

	if (NULL != oracle.svchp)
	{
		OCILogoff(oracle.svchp, oracle.errhp);
		oracle.svchp = NULL;
	}

	if (NULL != oracle.errhp)
	{
		OCIHandleFree(oracle.errhp, OCI_HTYPE_ERROR);
		oracle.errhp = NULL;
	}

	if (NULL != oracle.srvhp)
	{
		OCIHandleFree(oracle.srvhp, OCI_HTYPE_SERVER);
		oracle.srvhp = NULL;
	}

	if (NULL != oracle.envhp)
	{
		/* delete the environment handle, which deallocates all other handles associated with it */
		OCIHandleFree((dvoid *)oracle.envhp, OCI_HTYPE_ENV);
		oracle.envhp = NULL;
	}

	zbx_vector_ptr_destroy(&oracle.db_results);
#elif defined(HAVE_POSTGRESQL)
	if (NULL != conn)
	{
		PQfinish(conn);
		conn = NULL;
	}
#elif defined(HAVE_SQLITE3)
	if (NULL != conn)
	{
		sqlite3_close(conn);
		conn = NULL;
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_begin                                                     *
 *                                                                            *
 * Purpose: start transaction                                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于开启一个数据库事务。根据不同的数据库类型，执行相应的开启事务语句。在执行过程中，注意事务级别的控制，避免出现嵌套事务。同时，对错误情况进行处理，记录日志并返回相应的错误码。
 ******************************************************************************/
int	zbx_db_begin(void)
{
	// 定义一个整型变量 rc 并初始化为 ZBX_DB_OK
	int	rc = ZBX_DB_OK;

	// 判断当前事务级别是否大于0，如果是，则打印错误日志并设置 assert 断言
	if (txn_level > 0)
	{
		zabbix_log(LOG_LEVEL_CRIT, "ERROR: nested transaction detected. Please report it to Zabbix Team.");
		assert(0);
	}

	// 事务级别加1
	txn_level++;

	// 根据系统配置判断是否使用 IBM DB2 数据库
#if defined(HAVE_IBM_DB2)
	if (SUCCEED != zbx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_OFF, SQL_NTS)))
	{
		// 如果 IBM DB2 数据库配置失败，将 rc 设置为 ZBX_DB_DOWN
		rc = ZBX_DB_DOWN;
	}

	// 如果 rc 为 ZBX_DB_OK，则执行以下操作：
	if (ZBX_DB_OK == rc)
	{
		// 在 DB2 数据库中创建一个保存点
		if (0 <= (rc = zbx_db_execute("savepoint zbx_begin_savepoint unique on rollback retain cursors;")))
			rc = ZBX_DB_OK;
	}

	// 如果 rc 不是 ZBX_DB_OK，则记录错误日志并设置 rc
	if (ZBX_DB_OK != rc)
	{
		zbx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, "<begin>");
		rc = (SQL_CD_TRUE == IBM_DB2server_status() ? ZBX_DB_FAIL : ZBX_DB_DOWN);
	}
#elif defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	rc = zbx_db_execute("begin;");
#elif defined(HAVE_SQLITE3)
	zbx_mutex_lock(sqlite_access);
	rc = zbx_db_execute("begin;");
#endif

	// 如果 rc 为 ZBX_DB_DOWN，则将事务级别减1
	if (ZBX_DB_DOWN == rc)
		txn_level--;

	// 返回 rc，表示数据库操作结果
	return rc;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_commit                                                    *
 *                                                                            *
 * Purpose: commit transaction                                                *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
int	zbx_db_commit(void)
{
	int	rc = ZBX_DB_OK;
#ifdef HAVE_ORACLE
	sword	err;
#endif

	if (0 == txn_level)
	{
		zabbix_log(LOG_LEVEL_CRIT, "ERROR: commit without transaction."
				" Please report it to Zabbix Team.");
		assert(0);
	}

	if (ZBX_DB_OK != txn_error)
		return ZBX_DB_FAIL; /* commit called on failed transaction */

#if defined(HAVE_IBM_DB2)
	if (SUCCEED != zbx_ibm_db2_success(SQLEndTran(SQL_HANDLE_DBC, ibm_db2.hdbc, SQL_COMMIT)))
		rc = ZBX_DB_DOWN;
	if (SUCCEED != zbx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS)))
	{
		rc = ZBX_DB_DOWN;
	}
	if (ZBX_DB_OK != rc)
	{
		zbx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, "<commit>");
		rc = (SQL_CD_TRUE == IBM_DB2server_status() ? ZBX_DB_FAIL : ZBX_DB_DOWN);
	}
#elif defined(HAVE_ORACLE)
	if (OCI_SUCCESS != (err = OCITransCommit(oracle.svchp, oracle.errhp, OCI_DEFAULT)))
		rc = OCI_handle_sql_error(ERR_Z3005, err, "commit failed");
#elif defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL) || defined(HAVE_SQLITE3)
	rc = zbx_db_execute("commit;");
#endif

	if (ZBX_DB_OK > rc) { /* commit failed */
		txn_error = rc;
		return rc;
	}

#ifdef HAVE_SQLITE3
	zbx_mutex_unlock(sqlite_access);
#endif

	txn_level--;
	txn_end_error = ZBX_DB_OK;

	return rc;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个通用的数据库回滚功能。根据不同的数据库类型（ORACLE、IBM_DB2、MYSQL、POSTGRESQL、SQLITE3），执行相应的回滚操作。在回滚完成后，重置事务级别和错误。如果回滚失败，记录错误并返回失败状态。
 ******************************************************************************/
int	zbx_db_rollback(void)
{
	int	rc = ZBX_DB_OK, last_txn_error;
#ifdef HAVE_ORACLE
	sword	err;
#endif

	// 判断事务级别是否为0，如果不是，则日志记录错误并退出
	if (0 == txn_level)
	{
		zabbix_log(LOG_LEVEL_CRIT, "ERROR: rollback without transaction."
				" Please report it to Zabbix Team.");
		assert(0);
	}

	// 保存当前事务错误
	last_txn_error = txn_error;

	// 允许回滚失败的事务
	txn_error = ZBX_DB_OK;

	// 根据数据库类型进行相应的回滚操作
#if defined(HAVE_IBM_DB2)
	// 回滚到标记为savepoint的开始，撤销所有事务
	if (0 <= (rc = zbx_db_execute("rollback to savepoint zbx_begin_savepoint;")))
		rc = ZBX_DB_OK;

	if (SUCCEED != zbx_ibm_db2_success(SQLSetConnectAttr(ibm_db2.hdbc, SQL_ATTR_AUTOCOMMIT,
			(SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS)))
	{
		rc = ZBX_DB_DOWN;
	}

	if (ZBX_DB_OK != rc)
	{
		zbx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, "<rollback>");
		rc = (SQL_CD_TRUE == IBM_DB2server_status() ? ZBX_DB_FAIL : ZBX_DB_DOWN);
	}
#elif defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	rc = zbx_db_execute("rollback;");
#elif defined(HAVE_ORACLE)
	if (OCI_SUCCESS != (err = OCITransRollback(oracle.svchp, oracle.errhp, OCI_DEFAULT)))
		rc = OCI_handle_sql_error(ERR_Z3005, err, "rollback failed");
#elif defined(HAVE_SQLITE3)
	rc = zbx_db_execute("rollback;");
	zbx_mutex_unlock(sqlite_access);
#endif

	/* There is no way to recover from rollback errors, so there is no need to preserve transaction level / error. */
	txn_level = 0;
	txn_error = ZBX_DB_OK;

	if (ZBX_DB_FAIL == rc)
		txn_end_error = ZBX_DB_FAIL;
	else
		txn_end_error = last_txn_error;	/* error that caused rollback */

	return rc;
}

int	zbx_db_txn_level(void)
{
	return txn_level;
}

int	zbx_db_txn_error(void)
{
	return txn_error;
}

int	zbx_db_txn_end_error(void)
{
	return txn_end_error;
}

#ifdef HAVE_ORACLE
static sword	zbx_oracle_statement_prepare(const char *sql)
{
	return OCIStmtPrepare(oracle.stmthp, oracle.errhp, (text *)sql, (ub4)strlen((char *)sql), (ub4)OCI_NTV_SYNTAX,
			(ub4)OCI_DEFAULT);
}

static sword	zbx_oracle_statement_execute(ub4 iters, ub4 *nrows)
{
	sword	err;

	if (OCI_SUCCESS == (err = OCIStmtExecute(oracle.svchp, oracle.stmthp, oracle.errhp, iters, (ub4)0,
			(CONST OCISnapshot *)NULL, (OCISnapshot *)NULL,
			0 == txn_level ? OCI_COMMIT_ON_SUCCESS : OCI_DEFAULT)))
	{
		err = OCIAttrGet((void *)oracle.stmthp, OCI_HTYPE_STMT, nrows, (ub4 *)0, OCI_ATTR_ROW_COUNT,
				oracle.errhp);
	}

	return err;
}
#endif

#ifdef HAVE_ORACLE
int	zbx_db_statement_prepare(const char *sql)
{
	sword	err;
	// 定义一个整型变量ret，用于保存函数执行结果
	int ret = ZBX_DB_OK;

	// 判断当前事务级别（txn_level）是否为0，如果不是，则打印一条日志
	if (0 == txn_level)
		zabbix_log(LOG_LEVEL_DEBUG, "query without transaction detected");

	// 判断上一个数据库操作是否出错，如果出错，则打印一条日志，并返回ZBX_DB_FAIL
	if (ZBX_DB_OK != txn_error)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] within failed transaction", txn_level);
		return ZBX_DB_FAIL;
	}

	// 打印日志，记录当前事务级别和要执行的SQL语句
	zabbix_log(LOG_LEVEL_DEBUG, "query [txnlev:%d] [%s]", txn_level, sql);

	// 调用zbx_oracle_statement_prepare函数准备SQL语句，若调用失败，则通过OCI_handle_sql_error函数处理错误
	if (OCI_SUCCESS != (err = zbx_oracle_statement_prepare(sql)))
		ret = OCI_handle_sql_error(ERR_Z3005, err, sql);

	// 判断函数调用结果，如果失败且事务级别大于0，则打印一条日志，并将事务级别设置为失败
	if (ZBX_DB_FAIL == ret && 0 < txn_level)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "query [%s] failed, setting transaction as failed", sql);
		txn_error = ZBX_DB_FAIL;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: db_bind_dynamic_cb                                               *
 *                                                                            *
 * Purpose: callback function used by dynamic parameter binding               *
 *                                                                            *
 ******************************************************************************/
static sb4 db_bind_dynamic_cb(dvoid *ctxp, OCIBind *bindp, ub4 iter, ub4 index, dvoid **bufpp, ub4 *alenp, ub1 *piecep,
		dvoid **indpp)
{
	zbx_db_bind_context_t	*context = (zbx_db_bind_context_t *)ctxp;

	ZBX_UNUSED(bindp);
	ZBX_UNUSED(index);

	switch (context->type)
	{
		case ZBX_TYPE_ID: /* handle 0 -> NULL conversion */
			if (0 == context->rows[iter][context->position].ui64)
			{
				*bufpp = NULL;
				*alenp = 0;
				break;
			}
			/* break; is not missing here */
		case ZBX_TYPE_UINT:
			*bufpp = &((OCINumber *)context->data)[iter];
			*alenp = sizeof(OCINumber);
			break;
		case ZBX_TYPE_INT:
			*bufpp = &context->rows[iter][context->position].i32;
			*alenp = sizeof(int);
			break;
		case ZBX_TYPE_FLOAT:
			*bufpp = &context->rows[iter][context->position].dbl;
			*alenp = sizeof(double);
			break;
		case ZBX_TYPE_CHAR:
		case ZBX_TYPE_TEXT:
		case ZBX_TYPE_SHORTTEXT:
		case ZBX_TYPE_LONGTEXT:
			*bufpp = context->rows[iter][context->position].str;
			*alenp = ((size_t *)context->data)[iter];
			break;
		default:
			return FAIL;
	}

	*indpp = NULL;
	*piecep = OCI_ONE_PIECE;

	return OCI_CONTINUE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_bind_parameter_dyn                                        *
 *                                                                            *
 * Purpose: performs dynamic parameter binding, converting value if necessary *
 *                                                                            *
 * Parameters: context  - [OUT] the bind context                              *
 *             position - [IN] the parameter position                         *
 *             type     - [IN] the parameter type (ZBX_TYPE_* )               *
 *             rows     - [IN] the data to bind - array of rows,              *
 *                             each row being an array of columns             *
 *             rows_num - [IN] the number of rows in the data                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是用于动态绑定数据库查询语句中的参数。它根据传入的参数类型和数据，为数据库查询语句分配内存并设置相关参数。具体来说，这段代码实现了以下功能：
 *
 *1. 设置`context`的相关信息，如位置、数据类型等。
 *2. 根据传入的类型，对数据进行转换，如将整数转换为`OCINumber`类型。
 *3. 分配内存并获取字符串长度。
 *4. 为`bind_context`分配内存并设置相关参数。
 *5. 绑定动态参数。
 *
 *在整个过程中，如果遇到错误，会进行相应的错误处理，如调用`OCI_handle_sql_error`函数处理SQL错误。最后，如果绑定成功，返回`ZBX_DB_OK`，否则清理`bind_context`并返回相应的错误码。
 ******************************************************************************/
int zbx_db_bind_parameter_dyn(zbx_db_bind_context_t *context, int position, unsigned char type,
                               zbx_db_value_t **rows, int rows_num)
{
	int		i, ret = ZBX_DB_OK;
	size_t		*sizes;
	sword		err;
	OCINumber	*values;
	ub2		data_type;
	OCIBind		*bindhp = NULL;

	// 设置context的相关信息
	context->position = position;
	context->rows = rows;
	context->data = NULL;
	context->type = type;

	// 根据type进行相应的处理
	switch (type)
	{
		case ZBX_TYPE_ID:
		case ZBX_TYPE_UINT:
			values = (OCINumber *)zbx_malloc(NULL, sizeof(OCINumber) * rows_num);

			for (i = 0; i < rows_num; i++)
			{
				err = OCINumberFromInt(oracle.errhp, &rows[i][position].ui64, sizeof(zbx_uint64_t),
						OCI_NUMBER_UNSIGNED, &values[i]);

				if (OCI_SUCCESS != err)
				{
					ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);
					goto out;
				}
			}

			context->data = (OCINumber *)values;
			context->size_max = sizeof(OCINumber);
			data_type = SQLT_VNU;
			break;
		case ZBX_TYPE_INT:
			context->size_max = sizeof(int);
			data_type = SQLT_INT;
			break;
		case ZBX_TYPE_FLOAT:
			context->size_max = sizeof(double);
			data_type = SQLT_FLT;
			break;
		case ZBX_TYPE_CHAR:
		case ZBX_TYPE_TEXT:
		case ZBX_TYPE_SHORTTEXT:
		case ZBX_TYPE_LONGTEXT:
			sizes = (size_t *)zbx_malloc(NULL, sizeof(size_t) * rows_num);
			context->size_max = 0;

			for (i = 0; i < rows_num; i++)
			{
				sizes[i] = strlen(rows[i][position].str);

				if (sizes[i] > context->size_max)
					context->size_max = sizes[i];
			}

			context->data = sizes;
			data_type = SQLT_LNG;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	err = OCIBindByPos(oracle.stmthp, &bindhp, oracle.errhp, context->position + 1, NULL, context->size_max,
			data_type, NULL, NULL, NULL, 0, NULL, (ub4)OCI_DATA_AT_EXEC);

	if (OCI_SUCCESS != err)
	{
		ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);

		if (ZBX_DB_FAIL == ret && 0 < txn_level)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "query failed, setting transaction as failed");
			txn_error = ZBX_DB_FAIL;
		}

		goto out;
	}

	err = OCIBindDynamic(bindhp, oracle.errhp, (dvoid *)context, db_bind_dynamic_cb, NULL, NULL);

	if (OCI_SUCCESS != err)
	{
		ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);

		if (ZBX_DB_FAIL == ret && 0 < txn_level)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "query failed, setting transaction as failed");
			txn_error = ZBX_DB_FAIL;
		}

		goto out;
	}
out:
	if (ret != ZBX_DB_OK)
		zbx_db_clean_bind_context(context);

	return ret;
}

void	zbx_db_clean_bind_context(zbx_db_bind_context_t *context)
{
	zbx_free(context->data);
}

int	zbx_db_statement_execute(int iters)
{
	const char	*__function_name = "zbx_db_statement_execute";

	sword	err;
	ub4	nrows;
	int	ret;

	if (ZBX_DB_OK != txn_error)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] within failed transaction", txn_level);
		ret = ZBX_DB_FAIL;
		goto out;
	}

	if (OCI_SUCCESS != (err = zbx_oracle_statement_execute(iters, &nrows)))
		ret = OCI_handle_sql_error(ERR_Z3007, err, NULL);
	else
		ret = (int)nrows;

	if (ZBX_DB_FAIL == ret && 0 < txn_level)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "query failed, setting transaction as failed");
		txn_error = ZBX_DB_FAIL;
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "%s():%d", __function_name, ret);

	return ret;
}
#endif
/******************************************************************************
 * 
 ******************************************************************************/
int zbx_db_vexecute(const char *fmt, va_list args)
{
	// 定义一个字符串指针变量sql，用于存储SQL语句
	char *sql = NULL;
	// 定义一个整型变量ret，用于存储执行SQL语句后的返回值
	int ret = ZBX_DB_OK;
	// 定义一个双精度浮点型变量sec，用于存储执行SQL语句所用的时间
	double sec = 0;

	// 根据不同的数据库类型，分别定义相应的变量和操作
#if defined(HAVE_IBM_DB2)
	SQLHANDLE hstmt = 0;
	SQLRETURN ret1;
	SQLLEN row1;
	SQLLEN rows = 0;
#elif defined(HAVE_MYSQL)
	int status;
#elif defined(HAVE_ORACLE)
	sword err = OCI_SUCCESS;
#elif defined(HAVE_POSTGRESQL)
	PGresult *result;
	char *error = NULL;
#elif defined(HAVE_SQLITE3)
	int err;
	char *error = NULL;
#endif

	// 判断是否开启慢查询日志记录
	if (0 != CONFIG_LOG_SLOW_QUERIES)
		sec = zbx_time();

	// 根据传入的格式化字符串和参数，生成SQL语句
	sql = zbx_dvsprintf(sql, fmt, args);

	// 判断当前事务级别，如果为0，则不在事务中
	if (0 == txn_level)
		zabbix_log(LOG_LEVEL_DEBUG, "query without transaction detected");

	// 判断上一次事务是否有错误，如果有错误，则忽略当前查询
	if (ZBX_DB_OK != txn_error)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] [%s] within failed transaction", txn_level, sql);
		ret = ZBX_DB_FAIL;
		goto clean;
	}

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "query [txnlev:%d] [%s]", txn_level, sql);

	// 根据不同的数据库类型，执行SQL语句并处理结果
#if defined(HAVE_IBM_DB2)
	// 为IBM DB2分配语句句柄
	if (SUCCEED != zbx_ibm_db2_success(SQLAllocHandle(SQL_HANDLE_STMT, ibm_db2.hdbc, &hstmt)))
		ret = ZBX_DB_DOWN;

	// 直接执行SQL语句，返回SQL语句受影响的行数
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success_ext(SQLExecDirect(hstmt, (SQLCHAR *)sql, SQL_NTS)))
		ret = ZBX_DB_DOWN;

	// 获取受影响的行数
	if (ZBX_DB_OK == ret && SUCCEED != zbx_ibm_db2_success(SQLRowCount(hstmt, &rows)))
		ret = ZBX_DB_DOWN;

	// 处理其他SQL语句
	while (ZBX_DB_OK == ret && SUCCEED == zbx_ibm_db2_success(ret1 = SQLMoreResults(hstmt)))
	{
		if (SUCCEED != zbx_ibm_db2_success(SQLRowCount(hstmt, &row1)))
			ret = ZBX_DB_DOWN;
		else
			rows += row1;
	}

	// 如果没有更多结果，则结束循环
	if (ZBX_DB_OK == ret && SQL_NO_DATA_FOUND != ret1)
		ret = ZBX_DB_DOWN;

	// 处理错误
	if (ZBX_DB_OK != ret)
	{
		zbx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, sql);
		zbx_ibm_db2_log_errors(SQL_HANDLE_STMT, hstmt, ERR_Z3005, sql);

		ret = (SQL_CD_TRUE == IBM_DB2server_status() ? ZBX_DB_FAIL : ZBX_DB_DOWN);
	}
	else if (0 <= rows)
	{
		ret = (int)rows;
	}

	// 释放句柄
	if (hstmt)
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
#elif defined(HAVE_MYSQL)
	// 如果不存在连接，则报错
	if (NULL == conn)
	{
		zbx_db_errlog(ERR_Z3003, 0, NULL, NULL);
		ret = ZBX_DB_FAIL;
	}
	else
	{
		if (0 != (status = mysql_query(conn, sql)))
		{
			zbx_db_errlog(ERR_Z3005, mysql_errno(conn), mysql_error(conn), sql);

			ret = (SUCCEED == is_recoverable_mysql_error() ? ZBX_DB_DOWN : ZBX_DB_FAIL);
		}
		else
		{
			do
			{
				if (0 != mysql_field_count(conn))
				{
					zabbix_log(LOG_LEVEL_DEBUG, "cannot retrieve result set");
					break;
				}

				ret += (int)mysql_affected_rows(conn);

				/* more results? 0 = yes (keep looping), -1 = no, >0 = error */
				if (0 < (status = mysql_next_result(conn)))
				{
					zbx_db_errlog(ERR_Z3005, mysql_errno(conn), mysql_error(conn), sql);
					ret = (SUCCEED == is_recoverable_mysql_error() ? ZBX_DB_DOWN : ZBX_DB_FAIL);
				}
			}
			while (0 == status);
		}
	}
#elif defined(HAVE_ORACLE)
	if (OCI_SUCCESS == (err = zbx_oracle_statement_prepare(sql)))
	{
		ub4	nrows = 0;

		if (OCI_SUCCESS == (err = zbx_oracle_statement_execute(1, &nrows)))
			ret = (int)nrows;
	}

	if (OCI_SUCCESS != err)
		ret = OCI_handle_sql_error(ERR_Z3005, err, sql);

#elif defined(HAVE_POSTGRESQL)
	result = PQexec(conn,sql);

	if (NULL == result)
	{
		zbx_db_errlog(ERR_Z3005, 0, "result is NULL", sql);
		ret = (CONNECTION_OK == PQstatus(conn) ? ZBX_DB_FAIL : ZBX_DB_DOWN);
	}
	else if (PGRES_COMMAND_OK != PQresultStatus(result))
	{
		zbx_postgresql_error(&error, result);
		zbx_db_errlog(ERR_Z3005, 0, error, sql);
		zbx_free(error);

		ret = (SUCCEED == is_recoverable_postgresql_error(conn, result) ? ZBX_DB_DOWN : ZBX_DB_FAIL);
	}

	if (ZBX_DB_OK == ret)
		ret = atoi(PQcmdTuples(result));

	PQclear(result);
#elif defined(HAVE_SQLITE3)
	if (0 == txn_level)
		zbx_mutex_lock(sqlite_access);

lbl_exec:
	if (SQLITE_OK != (err = sqlite3_exec(conn, sql, NULL, 0, &error)))
	{
		if (SQLITE_BUSY == err)
			goto lbl_exec;

		zbx_db_errlog(ERR_Z3005, 0, error, sql);
		sqlite3_free(error);

		switch (err)
		{
			case SQLITE_ERROR:	/* SQL error or missing database; assuming SQL error, because if we
						   are this far into execution, zbx_db_connect() was successful */
			case SQLITE_NOMEM:	/* A malloc() failed */
			case SQLITE_TOOBIG:	/* String or BLOB exceeds size limit */
			case SQLITE_CONSTRAINT:	/* Abort due to constraint violation */
			case SQLITE_MISMATCH:	/* Data type mismatch */
				ret = ZBX_DB_FAIL;
				break;
			default:
				ret = ZBX_DB_DOWN;
				break;
		}
	}

	if (ZBX_DB_OK == ret)
		ret = sqlite3_changes(conn);

	if (0 == txn_level)
		zbx_mutex_unlock(sqlite_access);
#endif	/* HAVE_SQLITE3 */

	if (0 != CONFIG_LOG_SLOW_QUERIES)
	{
		sec = zbx_time() - sec;
		if (sec > (double)CONFIG_LOG_SLOW_QUERIES / 1000.0)
			zabbix_log(LOG_LEVEL_WARNING, "slow query: " ZBX_FS_DBL " sec, \"%s\"", sec, sql);
	}

	if (ZBX_DB_FAIL == ret && 0 < txn_level)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "query [%s] failed, setting transaction as failed", sql);
		txn_error = ZBX_DB_FAIL;
	}
clean:
	zbx_free(sql);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_vselect                                                   *
 *                                                                            *
 * Purpose: execute a select statement                                        *
 *                                                                            *
 * Return value: data, NULL (on error) or (DB_RESULT)ZBX_DB_DOWN              *
 *                                                                            *
 ******************************************************************************/
DB_RESULT	zbx_db_vselect(const char *fmt, va_list args)
{
	char		*sql = NULL;
	DB_RESULT	result = NULL;
	double		sec = 0;
#if defined(HAVE_IBM_DB2)
	int		i;
	SQLRETURN	ret = SQL_SUCCESS;
#elif defined(HAVE_ORACLE)
	sword		err = OCI_SUCCESS;
	ub4		prefetch_rows = 200, counter;
#elif defined(HAVE_POSTGRESQL)
	char		*error = NULL;
#elif defined(HAVE_SQLITE3)
	int		ret = FAIL;
	char		*error = NULL;
#endif

	if (0 != CONFIG_LOG_SLOW_QUERIES)
		sec = zbx_time();

	sql = zbx_dvsprintf(sql, fmt, args);

	if (ZBX_DB_OK != txn_error)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "ignoring query [txnlev:%d] [%s] within failed transaction", txn_level, sql);
		goto clean;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "query [txnlev:%d] [%s]", txn_level, sql);

#if defined(HAVE_IBM_DB2)
	result = zbx_malloc(result, sizeof(struct zbx_db_result));
	memset(result, 0, sizeof(struct zbx_db_result));

	/* allocate a statement handle */
	if (SUCCEED != zbx_ibm_db2_success(ret = SQLAllocHandle(SQL_HANDLE_STMT, ibm_db2.hdbc, &result->hstmt)))
		goto error;

	/* directly execute the statement */
	if (SUCCEED != zbx_ibm_db2_success(ret = SQLExecDirect(result->hstmt, (SQLCHAR *)sql, SQL_NTS)))
		goto error;

	/* identify the number of output columns */
	if (SUCCEED != zbx_ibm_db2_success(ret = SQLNumResultCols(result->hstmt, &result->ncolumn)))
		goto error;

	if (0 == result->ncolumn)
		goto error;

	result->nalloc = 0;
	result->values = zbx_malloc(result->values, sizeof(char *) * result->ncolumn);
	result->values_cli = zbx_malloc(result->values_cli, sizeof(char *) * result->ncolumn);
	result->values_len = zbx_malloc(result->values_len, sizeof(SQLINTEGER) * result->ncolumn);

	for (i = 0; i < result->ncolumn; i++)
	{
		/* get the display size for a column */
		if (SUCCEED != zbx_ibm_db2_success(ret = SQLColAttribute(result->hstmt, (SQLSMALLINT)(i + 1),
				SQL_DESC_DISPLAY_SIZE, NULL, 0, NULL, &result->values_len[i])))
		{
			goto error;
		}

		result->values_len[i] += 1; /* '\0'; */

		/* allocate memory to bind a column */
		result->values_cli[i] = zbx_malloc(NULL, result->values_len[i]);
		result->nalloc++;

		/* bind columns to program variables, converting all types to CHAR */
		if (SUCCEED != zbx_ibm_db2_success(ret = SQLBindCol(result->hstmt, (SQLSMALLINT)(i + 1),
				SQL_C_CHAR, result->values_cli[i], result->values_len[i], &result->values_len[i])))
		{
			goto error;
		}
	}
error:
	if (SUCCEED != zbx_ibm_db2_success(ret) || 0 == result->ncolumn)
	{
		zbx_ibm_db2_log_errors(SQL_HANDLE_DBC, ibm_db2.hdbc, ERR_Z3005, sql);
		zbx_ibm_db2_log_errors(SQL_HANDLE_STMT, result->hstmt, ERR_Z3005, sql);

		DBfree_result(result);

		result = (SQL_CD_TRUE == IBM_DB2server_status() ? NULL : (DB_RESULT)ZBX_DB_DOWN);
	}
#elif defined(HAVE_MYSQL)
	result = (DB_RESULT)zbx_malloc(NULL, sizeof(struct zbx_db_result));
	result->result = NULL;

	if (NULL == conn)
	{
		zbx_db_errlog(ERR_Z3003, 0, NULL, NULL);

		DBfree_result(result);
		result = NULL;
	}
	else
	{
		if (0 != mysql_query(conn, sql) || NULL == (result->result = mysql_store_result(conn)))
		{
			zbx_db_errlog(ERR_Z3005, mysql_errno(conn), mysql_error(conn), sql);

			DBfree_result(result);
			result = (SUCCEED == is_recoverable_mysql_error() ? (DB_RESULT)ZBX_DB_DOWN : NULL);
		}
	}
#elif defined(HAVE_ORACLE)
	result = zbx_malloc(NULL, sizeof(struct zbx_db_result));
	memset(result, 0, sizeof(struct zbx_db_result));
	zbx_vector_ptr_append(&oracle.db_results, result);

	err = OCIHandleAlloc((dvoid *)oracle.envhp, (dvoid **)&result->stmthp, OCI_HTYPE_STMT, (size_t)0, (dvoid **)0);

	/* Prefetching when working with Oracle is needed because otherwise it fetches only 1 row at a time when doing */
	/* selects (default behavior). There are 2 ways to do prefetching: memory based and rows based. Based on the   */
	/* study optimal (speed-wise) memory based prefetch is 2 MB. But in case of many subsequent selects CPU usage  */
	/* jumps up to 100 %. Using rows prefetch with up to 200 rows does not affect CPU usage, it is the same as     */
	/* without prefetching at all. See ZBX-5920, ZBX-6493 for details.                                             */
	/*                                                                                                             */
	/* Tested on Oracle 11gR2.                                                                                     */
	/*                                                                                                             */
	/* Oracle info: docs.oracle.com/cd/B28359_01/appdev.111/b28395/oci04sql.htm                                    */

	if (OCI_SUCCESS == err)
	{
		err = OCIAttrSet(result->stmthp, OCI_HTYPE_STMT, &prefetch_rows, sizeof(prefetch_rows),
				OCI_ATTR_PREFETCH_ROWS, oracle.errhp);
	}

	if (OCI_SUCCESS == err)
	{
		err = OCIStmtPrepare(result->stmthp, oracle.errhp, (text *)sql, (ub4)strlen((char *)sql),
				(ub4)OCI_NTV_SYNTAX, (ub4)OCI_DEFAULT);
	}

	if (OCI_SUCCESS == err)
	{
		err = OCIStmtExecute(oracle.svchp, result->stmthp, oracle.errhp, (ub4)0, (ub4)0,
				(CONST OCISnapshot *)NULL, (OCISnapshot *)NULL,
				0 == txn_level ? OCI_COMMIT_ON_SUCCESS : OCI_DEFAULT);
	}

	if (OCI_SUCCESS == err)
	{
		/* get the number of columns in the query */
		err = OCIAttrGet((void *)result->stmthp, OCI_HTYPE_STMT, (void *)&result->ncolumn,
				  (ub4 *)0, OCI_ATTR_PARAM_COUNT, oracle.errhp);
	}

	if (OCI_SUCCESS != err)
		goto error;

	assert(0 < result->ncolumn);

	result->values = zbx_malloc(NULL, result->ncolumn * sizeof(char *));
	result->clobs = zbx_malloc(NULL, result->ncolumn * sizeof(OCILobLocator *));
	result->values_alloc = zbx_malloc(NULL, result->ncolumn * sizeof(ub4));
	memset(result->values, 0, result->ncolumn * sizeof(char *));
	memset(result->clobs, 0, result->ncolumn * sizeof(OCILobLocator *));
	memset(result->values_alloc, 0, result->ncolumn * sizeof(ub4));

	for (counter = 1; OCI_SUCCESS == err && counter <= (ub4)result->ncolumn; counter++)
	{
		OCIParam	*parmdp = NULL;
		OCIDefine	*defnp = NULL;
		ub4		char_semantics;
		ub2		col_width = 0, data_type = 0;

		/* request a parameter descriptor in the select-list */
		err = OCIParamGet((void *)result->stmthp, OCI_HTYPE_STMT, oracle.errhp, (void **)&parmdp, (ub4)counter);

		if (OCI_SUCCESS == err)
		{
			/* retrieve the data type for the column */
			err = OCIAttrGet((void *)parmdp, OCI_DTYPE_PARAM, (dvoid *)&data_type, (ub4 *)NULL,
					(ub4)OCI_ATTR_DATA_TYPE, (OCIError *)oracle.errhp);
		}

		if (SQLT_CLOB == data_type)
		{
			if (OCI_SUCCESS == err)
			{
				/* allocate the lob locator variable */
				err = OCIDescriptorAlloc((dvoid *)oracle.envhp, (dvoid **)&result->clobs[counter - 1],
						OCI_DTYPE_LOB, (size_t)0, (dvoid **)0);
			}

			if (OCI_SUCCESS == err)
			{
				/* associate clob var with its define handle */
				err = OCIDefineByPos((void *)result->stmthp, &defnp, (OCIError *)oracle.errhp,
						(ub4)counter, (dvoid *)&result->clobs[counter - 1], (sb4)-1,
						data_type, (dvoid *)0, (ub2 *)0, (ub2 *)0, (ub4)OCI_DEFAULT);
			}
		}
		else
		{
			if (OCI_SUCCESS == err)
			{
				/* retrieve the length semantics for the column */
				char_semantics = 0;
				err = OCIAttrGet((void *)parmdp, (ub4)OCI_DTYPE_PARAM, (void *)&char_semantics,
						(ub4 *)NULL, (ub4)OCI_ATTR_CHAR_USED, (OCIError *)oracle.errhp);
			}

			if (OCI_SUCCESS == err)
			{
				if (0 != char_semantics)
				{
					/* retrieve the column width in characters */
					err = OCIAttrGet((void *)parmdp, (ub4)OCI_DTYPE_PARAM, (void *)&col_width,
							(ub4 *)NULL, (ub4)OCI_ATTR_CHAR_SIZE, (OCIError *)oracle.errhp);

					/* adjust for UTF-8 */
					col_width *= 4;
				}
				else
				{
					/* retrieve the column width in bytes */
					err = OCIAttrGet((void *)parmdp, (ub4)OCI_DTYPE_PARAM, (void *)&col_width,
							(ub4 *)NULL, (ub4)OCI_ATTR_DATA_SIZE, (OCIError *)oracle.errhp);
				}
			}
			col_width++;	/* add 1 byte for terminating '\0' */

			result->values_alloc[counter - 1] = col_width;
			result->values[counter - 1] = zbx_malloc(NULL, col_width);
			*result->values[counter - 1] = '\0';

			if (OCI_SUCCESS == err)
			{
				/* represent any data as characters */
				err = OCIDefineByPos(result->stmthp, &defnp, oracle.errhp, counter,
						(dvoid *)result->values[counter - 1], col_width, SQLT_STR,
						(dvoid *)0, (ub2 *)0, (ub2 *)0, OCI_DEFAULT);
			}
		}

		/* free cell descriptor */
		OCIDescriptorFree(parmdp, OCI_DTYPE_PARAM);
		parmdp = NULL;
	}
error:
	if (OCI_SUCCESS != err)
	{
		int	server_status;

		server_status = OCI_handle_sql_error(ERR_Z3005, err, sql);
		DBfree_result(result);

		result = (ZBX_DB_DOWN == server_status ? (DB_RESULT)(intptr_t)server_status : NULL);
	}
#elif defined(HAVE_POSTGRESQL)
	result = zbx_malloc(NULL, sizeof(struct zbx_db_result));
	result->pg_result = PQexec(conn, sql);
	result->values = NULL;
	result->cursor = 0;
	result->row_num = 0;

	if (NULL == result->pg_result)
		zbx_db_errlog(ERR_Z3005, 0, "result is NULL", sql);

	if (PGRES_TUPLES_OK != PQresultStatus(result->pg_result))
	{
		zbx_postgresql_error(&error, result->pg_result);
		zbx_db_errlog(ERR_Z3005, 0, error, sql);
		zbx_free(error);

		if (SUCCEED == is_recoverable_postgresql_error(conn, result->pg_result))
		{
			DBfree_result(result);
			result = (DB_RESULT)ZBX_DB_DOWN;
		}
		else
		{
			DBfree_result(result);
			result = NULL;
		}
	}
	else	/* init rownum */
		result->row_num = PQntuples(result->pg_result);
#elif defined(HAVE_SQLITE3)
	if (0 == txn_level)
		zbx_mutex_lock(sqlite_access);

	result = zbx_malloc(NULL, sizeof(struct zbx_db_result));
	result->curow = 0;

lbl_get_table:
	if (SQLITE_OK != (ret = sqlite3_get_table(conn,sql, &result->data, &result->nrow, &result->ncolumn, &error)))
	{
		if (SQLITE_BUSY == ret)
			goto lbl_get_table;

		zbx_db_errlog(ERR_Z3005, 0, error, sql);
		sqlite3_free(error);

		DBfree_result(result);

		switch (ret)
		{
			case SQLITE_ERROR:	/* SQL error or missing database; assuming SQL error, because if we
						   are this far into execution, zbx_db_connect() was successful */
			case SQLITE_NOMEM:	/* a malloc() failed */
			case SQLITE_MISMATCH:	/* data type mismatch */
				result = NULL;
				break;
			default:
				result = (DB_RESULT)ZBX_DB_DOWN;
				break;
		}
	}

	if (0 == txn_level)
		zbx_mutex_unlock(sqlite_access);
#endif	/* HAVE_SQLITE3 */
	if (0 != CONFIG_LOG_SLOW_QUERIES)
	{
		sec = zbx_time() - sec;
		if (sec > (double)CONFIG_LOG_SLOW_QUERIES / 1000.0)
			zabbix_log(LOG_LEVEL_WARNING, "slow query: " ZBX_FS_DBL " sec, \"%s\"", sec, sql);
	}

	if (NULL == result && 0 < txn_level)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "query [%s] failed, setting transaction as failed", sql);
		txn_error = ZBX_DB_FAIL;
	}
    
clean:
    // 释放内存
    zbx_free(sql);
    
    return result;
}


/*
 * Execute SQL statement. For select statements only.
 */
DB_RESULT	zbx_db_select_n(const char *query, int n)
{
#if defined(HAVE_IBM_DB2)
	return zbx_db_select("%s fetch first %d rows only", query, n);
#elif defined(HAVE_MYSQL)
	return zbx_db_select("%s limit %d", query, n);
#elif defined(HAVE_ORACLE)
	return zbx_db_select("select * from (%s) where rownum<=%d", query, n);
#elif defined(HAVE_POSTGRESQL)
	return zbx_db_select("%s limit %d", query, n);
#elif defined(HAVE_SQLITE3)
	return zbx_db_select("%s limit %d", query, n);
#endif
}

#ifdef HAVE_POSTGRESQL
/******************************************************************************
 *                                                                            *
 * Function: zbx_db_bytea_unescape                                            *
 *                                                                            *
 * Purpose: converts the null terminated string into binary buffer            *
 *                                                                            *
 * Transformations:                                                           *
 *      \ooo == a byte whose value = ooo (ooo is an octal number)             *
 *      \\   == \                                                             *
 *                                                                            *
 * Parameters:                                                                *
 *      io - [IN/OUT] null terminated string / binary data                    *
 *                                                                            *
 * Return value: length of the binary buffer                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_db_bytea_unescape 的函数，该函数用于解析输入缓冲区中的转义字符序列，并将其还原为原始的字节序列。输出缓冲区 o 存储了解析后的字节序列，而输入缓冲区 io 存储了原始的字节序列。函数最终返回输出缓冲区 o 的指针与输入缓冲区 io 的指针之差，即输出字节数。
 ******************************************************************************/
// 定义一个名为 zbx_db_bytea_unescape 的静态 size_t 类型函数，输入参数为 u_char 类型的指针 io
static size_t	zbx_db_bytea_unescape(u_char *io)
{
	// 定义两个指针 i 和 o，分别指向输入缓冲区 io 和输出缓冲区
	const u_char	*i = io;
	u_char		*o = io;

	// 遍历输入缓冲区 io 中的每个字符，直到遇到 '\0' 字符为止
	while ('\0' != *i)
	{
		// 切换到不同的处理场景
		switch (*i)
		{
			// 遇到 '\\' 字符，跳过一位，判断后续字符是否为 '\\'
			case '\\':
				i++;
				if ('\\' == *i)
				{
					// 输出 '\0' 字符
					*o++ = *i++;
				}
				else
				{
					// 判断是否为三位数字的字符串，如果是，执行以下操作
					if (0 != isdigit(i[0]) && 0 != isdigit(i[1]) && 0 != isdigit(i[2]))
					{
						// 输出 2 个 '\0' 字符，分别表示三位数字的十进制值
						*o = (*i++ - 0x30) << 6;
						*o += (*i++ - 0x30) << 3;
						*o++ += *i++ - 0x30;
					}
				}
				break;

			// 遇到非 '\\' 字符，直接输出
			default:
				*o++ = *i++;
		}
	}

	// 返回输出缓冲区 o 的指针与输入缓冲区 io 的指针之差，即输出字节数
	return o - io;
}

#endif

DB_ROW	zbx_db_fetch(DB_RESULT result)
{
#if defined(HAVE_IBM_DB2)
	int		i;
#elif defined(HAVE_ORACLE)
	int		i;
	sword		rc;
	static char	errbuf[512];
	sb4		errcode;
#endif

	if (NULL == result)
		return NULL;

#if defined(HAVE_IBM_DB2)
	if (SUCCEED != zbx_ibm_db2_success(SQLFetch(result->hstmt)))	/* e.g., SQL_NO_DATA_FOUND */
		return NULL;

	for (i = 0; i < result->ncolumn; i++)
		result->values[i] = (SQL_NULL_DATA == result->values_len[i] ? NULL : result->values_cli[i]);

	return result->values;
#elif defined(HAVE_MYSQL)
	if (NULL == result->result)
		return NULL;

	return (DB_ROW)mysql_fetch_row(result->result);
#elif defined(HAVE_ORACLE)
	if (NULL == result->stmthp)
		return NULL;

	if (OCI_NO_DATA == (rc = OCIStmtFetch2(result->stmthp, oracle.errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT)))
		return NULL;

	if (OCI_SUCCESS != rc)
	{
		ub4	rows_fetched;
		ub4	sizep = sizeof(ub4);

		if (OCI_SUCCESS != (rc = OCIErrorGet((dvoid *)oracle.errhp, (ub4)1, (text *)NULL,
				&errcode, (text *)errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR)))
		{
			zbx_db_errlog(ERR_Z3006, rc, zbx_oci_error(rc, NULL), NULL);
			return NULL;
		}

		switch (errcode)
		{
			case 1012:	/* ORA-01012: not logged on */
			case 2396:	/* ORA-02396: exceeded maximum idle time */
			case 3113:	/* ORA-03113: end-of-file on communication channel */
			case 3114:	/* ORA-03114: not connected to ORACLE */
				zbx_db_errlog(ERR_Z3006, errcode, errbuf, NULL);
				return NULL;
			default:
				rc = OCIAttrGet((void *)result->stmthp, (ub4)OCI_HTYPE_STMT, (void *)&rows_fetched,
						(ub4 *)&sizep, (ub4)OCI_ATTR_ROWS_FETCHED, (OCIError *)oracle.errhp);

				if (OCI_SUCCESS != rc || 1 != rows_fetched)
				{
					zbx_db_errlog(ERR_Z3006, errcode, errbuf, NULL);
					return NULL;
				}
		}
	}

	for (i = 0; i < result->ncolumn; i++)
	{
		ub4	alloc, amount;
		ub1	csfrm;
		sword	rc2;

		if (NULL == result->clobs[i])
			continue;

		if (OCI_SUCCESS != (rc2 = OCILobGetLength(oracle.svchp, oracle.errhp, result->clobs[i], &amount)))
		{
			/* If the LOB is NULL, the length is undefined. */
			/* In this case the function returns OCI_INVALID_HANDLE. */
			if (OCI_INVALID_HANDLE != rc2)
			{
				zbx_db_errlog(ERR_Z3006, rc2, zbx_oci_error(rc2, NULL), NULL);
				return NULL;
			}
			else
				amount = 0;
		}
		else if (OCI_SUCCESS != (rc2 = OCILobCharSetForm(oracle.envhp, oracle.errhp, result->clobs[i], &csfrm)))
		{
			zbx_db_errlog(ERR_Z3006, rc2, zbx_oci_error(rc2, NULL), NULL);
			return NULL;
		}

		if (result->values_alloc[i] < (alloc = amount * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1))
		{
			result->values_alloc[i] = alloc;
			result->values[i] = zbx_realloc(result->values[i], result->values_alloc[i]);
		}

		if (OCI_SUCCESS == rc2)
		{
			if (OCI_SUCCESS != (rc2 = OCILobRead(oracle.svchp, oracle.errhp, result->clobs[i], &amount,
					(ub4)1, (dvoid *)result->values[i], (ub4)(result->values_alloc[i] - 1),
					(dvoid *)NULL, (OCICallbackLobRead)NULL, (ub2)0, csfrm)))
			{
				zbx_db_errlog(ERR_Z3006, rc2, zbx_oci_error(rc2, NULL), NULL);
				return NULL;
			}
		}

		result->values[i][amount] = '\0';
	}

	return result->values;
#elif defined(HAVE_POSTGRESQL)
	/* free old data */
	if (NULL != result->values)
		zbx_free(result->values);

	/* EOF */
	if (result->cursor == result->row_num)
		return NULL;

	/* init result */
	result->fld_num = PQnfields(result->pg_result);

	if (result->fld_num > 0)
	{
		int	i;

		result->values = zbx_malloc(result->values, sizeof(char *) * result->fld_num);

		for (i = 0; i < result->fld_num; i++)
		{
			if (PQgetisnull(result->pg_result, result->cursor, i))
			{
				result->values[i] = NULL;
			}
			else
			{
				result->values[i] = PQgetvalue(result->pg_result, result->cursor, i);
				if (PQftype(result->pg_result, i) == ZBX_PG_BYTEAOID)	/* binary data type BYTEAOID */
					zbx_db_bytea_unescape((u_char *)result->values[i]);
			}
		}
	}

	result->cursor++;

	return result->values;
#elif defined(HAVE_SQLITE3)
	/* EOF */
	if (result->curow >= result->nrow)
		return NULL;

	if (NULL == result->data)
		return NULL;

	result->curow++;	/* NOTE: first row == header row */

	return &(result->data[result->curow * result->ncolumn]);
#endif
}

int	zbx_db_is_null(const char *field)
{
	if (NULL == field)
		return SUCCEED;
#ifdef HAVE_ORACLE
	if ('\0' == *field)
		return SUCCEED;
#endif
	return FAIL;
}

#ifdef HAVE_ORACLE
static void	OCI_DBclean_result(DB_RESULT result)
{
	if (NULL == result)
		return;

	if (NULL != result->values)
	{
		int	i;

		for (i = 0; i < result->ncolumn; i++)
		{
			zbx_free(result->values[i]);

			/* deallocate the lob locator variable */
			if (NULL != result->clobs[i])
			{
				OCIDescriptorFree((dvoid *)result->clobs[i], OCI_DTYPE_LOB);
				result->clobs[i] = NULL;
			}
		}

		zbx_free(result->values);
		zbx_free(result->clobs);
		zbx_free(result->values_alloc);
	}

	if (result->stmthp)
	{
		OCIHandleFree((dvoid *)result->stmthp, OCI_HTYPE_STMT);
		result->stmthp = NULL;
	}
}
#endif

void	DBfree_result(DB_RESULT result)
{
#if defined(HAVE_IBM_DB2)
	if (NULL == result)
		return;

	if (NULL != result->values_cli)
	{
		int	i;

		for (i = 0; i < result->nalloc; i++)
		{
			zbx_free(result->values_cli[i]);
		}

		zbx_free(result->values);
		zbx_free(result->values_cli);
		zbx_free(result->values_len);
	}

	if (result->hstmt)
		SQLFreeHandle(SQL_HANDLE_STMT, result->hstmt);

	zbx_free(result);
#elif defined(HAVE_MYSQL)
	if (NULL == result)
		return;

	mysql_free_result(result->result);
	zbx_free(result);
#elif defined(HAVE_ORACLE)
	int i;

	if (NULL == result)
		return;

	OCI_DBclean_result(result);

	for (i = 0; i < oracle.db_results.values_num; i++)
	{
		if (oracle.db_results.values[i] == result)
		{
			zbx_vector_ptr_remove_noorder(&oracle.db_results, i);
			break;
		}
	}

	zbx_free(result);
#elif defined(HAVE_POSTGRESQL)
	if (NULL == result)
		return;

	if (NULL != result->values)
	{
		result->fld_num = 0;
		zbx_free(result->values);
		result->values = NULL;
	}

	PQclear(result->pg_result);
	zbx_free(result);
#elif defined(HAVE_SQLITE3)
	if (NULL == result)
		return;

	if (NULL != result->data)
	{
		sqlite3_free_table(result->data);
	}

	zbx_free(result);
#endif	/* HAVE_SQLITE3 */
}

#ifdef HAVE_IBM_DB2
/* server status: SQL_CD_TRUE or SQL_CD_FALSE */
/******************************************************************************
 * *
 *这块代码的主要目的是检查 IBM DB2 服务器的连接状态。函数 IBM_DB2server_status() 接受无参数，返回一个整型值。首先，它定义了一个整型变量 server_status 并初始化为 SQL_CD_TRUE。然后，使用 SQLGetConnectAttr 函数获取 IBM DB2 连接的状态。如果无法确定服务器状态，则记录警告日志并假设连接已断开。最后，返回服务器状态，如果 server_status 为 SQL_CD_FALSE，则返回 SQL_CD_TRUE，否则返回 SQL_CD_FALSE。
 ******************************************************************************/
// 定义一个名为 IBM_DB2server_status 的静态函数，该函数返回一个整型值
static int IBM_DB2server_status()
{
    // 定义一个整型变量 server_status，并初始化为 SQL_CD_TRUE
    int server_status = SQL_CD_TRUE;

    // 使用 SQLGetConnectAttr 函数获取 IBM DB2 连接的状态
    if (SUCCEED != zbx_ibm_db2_success(SQLGetConnectAttr(ibm_db2.hdbc, SQL_ATTR_CONNECTION_DEAD, &server_status,
                                                     SQL_IS_POINTER, NULL)))
    {
        // 如果无法确定 IBM DB2 服务器的状态，记录警告日志并假设连接已断开
        zabbix_log(LOG_LEVEL_WARNING, "cannot determine IBM DB2 server status, assuming not connected");
    }

    // 返回服务器状态，如果 server_status 为 SQL_CD_FALSE，则返回 SQL_CD_TRUE，否则返回 SQL_CD_FALSE
    return (SQL_CD_FALSE == server_status ? SQL_CD_TRUE : SQL_CD_FALSE);
}


/******************************************************************************
 * *
 *这块代码的主要目的是判断一个传入的 SQLRETURN 类型的参数 ret 是否表示数据库操作成功。如果 ret 等于 SQL_SUCCESS 或 SQL_SUCCESS_WITH_INFO，则认为数据库操作成功，返回 SUCCEED；否则，返回 FAIL。在实际应用中，可以根据需要对成功的判断条件进行调整。
 *
 *注释中详细说明了每一行代码的作用，以及整个函数的功能。对于初学者来说，这样的注释有助于理解代码的功能和逻辑。
 ******************************************************************************/
// 定义一个名为 zbx_ibm_db2_success 的静态函数，接收一个 SQLRETURN 类型的参数 ret
static int	zbx_ibm_db2_success(SQLRETURN ret)
{
	return (SQL_SUCCESS == ret || SQL_SUCCESS_WITH_INFO == ret ? SUCCEED : FAIL);
}



/*
 * 判断ret的值，如果等于SQL_SUCCESS，或者等于SQL_SUCCESS_WITH_INFO，或者等于SQL_NO_DATA_FOUND，则返回SUCCEED
 * 否则，返回FAIL
 *
 *
 *
 * 总结：整个函数的主要目的是判断传入的SQLRETURN类型的ret值，根据不同的值返回不同的结果，SUCCEED或FAIL
 *
 ******************************************************************************/
static int	zbx_ibm_db2_success_ext(SQLRETURN ret)
{
	// 定义一个名为zbx_ibm_db2_success_ext的静态函数，接收一个SQLRETURN类型的参数ret
	return (SQL_SUCCESS == ret || SQL_SUCCESS_WITH_INFO == ret || SQL_NO_DATA_FOUND == ret ? SUCCEED : FAIL);
	// 判断ret的值，如果等于SQL_SUCCESS，或者等于SQL_SUCCESS_WITH_INFO，或者等于SQL_NO_DATA_FOUND，则返回SUCCEED
	// 否则，返回FAIL

}

// 整个函数的主要目的是判断传入的SQLRETURN类型的ret值，根据不同的值返回不同的结果，SUCCEED或FAIL


static void	zbx_ibm_db2_log_errors(SQLSMALLINT htype, SQLHANDLE hndl, zbx_err_codes_t err, const char *context)
{
	SQLCHAR		tmp_message[SQL_MAX_MESSAGE_LENGTH + 1], sqlstate[SQL_SQLSTATE_SIZE + 1];
	char		*message = NULL;
	SQLINTEGER	sqlcode = 0;
	SQLSMALLINT	rec_nr = 1;
	size_t		message_alloc = 0, message_offset = 0;

	while (SQL_SUCCESS == SQLGetDiagRec(htype, hndl, rec_nr++, sqlstate, &sqlcode, tmp_message, sizeof(tmp_message),
			NULL))
	{
		if (NULL != message)
			zbx_chrcpy_alloc(&message, &message_alloc, &message_offset, '|');

		zbx_snprintf_alloc(&message, &message_alloc, &message_offset, "[%s] %s", sqlstate, tmp_message);
	}

	zbx_db_errlog(err, (int)sqlcode, message, context);

	zbx_free(message);
}
#endif

#ifdef HAVE_ORACLE
/* server status: OCI_SERVER_NORMAL or OCI_SERVER_NOT_CONNECTED */
static ub4	OCI_DBserver_status(void)
{
	sword	err;
	ub4	server_status = OCI_SERVER_NOT_CONNECTED;

	err = OCIAttrGet((void *)oracle.srvhp, OCI_HTYPE_SERVER, (void *)&server_status,
			(ub4 *)0, OCI_ATTR_SERVER_STATUS, (OCIError *)oracle.errhp);

	if (OCI_SUCCESS != err)
		zabbix_log(LOG_LEVEL_WARNING, "cannot determine Oracle server status, assuming not connected");

	return server_status;
}
#endif	/* HAVE_ORACLE */

/******************************************************************************
 * *
 *整个代码块的主要目的是判断输入的字符是否为数据库转义序列。根据系统是否支持MySQL和PostgreSQL进行不同方式的判断。如果输入字符为单引号或反斜杠，则认为是转义序列，返回SUCCEED；否则返回FAIL。
 ******************************************************************************/
/**
 * 定义：判断字符是否为转义序列
 * 参数：char类型的输入字符c
 * 返回值：如果字符为转义序列，返回SUCCEED，否则返回FAIL
 */
static int	zbx_db_is_escape_sequence(char c)
{
    // 定义条件编译，根据系统是否支持MySQL和PostgreSQL进行判断
#if defined(HAVE_MYSQL)
    // 如果字符为单引号或反斜杠
    if ('\'' == c || '\\' == c)
#elif defined(HAVE_POSTGRESQL)
    // 如果字符为单引号或反斜杠且zbx_pg_escape_backslash为1
    if ('\'' == c || ('\\' == c && 1 == ZBX_PG_ESCAPE_BACKSLASH))
#else
    // 如果字符为单引号
    if ('\'' == c)
#endif
        // 如果是转义序列，返回SUCCEED
        return SUCCEED;
	return FAIL;
}
/******************************************************************************
 *                                                                            *
 * Function: zbx_db_escape_string                                             *
 *                                                                            *
 * Return value: escaped string                                               *
 *                                                                            *
 * Comments: sync changes with 'zbx_db_get_escape_string_len'                 *
 *           and 'zbx_db_dyn_escape_string'                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的原始字符串进行转义处理，将转义序列字符添加到目标字符串中。根据不同的数据库类型，转义序列字符可能有不同的处理方式。最后，确保目标字符串长度减至0，并添加字符串结束符'\\0'。
 ******************************************************************************/
/* 定义一个静态函数zbx_db_escape_string，用于对输入的字符串进行转义处理
 * 参数：
 *   src：原始字符串
 *   dst：目标字符串
 *   len：目标字符串长度
 *   flag：转义序列标志
 * 返回值：无
 */
static void zbx_db_escape_string(const char *src, char *dst, size_t len, zbx_escape_sequence_t flag)
{
	/* 声明两个指针变量，分别指向原始字符串和目标字符串 */
	const char *s;
	char *d;

	/* 断言，确保dst不为空 */
	assert(dst);

	/* 减去一个长度，因为字符串结束符'\0'不计入长度 */
	len--;

	/* 循环遍历原始字符串，直到遇到字符串结束符或长度为0 */
	for (s = src, d = dst; NULL != s && '\0' != *s && 0 < len; s++)
	{
		/* 如果转义序列标志为ESCAPE_SEQUENCE_ON且当前字符为转义序列字符
		 * 则将转义序列字符添加到目标字符串，并减去一个长度 */
		if (ESCAPE_SEQUENCE_ON == flag && SUCCEED == zbx_db_is_escape_sequence(*s))
		{
			if (2 > len)
				break;

			/* 根据不同数据库类型添加转义字符 */
#if defined(HAVE_MYSQL)
			*d++ = '\\';
#elif defined(HAVE_POSTGRESQL)
			*d++ = *s;
#else
			*d++ = '\'';
#endif
			len--;
		}
		/* 将当前字符添加到目标字符串，并减去一个长度 */
		*d++ = *s;
		len--;
	}
	*d = '\0';
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_get_escape_string_len                                     *
 *                                                                            *
 * Purpose: to calculate escaped string length limited by bytes or characters *
 *          whichever is reached first.                                       *
 *                                                                            *
 * Parameters: s         - [IN] string to escape                              *
 *             max_bytes - [IN] limit in bytes                                *
 *             max_chars - [IN] limit in characters                           *
 *             flag      - [IN] sequences need to be escaped on/off           *
 *                                                                            *
 * Return value: return length in bytes of escaped string                     *
 *               with terminating '\0'                                        *
 *                                                                            *
 ******************************************************************************/

static size_t	zbx_db_get_escape_string_len(const char *s, size_t max_bytes, size_t max_chars,
		zbx_escape_sequence_t flag)
{
	size_t	csize, len = 1;	/* '\0' */

	if (NULL == s)
		return len;

	while ('\0' != *s && 0 < max_chars)
	{
		csize = zbx_utf8_char_len(s);

		/* process non-UTF-8 characters as single byte characters */
		if (0 == csize)
			csize = 1;

		if (max_bytes < csize)
			break;

		if (ESCAPE_SEQUENCE_ON == flag && SUCCEED == zbx_db_is_escape_sequence(*s))
			len++;

		s += csize;
		len += csize;
		max_bytes -= csize;
		max_chars--;
	}

	return len;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_dyn_escape_string                                         *
 *                                                                            *
 * Purpose: to escape string limited by bytes or characters, whichever limit  *
 *          is reached first.                                                 *
 *                                                                            *
 * Parameters: src       - [IN] string to escape                              *
 *             max_bytes - [IN] limit in bytes                                *
 *             max_chars - [IN] limit in characters                           *
 *             flag      - [IN] sequences need to be escaped on/off           *
 *                                                                            *
 * Return value: escaped string                                               *
 *                                                                            *
 ******************************************************************************/
// zbx_db_dyn_escape_string 函数，用于动态生成转义后的字符串
char *zbx_db_dyn_escape_string(const char *src, size_t max_bytes, size_t max_chars, zbx_escape_sequence_t flag)
{
	// 定义一个指向目的字符串的指针 dst，初始值为 NULL
	char	*dst = NULL;
	size_t	len;
	// 计算源字符串需要转义的长度，存储在 len 变量中
	len = zbx_db_get_escape_string_len(src, max_bytes, max_chars, flag);

	// 为目的字符串分配内存空间，存储在 dst 指针中
	dst = (char *)zbx_malloc(dst, len);

	// 使用 zbx_db_escape_string 函数对源字符串进行转义，并将结果存储在目的字符串 dst 中
	zbx_db_escape_string(src, dst, len, flag);

	// 返回处理后的目的字符串 dst
	return dst;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_get_escape_like_pattern_len                               *
 *                                                                            *
 * Return value: return length of escaped LIKE pattern with terminating '\0'  *
 *                                                                            *
 * Comments: sync changes with 'zbx_db_escape_like_pattern'                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个C语言字符串中转义序列和特殊字符（包括'_'、'%'和ZBX_SQL_LIKE_ESCAPE_CHAR）的总长度。函数zbx_db_get_escape_like_pattern_len接收一个const char类型的指针作为输入参数，并返回计算得到的长度。在计算过程中，首先调用zbx_db_get_escape_string_len函数计算转义序列的长度，然后遍历字符串，统计特殊字符的出现次数，最后在字符串结尾添加一个'\\0'，并返回总长度。
 ******************************************************************************/
// 定义一个静态函数zbx_db_get_escape_like_pattern_len，接收一个const char类型的指针作为参数
static int zbx_db_get_escape_like_pattern_len(const char *src)
{
	// 定义一个整型变量len，用于存储结果
	int len;
	// 定义一个常量指针s，初始值为src，用于遍历字符串
	const char *s;

	// 调用zbx_db_get_escape_string_len函数，计算字符串中转义序列的长度，减去1以去掉结尾的'\0'
	len = zbx_db_get_escape_string_len(src, ZBX_SIZE_T_MAX, ZBX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON) - 1;

	// 遍历字符串，计算其中包含特殊字符的长度
	for (s = src; s && *s; s++)
	{
		// 统计字符串中'_'、'%'、ZBX_SQL_LIKE_ESCAPE_CHAR（即反斜杠）的出现次数
		len += (*s == '_' || *s == '%' || *s == ZBX_SQL_LIKE_ESCAPE_CHAR);
		// 每个特殊字符后面还需要加上1，因为后面会跟着一个转义字符
		len += 1;
	}

	// 在字符串结尾添加一个'\0'，以便后续处理
	len++;

	// 返回计算得到的总长度
	return len;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_escape_like_pattern                                       *
 *                                                                            *
 * Return value: escaped string to be used as pattern in LIKE                 *
 *                                                                            *
 * Comments: sync changes with 'zbx_db_get_escape_like_pattern_len'           *
 *                                                                            *
 *           For instance, we wish to find string a_b%c\d'e!f in our database *
 *           using '!' as escape character. Our queries then become:          *
 *                                                                            *
 *           ... LIKE 'a!_b!%c\\d\'e!!f' ESCAPE '!' (MySQL, PostgreSQL)       *
 *           ... LIKE 'a!_b!%c\d''e!!f' ESCAPE '!' (IBM DB2, Oracle, SQLite3) *
 *                                                                            *
 *           Using backslash as escape character in LIKE would be too much    *
 *           trouble, because escaping backslashes would have to be escaped   *
 *           as well, like so:                                                *
 *                                                                            *
 *           ... LIKE 'a\\_b\\%c\\\\d\'e!f' ESCAPE '\\' or                    *
 *           ... LIKE 'a\\_b\\%c\\\\d\\\'e!f' ESCAPE '\\' (MySQL, PostgreSQL) *
 *           ... LIKE 'a\_b\%c\\d''e!f' ESCAPE '\' (IBM DB2, Oracle, SQLite3) *
 *                                                                            *
 *           Hence '!' instead of backslash.                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的 SQL 查询字符串进行转义处理，将其中的 '_'、'%' 和 ZBX_SQL_LIKE_ESCAPE_CHAR 替换为 '%'，并在结尾添加 '\\0'，以便后续使用。最后释放分配的内存。
 ******************************************************************************/
static void zbx_db_escape_like_pattern(const char *src, char *dst, int len)
{
	/* 声明变量 */
	char		*d;
	char		*tmp = NULL;
	const char	*t;

	/* 断言确保 dst 指针不为空 */
	assert(dst);

	/* 为临时存储字符串分配内存 */
	tmp = (char *)zbx_malloc(tmp, len);

	/* 对源字符串进行转义处理 */
	zbx_db_escape_string(src, tmp, len, ESCAPE_SEQUENCE_ON);

	/* 减去 '\0' 字符，因为后面会将其替换为 '%' */
	len--;

	/* 遍历临时存储字符串 */
	for (t = tmp, d = dst; t && *t && len; t++)
	{
		/* 如果遇到 '_'、'%' 或 ZBX_SQL_LIKE_ESCAPE_CHAR，将其替换为 '%' */
		if (*t == '_' || *t == '%' || *t == ZBX_SQL_LIKE_ESCAPE_CHAR)
		{
			if (len <= 1)
				break;
			*d++ = ZBX_SQL_LIKE_ESCAPE_CHAR;
			len--;
		}
		*d++ = *t;
		len--;
	}

	*d = '\0';

	zbx_free(tmp);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_dyn_escape_like_pattern                                   *
 *                                                                            *
 * Return value: escaped string to be used as pattern in LIKE                 *
 *                                                                            *
 ******************************************************************************/
char	*zbx_db_dyn_escape_like_pattern(const char *src)
{
	int	len;
	char	*dst = NULL;

	len = zbx_db_get_escape_like_pattern_len(src);

	dst = (char *)zbx_malloc(dst, len);

	zbx_db_escape_like_pattern(src, dst, len);

	return dst;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_strlen_n                                                  *
 *                                                                            *
 * Purpose: return the string length to fit into a database field of the      *
 *          specified size                                                    *
 *                                                                            *
 * Return value: the string length in bytes                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 * * zbx_db_strlen_n函数用于计算给定字符串的长度，并根据不同的编译环境选择使用不同的算法。
 * *
 ******************************************************************************/
// 定义一个C语言函数zbx_db_strlen_n，接收两个参数：一个const char类型的指针text，表示要计算长度的字符串；一个size_t类型的整数maxlen，表示字符串的最大长度。
int zbx_db_strlen_n(const char *text, size_t maxlen)
{
    // 使用预处理器指令#ifdef HAVE_IBM_DB2，如果定义了HAVE_IBM_DB2宏，则执行后面的代码块。
    #ifdef HAVE_IBM_DB2
        // 调用zbx_strlen_utf8_nbytes函数，计算字符串text的长度，并将结果返回。
        return zbx_strlen_utf8_nbytes(text, maxlen);
    #else
        // 如果未定义HAVE_IBM_DB2宏，则执行下面的代码块。
        return zbx_strlen_utf8_nchars(text, maxlen);
    #endif
}

// 函数zbx_db_strlen_n的主要目的是计算给定字符串的长度，并根据不同的编译环境选择使用不同的算法。

