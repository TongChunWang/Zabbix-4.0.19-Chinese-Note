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
#include "dbupgrade.h"
#include "sysinfo.h"
#include "log.h"

/*
 * 3.0 development database patches
 */

#ifndef HAVE_SQLITE3

/******************************************************************************
 * *
 *这段代码的主要目的是在数据库中创建一个名为 \"httptest\" 的默认字段。函数 DBpatch_2050000 是一个静态函数，它定义了一个 ZBX_FIELD 类型的常量变量 field，用于存储数据字段信息。然后调用 DBset_default 函数，将 \"httptest\" 和 field 结构体作为参数传入，以便在数据库中创建默认字段。最后，返回 DBset_default 函数的执行结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050000 的静态函数
static int	DBpatch_2050000(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询符合条件的物品，并将SNMP发现OID更新到物品表中。具体步骤如下：
 *
 *1. 查询符合条件的物品，条件为 flags=1 和 type 在 (1,4,6) 之间。
 *2. 遍历查询结果，对每个物品处理参数和OID。
 *3. 检查OID是否合法且不超过长度限制，若不合法或超过长度限制，记录警告日志。
 *4. 更新物品表中的SNMP_OID，使用动态转义字符串避免SQL注入。
 *5. 判断更新是否成功，若失败则退出循环。
 *6. 更新成功后，返回SUCCEED。
 *
 *在整个过程中，若遇到错误，则会记录日志并进行相应的处理。
 ******************************************************************************/
/*
 * 函数名：DBpatch_2050001
 * 功能：从数据库中查询符合条件的物品，并将SNMP发现OID更新到物品表中
 * 返回值：成功更新后的返回值，失败则返回FAIL
 */
static int	DBpatch_2050001(void)
{
	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	char		*oid = NULL;
	size_t		oid_alloc = 0;
	int		ret = FAIL, rc;

	/* 查询符合条件的物品 */
	if (NULL == (result = DBselect("select itemid,snmp_oid from items where flags=1 and type in (1,4,6)")))
		return FAIL;

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 处理物品参数和OID */
		char	*param, *oid_esc;
		size_t	oid_offset = 0;

		param = zbx_strdup(NULL, row[1]);
		zbx_snprintf_alloc(&oid, &oid_alloc, &oid_offset, "discovery[{#SNMPVALUE},%s]", param);

		/* 检查OID是否合法且不超过长度限制 */
		if (FAIL == quote_key_param(&param, 0))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot convert SNMP discovery OID \"%s\":"
					" OID contains invalid character(s)", row[1]);
			rc = ZBX_DB_OK;
		}
		else if (255 < oid_offset && 255 < zbx_strlen_utf8(oid)) /* 255 - ITEM_SNMP_OID_LEN */
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot convert SNMP discovery OID \"%s\":"
					" resulting OID is too long", row[1]);
			rc = ZBX_DB_OK;
		}
		else
		{
			oid_esc = DBdyn_escape_string(oid);

			/* 更新物品表中的SNMP_OID */
			rc = DBexecute("update items set snmp_oid='%s' where itemid=%s", oid_esc, row[0]);

			zbx_free(oid_esc);
		}

		zbx_free(param);

		/* 判断更新是否成功，若失败则退出循环 */
		if (ZBX_DB_OK > rc)
			goto out;
	}

	/* 更新成功，设置返回值 */
	ret = SUCCEED;

out:
	/* 释放资源 */
	DBfree_result(result);
	zbx_free(oid);

	return ret;
}

	}

	ret = SUCCEED;
out:
	DBfree_result(result);
	zbx_free(oid);

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是向 \"proxy_history\" 表中添加一个名为 \"lastlogsize\" 的字段，该字段的值为 \"0\"，类型为无符号整数（ZBX_TYPE_UINT），非空（ZBX_NOTNULL），并设置其他相关属性。最后，返回添加字段的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050002 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050002(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储字段信息
	const ZBX_FIELD	field = {"lastlogsize", "0", NULL, NULL, 0, ZBX_TYPE_UINT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的字段添加到 "proxy_history" 表中
	return DBadd_field("proxy_history", &field);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050004 的静态函数，该函数用于向名为 \"proxy_history\" 的数据库表中添加一个整型（int）字段。添加字段的过程中，指定了字段的前缀、编号、数据类型、是否允许为空等属性。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050004 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050004(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，其中：
	// "meta"：表示字段的前缀
	// "0"：表示字段的编号，此处为固定值0
	// NULL：表示字段的前缀和编号之后的部分为空，即没有额外信息
	// NULL：表示字段值的数据类型为空，即没有指定具体的数据类型
	// 0：表示字段的长度为0，即没有指定具体的长度
	// ZBX_TYPE_INT：表示字段的数据类型为整型（int）
	// ZBX_NOTNULL：表示字段不允许为空
	// 0：表示其他未知属性，此处为固定值0

	const ZBX_FIELD	field = {"meta", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义好的字段添加到名为 "proxy_history" 的数据库表中
	// 传入两个参数：表名 "proxy_history" 和字段变量 field
	return DBadd_field("proxy_history", &field);
}

static int	DBpatch_2050003(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"mtime", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将名为 "proxy_history" 的表中添加一个新的字段
	// 传入的参数：
	// 1. 表名："proxy_history"
	// 2. 字段信息：&field（指向 ZBX_FIELD 结构体的指针）
	// 3. 返回值：函数调用成功则返回 0，失败则返回 -1
	return DBadd_field("proxy_history", &field);
}


static int	DBpatch_2050004(void)
{
	const ZBX_FIELD	field = {"meta", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("proxy_history", &field);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历 items 表中 type 为 ITEM_TYPE_ZABBIX、ITEM_TYPE_SIMPLE 或 ITEM_TYPE_ZABBIX_ACTIVE 的数据，对于 key 字段包含 \"net.tcp.service%%[%%ntp%%\" 的行，将其 key 替换为 \"ntp\"，并更新数据库中的相应记录。如果在同一主机 ID 下已存在相同 key 的记录，则打印警告日志。如果解析 item key 失败，也打印警告日志并跳过当前行。
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050012 的静态函数，该函数没有返回值，参数为一个空指针 */
static int	DBpatch_2050012(void)
{
	/* 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果 */
	DB_RESULT	result;
	/* 定义一个 DB_RESULT 类型的变量 result2，用于存储数据库查询结果 */
	DB_RESULT	result2;
	/* 定义一个 DB_ROW 类型的变量 row，用于存储数据库行的数据 */
	DB_ROW		row;
	/* 定义一个字符串指针 key，初始化为 NULL，用于存储 item 的 key */
	char		*key = NULL, *key_esc, *param;
	/* 定义一个整型变量 ret，用于存储函数返回值，初始值为 SUCCEED（0）*/
	int		ret = SUCCEED;
	/* 定义一个 AGENT_REQUEST 类型的变量 request，用于存储请求数据 */
	AGENT_REQUEST	request;

	/* 执行数据库查询，从 items 表中选择 type 为 ITEM_TYPE_ZABBIX、ITEM_TYPE_SIMPLE 或 ITEM_TYPE_ZABBIX_ACTIVE 的数据，key 字段包含 "net.tcp.service%%[%%ntp%%" */
	result = DBselect(
			"select hostid,itemid,key_"
			" from items"
			" where type in (0,3,7)"
				" and key_ like 'net.tcp.service%%[%%ntp%%'");

	/* 使用 while 循环逐行处理数据库查询结果 */
	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		/* 初始化请求数据结构体 request */
		init_request(&request);

		/* 解析 item key，若解析失败，打印警告日志并跳过当前行 */
		if (SUCCEED != parse_item_key(row[2], &request))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot parse item key \"%s\"", row[2]);
			continue;
		}

		/* 从请求数据结构体中获取参数 */
		param = get_rparam(&request, 0);

		/* 检查参数是否为 NULL 或不是 "service.ntp" 或 "ntp"，若是，释放请求数据结构体并跳过当前行 */
		if (NULL == param || (0 != strcmp("service.ntp", param) && 0 != strcmp("ntp", param)))
		{
			free_request(&request);
			continue;
		}

		/* 复制 item key，并进行替换操作 */
		key = zbx_strdup(key, row[2]);

		if (0 == strcmp("service.ntp", param))
		{
			/* 将 "service.ntp" 替换为 "ntp" */

			char	*p;

			p = strstr(key, "service.ntp");

			do
			{
				*p = *(p + 8);
			}
			while ('\0' != *(p++));
		}

		/* 释放请求数据结构体 */
		free_request(&request);

		/* 将 "net.tcp.service" 替换为 "net.udp.service" */

		key[4] = 'u';
		key[5] = 'd';
		key[6] = 'p';

		key_esc = DBdyn_escape_string(key);

		/* 查询数据库中是否存在相同的 item，如果不存在，则更新 item key，否则打印警告日志并跳过当前行 */
		result2 = DBselect("select null from items where hostid=%s and key_='%s'", row[0], key_esc);

		if (NULL == DBfetch(result2))
		{
			if (ZBX_DB_OK > DBexecute("update items set key_='%s' where itemid=%s", key_esc, row[1]))
				ret = FAIL;
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot convert item key \"%s\":"
					" item with converted key \"%s\" already exists on host ID [%s]",
					row[2], key, row[0]);
		}
		DBfree_result(result2);

		/* 释放 key_esc 内存 */
		zbx_free(key_esc);
	}
	/* 释放 result 结果集 */
	DBfree_result(result);

	/* 释放 key 内存 */
	zbx_free(key);

	/* 返回函数执行结果 */
	return ret;
}

 *- 字段长度：2048
 *- 字段类型：ZBX_TYPE_CHAR（字符类型）
 *- 是否非空：ZBX_NOTNULL（非空）
 *- 其他未知参数：0
 *
 *通过调用 DBmodify_field_type 函数，将 \"hosts\" 表中的对应字段类型改为 ZBX_TYPE_CHAR（字符类型）。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050008 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_2050008(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储数据
	const ZBX_FIELD	field = {"ipmi_error", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBmodify_field_type 函数，修改 "hosts" 表中的字段类型
	return DBmodify_field_type("hosts", &field, NULL);
}


