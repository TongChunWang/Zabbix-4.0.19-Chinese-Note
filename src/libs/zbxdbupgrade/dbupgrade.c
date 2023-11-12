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

#include "db.h"
#include "log.h"
#include "sysinfo.h"
#include "zbxdbupgrade.h"
#include "dbupgrade.h"

typedef struct
{
	zbx_dbpatch_t	*patches;
	const char	*description;
}
zbx_db_version_t;

#ifdef HAVE_MYSQL
#	define ZBX_DB_TABLE_OPTIONS	" engine=innodb"
#	define ZBX_DROP_FK		" drop foreign key"
#else
#	define ZBX_DB_TABLE_OPTIONS	""
#	define ZBX_DROP_FK		" drop constraint"
#endif

#if defined(HAVE_IBM_DB2)
#	define ZBX_DB_ALTER_COLUMN	" alter column"
#elif defined(HAVE_POSTGRESQL)
#	define ZBX_DB_ALTER_COLUMN	" alter"
#else
#	define ZBX_DB_ALTER_COLUMN	" modify"
#endif

#if defined(HAVE_IBM_DB2)
#	define ZBX_DB_SET_TYPE		" set data type"
#elif defined(HAVE_POSTGRESQL)
#	define ZBX_DB_SET_TYPE		" type"
#else
#	define ZBX_DB_SET_TYPE		""
#endif

/* NOTE: Do not forget to sync changes in ZBX_TYPE_*_STR defines for Oracle with zbx_oracle_column_type()! */

#if defined(HAVE_IBM_DB2) || defined(HAVE_POSTGRESQL)
#	define ZBX_TYPE_ID_STR		"bigint"
#elif defined(HAVE_MYSQL)
#	define ZBX_TYPE_ID_STR		"bigint unsigned"
#elif defined(HAVE_ORACLE)
#	define ZBX_TYPE_ID_STR		"number(20)"
#endif

#ifdef HAVE_ORACLE
#	define ZBX_TYPE_INT_STR		"number(10)"
#	define ZBX_TYPE_CHAR_STR	"nvarchar2"
#else
#	define ZBX_TYPE_INT_STR		"integer"
#	define ZBX_TYPE_CHAR_STR	"varchar"
#endif

#if defined(HAVE_IBM_DB2)
#	define ZBX_TYPE_FLOAT_STR	"decfloat(16)"
#	define ZBX_TYPE_UINT_STR	"bigint"
#elif defined(HAVE_MYSQL)
#	define ZBX_TYPE_FLOAT_STR	"double(16,4)"
#	define ZBX_TYPE_UINT_STR	"bigint unsigned"
#elif defined(HAVE_ORACLE)
#	define ZBX_TYPE_FLOAT_STR	"number(20,4)"
#	define ZBX_TYPE_UINT_STR	"number(20)"
#elif defined(HAVE_POSTGRESQL)
#	define ZBX_TYPE_FLOAT_STR	"numeric(16,4)"
#	define ZBX_TYPE_UINT_STR	"numeric(20)"
#endif

#if defined(HAVE_IBM_DB2)
#	define ZBX_TYPE_SHORTTEXT_STR	"varchar(2048)"
#elif defined(HAVE_ORACLE)
#	define ZBX_TYPE_SHORTTEXT_STR	"nvarchar2(2048)"
#else
#	define ZBX_TYPE_SHORTTEXT_STR	"text"
#endif

#if defined(HAVE_IBM_DB2)
#	define ZBX_TYPE_TEXT_STR	"varchar(2048)"
#elif defined(HAVE_ORACLE)
#	define ZBX_TYPE_TEXT_STR	"nclob"
#else
#	define ZBX_TYPE_TEXT_STR	"text"
#endif

#define ZBX_FIRST_DB_VERSION		2010000

extern unsigned char	program_type;


#ifndef HAVE_SQLITE3
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的 ZBX_FIELD 结构体中的 type 成员，将相应的数据类型转换为字符串，并将这些字符串拼接在一起。这个过程通过 switch 语句进行分支处理，根据不同的数据类型调用不同的字符串处理函数（如 zbx_strcpy_alloc 或 zbx_snprintf_alloc），并将结果存储在 sql 指向的内存区域。最后，输出整个拼接好的字符串。
 ******************************************************************************/
// 定义一个名为 DBfield_type_string 的静态函数，该函数接收四个参数：
// 第一个参数是一个指向字符指针的指针（char **sql），用于存储 SQL 语句；
// 第二个参数是一个指向 size_t 类型的指针（size_t *sql_alloc），用于存储分配给 sql 的内存大小；
// 第三个参数是一个指向 size_t 类型的指针（size_t *sql_offset），用于记录当前 sql 指针的位置；
// 第四个参数是一个指向 ZBX_FIELD 类型的指针（const ZBX_FIELD *field），用于表示一个数据字段。
static void DBfield_type_string(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_FIELD *field)
{
	// 使用 switch 语句根据 field 的类型进行分支处理：
	switch (field->type)
	{
		// 如果是 ZBX_TYPE_ID 类型，则执行以下操作：
		case ZBX_TYPE_ID:
		{
			// 使用 zbx_strcpy_alloc 函数将 ZBX_TYPE_ID_STR 字符串复制到 sql 指向的内存区域，
			// 并返回分配的内存大小，同时更新 sql_alloc 和 sql_offset。
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_ID_STR);
			break;
		}
		// 如果是 ZBX_TYPE_INT 类型，则执行以下操作：
		case ZBX_TYPE_INT:
		{
			// 使用 zbx_strcpy_alloc 函数将 ZBX_TYPE_INT_STR 字符串复制到 sql 指向的内存区域，
			// 并返回分配的内存大小，同时更新 sql_alloc 和 sql_offset。
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_INT_STR);
			break;
		}
		// 如果是 ZBX_TYPE_CHAR 类型，则执行以下操作：
		case ZBX_TYPE_CHAR:
		{
			// 使用 zbx_snprintf_alloc 函数将字符串 "%s(%hu)" 格式化输出到 sql 指向的内存区域，
			// 其中 %s 表示 ZBX_TYPE_CHAR_STR 字符串，%hu 表示 field->length，
			// 并返回分配的内存大小，同时更新 sql_alloc 和 sql_offset。
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s(%hu)", ZBX_TYPE_CHAR_STR, field->length);
			break;
		}
		// 如果是 ZBX_TYPE_FLOAT 类型，则执行以下操作：
		case ZBX_TYPE_FLOAT:
		{
			// 使用 zbx_strcpy_alloc 函数将 ZBX_TYPE_FLOAT_STR 字符串复制到 sql 指向的内存区域，
			// 并返回分配的内存大小，同时更新 sql_alloc 和 sql_offset。
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_FLOAT_STR);
			break;
		}
		// 如果是 ZBX_TYPE_UINT 类型，则执行以下操作：
		case ZBX_TYPE_UINT:
		{
			// 使用 zbx_strcpy_alloc 函数将 ZBX_TYPE_UINT_STR 字符串复制到 sql 指向的内存区域，
			// 并返回分配的内存大小，同时更新 sql_alloc 和 sql_offset。
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_UINT_STR);
			break;
		}
		// 如果是 ZBX_TYPE_SHORTTEXT 类型，则执行以下操作：
		case ZBX_TYPE_SHORTTEXT:
		{
			// 使用 zbx_strcpy_alloc 函数将 ZBX_TYPE_SHORTTEXT_STR 字符串复制到 sql 指向的内存区域，
			// 并返回分配的内存大小，同时更新 sql_alloc 和 sql_offset。
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_SHORTTEXT_STR);
			break;
		}
		// 如果是 ZBX_TYPE_TEXT 类型，则执行以下操作：
		case ZBX_TYPE_TEXT:
		{
			// 使用 zbx_strcpy_alloc 函数将 ZBX_TYPE_TEXT_STR 字符串复制到 sql 指向的内存区域，
			// 并返回分配的内存大小，同时更新 sql_alloc 和 sql_offset。
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_TEXT_STR);
			break;
		}
		// 如果是其他类型，则执行以下操作：
		default:
		{
			// 抛出一个断言，表示非法的类型。
			assert(0);
		}
	}
}


#ifdef HAVE_ORACLE
typedef enum
{
	ZBX_ORACLE_COLUMN_TYPE_NUMERIC,
	ZBX_ORACLE_COLUMN_TYPE_CHARACTER,
	ZBX_ORACLE_COLUMN_TYPE_UNKNOWN
}
zbx_oracle_column_type_t;

