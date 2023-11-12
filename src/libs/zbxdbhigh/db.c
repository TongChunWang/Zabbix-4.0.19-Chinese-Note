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

#include "db.h"
#include "log.h"
#include "common.h"
#include "events.h"
#include "threads.h"
#include "zbxserver.h"
#include "dbcache.h"
#include "zbxalgo.h"

#if defined(HAVE_MYSQL) || defined(HAVE_ORACLE) || defined(HAVE_POSTGRESQL)
#define ZBX_SUPPORTED_DB_CHARACTER_SET	"utf8"
#endif
#if defined(HAVE_MYSQL)
#define ZBX_SUPPORTED_DB_COLLATION	"utf8_bin"
#endif

typedef struct
{
	zbx_uint64_t	autoreg_hostid;
	zbx_uint64_t	hostid;
	char		*host;
	char		*ip;
	char		*dns;
	char		*host_metadata;
	int		now;
	unsigned short	port;
}
zbx_autoreg_host_t;

#if defined(HAVE_POSTGRESQL)
extern char	ZBX_PG_ESCAPE_BACKSLASH;
#endif

static int	connection_failure;
/******************************************************************************
 * *
 *这段代码定义了一个名为DBclose的函数，该函数不需要传入任何参数。在函数内部，调用了名为zbx_db_close的函数，用于关闭数据库连接。整个代码块的主要目的是在程序运行结束时关闭数据库连接，以确保资源得到正确释放。
 ******************************************************************************/
// 这是一个C语言函数，名为DBclose，其作用是关闭数据库连接。
void	DBclose(void)           // 定义一个名为DBclose的函数，不需要传入任何参数。
{
	zbx_db_close();           // 调用名为zbx_db_close的函数，该函数可能是用于关闭数据库连接的。
}

// 整个代码块的主要目的是关闭数据库连接。


/******************************************************************************
 *                                                                            *
 * Function: DBconnect                                                        *
 *                                                                            *
 * Purpose: connect to the database                                           *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 DBconnect 的函数，该函数接收一个整数参数 flag，用于控制连接数据库的策略。函数内部使用 zbx_db_connect 函数尝试连接数据库，如果连接失败，则根据 flag 的值进行不同的处理，如等待重试或记录错误日志并退出程序。当数据库连接成功时，记录调试日志并返回错误码。
 ******************************************************************************/
// 定义一个名为 DBconnect 的函数，接收一个整数参数 flag
int DBconnect(int flag)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DBconnect";

    // 定义一个整数变量 err，用于存储数据库连接错误码
    int err;

    // 使用 zabbix_log 函数记录调试信息，显示当前函数名和传入的 flag 值
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() flag:%d", __function_name, flag);

    // 使用 while 循环，当zbx_db_connect函数返回值不为ZBX_DB_OK时，继续循环
    while (ZBX_DB_OK != (err = zbx_db_connect(CONFIG_DBHOST, CONFIG_DBUSER, CONFIG_DBPASSWORD,
                                             CONFIG_DBNAME, CONFIG_DBSCHEMA, CONFIG_DBSOCKET, CONFIG_DBPORT)))
    {
        // 判断 flag 值，如果为 ZBX_DB_CONNECT_ONCE，则跳出循环
        if (ZBX_DB_CONNECT_ONCE == flag)
            break;

        // 如果连接失败或 flag 值为 ZBX_DB_CONNECT_EXIT，则记录错误日志并退出程序
        if (ZBX_DB_FAIL == err || ZBX_DB_CONNECT_EXIT == flag)
        {
            zabbix_log(LOG_LEVEL_CRIT, "Cannot connect to the database. Exiting...");
            exit(EXIT_FAILURE);
        }

        // 如果连接失败，记录错误日志，并等待一段时间后重试连接
        zabbix_log(LOG_LEVEL_ERR, "database is down: reconnecting in %d seconds", ZBX_DB_WAIT_DOWN);
        connection_failure = 1;
        zbx_sleep(ZBX_DB_WAIT_DOWN);
    }

    // 如果重试连接失败次数不为0，说明连接已恢复，重置重试次数
    if (0 != connection_failure)
    {
        zabbix_log(LOG_LEVEL_ERR, "database connection re-established");
        connection_failure = 0;
    }

    // 记录调试日志，显示函数结束和错误码
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, err);

    // 返回错误码
    return err;
}

		connection_failure = 0;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, err);

	return err;
}

/******************************************************************************
 *                                                                            *
 * Function: DBinit                                                           *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化数据库。函数DBinit接收一个字符指针数组作为参数，用于存储错误信息。函数内部首先定义了两个常量：数据库名称（CONFIG_DBNAME）和数据库架构文件（db_schema）。接着调用zbx_db_init函数来初始化数据库，并将返回值作为函数DBinit的返回值。如果初始化成功，返回0；如果失败，返回非0值。
 ******************************************************************************/
int DBinit(char **error)
{
    // 定义常量
    const char *CONFIG_DBNAME = "zbx_db";
    const char *db_schema = "zbx_db.schema";

    // 调用zbx_db_init函数初始化数据库
    return zbx_db_init(CONFIG_DBNAME, db_schema, error);
}

/**
 * 函数DBinit用于初始化数据库
 * 参数：error - 错误信息指针
 * 返回值：0 - 成功，非0 - 失败
 */

/******************************************************************************
 * *
 *整个代码块的主要目的是：初始化数据库。
 *
 *注释详细说明：
 *
 *1. 定义一个名为 DBdeinit 的函数，该函数为 void 类型（无返回值）。
 *2. 调用 zbx_db_deinit() 函数，用于初始化数据库。
 *3. 在程序运行过程中，当需要使用数据库时，可以调用此函数进行初始化。
 *
 *输出注释后的代码块：
 *
 *```c
 */**
 * * @file db_deinit.c
/******************************************************************************
 * *
 *这块代码的主要目的是在一个循环中不断尝试执行数据库事务操作，直到成功为止。在此过程中，如果数据库连接失败，程序会关闭数据库连接，然后等待一段时间后重新连接并尝试执行事务操作。如果仍然失败，则会继续等待并重试。整个过程通过调用`DBtxn_operation`函数来实现，该函数接收一个指向事务操作函数的指针作为参数。
 ******************************************************************************/
// 定义一个静态函数，用于执行数据库事务操作
static void DBtxn_operation(int (*txn_operation)(void))
{
    // 定义一个整型变量rc，用于存储事务操作的结果
    int rc;

    // 调用事务操作函数，并将结果存储在rc变量中
    rc = txn_operation();

    // 判断rc的值，如果为ZBX_DB_DOWN，则进入循环
    while (ZBX_DB_DOWN == rc)
/******************************************************************************
 * *
 *void\tDBbegin(void)
 *{
 *\t// 调用 DBtxn_operation 函数，传入参数 zbx_db_begin，表示开始一个数据库事务操作
 *\tDBtxn_operation(zbx_db_begin);
 *}
 *
 *// 总结：该代码块定义了一个名为 DBbegin 的函数，其主要目的是用于开始一个数据库事务操作。输出结果为：无
 *```
 *
 *void\tDBbegin(void)
 *{
 *\t// 调用 DBtxn_operation 函数，传入参数 zbx_db_begin，表示开始一个数据库事务操作
 *\tDBtxn_operation(zbx_db_begin);
 *\t// 函数执行完毕后，自动返回 void 类型的值，表示无事可做
 *}
 *
 *// 总结：该代码块定义了一个名为 DBbegin 的函数，其主要目的是用于开始一个数据库事务操作。输出结果为：无
 *```
 *
 *```c
 *// 定义一个名为 DBbegin 的函数，不接受任何参数，返回类型为 void
 *void DBbegin(void)
 *{
 *    // 调用 DBtxn_operation 函数，传入参数 zbx_db_begin
 *    // DBtxn_operation 函数用于开始一个数据库事务操作
 *    // 函数执行完毕后，自动返回 void 类型的值，表示无事可做
 *}
 *
 *// 总结：该代码块定义了一个名为 DBbegin 的函数，其主要目的是用于开始一个数据库事务操作。
 *```
 ******************************************************************************/
// 定义一个名为 DBbegin 的函数，该函数不接受任何参数，返回类型为 void
void DBbegin(void)
{
    // 调用 DBtxn_operation 函数，传入参数 zbx_db_begin
    // DBtxn_operation 函数用于开始一个数据库事务操作
}

// 总结：该代码块定义了一个名为 DBbegin 的函数，其主要目的是用于开始一个数据库事务操作。

        // 重新连接数据库，连接方式为正常连接
        DBconnect(ZBX_DB_CONNECT_NORMAL);

        // 再次调用事务操作函数，并将结果存储在rc变量中
        if (ZBX_DB_DOWN == (rc = txn_operation()))
        {
            // 如果数据库仍然处于关闭状态，记录日志并等待重试
            zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);
            connection_failure = 1;
            sleep(ZBX_DB_WAIT_DOWN);
        }
    }
}

 ******************************************************************************/
// 定义一个名为 DBdeinit 的函数，该函数为 void 类型（无返回值）
void DBdeinit(void)
{
    // 调用 zbx_db_deinit() 函数，用于初始化数据库
    zbx_db_deinit();
}



/******************************************************************************
 *                                                                            *
 * Function: DBtxn_operation                                                  *
 *                                                                            *
 * Purpose: helper function to loop transaction operation while DB is down    *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 ******************************************************************************/