static int	DBpatch_2050009(void)
{
	const ZBX_FIELD	field = {"snmp_error", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBmodify_field_type("hosts", &field, NULL);
}

/******************************************************************************
 * *
 *这块代码的主要目的是修改 \"hosts\" 表中的某个字段类型。具体来说，它定义了一个 ZBX_FIELD 结构体变量 `field`，其中包含了要修改的字段名、字段类型、长度等信息。然后通过调用 `DBmodify_field_type` 函数来修改 \"hosts\" 表中指定字段的类型。
 *
 *输出：
 *
 *```c
 *static int DBpatch_2050010(void)
 *{
 *    // 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
 *    const ZBX_FIELD field = {\"jmx_error\", \"\", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};
 *
 *    // 调用 DBmodify_field_type 函数，修改 \"hosts\" 表中的字段类型
 *    // 参数1：要修改的表名，这里是 \"hosts\"
 *    // 参数2：指向 ZBX_FIELD 结构体的指针，这里是 &field
 *    // 参数3： NULL，表示不需要返回值
 *    return DBmodify_field_type(\"hosts\", &field, NULL);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_2050010 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_2050010(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"jmx_error", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBmodify_field_type 函数，修改 "hosts" 表中的字段类型
	// 参数1：要修改的表名，这里是 "hosts"
	// 参数2：指向 ZBX_FIELD 结构体的指针，这里是 &field
	// 参数3： NULL，表示不需要返回值
	return DBmodify_field_type("hosts", &field, NULL);
}



/******************************************************************************
 * *
 *这块代码的主要目的是更新 items 表中的 trends 字段，将其值设置为 0。更新操作的条件是 value_type 字段值为 1（ITEM_VALUE_TYPE_STR）、2（ITEM_VALUE_TYPE_LOG）或 4（ITEM_VALUE_TYPE_TEXT）。如果数据库操作成功，返回 SUCCEED；如果操作失败，返回 FAIL。
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050011 的静态函数，该函数不接受任何参数，返回一个整型值 */
static int	DBpatch_2050011(void)
{
	/* 定义一个常量，表示数据库操作成功的最小值 */
	static const int ZBX_DB_OK = 0;

	/* 执行更新操作，将 items 表中的 trends 字段设置为 0，条件是 value_type 字段值为 1、2 或 4 */
	if (ZBX_DB_OK <= DBexecute("update items set trends=0 where value_type in (1,2,4)"))
		/* 如果数据库操作成功，返回 SUCCEED，表示执行成功 */
		return SUCCEED;
	else
		/* 如果数据库操作失败，返回 FAIL，表示执行失败 */
		return FAIL;
}


static int	DBpatch_2050012(void)
{
	DB_RESULT	result;
	DB_RESULT	result2;
	DB_ROW		row;
	char		*key = NULL, *key_esc, *param;
	int		ret = SUCCEED;
	AGENT_REQUEST	request;

	/* type - ITEM_TYPE_ZABBIX, ITEM_TYPE_SIMPLE, ITEM_TYPE_ZABBIX_ACTIVE */
	result = DBselect(
			"select hostid,itemid,key_"
			" from items"
			" where type in (0,3,7)"
				" and key_ like 'net.tcp.service%%[%%ntp%%'");

	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		init_request(&request);

		if (SUCCEED != parse_item_key(row[2], &request))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot parse item key \"%s\"", row[2]);
			continue;
		}

		param = get_rparam(&request, 0);

		/* NULL check to silence static analyzer warning */
		if (NULL == param || (0 != strcmp("service.ntp", param) && 0 != strcmp("ntp", param)))
		{
			free_request(&request);
			continue;
		}

		key = zbx_strdup(key, row[2]);

		if (0 == strcmp("service.ntp", param))
		{
			/* replace "service.ntp" with "ntp" */

			char	*p;

			p = strstr(key, "service.ntp");

			do
			{
				*p = *(p + 8);
			}
			while ('\0' != *(p++));
		}

		free_request(&request);

		/* replace "net.tcp.service" with "net.udp.service" */

		key[4] = 'u';
		key[5] = 'd';
		key[6] = 'p';

		key_esc = DBdyn_escape_string(key);

		result2 = DBselect("select null from items where hostid=%s and key_='%s'", row[0], key_esc);

		if (NULL == DBfetch(result2))
		{
			if (ZBX_DB_OK > DBexecute("update items set key_='%s' where itemid=%s", key_esc, row[1]))
				ret = FAIL;
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot convert item key \"%s\":"
					" item with converted key \"%s\" already exists on host ID [%s]",
					row[2], key, row[0]);
		}
		DBfree_result(result2);

		zbx_free(key_esc);
	}
	DBfree_result(result);

	zbx_free(key);

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是删除名为 \"user_history\" 的数据库表。通过调用 DBdrop_table 函数来实现这个功能。如果删除操作成功，函数返回0，否则返回非0值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050013 的静态函数，该函数不接受任何参数，返回类型为整型
/******************************************************************************
 * *
 *整个代码块的主要目的是：更新 config 表中的 default_theme 字段。根据 default_theme 的值，将其更新为 'blue-theme' 或 'dark-theme'。如果更新操作成功，返回 SUCCEED（成功）；如果失败，返回 FAIL（失败）。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050014 的静态函数，该函数不接受任何参数，返回一个整型变量
static int	DBpatch_2050014(void)
{
	// 判断 DBexecute 函数执行的结果是否为 ZBX_DB_OK 或者更小的值
	if (ZBX_DB_OK <= DBexecute(
		// 执行一条 SQL 语句，更新 config 表中的 default_theme 字段
		"update config"
		" set default_theme="
			"case when default_theme in ('classic', 'originalblue')"
			" then 'blue-theme'"
			" else 'dark-theme' end"))
/******************************************************************************
 * *
 *这块代码的主要目的是更新 users 表中 theme 字段的值。根据给定的条件，将 theme 为 'classic' 或 'originalblue' 的用户主题更新为 'blue-theme'，其他用户主题更新为 'dark-theme'。如果更新操作成功，返回 SUCCEED（成功）；如果失败，返回 FAIL（失败）。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050015 的静态函数，该函数不接受任何参数，返回一个整型值
static int DBpatch_2050015(void)
{
	// 判断 DBexecute 函数执行的结果是否为 ZBX_DB_OK 或者更小的值
	if (ZBX_DB_OK <= DBexecute(
		// 执行以下 SQL 语句：
		"update users"
		// 更新 users 表中的 theme 字段
		" set theme=case when theme in ('classic', 'originalblue') then 'blue-theme' else 'dark-theme' end"
		// 更新条件：theme 字段不等于 'default'
		" where theme<>'default'"))
	{
		// 如果 DBexecute 执行成功，返回 SUCCEED（成功）
		return SUCCEED;
	}

/******************************************************************************
 * *
 *这块代码的主要目的是向数据库中添加一个名为 \"media_type\" 的字段，该字段的值为 0。以下是详细注释的代码块：
 *
 *
 *
 *这段代码定义了一个名为 DBpatch_2050020 的静态函数，该函数的主要目的是向名为 \"media_type\" 的数据库字段中插入一个值为 0 的整型数据。为实现此目的，代码首先定义了一个 ZBX_FIELD 结构体变量，并设置了相应的手动内存管理结构。然后，通过调用 DBadd_field 函数将该结构体添加到数据库中。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050020 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050020(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，其中包含以下字段：
	// 字段名：smtp_security
	// 字段值：0
	// 字段类型：ZBX_TYPE_INT（整型）
	// 是否非空：ZBX_NOTNULL（非空）
	// 其他未知字段：NULL
	const ZBX_FIELD	field = {"smtp_security", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义好的 field 结构体添加到数据库中
	// 参数1：media_type（媒体类型）
	// 参数2：指向 field 结构体的指针
	return DBadd_field("media_type", &field);
}


	return FAIL;
}

static int	DBpatch_2050015(void)
{
	if (ZBX_DB_OK <= DBexecute(
		"update users"
		" set theme=case when theme in ('classic', 'originalblue') then 'blue-theme' else 'dark-theme' end"
		" where theme<>'default'"))
	{
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050019 的静态函数，该函数用于将一个 ZBX_FIELD 结构体变量（包含 smtp_port 字段，值为25）添加到数据库中的 media_type 表中。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050019 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050019(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"smtp_port", "25", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到数据库中
	return DBadd_field("media_type", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向数据库中添加一个新字段。具体来说，代码首先定义了一个名为 field 的 ZBX_FIELD 结构体变量，其中包含了字段的名称、类型、是否非空等属性。然后，通过调用 DBadd_field 函数将这个字段添加到名为 \"media_type\" 的数据库表中。最后，返回新增字段的 ID。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050021 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050021(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"smtp_verify_peer", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到数据库中，参数为 "media_type"
	// 返回 DBadd_field 函数的执行结果，即新增字段的 ID
	return DBadd_field("media_type", &field);
}

}

static int	DBpatch_2050021(void)
{
	const ZBX_FIELD	field = {"smtp_verify_peer", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("media_type", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向数据库中添加一个名为 \"media_type\" 的字段，该字段的值为 \"0\"。以下是详细注释：
 *
 *1. 定义一个名为 DBpatch_2050022 的静态函数，该函数不接受任何参数，返回类型为 int。
 *2. 定义一个名为 field 的常量 ZBX_FIELD 结构体变量。结构体变量中包含了字段的名称、类型、是否非空等属性。
 *3. 调用 DBadd_field 函数，将定义的 field 结构体变量添加到数据库中。
 *4. 返回 DBadd_field 函数的执行结果，即添加字段的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050022 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050022(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"application_prototype\" 的数据库表。表的结构包括以下字段：application_prototypeid（索引字段，类型为ZBX_TYPE_ID，非空），itemid（非空），templateid（非空），name（长度为255，非空）。创建表的过程通过调用DBcreate_table函数实现。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050030 的静态函数，该函数不接受任何参数，返回类型为整型。
static int DBpatch_2050030(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量。
	// 该结构体用于表示数据库表的结构。
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体变量，表名为 "application_prototype"，并设置以下字段：
			{"application_prototype", "application_prototypeid", 0,
					// 定义表的字段结构体数组，包括以下字段：
					{
						// 字段1：application_prototypeid，类型为ZBX_TYPE_ID，非空，索引字段
						{"application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
						// 字段2：itemid，类型为ZBX_TYPE_ID，非空
						{"itemid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
						// 字段3：templateid，类型为ZBX_TYPE_ID，非空
						{"templateid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0},
						// 字段4：name，类型为ZBX_TYPE_CHAR，长度为255，非空
						{"name", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
						// 字段5：null，用于表示字段列表的结束
						{0}
					},
					// 表的字段结构体数组结束
					NULL
				};

	// 调用 DBcreate_table 函数，传入 table 参数，用于创建名为 "application_prototype" 的数据库表。
	// 函数返回值为创建表的结果，0表示成功，非0表示失败。
	return DBcreate_table(&table);
}

 *整个代码块的主要目的是：设置默认主题。
 *
 *这块代码的功能是定义一个名为 DBpatch_2050029 的静态函数，该函数无返回值，输入参数为一个空指针。在函数内部，首先定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储默认主题的信息。然后，调用 DBset_default 函数，传入参数 \"config\" 和 field 结构体变量，设置默认主题。最后，返回 DBset_default 函数的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050029 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050029(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"default_theme", "blue-theme", NULL, NULL, 128, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBset_default 函数，传入参数 "config" 和 field 结构体变量，设置默认主题
	return DBset_default("config", &field);
}



static int	DBpatch_2050029(void)
{
	const ZBX_FIELD	field = {"default_theme", "blue-theme", NULL, NULL, 128, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBset_default("config", &field);
}

static int	DBpatch_2050030(void)
{
	const ZBX_TABLE table =
			{"application_prototype", "application_prototypeid", 0,
				{
					{"application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"itemid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"templateid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0},
					{"name", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"application_prototype_1\" 的索引，基于 \"application_prototype\" 表的 \"itemid\" 列。如果创建索引成功，返回1；如果创建索引失败，返回-1。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050031 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_2050031(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "application_prototype_1" 的索引
    // 索引基于 "application_prototype" 表，添加 "itemid" 列
    // 参数1：表名，这里是 "application_prototype"
    // 参数2：索引名，这里是 "application_prototype_1"
    // 参数3：索引基于的列名，这里是 "itemid"
    // 参数4：索引类型，这里是 B-tree 索引，值为 0

    // 调用 DBcreate_index 函数后，将返回值赋给整型变量 result
    int result = DBcreate_index("application_prototype", "application_prototype_1", "itemid", 0);

    // 判断返回值是否为0，如果不是0，表示创建索引成功，返回1（表示成功）
    if (result != 0)
    {
        return 1;
    }
    else
    {
        // 如果创建索引失败，返回-1（表示失败）
        return -1;
    }
}


/******************************************************************************
 * *
 *注释已添加到原始代码块中，请参考上述代码。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050032 的静态函数，该函数为空函数（void 类型）
/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"application_prototype\" 表中添加一个外键约束。具体来说，代码首先定义了一个 ZBX_FIELD 结构体变量 `field`，用于存储外键约束信息。然后调用 DBadd_foreign_key 函数，将定义好的外键约束添加到 \"application_prototype\" 表中。如果添加成功，函数返回值为 0。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050033 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050033(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD field = {"itemid", NULL, "items", "itemid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向 "application_prototype" 表中添加外键约束
	// 参数1：要添加外键约束的表名
	// 参数2：主键列索引，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，用于存储外键约束信息

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"item_application_prototype\" 的表。表中有三个字段：item_application_prototypeid、application_prototypeid 和 itemid，均为非空字段。通过调用 DBcreate_table 函数来创建这个表。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050035 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050035(void)
{
	// 定义一个常量 ZBX_TABLE 类型的变量 table
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包括以下字段：
			{"item_application_prototype", "item_application_prototypeid", 0,
					// 定义一个包含多个键值对的数组，用于描述表的字段信息
					{
						{"item_application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL,
								// 定义字段 item_application_prototypeid，非空，类型为 ZBX_TYPE_ID
								0},
						{"application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL,
								// 定义字段 application_prototypeid，非空，类型为 ZBX_TYPE_ID
								0},
						{"itemid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL,
								// 定义字段 itemid，非空，类型为 ZBX_TYPE_ID
								0},
						{0} // 结束字段定义数组
					},
					// 结束表结构体初始化
					NULL
				};

	// 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
	return DBcreate_table(&table);
}

}

/******************************************************************************
 * *
 *整个代码块的主要目的是：向名为 \"application_prototype\" 的表中添加外键约束。具体来说，代码创建了一个 ZBX_FIELD 结构体变量，用于存储外键约束信息，然后调用 DBadd_foreign_key 函数将这个外键约束添加到 \"application_prototype\" 表中。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050034 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_2050034(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"templateid", NULL, "application_prototype", "application_prototypeid",
			// 设置 field 的相关属性，包括：关联表、主键列、外键列、删除级联方式
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向名为 "application_prototype" 的表中添加外键约束
	// 参数1：表名
	// 参数2：主键列索引，此处为 2
	// 参数3：指向 ZBX_FIELD 结构体的指针，包含外键约束信息
	return DBadd_foreign_key("application_prototype", 2, &field);
}


static int	DBpatch_2050035(void)
{
	const ZBX_TABLE table =
			{"item_application_prototype", "item_application_prototypeid", 0,
				{
					{"item_application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL,
							0},
					{"application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"itemid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"item_application_prototype_1\" 的索引，基于字段 \"application_prototypeid\" 和 \"itemid\"，并设置升序排序。函数 DBpatch_2050036 调用 DBcreate_index 函数来实现这一目的，并返回创建索引的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050036 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_2050036(void)
/******************************************************************************
 * *
 *注释已添加到原始代码块中，以下是完整注释的代码：
 *
 *```c
 */**
 * * @file DBpatch_2050037.c
 * * @brief 创建索引的函数
 * * @author YourName
 * * @date 2022-01-01
 * * @description 创建一个名为 \"item_application_prototype_2\" 的索引，基于 \"item_application_prototype\" 表，索引字段为 \"itemid\"，设置为非动态索引
 * */
 *
 *#include <stdio.h>
 *
 *static int\tDBpatch_2050037(void)
 *{
 *    // 调用 DBcreate_index 函数，用于创建索引
 *    return DBcreate_index(\"item_application_prototype\", \"item_application_prototype_2\", \"itemid\", 0);
 *}
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"application_discovery\" 的数据库表。表中包含 application_discoveryid、applicationid、application_prototypeid、name、lastcheck 和 ts_delete 六个字段。其中，application_discoveryid 为主键，non-null 表示非空。name 字段最大长度为 255。lastcheck 和 ts_delete 字段均为非空，默认值为 0。最后，调用 DBcreate_table 函数来创建表。
 ******************************************************************************/
static int	DBpatch_2050040(void) // 定义一个名为 DBpatch_2050040 的静态函数，用于创建数据库表
{
	const ZBX_TABLE table = // 定义一个名为 table 的常量 ZBX_TABLE 结构体
			{"application_discovery", "application_discoveryid", 0, // 定义表名和主键名，这里的 0 表示没有额外参数
				{
					{"application_discoveryid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0}, // 定义一个名为 application_discoveryid 的字段，类型为 ZBX_TYPE_ID，非空
					{"applicationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0}, // 定义一个名为 applicationid 的字段，类型为 ZBX_TYPE_ID，非空
					{"application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0}, // 定义一个名为 application_prototypeid 的字段，类型为 ZBX_TYPE_ID，非空
					{"name", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0}, // 定义一个名为 name 的字段，类型为 ZBX_TYPE_CHAR，非空，最大长度为 255
					{"lastcheck", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0}, // 定义一个名为 lastcheck 的字段，类型为 ZBX_TYPE_INT，非空，默认值为 0
					{"ts_delete", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0}, // 定义一个名为 ts_delete 的字段，类型为 ZBX_TYPE_INT，非空，默认值为 0
					{0} // 结束字段定义
				},
				NULL // 结束 ZBX_TABLE 结构体定义
			};

	return DBcreate_table(&table); // 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
}

 *
 *    return 0;
 *}
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_2050037 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050037(void)
{
    // 调用 DBcreate_index 函数，用于创建索引
    return DBcreate_index("item_application_prototype", "item_application_prototype_2", "itemid", 0);
}

// 整个代码块的主要目的是创建一个名为 "item_application_prototype_2" 的索引，该索引基于 "item_application_prototype" 表，索引字段为 "itemid"，并设置索引为非动态索引。


/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"item_application_prototype\" 表中添加一条外键约束，约束的列名为 \"application_prototypeid\"，对应的数据库列为 \"application_prototype\"。当 \"application_prototypeid\" 发生变化时，采用级联删除方式删除相关记录。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050038 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050038(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
/******************************************************************************
 * *
 *整个代码块的主要目的是向数据表 \"item_application_prototype\" 添加一个外键约束。具体来说，代码通过定义一个 ZBX_FIELD 结构体变量来存储外键字段的信息，然后调用 DBadd_foreign_key 函数将这个外键约束添加到数据表中。注释中已经对每一行代码进行了详细的解释，帮助你更好地理解这个代码块的功能。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050039 的静态函数，该函数不接受任何参数，返回一个整型值
static int DBpatch_2050039(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储数据表中的字段信息
	const ZBX_FIELD field = {"itemid", NULL, "items", "itemid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据表 "item_application_prototype" 添加外键约束
	// 参数1：要添加外键的数据表名称
	// 参数2：外键字段1的索引，此处为2
	// 参数3：指向 ZBX_FIELD 结构体的指针，用于存储外键字段的信息
	return DBadd_foreign_key("item_application_prototype", 2, &field);
}

	// 参数2：主键列名，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针， containing the foreign key information
	return DBadd_foreign_key("item_application_prototype", 1, &field);
}

}

static int	DBpatch_2050038(void)
{
	const ZBX_FIELD	field = {"application_prototypeid", NULL, "application_prototype", "application_prototypeid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("item_application_prototype", 1, &field);
}

static int	DBpatch_2050039(void)
{
	const ZBX_FIELD	field = {"itemid", NULL, "items", "itemid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("item_application_prototype", 2, &field);
}

static int	DBpatch_2050040(void)
{
	const ZBX_TABLE table =
			{"application_discovery", "application_discoveryid", 0,
				{
					{"application_discoveryid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"applicationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"application_prototypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"name", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"lastcheck", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"ts_delete", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *这块代码的主要目的是创建一个名为 \"application_discovery_1\" 的索引，该索引基于 \"applicationid\" 列。需要注意的是，这里使用的是 DBcreate_index 函数，而不是常见的 CreateIndex 函数。在这里，返回值表示创建索引的成功与否。如果创建成功，返回值为 0，否则返回一个非零值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050041 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050041(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "application_discovery_1" 的索引
    // 索引基于 "applicationid" 列，不创建主键
    // 返回创建索引的返回值
    return DBcreate_index("application_discovery", "application_discovery_1", "applicationid", 0);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"application_discovery_2\" 的索引，该索引基于 \"application_discovery\" 表的 \"application_prototypeid\" 列。如果创建索引成功，函数返回 0，否则返回一个错误码。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050042 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050042(void)
{
    // 调用 DBcreate_index 函数，传入三个参数：第一个参数为索引名称，第二个参数为索引文件名，第三个参数为原始表名，最后一个参数为索引列名，此处为 0，表示不创建唯一索引
    // 返回 DBcreate_index 函数的返回值，用于判断索引创建是否成功
}



/******************************************************************************
 * *
 *整个代码块的主要目的是：向名为 application_discovery 的表中添加一条外键约束，约束的列是 applicationid，当 applicationid 发生变化时，采用级联删除方式删除相关记录。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050043 的静态函数，该函数为空返回类型为整数的函数
static int	DBpatch_2050043(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"applicationid", NULL, "applications", "applicationid", 0, 0, 0,
			// 设置外键约束，这里是应用发现表（application_discovery）的主键 applicationid 列
			ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：要添加外键约束的表名（application_discovery）
	// 参数2：主键列索引（1，表示 applicationid 列）
	// 参数3：指向 ZBX_FIELD 结构体的指针（&field），用于存储外键约束信息
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050045 的静态函数，该函数用于向 \"applications\" 数据库表中添加一个名为 \"flags\" 的字段，字段类型为整型（ZBX_TYPE_INT），非空（ZBX_NOTNULL），并设置默认值为 0。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050045 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050045(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"flags", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 "applications" 数据库表中
	return DBadd_field("applications", &field);
}

 *整个代码块的主要目的是向 \"application_discovery\" 表中添加一条外键约束，约束的列名为 \"application_prototypeid\"，设置级联操作均为 CASCADE。具体添加外键约束的操作通过调用 DBadd_foreign_key 函数实现。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050044 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050044(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD	field = {"application_prototypeid", NULL, "application_prototype", "application_prototypeid",
			// 设置 field 的相关参数，包括：索引、删除级联、插入级联、更新级联、删除级联
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向 "application_discovery" 表中添加外键约束
	// 参数1：表名
	// 参数2：主键列索引
	// 参数3：外键约束信息地址，这里是指向 field 结构体的指针
	return DBadd_foreign_key("application_discovery", 2, &field);
}


static int	DBpatch_2050045(void)
{
	const ZBX_FIELD field = {"flags", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};
/******************************************************************************
 * *
 *整个代码块的主要目的是：从名为 \"config\" 的数据库表中查询 severity_color_0 到 severity_color_5 的颜色值，如果查询结果符合预期，则执行更新操作，将颜色值更新为新的值。函数执行成功则返回 SUCCEED，失败则返回 FAIL。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050055 的静态函数，该函数不接受任何参数，返回类型为 int
static int DBpatch_2050055(void)
{
	// 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果
	DB_RESULT	result;

	// 定义一个 DB_ROW 类型的变量 row，用于存储数据库查询结果的行数据
	DB_ROW		row;

	// 定义一个 int 类型的变量 ret，初始值为 FAIL，用于存储函数执行结果
	int		ret = FAIL;

	// 执行数据库查询，从 config 表中选取 severity_color_0 到 severity_color_5 的数据
	if (NULL == (result = DBselect(
			"select severity_color_0,severity_color_1,severity_color_2,severity_color_3,severity_color_4,"
				"severity_color_5"
			" from config")))
	{
		// 如果查询失败，返回 FAIL
		return FAIL;
	}

	// 从查询结果中获取一行数据，存储在 row 变量中
	if (NULL != (row = DBfetch(result)) &&
			0 == strcmp(row[0], "DBDBDB") && 0 == strcmp(row[1], "D6F6FF") &&
			0 == strcmp(row[2], "FFF6A5") && 0 == strcmp(row[3], "FFB689") &&
			0 == strcmp(row[4], "FF9999") && 0 == strcmp(row[5], "FF3838"))
	{
		// 如果数据行符合条件，执行更新操作
		if (ZBX_DB_OK > DBexecute(
				"update config set severity_color_0='97AAB3',severity_color_1='7499FF',"
					"severity_color_2='FFC859',severity_color_3='FFA059',"
					"severity_color_4='E97659',severity_color_5='E45959'"))
		{
			// 如果更新操作失败，跳转到 out 标签处
			goto out;
		}
	}

	// 如果执行成功，将 ret 设置为 SUCCEED
	ret = SUCCEED;

out:
	// 释放查询结果内存
	DBfree_result(result);

	// 返回执行结果
	return ret;
}

 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050052 的静态函数，该函数用于将一个 ZBX_FIELD 结构体变量（包含字段名、类型、是否非空等信息）添加到名为 \"config\" 的数据库表中。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050052 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050052(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"opinventory\" 的表，其中包含两个字段：operationid（类型为 ZBX_TYPE_ID）和 inventory_mode（类型为 ZBX_TYPE_INT）。表的创建是通过调用 DBcreate_table 函数来实现的。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050053 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050053(void)
{
	// 定义一个常量 ZBX_TABLE 类型的变量 table
	const ZBX_TABLE table =
			// 初始化 table 结构体，包括以下内容：
			{"opinventory", "operationid", 0,
					// 定义一个包含两个字段的表结构体
					{
						// 第一个字段：operationid，非空，类型为 ZBX_TYPE_ID
						{"operationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
						// 第二个字段：inventory_mode，非空，类型为 ZBX_TYPE_INT
						{"inventory_mode", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
						// 字段列表结束标志
						{0}
					},
					// 表结构体结束标志
					NULL
				};

	// 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
	return DBcreate_table(&table);
}

					{"inventory_mode", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是向名为\"opinventory\"的表中添加一条外键约束。具体来说，代码首先定义了一个名为field的ZBX_FIELD结构体变量，用于存储外键约束信息，包括关联表名、主键列名、外键列名、删除约束等。然后，调用DBadd_foreign_key函数将这条外键约束添加到数据库中。如果添加成功，函数返回0，否则返回非0值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050054 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_2050054(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD	field = {"operationid", NULL, "operations", "operationid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：要添加外键约束的表名，这里是 "opinventory"
	// 参数2：主键列名，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
	// 返回值：操作结果，0表示成功，非0表示失败
	return DBadd_foreign_key("opinventory", 1, &field);
}


static int	DBpatch_2050055(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	if (NULL == (result = DBselect(
			"select severity_color_0,severity_color_1,severity_color_2,severity_color_3,severity_color_4,"
				"severity_color_5"
			" from config")))
	{
		return FAIL;
	}

	if (NULL != (row = DBfetch(result)) &&
			0 == strcmp(row[0], "DBDBDB") && 0 == strcmp(row[1], "D6F6FF") &&
			0 == strcmp(row[2], "FFF6A5") && 0 == strcmp(row[3], "FFB689") &&
			0 == strcmp(row[4], "FF9999") && 0 == strcmp(row[5], "FF3838"))
	{
		if (ZBX_DB_OK > DBexecute(
				"update config set severity_color_0='97AAB3',severity_color_1='7499FF',"
					"severity_color_2='FFC859',severity_color_3='FFA059',"
					"severity_color_4='E97659',severity_color_5='E45959'"))
		{
			goto out;
		}
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是设置名为 \"config\" 的数据库表的一个字段。具体来说，它定义了一个 ZBX_FIELD 结构体变量（field），用于存储字段的属性，如字段名、颜色、长度等。然后调用 DBset_default 函数，将这个字段的属性设置为 field 中的值。最后，返回 DBset_default 函数的执行结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050056 的静态函数
static int DBpatch_2050056(void)
{
/******************************************************************************
 * *
 *这块代码的主要目的是在名为 \"config\" 的数据库表中设置一个字符串类型的字段 \"severity_color_1\" 的默认值。代码通过定义一个 ZBX_FIELD 结构体变量来描述这个字段的属性，包括字段名、颜色、长度、数据类型、是否非空等。然后调用 DBset_default 函数，将表名和字段属性传入，完成设置默认值的操作。最后返回操作结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050057 的静态函数
static int DBpatch_2050057(void)
{
/******************************************************************************
 * *
 *这块代码的主要目的是设置一个名为 \"config\" 的数据库表为默认表，并为该表定义一个名为 \"severity_color_2\" 的字段，该字段的值为 \"FFC859\"。以下是详细注释：
 *
 *1. 定义一个名为 DBpatch_2050058 的静态函数，表示这是一个静态函数。
 *2. 定义一个名为 field 的常量 ZBX_FIELD 结构体变量，用于存储数据库表字段的信息。
 *3. 字段名称为 \"severity_color_2\"，字段值为 \"FFC859\"，其他字段属性为 NULL，表示不需要其他特殊设置。
 *4. 设置字段类型为 ZBX_TYPE_CHAR，表示该字段是一个字符类型字段。
 *5. 设置字段非空，即 ZBX_NOTNULL，表示该字段不能为空。
 *6. 设置字段长度为 6。
 *7. 调用 DBset_default 函数，将名为 \"config\" 的数据库表设置为默认表。
 *8. 将 field 结构体变量作为参数传递给 DBset_default 函数，以便设置默认表的字段信息。
 *
 *整个注释好的代码块如下：
 *
 *```c
 *static int\tDBpatch_2050058(void)
 *{
 *\tconst ZBX_FIELD field = {\"severity_color_2\", \"FFC859\", NULL, NULL, 6, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};
 *
 *\treturn DBset_default(\"config\", &field);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_2050058 的静态函数
static int DBpatch_2050058(void)
{
    // 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
    const ZBX_FIELD field = {"severity_color_2", "FFC859", NULL, NULL, 6, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

    // 调用 DBset_default 函数，将名为 "config" 的数据库表设置为默认表，并将 field 结构体变量作为参数传递
    return DBset_default("config", &field);
}

}
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个名为 DBpatch_2050059 的静态函数，用于设置数据库中的配置信息。
 *
 *代码详细注释如下：
 *
 *1. 定义一个名为 field 的常量 ZBX_FIELD 结构体变量，用于存储配置信息。
 *   - 字符串字段名：severity_color_3
 *   - 字符串字段值：FFA059
 *   - 字段类型：ZBX_TYPE_CHAR（字符型）
 *   - 是否非空：ZBX_NOTNULL（非空）
 *   - 字段长度：6
 *
 *2. 调用 DBset_default 函数，将 field 结构体中的配置信息存储到数据库中的 \"config\" 表中。
 *   - 参数1：数据库表名，这里是 \"config\"
 *   - 参数2：指向 ZBX_FIELD 结构体的指针，这里是 &field
 *   - 函数返回值：操作结果，0表示成功，非0表示失败
 *
 *3. 函数返回值被忽略，但这里我们可以推测出该函数的返回值可能是设置配置信息的操作结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050059 的静态函数
static int DBpatch_2050059(void)
{
    // 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
    const ZBX_FIELD field = {"severity_color_3", "FFA059", NULL, NULL, 6, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

    // 调用 DBset_default 函数，将配置信息存储到数据库中
    // 参数1：数据库表名，这里是 "config"
    // 参数2：指向 ZBX_FIELD 结构体的指针，这里是 &field
    // 函数返回值：操作结果，0表示成功，非0表示失败
    return DBset_default("config", &field);
}

	const ZBX_FIELD field = {"severity_color_1", "7499FF", NULL, NULL, 6, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBset_default("config", &field);
}

static int	DBpatch_2050058(void)
{
	const ZBX_FIELD field = {"severity_color_2", "FFC859", NULL, NULL, 6, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBset_default("config", &field);
}

static int	DBpatch_2050059(void)
{
	const ZBX_FIELD field = {"severity_color_3", "FFA059", NULL, NULL, 6, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBset_default("config", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是设置一个名为 field 的 ZBX_FIELD 结构体变量，并调用 DBset_default 函数将其作为默认值配置。输出结果为整数类型，表示设置默认值的操作是否成功。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050060 的静态函数
static int DBpatch_2050060(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是在 Zabbix 数据库中创建或更新一个名为 \"config\" 的表，并为该表添加一条记录。代码通过定义一个 ZBX_FIELD 结构体变量来描述记录的字段信息，然后调用 DBset_default 函数将表名和字段信息传入，实现对数据库的操作。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050061 的静态函数
static int DBpatch_2050061(void)
{
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数 DBpatch_2050062，该函数用于向数据库中添加一个名为 \"media_type\" 的字段，并设置其相关属性。
 *
 *函数内部首先定义了一个名为 field 的常量 ZBX_FIELD 结构体变量，用于存储字段的属性。然后，调用 DBadd_field 函数，将 field 结构体变量添加到数据库中，并为 \"media_type\" 字段赋值。最后，返回 DBadd_field 函数的执行结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050062 的静态函数，该函数为空函数（void 类型）
static int DBpatch_2050062(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"exec_params", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到数据库中，并为 "media_type" 字段赋值
	return DBadd_field("media_type", &field);
}

}

}

/******************************************************************************
 * *
 *这块代码的主要目的是向 \"hosts\" 表中添加一个名为 \"tls_connect\" 的字段，字段类型为 ZBX_TYPE_INT（整型），非空（ZBX_NOTNULL），其他参数为默认值。添加结果通过 DBadd_field 函数返回。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050064 的静态函数，该函数为空函数（void 类型）
static int DBpatch_2050064(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {
		// 字段名称为 "tls_connect"，类型为 ZBX_TYPE_INT，非空，其他参数为空
		{"tls_connect", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0}
	};

	// 调用 DBadd_field 函数，将定义的字段添加到 "hosts" 表中，并返回添加结果
	return DBadd_field("hosts", &field);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：更新 media_type 表中 type 为 1 的记录的 exec_params 字段值为 {'ALERT.SENDTO'，'ALERT.SUBJECT'，'ALERT.MESSAGE'}。如果数据库操作失败，返回 FAIL；如果成功，返回 SUCCEED。
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050063 的静态函数，该函数为空，即没有返回值 */
static int	DBpatch_2050063(void)
{
	/* 判断 ZBX_DB_OK 是否大于 DBexecute 执行的结果，即判断数据库操作是否成功 */
	if (ZBX_DB_OK > DBexecute("update media_type"
			" set exec_params='{ALERT.SENDTO}\
{ALERT.SUBJECT}\
{ALERT.MESSAGE}\
'"
			" where type=1"))
	{
		/* 如果数据库操作失败，返回 FAIL */
		return FAIL;
	}

	/* 如果数据库操作成功，返回 SUCCEED */
	return SUCCEED;
}

	if (ZBX_DB_OK > DBexecute("update media_type"
			" set exec_params='{ALERT.SENDTO}\n{ALERT.SUBJECT}\n{ALERT.MESSAGE}\n'"
			" where type=1"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_2050064(void)
{
	const ZBX_FIELD field = {"tls_connect", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("hosts", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向 \"hosts\" 数据库表中添加一个名为 \"tls_accept\" 的字段，字段类型为整数（ZBX_TYPE_INT），非空（ZBX_NOTNULL），并设置默认值为 1。整个代码块通过调用 DBadd_field 函数来实现这个功能。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050065 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050065(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"tls_accept", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 "hosts" 数据库表中
	return DBadd_field("hosts", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个名为 DBpatch_2050067 的静态函数，用于向 \"hosts\" 数据库表中添加一个名为 \"tls_subject\" 的字段。其中，字段的长度为 1024 字节，类型为 ZBX_TYPE_CHAR，非空。
 *
 *注释详细解释：
 *
 *1. 定义一个名为 DBpatch_2050067 的静态函数，表示这个函数在整个程序中只被编译一次，每次调用时直接从内存中取出。
 *
 *2. 定义一个名为 field 的常量 ZBX_FIELD 结构体变量。这个结构体变量包含了字段的名称为 \"tls_subject\"，字符串长度为 1024 字节，类型为 ZBX_TYPE_CHAR（即字符类型），不允许为空。
 *
 *3. 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 \"hosts\" 数据库表中。这个函数的主要作用是将指定的字段添加到数据库表中，以便在后续的操作中使用。
 *
 *整个代码块的目的是向 \"hosts\" 数据库表中添加一个名为 \"tls_subject\" 的字段。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050067 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050067(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"tls_subject", "", NULL, NULL, 1024, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 "hosts" 数据库表中
	return DBadd_field("hosts", &field);
}


static int	DBpatch_2050066(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量，该结构体用于描述数据库中的字段。
	const ZBX_FIELD field = {"tls_issuer", "", NULL, NULL, 1024, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的字段添加到名为 "hosts" 的数据库表中，并返回添加结果。
	return DBadd_field("hosts", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 DBpatch_2050068 的静态函数，该函数用于向 \"hosts\" 表中添加一个字段。在这个函数中，首先定义了一个 ZBX_FIELD 结构体变量 field，并对其进行了初始化，设置了字段名称、类型、长度、属性等参数。接着，调用 DBadd_field 函数将定义的字段添加到 \"hosts\" 表中。最后，返回添加字段的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050068 的静态函数，该函数为空函数（void 类型）
static int DBpatch_2050068(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {
		// 设置字段名称为 "tls_psk_identity"
		"tls_psk_identity",
		// 设置字段类型为字符型（ZBX_TYPE_CHAR）
		"",
		// 设置字段长度为 128
		NULL,
		NULL,
		// 设置字段属性，非空（ZBX_NOTNULL）
		128,
		// 设置字段类型为字符型（ZBX_TYPE_CHAR）
		ZBX_TYPE_CHAR,
		// 设置字段非空（ZBX_NOTNULL）
		0
	};

	// 调用 DBadd_field 函数，将定义的字段添加到 "hosts" 表中
	// 参数1：表名（"hosts"）
	// 参数2：指向字段的指针（&field）
	return DBadd_field("hosts", &field);
}

}
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050069 的静态函数，该函数用于将一个 ZBX_FIELD 结构体变量（包含字段名、字段类型、字段长度等信息）添加到名为 \"hosts\" 的数据库表中。
 *
 *输出：
 *
 *```c
 *#include <zbx.h>
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_2050069 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050069(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"tls_psk", "", NULL, NULL, 512, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 "hosts" 数据库表中
	return DBadd_field("hosts", &field);
}

}

static int	DBpatch_2050069(void)
{
	const ZBX_FIELD field = {"tls_psk", "", NULL, NULL, 512, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("hosts", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是修改全局宏定义的字段类型。具体来说，它定义了一个名为 field 的 ZBX_FIELD 结构体变量，用于存储要修改的字段信息，然后调用 DBmodify_field_type 函数来修改名为 \"globalmacro\" 的全局宏定义的字段类型。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050070 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_2050070(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是修改名为 \"hostmacro\" 的字段类型。函数 DBpatch_2050071 创建了一个 ZBX_FIELD 结构体变量，用于存储要修改的字段信息，然后调用 DBmodify_field_type 函数进行修改。修改完成后，返回修改后的字段类型。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050071 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050071(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，以下是对该结构体变量的初始化
	const ZBX_FIELD	field = {"macro", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBmodify_field_type 函数，修改 "hostmacro" 字段的类型
	// 参数1：要修改的字段名，"hostmacro"
	// 参数2：指向 ZBX_FIELD 结构体的指针，用于接收修改后的字段信息
	// 参数3：空指针，表示不需要返回修改前的字段信息
	return DBmodify_field_type("hostmacro", &field, NULL);
}



static int	DBpatch_2050071(void)
{
	const ZBX_FIELD	field = {"macro", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBmodify_field_type("hostmacro", &field, NULL);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"sysmaps\" 的数据库表中添加一个字段，字段的名称为 \"userid\"，类型为 ZBX_TYPE_ID。函数 DBpatch_2050077 是一个静态函数，用于实现这个功能。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050077 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_2050077(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"userid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};

	// 调用 DBadd_field 函数，将 field 结构体中的数据添加到名为 "sysmaps" 的数据库表中
	return DBadd_field("sysmaps", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是更新数据库中的 sysmaps 表，将 userid 设置为 users 表中 type 为 3（表示超级管理员）的用户的最小 userid。如果更新操作执行成功，返回 SUCCEED；如果执行失败，返回 FAIL。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050078 的静态函数，该函数无需传入任何参数
static int DBpatch_2050078(void)
{
	// 判断 DBexecute 执行的结果是否为 ZBX_DB_OK（表示执行成功）
	if (ZBX_DB_OK > DBexecute("update sysmaps set userid=(select min(userid) from users where type=3)"))
	{
		// 如果执行成功，返回 FAIL（表示失败）
		return FAIL;
	}

	// 如果执行成功，返回 SUCCEED（表示成功）
	return SUCCEED;
}


/******************************************************************************
 * *
 *这块代码的主要目的是设置 \"sysmaps\" 表中的 userid 字段为非空。通过定义一个 ZBX_FIELD 结构体变量 field，存储 userid 字段的相关信息，然后调用 DBset_not_null 函数来实现设置字段非空的功能。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050079 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_2050079(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"sysmaps\" 的表中添加一条外键约束，该约束关联的字段名为 \"userid\"，字段类型为未知（NULL），字段标签为 \"users\"，其他参数设置为0。函数返回添加外键约束的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050080 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_2050080(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下内容：
	// 字段名：userid
	// 字段类型：未知（NULL）
	// 字段标签：users
	// 字段键：userid
	// 字段长度：0
	// 字段小数位：0
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 sysmap_user 的数据库表，表结构如上所示。函数 DBpatch_2050082 用于实现这个目的。表包含以下列：sysmapuserid、sysmapid、userid 和 permission。其中，sysmapuserid、sysmapid 和 userid 列为 ID 类型，非空；permission 列为整型类型，非空，默认值为 2。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050082 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050082(void)
{
	// 定义一个常量 ZBX_TABLE 类型的变量 table，用于存储数据库表结构
	const ZBX_TABLE table =
		{"sysmap_user",	// 表名
			"sysmapuserid",	// 列名：sysmapuserid
			0,		// 列数，此处为 0，表示没有更多的列
			// 定义表结构，包括以下列：
			{
				{"sysmapuserid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},	// 列名：sysmapuserid，类型：ID，非空，无默认值
				{"sysmapid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},		// 列名：sysmapid，类型：ID，非空，无默认值
				{"userid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},		// 列名：userid，类型：ID，非空，无默认值
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},	// 列名：permission，类型：整型，非空，默认值为 2
				{0}		// 结束标记
			},
			NULL	// 结束标记
		};

	// 调用 DBcreate_table 函数，创建名为 sysmap_user 的数据库表，表结构定义为 table
	return DBcreate_table(&table);
}

 *这块代码的主要目的是在一个名为 \"sysmaps\" 的数据库表中添加一个新字段。函数 DBpatch_2050081 是一个静态函数，它接收空参数，返回一个整数值。在函数内部，首先定义了一个 ZBX_FIELD 结构体变量 field，用于存储要添加的字段信息。然后，调用 DBadd_field 函数将 field 结构体中的数据添加到名为 \"sysmaps\" 的数据库表中。整个代码块的输出结果取决于 DBadd_field 函数的执行结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050081 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050081(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"private", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将 field 结构体中的数据添加到名为 "sysmaps" 的数据库表中
	return DBadd_field("sysmaps", &field);
}

}

static int	DBpatch_2050081(void)
{
	const ZBX_FIELD field = {"private", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("sysmaps", &field);
}

static int	DBpatch_2050082(void)
{
	const ZBX_TABLE table =
		{"sysmap_user",	"sysmapuserid",	0,
			{
				{"sysmapuserid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"sysmapid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"userid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
/******************************************************************************
 * *
 *整个代码块的主要目的是：向名为 \"sysmap_user\" 的表中添加一条外键约束。代码通过调用 DBadd_foreign_key 函数来实现这一功能。传入的参数分别为表名、外键约束类型（这里是 1，表示主键约束）以及指向 ZBX_FIELD 结构体的指针。最后，函数返回添加外键约束的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050084 的静态函数，该函数为空返回类型为整型的函数
static int	DBpatch_2050084(void)
{
/******************************************************************************
/******************************************************************************
 * *
 *这段代码的主要目的是更新用户表（users）中的 URL 字段。程序首先从数据库中查询用户信息，然后遍历每个用户的记录，检查 URL 字段是否符合预期格式。如果不符合，程序会尝试按照预定的规则进行转换，并将转换后的 URL 字符串更新到数据库中。如果转换过程中遇到 URL 过长的情况，程序会记录一条日志，然后将 URL 设置为空。最后，程序返回更新操作的结果。
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050092 的静态函数，该函数用于更新用户表中的 URL 字段。
*/
static int	DBpatch_2050092(void)
{
	/* 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果。
	*/
	DB_RESULT	result;

	/* 定义一个 DB_ROW 类型的变量 row，用于存储数据库行的数据。
	*/
	DB_ROW		row;

	/* 定义两个指向字符串的指针，end 和 start，用于处理 URL 字符串。
	*/
	const char	*end, *start;

	/* 定义一个整型变量 len，用于存储 URL 字符串的长度。
	*/
	int		len, ret = FAIL, rc;

	/* 定义一个指向 URL 字符串的指针，url，初始值为 NULL。
	*/
	char		*url = NULL;

	/* 定义一个整型变量 url_alloc，用于存储 URL 字符串的最大长度。
	*/
	size_t		url_alloc = 0;

	/* 定义一个整型变量 url_offset，用于计算 URL 字符串的长度。
	*/
	size_t		url_offset;

	/* 定义一个字符串数组 url_map，用于存储不同的 URL 前缀。
	*/
	const char	*url_map[] = {
				"dashboard.php", "dashboard.view",
				"discovery.php", "discovery.view",
				"maps.php", "map.view",
				"httpmon.php", "web.view",
				"media_types.php", "mediatype.list",
				"proxies.php", "proxy.list",
				"scripts.php", "script.list",
				"report3.php", "report.services",
				"report1.php", "report.status"
			};

	/* 如果数据库查询结果为空，返回 FAIL。
	*/
	if (NULL == (result = DBselect("select userid,url from users where url<>''")))
		return FAIL;

	/* 循环获取数据库中的每一行数据，直到遍历完毕。
	*/
	while (NULL != (row = (DBfetch(result))))
	{
		/* 找到 URL 字符串中的 '?' 符号位置，将其之前的字符作为 start，之后的部分作为 end。
		*/
		if (NULL == (end = strchr(row[1], '?')))
			end = row[1] + strlen(row[1]);

		/* 从 end 向前遍历，找到最后一个 '/' 符号的位置，作为 start。
		*/
		for (start = end - 1; start > row[1] && '/' != start[-1]; start--)
			;

		/* 计算 URL 字符串的长度。
		*/
		len = end - start;

		/* 遍历 url_map 数组，查找匹配的 URL 前缀。
		*/
		for (i = 0; ARRSIZE(url_map) > i; i += 2)
		{
			if (0 == strncmp(start, url_map[i], len))
				break;
		}

		/* 如果没有找到匹配的 URL 前缀，继续遍历下一行数据。
		*/
		if (ARRSIZE(url_map) == i)
			continue;

		/* 初始化 URL 字符串的长度为 0。
		*/
		url_offset = 0;

		/* 拷贝 URL 字符串的前部分，直到遇到 '&' 符号。
		*/
		zbx_strncpy_alloc(&url, &url_alloc, &url_offset, row[1], start - row[1]);

		/* 在 URL 字符串末尾添加 '&' 符号。
		*/
		zbx_strcpy_alloc(&url, &url_alloc, &url_offset, "zabbix.php?action=");

		/* 拷贝 URL 地图中对应的 URL 后缀。
		*/
		zbx_strcpy_alloc(&url, &url_alloc, &url_offset, url_map[i + 1]);

		/* 如果 URL 字符串末尾还有其他字符，添加到 URL 字符串中。
		*/
		if ('\0' != *end)
		{
			/* 在 URL 字符串末尾添加 '&' 符号。
			*/
			zbx_chrcpy_alloc(&url, &url_alloc, &url_offset, '&');

			/* 拷贝 URL 字符串末尾的部分。
			*/
			zbx_strcpy_alloc(&url, &url_alloc, &url_offset, end + 1);
		}

		/* 如果 URL 字符串长度超过 255，截断 URL 字符串。
		*/
		if (url_offset > 255)
		{
			/* 设置 URL 字符串为空。
			*/
			*url = '\0';

			/* 记录日志，提示 URL 字符串过长。
			*/
			zabbix_log(LOG_LEVEL_WARNING, "Cannot convert URL for user id \"%s\":"
					" value is too long. The URL field was reset.", row[0]);
		}

		/* 对 URL 字符串进行转义，并将其存储到数据库中。
		*/
		url_esc = DBdyn_escape_string(url);

		/* 执行更新操作，更新 URL 字段。
		*/
		rc = DBexecute("update users set url='%s' where userid=%s", url_esc, row[0]);

		/* 如果更新操作失败，跳出循环。
		*/
		if (ZBX_DB_OK > rc)
			goto out;
	}

	/* 更新成功，设置 ret 为 SUCCEED。
	*/
	ret = SUCCEED;

out:
	/* 释放 URL 字符串分配的内存。
	*/
	zbx_free(url);

	/* 释放数据库查询结果。
	*/
	DBfree_result(result);

	/* 返回更新操作的结果。
	*/
	return ret;
}

	return DBcreate_table(&table);
}

}

static int	DBpatch_2050085(void)
{
	const ZBX_FIELD	field = {"userid", NULL, "users", "userid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("sysmap_user", 2, &field);
}

static int	DBpatch_2050086(void)
{
	const ZBX_TABLE table =
		{"sysmap_usrgrp", "sysmapusrgrpid", 0,
			{
				{"sysmapusrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"sysmapid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"usrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"sysmap_usrgrp_1\" 的索引，该索引依据的列为 \"sysmapid\" 和 \"usrgrpid\"，索引顺序为 1。函数 DBpatch_2050087 是一个静态函数，无返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050087 的静态函数，该函数为 void 类型（无返回值）
static int DBpatch_2050087(void)
{
    // 调用 DBcreate_index 函数，用于创建一个名为 "sysmap_usrgrp_1" 的索引
    // 索引依据的列为 "sysmapid" 和 "usrgrpid"
    // 索引的顺序为 1

    // 函数返回值为 DBcreate_index 的执行结果
}


/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"sysmap_usrgrp\" 的表中添加一条外键约束。代码通过调用 DBadd_foreign_key 函数来实现这一功能。该函数接受三个参数：表名、外键列索引和指向 ZBX_FIELD 结构体的指针。在这里，表名为 \"sysmap_usrgrp\"，外键列索引为 1，ZBX_FIELD 结构体变量包含外键的相关信息。函数返回一个整型值，表示添加外键的操作结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050088 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_2050088(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"sysmapid", NULL, "sysmaps", "sysmapid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：要添加外键的表名，这里是 "sysmap_usrgrp"
	// 参数2：外键列的索引，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
	// 函数返回整型值，表示添加外键的操作结果
	return DBadd_foreign_key("sysmap_usrgrp", 1, &field);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是在表 \"sysmap_usrgrp\" 中添加一条外键约束。具体来说，这条外键约束是指当 \"usrgrpid\" 列的值发生变化时，对应的 \"usrgrp\" 表中的记录也会随之发生变化。如果删除 \"usrgrp\" 表中的记录，那么 \"sysmap_usrgrp\" 表中的外键约束会自动删除相应的记录。这个过程是通过调用 DBadd_foreign_key 函数来实现的。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050089 的静态函数，该函数为空函数（void 类型）
static int DBpatch_2050089(void)
{
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050090 的静态函数，该函数不接受任何参数，返回一个整型值。
   这个函数的主要目的是更新数据库中的表（profiles）中的数据。
   具体来说，是将表中的一列（web.triggers.showdisabled）的值更新为另一列（web.triggers.filter_status）的值。
   当 value_int 为 0 时，更新后的值为 0，否则为 -1。
   如果更新操作失败，函数返回 FAIL，否则返回 SUCCEED。
*/
static int	DBpatch_2050090(void)
{
	// 判断 DBexecute 函数执行的结果，该函数用于执行更新操作
	if (ZBX_DB_OK > DBexecute("update profiles"
			" set idx='web.triggers.filter_status',value_int=case when value_int=0 then 0 else -1 end"
			" where idx='web.triggers.showdisabled'"))
	{
		// 如果 DBexecute 执行失败，返回 FAIL
		return FAIL;
	}

	// 如果 DBexecute 执行成功，返回 SUCCEED
	return SUCCEED;
}

static int	DBpatch_2050090(void)
{
	if (ZBX_DB_OK > DBexecute("update profiles"
			" set idx='web.triggers.filter_status',value_int=case when value_int=0 then 0 else -1 end"
			" where idx='web.triggers.showdisabled'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：更新 profiles 表中 web.httpconf.filter_status 字段的值。当 value_int 为 0 时，将其值更新为 0，否则更新为 -1。判断更新操作是否成功，如果成功返回 SUCCEED，否则返回 FAIL。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050091 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_2050091(void)
{
	// 判断 DBexecute 执行的结果是否正确
	if (ZBX_DB_OK > DBexecute("update profiles"
			// 更新 profiles 表中的 web.httpconf.filter_status 字段，当 value_int 为 0 时，将其值更新为 0，否则更新为 -1
			" set idx='web.httpconf.filter_status',value_int=case when value_int=0 then 0 else -1 end"
			" where idx='web.httpconf.showdisabled'"))
	{
		// 如果 DBexecute 执行失败，返回 FAIL
		return FAIL;
	}

	// 如果 DBexecute 执行成功，返回 SUCCEED
	return SUCCEED;
}


static int	DBpatch_2050092(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	const char	*end, *start;
	int		len, ret = FAIL, rc;
	char		*url = NULL, *url_esc;
	size_t		i, url_alloc = 0, url_offset;
	const char	*url_map[] = {
				"dashboard.php", "dashboard.view",
				"discovery.php", "discovery.view",
				"maps.php", "map.view",
				"httpmon.php", "web.view",
				"media_types.php", "mediatype.list",
				"proxies.php", "proxy.list",
				"scripts.php", "script.list",
				"report3.php", "report.services",
				"report1.php", "report.status"
			};

	if (NULL == (result = DBselect("select userid,url from users where url<>''")))
		return FAIL;

	while (NULL != (row = (DBfetch(result))))
	{
		if (NULL == (end = strchr(row[1], '?')))
			end = row[1] + strlen(row[1]);

		for (start = end - 1; start > row[1] && '/' != start[-1]; start--)
			;

		len = end - start;

		for (i = 0; ARRSIZE(url_map) > i; i += 2)
		{
			if (0 == strncmp(start, url_map[i], len))
				break;
		}

		if (ARRSIZE(url_map) == i)
			continue;

		url_offset = 0;
		zbx_strncpy_alloc(&url, &url_alloc, &url_offset, row[1], start - row[1]);
		zbx_strcpy_alloc(&url, &url_alloc, &url_offset, "zabbix.php?action=");
		zbx_strcpy_alloc(&url, &url_alloc, &url_offset, url_map[i + 1]);

		if ('\0' != *end)
		{
			zbx_chrcpy_alloc(&url, &url_alloc, &url_offset, '&');
			zbx_strcpy_alloc(&url, &url_alloc, &url_offset, end + 1);
		}

		/* 255 - user url field size */
		if (url_offset > 255)
		{
			*url = '\0';
			zabbix_log(LOG_LEVEL_WARNING, "Cannot convert URL for user id \"%s\":"
					" value is too long. The URL field was reset.", row[0]);
		}

		url_esc = DBdyn_escape_string(url);
		rc = DBexecute("update users set url='%s' where userid=%s", url_esc, row[0]);
		zbx_free(url_esc);

		if (ZBX_DB_OK > rc)
			goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(url);
	DBfree_result(result);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050093 的静态函数，该函数用于向名为 \"screens\" 的数据库表中添加一个字段名为 \"userid\" 的整型字段。添加过程通过调用 DBadd_field 函数实现，返回值为添加是否成功。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050093 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050093(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	// 字段名称为 "userid"，不分配内存，后续参数均为 NULL
	// 字段类型为 ZBX_TYPE_ID，即整型
	// 字段其他参数分别为 0，0

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到名为 "screens" 的数据库表中
	// 函数返回值为添加是否成功，即 1（成功）或 0（失败）
	return DBadd_field("screens", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是更新 screens 表中的数据。具体操作如下：
 *
 *1. 使用 DBexecute 函数执行一条 SQL 语句，更新 screens 表中的 userid 字段。更新规则是将 userid 设置为 users 表中 type 等于 3 的用户的最小 userid。
 *2. 在执行更新操作时，判断 DBexecute 函数的返回值是否为 ZBX_DB_OK。如果不是，说明更新操作失败，返回 FAIL 表示失败。
 *3. 如果更新操作成功，返回 SUCCEED 表示成功。
 *
 *整个代码块的作用是根据 users 表中 type 等于 3 的用户的最小 userid 更新 screens 表中的 userid 字段。
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050094 的静态函数，该函数不接受任何参数，返回一个整型值 */
static int	DBpatch_2050094(void)
{
	/* 判断 DBexecute 执行的结果是否正确，如果正确，则更新 screens 表中的数据 */
	if (ZBX_DB_OK > DBexecute("update screens"
			" set userid=(select min(userid) from users where type=3)"
			" where templateid is null"))
	{
		/* 如果执行更新操作失败，返回 FAIL 表示失败 */
		return FAIL;
/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"screens\" 的数据库表中添加一个新字段。具体来说，代码首先定义了一个名为 field 的常量结构体变量，用于描述要添加的字段的信息，包括字段类型、名称、值等。然后，通过调用 DBadd_field 函数将 field 结构体变量添加到 \"screens\" 数据库表中。最后，返回 DBadd_field 函数的返回值，表示添加字段的操作是否成功。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050096 的静态函数，该函数为空返回类型为整型的函数
static int	DBpatch_2050096(void)
{
	// 定义一个名为 field 的常量结构体变量，该结构体变量包含以下成员：
	// 1. 私有变量 private
	// 2. 字符串 "1"
	// 3. 两个空指针 NULL
	// 4. 整型变量 zbx_field_type_t 类型，值为 0
	// 5. 字段类型为 ZBX_TYPE_INT
	// 6. 非空校验，值为 0
	// 7. 其它未知字段，值为 0
	const ZBX_FIELD field = {"private", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义好的 field 结构体变量添加到数据库中，并将返回值赋值给整型变量 result
	int result = DBadd_field("screens", &field);

	// 返回 result 变量，即 DBadd_field 函数的返回值
	return result;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"screens\" 表中添加一条外键约束，该约束关联的字段名为 \"userid\"，标签为 \"users\"。函数返回值为添加外键约束的结果码。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050095 的静态函数，该函数为空返回类型为整数的函数
static int	DBpatch_2050095(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下字段：
	// 字段名：userid
	// 字段类型：未知（NULL）
	// 字段标签：users
	// 字段值：userid
	// 字段长度：0
	// 字段偏移：0
	// 字段数量：0
	const ZBX_FIELD	field = {"userid", NULL, "users", "userid", 0, 0, 0, 0};

	// 调用 DBadd_foreign_key 函数，向 "screens" 表中添加一条外键约束
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 screen_usrgrp 的表，其中包含以下字段：
 *
 *- screenusrgrpid：唯一标识屏幕用户组的整数类型字段
 *- screenid：用于标识屏幕的整数类型字段
 *- usrgrpid：用于标识用户组的整数类型字段
 *- permission：表示权限级别的字符串类型字段
 *
 *函数 DBpatch_2050101 用于执行这个创建表的操作，并返回创建表的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050101 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050101(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
		// 初始化一个 ZBX_TABLE 结构体，包含以下字段：
		// 1. screen_usrgrp：字符串类型，非空，用于标识屏幕用户组
		// 2. screenusrgrpid：整数类型，非空，用于唯一标识屏幕用户组
		// 3. 两个空字段，可能是预留的扩展字段
		{"screen_usrgrp", "screenusrgrpid", 0,
			// 定义一个包含字段的数组，以下字段将添加到 ZBX_TABLE 结构体中：
			// 1. screenusrgrpid：整数类型，非空，用于唯一标识屏幕用户组
			// 2. screenid：整数类型，非空，用于标识屏幕
			// 3. usrgrpid：整数类型，非空，用于标识用户组
			// 4. permission：字符串类型，非空，用于表示权限级别
			// 5. 一个空字段，可能是预留的扩展字段
			{
				{"screenusrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"screenid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"usrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			// 结束 ZBX_TABLE 结构体的定义
			NULL
		};

	// 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
	return DBcreate_table(&table);
}

 *整个代码块的主要目的是创建一个名为 `screen_user` 的表。表的结构包括以下字段：`screenuserid`、`screenid`、`userid` 和 `permission`。其中，`permission` 字段的值为 2。通过调用 `DBcreate_table` 函数来完成表的创建。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050097 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050097(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
		// 初始化一个 ZBX_TABLE 结构体，包括以下字段：
		{"screen_user",	"screenuserid",	0,
			// 定义一个包含多个字段的数组，这里只展示了四个字段
			{
				{"screenuserid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"screenid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"userid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			// 结束包含字段的数组
			NULL
		};

	// 调用 DBcreate_table 函数，传入 table 参数，返回创建表的结果
	return DBcreate_table(&table);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"screen_user_1\" 的索引，该索引基于 \"screenid\" 和 \"userid\" 两列，并按照升序排列。函数 DBpatch_2050098 调用 DBcreate_index 函数来实现这一目的，并返回新创建的索引的索引号。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050098 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050098(void)
{
    // 调用 DBcreate_index 函数，用于创建一个名为 "screen_user_1" 的索引
    // 索引依据的列为 "screenid" 和 "userid"
    // 索引的顺序为 1，即升序排列

    // 返回 DBcreate_index 函数的执行结果，即新创建的索引的索引号
}


/******************************************************************************
 * *
 *整个代码块的主要目的是在数据库中添加一个名为 \"screen_user\" 的字段，并设置其对应的外键约束。输出结果为整型值，表示添加外键操作的结果。如果添加成功，返回值为0；如果添加失败，返回非0值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050099 的静态函数，该函数不接受任何参数，返回类型为整型（int）
static int	DBpatch_2050099(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储屏幕 ID 字段的信息
	const ZBX_FIELD	field = {"screenid", NULL, "screens", "screenid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一个外键约束
	// 参数1：要添加外键的字段名，这里是 "screen_user"
	// 参数2：主键字段名，这里是 "1"（表示单个主键）
	// 参数3：指向 ZBX_FIELD 结构体的指针，这里是 field 变量
	return DBadd_foreign_key("screen_user", 1, &field);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"screen_user\" 的表中添加一个外键约束，约束的列名为 \"userid\"，关联的表名为 \"users\"，当 \"users\" 表中的数据发生变化时，会自动删除 \"screen_user\" 表中对应的数据。函数返回值为添加外键约束的操作结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050100 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_2050100(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD	field = {"userid", NULL, "users", "userid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加名为 "screen_user" 的表的外键约束
	// 参数1：表名，"screen_user"
	// 参数2：主键列序号，这里为 2
	// 参数3：指向 ZBX_FIELD 结构体的指针，包含外键约束信息
	return DBadd_foreign_key("screen_user", 2, &field);
}


static int	DBpatch_2050101(void)
{
	const ZBX_TABLE table =
		{"screen_usrgrp", "screenusrgrpid", 0,
			{
				{"screenusrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"screenid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"usrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"screen_usrgrp\" 的索引，索引字段为 \"screenid\" 和 \"usrgrpid\"，索引名称和索引字段名的组合为 \"screen_usrgrp_1\"。需要注意的是，代码中指定的索引类型可能存在错误，通常情况下，创建索引时不需要指定索引类型。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050102 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050102(void)
{
    // 调用 DBcreate_index 函数，用于创建一个索引
    // 参数1：索引名称，此处为 "screen_usrgrp"
    // 参数2：索引字段名，此处为 "screen_usrgrp_1"
    // 参数3：索引字段列表，此处为 "screenid,usrgrpid"
    // 参数4：索引类型，此处为 1（此处可能是误写，通常不需要指定索引类型）
}

// 以下是注释好的整个代码块：
/**
 * @file             DBpatch_2050102.c
 * @brief           创建一个名为 "screen_usrgrp" 的索引
 * @author           Unknown
 * @version          1.0
 * @date            2022-01-01
 * @copyright        Copyright (c) 2022, All rights reserved.
 * @license          GPLv3
 * @attributes        static
 */

static int DBpatch_2050102(void)
{
    // 调用 DBcreate_index 函数，用于创建一个索引
    // 参数1：索引名称，此处为 "screen_usrgrp"
    // 参数2：索引字段名，此处为 "screen_usrgrp_1"
    // 参数3：索引字段列表，此处为 "screenid,usrgrpid"
    // 参数4：索引类型，此处可能是误写，通常不需要指定索引类型
    return DBcreate_index("screen_usrgrp", "screen_usrgrp_1", "screenid,usrgrpid", 1);
}


/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 *static int\tDBpatch_2050103(void)
 *{
 *\tconst ZBX_FIELD\tfield = {\"screenid\", NULL, \"screens\", \"screenid\", 0, 0, 0, ZBX_FK_CASCADE_DELETE};
 *
 *\treturn DBadd_foreign_key(\"screen_usrgrp\", 1, &field);
 *}
 *
 *// 注释：整个代码块的主要目的是向 \"screen_usrgrp\" 表中添加一个外键约束，约束的列是 screenid，当 screenid 列的值发生变化时，会自动删除相关记录
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_2050103 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050103(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储屏幕信息
	const ZBX_FIELD field = {"screenid", NULL, "screens", "screenid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一个外键约束
	// 参数1：要添加外键的表名，这里是 "screen_usrgrp"
	// 参数2：主键列索引，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，这里是我们之前定义的 field
	return DBadd_foreign_key("screen_usrgrp", 1, &field);
}

// 整个代码块的主要目的是向 "screen_usrgrp" 表中添加一个外键约束，约束的列是 screenid，当 screenid 列的值发生变化时，会自动删除相关记录


/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"screen_usrgrp\" 的表中添加一个外键约束，该约束关联到 \"usrgrp\" 表的 \"usrgrpid\" 列。如果添加成功，函数返回0，否则返回其他整数值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050104 的静态函数，该函数不接受任何参数，返回类型为整型（int）
static int	DBpatch_2050104(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050105 的静态函数，该函数用于修改名为 \"proxy_history\" 的表中的 \"meta\" 字段，将其类型从 ZBX_TYPE_INT 更改为 ZBX_TYPE_STRING。
 *
 *输出：
 *
 *```c
 *static int DBpatch_2050105(void)
 *{
 *    // 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下字段：
 *    // 字段名：flags
 *    // 字段值：0
 *    // 字段类型：ZBX_TYPE_INT
 *    // 是否非空：ZBX_NOTNULL
 *    // 其他未知字段：0
 *    const ZBX_FIELD field = {\"flags\", \"0\", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};
 *
 *    // 调用 DBrename_field 函数，传入以下参数：
 *    // 1. 表名：proxy_history
 *    // 2. 旧字段名：meta
 *    // 3. 指向 ZBX_FIELD 结构体的指针：&field
 *    // 返回 DBrename_field 函数的执行结果
 *    return DBrename_field(\"proxy_history\", \"meta\", &field);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_2050105 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050105(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下字段：
	// 字段名：flags
	// 字段值：0
	// 字段类型：ZBX_TYPE_INT
	// 是否非空：ZBX_NOTNULL
	// 其他未知字段：0
	const ZBX_FIELD	field = {"flags", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBrename_field 函数，传入以下参数：
	// 1. 表名：proxy_history
	// 2. 旧字段名：meta
	// 3. 指向 ZBX_FIELD 结构体的指针：&field
	// 返回 DBrename_field 函数的执行结果
	return DBrename_field("proxy_history", "meta", &field);
}

	// 参数3：指向 ZBX_FIELD 结构体的指针，即 field 变量
	// 返回值：添加外键约束的结果，若成功则为0，失败则为其他整数值
	return DBadd_foreign_key("screen_usrgrp", 2, &field);
}


static int	DBpatch_2050105(void)
{
	const ZBX_FIELD	field = {"flags", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBrename_field("proxy_history", "meta", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是更新 proxy_history 表中的数据，将 flags 为 1 的记录转换为 flags 为 3（即 PROXY_HISTORY_FLAG_META | PROXY_HISTORY_FLAG_NOVALUE）的记录。如果数据库操作失败，函数返回 FAIL 表示失败；如果数据库操作成功，函数返回 SUCCEED 表示成功。
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050106 的静态函数，该函数不接受任何参数，返回一个整型值 */
static int	DBpatch_2050106(void)
{
	/* 转换 meta value（1）为 PROXY_HISTORY_FLAG_META | PROXY_HISTORY_FLAG_NOVALUE（0x03）标志位 */
	if (ZBX_DB_OK > DBexecute("update proxy_history set flags=3 where flags=1"))
	{
		/* 如果数据库操作失败，返回 FAIL 表示失败 */
		return FAIL;
	}

	/* 如果数据库操作成功，返回 SUCCEED 表示成功 */
	return SUCCEED;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"slideshows\" 的数据库表中添加一个名为 \"userid\" 的整型字段。通过定义一个 ZBX_FIELD 结构体变量并调用 DBadd_field 函数来实现这一目的。如果添加成功，函数返回 1，否则返回 0。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050107 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050107(void)
{
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个名为 DBpatch_2050108 的静态函数，该函数为空返回类型为 int 的函数。
   这个函数的主要目的是更新数据库中的 slideshows 表，将 userid 设置为最小 userid 的超级管理员（类型为 3）。
   若更新操作失败，返回 FAIL；若成功，返回 SUCCEED。
*/
static int	DBpatch_2050108(void)
{
	/* 判断 DBexecute 执行结果是否为 ZBX_DB_OK，即数据库操作是否成功。
       若成功，将 slideshows 表中的 userid 设置为最小 userid 的超级管理员（类型为 3）。
       若失败，返回 FAIL。
    */
	if (ZBX_DB_OK > DBexecute("update slideshows set userid=(select min(userid) from users where type=3)"))
		return FAIL;

	/* 如果数据库更新操作成功，返回 SUCCEED。 */
	return SUCCEED;
}

	// 整个函数的主要目的是向 "slideshows" 数据库表中添加一个名为 "userid" 的整型字段
}


static int	DBpatch_2050108(void)
{
	/* type=3 -> type=USER_TYPE_SUPER_ADMIN */
	if (ZBX_DB_OK > DBexecute("update slideshows set userid=(select min(userid) from users where type=3)"))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050109 的静态函数，该函数用于将一个 ZBX_FIELD 结构体中的数据插入到名为 \"slideshows\" 的数据库表中。其中，ZBX_FIELD 结构体定义了一个用户ID（userid）字段，要求非空，类型为整型（ZBX_TYPE_ID），不允许为空（ZBX_NOTNULL）。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050109 的静态函数，该函数为空函数（void 类型）
static int	DBpatch_2050109(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，其中包含以下字段：
	// userid：用户ID，非空，类型为整型（ZBX_TYPE_ID），不允许为空（ZBX_NOTNULL）
	const ZBX_FIELD	field = {"userid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0};

	// 调用 DBset_not_null 函数，将 field 结构体中的数据插入到名为 "slideshows" 的数据库表中
	// 注意：这里的 &field 是为了获取 field 结构体的地址，以便将其作为参数传递给 DBset_not_null 函数
	return DBset_not_null("slideshows", &field);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"slideshows\" 表中添加一条外键约束，该约束关联的字段名为 \"userid\"，表名为 \"users\"。函数返回值为添加外键约束的结果码。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050110 的静态函数，该函数为空返回类型为 int
static int	DBpatch_2050110(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下字段：
	// 字段名：userid
	// 字段类型：未知（NULL）
	// 字段标签：users
	// 字段键：userid
	// 字段长度：0
	// 字段小数位：0
	// 字段索引：0
	// 字段唯一：0

	const ZBX_FIELD	field = {"userid", NULL, "users", "userid", 0, 0, 0, 0};

	// 调用 DBadd_foreign_key 函数，向 "slideshows" 表中添加一条外键约束
	// 参数1：表名："slideshows"
	// 参数2：主键列索引：3
	// 参数3：指向 ZBX_FIELD 结构体的指针，包含外键约束信息

	return DBadd_foreign_key("slideshows", 3, &field);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_2050111 的静态函数，该函数用于向名为 \"slideshows\" 的数据库表中添加一个名为 \"private\" 的字段，字段类型为整型（ZBX_TYPE_INT），非空（ZBX_NOTNULL），并设置其值为 1。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050111 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050111(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `slideshow_user` 的表格。该表格包含四个字段：`slideshowuserid`、`slideshowid`、`userid` 和 `permission`。其中，`permission` 字段的值为 2。通过调用 `DBcreate_table` 函数来创建这个表格，并返回创建结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050112 的静态函数，该函数不接受任何参数，返回类型为整型。
static int	DBpatch_2050112(void)
{
	// 定义一个常量 ZBX_TABLE 类型的变量 table，用于存储表格信息。
	const ZBX_TABLE table =
		// 初始化一个 ZBX_TABLE 结构体，包括以下字段：
		{"slideshow_user", "slideshowuserid", 0,
			// 定义表格的字段信息，包括字段名、类型、是否非空等属性。
			{
				{"slideshowuserid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"slideshowid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"userid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 *static int\tDBpatch_2050113(void)
 *{
 *    // 调用 DBcreate_index 函数，创建一个名为 \"slideshow_user_1\" 的索引
 *    // 索引基于以下列：slideshowid 和 userid
 *    // 索引的排序顺序为升序（1）
 *    return DBcreate_index(\"slideshow_user\", \"slideshow_user_1\", \"slideshowid,userid\", 1);
 *}
 *```
 *
 *这段代码的主要目的是创建一个名为 \"slideshow_user_1\" 的索引，该索引基于两列：slideshowid 和 userid，并按照升序排序。创建索引的成功与否可以通过返回值判断。如果返回值为0，表示创建成功；否则为负数，表示创建失败。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050113 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_2050113(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "slideshow_user_1" 的索引
    // 索引基于以下列：slideshowid 和 userid
    // 索引的排序顺序为升序（1）

    // 以下是对代码块的详细注释
    // 1. 定义一个名为 DBpatch_2050113 的静态函数，表示这个函数是静态的，仅在本文件中可用
    // 2. 函数不接受任何参数，表示它是一个无参数的函数
    // 3. 函数返回一个整型值，表示函数的返回值是整数类型

    // 4. 调用 DBcreate_index 函数，创建一个名为 "slideshow_user_1" 的索引
    // 5. 索引基于以下列：slideshowid 和 userid，表示索引关联的两列分别是 slideshowid 和 userid
    // 6. 索引的排序顺序为升序（1），表示在查询时，结果将按照 slideshowid 和 userid 升序排列

    // 7. 函数返回 DBcreate_index 的返回值，即创建索引的结果，如果创建成功，返回值为 0，否则为负数
}

		};

	// 调用 DBcreate_table 函数，创建名为 table 的表格，并返回创建结果。
	return DBcreate_table(&table);
}

				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_2050113(void)
{
	return DBcreate_index("slideshow_user", "slideshow_user_1", "slideshowid,userid", 1);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向 \"slideshow_user\" 表中添加一条外键约束。具体来说，代码首先定义了一个 ZBX_FIELD 结构体变量 `field`，用于存储外键信息。然后调用 DBadd_foreign_key 函数，将这条外键约束添加到数据库中。如果添加成功，函数返回0，否则返回非0值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050114 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050114(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键信息
	const ZBX_FIELD field = {"slideshowid", NULL, "slideshows", "slideshowid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：表名，这里是 "slideshow_user"
	// 参数2：主键列索引，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
	// 函数返回值：操作结果，0 表示成功，非0表示失败
	return DBadd_foreign_key("slideshow_user", 1, &field);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"slideshow_user\" 表中添加一个外键约束，约束的列名为 \"userid\"，关联的表名为 \"users\"，当 \"users\" 表中的 \"userid\" 列发生变化时，会自动删除 \"slideshow_user\" 表中的相关记录。如果添加外键约束操作成功，函数返回 0，否则返回非 0 值。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050115 的静态函数，该函数为空返回类型为整数的函数
static int DBpatch_2050115(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"slideshow_usrgrp\" 的表，其中包含四个字段：slideshowusrgrpid、slideshowid、usrgrpid 和 permission。表的创建语句是通过调用 DBcreate_table 函数来实现的。表的结构定义使用了 ZBX_TABLE 类型，其中包含了字段的名称、类型、是否非空、索引字段等信息。在这段代码中，还定义了一个默认值为 2 的 int 类型字段 permission。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050116 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_2050116(void)
{
	// 定义一个常量 ZBX_TABLE 类型的变量 table
	const ZBX_TABLE table =
		// 初始化一个 ZBX_TABLE 结构体，包含以下字段：
		{"slideshow_usrgrp", "slideshowusrgrpid", 0,
			// 字段1：slideshowusrgrpid，类型为 ZBX_TYPE_ID，非空，索引字段
			{
				{"slideshowusrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				// 字段2：slideshowid，类型为 ZBX_TYPE_ID，非空
				{"slideshowid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				// 字段3：usrgrpid，类型为 ZBX_TYPE_ID，非空
				{"usrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				// 字段4：permission，类型为 ZBX_TYPE_INT，非空，默认值为 2
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				// 字段5：无，使用空字符串结束
				{0}
			},
			// 表名为 NULL
			NULL
		};

	// 调用 DBcreate_table 函数，传入 table 参数，返回创建表的结果
	return DBcreate_table(&table);
}

		{"slideshow_usrgrp", "slideshowusrgrpid", 0,
			{
				{"slideshowusrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"slideshowid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"usrgrpid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"permission", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"slideshow_usrgrp_1\" 的索引，该索引基于表 \"slideshow_usrgrp\"，包含 \"slideshowid\" 和 \"usrgrpid\" 两列，索引顺序为 1。最后返回创建索引的整型结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050117 的静态函数，该函数不接受任何参数，返回类型为整型
/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"slideshow_usrgrp\" 表中添加一条外键约束。该函数通过调用 DBadd_foreign_key 函数来实现这一目的。在这个过程中，创建了一个 ZBX_FIELD 结构体变量来存储外键约束的相关信息，包括字段名、所属表、外键名等。然后将这个 ZBX_FIELD 结构体变量作为参数传递给 DBadd_foreign_key 函数，完成外键约束的添加。最后，返回添加外键约束的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050118 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_2050118(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下内容：
	// 字段名：slideshowid
	// 字段类型：NULL（未知类型）
	// 字段所属表：slideshows
	// 外键名：slideshowid
	// 索引：0（未使用）
	// 浮点数：0（未使用）
	// 级联删除：ZBX_FK_CASCADE_DELETE

	// 创建一个名为 field 的 ZBX_FIELD 结构体变量
	ZBX_FIELD field = {"slideshowid", NULL, "slideshows", "slideshowid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向 "slideshow_usrgrp" 表中添加一条外键约束
	// 参数1：表名（slideshow_usrgrp）
	// 参数2：主键序号（1，表示添加第一条外键约束）
	// 参数3：指向 ZBX_FIELD 结构体的指针（&field，即 field 变量本身）
	// 返回值：添加外键约束的结果（0表示成功，非0表示失败）
	return DBadd_foreign_key("slideshow_usrgrp", 1, &field);
}

    
/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"slideshow_usrgrp\" 表中添加一条外键约束，约束的列名为 \"usrgrpid\"，约束类型为 ZBX_FK_CASCADE_DELETE，即级联删除。函数返回值为添加外键约束的结果码。
 ******************************************************************************/
// 定义一个名为 DBpatch_2050119 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_2050119(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD field = {"usrgrpid", NULL, "usrgrp", "usrgrpid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向 "slideshow_usrgrp" 表中添加外键约束
	// 参数1：表名
	// 参数2：主键列索引，这里是 2
	// 参数3：指向 ZBX_FIELD 结构体的指针，包含外键约束信息
	return DBadd_foreign_key("slideshow_usrgrp", 2, &field);
}

{
	const ZBX_FIELD	field = {"slideshowid", NULL, "slideshows", "slideshowid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("slideshow_usrgrp", 1, &field);
}

static int	DBpatch_2050119(void)
{
	const ZBX_FIELD	field = {"usrgrpid", NULL, "usrgrp", "usrgrpid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("slideshow_usrgrp", 2, &field);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是更新系统映射表中的private字段值为0，将私有数据变为公有共享。判断更新操作是否成功，如果成功则返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
/*
 * 这段C语言代码定义了一个名为DBpatch_2050120的静态函数。
 * 函数的作用是更新系统映射表（sysmaps）中的private字段值为0，
 * 将原本私有的数据变为公有共享（PUBLIC_SHARING）。
 * 函数返回两个可能的结果：SUCCEED（成功）和FAIL（失败）。
 */
static int	DBpatch_2050120(void)
{
	/* 判断DBexecute("update sysmaps set private=0")的结果，如果大于等于ZBX_DB_OK，表示执行成功 */
	if (ZBX_DB_OK <= DBexecute("update sysmaps set private=0"))
		/* 如果执行成功，返回SUCCEED（成功） */
		return SUCCEED;
	/* 如果执行失败，返回FAIL（失败） */
	else
		return FAIL;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据库中的 screens 表，将 private 字段的值从 1 修改为 0，表示从私有状态改为公开共享状态。如果更新成功，返回 SUCCEED；如果更新失败，返回 FAIL。
 ******************************************************************************/
/*
 * 这段C语言代码定义了一个名为 DBpatch_2050121 的静态函数。
 * 函数的作用是更新数据库中的 screens 表，将 private 字段的值从 1 修改为 0，
 * 表示从私有状态改为公开共享状态。
 * 函数返回两个可能的结果：SUCCEED 表示更新成功，FAIL 表示更新失败。
 */
static int	DBpatch_2050121(void)
{
	/* 定义一个条件，判断 DBexecute 执行的结果是否为 ZBX_DB_OK 或更高版本 */
	if (ZBX_DB_OK <= DBexecute("update screens set private=0"))
		/* 如果条件成立，表示更新成功，返回 SUCCEED */
		return SUCCEED;
	/* 如果条件不成立，表示更新失败，返回 FAIL */
	else
		return FAIL;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据库中的 slideshows 表，将 private 字段的值从1改为0，表示从私有变为公开共享。通过判断 DBexecute 执行的结果是否为 ZBX_DB_OK 或其以下，来判断更新操作是否成功。如果成功，则返回 SUCCEED，表示操作成功；否则返回 FAIL，表示操作失败。
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为DBpatch_2050122。函数声明为静态 int 类型，意味着它是静态函数，
 * 只能被外部函数调用，而且不需要使用 static 关键字修饰。这个函数没有参数，也没有返回值。
 * 
 * 函数主要目的是更新数据库中的 slideshows 表，将 private 字段的值从1改为0，表示从私有变为公开共享。
 * 判断更新操作是否成功，如果成功则返回SUCCEED，否则返回FAIL。
 */
static int	DBpatch_2050122(void)
{
	/* private=0 -> PUBLIC_SHARING */
	// 判断 DBexecute 执行的结果是否为 ZBX_DB_OK 或其以下，表示更新操作成功
	if (ZBX_DB_OK <= DBexecute("update slideshows set private=0"))
		// 如果更新成功，返回 SUCCEED，表示操作成功
		return SUCCEED;

	// 更新失败，返回 FAIL，表示操作失败
	return FAIL;
}


#endif

DBPATCH_START(2050)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(2050000, 0, 1)
DBPATCH_ADD(2050001, 0, 1)
DBPATCH_ADD(2050002, 0, 1)
DBPATCH_ADD(2050003, 0, 1)
DBPATCH_ADD(2050004, 0, 1)
DBPATCH_ADD(2050005, 0, 0)
DBPATCH_ADD(2050006, 0, 0)
DBPATCH_ADD(2050007, 0, 1)
DBPATCH_ADD(2050008, 0, 1)
DBPATCH_ADD(2050009, 0, 1)
DBPATCH_ADD(2050010, 0, 1)
DBPATCH_ADD(2050011, 0, 1)
DBPATCH_ADD(2050012, 0, 1)
DBPATCH_ADD(2050013, 0, 0)
DBPATCH_ADD(2050014, 0, 1)
DBPATCH_ADD(2050015, 0, 1)
DBPATCH_ADD(2050019, 0, 1)
DBPATCH_ADD(2050020, 0, 1)
DBPATCH_ADD(2050021, 0, 1)
DBPATCH_ADD(2050022, 0, 1)
DBPATCH_ADD(2050023, 0, 1)
DBPATCH_ADD(2050029, 0, 1)
DBPATCH_ADD(2050030, 0, 1)
DBPATCH_ADD(2050031, 0, 1)
DBPATCH_ADD(2050032, 0, 1)
DBPATCH_ADD(2050033, 0, 1)
DBPATCH_ADD(2050034, 0, 1)
DBPATCH_ADD(2050035, 0, 1)
DBPATCH_ADD(2050036, 0, 1)
DBPATCH_ADD(2050037, 0, 1)
DBPATCH_ADD(2050038, 0, 1)
DBPATCH_ADD(2050039, 0, 1)
DBPATCH_ADD(2050040, 0, 1)
DBPATCH_ADD(2050041, 0, 1)
DBPATCH_ADD(2050042, 0, 1)
DBPATCH_ADD(2050043, 0, 1)
DBPATCH_ADD(2050044, 0, 1)
DBPATCH_ADD(2050045, 0, 1)
DBPATCH_ADD(2050051, 0, 1)
DBPATCH_ADD(2050052, 0, 1)
DBPATCH_ADD(2050053, 0, 1)
DBPATCH_ADD(2050054, 0, 1)
DBPATCH_ADD(2050055, 0, 1)
DBPATCH_ADD(2050056, 0, 1)
DBPATCH_ADD(2050057, 0, 1)
DBPATCH_ADD(2050058, 0, 1)
DBPATCH_ADD(2050059, 0, 1)
DBPATCH_ADD(2050060, 0, 1)
DBPATCH_ADD(2050061, 0, 1)
DBPATCH_ADD(2050062, 0, 1)
DBPATCH_ADD(2050063, 0, 1)
DBPATCH_ADD(2050064, 0, 1)
DBPATCH_ADD(2050065, 0, 1)
DBPATCH_ADD(2050066, 0, 1)
DBPATCH_ADD(2050067, 0, 1)
DBPATCH_ADD(2050068, 0, 1)
DBPATCH_ADD(2050069, 0, 1)
DBPATCH_ADD(2050070, 0, 1)
DBPATCH_ADD(2050071, 0, 1)
DBPATCH_ADD(2050077, 0, 1)
DBPATCH_ADD(2050078, 0, 1)
DBPATCH_ADD(2050079, 0, 1)
DBPATCH_ADD(2050080, 0, 1)
DBPATCH_ADD(2050081, 0, 1)
DBPATCH_ADD(2050082, 0, 1)
DBPATCH_ADD(2050083, 0, 1)
DBPATCH_ADD(2050084, 0, 1)
DBPATCH_ADD(2050085, 0, 1)
DBPATCH_ADD(2050086, 0, 1)
DBPATCH_ADD(2050087, 0, 1)
DBPATCH_ADD(2050088, 0, 1)
DBPATCH_ADD(2050089, 0, 1)
DBPATCH_ADD(2050090, 0, 1)
DBPATCH_ADD(2050091, 0, 1)
DBPATCH_ADD(2050092, 0, 1)
DBPATCH_ADD(2050093, 0, 1)
DBPATCH_ADD(2050094, 0, 1)
DBPATCH_ADD(2050095, 0, 1)
DBPATCH_ADD(2050096, 0, 1)
DBPATCH_ADD(2050097, 0, 1)
DBPATCH_ADD(2050098, 0, 1)
DBPATCH_ADD(2050099, 0, 1)
DBPATCH_ADD(2050100, 0, 1)
DBPATCH_ADD(2050101, 0, 1)
DBPATCH_ADD(2050102, 0, 1)
DBPATCH_ADD(2050103, 0, 1)
DBPATCH_ADD(2050104, 0, 1)
DBPATCH_ADD(2050105, 0, 1)
DBPATCH_ADD(2050106, 0, 1)
DBPATCH_ADD(2050107, 0, 1)
DBPATCH_ADD(2050108, 0, 1)
DBPATCH_ADD(2050109, 0, 1)
DBPATCH_ADD(2050110, 0, 1)
DBPATCH_ADD(2050111, 0, 1)
DBPATCH_ADD(2050112, 0, 1)
DBPATCH_ADD(2050113, 0, 1)
DBPATCH_ADD(2050114, 0, 1)
DBPATCH_ADD(2050115, 0, 1)
DBPATCH_ADD(2050116, 0, 1)
DBPATCH_ADD(2050117, 0, 1)
DBPATCH_ADD(2050118, 0, 1)
DBPATCH_ADD(2050119, 0, 1)
DBPATCH_ADD(2050120, 0, 1)
DBPATCH_ADD(2050121, 0, 1)
DBPATCH_ADD(2050122, 0, 1)

DBPATCH_END()