/******************************************************************************
 *                                                                            *
 * Function: zbx_oracle_column_type                                           *
 *                                                                            *
 * Purpose: determine whether column type is character or numeric             *
 *                                                                            *
 * Parameters: field_type - [IN] column type in Zabbix definitions            *
 *                                                                            *
 * Return value: column type (character/raw, numeric) in Oracle definitions   *
 *                                                                            *
 * Comments: The size of a character or raw column or the precision of a      *
 *           numeric column can be changed, whether or not all the rows       *
 *           contain nulls. Otherwise in order to change the datatype of a    *
 *           column all rows of the column must contain nulls.                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据输入的 field_type 值，判断并返回相应的 Oracle 列类型。输出结果如下：
 *
 *```
 *当 field_type 为 ZBX_TYPE_ID 时，返回 ZBX_ORACLE_COLUMN_TYPE_NUMERIC
 *当 field_type 为 ZBX_TYPE_INT 时，返回 ZBX_ORACLE_COLUMN_TYPE_NUMERIC
 *当 field_type 为 ZBX_TYPE_FLOAT 时，返回 ZBX_ORACLE_COLUMN_TYPE_NUMERIC
 *当 field_type 为 ZBX_TYPE_UINT 时，返回 ZBX_ORACLE_COLUMN_TYPE_NUMERIC
 *当 field_type 为 ZBX_TYPE_CHAR 时，返回 ZBX_ORACLE_COLUMN_TYPE_CHARACTER
 *当 field_type 为 ZBX_TYPE_SHORTTEXT 时，返回 ZBX_ORACLE_COLUMN_TYPE_CHARACTER
 *当 field_type 为 ZBX_TYPE_TEXT 时，返回 ZBX_ORACLE_COLUMN_TYPE_CHARACTER
 *当 field_type 为其他值时，返回 ZBX_ORACLE_COLUMN_TYPE_UNKNOWN
 *```
 ******************************************************************************/
// 定义一个名为 zbx_oracle_column_type 的静态函数，该函数接收一个 unsigned char 类型的参数 field_type，并返回一个 zbx_oracle_column_type_t 类型的值
static zbx_oracle_column_type_t zbx_oracle_column_type(unsigned char field_type)
{
    // 使用 switch 语句根据 field_type 的值进行分支处理
    switch (field_type)
/******************************************************************************
 * *
 *整个代码块的主要目的是生成数据库字段定义的SQL语句。具体来说，它会根据给定的字段信息（包括字段名、字段类型、默认值和约束等），生成如下格式的SQL语句：
 *
 *```
 *字段类型 字段名
 *```
 *
 *如果字段有默认值，还会生成默认值的SQL语句：
 *
 *```
 *default '默认值'
 *```
 *
 *同时，如果字段有NOT NULL约束，还会生成NOT NULL约束的SQL语句：
 *
 *```
 *not null
 *```
 *
 *最后，将这些生成的SQL语句拼接在一起，形成完整的字段定义SQL语句。
 ******************************************************************************/
// 定义一个静态函数，用于生成数据库字段定义的SQL语句
static void DBfield_definition_string(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_FIELD *field)
{
	// 使用zbx_snprintf_alloc函数生成字段名的SQL语句，格式为 "字段类型 字段名"
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ZBX_FS_SQL_NAME " ", field->name);

	// 调用DBfield_type_string函数，将字段类型转换为SQL语句中的字段类型，并追加到缓冲区
	DBfield_type_string(sql, sql_alloc, sql_offset, field);

	// 如果字段有默认值，则生成默认值的SQL语句
	if (NULL != field->default_value)
	{
		char	*default_value_esc;

#if defined(HAVE_MYSQL)
		// 根据字段类型判断是否可以设置默认值
		switch (field->type)
		{
			case ZBX_TYPE_BLOB:
			case ZBX_TYPE_TEXT:
			case ZBX_TYPE_SHORTTEXT:
			case ZBX_TYPE_LONGTEXT:
				/* MySQL: BLOB and TEXT columns cannot be assigned a default value */
				break;
			default:
#endif
				// 对默认值进行转义，并生成SQL语句
				default_value_esc = DBdyn_escape_string(field->default_value);
				zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " default '%s'", default_value_esc);
				zbx_free(default_value_esc);
#if defined(HAVE_MYSQL)
		}
#endif
	}

	// 如果字段有NOT NULL约束，则生成NOT NULL约束的SQL语句
	if (0 != (field->flags & ZBX_NOTNULL))
	{
#if defined(HAVE_ORACLE)
		// 根据字段类型判断是否可以设置NOT NULL约束
		switch (field->type)
		{
			case ZBX_TYPE_INT:
			case ZBX_TYPE_FLOAT:
			case ZBX_TYPE_BLOB:
			case ZBX_TYPE_UINT:
			case ZBX_TYPE_ID:
				zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
				break;
			default:	/* ZBX_TYPE_CHAR, ZBX_TYPE_TEXT, ZBX_TYPE_SHORTTEXT or ZBX_TYPE_LONGTEXT */
				/* nothing to do */;
		}
#else
		// 其他情况，直接生成NOT NULL约束的SQL语句
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
#endif
	}
}

			case ZBX_TYPE_SHORTTEXT:
			case ZBX_TYPE_LONGTEXT:
				/* MySQL: BLOB and TEXT columns cannot be assigned a default value */
				break;
			default:
#endif
				default_value_esc = DBdyn_escape_string(field->default_value);
				zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " default '%s'", default_value_esc);
				zbx_free(default_value_esc);
#if defined(HAVE_MYSQL)
		}
#endif
	}

	if (0 != (field->flags & ZBX_NOTNULL))
	{
#if defined(HAVE_ORACLE)
		switch (field->type)
		{
			case ZBX_TYPE_INT:
			case ZBX_TYPE_FLOAT:
			case ZBX_TYPE_BLOB:
			case ZBX_TYPE_UINT:
			case ZBX_TYPE_ID:
				zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
				break;
			default:	/* ZBX_TYPE_CHAR, ZBX_TYPE_TEXT, ZBX_TYPE_SHORTTEXT or ZBX_TYPE_LONGTEXT */
				/* nothing to do */;
		}
#else
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
#endif
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是生成一个创建表的 SQL 语句。首先，使用 zbx_snprintf_alloc 函数生成一个模板 SQL 语句，然后遍历表中的字段，逐个生成字段定义 SQL 语句，并将它们添加到模板中。如果表中有主键字段，则在 SQL 语句中添加主键约束语句。最后，添加一个右括号、一个逗号和一个换行符，完成 SQL 语句的生成。整个过程通过指针参数传递表信息，最终将生成的 SQL 语句存储在 sql 数组中。
 ******************************************************************************/
// 定义一个名为 DBcreate_table_sql 的静态函数，输入参数为一个字符指针数组（用于存储 SQL 语句）、一个 size_t 类型的指针（用于记录 SQL 分配的大小）、一个 size_t 类型的指针（用于记录 SQL 偏移量）、一个 ZBX_TABLE 类型的指针（用于存储表信息）
static void DBcreate_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_TABLE *table)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 使用 zbx_snprintf_alloc 函数生成 SQL 语句，模板为 "create table 表名 （"，并将表名替换为实参 table->table
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "create table %s (\
", table->table);

	// 使用 for 循环遍历表中的字段，直到表尾
	for (i = 0; NULL != table->fields[i].name; i++)
	{
		// 如果当前字段不是第一个，则在 SQL 语句中添加一个逗号和一个换行符
		if (0 != i)
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ",\
");
		// 调用 DBfield_definition_string 函数，生成字段定义 SQL 语句，并将结果存储在 sql 数组中
		DBfield_definition_string(sql, sql_alloc, sql_offset, &table->fields[i]);
	}
	// 如果表中有主键字段，则在 SQL 语句中添加一个逗号和一个换行符，然后添加主键约束语句
	if ('\0' != *table->recid)
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ",\
primary key (%s)", table->recid);

	// 在 SQL 语句末尾添加一个右括号、一个逗号和一个换行符
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "\
)" ZBX_DB_TABLE_OPTIONS);
}


/******************************************************************************
 * *
 *代码块主要目的是：修改数据库表名。该函数接收5个参数，分别是：
 *
 *1. sql：指向字符指针数组的指针，用于存储生成的 SQL 语句；
 *2. sql_alloc：指向 size_t 类型的指针，用于存储数组分配的大小；
 *3. sql_offset：指向 size_t 类型的指针，用于存储当前 SQL 语句在数组中的偏移量；
 *4. table_name：指向 const char 类型的指针，用于存储原表名；
 *5. new_name：指向 const char 类型的指针，用于存储新表名。
 *
 *根据不同的数据库类型（HAVE_IBM_DB2 标志存在时使用 IBM DB2，否则使用其他数据库），生成相应的 SQL 语句来执行表名修改操作。最终将生成的 SQL 语句存储在 sql 数组中，供后续操作使用。
 ******************************************************************************/
// 定义一个名为 DBrename_table_sql 的静态函数，该函数用于修改数据库表名
static void DBrename_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name,
                             const char *new_name)
{
    // 使用预处理语句（Prepared Statement）来执行修改表名的操作
#ifdef HAVE_IBM_DB2
    // 使用 zbx_snprintf_alloc 函数生成 SQL 语句，将表名从 table_name 修改为 new_name
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename table " ZBX_FS_SQL_NAME " to " ZBX_FS_SQL_NAME,
                      table_name, new_name);
#else
    // 使用 zbx_snprintf_alloc 函数生成 SQL 语句，将表名从 table_name 修改为 new_name
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME " rename to " ZBX_FS_SQL_NAME,
                      table_name, new_name);