static void	DBtxn_operation(int (*txn_operation)(void))
{
	int	rc;

	rc = txn_operation();

	while (ZBX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		if (ZBX_DB_DOWN == (rc = txn_operation()))
		{
			zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(ZBX_DB_WAIT_DOWN);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DBbegin                                                          *
 *                                                                            *
 * Purpose: start a transaction                                               *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
void	DBbegin(void)
{
	DBtxn_operation(zbx_db_begin);
}

/******************************************************************************
 *                                                                            *
 * Function: DBcommit                                                         *
 *                                                                            *
 * Purpose: commit a transaction                                              *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于处理数据库事务，当调用commit函数时，如果数据库事务失败，则打印日志并执行回滚操作。最后结束事务并返回错误状态码。
 ******************************************************************************/
// 定义一个名为 DBcommit 的函数，该函数为 void 类型，即无返回值
int DBcommit(void)
{
    // 判断 ZBX_DB_OK 是否大于zbx_db_commit()的返回值
    if (ZBX_DB_OK > zbx_db_commit())
    {
        // 如果事务失败，打印一条日志，表示调用commit时发生错误，改为进行回滚操作
        zabbix_log(LOG_LEVEL_DEBUG, "commit called on failed transaction, doing a rollback instead");

        // 调用 DBrollback() 函数进行回滚操作
        DBrollback();
    }

    // 调用 zbx_db_txn_end_error() 函数，结束事务并返回错误状态码
    return zbx_db_txn_end_error();
}


/******************************************************************************
 *                                                                            *
 * Function: DBrollback                                                       *
 *                                                                            *
 * Purpose: rollback a transaction                                            *
 *                                                                            *
 * Author: Eugene Grigorjev, Vladimir Levijev                                 *
 *                                                                            *
 * Comments: do nothing if DB does not support transactions                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是执行数据库回滚操作。当回滚操作失败时，记录警告日志，关闭数据库连接，并重新连接数据库。
 ******************************************************************************/
void	DBrollback(void) // 定义一个名为 DBrollback 的函数，用于执行数据库回滚操作
{
	if (ZBX_DB_OK > zbx_db_rollback()) // 判断 zbx_db_rollback 函数返回值是否大于 ZBX_DB_OK，即是否成功执行回滚操作
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot perform transaction rollback, connection will be reset") // 如果回滚失败，记录日志，提示无法执行事务回滚，并将连接重置

		DBclose(); // 关闭数据库连接
		DBconnect(ZBX_DB_CONNECT_NORMAL) // 重新连接数据库，连接方式为正常连接
	}
}


/******************************************************************************
 *                                                                            *
 * Function: DBend                                                            *
 *                                                                            *
/******************************************************************************
 * *
 *这块代码的主要目的是判断数据库操作是否成功，并在成功的情况下返回 SUCCEED，失败的情况下返回 FAIL。具体来说，函数 DBend 接收一个整型参数 ret，表示数据库操作的返回值。首先判断 ret 是否为 SUCCEED，如果是，则调用 DBcommit() 函数判断数据库操作是否成功。如果 DBcommit() 返回 SUCCEED，则返回 SUCCEED；否则，调用 DBrollback() 函数回滚数据库操作，并返回 FAIL。如果 ret 不是 SUCCEED，直接返回 FAIL。无论何种情况，最后返回 FAIL。
 ******************************************************************************/
// 定义一个函数 DBend，接收一个整型参数 ret
int DBend(int ret)
{
    // 判断 ret 是否为 SUCCEED（成功）
    if (SUCCEED == ret)
    {
        // 调用 DBcommit() 函数，判断数据库操作是否成功
        if (ZBX_DB_OK == DBcommit())
        {
            // 如果 DBcommit() 返回 SUCCEED，则返回 SUCCEED，否则返回 FAIL
            return SUCCEED;
        }
        else
        {
            // 如果 DBcommit() 返回 FAIL，则调用 DBrollback() 函数回滚数据库操作
            DBrollback();
        }
    }
    // 如果 ret 不为 SUCCEED，直接返回 FAIL
    else
    {
        DBrollback();
    }

    // 无论何种情况，最后返回 FAIL
    return FAIL;
}


	DBrollback();

	return FAIL;
}

#ifdef HAVE_ORACLE
/******************************************************************************
 *                                                                            *
 * Function: DBstatement_prepare                                              *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是：解析给定的 SQL 语句，如果数据库连接失败，则不断尝试重新连接数据库，并等待一段时间后再次尝试。在这个过程中，如果数据库仍然连接失败，则会记录日志并设置 connection_failure 变量为 1。
 ******************************************************************************/
// 定义一个名为 DBstatement_prepare 的函数，参数为一个 const char 类型的指针，表示 SQL 语句
void DBstatement_prepare(const char *sql)
{
    // 定义一个整型变量 rc，用于存储数据库操作的结果
    int rc;

    // 调用 zbx_db_statement_prepare 函数，将 sql 字符串解析为数据库语句，并将结果存储在 rc 中
    rc = zbx_db_statement_prepare(sql);

    // 判断 rc 的值，如果等于 ZBX_DB_DOWN，表示数据库连接失败
    while (ZBX_DB_DOWN == rc)
    {
        // 关闭数据库连接
        DBclose();

        // 重新连接数据库，连接方式为正常连接
        DBconnect(ZBX_DB_CONNECT_NORMAL);

        // 再次调用 zbx_db_statement_prepare 函数，如果 rc 仍然等于 ZBX_DB_DOWN，表示数据库仍然连接失败
        if (ZBX_DB_DOWN == (rc = zbx_db_statement_prepare(sql)))
        {
            // 记录日志，表示数据库连接失败
            zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);

            // 设置 connection_failure 变量为 1，表示数据库连接失败
            connection_failure = 1;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 DBexecute 的函数，该函数接收一个格式化字符串 fmt 和一个可变参数列表 args。函数的主要功能是执行数据库操作，如果数据库连接失败，则不断尝试重新连接并执行操作，同时记录连接失败日志。当数据库操作成功时，返回执行结果。
 ******************************************************************************/
// 定义一个名为 DBexecute 的函数，接收一个 const char * 类型的参数 fmt 和一个可变参数列表 ...
int DBexecute(const char *fmt, ...)
{
	// 声明一个 va_list 类型的变量 args，用于存储可变参数列表
	va_list args;

	// 声明一个 int 类型的变量 rc，用于存储函数执行结果
	int rc;

	// 使用 va_start 初始化 args 变量，使其指向可变参数列表
	va_start(args, fmt);

	// 调用 zbx_db_vexecute 函数，传入 fmt 和 args 参数，并将执行结果存储在 rc 变量中
	rc = zbx_db_vexecute(fmt, args);

	// 判断 rc 是否等于 ZBX_DB_DOWN，如果是，则进入循环进行重试
	while (ZBX_DB_DOWN == rc)
	{
		// 调用 DBclose 函数关闭数据库连接
		DBclose();

		// 调用 DBconnect 函数重新连接数据库，传入 ZBX_DB_CONNECT_NORMAL 参数
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		// 再次调用 zbx_db_vexecute 函数，传入 fmt 和 args 参数，并将执行结果存储在 rc 变量中
		if (ZBX_DB_DOWN == (rc = zbx_db_vexecute(fmt, args)))
		{
			// 如果数据库仍然处于down状态，记录日志并等待一段时间后重试
			zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(ZBX_DB_WAIT_DOWN);
		}
	}

	// 使用 va_end 清理 va_list 类型的变量 args
	va_end(args);

	// 返回 rc 变量，即 DBexecute 函数的执行结果
	return rc;
}

	int	rc;

	va_start(args, fmt);

	rc = zbx_db_vexecute(fmt, args);

	while (ZBX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		if (ZBX_DB_DOWN == (rc = zbx_db_vexecute(fmt, args)))
		{
			zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(ZBX_DB_WAIT_DOWN);
		}
	}

	va_end(args);

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Function: __zbx_DBexecute_once                                             *
 *                                                                            *
 * Purpose: execute a non-select statement                                    *
 *                                                                            *
 * Comments: don't retry if DB is down                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 DBexecute_once 的函数，该函数接收一个字符指针（const char *fmt）以及可变参数...，然后调用 zbx_db_vexecute 函数执行数据库操作，并返回执行结果。在此过程中，使用了 va_list 类型的变量 args 用于存储可变参数列表，并在调用完 zbx_db_vexecute 函数后释放内存。
 ******************************************************************************/
// 定义一个名为 DBexecute_once 的函数，参数为一个字符指针（const char *fmt）以及可变参数...
int DBexecute_once(const char *fmt, ...)
{
	// 声明一个 va_list 类型的变量 args，用于存储可变参数列表
	va_list args;

	// 初始化 args 变量，准备接收可变参数
	va_start(args, fmt);

	// 调用 zbx_db_vexecute 函数，传入 fmt 和 args 作为参数，执行数据库操作
	int rc = zbx_db_vexecute(fmt, args);

	// 结束 va_list 类型的变量 args，释放内存
	va_end(args);

	// 返回 zbx_db_vexecute 函数的执行结果rc
	return rc;
}

/******************************************************************************
 * *
 *这段代码的主要目的是判断一个数据库字段的值是否为空。通过调用 `zbx_db_is_null` 函数，将字段指针传递给该函数，判断其指向的字符串是否为空。如果为空，函数返回 1（表示空），否则返回 0。这样，我们可以根据返回值来判断字段值是否为空。
 ******************************************************************************/
// 定义一个函数 DBis_null，用于判断数据库字段值是否为空
int DBis_null(const char *field)
{
    // 调用 zbx_db_is_null 函数，判断 field 指针所指向的字符串是否为空
    // 如果为空，返回 1（表示空），否则返回 0
    return zbx_db_is_null(field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 DB_ROW 的结构体，用于存储从数据库查询结果中获取的一行数据。同时，实现了一个名为 DBfetch 的函数，该函数从 DB_RESULT 类型的结果对象中获取查询结果中的一行数据，并将结果存储在 DB_ROW 类型的结构体中。
 ******************************************************************************/
// 定义一个名为 DB_ROW 的结构体，用于存储从数据库查询结果中获取的一行数据
typedef struct {
    // 以下是一些列变量，用于存储查询结果中的数据
    char *column1;
    char *column2;
    // ...
} DB_ROW;

// DBfetch 函数，用于从 DB_RESULT 类型的结果对象中获取一行数据
DB_ROW DB_ROW::DBfetch(DB_RESULT result)
{
    // 调用 zbx_db_fetch 函数，传入 DB_RESULT 类型的结果对象，获取查询结果中的一行数据
    return zbx_db_fetch(result);
}


/******************************************************************************
 *                                                                            *
 * Function: DBselect_once                                                    *
 *                                                                            *
 * Purpose: execute a select statement                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 DBselect 的函数，该函数接收一个 const char * 类型的参数 fmt 和可变参数...，用于执行数据库查询操作。当数据库连接出现故障时，函数会尝试重新连接数据库并等待一段时间后再次执行查询，直到成功为止。同时，函数还会记录数据库连接失败的日志。最后，返回数据库查询结果。
 ******************************************************************************/
// 定义一个名为 DBselect 的函数，接收一个 const char * 类型的参数 fmt 和可变参数...
DB_RESULT	DBselect(const char *fmt, ...)
{
	// 定义一个 va_list 类型的变量 args，用于存储可变参数列表
	va_list		args;
	// 定义一个 DB_RESULT 类型的变量 rc，用于存储函数返回值
	DB_RESULT	rc;

	// 使用 va_start 初始化 args 变量，使其指向可变参数列表
	va_start(args, fmt);

	// 调用 zbx_db_vselect 函数，传入 fmt 和 args 参数，并将返回值赋给 rc
	rc = zbx_db_vselect(fmt, args);

	// 判断 rc 的值是否等于 ZBX_DB_DOWN，如果是，则进入循环
	while ((DB_RESULT)ZBX_DB_DOWN == rc)
	{
		// 调用 DBclose 函数关闭数据库连接
		DBclose();
		// 调用 DBconnect 函数重新连接数据库，传入 ZBX_DB_CONNECT_NORMAL 参数
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		// 再次调用 zbx_db_vselect 函数，传入 fmt 和 args 参数，并将返回值与 ZBX_DB_DOWN 进行比较
		if ((DB_RESULT)ZBX_DB_DOWN == (rc = zbx_db_vselect(fmt, args)))
		{
			// 如果数据库仍然处于下线状态，记录日志并等待一段时间后重试
			zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(ZBX_DB_WAIT_DOWN);
		}
	}

	// 使用 va_end 清理 args 变量
	va_end(args);

	// 返回 rc 变量，表示数据库查询结果
	return rc;
}

 * Function: DBselect                                                         *
 *                                                                            *
 * Purpose: execute a select statement                                        *
 *                                                                            *
 * Comments: retry until DB is up                                             *
 *                                                                            *
 ******************************************************************************/
DB_RESULT	DBselect(const char *fmt, ...)
{
	va_list		args;
	DB_RESULT	rc;

	va_start(args, fmt);

	rc = zbx_db_vselect(fmt, args);

	while ((DB_RESULT)ZBX_DB_DOWN == rc)
	{
		DBclose();
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		if ((DB_RESULT)ZBX_DB_DOWN == (rc = zbx_db_vselect(fmt, args)))
		{
			zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);
			connection_failure = 1;
			sleep(ZBX_DB_WAIT_DOWN);
		}
	}

	va_end(args);

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Function: DBselectN                                                        *
 *                                                                            *
 * Purpose: execute a select statement and get the first N entries            *
 *                                                                            *
 * Comments: retry until DB is up                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 DBselectN 的函数，该函数接收一个字符串指针 query 和一个整数 n 作为参数，执行查询并将结果存储在 DB_RESULT 类型的变量 rc 中。如果查询结果为 ZBX_DB_DOWN，则表示数据库故障，函数会循环重试连接数据库并重新执行查询。在重试过程中，会记录日志、设置连接失败标志并等待一段时间后再次尝试查询。当查询成功时，返回查询结果 rc。
 ******************************************************************************/
// 定义一个名为 DBselectN 的函数，接收两个参数：一个字符串指针 query 和一个整数 n
DB_RESULT	DBselectN(const char *query, int n)
{
	// 定义一个名为 rc 的 DB_RESULT 类型的变量，用于存储查询结果
	DB_RESULT	rc;

	// 调用 zbx_db_select_n 函数执行查询，并将结果存储在 rc 变量中
	rc = zbx_db_select_n(query, n);

	// 当查询结果为 ZBX_DB_DOWN 时，进入循环进行重试
	while ((DB_RESULT)ZBX_DB_DOWN == rc)
	{
		// 关闭数据库连接
		DBclose();

		// 重新连接数据库，连接方式为 ZBX_DB_CONNECT_NORMAL
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		// 再次调用 zbx_db_select_n 函数执行查询，并将结果存储在 rc 变量中
		if ((DB_RESULT)ZBX_DB_DOWN == (rc = zbx_db_select_n(query, n)))
		{
			// 记录日志：数据库故障，等待重试
			zabbix_log(LOG_LEVEL_ERR, "database is down: retrying in %d seconds", ZBX_DB_WAIT_DOWN);

			// 设置 connection_failure 变量为 1，表示数据库连接失败
			connection_failure = 1;

			// 睡眠 ZBX_DB_WAIT_DOWN 秒，等待后再次尝试查询
			sleep(ZBX_DB_WAIT_DOWN);
		}
	}

	// 返回查询结果 rc
	return rc;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查询给定表名中的行数，并将结果返回。过程中使用了 DBselect 函数执行 SQL 查询，DBfetch 函数获取查询结果的一行数据，并将行数转换为整型。最后，使用 zabbix_log 函数记录调试信息。
 ******************************************************************************/
/* 定义一个名为 DBget_row_count 的函数，接收一个 const char * 类型的参数 table_name。
* 该函数的主要目的是查询给定表名中的行数。
*/
int	DBget_row_count(const char *table_name)
{
	/* 定义一个常量字符串，用于存储函数名 */
	const char	*__function_name = "DBget_row_count";

	/* 定义一个整型变量 count，用于存储查询结果的行数 */
	int		count = 0;

	/* 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果 */
	DB_RESULT	result;

	/* 定义一个 DB_ROW 类型的变量 row，用于存储查询结果的一行数据 */
	DB_ROW		row;

	/* 使用 zabbix_log 函数记录调试信息，表示函数调用，传入函数名和表名 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() table_name:'%s'", __function_name, table_name);

	/* 使用 DBselect 函数执行 SQL 查询，查询表中的行数，并将结果存储在 result 变量中 */
	result = DBselect("select count(*) from %s", table_name);

	/* 判断是否从结果中获取到一行数据，如果获取到，将其存储在 row 变量中 */
	if (NULL != (row = DBfetch(result)))
	{
		/* 将 row 变量中的第一列数据（字符串类型）转换为整型，并存储在 count 变量中 */
		count = atoi(row[0]);
	}

	/* 释放 result 变量占用的内存 */
	DBfree_result(result);

	/* 使用 zabbix_log 函数记录调试信息，表示函数执行结束，传入函数名和 count 变量值 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, count);

	/* 返回 count 变量的值，即查询到的行数 */
	return count;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从一个数据库表（hosts）中查询代理服务器的最近访问时间，并将结果存储在 `lastaccess` 指针指向的内存区域。如果查询失败，则返回一个错误信息。
 *
 *函数输入参数：
 *- `hostname`：代理服务器的名称。
 *- `lastaccess`：指向一个整数类型的指针，用于存储查询到的最近访问时间。
 *- `error`：指向一个字符串类型的指针，用于存储错误信息。
 *
 *函数返回值：
 *- `ret`：表示查询是否成功的整数，成功则返回 `SUCCEED`，失败则返回 `FAIL`。
 ******************************************************************************/
// 定义一个函数，用于获取代理服务器的最近访问时间
int DBget_proxy_lastaccess(const char *hostname, int *lastaccess, char **error)
{
    // 定义一些常量和变量
    const char *__function_name = "DBget_proxy_lastaccess";
    DB_RESULT	result;
    DB_ROW		row;
    char		*host_esc;
    int		ret = FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 对主机名进行转义，防止SQL注入
    host_esc = DBdyn_escape_string(hostname);

    // 从数据库中查询代理服务器的最近访问时间
    result = DBselect("select lastaccess from hosts where host='%s' and status in (%d,%d)",
                    host_esc, HOST_STATUS_PROXY_ACTIVE, HOST_STATUS_PROXY_PASSIVE);

    // 释放 host_esc 内存
    zbx_free(host_esc);

    // 如果查询结果不为空，则提取最近访问时间并返回成功
    if (NULL != (row = DBfetch(result)))
    {
        *lastaccess = atoi(row[0]);
        ret = SUCCEED;
    }
    // 如果没有查询到结果，返回错误信息
    else
    {
        *error = zbx_dsprintf(*error, "Proxy \"%s\" does not exist.", hostname);
    }

    // 释放查询结果内存
    DBfree_result(result);

    // 记录函数调用结果日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回结果
    return ret;
}


#ifdef HAVE_MYSQL
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的无符号字符型参数 type，判断并返回相应的字符串字段大小。其中，字符串字段大小可能为 ZBX_SIZE_T_MAX（表示最大值）、65535u（表示固定值）或程序异常退出。
 ******************************************************************************/
// 定义一个名为 get_string_field_size 的静态 size_t 类型函数，接收一个无符号字符型参数 type
static size_t get_string_field_size(unsigned char type)
{
	// 使用 switch 语句根据 type 参数的不同值，进行分支处理
	switch(type)
	{
		// 判断 type 是否为 ZBX_TYPE_LONGTEXT
		case ZBX_TYPE_LONGTEXT:
			// 如果 type 是 ZBX_TYPE_LONGTEXT，则返回 ZBX_SIZE_T_MAX，表示字符串字段大小为最大值
			return ZBX_SIZE_T_MAX;

		// 判断 type 是否为 ZBX_TYPE_CHAR、ZBX_TYPE_TEXT 或 ZBX_TYPE_SHORTTEXT
		case ZBX_TYPE_CHAR:
		case ZBX_TYPE_TEXT:
		case ZBX_TYPE_SHORTTEXT:
			// 如果 type 是 ZBX_TYPE_CHAR、ZBX_TYPE_TEXT 或 ZBX_TYPE_SHORTTEXT，则返回 65535u，表示字符串字段大小为 65535 个字符
			return 65535u;

		// 如果 type 不是以上几种类型，则表示错误情况
		default:
			// 执行 THIS_SHOULD_NEVER_HAPPEN，表示这种情况不应该发生
			THIS_SHOULD_NEVER_HAPPEN;

			// 退出程序，返回 EXIT_FAILURE 状态码，表示程序执行失败
			exit(EXIT_FAILURE);
	}
}

#elif defined(HAVE_ORACLE)
/******************************************************************************
 * *
 *整个代码块的主要目的是根据输入的无符号字符型参数 type 确定字符串字段的大小。根据不同的 type 值，返回相应的字符串长度限制。如果遇到未知类型，输出错误信息并退出程序。
 ******************************************************************************/
// 定义一个名为 get_string_field_size 的静态 size_t 类型函数，接收一个无符号字符型参数 type
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个函数`DBdyn_escape_string_len`，用于根据给定的源字符串`src`和字符串长度`length`，对字符串进行动态转义处理。转义处理后的字符串将被返回。转义处理的方式取决于是否为IBM DB2，如果是，则限制字符串长度为字节数而非字符数。
 ******************************************************************************/
// 定义一个函数，用于将动态生成的字符串进行转义处理
char *DBdyn_escape_string_len(const char *src, size_t length)
{
    // 使用预定义的宏判断是否为IBM DB2，如果是，则限制字符串长度为字节数而非字符数
#if defined(HAVE_IBM_DB2)
    // 使用zbx_db_dyn_escape_string函数进行动态转义处理，参数分别为：
    // src：源字符串
    // length：源字符串长度
    // ZBX_SIZE_T_MAX：表示最大字节数
    // ESCAPE_SEQUENCE_ON：表示开启转义序列
    return zbx_db_dyn_escape_string(src, length, ZBX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
#else
    // 如果不是IBM DB2，则直接进行动态转义处理，参数分别为：
    // src：源字符串
    // ZBX_SIZE_T_MAX：表示最大字节数
    // length：源字符串长度
    // ESCAPE_SEQUENCE_ON：表示开启转义序列
    return zbx_db_dyn_escape_string(src, ZBX_SIZE_T_MAX, length, ESCAPE_SEQUENCE_ON);
#endif
}

			// 返回 ZBX_SIZE_T_MAX，表示字符串长度不受限制
			return ZBX_SIZE_T_MAX;
		// 如果是 ZBX_TYPE_CHAR 或 ZBX_TYPE_SHORTTEXT 类型
		case ZBX_TYPE_CHAR:
		case ZBX_TYPE_SHORTTEXT:
			// 返回 4000u，表示字符串长度最大为 4000 个字符
			return 4000u;
		// 如果是其他未知类型
		default:
			// 输出错误信息，表示不应该发生这种情况
			THIS_SHOULD_NEVER_HAPPEN;
			// 退出程序，表示错误
			exit(EXIT_FAILURE);
	}
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_string_len                                          *
 *                                                                            *
 ******************************************************************************/
char	*DBdyn_escape_string_len(const char *src, size_t length)
{
#if defined(HAVE_IBM_DB2)	/* IBM DB2 fields are limited by bytes rather than characters */
	return zbx_db_dyn_escape_string(src, length, ZBX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON);
#else
	return zbx_db_dyn_escape_string(src, ZBX_SIZE_T_MAX, length, ESCAPE_SEQUENCE_ON);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_string                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为DBdyn_escape_string的函数，用于对传入的字符串进行动态转义处理，并将处理后的字符串返回。该函数内部调用zbx_db_dyn_escape_string函数进行转义处理。
 ******************************************************************************/
// 定义一个C语言函数，名为DBdyn_escape_string，参数为一个const char类型的指针（字符串）
char *DBdyn_escape_string(const char *src)
{
    // 定义一个返回值为char类型的指针，用于存储处理后的字符串
    // 调用名为zbx_db_dyn_escape_string的函数，传入以下参数：
    // 1. 源字符串（const char *src）
/******************************************************************************
 * *
 *代码块主要目的是对给定的字符串进行动态转义，以适应不同数据库的字段要求。根据字段类型和长度，分别采用不同的转义方式。转义后的字符串返回给调用者。
 ******************************************************************************/
/* 定义一个函数，用于动态转义数据库字段中的字符串
 * 参数：
 *   field：字段结构体指针，包含字段类型、长度等信息
 *   src：待转义的字符串
 *   flag：转义序列标志，用于控制转义方式
 * 返回值：
 *   转义后的字符串指针
 */
static char *DBdyn_escape_field_len(const ZBX_FIELD *field, const char *src, zbx_escape_sequence_t flag)
{
	/* 获取字段长度 */
	size_t	length;

	/* 判断字段类型为 LONGTEXT 且长度为 0 的情况 */
	if (ZBX_TYPE_LONGTEXT == field->type && 0 == field->length)
		length = ZBX_SIZE_T_MAX;
	else
		length = field->length;

	/* 根据数据库类型进行动态转义 */
#if defined(HAVE_MYSQL) || defined(HAVE_ORACLE)
	return zbx_db_dyn_escape_string(src, get_string_field_size(field->type), length, flag);
#elif defined(HAVE_IBM_DB2)	/* IBM DB2 字段长度限制为字节数而非字符数 */
	return zbx_db_dyn_escape_string(src, length, ZBX_SIZE_T_MAX, flag);
#else
	return zbx_db_dyn_escape_string(src, ZBX_SIZE_T_MAX, length, flag);
#endif
}


	if (ZBX_TYPE_LONGTEXT == field->type && 0 == field->length)
		length = ZBX_SIZE_T_MAX;
	else
		length = field->length;

#if defined(HAVE_MYSQL) || defined(HAVE_ORACLE)
	return zbx_db_dyn_escape_string(src, get_string_field_size(field->type), length, flag);
#elif defined(HAVE_IBM_DB2)	/* IBM DB2 fields are limited by bytes rather than characters */
	return zbx_db_dyn_escape_string(src, length, ZBX_SIZE_T_MAX, flag);
#else
	return zbx_db_dyn_escape_string(src, ZBX_SIZE_T_MAX, length, flag);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_field                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个函数`DBdyn_escape_field`，用于对给定的表名、字段名和源字符串进行动态转义。首先，检查表名和字段名是否有效，如果无效则报错并退出程序。如果表名和字段名有效，接着调用另一个函数`DBdyn_escape_field_len`对字段值进行动态转义，并返回转义后的字符串。
 ******************************************************************************/
// 定义一个函数，用于对的字段值进行动态转义
/******************************************************************************
 * *
 *```c
 *#include <stdio.h>
 *#include <string.h>
 *
 *// 定义一个C语言函数，名为DBdyn_escape_like_pattern，参数为一个const char类型的指针（字符串），返回值为char类型的指针（字符串）
 *char *DBdyn_escape_like_pattern(const char *src)
 *{
 *    // 声明一个字符数组，用于存储处理后的字符串
 *    char result[256];
 *    
 *    // 调用另一个名为zbx_db_dyn_escape_like_pattern的函数，传入参数src（字符串），并将返回值存储在result数组中
 *    strncpy(result, zbx_db_dyn_escape_like_pattern(src), sizeof(result));
 *    
 *    // 为了防止数组越界，设置result数组的最大长度为255
 *    result[255] = '\\0';
 *    
 *    // 返回处理后的字符串
 *    return result;
 *}
 *
 *int main()
 *{
 *    const char *src = \"Hello, World!\";
 *    char *result = DBdyn_escape_like_pattern(src);
 *    
 *    // 输出结果
 *    printf(\"原始字符串：%s\
 *\", src);
 *    printf(\"处理后的字符串：%s\
 *\", result);
 *    
 *    return 0;
 *}
 *
 *// 另一个名为zbx_db_dyn_escape_like_pattern的函数，用于对字符串进行编码处理
 *```
 *
 *整个代码块的主要目的是定义一个名为DBdyn_escape_like_pattern的函数，该函数用于处理字符串src，并通过调用zbx_db_dyn_escape_like_pattern函数对处理后的字符串进行编码，最后将编码后的字符串作为返回值返回。在main函数中，调用DBdyn_escape_like_pattern函数处理一个示例字符串，并输出原始字符串和处理后的字符串。
 ******************************************************************************/
// 定义一个C语言函数，名为DBdyn_escape_like_pattern，参数为一个const char类型的指针（字符串），返回值为char类型的指针（字符串）
char *DBdyn_escape_like_pattern(const char *src)
{
    // 调用另一个名为zbx_db_dyn_escape_like_pattern的函数，传入参数src（字符串），并将返回值赋给当前函数的返回值
    return zbx_db_dyn_escape_like_pattern(src);
}

// 整个代码块的主要目的是定义一个名为DBdyn_escape_like_pattern的函数，该函数用于处理字符串src，并通过调用zbx_db_dyn_escape_like_pattern函数对处理后的字符串进行编码，最后将编码后的字符串作为返回值返回。

	const ZBX_FIELD	*field;

	// 检查传入的表名和字段名是否为空，如果为空则报错并退出程序
	if (NULL == (table = DBget_table(table_name)) || NULL == (field = DBget_field(table, field_name)))
	{
		// 报错日志
		zabbix_log(LOG_LEVEL_CRIT, "invalid table: \"%s\" field: \"%s\"", table_name, field_name);
		// 退出程序，返回失败
		exit(EXIT_FAILURE);
	}

	// 调用另一个函数，对字段值进行动态转义，并返回转义后的字符串
	return DBdyn_escape_field_len(field, src, ESCAPE_SEQUENCE_ON);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdyn_escape_like_pattern                                        *
 *                                                                            *
 ******************************************************************************/
char	*DBdyn_escape_like_pattern(const char *src)
{
	return zbx_db_dyn_escape_like_pattern(src);
}
/******************************************************************************
 * *
 *这块代码的主要目的是：根据传入的表名（字符串类型）在数组tables中查找对应的表结构（ZBX_TABLE类型），如果找到，则返回该表结构的地址；如果没有找到，则返回NULL。
 *
 *整个注释好的代码块如上所示。
 ******************************************************************************/
const ZBX_TABLE	*DBget_table(const char *tablename)	// 定义一个函数，传入一个字符串参数tablename，返回一个指向ZBX_TABLE类型的指针
{
	int	t;				// 定义一个整型变量t，用于循环计数

	for (t = 0; NULL != tables[t].table; t++)	// 使用一个循环，遍历数组tables
	{
		if (0 == strcmp(tables[t].table, tablename))	// 判断数组tables中的table名称是否与传入的tablename相同
			return &tables[t];	// 如果相同，返回当前table的地址
	}

	return NULL;			// 如果没有找到相同的table，返回NULL
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 DBget_field 的函数，该函数接收一个 ZBX_TABLE 结构体的指针和一个字符串指针作为参数。函数的作用是在给定的 ZBX_TABLE 结构体中查找指定的字段名，如果找到，则返回该字段的指针；如果没有找到，则返回 NULL。
 ******************************************************************************/
/* 定义一个函数 DBget_field，接收两个参数：指向 ZBX_TABLE 结构体的指针 table 和字符串指针 fieldname。
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个ID初始化的功能。程序首先接收用户输入的表名，然后调用`init_ids`函数进行ID初始化。`init_ids`函数的主要逻辑如下：
 *
 *1. 获取表结构。
 *2. 循环查找下一个ID。
 *3. 避免在失败的事务中产生永恒循环。
 *4. 查询下一个ID。
 *5. 如果找不到下一条记录，或者记录中的ID小于等于最小ID，插入新记录。
 *6. 如果找到的ID小于等于最小ID或大于等于最大ID，删除现有记录。
 *7. 更新ID字段。
 *8. 找到下一条记录，如果满足条件，则退出循环。
 *
 *整个代码块的输出结果为：
 *
 *```
 *表名：test，最小ID：1，最大ID：1000000000
 *```
 *
 *这意味着成功地为表`test`初始化了ID。
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 引入DB库
#include "db.h"

// 引入Zabbix日志库
#include "zabbix_log.h"

// 定义日志级别
#define LOG_LEVEL_DEBUG 1

// 定义常量
#define ZBX_FS_UI64 "unsigned int64"

// 函数原型声明
int init_ids(char *tablename, unsigned int64_t *min, unsigned int64_t *max, unsigned int64_t num);

int main()
{
    // 定义变量
    char tablename[256];
    unsigned int64_t min, max, ret1, ret2, found;
    DB_TABLE *table;
    DB_RESULT *result;
    DB_ROW *row;
    int dbres;

    // 输入表名
    printf("请输入表名：");
    fgets(tablename, 256, stdin);

    // 调用函数初始化ID
    if (init_ids(tablename, &min, &max, 1) == 0)
    {
        printf("ID初始化失败，请检查表名和字段名是否正确。\
");
        return 1;
    }

    printf("表名：%s，最小ID：%llu，最大ID：%llu\
", tablename, (long long)min, (long long)max);

    return 0;
}

// 初始化ID函数
int init_ids(char *tablename, unsigned int64_t *min, unsigned int64_t *max, unsigned int64_t num)
{
    // 记录函数调用信息
    const char *__function_name = "init_ids";

    // 获取表结构
    table = DBget_table(tablename);

    // 循环查找下一个ID
    while (found == FAIL)
    {
        // 避免在失败的事务中产生永恒循环
        if (zbx_db_txn_level() > 0 && zbx_db_txn_error() != 0)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "事务失败");
            return 0;
        }

        // 查询下一个ID
        result = DBselect("select nextid from ids where table_name='%s' and field_name='%s'",
                        table->table, table->recid);

        // 如果没有找到下一条记录，或者记录为空
        if (NULL == (row = DBfetch(result)))
        {
            DBfree_result(result);

            // 查询最大ID
            result = DBselect("select max(%s) from %s where %s between " ZBX_FS_UI64 " and " ZBX_FS_UI64,
                             table->recid, table->table, table->recid, min, max);

            // 如果找不到记录，或者记录中的ID小于等于最小ID
            if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
            {
                ret1 = min;
            }
            else
            {
                ZBX_STR2UINT64(ret1, row[0]);
                if (ret1 >= max)
                {
                    zabbix_log(LOG_LEVEL_CRIT, "最大ID超出范围");
                    return 0;
                }
            }

            DBfree_result(result);

            // 插入新记录
            dbres = DBexecute("insert into ids (table_name, field_name, nextid)"
                             " values ('%s', '%s', %llu)",
                            table->table, table->recid, (unsigned long long)ret1);

            if (ZBX_DB_OK > dbres)
            {
                // 处理并发事务中的问题
                DBexecute("update ids set nextid=nextid+1 where table_name='%s' and field_name='%s'",
                        table->table, table->recid);
            }

            continue;
        }
        else
        {
            ZBX_STR2UINT64(ret1, row[0]);
            DBfree_result(result);

            // 如果找到的ID小于等于最小ID或大于等于最大ID
            if (ret1 < min || ret1 >= max)
            {
                DBexecute("delete from ids where table_name='%s' and field_name='%s'",
                        table->table, table->recid);
                continue;
            }

            DBexecute("update ids set nextid=nextid+%d where table_name='%s' and field_name='%s'",
                    num, table->table, table->recid);

            result = DBselect("select nextid from ids where table_name='%s' and field_name='%s'",
                    table->table, table->recid);

            // 如果找到的下一条记录的ID等于当前ID加num
            if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
            {
                ZBX_STR2UINT64(ret2, row[0]);

                if (ret1 + num == ret2)
                    found = SUCCEED;
            }
            else
            {
                THIS_SHOULD_NEVER_HAPPEN;
            }

            DBfree_result(result);
        }
    }

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "ID初始化完成");

    return ret2 - num + 1;
}

				DBexecute("update ids set nextid=nextid+1 where table_name='%s' and field_name='%s'",
						table->table, table->recid);
			}

			continue;
		}
		else
		{
			ZBX_STR2UINT64(ret1, row[0]);
			DBfree_result(result);

			if (ret1 < min || ret1 >= max)
			{
				DBexecute("delete from ids where table_name='%s' and field_name='%s'",
						table->table, table->recid);
				continue;
			}

			DBexecute("update ids set nextid=nextid+%d where table_name='%s' and field_name='%s'",
					num, table->table, table->recid);

			result = DBselect("select nextid from ids where table_name='%s' and field_name='%s'",
					table->table, table->recid);

			if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
			{
				ZBX_STR2UINT64(ret2, row[0]);

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数 `DBget_nextid`，该函数根据传入的表名 `tablename` 和序号 `num` 获取下一个 ID。首先，通过 `if` 语句判断 `tablename` 是否为八个预定义的表名之一，如果是，则调用 `DCget_nextid` 函数获取下一个 ID。否则，直接调用 `DBget_nextid` 函数获取下一个 ID。
 *
 *此外，代码中还定义了一个静态函数 `DBadd_condition_alloc_btw`，用于生成 SQL 查询中的 WHERE 条件部分。该函数接收多个参数，包括 SQL 查询缓冲区、缓冲区大小、当前缓冲区位置、表名、值数组、值数组长度、序列长度数组、序列长度、IN 条件数量、BETWEEN 条件数量等。函数的主要目的是根据给定的值数组生成 WHERE 条件中的 BETWEEN 部分。在这个过程中，首先判断序列长度是否满足最小要求，然后组装 BETWEEN 条件。如果表达式数量超过限制，则分配新的 sql 缓冲区。
 ******************************************************************************/
// 定义一个函数，用于生成 SQL 查询中的 WHERE 条件部分
static void DBadd_condition_alloc_btw(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
                                   const zbx_uint64_t *values, const int num, int **seq_len, int *seq_num, int *in_num, int *between_num)
{
	int		i, len, first, start;
	zbx_uint64_t	value;

	// 存储连续序列的长度到临时数组 'seq_len' 中
	*seq_len = (int *)zbx_malloc(*seq_len, num * sizeof(int));

	for (i = 1, *seq_num = 0, value = values[0], len = 1; i < num; i++)
	{
		if (values[i] != ++value)
		{
			if (MIN_NUM_BETWEEN <= len)
				(*between_num)++;
			else
				*in_num += len;

			(*seq_len)[(*seq_num)++] = len;
			len = 1;
			value = values[i];
		}
		else
			len++;
	}

	if (MIN_NUM_BETWEEN <= len)
		(*between_num)++;
	else
		*in_num += len;

	(*seq_len)[(*seq_num)++] = len;

	// 如果表达式数量超过限制，则分配新的 sql 缓冲区
	if (MAX_EXPRESSIONS < *in_num || 1 < *between_num || (0 < *in_num && 0 < *between_num))
		zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, '(');

	// 组装 "between" 条件
	for (i = 0, first = 1, start = 0; i < *seq_num; i++)
	{
		if (MIN_NUM_BETWEEN <= (*seq_len)[i])
		{
			if (1 != first)
			{
				zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
			}
			else
				first = 0;

			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s between " ZBX_FS_UI64 " and " ZBX_FS_UI64,
					fieldname, values[start], values[start + (*seq_len)[i] - 1]);
		}

		start += (*seq_len)[i];
	}

	// 如果既有 "IN" 条件，又有 "BETWEEN" 条件
	if (0 < *in_num && 0 < *between_num)
	{
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
	}
}

// 函数原型：int* DBget_nextid(const char *tablename, int num)
int* DBget_nextid(const char *tablename, int num)
{
	/* 判断 tablename 是否为以下八个预定义的表名之一，如果是，则执行相应的操作 */
	if (0 == strcmp(tablename, "events") ||
	    0 == strcmp(tablename, "event_tag") ||
	    0 == strcmp(tablename, "problem_tag") ||
	    0 == strcmp(tablename, "dservices") ||
	    0 == strcmp(tablename, "dhosts") ||
	    0 == strcmp(tablename, "alerts") ||
	    0 == strcmp(tablename, "escalations") ||
	    0 == strcmp(tablename, "autoreg_host") ||
	    0 == strcmp(tablename, "event_suppress"))
	{
		/* 使用 DCget_nextid 函数获取下一个 ID */
		return DCget_nextid(tablename, num);
	}

	/* 否则，使用 DBget_nextid 函数获取下一个 ID */
	return DBget_nextid(tablename, num);
}

			{
					zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
			}
			else
				first = 0;

			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s between " ZBX_FS_UI64 " and " ZBX_FS_UI64,
					fieldname, values[start], values[start + (*seq_len)[i] - 1]);
		}

		start += (*seq_len)[i];
	}

	if (0 < *in_num && 0 < *between_num)
	{
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: DBadd_condition_alloc                                            *
 *                                                                            *
 * Purpose: Takes an initial part of SQL query and appends a generated        *
 *          WHERE condition. The WHERE condition is generated from the given  *
/******************************************************************************
 * *
 *该代码块的主要目的是为一个C语言函数（`DBadd_condition_alloc`）添加注释。这个函数用于处理SQL查询中的条件部分，根据不同的数据库类型（ORACLE和SQLITE3）生成相应的查询语句。函数接收多个参数，包括一个指向字符串的指针（`sql`）、一个大小为`sql_alloc`的字符数组、一个指向字符串偏移量的指针（`sql_offset`）、一个字段名（`fieldname`）、一个包含值的字符数组（`values`）以及一个表示值数量的整数（`num`）。
 *
 *注释中详细说明了代码的执行过程，包括以下几个步骤：
 *
 *1. 定义变量：声明了一些用于后续操作的变量，如`start`、`between_num`、`in_num`、`seq_num`以及`seq_len`。
 *
 *2. 判断`num`是否为0，若为0则直接返回。
 *
 *3. 为`sql`分配内存，并填充空格。
 *
 *4. 根据不同的数据库类型处理ORACLE和SQLITE3情况。
 *
 *5. 拼接\"in\"字符串，用于表示多个值的情况。
 *
 *6. 处理ORACLE情况下的特殊情况，如`MIN_NUM_BETWEEN`。
 *
 *7. 释放`seq_len`内存。
 *
 *8. 拼接\")\"字符串，表示查询条件的结束。
 *
 *9. 处理SQLITE3情况，如`MAX_EXPRESSIONS`。
 *
 *10. 结束条件，释放`sql`指向的字符数组。
 *
 *通过这个函数，可以方便地为SQL查询中的条件部分生成合适的语句，从而实现对数据的有效查询。
 ******************************************************************************/
void DBadd_condition_alloc(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
                          const zbx_uint64_t *values, const int num)
{
    // 定义变量
    int start, between_num = 0, in_num = 0, seq_num;
    int *seq_len = NULL;

#if defined(HAVE_SQLITE3)
    int expr_num, expr_cnt = 0;
#endif

    // 判断num是否为0，若为0则直接返回
    if (0 == num)
        return;

    // 为sql分配内存，并填充空格
    zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ' ');

#ifdef HAVE_ORACLE
    // 调用DBadd_condition_alloc_btw函数处理ORACLE情况
    DBadd_condition_alloc_btw(sql, sql_alloc, sql_offset, fieldname, values, num, &seq_len, &seq_num, &in_num,
                             &between_num);

    // 如果in_num大于1，则拼接"in"字符串
    if (1 < in_num)
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s in (", fieldname);

    // 拼接"in"字符串
    for (int i = 0, in_cnt = 0, start = 0; i < seq_num; i++)
    {
        // 处理ORACLE情况下的特殊情况
        if (MIN_NUM_BETWEEN > seq_len[i])
        {
            if (1 == in_num)
            {
                // 拼接字符串
                zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s=" ZBX_FS_UI64, fieldname,
                                  values[start]);
                break;
            }
            else
            {
                // 拼接"in"字符串
                do
                {
                    if (MAX_EXPRESSIONS == in_cnt)
                    {
                        in_cnt = 0;
                        (*sql_offset)--;

                        // 处理ORACLE情况下的特殊情况
                        if (HAVE_ORACLE)
                        {
                            if (MAX_EXPRESSIONS == ++expr_cnt)
                            {
                                zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ")) or (%s in (", fieldname);
                                expr_cnt = 0;
                            }
                            else
                            {
                                zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ") or %s in (", fieldname);
                            }
                        }
                        else
                        {
                            zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s in (", fieldname);
                        }
                    }

                    in_cnt++;
                    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ZBX_FS_UI64 ",",
                                      values[start++]);
                }
                while (0 != --seq_len[i]);
            }
        }
        else
            start += seq_len[i];
    }

    // 释放seq_len内存
    zbx_free(seq_len);

    // 如果in_num大于1，拼接")"字符串
    if (1 < in_num)
    {
        (*sql_offset)--;
        zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');
    }

    // 处理SQLITE3情况
    if (MAX_EXPRESSIONS < expr_num)
        zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

    // 结束条件
    zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');
}

#ifdef HAVE_ORACLE
	if (MAX_EXPRESSIONS < in_num || 1 < between_num || (0 < in_num && 0 < between_num))
#else
	if (MAX_EXPRESSIONS < num)
#endif
		zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

#undef MAX_EXPRESSIONS
#ifdef HAVE_ORACLE
#undef MIN_NUM_BETWEEN
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: DBadd_str_condition_alloc                                        *
 *                                                                            *
 * Purpose: This function is similar to DBadd_condition_alloc(), except it is *
 *          designed for generating WHERE conditions for strings. Hence, this *
 *          function is simpler, because only IN condition is possible.       *
 *                                                                            *
 * Parameters: sql        - [IN/OUT] buffer for SQL query construction        *
 *             sql_alloc  - [IN/OUT] size of the 'sql' buffer                 *
 *             sql_offset - [IN/OUT] current position in the 'sql' buffer     *
 *             fieldname  - [IN] field name to be used in SQL WHERE condition *
 *             values     - [IN] array of string values                       *
 *             num        - [IN] number of elements in 'values' array         *
 *                                                                            *
 * Comments: To support Oracle empty values are checked separately (is null   *
 *           for Oracle and ='' for the other databases).                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的参数，生成一个带有多个参数的 SQL 查询条件。这个函数接收一个指向 SQL 字符串的指针、一个用于分配内存的指针、一个指向偏移量的指针、一个字段名、一个指向值数组的指针以及值的数量。函数首先定义了一些变量，然后根据传入的参数和条件生成 SQL 查询字符串。最后，释放分配的内存。
 ******************************************************************************/
void	DBadd_str_condition_alloc(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *fieldname,
		const char **values, const int num)
{
    // 定义一个常量 MAX_EXPRESSIONS，表示最多允许的 expressions 数量，防止内存溢出
    int	i, cnt = 0;
    char	*value_esc;
    int	values_num = 0, empty_num = 0;

    // 如果传入的 values 数量为 0，直接返回，不进行操作
    if (0 == num)
        return;

    // 为 sql 指针分配一块内存，并填充空格字符
    zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ' ');

    // 遍历 values 数组，统计非空值和空值的个数
    for (i = 0; i < num; i++)
    {
        if ('\0' == *values[i])
            empty_num++;
        else
            values_num++;
    }

    // 如果 values 非空数量大于 MAX_EXPRESSIONS 或者有空值，则在 sql 字符串中添加左括号 '('
    if (MAX_EXPRESSIONS < values_num || (0 != values_num && 0 != empty_num))
        zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, '(');

    // 如果空值的个数不为 0，则添加一个空字段匹配条件
    if (0 != empty_num)
    {
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s" ZBX_SQL_STRCMP, fieldname, ZBX_SQL_STRVAL_EQ(""));

        // 如果非空值的个数为 0，直接返回，不进行后续操作
        if (0 == values_num)
            return;

        zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " or ");
    }

    // 如果非空值的个数为 1，则遍历数组，将每个值添加到 sql 字符串中
    if (1 == values_num)
    {
        for (i = 0; i < num; i++)
        {
            if ('\0' == *values[i])
                continue;

            // 对值进行转义，防止 sql 注入攻击
            value_esc = DBdyn_escape_string(values[i]);
            zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s='%s'", fieldname, value_esc);
            zbx_free(value_esc);
        }

        // 如果空值的个数不为 0，添加右括号 ')'
        if (0 != empty_num)
            zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');
        return;
    }

    // 否则，添加 fieldname 到 sql 字符串中，并添加 " in ("
    zbx_strcpy_alloc(sql, sql_alloc, sql_offset, fieldname);
    zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " in (");

    // 遍历 values 数组，将每个值添加到 sql 字符串中，并添加逗号分隔符
    for (i = 0; i < num; i++)
    {
        if ('\0' == *values[i])
            continue;

        // 如果已经达到了 MAX_EXPRESSIONS，则重新分配内存，并添加 "or " 分隔符
        if (MAX_EXPRESSIONS == cnt)
        {
            cnt = 0;
            (*sql_offset)--;
            zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ") or ");
            zbx_strcpy_alloc(sql, sql_alloc, sql_offset, fieldname);
            zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " in (");
        }

        // 对值进行转义，防止 sql 注入攻击
        value_esc = DBdyn_escape_string(values[i]);
        zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, '\'');
        zbx_strcpy_alloc(sql, sql_alloc, sql_offset, value_esc);
        zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "',");
        zbx_free(value_esc);

        cnt++;
    }

    // 添加右括号 ')'
    (*sql_offset)--;
    zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

    // 如果非空值数量大于 MAX_EXPRESSIONS 或者有空值，添加右括号 ')'
    if (MAX_EXPRESSIONS < values_num || 0 != empty_num)
        zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

    // 释放内存
    #undef MAX_EXPRESSIONS
}


static char	buf_string[640];

/******************************************************************************
 *                                                                            *
 * Function: zbx_host_string                                                  *
 *                                                                            *
 * Return value: <host> or "???" if host not found                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的hostid，查询hosts表中对应的主机名，并将查询结果存储在buf_string中，最后返回buf_string。如果查询结果为空，则返回一个默认的字符串\"???\"。
 ******************************************************************************/
// 定义一个常量字符指针变量zbx_host_string，它接收一个zbx_uint64_t类型的参数hostid
const char *zbx_host_string(zbx_uint64_t hostid)
{
	// 定义一个DB_RESULT类型的变量result，用于存储数据库查询结果
	DB_RESULT	result;
	// 定义一个DB_ROW类型的变量row，用于存储数据库查询到的行数据
	DB_ROW		row;

	// 使用DBselect函数执行SQL查询，从hosts表中获取hostid对应的主机信息
	result = DBselect(
			"select host"
			" from hosts"
			" where hostid=" ZBX_FS_UI64,
			hostid);

	// 判断查询结果是否有效，如果有效则执行以下操作
	if (NULL != (row = DBfetch(result)))
	{
		// 获取查询结果中的第一列数据（即主机名），并将其存储在buf_string中
		zbx_snprintf(buf_string, sizeof(buf_string), "%s", row[0]);
	}
	else
	{
		// 如果没有查询到有效数据，则将buf_string填充为"???"
		zbx_snprintf(buf_string, sizeof(buf_string), "???");
	}

	// 释放查询结果占用的内存
	DBfree_result(result);

	// 返回buf_string字符串，即查询到的主机名
	return buf_string;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_host_key_string                                              *
 *                                                                            *
 * Return value: <host>:<key> or "???" if item not found                      *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的itemid，查询数据库中对应的host和key信息，并将它们拼接成一个字符串返回。具体来说，代码首先执行一个SQL查询，查询条件为hostid等于给定的itemid。如果查询结果有效，则将查询结果中的host和key拼接成一个字符串，并返回。如果没有查询到结果，则返回一个疑问符的字符串。最后，释放查询结果资源。
 ******************************************************************************/
const char *zbx_host_key_string(zbx_uint64_t itemid)
{
	// 定义两个数据库操作结果变量result和row，用于存储查询结果
	DB_RESULT	result;
	DB_ROW		row;

	// 使用DBselect函数执行SQL查询，从数据库中获取host和item的相关信息
	result = DBselect(
			"select h.host,i.key_"
			" from hosts h,items i"
			" where h.hostid=i.hostid"
				" and i.itemid=" ZBX_FS_UI64,
			itemid);

	// 判断查询结果是否有效，如果有效则进行下一步操作
	if (NULL != (row = DBfetch(result)))
	{
		// 拼接host和key的字符串，格式为"host:key"
		zbx_snprintf(buf_string, sizeof(buf_string), "%s:%s", row[0], row[1]);
	}
	else
	{
		// 如果没有查询到结果，则输出一个疑问符的字符串
		zbx_snprintf(buf_string, sizeof(buf_string), "???");
	}

	// 释放查询结果资源
	DBfree_result(result);

	// 返回拼接好的host和key字符串
	return buf_string;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_check_user_permissions                                       *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是检查两个用户（userid和recipient_userid）之间的权限关系。首先，查询recipient_userid对应的用户类型，然后判断userid和recipient_userid是否属于同一用户组。如果满足条件，返回成功（SUCCEED），否则返回失败（FAIL）。
 ******************************************************************************/
int zbx_check_user_permissions(const zbx_uint64_t *userid, const zbx_uint64_t *recipient_userid)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "zbx_check_user_permissions";

	// 定义一个DB_RESULT类型变量，用于存储数据库操作结果
	DB_RESULT	result;
	// 定义一个DB_ROW类型变量，用于存储数据库一行数据
	DB_ROW		row;
	// 定义一个整型变量，表示用户类型，初始值为-1
	int		user_type = -1, ret = SUCCEED;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断recipient_userid是否为空或者userid是否等于recipient_userid，如果满足条件，直接退出函数
	if (NULL == recipient_userid || *userid == *recipient_userid)
		goto out;

	// 查询数据库，获取recipient_userid对应的用户类型
	result = DBselect("select type from users where userid=" ZBX_FS_UI64, *recipient_userid);

	// 如果数据库查询结果中含有有效数据且不为空，则将用户类型保存到user_type变量中
	if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
		user_type = atoi(row[0]);
	// 释放数据库查询结果内存
	DBfree_result(result);

	// 如果user_type仍然为-1，表示无法获取用户类型，记录日志并返回错误
	if (-1 == user_type)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot check permissions", __function_name);
		ret = FAIL;
		goto out;
	}

	// 如果用户类型不是SUPER_ADMIN，则检查用户是否属于同一用户组
	if (USER_TYPE_SUPER_ADMIN != user_type)
	{
		// 查询数据库，检查用户是否属于同一用户组
		result = DBselect(
				"select null"
				" from users_groups ug1"
				" where ug1.userid=" ZBX_FS_UI64
					" and exists (select null"
						" from users_groups ug2"
						" where ug1.usrgrpid=ug2.usrgrpid"
							" and ug2.userid=" ZBX_FS_UI64
					")",
				*userid, *recipient_userid);

		// 如果数据库查询结果为空，表示用户不属于同一用户组，返回失败
		if (NULL == DBfetch(result))
			ret = FAIL;
		// 释放数据库查询结果内存
		DBfree_result(result);
	}
out:
	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

					" and exists (select null"
						" from users_groups ug2"
						" where ug1.usrgrpid=ug2.usrgrpid"
							" and ug2.userid=" ZBX_FS_UI64
					")",
				*userid, *recipient_userid);

		if (NULL == DBfetch(result))
			ret = FAIL;
		DBfree_result(result);
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_user_string                                                  *
 *                                                                            *
 * Return value: "Name Surname (Alias)" or "unknown" if user not found        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是根据用户 ID 查询用户信息（包括姓名、姓氏和别名），并将查询结果拼接成一个字符串返回。如果查询结果为空，则返回一个默认的字符串 \"unknown\"。
 ******************************************************************************/
// 定义一个常量字符指针变量 zbx_user_string，接收一个 zbx_uint64_t 类型的参数 userid
const char *zbx_user_string(zbx_uint64_t userid)
{
	// 声明一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果
	DB_RESULT	result;
	// 声明一个 DB_ROW 类型的变量 row，用于存储数据库查询的一行数据
	DB_ROW		row;

	// 使用 DBselect 函数执行 SQL 查询，查询用户信息，查询条件为 userid=userid
	result = DBselect("select name,surname,alias from users where userid=" ZBX_FS_UI64, userid);

	// 判断查询结果是否不为空，如果不为空，则执行以下操作：
	if (NULL != (row = DBfetch(result)))
	{
		// 使用 zbx_snprintf 函数格式化字符串，将用户名、姓氏和别名拼接在一起，存储在 buf_string 变量中
		zbx_snprintf(buf_string, sizeof(buf_string), "%s %s (%s)", row[0], row[1], row[2]);
	}
	else
	{
		// 如果没有查询到数据，则将 "unknown" 字符串存储在 buf_string 变量中
		zbx_snprintf(buf_string, sizeof(buf_string), "unknown");
	}

	// 释放数据库查询结果
	DBfree_result(result);

	// 返回拼接好的字符串 buf_string
	return buf_string;
}


/******************************************************************************
 *                                                                            *
 * Function: DBsql_id_cmp                                                     *
 *                                                                            *
 * Purpose: construct where condition                                         *
 *                                                                            *
 * Return value: "=<id>" if id not equal zero,                                *
 *               otherwise " is null"                                         *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: NB! Do not use this function more than once in same SQL query    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个 zbx_uint64_t 类型的 id 值是否相等，并返回一个字符串表示比较结果。当 id 为 NULL 时，返回 \" is null\" 字符串。
 ******************************************************************************/
/* 定义一个名为 DBsql_id_cmp 的常量指针函数，接收一个 zbx_uint64_t 类型的参数 id。
* 该函数的主要目的是比较两个 id 值是否相等，返回一个字符串表示比较结果。
* 注释中会详细解释代码的每一行。
*/

const char	*DBsql_id_cmp(zbx_uint64_t id)
{
	/* 定义一个静态字符数组 buf，用于存储比较结果。
	* 数组大小为 22，其中包括：
	* 1 - '=', 用于表示比较操作符
	* 20 - 用于存储 id 值的字符串长度
	* 1 - '\0'，用于表示字符串的结束
	*/
	static char		buf[22];	

	/* 定义一个静态字符数组 is_null，用于表示 id 为 NULL 时的情况。
	* 数组大小为 9，其中包括：
	* 4 - "is nil"（表示 NULL 的英文）
	* 5 - 空格，用于分隔字符串
	*/
	static const char	is_null[9] = " is null";

	/* 判断 id 是否为 0，如果是，则返回 is_null 数组。
	* 这意味着当 id 为 NULL 时，函数返回 " is null" 字符串。
	*/
	if (0 == id)
		return is_null;

	/* 使用 zbx_snprintf 函数将 id 值格式化为字符串，并存储在 buf 数组中。
	* 格式化字符串为 "=id"，其中 id 是 zbx_uint64_t 类型的值。
	* 注意，这里使用了 ZBX_FS_UI64 宏，它可能是一个便于阅读的格式字符串，表示大端序排列的 64 位无符号整数。
	*/
	zbx_snprintf(buf, sizeof(buf), "=" ZBX_FS_UI64, id);

	/* 返回 buf 数组，即 id 值比较结果的字符串表示。
	* 此时 buf 数组中的字符串为 "=id"，表示 id 值相等。
	*/
	return buf;
}

/******************************************************************************
 * *
 *主要目的：这个代码块用于处理自动注册的主机，根据给定的proxy_hostid，更新主机表中的主机元数据。
 *
 *整个代码块的输出：
 *
 *```
 *static void process_autoreg_hosts(zbx_vector_ptr_t *autoreg_hosts, zbx_uint64_t proxy_hostid)
 *{
 *\tDB_RESULT\t\tresult;
 *\tDB_ROW\t\t\trow;
 *\tzbx_vector_str_t\thosts;
 *\tzbx_uint64_t\t\tcurrent_proxy_hostid;
 *\tchar\t\t\t*sql = NULL;
 *\tsize_t\t\t\tsql_alloc = 256, sql_offset;
 *\tzbx_autoreg_host_t\t*autoreg_host;
 *\tint\t\t\ti;
 *
 *\tsql = (char *)zbx_malloc(sql, sql_alloc);
 *\tzbx_vector_str_create(&hosts);
 *
 *\tif (0 != proxy_hostid)
 *\t{
 *\t\tautoreg_get_hosts(autoreg_hosts, &hosts);
 *
 *\t\t/* delete from vector if already exist in hosts table */
 *\t\tsql_offset = 0;
 *\t\tzbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
 *\t\t\t\t\"select h.host,h.hostid,h.proxy_hostid,a.host_metadata\"
 *\t\t\t\t\" from hosts h\"
 *\t\t\t\t\" left join autoreg_host a\"
 *\t\t\t\t\t\" on a.proxy_hostid=h.proxy_hostid and a.host=h.host\"
 *\t\t\t\t\" where\");
 *\t\tDBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, \"h.host\",
 *\t\t\t\t(const char **)hosts.values, hosts.values_num);
 *
 *\t\tresult = DBselect(\"%s\", sql);
 *
 *\t\twhile (NULL != (row = DBfetch(result)))
 *\t\t{
 *\t\t\tfor (i = 0; i < autoreg_hosts->values_num; i++)
 *\t\t\t{
 *\t\t\t\tautoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];
 *
 *\t\t\t\tif (0 != strcmp(autoreg_host->host, row[0]))
 *\t\t\t\t\tcontinue;
 *
 *\t\t\t\tZBX_STR2UINT64(autoreg_host->hostid, row[1]);
 *\t\t\t\tZBX_DBROW2UINT64(current_proxy_hostid, row[2]);
 *
 *\t\t\t\tif (current_proxy_hostid != proxy_hostid || SUCCEED == DBis_null(row[3]) ||
 *\t\t\t\t\t\t0 != strcmp(autoreg_host->host_metadata, row[3]))
 *\t\t\t\t{
 *\t\t\t\t\t/* 移除主机 */
 *\t\t\t\t\tzbx_vector_ptr_remove(autoreg_hosts, i);
 *\t\t\t\t\t/* 释放主机结构体内存 */
 *\t\t\t\t\tautoreg_host_free(autoreg_host);
 *
 *\t\t\t\t\t/* 跳出循环 */
 *\t\t\t\t\tbreak;
 *\t\t\t\t}
 *\t\t\t}
 *\t\t}
 *\t\tDBfree_result(result);
 *
 *\t\thosts.values_num = 0;
 *\t}
 *
 *\tif (0 != autoreg_hosts->values_num)
 *\t{
 *\t\tautoreg_get_hosts(autoreg_hosts, &hosts);
 *
 *\t\t/* 更新主机表中的主机元数据 */
 *\t\tsql_offset = 0;
 *\t\tzbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
 *\t\t\t\t\"select autoreg_hostid,host\"
 *\t\t\t\t\" from autoreg_host\"
 *\t\t\t\t\" where\");
 *\t\tDBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, \"host\",
 *\t\t\t\t(const char **)hosts.values, hosts.values_num);
 *
 *\t\tresult = DBselect(\"%s\", sql);
 *
 *\t\twhile (NULL != (row = DBfetch(result)))
 *\t\t{
 *\t\t\tfor (i = 0; i < autoreg_hosts->values_num; i++)
 *\t\t\t{
 *\t\t\t\tautoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];
 *
 *\t\t\t\t/* 如果主机ID为0且主机名相等，则更新主机ID */
 *\t\t\t\tif (0 == autoreg_host->autoreg_hostid && 0 == strcmp(autoreg_host->host, row[1]))
 *\t\t\t\t{
 *\t\t\t\t\t/* 解析主机ID */
 *\t\t\t\t\tZBX_STR2UINT64(autoreg_host->autoreg_hostid, row[0]);
 *\t\t\t\t\tbreak;
 *\t\t\t\t}
 *\t\t\t}
 *\t\t}
 *\t\tDBfree_result(result);
 *
 *\t\thosts.values_num = 0;
 *\t}
 *
 *\tzbx_vector_str_destroy(&hosts);
 *\tzbx_free(sql);
 *}
 *```
/******************************************************************************
 * 
 ******************************************************************************/
// 定义函数名：DBregister_host_flush
// 函数原型：void DBregister_host_flush(zbx_vector_ptr_t *autoreg_hosts, zbx_uint64_t proxy_hostid)
// 函数作用：将自动注册的主机信息插入或更新到数据库中，并触发相关事件

void	DBregister_host_flush(zbx_vector_ptr_t *autoreg_hosts, zbx_uint64_t proxy_hostid)
{
	// 定义常量字符串，表示函数名
	const char		*__function_name = "DBregister_host_flush";

	// 定义一个zbx_autoreg_host_t结构体指针
	zbx_autoreg_host_t	*autoreg_host;

	// 定义一个zbx_uint64_t类型的变量，用于存储自动注册主机的ID
	zbx_uint64_t		autoreg_hostid;

	// 定义一个zbx_db_insert_t类型的变量，用于存储数据库插入操作的信息
	zbx_db_insert_t		db_insert;

	// 定义一个整型变量，用于循环计数
	int			i, create = 0, update = 0;

	// 定义一个字符串指针，用于存储SQL语句
	char			*sql = NULL;

	// 定义一个字符串指针，用于存储IP、DNS和主机元数据
	char			*ip_esc, *dns_esc, *host_metadata_esc;

	// 定义一个大小为256的字符串缓冲区，用于存储SQL语句
	size_t			sql_alloc = 256;
	size_t			sql_offset = 0;

	// 定义一个zbx_timespec_t类型的变量，用于存储时间戳
	zbx_timespec_t		ts = {0, 0};

	// 打印日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断是否成功获取到活跃自动注册主机的信息，如果没有，则退出函数
	if (SUCCEED != DBregister_host_active())
		goto exit;

	// 处理自动注册主机列表
	process_autoreg_hosts(autoreg_hosts, proxy_hostid);

	// 遍历自动注册主机列表，统计创建和更新的主机数量
	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

		// 如果主机ID为0，表示需要创建新的主机记录
		if (0 == autoreg_host->autoreg_hostid)
			create++;
	}

	// 如果存在需要创建的主机，则执行以下操作：
	if (0 != create)
	{
		// 获取自动注册主机ID的最大值，并加1，作为新创建的主机ID
		autoreg_hostid = DBget_maxid_num("autoreg_host", create);

		// 准备数据库插入操作
		zbx_db_insert_prepare(&db_insert, "autoreg_host", "autoreg_hostid", "proxy_hostid", "host", "listen_ip",
				"listen_dns", "listen_port", "host_metadata", NULL);
	}

	// 如果存在需要更新的主机，则执行以下操作：
	if (0 != (update = autoreg_hosts->values_num - create))
	{
		// 分配内存，用于存储SQL语句
		sql = (char *)zbx_malloc(sql, sql_alloc);
		// 开始执行数据库批量更新操作
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	// 对自动注册主机列表进行排序，方便后续操作
	zbx_vector_ptr_sort(autoreg_hosts, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	// 遍历自动注册主机列表，执行插入或更新操作
	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

		// 如果主机ID为0，表示需要创建新的主机记录
		if (0 == autoreg_host->autoreg_hostid)
		{
			// 为主机分配ID，并加1
			autoreg_host->autoreg_hostid = autoreg_hostid++;

			// 准备插入主机记录的SQL语句
			zbx_db_insert_add_values(&db_insert, autoreg_host->autoreg_hostid, proxy_hostid,
					autoreg_host->host, autoreg_host->ip, autoreg_host->dns,
					(int)autoreg_host->port, autoreg_host->host_metadata);
		}
		else
		{
			// 转义IP、DNS和主机元数据，以便插入数据库
			ip_esc = DBdyn_escape_string(autoreg_host->ip);
			dns_esc = DBdyn_escape_string(autoreg_host->dns);
			host_metadata_esc = DBdyn_escape_string(autoreg_host->host_metadata);

			// 构建更新主机记录的SQL语句
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update autoreg_host"
					" set listen_ip='%s',"
						"listen_dns='%s',"
						"listen_port=%hu,"
						"host_metadata='%s',"
						"proxy_hostid=%s"
					" where autoreg_hostid=" ZBX_FS_UI64 ";\
",
				ip_esc, dns_esc, autoreg_host->port, host_metadata_esc, DBsql_id_ins(proxy_hostid),
				autoreg_host->autoreg_hostid);

			// 释放内存
			zbx_free(host_metadata_esc);
			zbx_free(dns_esc);
			zbx_free(ip_esc);
		}
	}

	// 如果存在需要创建的主机，则执行以下操作：
	if (0 != create)
	{
		// 执行插入操作
		zbx_db_insert_execute(&db_insert);
		// 清理插入操作的相关信息
		zbx_db_insert_clean(&db_insert);
	}

	// 如果存在需要更新的主机，则执行以下操作：
	if (0 != update)
	{
		// 结束批量更新操作
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		// 执行更新操作
		DBexecute("%s", sql);

		// 释放内存
		zbx_free(sql);
	}

	// 对自动注册主机列表进行排序，方便后续操作
	zbx_vector_ptr_sort(autoreg_hosts, compare_autoreg_host_by_hostid);

	// 遍历自动注册主机列表，执行相关操作
	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

		// 设置主机状态，并触发相关事件
		ts.sec = autoreg_host->now;
		zbx_add_event(EVENT_SOURCE_AUTO_REGISTRATION, EVENT_OBJECT_ZABBIX_ACTIVE, autoreg_host->autoreg_hostid,
				&ts, TRIGGER_VALUE_PROBLEM, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
	}

	// 处理事件
	zbx_process_events(NULL, NULL);
	// 清理事件
	zbx_clean_events();

exit:
	// 打印日志，表示退出函数
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		DBfree_result(result);

		/* 清理主机列表 */
		hosts.values_num = 0;
	}

	/* 释放内存并清理 */
	zbx_vector_str_destroy(&hosts);
	zbx_free(sql);
}

// 定义一个静态函数，用于释放zbx_autoreg_host_t结构体中的内存空间
static void autoreg_host_free(zbx_autoreg_host_t *autoreg_host)
{
    // 释放host变量所占用的内存空间
/******************************************************************************
 * *
 *整个代码块的主要目的是：注册主机信息。通过遍历autoreg_hosts数组检查主机名是否已存在，若不存在则分配内存创建一个新的zbx_autoreg_host_t结构体，并将其添加到autoreg_hosts数组中。
 ******************************************************************************/
// 定义一个函数，用于注册主机信息
void DBregister_host_prepare(zbx_vector_ptr_t *autoreg_hosts, const char *host, const char *ip, const char *dns,
                           unsigned short port, const char *host_metadata, int now)
{
    // 定义一个指向zbx_autoreg_host_t结构体的指针
    zbx_autoreg_host_t *autoreg_host;
    int 			i;

    // 遍历autoreg_hosts数组，检查主机名是否已存在（去重）
    for (i = 0; i < autoreg_hosts->values_num; i++)
    {
        // 获取autoreg_hosts数组中的第i个元素
        autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

        // 判断主机名是否与传入的主机名相同，如果相同则删除该元素
        if (0 == strcmp(host, autoreg_host->host))
        {
            zbx_vector_ptr_remove(autoreg_hosts, i);
            autoreg_host_free(autoreg_host);
            break;
        }
    }

    // 分配内存，创建一个新的zbx_autoreg_host_t结构体
    autoreg_host = (zbx_autoreg_host_t *)zbx_malloc(NULL, sizeof(zbx_autoreg_host_t));
    // 初始化zbx_autoreg_host_t结构体的成员变量
    autoreg_host->autoreg_hostid = autoreg_host->hostid = 0;
    autoreg_host->host = zbx_strdup(NULL, host);
    autoreg_host->ip = zbx_strdup(NULL, ip);
    autoreg_host->dns = zbx_strdup(NULL, dns);
    autoreg_host->port = port;
    autoreg_host->host_metadata = zbx_strdup(NULL, host_metadata);
    autoreg_host->now = now;

    // 将新创建的zbx_autoreg_host_t结构体添加到autoreg_hosts数组中
    zbx_vector_ptr_append(autoreg_hosts, autoreg_host);
}

			break;
		}
	}

	autoreg_host = (zbx_autoreg_host_t *)zbx_malloc(NULL, sizeof(zbx_autoreg_host_t));
	autoreg_host->autoreg_hostid = autoreg_host->hostid = 0;
	autoreg_host->host = zbx_strdup(NULL, host);
	autoreg_host->ip = zbx_strdup(NULL, ip);
	autoreg_host->dns = zbx_strdup(NULL, dns);
	autoreg_host->port = port;
	autoreg_host->host_metadata = zbx_strdup(NULL, host_metadata);
	autoreg_host->now = now;

	zbx_vector_ptr_append(autoreg_hosts, autoreg_host);
}

/******************************************************************************
 * *
 *这块代码的主要目的是从自动注册的主机向量（autoreg_hosts）中获取所有主机名，并将这些主机名添加到另一个主机向量（hosts）中。函数采用循环遍历 autoreg_hosts 中的每个元素，然后将对应元素的主机名添加到 hosts 向量中。整个代码块的功能可以简单总结为：从一个向量中提取主机名，并将这些主机名添加到另一个向量中。
 ******************************************************************************/
// 定义一个静态函数，用于获取自动注册的主机
static void autoreg_get_hosts(zbx_vector_ptr_t *autoreg_hosts, zbx_vector_str_t *hosts)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 使用 for 循环遍历 autoreg_hosts 中的每个元素
	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		// 获取 autoreg_hosts 中的第 i 个元素，类型为 zbx_autoreg_host_t
		zbx_autoreg_host_t *autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

		// 将 autoreg_host 中的主机名添加到 hosts 向量中
		zbx_vector_str_append(hosts, autoreg_host->host);
	}
}


static void	process_autoreg_hosts(zbx_vector_ptr_t *autoreg_hosts, zbx_uint64_t proxy_hostid)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_str_t	hosts;
	zbx_uint64_t		current_proxy_hostid;
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset;
	zbx_autoreg_host_t	*autoreg_host;
	int			i;

	sql = (char *)zbx_malloc(sql, sql_alloc);
	zbx_vector_str_create(&hosts);

	if (0 != proxy_hostid)
	{
		autoreg_get_hosts(autoreg_hosts, &hosts);

		/* delete from vector if already exist in hosts table */
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select h.host,h.hostid,h.proxy_hostid,a.host_metadata"
				" from hosts h"
				" left join autoreg_host a"
					" on a.proxy_hostid=h.proxy_hostid and a.host=h.host"
				" where");
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "h.host",
				(const char **)hosts.values, hosts.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < autoreg_hosts->values_num; i++)
			{
				autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

				if (0 != strcmp(autoreg_host->host, row[0]))
					continue;

				ZBX_STR2UINT64(autoreg_host->hostid, row[1]);
				ZBX_DBROW2UINT64(current_proxy_hostid, row[2]);

				if (current_proxy_hostid != proxy_hostid || SUCCEED == DBis_null(row[3]) ||
						0 != strcmp(autoreg_host->host_metadata, row[3]))
				{
					break;
				}

				zbx_vector_ptr_remove(autoreg_hosts, i);
				autoreg_host_free(autoreg_host);

				break;
			}

		}
		DBfree_result(result);

		hosts.values_num = 0;
	}

	if (0 != autoreg_hosts->values_num)
	{
		autoreg_get_hosts(autoreg_hosts, &hosts);

		/* update autoreg_id in vector if already exists in autoreg_host table */
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select autoreg_hostid,host"
				" from autoreg_host"
				" where");
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "host",
				(const char **)hosts.values, hosts.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < autoreg_hosts->values_num; i++)
			{
				autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

				if (0 == autoreg_host->autoreg_hostid && 0 == strcmp(autoreg_host->host, row[1]))
				{
					ZBX_STR2UINT64(autoreg_host->autoreg_hostid, row[0]);
					break;
				}
			}
		}
		DBfree_result(result);

		hosts.values_num = 0;
	}

	zbx_vector_str_destroy(&hosts);
	zbx_free(sql);
}

/******************************************************************************
 * *
 *这块代码的主要目的是比较两个自动注册的主机对象（zbx_autoreg_host_t 结构体）的唯一标识符（hostid）是否相等。如果相等，返回0，表示比较成功；如果不相等，返回1，表示比较失败。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个自动注册的主机对象（zbx_autoreg_host_t 结构体）
static int	compare_autoreg_host_by_hostid(const void *d1, const void *d2)
{
	// 解指针，获取两个主机对象的指针
	const zbx_autoreg_host_t	*p1 = *(const zbx_autoreg_host_t **)d1;
	const zbx_autoreg_host_t	*p2 = *(const zbx_autoreg_host_t **)d2;

	// 判断两个主机对象的唯一标识符（hostid）是否相等，如果不相等，返回1，表示比较失败
	ZBX_RETURN_IF_NOT_EQUAL(p1->hostid, p2->hostid);

	// 如果两个主机对象的 hostid 相等，返回0，表示比较成功
	return 0;
}


void	DBregister_host_flush(zbx_vector_ptr_t *autoreg_hosts, zbx_uint64_t proxy_hostid)
{
	const char		*__function_name = "DBregister_host_flush";

	zbx_autoreg_host_t	*autoreg_host;
	zbx_uint64_t		autoreg_hostid;
	zbx_db_insert_t		db_insert;
	int			i, create = 0, update = 0;
	char			*sql = NULL, *ip_esc, *dns_esc, *host_metadata_esc;
	size_t			sql_alloc = 256, sql_offset = 0;
	zbx_timespec_t		ts = {0, 0};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (SUCCEED != DBregister_host_active())
		goto exit;

	process_autoreg_hosts(autoreg_hosts, proxy_hostid);

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

		if (0 == autoreg_host->autoreg_hostid)
			create++;
	}

	if (0 != create)
	{
		autoreg_hostid = DBget_maxid_num("autoreg_host", create);

		zbx_db_insert_prepare(&db_insert, "autoreg_host", "autoreg_hostid", "proxy_hostid", "host", "listen_ip",
				"listen_dns", "listen_port", "host_metadata", NULL);
	}

	if (0 != (update = autoreg_hosts->values_num - create))
	{
		sql = (char *)zbx_malloc(sql, sql_alloc);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	zbx_vector_ptr_sort(autoreg_hosts, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

		if (0 == autoreg_host->autoreg_hostid)
		{
			autoreg_host->autoreg_hostid = autoreg_hostid++;

			zbx_db_insert_add_values(&db_insert, autoreg_host->autoreg_hostid, proxy_hostid,
					autoreg_host->host, autoreg_host->ip, autoreg_host->dns,
					(int)autoreg_host->port, autoreg_host->host_metadata);
		}
		else
		{
			ip_esc = DBdyn_escape_string(autoreg_host->ip);
			dns_esc = DBdyn_escape_string(autoreg_host->dns);
			host_metadata_esc = DBdyn_escape_string(autoreg_host->host_metadata);

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update autoreg_host"
					" set listen_ip='%s',"
						"listen_dns='%s',"
						"listen_port=%hu,"
						"host_metadata='%s',"
						"proxy_hostid=%s"
					" where autoreg_hostid=" ZBX_FS_UI64 ";\n",
				ip_esc, dns_esc, autoreg_host->port, host_metadata_esc, DBsql_id_ins(proxy_hostid),
				autoreg_host->autoreg_hostid);

			zbx_free(host_metadata_esc);
			zbx_free(dns_esc);
			zbx_free(ip_esc);
		}
	}

	if (0 != create)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	if (0 != update)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		DBexecute("%s", sql);
		zbx_free(sql);
	}

	zbx_vector_ptr_sort(autoreg_hosts, compare_autoreg_host_by_hostid);

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		autoreg_host = (zbx_autoreg_host_t *)autoreg_hosts->values[i];

		ts.sec = autoreg_host->now;
		zbx_add_event(EVENT_SOURCE_AUTO_REGISTRATION, EVENT_OBJECT_ZABBIX_ACTIVE, autoreg_host->autoreg_hostid,
				&ts, TRIGGER_VALUE_PROBLEM, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
	}

	zbx_process_events(NULL, NULL);
	zbx_clean_events();
exit:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 * *
 *这块代码的主要目的是清理自动注册的主机信息。函数接收一个指向 zbx_vector_ptr_t 类型的指针，该指针指向一个存储自动注册主机信息的 vector。通过调用 zbx_vector_ptr_clear_ext 函数，清理 vector 中的所有元素。清理过程中，使用 zbx_mem_free_func_t 类型指针调用 autoreg_host_free 函数来释放内存。
 ******************************************************************************/
// 定义一个名为 DBregister_host_clean 的函数，参数是一个指向 zbx_vector_ptr_t 类型的指针 autoreg_hosts。
void DBregister_host_clean(zbx_vector_ptr_t *autoreg_hosts)
{
    // 使用 zbx_vector_ptr_clear_ext 函数清理 autoreg_hosts 指向的 vector 中的元素。
    // zbx_mem_free_func_t 类型是一个函数指针，这里将其指向 autoreg_host_free 函数，用于释放内存。
    zbx_vector_ptr_clear_ext(autoreg_hosts, (zbx_mem_free_func_t)autoreg_host_free);
}


/******************************************************************************
 *                                                                            *
 * Function: DBproxy_register_host                                            *
 *                                                                            *
 * Purpose: register unknown host                                             *
 *                                                                            *
 * Parameters: host - host name                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将代理主机的信息（包括主机名、IP地址、DNS、端口和主机元数据）插入到数据库中。首先，使用 DBdyn_escape_field 函数对输入的字符串进行转义。然后，使用 DBexecute 函数执行插入操作，将转义后的字符串插入到数据库中。最后，释放内存。
 ******************************************************************************/
// 定义一个函数 void DBproxy_register_host，这个函数的主要目的是用于注册代理主机
void	DBproxy_register_host(const char *host, const char *ip, const char *dns, unsigned short port,
                            const char *host_metadata)
{
    // 定义一些字符指针，用于存储转义后的字符串
    char	*host_esc, *ip_esc, *dns_esc, *host_metadata_esc;

    // 使用 DBdyn_escape_field 函数对输入的字符串进行转义，分别为 host、ip、dns 和 host_metadata
    host_esc = DBdyn_escape_field("proxy_autoreg_host", "host", host);
    ip_esc = DBdyn_escape_field("proxy_autoreg_host", "listen_ip", ip);
    dns_esc = DBdyn_escape_field("proxy_autoreg_host", "listen_dns", dns);
    host_metadata_esc = DBdyn_escape_field("proxy_autoreg_host", "host_metadata", host_metadata);

    // 使用 DBexecute 函数执行插入操作，将转义后的字符串插入到数据库中
    DBexecute("insert into proxy_autoreg_host"
             " (clock,host,listen_ip,listen_dns,listen_port,host_metadata)"
             " values"
             " (%d,'%s','%s','%s',%d,'%s')",
             (int)time(NULL), host_esc, ip_esc, dns_esc, (int)port, host_metadata_esc);

/******************************************************************************
 * *
 *整个代码块的主要目的是处理SQL语句执行过程中的 overflow 问题。具体来说，当SQL语句长度超过预设的最大长度时，该函数会被调用。函数首先检查最后一个字符是否为逗号，如果是，则减少sql_offset的值，并在内存分配的sql数组末尾添加一个分号和换行符。接下来，根据编译时是否启用了HAVE_ORACLE宏，对SQL语句进行处理，去掉末尾的换行符和空格，以避免Oracle在遇到分号时报错。然后调用DBexecute函数执行SQL语句，并在执行完成后，重置sql_offset为0，开始下一轮多条更新操作。最后，返回执行结果。
 ******************************************************************************/
// 定义一个函数int DBexecute_overflowed_sql，接收三个参数：char **sql（字符指针指针，指向SQL语句），size_t *sql_alloc（SQL语句分配大小），size_t *sql_offset（指向当前执行的SQL语句位置）
int	DBexecute_overflowed_sql(char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	// 定义一个int类型的变量ret，初始值为SUCCEED（0）
	int	ret = SUCCEED;

	// 判断当前的sql_offset是否大于ZBX_MAX_OVERFLOW_SQL_SIZE
	if (ZBX_MAX_OVERFLOW_SQL_SIZE < *sql_offset)
	{
		// 定义一个宏HAVE_MULTIROW_INSERT，如果在编译时启用，则以下代码段生效
		// 判断最后一个字符是否为逗号，如果是，则减少sql_offset的值，并在内存分配的sql数组末尾添加一个分号和换行符
		// 注意：这里使用了zbx_strcpy_alloc函数，它分配了新的内存并复制了原字符串，但是这里并没有释放原内存，可能是在后续代码中会处理
		#ifdef HAVE_MULTIROW_INSERT
		if (',' == (*sql)[*sql_offset - 1])
		{
			(*sql_offset)--;
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\
");
		}
		#endif
/******************************************************************************
 * *
 *这块代码的主要目的是根据给定的主机名示例，查找符合条件的主机名，并构造一个唯一的主机名返回。代码首先定义了一些变量，然后执行 SQL 查询寻找符合条件的主机名。接着对查询结果进行处理，找到符合条件且长度最小的数字，并构造唯一的主机名返回。整个过程中，代码还使用了日志记录和内存管理功能，确保程序的稳定运行。
 ******************************************************************************/
// 定义一个函数，输入一个主机名示例，输出一个唯一的主机名
char *DBget_unique_hostname_by_sample(const char *host_name_sample)
{
	// 定义一些变量
	const char		*__function_name = "DBget_unique_hostname_by_sample";
	DB_RESULT		result;
	DB_ROW			row;
	int			full_match = 0, i;
	char			*host_name_temp = NULL, *host_name_sample_esc;
	zbx_vector_uint64_t	nums;
	zbx_uint64_t		num = 2;	/* 产生替代方案，从 "2" 开始 */
	size_t			sz;

	// 确保输入的主机名示例不为空
	assert(host_name_sample && *host_name_sample);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() sample:'%s'", __function_name, host_name_sample);

	// 创建一个 uint64 类型的 vector
	zbx_vector_uint64_create(&nums);
	// 为 vector 预留空间
	zbx_vector_uint64_reserve(&nums, 8);

	// 计算主机名示例的长度
	sz = strlen(host_name_sample);
	// 转义主机名示例中的特殊字符
	host_name_sample_esc = DBdyn_escape_like_pattern(host_name_sample);

	// 执行 SQL 查询，寻找符合条件的主机名
	result = DBselect(
			"select host"
			" from hosts"
			" where host like '%s%%' escape '%c'"
				" and flags<>%d"
				" and status in (%d,%d,%d)",
			host_name_sample_esc, ZBX_SQL_LIKE_ESCAPE_CHAR,
			ZBX_FLAG_DISCOVERY_PROTOTYPE,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, HOST_STATUS_TEMPLATE);

	// 释放 host_name_sample_esc 内存
	zbx_free(host_name_sample_esc);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	n;
		const char	*p;

		// 如果主机名长度不一致，跳过此次循环
		if (0 != strncmp(row[0], host_name_sample, sz))
			continue;

		// 查找 "_" 符号后面的数字
		p = row[0] + sz;

		// 如果不是数字，跳过此次循环
		if ('\0' == *p)
			continue;

		// 如果是数字，将其添加到 vector 中
		if ('_' != *p || FAIL == is_uint64(p + 1, &n))
			continue;

		zbx_vector_uint64_append(&nums, n);
	}
	// 释放 result 内存
	DBfree_result(result);

	// 对 vector 进行排序
	zbx_vector_uint64_sort(&nums, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 如果未找到完全匹配的主机名，直接返回原始主机名
	if (0 == full_match)
	{
		host_name_temp = zbx_strdup(host_name_temp, host_name_sample);
		goto clean;
	}

	// 寻找最小的符合条件的数字
	for (i = 0; i < nums.values_num; i++)
	{
		if (num > nums.values[i])
			continue;

		// 如果找到了，跳出循环
		if (num < nums.values[i])
			break;

		num++;
	}

	// 构造唯一的主机名
	host_name_temp = zbx_dsprintf(host_name_temp, "%s_" ZBX_FS_UI64, host_name_sample, num);
clean:
	// 释放 vector 内存
	zbx_vector_uint64_destroy(&nums);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __function_name, host_name_temp);

	// 返回唯一的主机名
	return host_name_temp;
}


	zbx_vector_uint64_create(&nums);
	zbx_vector_uint64_reserve(&nums, 8);

	sz = strlen(host_name_sample);
	host_name_sample_esc = DBdyn_escape_like_pattern(host_name_sample);

	result = DBselect(
			"select host"
			" from hosts"
			" where host like '%s%%' escape '%c'"
				" and flags<>%d"
				" and status in (%d,%d,%d)",
			host_name_sample_esc, ZBX_SQL_LIKE_ESCAPE_CHAR,
			ZBX_FLAG_DISCOVERY_PROTOTYPE,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, HOST_STATUS_TEMPLATE);

	zbx_free(host_name_sample_esc);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	n;
		const char	*p;

		if (0 != strncmp(row[0], host_name_sample, sz))
			continue;

		p = row[0] + sz;

		if ('\0' == *p)
		{
			full_match = 1;
			continue;
		}

		if ('_' != *p || FAIL == is_uint64(p + 1, &n))
			continue;

		zbx_vector_uint64_append(&nums, n);
	}
	DBfree_result(result);

	zbx_vector_uint64_sort(&nums, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	if (0 == full_match)
	{
		host_name_temp = zbx_strdup(host_name_temp, host_name_sample);
		goto clean;
	}

	for (i = 0; i < nums.values_num; i++)
	{
		if (num > nums.values[i])
			continue;

		if (num < nums.values[i])	/* found, all other will be bigger */
			break;

		num++;
	}

	host_name_temp = zbx_dsprintf(host_name_temp, "%s_" ZBX_FS_UI64, host_name_sample, num);
clean:
	zbx_vector_uint64_destroy(&nums);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __function_name, host_name_temp);

	return host_name_temp;
}

/******************************************************************************
 *                                                                            *
 * Function: DBsql_id_ins                                                     *
 *                                                                            *
 * Purpose: construct insert statement                                        *
 *                                                                            *
 * Return value: "<id>" if id not equal zero,                                 *
 *               otherwise "null"                                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：接收一个 zbx_uint64_t 类型的参数 id，将其转换为字符串，并返回。如果 id 为空值（0），则直接返回空字符串 null。
 *
 *代码注释详细说明：
 *1. 定义静态变量 n，用于记录当前是第几组缓冲区。
 *2. 定义静态数组 buf，用于存储生成的字符串。数组大小为 4，每个元素长度为 20，额外预留一个字节用于 '\\0' 结尾。
 *3. 定义静态常量字符串 null，用于表示空值。
 *4. 如果 id 为 0，表示空值，直接返回 null。
 *5. 计算 n 的值，使其在 0、1、2、3 之间循环变化。
 *6. 使用 zbx_snprintf 格式化字符串，将 id 转换为字符串并存储在 buf[n] 中。
 *7. 返回生成的字符串 buf[n]。
 ******************************************************************************/
const char *DBsql_id_ins(zbx_uint64_t id)
{
	// 定义一个静态变量 n，用于记录当前是第几组缓冲区
	static unsigned char	n = 0;

	// 定义一个静态数组 buf，用于存储生成的字符串
	static char		buf[4][21];	/* 20 - value size, 1 - '\0' */

	// 定义一个静态常量字符串 null，用于表示空值
	static const char	null[5] = "null";

	// 如果 id 为 0，表示空值，直接返回 null
	if (0 == id)
		return null;

	// 计算 n 的值，使其在 0、1、2、3 之间循环变化
	n = (n + 1) & 3;

	// 使用 zbx_snprintf 格式化字符串，将 id 转换为字符串并存储在 buf[n] 中
	zbx_snprintf(buf[n], sizeof(buf[n]), ZBX_FS_UI64, id);

	// 返回生成的字符串 buf[n]
	return buf[n];
}


/******************************************************************************
 *                                                                            *
 * Function: DBget_inventory_field                                            *
 *                                                                            *
 * Purpose: get corresponding host_inventory field name                       *
 *                                                                            *
 * Parameters: inventory_link - [IN] field link 1..HOST_INVENTORY_FIELD_COUNT *
 *                                                                            *
 * Return value: field name or NULL if value of inventory_link is incorrect   *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是提供一个函数`DBget_inventory_field`，接收一个无符号字符型参数`inventory_link`，根据该参数获取对应的库存字段名称。如果传入的参数非法，函数返回NULL。库存字段名称存储在一个静态常量字符指针数组中，数组下标从1开始，因为0号元素作为数组长度使用。
 ******************************************************************************/
/* 定义一个常量字符指针数组，存储库存字段名称
 * 数组下标从1开始，因为0号元素作为数组长度使用
 */
const char *DBget_inventory_field(unsigned char inventory_link)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的表名（table_name）在数据库中是否存在。根据不同的数据库类型（DB2、MySQL、Oracle、POSTGRESQL、SQLite3），执行相应的查询语句来判断表名是否存在。如果表名存在，则返回SUCCEED，否则返回FAIL。在这个过程中，还对表名进行了转义处理，以防止SQL注入攻击。
 ******************************************************************************/
int	DBtable_exists(const char *table_name)
{
	// 定义一个字符串指针变量table_name_esc，用于存储table_name转义后的结果
	char		*table_name_esc;

	// 定义一个符号常量，表示是否支持POSTGRESQL
	#ifdef HAVE_POSTGRESQL
	char		*table_schema_esc;
	// 如果支持POSTGRESQL，则继续执行以下代码
	#endif

	// 定义一个DB_RESULT类型的变量result，用于存储数据库查询结果
	DB_RESULT	result;
	// 定义一个int类型的变量ret，用于存储最终返回的结果
	int		ret;

	// 使用DBdyn_escape_string()函数将table_name进行转义，并将结果存储在table_name_esc指向的字符串中
	table_name_esc = DBdyn_escape_string(table_name);

	// 根据不同的数据库类型，执行相应的查询语句
	#if defined(HAVE_IBM_DB2)
		// 针对DB2数据库的特定查询语句
		result = DBselect(
			"select 1"
			" from syscat.tables"
			" where tabschema=user"
				" and lower(tabname)='%s'",
			table_name_esc);
	#elif defined(HAVE_MYSQL)
		// 针对MySQL数据库的特定查询语句
		result = DBselect("show tables like '%s'", table_name_esc);
	#elif defined(HAVE_ORACLE)
		// 针对Oracle数据库的特定查询语句
		result = DBselect(
			"select 1"
			" from tab"
			" where tabtype='TABLE'"
				" and lower(tname)='%s'",
			table_name_esc);
	#elif defined(HAVE_POSTGRESQL)
		// 针对POSTGRESQL数据库的特定查询语句
		table_schema_esc = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ?
			"public" : CONFIG_DBSCHEMA);

		result = DBselect(
			"select 1"
			" from information_schema.tables"
			" where table_name='%s'"
				" and table_schema='%s'",
			table_name_esc, table_schema_esc);

		// 释放table_schema_esc内存
		zbx_free(table_schema_esc);
	#elif defined(HAVE_SQLITE3)
		// 针对SQLite3数据库的特定查询语句
		result = DBselect(
			"select 1"
			" from sqlite_master"
			" where tbl_name='%s'"
				" and type='table'",
			table_name_esc);
	#endif

	// 释放table_name_esc内存
	zbx_free(table_name_esc);

	// 判断result是否为空，若为空则返回FAIL，否则返回SUCCEED
	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	// 释放result所占用的数据库资源
	DBfree_result(result);

	// 返回ret，表示表格是否存在
	return ret;
}

	/* publib.boulder.ibm.com/infocenter/db2luw/v9r7/topic/com.ibm.db2.luw.admin.cmd.doc/doc/r0001967.html */
	result = DBselect(
			"select 1"
			" from syscat.tables"
			" where tabschema=user"
				" and lower(tabname)='%s'",
			table_name_esc);
#elif defined(HAVE_MYSQL)
	result = DBselect("show tables like '%s'", table_name_esc);
#elif defined(HAVE_ORACLE)
	result = DBselect(
			"select 1"
			" from tab"
			" where tabtype='TABLE'"
				" and lower(tname)='%s'",
			table_name_esc);
#elif defined(HAVE_POSTGRESQL)
	table_schema_esc = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ?
			"public" : CONFIG_DBSCHEMA);

	result = DBselect(
			"select 1"
			" from information_schema.tables"
			" where table_name='%s'"
				" and table_schema='%s'",
			table_name_esc, table_schema_esc);

	zbx_free(table_schema_esc);

/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的表名和字段名是否存在于数据库中。根据不同的数据库类型，执行相应的查询语句，判断查询结果是否有数据。如果没有数据，则返回FAIL，否则返回SUCCEED。
 ******************************************************************************/
int	DBfield_exists(const char *table_name, const char *field_name)
{
	// 定义一个DB_RESULT类型的变量result，用于存储查询结果
	DB_RESULT	result;

	// 根据不同的数据库类型，分别进行不同的处理
#if defined(HAVE_IBM_DB2)
	char		*table_name_esc, *field_name_esc;
	int		ret;
#elif defined(HAVE_MYSQL)
	char		*field_name_esc;
	int		ret;
#elif defined(HAVE_ORACLE)
	char		*table_name_esc, *field_name_esc;
	int		ret;
#elif defined(HAVE_POSTGRESQL)
	char		*table_name_esc, *field_name_esc, *table_schema_esc;
	int		ret;
#elif defined(HAVE_SQLITE3)
	char		*table_name_esc;
	DB_ROW		row;
	int		ret = FAIL;
#endif

	// 对表名和字段名进行转义，防止SQL注入攻击
#if defined(HAVE_IBM_DB2) || defined(HAVE_MYSQL) || defined(HAVE_ORACLE)
	table_name_esc = DBdyn_escape_string(table_name);
	field_name_esc = DBdyn_escape_string(field_name);
#endif

	// 根据不同数据库类型，执行相应的查询语句
#if defined(HAVE_IBM_DB2)
	result = DBselect(
			"select 1"
			" from syscat.columns"
			" where tabschema=user"
				" and lower(tabname)='%s'"
				" and lower(colname)='%s'",
			table_name_esc, field_name_esc);
#elif defined(HAVE_MYSQL)
	result = DBselect("show columns from %s like '%s'",
			table_name, field_name_esc);
#elif defined(HAVE_ORACLE)
	result = DBselect(
			"select 1"
			" from col"
			" where lower(tname)='%s'"
				" and lower(cname)='%s'",
			table_name_esc, field_name_esc);
#elif defined(HAVE_POSTGRESQL)
	table_schema_esc = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ?
			"public" : CONFIG_DBSCHEMA);
	result = DBselect(
			"select 1"
			" from information_schema.columns"
			" where table_name='%s'"
				" and column_name='%s'"
				" and table_schema='%s'",
			table_name_esc, field_name_esc, table_schema_esc);
#elif defined(HAVE_SQLITE3)
	result = DBselect("PRAGMA table_info('%s')", table_name_esc);
#endif

	// 释放内存
#if defined(HAVE_IBM_DB2) || defined(HAVE_ORACLE)
	zbx_free(field_name_esc);
	zbx_free(table_name_esc);
#endif

	// 判断查询结果是否有数据，如果没有数据，则返回FAIL，否则返回SUCCEED
	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	// 释放查询结果
#if defined(HAVE_IBM_DB2) || defined(HAVE_MYSQL) || defined(HAVE_ORACLE) || defined(HAVE_POSTGRESQL)
	DBfree_result(result);
#endif

	return ret;
}

	result = DBselect("PRAGMA table_info('%s')", table_name_esc);

	zbx_free(table_name_esc);

	while (NULL != (row = DBfetch(result)))
	{
		if (0 != strcmp(field_name, row[1]))
			continue;

		ret = SUCCEED;
		break;
	}
	DBfree_result(result);
#endif

	return ret;
}

#ifndef HAVE_SQLITE3
/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的表和索引是否存在。根据不同的数据库类型，执行相应的查询语句。如果查询结果为空，则表示不存在，返回失败；否则返回成功。在查询过程中，还对表名和索引名进行了转义，以防止SQL注入攻击。
 ******************************************************************************/
// 定义一个函数，用于检查表和索引是否存在
int DBindex_exists(const char *table_name, const char *index_name)
{
	// 定义一些变量
	char *table_name_esc, *index_name_esc;
#if defined(HAVE_POSTGRESQL)
	char *table_schema_esc;
#endif
	DB_RESULT result;
	int ret;

	// 对表名和索引名进行转义
	table_name_esc = DBdyn_escape_string(table_name);
	index_name_esc = DBdyn_escape_string(index_name);

	// 根据不同的数据库类型，执行不同的查询语句
#if defined(HAVE_IBM_DB2)
	result = DBselect(
			"select 1"
			" from syscat.indexes"
			" where tabschema=user"
				" and lower(tabname)='%s'"
				" and lower(indname)='%s'",
			table_name_esc, index_name_esc);
#elif defined(HAVE_MYSQL)
	result = DBselect(
			"show index from %s"
			" where key_name='%s'",
			table_name_esc, index_name_esc);
#elif defined(HAVE_ORACLE)
	result = DBselect(
			"select 1"
			" from user_indexes"
			" where lower(table_name)='%s'"
				" and lower(index_name)='%s'",
			table_name_esc, index_name_esc);
#elif defined(HAVE_POSTGRESQL)
	table_schema_esc = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ?
				"public" : CONFIG_DBSCHEMA);

	result = DBselect(
			"select 1"
			" from pg_indexes"
			" where tablename='%s'"
				" and indexname='%s'"
				" and schemaname='%s'",
			table_name_esc, index_name_esc, table_schema_esc);

	zbx_free(table_schema_esc);
#endif

	// 判断查询结果是否有数据，如果没有数据，则返回失败，否则返回成功
	ret = (NULL == DBfetch(result) ? FAIL : SUCCEED);

	// 释放查询结果
	DBfree_result(result);

	// 释放转义后的表名和索引名
	zbx_free(table_name_esc);
	zbx_free(index_name_esc);

	// 返回结果
	return ret;
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: DBselect_uint64                                                  *
 *                                                                            *
 * Parameters: sql - [IN] sql statement                                       *
 *             ids - [OUT] sorted list of selected uint64 values              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据给定的 SQL 语句查询数据库，将查询结果中的 id 字段存储到 zbx_vector_uint64_t 类型的向量 ids 中，并对向量进行排序。最后释放查询结果占用的内存。
 ******************************************************************************/
// 定义一个名为 DBselect_uint64 的 void 类型函数，接收两个参数：一个指向 const char 类型的指针 sql 和一个 zbx_vector_uint64_t 类型的指针 ids。
void DBselect_uint64(const char *sql, zbx_vector_uint64_t *ids)
{
	// 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询的结果
	DB_RESULT result;
	// 定义一个 DB_ROW 类型的变量 row，用于存储数据库查询的一行数据
	DB_ROW row;
	// 定义一个 zbx_uint64_t 类型的变量 id，用于存储查询到的数据中的 id 字段
	zbx_uint64_t id;

	// 使用 DBselect 函数执行给定的 sql 语句，并将结果存储在 result 变量中
	result = DBselect("%s", sql);

	// 使用一个 while 循环，当 DBfetch 函数返回不为 NULL 的行时，执行循环体
/******************************************************************************
 * *
 *整个代码块的主要目的是执行多次SQL查询，对于给定的查询语句、字段名和ID列表，按照ORACLE的语法规范进行分组和更新操作。代码首先分配内存用于存储SQL语句，然后开始执行多次更新操作。遍历ID列表，将查询语句、字段名和ID条件添加到SQL缓冲区，并在每个查询语句末尾添加分隔符。执行 overflowed_sql操作，如果失败，则退出循环。如果循环成功结束，且sql_offset大于16，说明执行成功，结束多次更新操作并执行最终的SQL语句。如果执行失败，释放内存并返回失败状态。
 ******************************************************************************/
int	DBexecute_multiple_query(const char *query, const char *field_name, zbx_vector_uint64_t *ids)
{
    // 定义一个常量 ZBX_MAX_IDS，表示一次最大处理的ID数量为950
    char	*sql = NULL;
    size_t	sql_alloc = ZBX_KIBIBYTE, sql_offset = 0;
    int	i, ret = SUCCEED;

    // 分配内存用于存储SQL语句
    sql = (char *)zbx_malloc(sql, sql_alloc);

    // 开始执行多次更新操作
    DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
/******************************************************************************
 * *
 *主要目的：检查数据库的字符集和排序规则是否符合Zabbix的支持范围，如果不符合，则发出警告。同时，检查数据库中的表是否使用了不支持的排序规则。
 *
 *注释详细说明：
 *
 *1. 定义宏，根据数据库类型选择不同的处理方式。
 *2. 针对MySQL数据库，连接数据库并查询字符集和排序规则。
 *3. 针对Oracle数据库，连接数据库并查询字符集和排序规则。
 *4. 释放资源。
 *5. 检查数据库中的表是否使用了不支持的排序规则。
 *6. 释放资源，关闭数据库连接。
 *7. 自由使用。
 ******************************************************************************/
void DBcheck_character_set(void)
{
    // 定义宏，根据数据库类型选择不同的处理方式
#if defined(HAVE_MYSQL)
    // 针对MySQL数据库的代码块
    char *database_name_esc;
    DB_RESULT result;
    DB_ROW row;

    // 获取数据库名称并转义
    database_name_esc = DBdyn_escape_string(CONFIG_DBNAME);
    // 连接数据库
    DBconnect(ZBX_DB_CONNECT_NORMAL);

    // 查询数据库的字符集和排序规则
    result = DBselect(
            "select default_character_set_name,default_collation_name"
            " from information_schema.SCHEMATA"
            " where schema_name='%s'", database_name_esc);

    // 检查结果是否为空，如果不支持该数据库的字符集和排序规则，则警告
    if (NULL == result || NULL == (row = DBfetch(result)))
    {
        zbx_warn_no_charset_info(CONFIG_DBNAME);
    }
    else
    {
        char *char_set = row[0];
        char *collation = row[1];

        // 检查字符集和排序规则是否与支持的规则匹配，如果不匹配，则警告
        if (0 != strcasecmp(char_set, ZBX_SUPPORTED_DB_CHARACTER_SET))
            zbx_warn_char_set(CONFIG_DBNAME, char_set);

        if (0 != zbx_strncasecmp(collation, ZBX_SUPPORTED_DB_COLLATION, sizeof(ZBX_SUPPORTED_DB_COLLATION)))
        {
            zabbix_log(LOG_LEVEL_WARNING, "Zabbix supports only \"%s\" collation."
                    " Database \"%s\" has default collation \"%s\"", ZBX_SUPPORTED_DB_COLLATION,
                    CONFIG_DBNAME, collation);
        }
    }

    // 释放资源
    DBfree_result(result);

    // 查询数据库表的字符集和排序规则
    result = DBselect(
            "select count(*)"
            " from information_schema.`COLUMNS`"
            " where table_schema='%s'"
            " and data_type in ('text','varchar','longtext')"
            " and (character_set_name<>'%s' or collation_name<>'%s')",
            database_name_esc, ZBX_SUPPORTED_DB_CHARACTER_SET, ZBX_SUPPORTED_DB_COLLATION);

    // 检查结果是否为空，如果不支持该数据库的字符集和排序规则，则警告
    if (NULL == result || NULL == (row = DBfetch(result)))
    {
        zabbix_log(LOG_LEVEL_WARNING, "cannot get character set of database \"%s\" tables", CONFIG_DBNAME);
    }
    else if (0 != strcmp("0", row[0]))
    {
        zabbix_log(LOG_LEVEL_WARNING, "character set name or collation name that is not supported by Zabbix"
                " found in %s column(s) of database \"%s\"", row[0], CONFIG_DBNAME);
        zabbix_log(LOG_LEVEL_WARNING, "only character set \"%s\" and collation \"%s\" should be used in "
                "database", ZBX_SUPPORTED_DB_CHARACTER_SET, ZBX_SUPPORTED_DB_COLLATION);
    }

    // 释放资源
    DBfree_result(result);
    DBclose();
    zbx_free(database_name_esc);
#elif defined(HAVE_ORACLE)
    // 针对Oracle数据库的代码块
    DB_RESULT	result;
    DB_ROW		row;

    // 连接数据库
    DBconnect(ZBX_DB_CONNECT_NORMAL);
    result = DBselect(
            "select parameter,value"
            " from NLS_DATABASE_PARAMETERS"
            " where parameter in ('NLS_CHARACTERSET','NLS_NCHAR_CHARACTERSET')");

    // 检查结果是否为空，如果不支持该数据库的字符集和排序规则，则警告
    if (NULL == result)
    {
        zbx_warn_no_charset_info(CONFIG_DBNAME);
    }
    else
    {
        while (NULL != (row = DBfetch(result)))
        {
            const char	*parameter = row[0];
            const char	*value = row[1];

            if (NULL == parameter || NULL == value)
            {
                continue;
            }
            else if (0 == strcasecmp("NLS_CHARACTERSET", parameter) ||
                    (0 == strcasecmp("NLS_NCHAR_CHARACTERSET", parameter)))
            {
                if (0 != strcasecmp(ZBX_SUPPORTED_DB_CHARACTER_SET, value))
                {
                    zabbix_log(LOG_LEVEL_WARNING, "database \"%s\" parameter \"%s\" has value"
                        " \"%s\". Zabbix supports only \"%s\" character set",
                        CONFIG_DBNAME, parameter, value,
                        ZBX_SUPPORTED_DB_CHARACTER_SET);
                }
            }
        }
    }

    // 释放资源
    DBfree_result(result);

    // 查询数据库表的字符集和排序规则
    result = DBselect(
            "select oid"
            " from pg_namespace"
            " where nspname='%s'",
            schema_name_esc);

    // 检查结果是否为空，如果不支持该数据库的字符集和排序规则，则警告
    if (NULL == result || NULL == (row = DBfetch(result)) || '\0' == **row)
    {
        zabbix_log(LOG_LEVEL_WARNING, "cannot get character set of database \"%s\" fields", CONFIG_DBNAME);
    }

    // 释放资源
    DBfree_result(result);

    // 查询数据库的字符集和排序规则
    result = DBselect("show client_encoding");

    // 检查结果是否为空，如果不支持该数据库的字符集和排序规则，则警告
    if (NULL == result || NULL == (row = DBfetch(result)))
    {
        zabbix_log(LOG_LEVEL_WARNING, "cannot get info about database \"%s\" client encoding", CONFIG_DBNAME);
    }
    else if (0 != strcasecmp(row[0], ZBX_SUPPORTED_DB_CHARACTER_SET))
    {
        zabbix_log(LOG_LEVEL_WARNING, "client_encoding for database \"%s\" is \"%s\". Zabbix supports only"
            " \"%s\"", CONFIG_DBNAME, row[0], ZBX_SUPPORTED_DB_CHARACTER_SET);
    }

    // 释放资源
    DBfree_result(result);

    // 查询数据库的服务器字符集和排序规则
    result = DBselect("show server_encoding");

    // 检查结果是否为空，如果不支持该数据库的字符集和排序规则，则警告
    if (NULL == result || NULL == (row = DBfetch(result)))
    {
        zabbix_log(LOG_LEVEL_WARNING, "cannot get info about database \"%s\" server encoding", CONFIG_DBNAME);
    }
    else if (0 != strcasecmp(row[0], ZBX_SUPPORTED_DB_CHARACTER_SET))
    {
        zabbix_log(LOG_LEVEL_WARNING, "server_encoding for database \"%s\" is \"%s\". Zabbix supports only"
            " \"%s\"", CONFIG_DBNAME, row[0], ZBX_SUPPORTED_DB_CHARACTER_SET);
    }
out:
    DBfree_result(result);
    DBclose();
    zbx_free(schema_name_esc);
    zbx_free(database_name_esc);
#endif
}

		zbx_warn_no_charset_info(CONFIG_DBNAME);
		goto out;
	}
	else if (strcasecmp(row[0], ZBX_SUPPORTED_DB_CHARACTER_SET))
	{
		zbx_warn_char_set(CONFIG_DBNAME, row[0]);
		goto out;

	}

	DBfree_result(result);

	result = DBselect(
			"select oid"
			" from pg_namespace"
			" where nspname='%s'",
			schema_name_esc);

	if (NULL == result || NULL == (row = DBfetch(result)) || '\0' == **row)
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get character set of database \"%s\" fields", CONFIG_DBNAME);
		goto out;
	}

	strscpy(oid, *row);

	DBfree_result(result);

	result = DBselect(
			"select count(*)"
			" from pg_attribute as a"
				" left join pg_class as c"
					" on c.relfilenode=a.attrelid"
				" left join pg_collation as l"
					" on l.oid=a.attcollation"
			" where atttypid in (25,1043)"
				" and c.relnamespace=%s"
				" and c.relam=0"
				" and l.collname<>'default'",
			oid);

	if (NULL == result || NULL == (row = DBfetch(result)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get character set of database \"%s\" fields", CONFIG_DBNAME);
	}
	else if (0 != strcmp("0", row[0]))
	{
		zabbix_log(LOG_LEVEL_WARNING, "database has %s fields with unsupported character set. Zabbix supports"
				" only \"%s\" character set", row[0], ZBX_SUPPORTED_DB_CHARACTER_SET);
	}

	DBfree_result(result);

	result = DBselect("show client_encoding");

	if (NULL == result || NULL == (row = DBfetch(result)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get info about database \"%s\" client encoding", CONFIG_DBNAME);
	}
	else if (0 != strcasecmp(row[0], ZBX_SUPPORTED_DB_CHARACTER_SET))
	{
		zabbix_log(LOG_LEVEL_WARNING, "client_encoding for database \"%s\" is \"%s\". Zabbix supports only"
				" \"%s\"", CONFIG_DBNAME, row[0], ZBX_SUPPORTED_DB_CHARACTER_SET);
	}

	DBfree_result(result);

	result = DBselect("show server_encoding");

	if (NULL == result || NULL == (row = DBfetch(result)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get info about database \"%s\" server encoding", CONFIG_DBNAME);
	}
	else if (0 != strcasecmp(row[0], ZBX_SUPPORTED_DB_CHARACTER_SET))
	{
		zabbix_log(LOG_LEVEL_WARNING, "server_encoding for database \"%s\" is \"%s\". Zabbix supports only"
				" \"%s\"", CONFIG_DBNAME, row[0], ZBX_SUPPORTED_DB_CHARACTER_SET);
	}
out:
	DBfree_result(result);
	DBclose();
	zbx_free(schema_name_esc);
	zbx_free(database_name_esc);
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是对一个包含多个字段的数组进行格式化，将字段的数据按照指定的格式添加到一个新的字符串中。输出的字符串可以用于数据库查询或其他操作。
 ******************************************************************************/
// 定义一个静态字符指针变量，用于存储格式化后的字符串
static char *zbx_db_format_values(ZBX_FIELD **fields, const zbx_db_value_t *values, int values_num);

// 定义变量
int	i;
char	*str = NULL;
size_t	str_alloc = 0, str_offset = 0;

// 遍历 values 数组
for (i = 0; i < values_num; i++)
{
	// 获取当前字段
	ZBX_FIELD		*field = fields[i];
	const zbx_db_value_t	*value = &values[i];

	// 如果当前不是第一个字段，则在字符串中添加一个逗号
	if (0 < i)
		zbx_chrcpy_alloc(&str, &str_alloc, &str_offset, ',');

	// 根据字段的类型进行格式化
	switch (field->type)
	{
		// 如果是字符、文本、短文本或长文本类型，则用单引号括起来的字符串表示
		case ZBX_TYPE_CHAR:
		case ZBX_TYPE_TEXT:
		case ZBX_TYPE_SHORTTEXT:
		case ZBX_TYPE_LONGTEXT:
			zbx_snprintf_alloc(&str, &str_alloc, &str_offset, "'%s'", value->str);
			break;

		// 如果是浮点类型，则用科学计数法表示
		case ZBX_TYPE_FLOAT:
			zbx_snprintf_alloc(&str, &str_alloc, &str_offset, ZBX_FS_DBL, value->dbl);
			break;

		// 如果是整数或无符号整数类型，则用十进制表示
		case ZBX_TYPE_ID:
		case ZBX_TYPE_UINT:
			zbx_snprintf_alloc(&str, &str_alloc, &str_offset, ZBX_FS_UI64, value->ui64);
			break;

		// 如果是整数类型，则用十进制表示
		case ZBX_TYPE_INT:
			zbx_snprintf_alloc(&str, &str_alloc, &str_offset, "%d", value->i32);
			break;

		// 如果是未知类型，则用 "(unknown type)" 表示
		default:
			zbx_strcpy_alloc(&str, &str_alloc, &str_offset, "(unknown type)");
			break;
	}
}

// 返回格式化后的字符串
return str;

				zbx_snprintf_alloc(&str, &str_alloc, &str_offset, ZBX_FS_DBL, value->dbl);
				break;
			case ZBX_TYPE_ID:
			case ZBX_TYPE_UINT:
				zbx_snprintf_alloc(&str, &str_alloc, &str_offset, ZBX_FS_UI64, value->ui64);
				break;
			case ZBX_TYPE_INT:
				zbx_snprintf_alloc(&str, &str_alloc, &str_offset, "%d", value->i32);
				break;
			default:
				zbx_strcpy_alloc(&str, &str_alloc, &str_offset, "(unknown type)");
				break;
		}
	}

	return str;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_insert_clean                                              *
 *                                                                            *
 * Purpose: releases resources allocated by bulk insert operations            *
 *                                                                            *
 * Parameters: self        - [IN] the bulk insert data                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清理数据库插入操作所使用的内存资源。具体来说，该函数遍历数据库插入操作所需的行和字段，释放它们所占用的内存空间。最后，销毁 rows 和 fields 向量，以便彻底清理资源。
 ******************************************************************************/
// 定义一个名为 zbx_db_insert_clean 的函数，参数为一个 zbx_db_insert_t 类型的指针。
void	zbx_db_insert_clean(zbx_db_insert_t *self)
{
	// 定义两个循环变量 i 和 j，分别用于遍历行和字段。
	int	i, j;

	// 遍历行的数组，直到数组末尾。
	for (i = 0; i < self->rows.values_num; i++)
	{
		// 获取当前行的指针。
		zbx_db_value_t	*row = (zbx_db_value_t *)self->rows.values[i];

		// 遍历行的每个字段，直到字段数组末尾。
		for (j = 0; j < self->fields.values_num; j++)
		{
			// 获取当前字段的指针。
			ZBX_FIELD	*field = (ZBX_FIELD *)self->fields.values[j];

			// 根据字段的类型进行切换操作。
			switch (field->type)
			{
				case ZBX_TYPE_CHAR:
				case ZBX_TYPE_TEXT:
				case ZBX_TYPE_SHORTTEXT:
				case ZBX_TYPE_LONGTEXT:
					// 释放当前字段所占用的内存（字符串）。
					zbx_free(row[j].str);
			}
		}

		// 释放当前行的内存。
		zbx_free(row);
	}

	// 销毁 rows 向量。
	zbx_vector_ptr_destroy(&self->rows);

	// 销毁 fields 向量。
	zbx_vector_ptr_destroy(&self->fields);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_insert_prepare_dyn                                        *
 *                                                                            *
 * Purpose: prepare for database bulk insert operation                        *
 *                                                                            *
 * Parameters: self        - [IN] the bulk insert data                        *
 *             table       - [IN] the target table name                       *
 *             fields      - [IN] names of the fields to insert               *
 *             fields_num  - [IN] the number of items in fields array         *
 *                                                                            *
 * Comments: The operation fails if the target table does not have the        *
 *           specified fields defined in its schema.                          *
 *                                                                            *
 *           Usage example:                                                   *
 *             zbx_db_insert_t ins;                                           *
 *                                                                            *
 *             zbx_db_insert_prepare(&ins, "history", "id", "value");         *
 *             zbx_db_insert_add_values(&ins, (zbx_uint64_t)1, 1.0);          *
 *             zbx_db_insert_add_values(&ins, (zbx_uint64_t)2, 2.0);          *
 *               ...                                                          *
 *             zbx_db_insert_execute(&ins);                                   *
 *             zbx_db_insert_clean(&ins);                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是预处理插入数据时的动态数据结构。它创建了一个zbx_db_insert_t类型的对象，设置了表名、字段数组和自动递增字段等属性。同时，还将字段数组中的所有元素添加到对象的字段数组中。在程序执行过程中，这个对象将用于插入数据到数据库。
 ******************************************************************************/
// 定义一个函数，用于预处理插入数据时的动态数据结构
void zbx_db_insert_prepare_dyn(zbx_db_insert_t *self, const ZBX_TABLE *table, const ZBX_FIELD **fields, int fields_num)
{
	// 定义一个循环变量 i，用于遍历 fields 数组
	int i;

	// 检查 fields_num 是否为0，如果不是，则说明 fields 数组为空，这种情况不应该发生，退出程序
	if (0 == fields_num)
/******************************************************************************
 * *
 *整个代码块的主要目的是为插入数据到数据库做准备。该函数接收一个zbx_db_insert_t类型的指针、一个表名以及可变数量的参数（用于存储字段名）。首先，它查找数据库架构中的表和字段，然后创建一个字段列表。接着，遍历可变数量的参数，获取字段名并将其添加到字段列表中。最后，调用zbx_db_insert_prepare_dyn函数准备插入数据。在整个过程中，如果遇到表或字段找不到的情况，会记录日志并退出程序。
 ******************************************************************************/
void zbx_db_insert_prepare(zbx_db_insert_t *self, const char *table, ...)
{
	// 定义一个指向zbx_db_insert_t结构体的指针，用于传递参数
	zbx_db_insert_t *self;
	// 定义一个字符串指针，用于存储表名
	const char *table;
	// 使用变量参数列表（va_list）来接收后续的参数
	va_list args;

	// 初始化一个zbx_vector_ptr_t类型的变量fields，用于存储字段信息
	zbx_vector_ptr_t fields;
	// 初始化一个char类型的指针变量field，用于存储字段名
	char *field;
	// 初始化一个指向ZBX_TABLE类型的指针，用于存储表结构信息
	const ZBX_TABLE *ptable;
	// 初始化一个指向ZBX_FIELD类型的指针，用于存储字段结构信息
	const ZBX_FIELD *pfield;

	/* 查找数据库架构中的表和字段 */
	if (NULL == (ptable = DBget_table(table)))
	{
		// 表名找不到，这是一个不可能的情况，记录日志并退出程序
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	// 开始解析变量参数列表
	va_start(args, table);

	// 创建一个zbx_vector_ptr_t类型的变量fields，用于存储字段信息
	zbx_vector_ptr_create(&fields);

	// 遍历变量参数列表，获取字段名
	while (NULL != (field = va_arg(args, char *)))
	{
		// 查找表结构中的字段
		if (NULL == (pfield = DBget_field(ptable, field)))
		{
			// 找不到字段，记录日志并退出程序
			zabbix_log(LOG_LEVEL_ERR, "Cannot locate table \"%s\" field \"%s\" in database schema",
					table, field);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}
		// 将字段添加到fields vector中
		zbx_vector_ptr_append(&fields, (ZBX_FIELD *)pfield);
	}

	va_end(args);

	// 调用zbx_db_insert_prepare_dyn函数，准备插入数据
	zbx_db_insert_prepare_dyn(self, ptable, (const ZBX_FIELD **)fields.values, fields.values_num);

	// 释放fields vector占用的内存
	zbx_vector_ptr_destroy(&fields);
}

 * Comments: This is a convenience wrapper for zbx_db_insert_prepare_dyn()    *
 *           function.                                                        *
 *                                                                            *
 ******************************************************************************/
void	zbx_db_insert_prepare(zbx_db_insert_t *self, const char *table, ...)
{
	zbx_vector_ptr_t	fields;
	va_list			args;
	char			*field;
	const ZBX_TABLE		*ptable;
	const ZBX_FIELD		*pfield;

	/* find the table and fields in database schema */
	if (NULL == (ptable = DBget_table(table)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	va_start(args, table);

	zbx_vector_ptr_create(&fields);

	while (NULL != (field = va_arg(args, char *)))
	{
		if (NULL == (pfield = DBget_field(ptable, field)))
		{
			zabbix_log(LOG_LEVEL_ERR, "Cannot locate table \"%s\" field \"%s\" in database schema",
					table, field);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}
		zbx_vector_ptr_append(&fields, (ZBX_FIELD *)pfield);
	}

	va_end(args);

	zbx_db_insert_prepare_dyn(self, ptable, (const ZBX_FIELD **)fields.values, fields.values_num);

	zbx_vector_ptr_destroy(&fields);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_insert_add_values_dyn                                     *
 *                                                                            *
 * Purpose: adds row values for database bulk insert operation                *
 *                                                                            *
 * Parameters: self        - [IN] the bulk insert data                        *
 *             values      - [IN] the values to insert                        *
 *             fields_num  - [IN] the number of items in values array         *
 *                                                                            *
 * Comments: The values must be listed in the same order as the field names   *
 *           for insert preparation functions.                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个 zbx_db_insert_t 类型的指针（表示数据库插入操作的结构体）、一个 zbx_db_value_t 类型的指针数组（表示要插入的数据值）以及数据值的数量，然后动态生成一个数据行，并将该数据行添加到数据库插入操作的结构体的 rows 向量中。在这个过程中，还对数据值进行了转义处理，以防止 SQL 注入。
 ******************************************************************************/
// 定义一个函数，用于向数据库插入动态生成的数据行
void zbx_db_insert_add_values_dyn(zbx_db_insert_t *self, const zbx_db_value_t **values, int values_num)
{
	// 定义一个整型变量 i，用于循环计数
	int i;
	// 定义一个指向 zbx_db_value_t 类型的指针 row，用于存储数据行
	zbx_db_value_t *row;

	// 检查传入的 values 数量是否与 self->fields.values_num 相等
	if (values_num != self->fields.values_num)
	{
		// 如果不相等，表示出现了错误，不应该发生这种情况
		THIS_SHOULD_NEVER_HAPPEN;
		// 退出程序，返回失败代码
		exit(EXIT_FAILURE);
	}

	// 为 row 分配内存，使其可以存储 self->fields.values_num 个 zbx_db_value_t 类型的数据
	row = (zbx_db_value_t *)zbx_malloc(NULL, self->fields.values_num * sizeof(zbx_db_value_t));

	// 遍历 self->fields.values_num 次，处理每个数据字段
	for (i = 0; i < self->fields.values_num; i++)
	{
		// 获取当前字段的指针
		ZBX_FIELD *field = (ZBX_FIELD *)self->fields.values[i];
		// 获取当前字段的值指针
		const zbx_db_value_t *value = values[i];

		// 根据字段类型进行不同处理
		switch (field->type)
		{
			case ZBX_TYPE_LONGTEXT:
			case ZBX_TYPE_CHAR:
			case ZBX_TYPE_TEXT:
			case ZBX_TYPE_SHORTTEXT:
#ifdef HAVE_ORACLE
				// 对字段值进行转义处理，防止SQL注入
				row[i].str = DBdyn_escape_field_len(field, value->str, ESCAPE_SEQUENCE_OFF);
#else
				// 对字段值进行转义处理，防止SQL注入
				row[i].str = DBdyn_escape_field_len(field, value->str, ESCAPE_SEQUENCE_ON);
#endif
				break;
			default:
				// 对于其他类型，直接复制值
				row[i] = *value;
				break;
		}
	}

/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个zbx_db_insert_t类型的指针以及一系列命令行参数，根据传入的ZBX_FIELD结构体，创建对应的zbx_db_value_t结构体，并将这些数据存储在一个zbx_vector_ptr结构体中。最后，调用zbx_db_insert_add_values_dyn函数将这些数据插入到数据库中。在处理完成后，释放分配的内存。
 ******************************************************************************/
void	zbx_db_insert_add_values(zbx_db_insert_t *self, ...)
{
	// 定义一个指向zbx_db_insert_t结构体的指针
	zbx_vector_ptr_t	values;
	// 定义一个命令行参数解析器
	va_list			args;
	// 定义一个循环变量
	int			i;
	// 定义一个指向ZBX_FIELD结构体的指针
	ZBX_FIELD		*field;
	// 定义一个指向zbx_db_value_t结构体的指针
	zbx_db_value_t		*value;

	// 开始解析命令行参数
	va_start(args, self);

	// 创建一个zbx_vector_ptr类型的变量，用于存储数据
	zbx_vector_ptr_create(&values);

	// 遍历传入的ZBX_FIELD结构体数组
	for (i = 0; i < self->fields.values_num; i++)
	{
		// 获取当前ZBX_FIELD结构体
		field = (ZBX_FIELD *)self->fields.values[i];

		// 分配一块内存，用于存储zbx_db_value_t结构体
		value = (zbx_db_value_t *)zbx_malloc(NULL, sizeof(zbx_db_value_t));

		// 根据field的类型，设置value的对应成员变量
		switch (field->type)
		{
			case ZBX_TYPE_CHAR:
			case ZBX_TYPE_TEXT:
			case ZBX_TYPE_SHORTTEXT:
			case ZBX_TYPE_LONGTEXT:
				value->str = va_arg(args, char *);
				break;
			case ZBX_TYPE_INT:
				value->i32 = va_arg(args, int);
				break;
			case ZBX_TYPE_FLOAT:
				value->dbl = va_arg(args, double);
				break;
			case ZBX_TYPE_UINT:
			case ZBX_TYPE_ID:
				value->ui64 = va_arg(args, zbx_uint64_t);
				break;
			default:
				// 不应该出现这种情况，表示错误
				THIS_SHOULD_NEVER_HAPPEN;
				exit(EXIT_FAILURE);
		}

		// 将value添加到values vector中
		zbx_vector_ptr_append(&values, value);
	}

	// 结束解析命令行参数
	va_end(args);

	// 调用zbx_db_insert_add_values_dyn函数，将数据插入到数据库中
	zbx_db_insert_add_values_dyn(self, (const zbx_db_value_t **)values.values, values.values_num);

	// 清理内存，释放zbx_vector_ptr中的数据
	zbx_vector_ptr_clear_ext(&values, zbx_ptr_free);
	// 销毁zbx_vector_ptr结构体
	zbx_vector_ptr_destroy(&values);
}

				break;
			case ZBX_TYPE_UINT:
			case ZBX_TYPE_ID:
				value->ui64 = va_arg(args, zbx_uint64_t);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				exit(EXIT_FAILURE);
		}

		zbx_vector_ptr_append(&values, value);
	}

	va_end(args);

	zbx_db_insert_add_values_dyn(self, (const zbx_db_value_t **)values.values, values.values_num);

	zbx_vector_ptr_clear_ext(&values, zbx_ptr_free);
	zbx_vector_ptr_destroy(&values);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_insert_execute                                            *
 *                                                                            *
 * Purpose: executes the prepared database bulk insert operation              *
 *                                                                            *
 * Parameters: self - [IN] the bulk insert data                               *
 *                                                                            *
 * Return value: Returns SUCCEED if the operation completed successfully or   *
 *               FAIL otherwise.                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *主要目的：
 *
 *1. 检查传入的参数是否合法。
 *2. 处理自动递增字段。
 *3. 分配内存用于存储SQL插入语句。
 *4. 构建SQL插入语句。
 *5. 添加MySQL特定处理。
 *6. 预处理SQL语句。
 *7. 执行插入操作。
 *8. 释放分配的内存。
 ******************************************************************************/
int zbx_db_insert_execute(zbx_db_insert_t *self)
{
	int		ret = FAIL, i, j;
	const ZBX_FIELD	*field;
	char		*sql_command, delim[2] = {',', '('};
	size_t		sql_command_alloc = 512, sql_command_offset = 0;

	// 检查传入的参数是否合法
	if (0 == self->rows.values_num)
		return SUCCEED;

	// 处理自动递增字段
	if (-1 != self->autoincrement)
	{
		zbx_uint64_t	id;

		id = DBget_maxid_num(self->table->table, self->rows.values_num);

		for (i = 0; i < self->rows.values_num; i++)
		{
			zbx_db_value_t	*values = (zbx_db_value_t *)self->rows.values[i];

			values[self->autoincrement].ui64 = id++;
		}
	}

	// 分配内存用于存储SQL插入语句
	sql_command = (char *)zbx_malloc(NULL, sql_command_alloc);

	// 构建SQL插入语句
	zbx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, "insert into ");
	zbx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, self->table->table);
	zbx_chrcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, ' ');

	for (i = 0; i < self->fields.values_num; i++)
	{
		field = (ZBX_FIELD *)self->fields.values[i];

		zbx_chrcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, delim[0 == i]);
		zbx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, field->name);
	}

	// 添加MySQL特定处理
	zbx_strcpy_alloc(&sql_command, &sql_command_alloc, &sql_command_offset, ") values ");

	// 预处理SQL语句
	zbx_free(sql_command);

#ifndef HAVE_ORACLE
	sql = (char *)zbx_malloc(NULL, sql_alloc);
#endif

	// 执行插入操作
	ret = zbx_db_insert_execute_internal(self, sql, sql_alloc);

	// 释放分配的内存
	zbx_free(sql);

#ifdef HAVE_MYSQL
	zbx_free(sql_values);
#endif
#else
	zbx_free(contexts);
#endif

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_insert_autoincrement                                      *
 *                                                                            *
 * Purpose: executes the prepared database bulk insert operation              *
 *                                                                            *
 * Parameters: self - [IN] the bulk insert data                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是：接收一个zbx_db_insert_t结构体的指针和一个字段名，遍历结构体中的字段，找到与给定字段名匹配的字段，并将该字段的自动递增索引设置为当前循环索引。如果没有找到匹配的字段，触发错误并终止程序运行。
 ******************************************************************************/
void zbx_db_insert_autoincrement(zbx_db_insert_t *self, const char *field_name)
{
	// 定义一个循环变量 i，用于遍历 self->fields.values 数组
	int i;

	// 遍历 self->fields.values 数组，查找字段名与 field_name 相同的字段
	for (i = 0; i < self->fields.values_num; i++)
	{
		ZBX_FIELD *field = (ZBX_FIELD *)self->fields.values[i];

		// 判断字段类型是否为 ZBX_TYPE_ID，即整型字段
		if (ZBX_TYPE_ID == field->type && 0 == strcmp(field_name, field->name))
		{
			// 找到匹配的字段，将其自动递增索引设置为当前循环索引 i
			self->autoincrement = i;
			// 函数执行完毕，返回 void 类型，不返回任何值
			return;
		}
	}

	// 如果没有找到匹配的字段，触发 THIS_SHOULD_NEVER_HAPPEN 错误
	THIS_SHOULD_NEVER_HAPPEN;
	// 终止程序运行，返回 EXIT_FAILURE 状态码
	exit(EXIT_FAILURE);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从一个名为\"users\"的数据库表中获取userid字段的数据，根据userid的值判断数据库类型（服务器或代理），并返回相应的类型标识。输出结果为：
 *
 *```
 *ZBX_DB_GET_DATABASE_TYPE() 函数执行结束：ZBX_DB_SERVER
 *```
 ******************************************************************************/
// 定义一个函数zbx_db_get_database_type，该函数接收void类型的参数，没有返回值。
int	zbx_db_get_database_type(void)
{
	// 定义一个字符串指针__function_name，用于存储函数名
	const char	*__function_name = "zbx_db_get_database_type";

	// 定义一个字符串指针result_string，用于存储查询结果
	const char	*result_string;
	// 定义一个DB_RESULT类型的变量result，用于存储数据库查询结果
	DB_RESULT	result;
	// 定义一个DB_ROW类型的变量row，用于存储数据库记录
	DB_ROW		row;
	// 定义一个整型变量ret，用于存储最终结果
	int		ret = ZBX_DB_UNKNOWN;

	// 打印调试信息，表示进入函数__function_name()
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 连接数据库
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	// 执行SQL查询，从"users"表中获取userid字段的数据
	if (NULL == (result = DBselectN("select userid from users", 1)))
	{
		// 打印调试信息，表示无法从"users"表中选取记录
		zabbix_log(LOG_LEVEL_DEBUG, "cannot select records from \"users\" table");
		// 结束函数执行，跳转到out标签处
		goto out;
	}

	// 获取查询结果中的记录
	if (NULL != (row = DBfetch(result)))
	{
		// 打印调试信息，表示"users"表中至少有一条记录
		zabbix_log(LOG_LEVEL_DEBUG, "there is at least 1 record in \"users\" table");
		// 设置ret值为ZBX_DB_SERVER，表示数据库类型为服务器
		ret = ZBX_DB_SERVER;
	}
	else
	{
		// 打印调试信息，表示"users"表中没有记录
		zabbix_log(LOG_LEVEL_DEBUG, "no records in \"users\" table");
		// 设置ret值为ZBX_DB_PROXY，表示数据库类型为代理
		ret = ZBX_DB_PROXY;
	}

	// 释放查询结果
	DBfree_result(result);
out:
	// 关闭数据库连接
	DBclose();

	// 根据ret的值，切换输出结果字符串
	switch (ret)
	{
		case ZBX_DB_SERVER:
			// 设置result_string值为"ZBX_DB_SERVER"
			result_string = "ZBX_DB_SERVER";
			break;
		case ZBX_DB_PROXY:
			// 设置result_string值为"ZBX_DB_PROXY"
			result_string = "ZBX_DB_PROXY";
			break;
		case ZBX_DB_UNKNOWN:
			// 设置result_string值为"ZBX_DB_UNKNOWN"
			result_string = "ZBX_DB_UNKNOWN";
			break;
	}

	// 打印调试信息，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, result_string);

	// 返回ret值，表示数据库类型
	return ret;
}

		case ZBX_DB_PROXY:
			result_string = "ZBX_DB_PROXY";
			break;
		case ZBX_DB_UNKNOWN:
			result_string = "ZBX_DB_UNKNOWN";
			break;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, result_string);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBlock_record                                                    *
 *                                                                            *
 * Purpose: locks a record in a table by its primary key and an optional      *
 *          constraint field                                                  *
 *                                                                            *
 * Parameters: table     - [IN] the target table                              *
 *             id        - [IN] primary key value                             *
 *             add_field - [IN] additional constraint field name (optional)   *
 *             add_id    - [IN] constraint field value                        *
 *                                                                            *
 * Return value: SUCCEED - the record was successfully locked                 *
 *               FAIL    - the table does not contain the specified record    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是提供一个函数DBlock_record，用于查询数据库表中满足条件的记录。函数接收4个参数，分别是表名、主键ID、附加字段名和附加字段值。根据附加字段是否为空，执行不同的查询语句。如果查询结果为空，则返回FAIL，否则返回SUCCEED。在整个查询过程中，还对函数调用进行了事务检查和调试信息输出。
 ******************************************************************************/
// 定义一个函数DBlock_record，接收4个参数：
// 1. 一个字符串指针table，表示数据库表名；
// 2. 一个zbx_uint64_t类型的id，表示要查询的主键ID；
// 3. 一个字符串指针add_field，表示要查询的附加字段名；
// 4. 一个zbx_uint64_t类型的add_id，表示要查询的附加字段值。
int DBlock_record(const char *table, zbx_uint64_t id, const char *add_field, zbx_uint64_t add_id)
{
	// 定义一个常量字符串__function_name，值为"DBlock_record"，用于记录函数名

	// 定义一个DB_RESULT类型的变量result，用于存储数据库操作结果
	// 定义一个const ZBX_TABLE类型的指针变量t，用于存储表格信息
	// 定义一个int类型的变量ret，用于存储函数返回值

	// 打印调试信息，表示进入DBlock_record函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断当前是否处于事务中，如果不是，则打印调试信息
	if (0 == zbx_db_txn_level())
		zabbix_log(LOG_LEVEL_DEBUG, "%s() called outside of transaction", __function_name);

	// 获取表格信息
	t = DBget_table(table);

	// 如果add_field为空，则执行以下查询：
	// 从表t中选取主键id等于给定id的记录，返回值为NULL
	if (NULL == add_field)
	{
		result = DBselect("select null from %s where %s=" ZBX_FS_UI64 ZBX_FOR_UPDATE, table, t->recid, id);
	}
	// 如果add_field不为空，则执行以下查询：
	// 从表t中选取主键id等于给定id且附加字段add_field等于给定add_id的记录，返回值为NULL
	else
	{
		result = DBselect("select null from %s where %s=" ZBX_FS_UI64 " and %s=" ZBX_FS_UI64 ZBX_FOR_UPDATE,
				table, t->recid, id, add_field, add_id);
	}

	// 判断查询结果是否为空，如果为空，则返回FAIL，否则返回SUCCEED
	if (NULL == DBfetch(result))
		ret = FAIL;
	else
		ret = SUCCEED;

	// 释放查询结果
	DBfree_result(result);

	// 打印调试信息，表示结束DBlock_record函数
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: DBlock_records                                                   *
 *                                                                            *
 * Purpose: locks a records in a table by its primary key                     *
 *                                                                            *
 * Parameters: table     - [IN] the target table                              *
 *             ids       - [IN] primary key values                            *
 *                                                                            *
 * Return value: SUCCEED - one or more of the specified records were          *
 *                         successfully locked                                *
 *               FAIL    - the table does not contain any of the specified    *
 *                         records                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个指定的表中查询符合条件的记录。具体步骤如下：
 *
 *1. 记录函数调用日志。
 *2. 检查当前是否处于事务中，如果不是，则记录日志并退出。
 *3. 获取表结构。
 *4. 构造SQL查询语句，并添加条件。
 *5. 执行查询语句并获取结果。
 *6. 检查查询结果是否为空，如果为空则返回失败，否则返回成功。
 *7. 释放查询结果占用的内存。
 *8. 记录函数调用结果的日志。
 *9. 返回结果。
 ******************************************************************************/
// 定义一个函数DBlock_records，接收两个参数，一个是指向表名的字符指针，另一个是指向zbx_vector_uint64_t类型数据的指针
int DBlock_records(const char *table, const zbx_vector_uint64_t *ids)
{
    // 定义一些变量，包括DB_RESULT类型的result，指向表结构的指针t，以及一些用于操作数据库的指针和字符串
    const char *__function_name = "DBlock_records";

    DB_RESULT	result;
    const ZBX_TABLE	*t;
    int		ret;
    char		*sql = NULL;
    size_t		sql_alloc = 0, sql_offset = 0;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 检查当前是否处于事务中，如果不是，则记录日志并退出
    if (0 == zbx_db_txn_level())
        zabbix_log(LOG_LEVEL_DEBUG, "%s() called outside of transaction", __function_name);

    // 获取表结构
    t = DBget_table(table);

    // 构造SQL查询语句，查询符合条件的记录
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select null from %s where", table);
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, t->recid, ids->values, ids->values_num);

    // 执行查询语句并获取结果
    result = DBselect("%s" ZBX_FOR_UPDATE, sql);

    // 释放SQL语句占用的内存
    zbx_free(sql);

    // 检查查询结果是否为空，如果为空则返回失败，否则返回成功
    if (NULL == DBfetch(result))
        ret = FAIL;
    else
        ret = SUCCEED;

    // 释放查询结果占用的内存
    DBfree_result(result);

    // 记录函数调用结果的日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_sql_add_host_availability                                    *
 *                                                                            *
 * Purpose: adds host availability update to sql statement                    *
 *                                                                            *
 * Parameters: sql        - [IN/OUT] the sql statement                        *
 *             sql_alloc  - [IN/OUT] the number of bytes allocated for sql    *
 *                                   statement                                *
 *             sql_offset - [IN/OUT] the number of bytes used in sql          *
 *                                   statement                                *
 *             ha           [IN] the host availability data                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是更新主机可用性数据到数据库。函数接收四个参数，其中 `ha` 是指向主机可用性数据的指针。函数首先检查主机可用性数据是否已设置，如果已设置，则开始拼接SQL语句。遍历不同类型的代理，设置其可用性、错误信息、错误来源和禁用至时间等字段。最后设置主机ID，并返回成功。
 ******************************************************************************/
// 定义一个函数，用于更新主机可用性数据到数据库
int zbx_sql_add_host_availability(char **sql, size_t *sql_alloc, size_t *sql_offset,
                                   const zbx_host_availability_t *ha)
{
	// 定义一个字符串数组，用于存储不同代理类型的前缀，如 snmp_、ipmi_、jmx_等
	const char *field_prefix[ZBX_AGENT_MAX] = {"", "snmp_", "ipmi_", "jmx_"};
	char		delim = ' ';
	int		i;

	// 检查主机可用性数据是否已设置
	if (FAIL == zbx_host_availability_is_set(ha))
		return FAIL;

	// 拼接SQL语句，更新主机的可用性数据
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "update hosts set");

	// 遍历代理类型
	for (i = 0; i < ZBX_AGENT_MAX; i++)
	{
		// 如果代理状态为可用
		if (0 != (ha->agents[i].flags & ZBX_FLAGS_AGENT_STATUS_AVAILABLE))
		{
			// 拼接SQL语句，设置代理可用性
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%savailable=%d", delim, field_prefix[i],
			                (int)ha->agents[i].available);
			delim = ',';
		}

		// 如果代理状态为错误
		if (0 != (ha->agents[i].flags & ZBX_FLAGS_AGENT_STATUS_ERROR))
		{
			// 拼接SQL语句，设置代理错误信息
			char	*error_esc;

			error_esc = DBdyn_escape_field("hosts", "error", ha->agents[i].error);
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%serror='%s'", delim, field_prefix[i],
			                error_esc);
			zbx_free(error_esc);
			delim = ',';
		}

		// 如果代理状态包含错误信息
		if (0 != (ha->agents[i].flags & ZBX_FLAGS_AGENT_STATUS_ERRORS_FROM))
		{
			// 拼接SQL语句，设置代理错误来源
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%serrors_from=%d", delim, field_prefix[i],
			                ha->agents[i].errors_from);
			delim = ',';
		}

		// 如果代理状态禁用至指定时间
		if (0 != (ha->agents[i].flags & ZBX_FLAGS_AGENT_STATUS_DISABLE_UNTIL))
		{
			// 拼接SQL语句，设置代理禁用至时间
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%c%sdisable_until=%d", delim, field_prefix[i],
			                ha->agents[i].disable_until);
			delim = ',';
		}
	}

	// 拼接SQL语句，设置主机ID
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " where hostid=" ZBX_FS_UI64, ha->hostid);

	// 返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: DBget_user_by_active_session                                     *
 *                                                                            *
 * Purpose: validate that session is active and get associated user data      *
 *                                                                            *
 * Parameters: sessionid - [IN] the session id to validate                    *
 *             user      - [OUT] user information                             *
 *                                                                            *
 * Return value:  SUCCEED - session is active and user data was retrieved     *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个active session中获取对应的用户信息，并将用户ID和类型存储在`zbx_user_t`结构体指针所指向的内存空间中。函数输入参数为一个sessionid字符串和一个`zbx_user_t`结构体指针，输出为一个布尔值，表示查询是否成功。在函数内部，首先对sessionid进行转义，然后执行SQL查询语句，从数据库中获取用户信息。接着将查询结果中的用户ID和类型转换为uint64和整数类型，并存储在`zbx_user_t`结构体中。最后，释放内存并返回查询结果。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "DBget_user_by_active_session";
int	LOG_LEVEL_DEBUG = 1;

// 定义函数原型
int DBget_user_by_active_session(const char *sessionid, zbx_user_t *user);

// 初始化变量
char		*sessionid_esc;
int		ret = FAIL;
DB_RESULT	result;
DB_ROW		row;

// 记录日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s() sessionid:%s", __function_name, sessionid);

// 对sessionid进行转义，防止SQL注入
sessionid_esc = DBdyn_escape_string(sessionid);

// 执行SQL查询，从数据库中获取用户信息
if (NULL == (result = DBselect(
    "select u.userid,u.type"
        " from sessions s,users u"
    " where s.userid=u.userid"
        " and s.sessionid='%s'"
        " and s.status=%d",
    sessionid_esc, ZBX_SESSION_ACTIVE)))
{
    // 如果查询失败，跳转到out标签处
    goto out;
}

// 从查询结果中获取一行数据
if (NULL == (row = DBfetch(result)))
{
    // 如果数据获取失败，跳转到out标签处
    goto out;
}

// 将查询结果中的用户ID和类型转换为uint64和整数类型
ZBX_STR2UINT64(user->userid, row[0]);
user->type = atoi(row[1]);

// 标记查询成功，将ret变量设置为SUCCEED
ret = SUCCEED;

out:
    // 释放查询结果
    DBfree_result(result);
    // 释放sessionid_esc内存
    zbx_free(sessionid_esc);

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回查询结果
    return ret;
}