#endif
}


/******************************************************************************
 * *
 *这块代码的主要目的是生成一条 SQL 语句，用于删除指定的表。函数接收四个参数：
 *
 *1. `sql`：指向指针的指针，用于存储生成的 SQL 语句。
/******************************************************************************
 * *
 *整个代码块的主要目的是设置数据库表中字段的默认值。根据不同的数据库类型（MySQL 或 Oracle），生成相应的 SQL 语句来实现字段默认值的设置。输出为一个字符串，包含设置默认值的 SQL 语句。
 ******************************************************************************/
// 定义一个名为 DBset_default_sql 的静态函数，该函数用于设置数据库字段的默认值
static void DBset_default_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                            const char *table_name, const ZBX_FIELD *field)
{
    // 使用 zbx_snprintf_alloc 函数生成一个 SQL 语句，设置表的字段默认值
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DB_ALTER_COLUMN " ", table_name);

    // 判断当前系统是否支持 MySQL 数据库
    #if defined(HAVE_MYSQL)
        // 如果支持 MySQL，则调用 DBfield_definition_string 函数，将字段定义添加到 SQL 语句中
        DBfield_definition_string(sql, sql_alloc, sql_offset, field);
    #elif defined(HAVE_ORACLE)  // 如果不支持 MySQL，则判断系统是否支持 Oracle 数据库
        // 如果支持 Oracle，则使用 zbx_snprintf_alloc 函数生成一个 SQL 语句，设置字段默认值
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s default '%s'", field->name, field->default_value);
    #else  // 如果不支持 MySQL 和 Oracle，则使用通用的设置默认值 SQL 语句
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s set default '%s'", field->name, field->default_value);
    #endif
}


/******************************************************************************
 * *
 *整个代码块的主要目的是修改数据库表中的字段类型。根据不同的数据库类型（MYSQL或POSTGRESQL），生成相应的SQL语句来完成字段类型的修改。其中，涉及到以下几个步骤：
 *
 *1. 使用zbx_snprintf_alloc函数生成一个修改表字段类型的SQL语句，分配内存给sql指针，并计算偏移量。
 *2. 判断是否支持MYSQL数据库，如果支持，则调用DBfield_definition_string函数生成字段定义字符串，并将其添加到SQL语句中。
 *3. 如果不支持MYSQL，则生成字段类型修改语句，并添加到SQL语句中。
 *4. 判断是否支持POSTGRESQL数据库，如果支持，且字段有默认值，则生成默认值设置语句，并添加到SQL语句中。
 *
 *最终，将生成的SQL语句作为参数传递给调用函数，完成表字段类型的修改。
 ******************************************************************************/
// 定义一个函数，用于修改数据库表字段的类型
static void DBmodify_field_type_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                                  const char *table_name, const ZBX_FIELD *field)
{
    // 使用zbx_snprintf_alloc函数生成一个修改表字段类型的SQL语句，分配内存给sql指针，并计算偏移量
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME ZBX_DB_ALTER_COLUMN " ",
                      table_name);

    // 判断是否支持MYSQL数据库
    #ifdef HAVE_MYSQL
        // 使用DBfield_definition_string函数生成字段定义字符串，并将其添加到SQL语句中
        DBfield_definition_string(sql, sql_alloc, sql_offset, field);
    #else
        // 如果不支持MYSQL，则使用以下方式生成字段类型修改语句
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s" ZBX_DB_SET_TYPE " ", field->name);
        DBfield_type_string(sql, sql_alloc, sql_offset, field);

        // 判断是否支持POSTGRESQL数据库
        #ifdef HAVE_POSTGRESQL
            // 如果字段有默认值，则生成默认值设置语句
            if (NULL != field->default_value)
            {
                zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\
");
                DBset_default_sql(sql, sql_alloc, sql_offset, table_name, field);
            }
        #endif
    #endif
}

#ifdef HAVE_MYSQL
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s" ZBX_DB_SET_TYPE " ", field->name);
	DBfield_type_string(sql, sql_alloc, sql_offset, field);
#ifdef HAVE_POSTGRESQL
	if (NULL != field->default_value)
	{
/******************************************************************************
 * *
 *整个代码块的主要目的是修改表结构，通过拼接 SQL 语句来添加一个新的字段。该函数接收表名和字段信息作为参数，生成一个修改表结构的 SQL 语句，并将字段信息添加到 SQL 语句中。最后，将生成的 SQL 语句存储在传入的字符数组中。
 ******************************************************************************/
// 定义一个名为 DBadd_field_sql 的静态函数，该函数接收四个参数：
// 第一个参数是一个指针，指向一个字符数组，该数组用于存储 SQL 语句；
// 第二个参数是一个指针，指向一个 size_t 类型的变量，用于记录 SQL 语句的长度；
// 第三个参数是一个指针，指向一个 size_t 类型的变量，用于记录 SQL 语句的偏移量；
// 第四个参数是一个指向 const char 类型的指针，用于存储表名；
// 第五个参数是一个指向 const ZBX_FIELD 类型的指针，用于存储字段信息。
static void DBadd_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                          const char *table_name, const ZBX_FIELD *field)
{
    // 使用 zbx_snprintf_alloc 函数生成一个修改表结构的 SQL 语句，
    // 该函数会将表名和字段信息拼接到 SQL 语句中，并分配内存存储该 SQL 语句。
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME " add ", table_name);
    
    // 调用 DBfield_definition_string 函数，将字段信息添加到 SQL 语句中。
    // 该函数会使用指针参数 sql、sql_alloc 和 sql_offset 来接收和存储拼接后的 SQL 语句。
    DBfield_definition_string(sql, sql_alloc, sql_offset, field);
}


/******************************************************************************
 * *
 *代码块主要目的是：根据给定的表名和字段信息，生成一个SQL语句，将指定表中的某个字段设置为非空。根据不同的数据库类型（MySQL或Oracle），生成的SQL语句略有不同。最后，将生成的SQL语句存储在分配的内存中。
 ******************************************************************************/
/* 定义一个函数，用于生成SQL语句，将指定表中的某个字段设置为非空 */
static void DBdrop_not_null_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                              const char *table_name, const ZBX_FIELD *field)
{
    // 分配内存，用于存储生成的SQL语句
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DB_ALTER_COLUMN " ", table_name);

    /* 根据不同的数据库类型，生成相应的SQL语句 */
    #if defined(HAVE_MYSQL)
        DBfield_definition_string(sql, sql_alloc, sql_offset, field);
    #elif defined(HAVE_ORACLE)
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s null", field->name);
    #else
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s drop not null", field->name);
    #endif
}


/******************************************************************************
 * *
 *整个代码块的主要目的是设置数据库表中某个字段不能为空。根据不同的数据库系统（MySQL、Oracle或其他），生成相应的SQL语句。对于MySQL系统，调用DBfield_definition_string函数生成字段定义SQL语句；对于Oracle系统，直接设置字段为not null；对于其他系统，设置字段为set not null。
 ******************************************************************************/
// 定义一个静态函数，用于设置数据库表中某个字段不能为空
static void DBset_not_null_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                             const char *table_name, const ZBX_FIELD *field)
{
    // 使用zbx_snprintf_alloc函数生成一个SQL语句，设置表名为table_name的字段不能为空
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DB_ALTER_COLUMN " ", table_name);

    // 根据系统类型（MySQL或Oracle）生成不同的SQL语句
#if defined(HAVE_MYSQL)
    // 对于MySQL系统，调用DBfield_definition_string函数生成字段定义SQL语句
    DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
    // 对于Oracle系统，直接设置字段为not null
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s not null", field->name);
#else
    // 对于其他系统，设置字段为set not null
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s set not null", field->name);
/******************************************************************************
 * *
 *这块代码的主要目的是用于修改数据库表的结构，具体包括更改列名和字段定义。其中，`DBrename_field_sql` 函数接受五个参数：
 *
 *1. `sql`：指向 SQL 语句指针的指针，用于存储生成的 SQL 语句。
 *2. `sql_alloc`：指向分配给 `sql` 指针的空间大小。
 *3. `sql_offset`：指向当前 SQL 语句的偏移量，用于计算生成的 SQL 语句长度。
 *4. `table_name`：要修改的表名。
 *5. `field_name`：要更改的列名。
 *6. `field`：包含字段信息的结构体指针。
 *
 *根据是否支持 MySQL，分别生成不同的 SQL 语句来实现列名的更改。如果不支持 MySQL，则直接使用 `rename column` 语句更改列名。如果支持 MySQL，还需要生成字段定义字符串，并将其添加到 SQL 语句中。整个函数的目的是为了实现数据库表结构的修改。
 ******************************************************************************/
// 定义一个名为 DBrename_field_sql 的静态函数，参数为指针类型
static void DBrename_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                             const char *table_name, const char *field_name, const ZBX_FIELD *field)
{
    // 使用 zbx_snprintf_alloc 函数生成一个 SQL 语句，用于修改表结构
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME " ", table_name);

    // 判断是否支持 MySQL，如果不支持，则执行以下操作：
#ifdef HAVE_MYSQL
    // 在 SQL 语句中添加一行，用于更改列名
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "change column " ZBX_FS_SQL_NAME " ", field_name);
    // 生成字段定义字符串，并将其添加到 SQL 语句中
    DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#else
    // 如果不支持 MySQL，则执行以下操作：
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename column " ZBX_FS_SQL_NAME " to " ZBX_FS_SQL_NAME,
                      field_name, field->name);
#endif
}

{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME " ", table_name);

#ifdef HAVE_MYSQL
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "change column " ZBX_FS_SQL_NAME " ", field_name);
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename column " ZBX_FS_SQL_NAME " to " ZBX_FS_SQL_NAME,
			field_name, field->name);
#endif
}

/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为 DBdrop_field_sql 的函数，该函数接收五个参数：
 *
 *1. 一个指向 char 类型数组的指针 sql，用于存储生成的 SQL 语句；
 *2. 一个 size_t 类型的指针 sql_alloc，用于存储数组的分配大小；
/******************************************************************************
 * *
 *这块代码的主要目的是生成一个创建索引的SQL语句。函数接受5个参数，分别是：
 *
 *1. `sql`：指向存储SQL语句的指针，用于接收生成后的SQL语句。
 *2. `sql_alloc`：指向存储SQL语句的内存分配大小，用于控制内存分配。
 *3. `sql_offset`：指向当前SQL语句的偏移量，用于计算字符串长度。
 *4. `table_name`：表名，用于创建索引的表。
 *5. `index_name`：索引名，用于创建的索引。
 *6. `fields`：索引字段，用于指定索引的字段。
 *7. `unique`：布尔值，表示是否创建唯一索引。如果为1，则创建唯一索引；如果为0，则创建普通索引。
 *
 *函数内部首先分配内存用于存储SQL语句，然后根据`unique`参数判断是否需要添加\" unique\"字符串，最后使用`zbx_snprintf_alloc`函数生成索引创建语句并存储在分配的内存中。
 ******************************************************************************/
// 定义一个函数，用于生成创建索引的SQL语句
static void	DBcreate_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *index_name, const char *fields, int unique)
{
	// 分配内存，准备存储SQL语句
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "create");
/******************************************************************************
 * *
 *这块代码的主要目的是根据不同的数据库类型（IBM DB2、MySQL、Oracle、PostgreSQL），构造相应的SQL语句来重命名索引。具体操作如下：
 *
 *1. 对于IBM DB2，直接构造SQL语句，重命名索引。
 *2. 对于MySQL，先创建一个新的索引（使用DBcreate_index_sql函数），然后删除旧的索引（使用DBdrop_index_sql函数）。
 *3. 对于Oracle和PostgreSQL，使用ALTER INDEX语句重命名索引。
 *
 *代码中使用了预处理指令（#if、#elif、#endif）来根据不同的数据库类型选择执行不同的操作。同时，使用了ZBX_UNUSED宏来忽略一些不必要的参数。在整个过程中，还使用了zbx_snprintf_alloc、zbx_strcpy_alloc等函数来处理字符串操作。
 ******************************************************************************/
static void	DBrename_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name,
		const char *old_name, const char *new_name, const char *fields, int unique)
{
    // 定义条件，判断使用的是哪种数据库
#if defined(HAVE_IBM_DB2)
    ZBX_UNUSED(table_name);
    ZBX_UNUSED(fields);
    ZBX_UNUSED(unique);
    // 对于IBM DB2，直接构造SQL语句，重命名索引
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename index %s to %s", old_name, new_name);
#elif defined(HAVE_MYSQL)
    // 对于MySQL，先创建一个新的索引，然后删除旧的索引
    DBcreate_index_sql(sql, sql_alloc, sql_offset, table_name, new_name, fields, unique);
    zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\
");
    DBdrop_index_sql(sql, sql_alloc, sql_offset, table_name, old_name);
    zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\
");
#elif defined(HAVE_ORACLE) || defined(HAVE_POSTGRESQL)
    ZBX_UNUSED(table_name);
    ZBX_UNUSED(fields);
    ZBX_UNUSED(unique);
    // 对于Oracle和PostgreSQL，使用ALTER INDEX语句重命名索引
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter index %s rename to %s", old_name, new_name);
#endif
}

	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "create");
	if (0 != unique)
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " unique");
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " index %s on %s (%s)", index_name, table_name, fields);
}

/******************************************************************************
 * *
 *这块代码的主要目的是生成一个用于删除索引的 SQL 语句。首先，使用 `zbx_snprintf_alloc` 函数生成一个基本的 SQL 语句，例如 \"drop index\"，然后根据是否支持 MySQL 语法，添加 \"on\" 关键字和表名。最后，将生成的 SQL 语句存储在 `sql` 指针所指向的内存空间中。
 ******************************************************************************/
/* 定义一个名为 DBdrop_index_sql 的静态函数，该函数用于删除索引 */
static void DBdrop_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                           const char *table_name, const char *index_name)
{
    // 使用 zbx_snprintf_alloc 函数生成一个 SQL 语句，用于删除索引
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "drop index %s", index_name);

    /* 判断是否支持 MySQL 语法，如果支持，则生成完整的 SQL 语句 */
#ifdef HAVE_MYSQL
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " on %s", table_name);
#else
    /* 如果不需要支持 MySQL 语法，则忽略 table_name 参数 */
    ZBX_UNUSED(table_name);
#endif
}


static void	DBrename_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name,
		const char *old_name, const char *new_name, const char *fields, int unique)
{
#if defined(HAVE_IBM_DB2)
	ZBX_UNUSED(table_name);
	ZBX_UNUSED(fields);
	ZBX_UNUSED(unique);
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename index %s to %s", old_name, new_name);
#elif defined(HAVE_MYSQL)
	DBcreate_index_sql(sql, sql_alloc, sql_offset, table_name, new_name, fields, unique);
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
/******************************************************************************
 * *
 *整个代码块的主要目的是生成一个添加外键约束的SQL语句。输入参数包括表名、记录ID、待添加的外键字段信息。生成的SQL语句将包含表名、外键名、关联字段名，以及可能需要的on delete cascade约束。
 ******************************************************************************/
// 定义一个函数，用于生成添加外键约束的SQL语句
static void DBadd_foreign_key_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                                const char *table_name, int id, const ZBX_FIELD *field)
{
    // 使用zbx_snprintf_alloc函数生成SQL语句，参数如下：
    // sql：指向字符指针数组的指针，用于存储生成的SQL语句
    // sql_alloc：指向分配给sql数组的内存大小，用于控制内存分配
    // sql_offset：指向当前待填充的SQL语句字符串的位置
    // table_name：表名
    // id：待添加的外键关联的记录ID
    // field：待添加的外键字段信息

    // 生成SQL语句模板： alter table 表名 add constraint 外键名 foreign key (表名) references 表名 (字段名)
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
                        "alter table " ZBX_FS_SQL_NAME " add constraint c_%s_%d foreign key (" ZBX_FS_SQL_NAME ")"
                                " references " ZBX_FS_SQL_NAME " (" ZBX_FS_SQL_NAME ")", table_name, table_name,
                                id, field->name, field->fk_table, field->fk_field);

    // 判断字段是否需要添加on delete cascade约束，如果需要，则添加
    if (0 != (field->fk_flags & ZBX_FK_CASCADE_DELETE))
    {
        // 使用zbx_strcpy_alloc函数将on delete cascade添加到SQL语句末尾
        zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " on delete cascade");
    }
}

static void	DBadd_foreign_key_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, int id, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
			"alter table " ZBX_FS_SQL_NAME " add constraint c_%s_%d foreign key (" ZBX_FS_SQL_NAME ")"
					" references " ZBX_FS_SQL_NAME " (" ZBX_FS_SQL_NAME ")", table_name, table_name,
					id, field->name, field->fk_table, field->fk_field);
	if (0 != (field->fk_flags & ZBX_FK_CASCADE_DELETE))
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " on delete cascade");
}

/******************************************************************************
 * *
 *这块代码的主要目的是生成一个 SQL 语句，用于删除指定表的外键约束。函数接收五个参数：
 *
 *1. `sql`：指向字符指针数组的指针，用于存储生成的 SQL 语句。
 *2. `sql_alloc`：用于存储字符指针数组的长度，后续可以根据需要进行扩展。
 *3. `sql_offset`：用于记录当前 SQL 语句在字符指针数组中的位置。
/******************************************************************************
 * *
 *整个代码块的主要目的是对名为 table_name 的表进行重组操作。根据是否支持 IBM DB2 数据库，分别执行不同的操作。如果支持，使用 DBexecute 函数执行 SQL 语句进行重组，并根据执行结果返回 SUCCEED 或 FAIL。如果不支持 IBM DB2，则直接返回 SUCCEED。
 ******************************************************************************/
// 定义一个名为 DBreorg_table 的静态函数，接收一个 const char * 类型的参数 table_name
static int DBreorg_table(const char *table_name)
{
    // 使用预处理指令定义是否支持 IBM DB2 数据库，如果支持，则执行以下操作
#ifdef HAVE_IBM_DB2
    // 执行 SQL 语句，对名为 table_name 的表进行重组操作
    if (ZBX_DB_OK <= DBexecute("call sysproc.admin_cmd ('reorg table %s')", table_name))
    {
        // 如果执行成功，返回 SUCCEED 表示成功
        return SUCCEED;
    }
    // 如果执行失败，返回 FAIL 表示失败
    else
    {
        return FAIL;
    }
#else
    // 如果未定义 HAVE_IBM_DB2，则不处理 table_name 参数
    ZBX_UNUSED(table_name);
    // 返回 SUCCEED，表示函数执行成功
    return SUCCEED;
#endif
}

static void DBdrop_foreign_key_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
                                 const char *table_name, int id)
{
    // 使用 zbx_snprintf_alloc 函数生成一个 SQL 语句，用于删除表的外键约束
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DROP_FK " c_%s_%d",
                      table_name, table_name, id);
}


static int	DBreorg_table(const char *table_name)
{
#ifdef HAVE_IBM_DB2
	if (ZBX_DB_OK <= DBexecute("call sysproc.admin_cmd ('reorg table %s')", table_name))
		return SUCCEED;

	return FAIL;
#else
	ZBX_UNUSED(table_name);
	return SUCCEED;
#endif
}
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBcreate_table 的函数，接收一个 ZBX_TABLE 类型的指针作为参数，用于创建数据库表。函数首先分配一段内存存储 SQL 语句，然后调用 DBcreate_table_sql 函数将生成的 SQL 语句存储在分配的内存中，并返回 sql_alloc、sql_offset。接下来判断 DBexecute 函数执行是否成功，若成功，则返回 SUCCEED，否则返回 FAIL。最后释放 sql 指向的字符串内存，并返回执行结果。
 ******************************************************************************/
int	DBcreate_table(const ZBX_TABLE *table)
{
	// 定义一个字符指针变量 sql，用于存储 SQL 语句
	char	*sql = NULL;
	// 定义一个 size_t 类型的变量 sql_alloc，用于存储 sql 分配的大小
	size_t	sql_alloc = 0;
	// 定义一个 size_t 类型的变量 sql_offset，用于存储 sql 的偏移量
	size_t	sql_offset = 0;
	// 定义一个整型变量 ret，初始值为 FAIL，用于存储函数执行结果
	int	ret = FAIL;

	// 调用 DBcreate_table_sql 函数，将生成的 SQL 语句存储在 sql 指针中，并返回 sql_alloc、sql_offset
	DBcreate_table_sql(&sql, &sql_alloc, &sql_offset, table);

	// 判断 DBexecute 函数执行是否成功，若成功，将 ret 设置为 SUCCEED，否则为 FAIL
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	// 释放 sql 指向的字符串内存
	zbx_free(sql);

	// 返回 ret，表示创建表的操作结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现数据库表的改名操作。首先，通过DBrename_table_sql函数将表名和新表名转换为SQL语句，然后判断SQL语句是否执行成功。如果执行成功，再调用DBreorg_table函数根据新表名进行数据库表的重新组织。最后，释放SQL语句占用的内存，并返回函数执行结果。
 ******************************************************************************/
// 定义一个函数DBrename_table，接收两个参数：table_name（字符串指针，表示要修改的数据库表名），new_name（字符串指针，表示新的表名）
int	DBrename_table(const char *table_name, const char *new_name)
{
	// 定义一个字符指针sql，用于存储SQL语句，初始化为NULL
	char	*sql = NULL;
	// 定义一个size_t类型的变量sql_alloc，用于存储sql分配的大小，初始化为0
	size_t	sql_alloc = 0;
	// 定义一个size_t类型的变量sql_offset，用于存储sql偏移量，初始化为0
	size_t	sql_offset = 0;
	// 定义一个整型变量ret，用于存储函数返回值，初始化为FAIL（表示失败）
	int	ret = FAIL;

	// 调用DBrename_table_sql函数，将table_name和new_name转换为SQL语句，并将结果存储在sql指针中
	DBrename_table_sql(&sql, &sql_alloc, &sql_offset, table_name, new_name);

/******************************************************************************
 * *
 *这块代码的主要目的是为一个名为`table_name`的表添加一个新的字段，该字段的信息存储在`ZBX_FIELD`结构体中。整个代码块分为以下几个步骤：
 *
 *1. 定义必要的变量，包括指向SQL语句的指针`sql`，以及SQL语句的分配大小`sql_alloc`和偏移量`sql_offset`。
 *2. 初始化`sql`指针为空，`sql_alloc`和`sql_offset`为0。
 *3. 调用`DBadd_field_sql`函数，将表名`table_name`和字段信息`field`传递给该函数，同时分配SQL语句的空间，并将结果存储在`sql`指针中。
 *4. 判断`DBexecute`函数执行的结果，如果执行成功（结果大于等于ZBX_DB_OK），则调用`DBreorg_table`函数对表进行重组。
 *5. 释放`sql`指针所占用的内存。
 *6. 返回执行结果。
 ******************************************************************************/
// 定义一个函数DBadd_field，接收两个参数：一个是指向表名的字符指针，另一个是指向字段结构的指针
int DBadd_field(const char *table_name, const ZBX_FIELD *field)
{
	// 定义一个指向SQL语句的指针，初始化为空指针
	char *sql = NULL;
	// 定义SQL语句分配的大小，初始化为0
	size_t sql_alloc = 0;
	// 定义SQL语句的偏移量，初始化为0
	size_t sql_offset = 0;
	// 定义返回值，初始化为失败状态
	int ret = FAIL;

	// 调用DBadd_field_sql函数，将SQL语句分配大小、偏移量和表名、字段结构传递给该函数
	DBadd_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	// 判断DBexecute函数执行的结果，如果执行结果大于等于ZBX_DB_OK，说明执行成功
	if (ZBX_DB_OK <= DBexecute("%s", sql))
	{
		// 如果执行成功，调用DBreorg_table函数对表进行重组
		ret = DBreorg_table(table_name);
	}

	// 释放SQL语句分配的内存
	zbx_free(sql);

	// 返回执行结果
	return ret;
}

 *这块代码的主要目的是删除数据库中的表。函数`DBdrop_table`接收一个表名作为参数，通过调用`DBdrop_table_sql`函数生成删除表的SQL语句，然后使用`DBexecute`函数执行SQL语句。如果执行成功，函数返回成功状态，否则返回失败状态。最后，释放SQL语句占用的内存。
 ******************************************************************************/
// 定义一个函数，用于删除数据库中的表
int DBdrop_table(const char *table_name)
{
    // 定义一个指向字符串的指针sql，用于存储SQL语句
    char *sql = NULL;
    // 定义sql字符串的长度分配大小
    size_t sql_alloc = 0;
    // 定义sql字符串的当前偏移量
    size_t sql_offset = 0;
    // 定义返回值，初始化为失败状态
    int ret = FAIL;

    // 调用DBdrop_table_sql函数，生成删除表的SQL语句，并将结果存储在sql指针中
    DBdrop_table_sql(&sql, &sql_alloc, &sql_offset, table_name);

    // 判断执行SQL语句的结果，如果执行成功，将返回成功状态
    if (ZBX_DB_OK <= DBexecute("%s", sql))
        ret = SUCCEED;

    // 释放sql字符串占用的内存
    zbx_free(sql);

    // 返回执行结果
    return ret;
}


int	DBadd_field(const char *table_name, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBadd_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	zbx_free(sql);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是修改数据库中表的字段名称。首先，通过调用DBrename_field_sql函数生成SQL语句，然后判断SQL语句执行是否成功。如果成功，则执行DBreorg_table函数修改表结构，最后释放内存并返回函数执行结果。
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是修改数据库表中的字段类型。具体步骤如下：
 *
 *1. 使用`zbx_snprintf_alloc`动态分配内存并拼接SQL语句，将表中的某个字段更名为`ZBX_OLD_FIELD`。
 *2. 判断`DBexecute`函数执行是否成功，如果失败，跳转到`out`标签处。
 *3. 使用`DBadd_field`函数添加新字段，判断执行是否成功，如果失败，跳转到`out`标签处。
 *4. 清空`sql_offset`，重新拼接SQL语句，更新表中的数据。
 *5. 判断`DBexecute`函数执行是否成功，如果失败，跳转到`out`标签处。
 *6. 调用`DBdrop_field`函数，删除原名称为`ZBX_OLD_FIELD`的字段，并将结果存储在`ret`变量中。
 *7. 释放内存，并返回`ret`变量的值。
 ******************************************************************************/
static int	DBmodify_field_type_with_copy(const char *table_name, const ZBX_FIELD *field)
{
    // 定义一个常量字符串，用于存放旧的字段名
#define ZBX_OLD_FIELD	"zbx_old_tmp"

    // 声明一个字符指针变量，用于存储SQL语句
    char	*sql = NULL;
    // 声明一个大小为0的字符数组，用于存储SQL语句
    size_t	sql_alloc = 0, sql_offset = 0;
    // 定义一个返回值，初始值为FAIL
    int	ret = FAIL;

    // 使用zbx_snprintf_alloc函数动态分配内存，并拼接SQL语句，将表中的某个字段更名为ZBX_OLD_FIELD
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "alter table %s rename column %s to " ZBX_OLD_FIELD,
                    table_name, field->name);

    // 判断DBexecute函数执行是否成功，如果失败，跳转到out标签处
    if (ZBX_DB_OK > DBexecute("%s", sql))
        goto out;

    // 判断DBadd_field函数执行是否成功，如果失败，跳转到out标签处
    if (ZBX_DB_OK > DBadd_field(table_name, field))
        goto out;

    // 清空sql_offset，重新拼接SQL语句
    sql_offset = 0;
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set %s=" ZBX_OLD_FIELD, table_name,
                    field->name);

    // 判断DBexecute函数执行是否成功，如果失败，跳转到out标签处
    if (ZBX_DB_OK > DBexecute("%s", sql))
        goto out;

    // 调用DBdrop_field函数，删除原名称为ZBX_OLD_FIELD的字段，并将ret变量记录结果
    ret = DBdrop_field(table_name, ZBX_OLD_FIELD);

out:
    // 释放sql内存
    zbx_free(sql);

    // 返回ret变量的值
    return ret;

#undef ZBX_OLD_FIELD
}


/******************************************************************************
 * *
 *整个代码块的主要目的是修改数据库表中的字段类型。函数`DBmodify_field_type`接收三个参数：表名、新字段和旧字段。在处理过程中，首先判断当前环境是否有Oracle数据库，如果有，则针对Oracle数据库的特殊情况进行处理。接着生成SQL语句，用于修改字段类型。最后执行SQL语句并重新组织表结构，返回函数执行结果。
 ******************************************************************************/
int	DBmodify_field_type(const char *table_name, const ZBX_FIELD *field, const ZBX_FIELD *old_field)
{
	/* 定义一个函数，用于修改数据库表中的字段类型 */

	char	*sql = NULL; // 声明一个字符指针，用于存储SQL语句
	size_t	sql_alloc = 0, sql_offset = 0; // 声明两个大小为0的字节数组，用于存储SQL语句
	int	ret = FAIL; // 定义一个整型变量，用于存储函数返回值

#ifndef HAVE_ORACLE // 如果当前环境没有Oracle数据库
	ZBX_UNUSED(old_field); // 忽略旧字段的参数，因为在没有Oracle数据库的情况下不需要使用它
#else // 如果有Oracle数据库
	/* Oracle 不能在一般情况下更改列类型，如果列内容不为空。转换如数字 -> nvarchar2需要特殊处理。创建一个新的列，用旧列的數據类型和数据，然后删除旧列。这种方法不保留列顺序。 */
	/* 注意：当前实现不尊重现有的列索引和约束！ */

	if (NULL != old_field && zbx_oracle_column_type(old_field->type) != zbx_oracle_column_type(field->type)) // 如果旧字段不为空，并且字段类型不同
		return DBmodify_field_type_with_copy(table_name, field); // 调用另一个函数进行字段类型修改
#endif

	DBmodify_field_type_sql(&sql, &sql_alloc, &sql_offset, table_name, field); // 调用另一个函数生成SQL语句

	if (ZBX_DB_OK <= DBexecute("%s", sql)) // 如果执行SQL语句成功
		ret = DBreorg_table(table_name); // 调用另一个函数重新组织表结构

	zbx_free(sql); // 释放SQL语句内存

	return ret; // 返回函数执行结果
}

}
#endif

int	DBmodify_field_type(const char *table_name, const ZBX_FIELD *field, const ZBX_FIELD *old_field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

#ifndef HAVE_ORACLE
	ZBX_UNUSED(old_field);
#else
	/* Oracle cannot change column type in a general case if column contents are not null. Conversions like   */
	/* number -> nvarchar2 need special processing. New column is created with desired datatype and data from */
	/* old column is copied there. Then old column is dropped. This method does not preserve column order.    */
	/* NOTE: Existing column indexes and constraints are not respected by the current implementation!         */

	if (NULL != old_field && zbx_oracle_column_type(old_field->type) != zbx_oracle_column_type(field->type))
		return DBmodify_field_type_with_copy(table_name, field);
#endif
	DBmodify_field_type_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	zbx_free(sql);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是设置数据库中表的字段不为空。首先，通过DBset_not_null_sql函数根据表名和字段信息生成SQL语句，并将结果存储在sql指针中。然后，判断生成的SQL语句是否可以执行，如果可以执行，则调用DBreorg_table函数执行设置字段不为空的操作。最后，释放sql内存并返回执行结果。
 ******************************************************************************/
// 定义一个函数DBset_not_null，参数分别为表名（const char *table_name）和字段信息（const ZBX_FIELD *field）。
// 该函数的主要目的是设置数据库中表的字段不为空。
int	DBset_not_null(const char *table_name, const ZBX_FIELD *field)
{
	// 定义一个指向字符串的指针sql，以及sql的分配大小和偏移量
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	// 定义一个返回值，初始化为失败（FAIL）
	int	ret = FAIL;

	// 调用DBset_not_null_sql函数，根据表名和字段信息生成SQL语句，并将结果存储在sql指针中
	DBset_not_null_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	// 判断生成的SQL语句是否可以执行，如果可以执行，则调用DBreorg_table函数执行设置字段不为空的操作
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	// 释放sql内存
	zbx_free(sql);

	// 返回执行结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个DBset_default函数，接收表名和字段指针作为参数，生成对应的SQL语句，然后执行SQL语句对表进行操作（这里是重组表），最后返回操作结果。
 ******************************************************************************/
// 定义一个函数DBset_default，接收两个参数：一个是指向表名字符串的指针，另一个是指向字段结构体的指针
int DBset_default(const char *table_name, const ZBX_FIELD *field)
{
	// 定义一个指向SQL语句的指针，初始化为空指针
	char *sql = NULL;
	// 定义一个size_t类型的变量，用于存储SQL语句分配的大小
	size_t sql_alloc = 0;
	// 定义一个size_t类型的变量，用于存储SQL语句的偏移量
	size_t sql_offset = 0;
	// 定义一个整型变量，用于存储函数返回值，初始值为FAIL（表示失败）
	int ret = FAIL;

	// 调用DBset_default_sql函数，将生成的SQL语句存储在sql指针指向的空间中，并返回SQL语句的长度
	DBset_default_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	// 判断DBexecute函数执行的结果，如果执行结果大于等于ZBX_DB_OK（表示执行成功）
	if (ZBX_DB_OK <= DBexecute("%s", sql))
	{
		// 如果执行成功，调用DBreorg_table函数对表进行重组，传入的参数是表名
		ret = DBreorg_table(table_name);
	}

	// 释放sql指向的空间
	zbx_free(sql);

	// 返回ret变量，表示函数执行的结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是删除表中不为空的字段。首先，定义一个函数DBdrop_not_null，接收两个参数：表名table_name和不为空的字段field。接着，生成SQL语句，判断数据库操作是否成功。如果成功，调用DBreorg_table函数进行表重组。最后，释放内存并返回操作结果。
 ******************************************************************************/
// 定义一个函数int DBdrop_not_null(const char *table_name, const ZBX_FIELD *field)，这个函数的主要目的是删除表中不为空的字段

int	DBdrop_not_null(const char *table_name, const ZBX_FIELD *field)
{ // 定义一个整型变量DBdrop_not_null，参数分别为表名table_name和不为空的字段field

	char	*sql = NULL; // 定义一个字符指针变量sql，用于存储SQL语句
	size_t	sql_alloc = 0, sql_offset = 0; // 定义两个大小为0的变量sql_alloc和sql_offset，用于存储SQL语句的长度和偏移量
	int	ret = FAIL; // 定义一个整型变量ret，初始值为FAIL，表示操作失败

	DBdrop_not_null_sql(&sql, &sql_alloc, &sql_offset, table_name, field); // 调用DBdrop_not_null_sql函数，生成SQL语句，并将结果存储在sql变量中

	if (ZBX_DB_OK <= DBexecute("%s", sql)) // 如果数据库操作成功（ZBX_DB_OK表示成功），执行以下操作
		ret = DBreorg_table(table_name); // 调用DBreorg_table函数，根据表名table_name进行重组

	zbx_free(sql); // 释放sql变量占用的内存

	return ret; // 返回ret变量的值，即操作结果
}

/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为DBdrop_field的函数，该函数根据传入的表名和字段名，构造一条SQL语句用于删除字段，然后执行该SQL语句。如果执行成功，函数返回DBreorg_table函数的调用结果，用于对表进行重组。最后，释放分配的内存，并返回函数执行结果。
 ******************************************************************************/
// 定义一个函数DBdrop_field，接收两个字符指针作为参数，一个是表名（table_name），一个是字段名（field_name）
int	DBdrop_field(const char *table_name, const char *field_name)
{
	// 定义一个字符指针变量sql，用于存储SQL语句
	char	*sql = NULL;
	// 定义一个size_t类型的变量sql_alloc，用于存储sql内存分配大小
	size_t	sql_alloc = 0;
	// 定义一个size_t类型的变量sql_offset，用于存储sql指针偏移量
	size_t	sql_offset = 0;
	// 定义一个整型变量ret，用于存储函数返回值
	int	ret = FAIL;

	// 调用DBdrop_field_sql函数，将生成的SQL语句存储在sql指针中，并分配内存
	DBdrop_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field_name);

	// 判断DBexecute函数执行是否成功，如果成功，则执行DBreorg_table函数，对表进行重组
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	// 释放sql指向的内存
	zbx_free(sql);

	// 返回ret变量，表示函数执行结果
	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是用于创建数据库索引。函数`DBcreate_index`接收四个参数：表名（table_name）、索引名（index_name）、字段名（fields）和唯一性约束（unique）。函数首先调用`DBcreate_index_sql`生成创建索引的SQL语句，然后判断执行该SQL语句是否成功。如果成功，返回SUCCEED，否则返回FAIL。最后释放生成的SQL语句内存并返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，用于创建数据库索引
int DBcreate_index(const char *table_name, const char *index_name, const char *fields, int unique)
{
    // 定义一个指向字符串的指针，用于存储SQL语句
    char *sql = NULL;
    // 定义一个大小为0的字符串缓冲区，用于存储SQL语句
    size_t sql_alloc = 0;
    // 定义一个初始值为0的整数，用于记录SQL语句的长度
    size_t sql_offset = 0;
    // 定义一个整数，用于存储函数返回值
    int ret = FAIL;

    // 调用DBcreate_index_sql函数，生成创建索引的SQL语句
    DBcreate_index_sql(&sql, &sql_alloc, &sql_offset, table_name, index_name, fields, unique);

    // 判断执行生成的SQL语句是否成功，如果成功，将ret设置为SUCCEED，否则为FAIL
    if (ZBX_DB_OK <= DBexecute("%s", sql))
    {
        ret = SUCCEED;
    }

    // 释放生成的SQL语句内存
    zbx_free(sql);

    // 返回函数执行结果
    return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是删除数据库中的索引。函数`DBdrop_index`接受两个参数，分别是表名和索引名。首先，调用`DBdrop_index_sql`函数生成删除索引的SQL语句，然后使用`DBexecute`函数执行SQL语句。如果执行成功，返回成功，否则返回失败。最后，释放内存并返回执行结果。
 ******************************************************************************/
// 定义一个函数，用于删除数据库中的索引
int DBdrop_index(const char *table_name, const char *index_name)
{
    // 定义一个指向字符串的指针sql，以及sql的长度分配和偏移量
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;
    // 定义一个返回值，初始化为失败
    int ret = FAIL;

    // 调用DBdrop_index_sql函数，生成删除索引的SQL语句
    DBdrop_index_sql(&sql, &sql_alloc, &sql_offset, table_name, index_name);

/******************************************************************************
 * *
 *整个代码块的主要目的是重命名数据库表的索引。函数`DBrename_index`接收5个参数：`table_name`（表名）、`old_name`（旧索引名）、`new_name`（新索引名）、`fields`（索引字段）、`unique`（唯一性约束）。函数首先调用`DBrename_index_sql`生成重命名索引的SQL语句，然后使用`DBexecute`执行SQL语句。如果执行成功，更新返回值为成功。最后释放SQL语句内存并返回函数结果。
 ******************************************************************************/
// 定义一个函数，用于重命名数据库表的索引
int DBrename_index(const char *table_name, const char *old_name, const char *new_name, const char *fields, int unique)
{
    // 定义一个指向字符串的指针，用于存储SQL语句
    char *sql = NULL;
    // 定义一个大小为0的字符串缓冲区，用于存储SQL语句
    size_t sql_alloc = 0;
    // 定义一个初始值为0的整数，用于存储SQL语句的长度
    size_t sql_offset = 0;
    // 定义一个初始值为失败的整数，用于存储函数返回值
    int ret = FAIL;

    // 调用DBrename_index_sql函数，生成重命名索引的SQL语句
    DBrename_index_sql(&sql, &sql_alloc, &sql_offset, table_name, old_name, new_name, fields, unique);

    // 判断DBexecute函数执行是否成功，若成功，则更新返回值为成功
    if (ZBX_DB_OK <= DBexecute("%s", sql))
        ret = SUCCEED;

    // 释放SQL语句内存
    zbx_free(sql);

    // 返回函数结果
    return ret;
}

	int	ret = FAIL;

	DBrename_index_sql(&sql, &sql_alloc, &sql_offset, table_name, old_name, new_name, fields, unique);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}
/******************************************************************************
 * *
 *这块代码的主要目的是向数据库中添加外键约束。函数`DBadd_foreign_key`接受三个参数：表名（table_name）、主键ID（id）和字段信息（field）。首先，调用`DBadd_foreign_key_sql`函数生成添加外键约束的SQL语句，然后使用`DBexecute`函数执行SQL语句。如果执行成功，返回成功（SUCCEED），否则返回失败（FAIL）。最后，释放SQL语句占用的内存，并返回操作结果。
 ******************************************************************************/
// 定义一个函数，用于向数据库中添加外键约束
int DBadd_foreign_key(const char *table_name, int id, const ZBX_FIELD *field)
{
	// 定义一个指向字符串的指针sql，以及sql的分配大小和偏移量
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	// 定义一个变量，用于存储函数返回值，初始值为失败（FAIL）
	int ret = FAIL;

	// 调用DBadd_foreign_key_sql函数，生成添加外键约束的SQL语句
	DBadd_foreign_key_sql(&sql, &sql_alloc, &sql_offset, table_name, id, field);

	// 判断执行添加外键约束的操作是否成功，如果成功，将返回成功（SUCCEED）
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	// 释放SQL语句占用的内存
	zbx_free(sql);

	// 返回添加外键约束的操作结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是删除表中的外键约束。首先，通过DBdrop_foreign_key_sql函数生成删除外键约束的SQL语句，并将结果存储在sql指针指向的字符串中。然后，使用DBexecute函数执行生成的SQL语句，判断执行结果。如果执行成功，返回SUCCEED，表示删除外键约束操作成功；否则，返回FAIL。最后，释放sql指向的字符串内存，并返回操作结果。
 ******************************************************************************/
// 定义一个函数int DBdrop_foreign_key(const char *table_name, int id)，该函数的主要目的是删除表中的外键约束。
// 参数1：table_name，要操作的表名；
// 参数2：id，要删除的外键约束的ID。

int	DBdrop_foreign_key(const char *table_name, int id)
{
	// 声明一个指向字符串的指针sql，以及三个size_t类型的变量sql_alloc、sql_offset，和一个整型变量ret，用于存储函数执行结果。
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	// 调用DBdrop_foreign_key_sql函数，生成删除外键约束的SQL语句，并将结果存储在sql指针指向的字符串中。
	DBdrop_foreign_key_sql(&sql, &sql_alloc, &sql_offset, table_name, id);
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `dbversion` 的数据库表，并在表中插入一条初始数据。表的结构包括两个字段：`mandatory` 和 `optional`，均为整型。如果创建表和插入数据操作成功，函数返回 SUCCEED；如果失败，返回 FAIL。
 ******************************************************************************/
// 定义一个名为 DBcreate_dbversion_table 的静态函数，该函数不接受任何参数
static int	DBcreate_dbversion_table(void)
{
	// 定义一个常量结构体变量 table，用于存储数据库表的结构信息
	const ZBX_TABLE	table =
			{"dbversion", "", 0,
				// 定义表的字段及其类型和属性
				{
					{"mandatory", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"optional", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{NULL}
				},
				NULL
			};
	// 定义一个整型变量 ret，用于存储函数返回值
	int		ret;

	// 开始执行数据库操作
	DBbegin();
	// 尝试创建表，如果创建成功，返回值应为 SUCCEED
	if (SUCCEED == (ret = DBcreate_table(&table)))
	{
		// 向 dbversion 表中插入一条初始数据，如果插入成功，返回值应为 ZBX_DB_OK
		if (ZBX_DB_OK > DBexecute("insert into dbversion (mandatory,optional) values (%d,%d)",
				ZBX_FIRST_DB_VERSION, ZBX_FIRST_DB_VERSION))
		{
			// 如果插入失败，将 ret 设置为 FAIL
			ret = FAIL;
		}
	}

	// 结束数据库操作，并返回 ret
	return DBend(ret);
}


	DBbegin();
	if (SUCCEED == (ret = DBcreate_table(&table)))
	{
		if (ZBX_DB_OK > DBexecute("insert into dbversion (mandatory,optional) values (%d,%d)",
				ZBX_FIRST_DB_VERSION, ZBX_FIRST_DB_VERSION))
/******************************************************************************
 * *
 *这个代码块的主要目的是检查数据库版本，并根据需要升级数据库。具体来说，它执行以下操作：
 *
 *1. 定义常量字符串和变量。
 *2. 进入函数并连接数据库。
 *3. 检查数据库表是否存在，如果不存在，则创建数据库版本表。
 *4. 获取数据库版本号。
 *5. 判断当前数据库版本是否符合要求。
 *6. 如果不符合要求，打印错误日志并退出。
 *7. 打印当前数据库版本和所需版本。
 *8. 遍历版本补丁链表，升级数据库。
 *9. 升级完成后，打印升级结果。
 *
 *整个代码块的主要目的是确保数据库版本与Zabbix要求的一致，如果不一致，则自动升级数据库。
 ******************************************************************************/
int DBcheck_version(void)
{
	// 定义常量字符串
	const char *__function_name = "DBcheck_version";
	const char *dbversion_table_name = "dbversion";
	int	db_mandatory, db_optional, required, ret = FAIL, i;
	zbx_db_version_t	*dbversion;
	zbx_dbpatch_t		*patches;

	// 判断是否支持SQLite3
#ifndef HAVE_SQLITE3
	int	total = 0, current = 0, completed, last_completed = -1, optional_num = 0;
#endif

	// 进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 设置所需版本号为最低版本
	required = ZBX_FIRST_DB_VERSION;

	// 获取最后一个版本补丁数组的最后一个元素
	for (dbversion = dbversions; NULL != dbversion->patches; dbversion++)
		;

	// 获取补丁链表
	patches = (--dbversion)->patches;

	// 遍历补丁链表，找到所需版本号
	for (i = 0; 0 != patches[i].version; i++)
	{
		if (0 != patches[i].mandatory)
			required = patches[i].version;
	}

	// 连接数据库
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	// 检查数据库表是否存在
	if (SUCCEED != DBtable_exists(dbversion_table_name))
	{
#ifndef HAVE_SQLITE3
		// 打印日志
		zabbix_log(LOG_LEVEL_DEBUG, "%s() \"%s\" does not exist",
				__function_name, dbversion_table_name);

		// 检查配置表中是否存在"server_check_interval"字段
		if (SUCCEED != DBfield_exists("config", "server_check_interval"))
		{
			// 打印日志
			zabbix_log(LOG_LEVEL_CRIT, "Cannot upgrade database: the database must"
					" correspond to version 2.0 or later. Exiting ...");
			goto out;
		}

		// 创建数据库版本表
		if (SUCCEED != DBcreate_dbversion_table())
			goto out;
#else
		// 打印日志
		zabbix_log(LOG_LEVEL_CRIT, "The %s does not match Zabbix database."
				" Current database version (mandatory/optional): UNKNOWN."
				" Required mandatory version: %08d.",
				get_program_type_string(program_type), required);
		zabbix_log(LOG_LEVEL_CRIT, "Zabbix does not support SQLite3 database upgrade.");

		goto out;
#endif
	}

	// 获取数据库版本号
	DBget_version(&db_mandatory, &db_optional);

#ifndef HAVE_SQLITE3
	// 遍历版本补丁链表，找到当前已应用的补丁
	for (dbversion = dbversions; NULL != (patches = dbversion->patches); dbversion++)
	{
		for (i = 0; 0 != patches[i].version; i++)
		{
			if (db_optional < patches[i].version)
				total++;
		}
	}

	// 判断数据库版本是否符合要求
	if (required < db_mandatory)
#else
	// 判断数据库版本是否符合要求
	if (required != db_mandatory)
#endif
	{
		// 打印日志
		zabbix_log(LOG_LEVEL_CRIT, "The %s does not match Zabbix database."
				" Current database version (mandatory/optional): %08d/%08d."
				" Required mandatory version: %08d.",
				get_program_type_string(program_type), db_mandatory, db_optional, required);
#ifdef HAVE_SQLITE3
		if (required > db_mandatory)
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix does not support SQLite3 database upgrade.");
#endif
		goto out;
	}

	// 打印日志
	zabbix_log(LOG_LEVEL_INFORMATION, "current database version (mandatory/optional): %08d/%08d",
			db_mandatory, db_optional);
	zabbix_log(LOG_LEVEL_INFORMATION, "required mandatory version: %08d", required);

	// 设置升级状态为成功
	ret = SUCCEED;

#ifndef HAVE_SQLITE3
	// 遍历版本补丁链表，升级数据库
	if (0 == total)
		goto out;

	// 判断是否需要升级
	if (0 != optional_num)
		zabbix_log(LOG_LEVEL_INFORMATION, "optional patches were found");

	zabbix_log(LOG_LEVEL_WARNING, "starting automatic database upgrade");

	for (dbversion = dbversions; NULL != dbversion->patches; dbversion++)
	{
		patches = dbversion->patches;

		for (i = 0; 0 != patches[i].version; i++)
		{
			static sigset_t	orig_mask, mask;

			// 阻塞信号，防止中断升级过程
			sigemptyset(&mask);
			sigaddset(&mask, SIGTERM);
			sigaddset(&mask, SIGINT);
			sigaddset(&mask, SIGQUIT);

			if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
				zabbix_log(LOG_LEVEL_WARNING, "cannot set sigprocmask to block the user signal");

			DBbegin();

			// 跳过重复的补丁
			if ((0 != patches[i].duplicates && patches[i].duplicates <= db_optional) ||
					SUCCEED == (ret = patches[i].function()))
			{
				ret = DBset_version(patches[i].version, patches[i].mandatory);
			}

			ret = DBend(ret);

			if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
				zabbix_log(LOG_LEVEL_WARNING,"cannot restore sigprocmask");

			if (SUCCEED != ret)
				break;

			current++;
			completed = (int)(100.0 * current / total);

			// 打印升级进度
			if (last_completed != completed)
			{
				zabbix_log(LOG_LEVEL_WARNING, "completed %d%% of database upgrade", completed);
				last_completed = completed;
			}
		}

		if (SUCCEED != ret)
			break;
	}

	// 升级成功
	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_WARNING, "database upgrade fully completed");
	else
		zabbix_log(LOG_LEVEL_CRIT, "database upgrade failed");
#endif	/* not HAVE_SQLITE3 */

out:
	// 关闭数据库连接
	DBclose();

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	}

	DBget_version(&db_mandatory, &db_optional);

#ifndef HAVE_SQLITE3
	for (dbversion = dbversions; NULL != (patches = dbversion->patches); dbversion++)
	{
		for (i = 0; 0 != patches[i].version; i++)
		{
			if (0 != patches[i].mandatory)
				optional_num = 0;
			else
				optional_num++;

			if (db_optional < patches[i].version)
				total++;
		}
	}

	if (required < db_mandatory)
#else
	if (required != db_mandatory)
#endif
	{
		zabbix_log(LOG_LEVEL_CRIT, "The %s does not match Zabbix database."
				" Current database version (mandatory/optional): %08d/%08d."
				" Required mandatory version: %08d.",
				get_program_type_string(program_type), db_mandatory, db_optional, required);
#ifdef HAVE_SQLITE3
		if (required > db_mandatory)
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix does not support SQLite3 database upgrade.");
#endif
		goto out;
	}

	zabbix_log(LOG_LEVEL_INFORMATION, "current database version (mandatory/optional): %08d/%08d",
			db_mandatory, db_optional);
	zabbix_log(LOG_LEVEL_INFORMATION, "required mandatory version: %08d", required);

	ret = SUCCEED;

#ifndef HAVE_SQLITE3
	if (0 == total)
		goto out;

	if (0 != optional_num)
		zabbix_log(LOG_LEVEL_INFORMATION, "optional patches were found");

	zabbix_log(LOG_LEVEL_WARNING, "starting automatic database upgrade");

	for (dbversion = dbversions; NULL != dbversion->patches; dbversion++)
	{
		patches = dbversion->patches;

		for (i = 0; 0 != patches[i].version; i++)
		{
			static sigset_t	orig_mask, mask;

			if (db_optional >= patches[i].version)
				continue;

			/* block signals to prevent interruption of statements that cause an implicit commit */
			sigemptyset(&mask);
			sigaddset(&mask, SIGTERM);
			sigaddset(&mask, SIGINT);
			sigaddset(&mask, SIGQUIT);

			if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
				zabbix_log(LOG_LEVEL_WARNING, "cannot set sigprocmask to block the user signal");

			DBbegin();

			/* skipping the duplicated patches */
			if ((0 != patches[i].duplicates && patches[i].duplicates <= db_optional) ||
					SUCCEED == (ret = patches[i].function()))
			{
				ret = DBset_version(patches[i].version, patches[i].mandatory);
			}

			ret = DBend(ret);

			if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
				zabbix_log(LOG_LEVEL_WARNING,"cannot restore sigprocmask");

			if (SUCCEED != ret)
				break;

			current++;
			completed = (int)(100.0 * current / total);

			if (last_completed != completed)
			{
				zabbix_log(LOG_LEVEL_WARNING, "completed %d%% of database upgrade", completed);
				last_completed = completed;
			}
		}

		if (SUCCEED != ret)
			break;
	}

	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_WARNING, "database upgrade fully completed");
	else
		zabbix_log(LOG_LEVEL_CRIT, "database upgrade failed");
#endif	/* not HAVE_SQLITE3 */

out:
	DBclose();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
