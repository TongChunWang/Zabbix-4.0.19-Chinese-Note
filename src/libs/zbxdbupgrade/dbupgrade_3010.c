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
#include "log.h"

/*
 * 3.2 development database patches
 */

#ifndef HAVE_SQLITE3

/******************************************************************************
 * *
 *整个代码块的主要目的是删除名为 \"history_log\" 表中的名为 \"history_log_2\" 的索引。通过调用 DBdrop_index 函数来实现这个功能，并返回删除操作的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010000 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_3010000(void)
{
    // 调用 DBdrop_index 函数，传入两个参数：表名（"history_log"）和索引名（"history_log_2"）
    // 该函数的主要目的是删除名为 "history_log" 表中的名为 "history_log_2" 的索引
    return DBdrop_index("history_log", "history_log_2");
}


/******************************************************************************
 * *
 *这段代码的主要目的是删除名为 \"history_log\" 表中的 \"id\" 字段。通过调用 DBdrop_field 函数来实现这个功能。需要注意的是，这里使用的是静态函数，因此在其他地方可以直接调用这个函数，而不需要进行实例化。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010001 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_3010001(void)
{
    // 调用 DBdrop_field 函数，传入两个参数：表名（"history_log"）和要删除的字段名（"id"）
    // DBdrop_field 函数的作用是删除指定表中的指定字段
    return DBdrop_field("history_log", "id");
}


/******************************************************************************
 * *
 *整个代码块的主要目的是删除名为 \"history_text\" 表中的名为 \"history_text_2\" 的索引。代码通过调用 DBdrop_index 函数来实现这一功能，并返回删除操作的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010002 的静态函数，该函数不接受任何参数，返回类型为整型（int）
/******************************************************************************
 * *
 *整个代码块的主要目的是删除名为 \"history_text\" 表中的 \"id\" 字段。通过调用 DBdrop_field 函数来实现这个功能，并返回一个整型数据作为结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010003 的静态函数，该函数不接受任何参数，返回一个整型数据
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_3010004 的静态函数，该函数用于向名为 \"triggers\" 的触发器中添加一个整型字段（recovery_mode），添加成功后返回 0。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010004 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010004(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下字段：
	// 字段名：recovery_mode
	// 字段值：0
	// 字段类型：ZBX_TYPE_INT（整型）
	// 是否非空：ZBX_NOTNULL（非空）
	// 其他属性：0
	const ZBX_FIELD	field = {"recovery_mode", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义好的 field 结构体添加到名为 "triggers" 的触发器中
	// 函数返回值即为添加操作的结果，这里假设添加成功，返回值为 0
	return DBadd_field("triggers", &field);
}


    return DBdrop_index("history_text", "history_text_2");
}


static int	DBpatch_3010003(void)
{
	return DBdrop_field("history_text", "id");
}

static int	DBpatch_3010004(void)
{
	const ZBX_FIELD	field = {"recovery_mode", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("triggers", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向 \"triggers\" 表中添加一个新的字段，该字段的名称是 \"recovery_expression\"，类型为 ZBX_TYPE_CHAR，长度为 2048，非空，其他参数为默认值。最后调用 DBadd_field 函数将这个字段添加到 \"triggers\" 表中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010005 的静态函数，该函数为空返回类型为整型的函数
static int	DBpatch_3010005(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `trigger_tag` 的数据库表。表结构包括以下字段：`triggertagid`（主键，类型为整型，非空），`triggerid`（外键，类型为整型，非空），`tag`（字符串，最大长度为255，非空），`value`（字符串，最大长度为255，非空）。创建表的成功与否取决于 DBcreate_table 函数的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010006 的静态函数，该函数不接受任何参数，返回类型为整型。
static int	DBpatch_3010006(void)
{
	// 定义一个常量结构体 ZBX_TABLE，用于表示数据库表的结构。
	const ZBX_TABLE table =
			{"trigger_tag", "triggertagid", 0,
				// 定义表的字段及其类型和约束
				{
					{"triggertagid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"triggerid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				// 表的其他设置，如主键、外键等
				NULL
			};

	// 调用 DBcreate_table 函数，根据 table 结构创建一个数据库表，并返回创建结果。
	return DBcreate_table(&table);
}

					{"triggerid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `event_tag` 的表。表的结构包括 `eventtagid`、`eventid`、`tag` 和 `value` 四个字段。其中，`eventtagid` 和 `eventid` 字段类型为 ZBX_TYPE_ID，`tag` 和 `value` 字段类型为 ZBX_TYPE_CHAR，字段长度均为 255。最后，调用 `DBcreate_table` 函数创建该表。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010009 的静态函数，该函数不接受任何参数，返回类型为整型。
static int DBpatch_3010009(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量。
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包含以下字段：
			{"event_tag", "eventtagid", 0,
					// 字段定义，包括字段名、类型、长度等属性。
					{
						{"eventtagid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
						{"eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
						{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
						{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
						{0}
					},
					NULL
				};

	// 调用 DBcreate_table 函数，传入 table 参数，返回创建表的结果。
	return DBcreate_table(&table);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"trigger_tag\" 的表中添加一条外键约束。在这个过程中，首先定义了一个 ZBX_FIELD 结构体变量 field，用于存储外键约束的相关信息。然后调用 DBadd_foreign_key 函数，将这条外键约束添加到数据库中。最后返回添加外键约束的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010008 的静态函数，该函数不接受任何参数，返回类型为整型（int）
static int	DBpatch_3010008(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"triggerid", NULL, "triggers", "triggerid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：要添加外键的表名，这里是 "trigger_tag"
	// 参数2：主键列索引，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
	return DBadd_foreign_key("trigger_tag", 1, &field);
}


static int	DBpatch_3010009(void)
{
	const ZBX_TABLE table =
			{"event_tag", "eventtagid", 0,
				{
					{"eventtagid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *这块代码的主要目的是创建一个名为 \"event_tag_1\" 的索引，基于字段 \"eventid\"。如果创建成功，函数返回 0，否则返回非 0 值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010010 的静态函数，该函数不接受任何参数，返回类型为整型
/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"event_tag\" 的表中添加一条外键约束。函数 DBpatch_3010011 是一个静态函数，不需要接受任何参数。在函数内部，首先定义了一个 ZBX_FIELD 结构体变量 field，用于存储外键约束信息。然后调用 DBadd_foreign_key 函数，将定义好的外键约束添加到 \"event_tag\" 表中。最终返回添加外键约束的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010011 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_3010011(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD field = {"eventid", NULL, "events", "eventid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：要添加外键约束的表名，这里是 "event_tag"
	// 参数2：主键列索引，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，用于存储外键约束信息
	return DBadd_foreign_key("event_tag", 1, &field);
}

}
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_3010012 的静态函数，该函数用于向 \"conditions\" 数据库表中添加一个 ZBX_FIELD 结构体变量（field）。
 *
 *注释详细说明：
 *1. 定义一个名为 field 的 ZBX_FIELD 结构体变量，设置了字段名（\"value2\"）、字段类型（ZBX_TYPE_CHAR）、是否非空（ZBX_NOTNULL）、最大长度（255）等属性。
 *2. 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 \"conditions\" 数据库表中。
 *3. 函数返回 DBadd_field 的执行结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010012 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010012(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"value2", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 "conditions" 数据库表中
	return DBadd_field("conditions", &field);
}

	return DBadd_foreign_key("event_tag", 1, &field);
}

static int	DBpatch_3010012(void)
{
	const ZBX_FIELD	field = {"value2", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("conditions", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"actions\" 的数据库表中添加一个名为 \"maintenance_mode\" 的字段，字段类型为整型，非空。添加操作的返回值存储在整型变量 `DBpatch_3010013` 中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010013 的静态函数，该函数为空返回类型为整型的函数
static int	DBpatch_3010013(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"problem\" 的表，包含 \"eventid\"、\"source\"、\"object\"、\"objectid\" 四个字段，并设置相应的类型、名称和必填属性。创建表的操作通过调用 DBcreate_table 函数完成。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010014 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010014(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包括以下字段：
			{"problem", "eventid", 0,
				// 字段定义，包括类型、名称、是否必填等属性
				{
					{"eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"source", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"object", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"objectid", "0", NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{0}
				},
				// 表结构定义结束
				NULL
			};

	// 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
	return DBcreate_table(&table);
}

					{"objectid", "0", NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *注意：由于原始代码中没有提供 DBcreate_index 函数的定义，无法为您提供关于该函数的具体参数和返回值的详细注释。请确保在理解此代码时，了解 DBcreate_index 函数的相关知识。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010015 的函数，该函数为空指针类型（void）
/******************************************************************************
 * *
 *整个代码块的主要目的是：向 \"problem\" 表中添加一个外键约束，该约束关联到 \"events\" 表的 \"eventid\" 字段，当 \"events\" 表中的 \"eventid\" 字段发生变化时，会自动更新 \"problem\" 表中的相关数据。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010016 的静态函数，该函数不接受任何参数，返回一个整型值
static int DBpatch_3010016(void)
{
    // 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
    const ZBX_FIELD field = {
        // 设置字段名称为 "eventid"
        "eventid",
        // 设置字段值的指针为 NULL
        NULL,
        // 设置字段所属的表名为 "events"
        "events",
        // 设置字段名称为 "eventid"
        "eventid",
        // 设置字段类型为整型（ZBX_TYPE_ID）
        0,
        // 设置字段不允许为空（ZBX_NOTNULL）
        ZBX_NOTNULL,
        // 设置外键约束（ZBX_FK_CASCADE_DELETE）
        ZBX_FK_CASCADE_DELETE
    };

    // 调用 DBadd_foreign_key 函数，向 "problem" 表中添加外键约束
    // 参数1：要添加外键的表名（"problem"）
    // 参数2：主键值（1）
    // 参数3：指向 ZBX_FIELD 结构体的指针（&field）
    return DBadd_foreign_key("problem", 1, &field);
}

}
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `event_recovery` 的表，其中包含两个字段：`eventid` 和 `r_eventid`。代码通过定义一个 ZBX_TABLE 结构体变量 `table` 来实现这一目的。最后调用 `DBcreate_table` 函数来创建表。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010017 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010017(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体变量，包含以下字段：
			{"event_recovery", "eventid", 0,
				// 字段定义，包括事件ID（eventid）
				{
					{"eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 字段定义，包括回收事件ID（r_eventid）
					{"r_eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{0}
				},
				// 表结构定义结束
				NULL
			};

	// 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
	return DBcreate_table(&table);
}

	const ZBX_TABLE table =
			{"event_recovery", "eventid", 0,
				{
					{"eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"r_eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"event_recovery_1\" 的索引，该索引基于表 \"event_recovery\"，包含 \"r_eventid\" 字段，顺序为主键。如果创建索引成功，函数返回 0，否则返回非 0 的错误码。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010018 的静态函数，该函数不接受任何参数，返回类型为 int
/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"event_recovery\" 表中添加一条外键约束，约束的字段名为 \"eventid\"，类型为整型，不允许为空，且为一对多外键。函数 DBpatch_3010019 负责实现这个功能。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010019 的静态函数，该函数不接受任何参数，返回一个整型值
static int DBpatch_3010019(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {
		// 设置字段名称为 "eventid"
		"eventid",
		// 设置字段值的指针为 NULL
		NULL,
		// 设置字段所属的表名为 "events"
		"events",
		// 设置字段名为 "eventid"
		"eventid",
		// 设置字段类型为整型（ZBX_TYPE_ID）
		0,
		// 设置字段不允许为空
		ZBX_NOTNULL,
		// 设置字段为一对多外键（ZBX_FK_CASCADE_DELETE）
		ZBX_FK_CASCADE_DELETE
	};

	// 调用 DBadd_foreign_key 函数，向 "event_recovery" 表中添加一条外键约束
	// 参数1：表名（"event_recovery"）
	// 参数2：主键列索引（1）
	// 参数3：指向 ZBX_FIELD 结构体的指针（&field）
	return DBadd_foreign_key("event_recovery", 1, &field);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 DBpatch_3010021_trigger_events_hash_func 的函数，该函数接收一个 void * 类型的数据作为输入，计算 zbx_object_events_t 结构体中各个字段的哈希值，并将结果返回。在这个过程中，首先将输入数据转换为 zbx_object_events_t 类型的指针，然后分别计算 objectid、source 和 object 字段的哈希值，最后将三个哈希值异或操作得到最终的哈希值并返回。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010021_trigger_events_hash_func 的函数，该函数接收一个 void * 类型的参数 data
static zbx_hash_t	DBpatch_3010021_trigger_events_hash_func(const void *data)
{
	// 类型转换，将 data 指针转换为 const zbx_object_events_t 类型的指针，并将其赋值给 oe
	const zbx_object_events_t	*oe = (const zbx_object_events_t *)data;

	// 声明一个 zbx_hash_t 类型的变量 hash，用于存储计算得到的哈希值
	zbx_hash_t		hash;

	// 使用 ZBX_DEFAULT_UINT64_HASH_FUNC 计算 oe->objectid 的哈希值，并将结果存储在 hash 中
	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&oe->objectid);

	// 使用 ZBX_DEFAULT_UINT64_HASH_ALGO 计算 oe->source 的哈希值，并将结果与 hash 进行异或操作，然后将结果存储在 hash 中
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&oe->source, sizeof(oe->source), hash);

	// 使用 ZBX_DEFAULT_UINT64_HASH_ALGO 计算 oe->object 的哈希值，并将结果与 hash 进行异或操作，然后将结果存储在 hash 中
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&oe->object, sizeof(oe->object), hash);

	// 返回计算得到的哈希值
	return hash;
}

 *整个代码块的主要目的是在 \"event_recovery\" 表中添加一个外键约束，约束的字段名称为 \"r_eventid\"，字段类型为 ZBX_TYPE_ID，不允许为空，且设置级联删除。最后调用 DBadd_foreign_key 函数向表中添加该外键约束。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010020 的静态函数，该函数不接受任何参数，返回类型为 int
static int DBpatch_3010020(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD field = {
		// 设置字段名称为 "r_eventid"
		"r_eventid",
		// 设置字段值为 NULL
		NULL,
		// 设置字段所属的表名为 "events"
		"events",
		// 设置字段别名为 "eventid"
		"eventid",
		// 设置字段类型为 ZBX_TYPE_ID
		0,
		// 设置字段不允许为空
		ZBX_NOTNULL,
		// 设置外键约束为级联删除
		ZBX_FK_CASCADE_DELETE
	};

	// 调用 DBadd_foreign_key 函数，向 "event_recovery" 表中添加外键约束
	// 参数1：表名
	// 参数2：主键列索引（这里是 2，因为 r_eventid 是最左边的列）
	// 参数3：指向 ZBX_FIELD 结构体的指针，即 field 变量
	return DBadd_foreign_key("event_recovery", 2, &field);
}


/* DBpatch_3010021 () */

#define ZBX_OPEN_EVENT_WARNING_NUM	10000000

/* problem eventids by triggerid */
typedef struct
{
	int			source;
	int			object;
	zbx_uint64_t		objectid;
	zbx_vector_uint64_t	eventids;
}
zbx_object_events_t;


/* source events hashset support */
static zbx_hash_t	DBpatch_3010021_trigger_events_hash_func(const void *data)
{
	const zbx_object_events_t	*oe = (const zbx_object_events_t *)data;

	zbx_hash_t		hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&oe->objectid);
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&oe->source, sizeof(oe->source), hash);
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&oe->object, sizeof(oe->object), hash);

	return hash;
}

/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_object_events_t类型的结构体对象是否相同。函数接受两个参数，分别是两个待比较的对象指针d1和d2。通过逐个比较对象的source（来源）、object（对象）和objectid（对象ID）三个字段，如果三者均相同，则认为两个对象相同，返回0；否则，返回错误。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_object_events_t结构体对象
static int	DBpatch_3010021_trigger_events_compare_func(const void *d1, const void *d2)
{
	// 类型转换，将传入的指针d1和d2分别转换为zbx_object_events_t类型的指针oe1和oe2
	const zbx_object_events_t	*oe1 = (const zbx_object_events_t *)d1;
	const zbx_object_events_t	*oe2 = (const zbx_object_events_t *)d2;

	// 判断oe1和oe2的source（来源）是否相同，如果不同，则返回错误
	ZBX_RETURN_IF_NOT_EQUAL(oe1->source, oe2->source);

	// 判断oe1和oe2的object（对象）是否相同，如果不同，则返回错误
	ZBX_RETURN_IF_NOT_EQUAL(oe1->object, oe2->object);

	// 判断oe1和oe2的objectid（对象ID）是否相同，如果不同，则返回错误
	ZBX_RETURN_IF_NOT_EQUAL(oe1->objectid, oe2->objectid);

	// 如果以上三个条件都满足，则返回0，表示两个对象相同
	return 0;
}



/******************************************************************************
 * *
 *整个代码块的主要目的是：从数据库中查询符合条件的旧事件，根据事件的value值更新事件状态，并将更新后的事件插入到新的事件列表中。其中，符合条件的旧事件是指事件id大于给定值的事件，事件来源为TRIGGER_SOURCE_TRIGGERS或TRIGGER_SOURCE_INTERNAL。在整个过程中，会记录开放问题事件的数量，当数量过多时，会输出警告日志。
 ******************************************************************************/
static int	DBpatch_3010021_update_event_recovery(zbx_hashset_t *events, zbx_uint64_t *eventid)
{
	// 定义变量
	DB_ROW			row;
	DB_RESULT		result;
	char			*sql = NULL;
	size_t			sql_alloc = 4096, sql_offset = 0;
	int			i, value, ret = FAIL;
	zbx_object_events_t	*object_events, object_events_local;
	zbx_db_insert_t		db_insert;

	// 分配sql内存
	sql = (char *)zbx_malloc(NULL, sql_alloc);

	/* 查询事件表，筛选出需要恢复的事件 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select source,object,objectid,eventid,value"
			" from events"
			" where eventid>" ZBX_FS_UI64
				" and source in (0,3)"
			" order by eventid",
			*eventid);

	/* 查询事件，每次处理10000条 */
	if (NULL == (result = DBselectN(sql, 10000)))
		goto out;

	// 预处理插入操作
	zbx_db_insert_prepare(&db_insert, "event_recovery", "eventid", "r_eventid", NULL);

	while (NULL != (row = DBfetch(result)))
	{
		// 解析事件信息
		object_events_local.source = atoi(row[0]);
		object_events_local.object = atoi(row[1]);

		ZBX_STR2UINT64(object_events_local.objectid, row[2]);
		ZBX_STR2UINT64(*eventid, row[3]);
		value = atoi(row[4]);

		/* 查询事件是否已存在 */
		if (NULL == (object_events = (zbx_object_events_t *)zbx_hashset_search(events, &object_events_local)))
		{
			// 如果不存在，则插入新的事件
			object_events = (zbx_object_events_t *)zbx_hashset_insert(events, &object_events_local,
					sizeof(object_events_local));

			zbx_vector_uint64_create(&object_events->eventids);
		}

		/* 根据value值更新事件状态 */
		if (1 == value)
		{
			/* 1 - TRIGGER_VALUE_TRUE (PROBLEM state) */

			// 添加事件id到事件列表
			zbx_vector_uint64_append(&object_events->eventids, *eventid);

			/* 警告：开放问题事件数量过多 */
			if (ZBX_OPEN_EVENT_WARNING_NUM == object_events->eventids.values_num)
			{
				zabbix_log(LOG_LEVEL_WARNING, "too many open problem events by event source:%d,"
						" object:%d and objectid:" ZBX_FS_UI64, object_events->source,
						object_events->object, object_events->objectid);
			}
		}
		else
		{
			/* 0 - TRIGGER_VALUE_FALSE (OK state) */

			// 清除原有事件，并将新事件id添加到事件列表
			for (i = 0; i < object_events->eventids.values_num; i++)
				zbx_db_insert_add_values(&db_insert, object_events->eventids.values[i], *eventid);

			zbx_vector_uint64_clear(&object_events->eventids);
		}
	}
	DBfree_result(result);

	/* 执行插入操作 */
	ret = zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
out:
	// 释放内存
	zbx_free(sql);

	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是从一个名为 `events` 的 hashset 中处理未恢复的事件，并将这些事件生成问题插入到数据库中。以下是代码的详细注释：
 *
 *1. 定义变量 `i`、`ret`、`eventid`、`db_insert`、`events`、`iter` 和 `object_events`。
 *2. 创建一个名为 `events` 的 hashset，并设置哈希函数和比较函数。
 *3. 准备数据库插入操作。
 *4. 使用 `do-while` 循环，直到事件ID发生变化。
 *5. 在循环中，更新事件恢复，并将事件ID赋值给 `eventid`。
 *6. 从未恢复的事件中生成问题，并将问题插入到数据库中。
 *7. 如果数据库插入的数据行数超过 1000，则执行以下操作：
 *   a. 执行数据库插入操作。
 *   b. 清理数据库插入数据。
 *   c. 重新准备数据库插入操作。
 *8. 执行数据库插入操作。
 *9. 清理数据库插入数据。
 *10. 销毁 `events` hashset。
 *11. 返回 `ret`。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010021 的函数，该函数为静态函数，参数为一个空指针，返回值为一个整型。
static int DBpatch_3010021(void)
{
	// 定义一个整型变量 i，用于循环计数。
	int i;

	// 定义一个整型变量 ret，初始值为失败码。
	int ret = FAIL;

	// 定义一个 zbx_uint64_t 类型的变量 eventid，初始值为0。
	zbx_uint64_t eventid = 0;

	// 定义一个 zbx_db_insert_t 类型的变量 db_insert，用于数据库插入操作。
	zbx_db_insert_t db_insert;

	// 定义一个 zbx_hashset_t 类型的变量 events，用于存储事件。
	zbx_hashset_t events;

	// 定义一个 zbx_hashset_iter_t 类型的变量 iter，用于遍历 hashset。
	zbx_hashset_iter_t iter;

	// 定义一个 zbx_object_events_t 类型的指针变量 object_events，用于存储对象事件。
	zbx_object_events_t *object_events;

	// 创建一个名为 events 的 hashset，初始容量为 1024，并设置哈希函数和比较函数。
	zbx_hashset_create(&events, 1024, DBpatch_3010021_trigger_events_hash_func,
			DBpatch_3010021_trigger_events_compare_func);

	// 准备数据库插入操作，关联字段为 problem、eventid、source、object 和 objectid。
	zbx_db_insert_prepare(&db_insert, "problem", "eventid", "source", "object", "objectid", NULL);

	// 进行循环，直到事件ID发生变化。
	do
	{
		// 保存旧的事件ID。
		old_eventid = eventid;

		// 如果更新事件恢复失败，跳转到 out 标签处。
		if (SUCCEED != DBpatch_3010021_update_event_recovery(&events, &eventid))
			goto out;
	}
	while (eventid != old_eventid);

	// 从未恢复的事件中生成问题。
	zbx_hashset_iter_reset(&events, &iter);

	// 遍历 events 中的对象事件。
	while (NULL != (object_events = (zbx_object_events_t *)zbx_hashset_iter_next(&iter)))
	{
		// 遍历对象事件的 eventids 数组。
		for (i = 0; i < object_events->eventids.values_num; i++)
		{
			// 将对象事件的数据插入到数据库中。
			zbx_db_insert_add_values(&db_insert, object_events->eventids.values[i], object_events->source,
					object_events->object, object_events->objectid);
		}

		// 如果数据库插入的数据行数超过 1000，则执行以下操作：
		if (1000 < db_insert.rows.values_num)
		{
			// 执行数据库插入操作。
			if (SUCCEED != zbx_db_insert_execute(&db_insert))
				goto out;

			// 清理数据库插入数据。
			zbx_db_insert_clean(&db_insert);

			// 重新准备数据库插入操作。
			zbx_db_insert_prepare(&db_insert, "problem", "eventid", "source", "object", "objectid", NULL);
		}

		// 销毁对象事件的 eventids 数组。
		zbx_vector_uint64_destroy(&object_events->eventids);
	}

	// 执行数据库插入操作。
	if (SUCCEED != zbx_db_insert_execute(&db_insert))
		goto out;

	// 设置返回值。
	ret = SUCCEED;

out:
	// 清理数据库插入数据。
	zbx_db_insert_clean(&db_insert);

	// 销毁 events hashset。
	zbx_hashset_destroy(&events);

	// 返回 ret。
	return ret;
}

		zbx_vector_uint64_destroy(&object_events->eventids);
	}

	if (SUCCEED != zbx_db_insert_execute(&db_insert))
		goto out;

	ret = SUCCEED;
out:
	zbx_db_insert_clean(&db_insert);
	zbx_hashset_destroy(&events);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_3010022 的静态函数，该函数用于将一个 ZBX_FIELD 结构体变量（包含字段名、字段类型、是否非空等信息）添加到名为 \"operations\" 的数据库表中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010022 的静态函数，该函数为空返回类型为整型的函数
static int	DBpatch_3010022(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是：统计 actions 表中 recovery_msg 为 1 的记录数量，并为每条记录生成一个新的操作 ID，然后将这些操作及其相关信息插入到 oper
 ******************************************************************************/
// 定义一个名为 DBpatch_3010023 的静态函数，该函数不接受任何参数，返回值为 int 类型。
static int	DBpatch_3010023(void)
{
	// 定义一个 zbx_db_insert_t 类型的结构体变量 db_insert，用于存储插入操作的相关信息。
	// 定义一个 zbx_db_insert_t 类型的结构体变量 db_insert_msg，用于存储插入操作的相关信息。
	// 定义一个 DB_ROW 类型的结构体变量 row，用于存储查询结果的数据。
	// 定义一个 DB_RESULT 类型的变量 result，用于存储查询结果。
	// 定义一个 int 类型的变量 ret，用于存储操作返回值。
	// 定义一个 int 类型的变量 actions_num，用于存储操作的数量。
	// 定义一个 zbx_uint64_t 类型的变量 actionid，用于存储操作 ID。
	// 定义一个 zbx_uint64_t 类型的变量 operationid，用于存储操作 ID。

	// 执行 DBselect 查询，统计 actions 表中 recovery_msg 为 1 的记录数量。
	// 如果查询结果为空或者 actions_num 为 0，说明没有符合条件的记录，直接返回成功。
	result = DBselect("select count(*) from actions where recovery_msg=1");
	if (NULL == (row = DBfetch(result)) || 0 == (actions_num = atoi(row[0])))
	{
		ret = SUCCEED;
		goto out;
	}

	// 获取操作 ID，用于后续插入操作。
	operationid = DBget_maxid_num("operations", actions_num);

	// 预处理插入操作，准备插入操作的相关信息。
	// 准备插入操作：operations 表，插入操作 ID、操作 ID、操作类型、恢复信息。
	// 准备插入操作：opmessage 表，插入操作 ID、默认信息、主题、消息。
	zbx_db_insert_prepare(&db_insert, "operations", "operationid", "actionid", "operationtype", "recovery", NULL);
	zbx_db_insert_prepare(&db_insert_msg, "opmessage", "operationid", "default_msg", "subject", "message", NULL);

	// 释放上一查询的结果，开始执行新的查询。
	DBfree_result(result);
	// 查询 actions 表中 recovery_msg 为 1 的记录，循环处理。
	result = DBselect("select actionid,r_shortdata,r_longdata from actions where recovery_msg=1");

	while (NULL != (row = DBfetch(result)))
	{
		// 将字符串转换为 UINT64 类型，便于后续使用。
		ZBX_STR2UINT64(actionid, row[0]);
		// 设置操作类型为 OPERATION_TYPE_RECOVERY_MESSAGE，方便后续处理。
		/* operationtype: 11 - OPERATION_TYPE_RECOVERY_MESSAGE */
		zbx_db_insert_add_values(&db_insert, operationid, actionid, 11, 1);
		zbx_db_insert_add_values(&db_insert_msg, operationid, 1, row[1], row[2]);

		// 递增操作 ID，便于后续插入操作。
		operationid++;
	}

	// 执行插入操作，如果成功，继续执行插入消息的操作。
/******************************************************************************
 * *
 *这段代码的主要目的是对数据库中的action进行处理，包括禁用和转换。具体来说，代码的功能如下：
 *
 *1. 从数据库中查询action信息，根据eventsource和evaltype进行筛选。
 *2. 遍历查询结果，对每个action进行验证。
 *3. 根据验证结果，将action分为disable和convert两类。
 *4. 对于disable类别的action，将actionid添加到actionids_disable中，并打印警告日志。
 *5. 对于convert类别的action，将actionid添加到actionids_convert中，并打印警告日志。
 *6. 对actionids_disable和actionids_convert中的action进行更新操作，包括禁用action、更新action的r_shortdata和r_longdata为默认值，以及将operation状态更新为recovery。
 *7. 判断更新操作是否成功，如果失败，则返回FAIL。
 *8. 释放内存，返回ret值。
 ******************************************************************************/
static int	DBpatch_3010024(void)
{
	// 定义变量
	DB_ROW			row;
	DB_RESULT		result;
	zbx_vector_uint64_t	actionids_disable, actionids_convert;
	int			ret, evaltype, eventsource, recovery_msg;
	zbx_uint64_t		actionid;

	// 创建两个vector用于存储actionid
	zbx_vector_uint64_create(&actionids_disable);
	zbx_vector_uint64_create(&actionids_convert);

	/* eventsource: 0 - EVENT_SOURCE_TRIGGERS, 3 - EVENT_SOURCE_INTERNAL */
	result = DBselect("select actionid,name,eventsource,evaltype,recovery_msg from actions"
			" where eventsource in (0,3)");

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 将actionid从row[0]转换为uint64类型
		ZBX_STR2UINT64(actionid, row[0]);
		eventsource = atoi(row[2]);
		evaltype = atoi(row[3]);
		recovery_msg = atoi(row[4]);

		// 调用DBpatch_3010024_validate_action函数验证action
		ret = DBpatch_3010024_validate_action(actionid, eventsource, evaltype, recovery_msg);

		// 根据ret值判断action处理方式
		if (ZBX_3010024_ACTION_DISABLE == ret)
		{
			// 如果为disable，则将actionid添加到actionids_disable中
			zbx_vector_uint64_append(&actionids_disable, actionid);
			// 打印警告日志
			zabbix_log(LOG_LEVEL_WARNING, "Action \"%s\" will be disabled during database upgrade:"
					" conditions might have matched success event which is not supported anymore.",
					row[1]);
		}
		else if (ZBX_3010024_ACTION_CONVERT == ret)
		{
			// 如果为convert，则将actionid添加到actionids_convert中
			zbx_vector_uint64_append(&actionids_convert, actionid);
			// 打印警告日志
			zabbix_log(LOG_LEVEL_WARNING, "Action \"%s\" operations will be converted to recovery"
					" operations during database upgrade.", row[1]);
		}
	}
	// 释放result内存
	DBfree_result(result);

	// 初始化ret值为0
	ret = SUCCEED;

	// 如果actionids_disable或actionids_convert不为空，则执行更新操作
	if (0 != actionids_disable.values_num || 0 != actionids_convert.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		// 开始执行多条更新操作
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		// 如果actionids_disable不为空，则执行禁用操作
		if (0 != actionids_disable.values_num)
		{
			// 更新action状态为disabled
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update actions set status=1 where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids_disable.values,
					actionids_disable.values_num);
			// 添加分号和换行符
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
		}

		// 如果actionids_convert不为空，则执行转换操作
		if (0 != actionids_convert.values_num)
		{
			// 更新action的r_shortdata和r_longdata为默认值
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update actions"
					" set r_shortdata=def_shortdata,"
						"r_longdata=def_longdata"
					" where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids_convert.values,
					actionids_convert.values_num);
			// 添加分号和换行符
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

			// 更新operation状态为recovery
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update operations set recovery=1 where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids_convert.values,
					actionids_convert.values_num);
			// 添加分号和换行符
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
		}

		// 结束执行多条更新操作
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		// 判断更新操作是否成功
		if (ZBX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;

		// 释放sql内存
		zbx_free(sql);
	}

	// 释放actionids_disable和actionids_convert内存
	zbx_vector_uint64_destroy(&actionids_convert);
	zbx_vector_uint64_destroy(&actionids_disable);

	// 返回ret值
	return ret;
}

			if (1 == value)
			{
				if (ZBX_3010024_ACTION_CONVERT == ret)
				{
					ret = ZBX_3010024_ACTION_DISABLE;
					break;

				}
				ret = ZBX_3010024_ACTION_NOTHING;
			}
		}
		else if (3 == eventsource)
		{
			/* conditiontype: 23 -  CONDITION_TYPE_EVENT_TYPE */
			if (23 != conditiontype)
				continue;

			value = atoi(row[1]);

			/* event types:                                                          */
			/*            1 - Event type:  Item in "normal" state                    */
			/*            3 - Low-level discovery rule in "normal" state             */
			/*            5 - Trigger in "normal" state                              */
			if (1 == value || 3 == value || 5 == value)
			{
				ret = ZBX_3010024_ACTION_DISABLE;
				break;
			}

			/* event types:                                                          */
			/*            0 - Event type:  Item in "not supported" state             */
			/*            2 - Low-level discovery rule in "not supported" state      */
			/*            4 - Trigger in "unknown" state                             */
			if (0 == value || 2 == value || 4 == value)
				ret = ZBX_3010024_ACTION_NOTHING;
		}
	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 如果ret值为ZBX_3010024_ACTION_CONVERT，则继续执行以下操作
	if (ZBX_3010024_ACTION_CONVERT == ret)
	{
		// 查询操作信息
		result = DBselect("select o.operationtype,o.esc_step_from,o.esc_step_to,count(oc.opconditionid)"
					" from operations o"
					" left join opconditions oc"
						" on oc.operationid=o.operationid"
					" where o.actionid=" ZBX_FS_UI64
					" group by o.operationid,o.operationtype,o.esc_step_from,o.esc_step_to",
					actionid);

		// 遍历查询结果
		while (NULL != (row = DBfetch(result)))
		{
			/* 不能转换动作如果：                                                                    */
			/*   有未在 escalation_start 时刻执行的升级步骤                                      */
			/*   有用于动作操作的条件定义                                                         */
			/*   有发送消息的动作且恢复消息已启用 */
			if (1 != atoi(row[1]) || 0 != atoi(row[3]) || (0 == atoi(row[0]) && 0 != recovery_msg))
			{
				ret = ZBX_3010024_ACTION_DISABLE;
				break;
			}
		}

		// 释放数据库查询结果
		DBfree_result(result);
	}

	// 返回验证结果
	return ret;
}

			}
		}

		DBfree_result(result);
	}

	return ret;
}

static int	DBpatch_3010024(void)
{
	DB_ROW			row;
	DB_RESULT		result;
	zbx_vector_uint64_t	actionids_disable, actionids_convert;
	int			ret, evaltype, eventsource, recovery_msg;
	zbx_uint64_t		actionid;

	zbx_vector_uint64_create(&actionids_disable);
	zbx_vector_uint64_create(&actionids_convert);

	/* eventsource: 0 - EVENT_SOURCE_TRIGGERS, 3 - EVENT_SOURCE_INTERNAL */
	result = DBselect("select actionid,name,eventsource,evaltype,recovery_msg from actions"
			" where eventsource in (0,3)");

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(actionid, row[0]);
		eventsource = atoi(row[2]);
		evaltype = atoi(row[3]);
		recovery_msg = atoi(row[4]);

		ret = DBpatch_3010024_validate_action(actionid, eventsource, evaltype, recovery_msg);

		if (ZBX_3010024_ACTION_DISABLE == ret)
		{
			zbx_vector_uint64_append(&actionids_disable, actionid);
			zabbix_log(LOG_LEVEL_WARNING, "Action \"%s\" will be disabled during database upgrade:"
					" conditions might have matched success event which is not supported anymore.",
					row[1]);
		}
		else if (ZBX_3010024_ACTION_CONVERT == ret)
		{
			zbx_vector_uint64_append(&actionids_convert, actionid);
			zabbix_log(LOG_LEVEL_WARNING, "Action \"%s\" operations will be converted to recovery"
					" operations during database upgrade.", row[1]);
		}
	}
	DBfree_result(result);

	ret = SUCCEED;

	if (0 != actionids_disable.values_num || 0 != actionids_convert.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (0 != actionids_disable.values_num)
		{
			/* status: 1 - ACTION_STATUS_DISABLED */

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update actions set status=1 where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids_disable.values,
					actionids_disable.values_num);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
		}

		if (0 != actionids_convert.values_num)
		{
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update actions"
					" set r_shortdata=def_shortdata,"
						"r_longdata=def_longdata"
					" where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids_convert.values,
					actionids_convert.values_num);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update operations set recovery=1 where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids_convert.values,
					actionids_convert.values_num);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (ZBX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;

		zbx_free(sql);
	}

	zbx_vector_uint64_destroy(&actionids_convert);
	zbx_vector_uint64_destroy(&actionids_disable);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是删除名为 \"actions\" 表中的 \"recovery_msg\" 字段。通过调用 DBdrop_field 函数实现，该函数接收两个参数，分别是表名和要删除的字段名。最后，代码块返回整型值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010025 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_3010025(void)
{
    // 调用 DBdrop_field 函数，传入两个参数：表名 "actions" 和要删除的字段名 "recovery_msg"
    // 该函数的主要目的是删除名为 "recovery_msg" 的字段 from 名为 "actions" 的表
    return DBdrop_field("actions", "recovery_msg");
}


/* patch 3010026 */

#define	ZBX_3010026_TOKEN_UNKNOWN	0
#define	ZBX_3010026_TOKEN_OPEN		1
#define	ZBX_3010026_TOKEN_CLOSE		2
#define	ZBX_3010026_TOKEN_AND		3
#define	ZBX_3010026_TOKEN_OR		4
#define	ZBX_3010026_TOKEN_VALUE		5
#define	ZBX_3010026_TOKEN_END		6
/******************************************************************************
 * *
 *整个代码块的主要目的是获取符合条件的conditionids。函数接受四个参数：actionid（动作ID）、name（动作名称）、eventsource（事件源）和conditionids（用于存储符合条件的conditionid的向量）。根据eventsource的值，查询数据库中对应的条件类型和值，并将符合条件的conditionid添加到conditionids向量中。同时，输出相应的日志警告，提示这些条件将在数据库升级过程中被移除。
 ******************************************************************************/
/* 定义一个函数，用于获取符合条件的conditionids */
static void DBpatch_3010026_get_conditionids(zbx_uint64_t actionid, const char *name, int eventsource,
                                             zbx_vector_uint64_t *conditionids)
{
    /* 定义一个DB_ROW结构体变量，用于存储数据库查询结果的一行数据 */
    DB_ROW row;
    /* 定义一个DB_RESULT结构体变量，用于存储数据库查询结果 */
    DB_RESULT result;
    /* 定义一个zbx_uint64_t类型的变量，用于存储conditionid */
    zbx_uint64_t conditionid;
    /* 定义一个char类型的指针变量，用于存储condition字符串 */
    char *condition = NULL;
    /* 定义一个size_t类型的变量，用于存储condition分配的大小 */
    size_t condition_alloc = 0, condition_offset = 0;
    /* 定义一个int类型的变量，用于存储value */
    int value;

    /* 根据eventsource的值，判断执行哪种查询 */
    if (0 == eventsource)
    {
        /* conditiontype为5时，表示查询触发器值 */
        result = DBselect("select conditionid,value from conditions"
                        " where actionid=%zu"
                        " and conditiontype=5",
                        actionid);
    }
    else if (3 == eventsource)
    {
        /* conditiontype为23时，表示查询事件类型 */
        result = DBselect("select conditionid,value from conditions"
                        " where actionid=%zu"
                        " and conditiontype=23"
                        " and value in ('1', '3', '5')",
                        actionid);
    }
    else
        return;

    /* 循环读取数据库查询结果 */
    while (NULL != (row = DBfetch(result)))
    {
        /* 将conditionid从row中解析出来 */
        ZBX_STR2UINT64(conditionid, row[0]);
        /* 将conditionid添加到conditionids向量中 */
        zbx_vector_uint64_append(conditionids, conditionid);

        /* 解析value */
        value = atoi(row[1]);

        /* 根据eventsource的值，输出相应的日志信息 */
        if (0 == eventsource)
        {
            /* value为0时，表示触发器正常；值为1时，表示触发器存在问题 */
            const char *values[] = {"OK", "PROBLEM"};

            /* 格式化condition字符串 */
            zbx_snprintf_alloc(&condition, &condition_alloc, &condition_offset, "Trigger value = %s",
                              values[value]);
        }
        else
        {
            /* value为1时，表示物品处于正常状态；值为3时，表示低层发现规则处于正常状态；值为5时，表示触发器处于正常状态 */
            const char *values[] = {NULL, "Item in 'normal' state",
                                  NULL, "Low-level discovery rule in 'normal' state",
                                  NULL, "Trigger in 'normal' state"};

            /* 格式化condition字符串 */
            zbx_snprintf_alloc(&condition, &condition_alloc, &condition_offset, "Event type = %s",
                              values[value]);
        }

        /* 输出日志警告 */
        zabbix_log(LOG_LEVEL_WARNING, "Action \"%s\" condition \"%s\" will be removed during database upgrade:"
                  " this type of condition is not supported anymore", name, condition);

        /* 清零condition_offset，以便下一次循环时从开头开始输出 */
        condition_offset = 0;
    }

    /* 释放condition内存 */
    zbx_free(condition);
    /* 释放数据库查询结果 */
    DBfree_result(result);
}

							NULL, "Low-level discovery rule in 'normal' state",
							NULL, "Trigger in 'normal' state"};

			zbx_snprintf_alloc(&condition, &condition_alloc, &condition_offset, "Event type = %s",
					values[value]);
		}

		zabbix_log(LOG_LEVEL_WARNING, "Action \"%s\" condition \"%s\" will be removed during database upgrade:"
				" this type of condition is not supported anymore", name, condition);

		condition_offset = 0;
	}

	zbx_free(condition);
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: DBpatch_3010026_expression_skip_whitespace                       *
 *                                                                            *
 * Purpose: skips whitespace characters                                       *
 *                                                                            *
 * Parameters: expression - [IN] the expression to process                    *
 *             offset     - [IN] the starting offset in expression            *
 *                                                                            *
 * Return value: the position of first non-whitespace character after offset  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个函数的主要目的是：接收一个表达式字符串和一个偏移量，跳过字符串中的所有空格，然后返回空格之后的第一个有效字符的位置。这样做的好处是在处理表达式时，可以忽略掉表达式中的空格，从而简化后续的处理逻辑。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010026_expression_skip_whitespace 的静态 size_t 类型函数，接收两个参数：
// 1. 一个 const char * 类型的表达式字符串（expression）
// 2. 一个 size_t 类型的偏移量（offset）
static size_t DBpatch_3010026_expression_skip_whitespace(const char *expression, size_t offset)
{
	// 使用一个 while 循环，当 expression 字符串中的当前字符为空格（' '）时，继续循环
	while (' ' == expression[offset])
	{
		// 移动 offset 指针，跳过空格字符
		offset++;
	}
/******************************************************************************
 * *
 *整个代码块的主要目的是解析给定的 C 语言表达式，根据表达式中的关键字和括号序列生成对应的令牌（ZBX_3010026_TOKEN_UNKNOWN、ZBX_3010026_TOKEN_END、ZBX_3010026_TOKEN_OPEN、ZBX_3010026_TOKEN_CLOSE、ZBX_3010026_TOKEN_OR、ZBX_3010026_TOKEN_AND、ZBX_3010026_TOKEN_VALUE），并将解析结果存储在传入的 zbx_strloc_t 结构体指针所指向的内存空间中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010026_expression_get_token 的静态函数，该函数用于解析 C 语言表达式
// 参数：
//     expression：表达式字符串
//     offset：当前解析的位置
//     token：存储解析结果的结构体指针
// 返回值：解析得到的令牌类型（ZBX_3010026_TOKEN_UNKNOWN、ZBX_3010026_TOKEN_END、ZBX_3010026_TOKEN_OPEN、ZBX_3010026_TOKEN_CLOSE、ZBX_3010026_TOKEN_OR、ZBX_3010026_TOKEN_AND、ZBX_3010026_TOKEN_VALUE）

static int	DBpatch_3010026_expression_get_token(const char *expression, int offset, zbx_strloc_t *token)
{
	// 初始化返回值 ret 为 ZBX_3010026_TOKEN_UNKNOWN
	int	ret = ZBX_3010026_TOKEN_UNKNOWN;

	// 更新 offset，跳过表达式中的空白字符
	offset = DBpatch_3010026_expression_skip_whitespace(expression, offset);
	// 保存当前解析位置，即令牌的左边界
	token->l = offset;

	// 判断 expression[offset] 的字符类型
	switch (expression[offset])
	{
		// 遇到空字符，表示表达式结束
		case '\0':
			// 更新令牌的右边界
			token->r = offset;
			// 设置返回值为 ZBX_3010026_TOKEN_END，表示表达式结束
			ret = ZBX_3010026_TOKEN_END;
			break;
		// 遇到左括号 '('，表示开启一个括号序列
		case '(':
			// 更新令牌的右边界
			token->r = offset;
			// 设置返回值为 ZBX_3010026_TOKEN_OPEN，表示开启一个括号
			ret = ZBX_3010026_TOKEN_OPEN;
			break;
		// 遇到右括号 ')'，表示关闭一个括号序列
		case ')':
			// 更新令牌的右边界
			token->r = offset;
			// 设置返回值为 ZBX_3010026_TOKEN_CLOSE，表示关闭一个括号
			ret = ZBX_3010026_TOKEN_CLOSE;
			break;
		// 遇到 'o' 字符，判断是否为 'or' 关键字
		case 'o':
			if ('r' == expression[offset + 1])
			{
				// 更新令牌的右边界
				token->r = offset + 1;
				// 设置返回值为 ZBX_3010026_TOKEN_OR，表示找到 'or' 关键字
				ret = ZBX_3010026_TOKEN_OR;
			}
			break;
		// 遇到 'a' 字符，判断是否为 'and' 关键字
		case 'a':
			if ('n' == expression[offset + 1] && 'd' == expression[offset + 2])
			{
				// 更新令牌的右边界
				token->r = offset + 2;
				// 设置返回值为 ZBX_3010026_TOKEN_AND，表示找到 'and' 关键字
				ret = ZBX_3010026_TOKEN_AND;
			}
			break;
		// 遇到 '{' 字符，判断是否为数字字符串
		case '{':
			// 遍历表达式中的数字字符
			while (0 != isdigit(expression[++offset]))
				;
			// 遇到 '}' 字符，表示找到一个数字字符串
			if ('}' == expression[offset])
			{
				// 更新令牌的右边界
				token->r = offset;
				// 设置返回值为 ZBX_3010026_TOKEN_VALUE，表示找到一个数字字符串
				ret = ZBX_3010026_TOKEN_VALUE;
			}
			break;
		// 其他情况下，返回 ZBX_3010026_TOKEN_UNKNOWN
		default:
			break;
	}

	// 返回解析结果
	return ret;
}

			break;
		case 'a':
			if ('n' == expression[offset + 1] && 'd' == expression[offset + 2])
			{
				token->r = offset + 2;
				ret = ZBX_3010026_TOKEN_AND;
			}
			break;
		case '{':
			while (0 != isdigit(expression[++offset]))
				;
			if ('}' == expression[offset])
			{
				token->r = offset;
				ret = ZBX_3010026_TOKEN_VALUE;
			}
			break;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBpatch_3010026_expression_validate_value                        *
 *                                                                            *
 * Purpose: checks if the value does not match any filter value               *
 *                                                                            *
 * Parameters: expression - [IN] the expression to process                    *
 *             value      - [IN] the location of value in expression          *
 *             filter     - [IN] a list of values to compare                  *
 *                                                                            *
 * Return value: SUCCEED - the value does not match any filter values         *
 *               FAIL    - otherwise                                          *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个C语言表达式，并根据给定的过滤器过滤掉符合条件的值。具体来说，这个函数会对表达式中的每个字符进行解析，遇到左括号时递归调用自身，过滤内部表达式；遇到值时判断是否符合过滤条件，如果符合则剪裁值；遇到操作符时，根据操作符的类型和上一个操作符的类型，剪裁相应的操作符和值。最后，当遇到结束符时，结束整个解析过程。
 ******************************************************************************/
/*
 * 静态函数：DBpatch_3010026_expression_remove_values_impl
 * 功能：根据给定的表达式，过滤掉符合条件的值
 * 参数：
 *   expression：字符串类型的表达式
 *   exp_token：指向zbx_strloc_t结构体的指针，用于保存当前解析到的位置
 *   filter：指向zbx_vector_str_t结构体的指针，用于存储需要过滤的值
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int DBpatch_3010026_expression_remove_values_impl(char *expression, zbx_strloc_t *exp_token, const zbx_vector_str_t *filter)
{
	/* 定义变量，用于保存解析过程中的状态和值 */
	zbx_strloc_t	token, cut_loc, op_token, value_token;
	int		token_type, cut_value = 0, state = ZBX_3010026_PARSE_VALUE,
			prevop_type = ZBX_3010026_TOKEN_UNKNOWN;

	/* 初始化exp_token，使其指向expression的起始位置 */
	exp_token->r = exp_token->l = 0;

	/* 循环解析表达式中的每个字符 */
	while (ZBX_3010026_TOKEN_UNKNOWN != (token_type =
			DBpatch_3010026_expression_get_token(expression, exp_token->r, &token)))
	{
		/* 处于VALUE状态，解析值 */
		if (ZBX_3010026_PARSE_VALUE == state)
		{
			state = ZBX_3010026_PARSE_OP;

			/* 遇到左括号，解析其内部的表达式 */
			if (ZBX_3010026_TOKEN_OPEN == token_type)
			{
				token.l = token.r + 1;

				/* 递归调用自身，过滤内部表达式 */
				if (FAIL == DBpatch_3010026_expression_remove_values_impl(expression, &token, filter))
					return FAIL;

				/* 判断是否为右括号 */
				if (')' != expression[token.r])
					return FAIL;

				/* 跳过空格 */
				if (token.r == DBpatch_3010026_expression_skip_whitespace(expression, token.l))
					cut_value = 1;

				/* 把左括号包含在内 */
				token.l--;

				value_token = token;
				exp_token->r = token.r + 1;

				continue;
			}
			/* 遇到值，判断是否符合过滤条件 */
			else if (ZBX_3010026_TOKEN_VALUE != token_type)
				return FAIL;

			/* 验证值 */
			if (SUCCEED == DBpatch_3010026_expression_validate_value(expression, &token, filter))
				cut_value = 1;

			value_token = token;
			exp_token->r = token.r + 1;

			continue;
		}

		/* 处于OP状态，解析操作符 */
		state = ZBX_3010026_PARSE_VALUE;

		/* 剪裁值 */
		if (1 == cut_value)
		{
			/* 遇到AND或OR操作符，剪裁相应的操作符和值 */
			if (ZBX_3010026_TOKEN_AND == prevop_type || (ZBX_3010026_TOKEN_OR == prevop_type &&
					(ZBX_3010026_TOKEN_CLOSE == token_type || ZBX_3010026_TOKEN_END == token_type)))
			{
				cut_loc.l = op_token.l;
				cut_loc.r = value_token.r;
				/* 移动位置，去掉剪裁的部分 */
				DBpatch_3010026_expression_move_location(&token, -(cut_loc.r - cut_loc.l + 1));
				prevop_type = token_type;
				op_token = token;
			}
			else
			{
				cut_loc.l = value_token.l;

				/* 遇到操作符或结束符，剪裁至操作符或结束符之前 */
				if (ZBX_3010026_TOKEN_CLOSE == token_type || ZBX_3010026_TOKEN_END == token_type)
					cut_loc.r = token.l - 1;
				else
					cut_loc.r = token.r;

				/* 移动位置，去掉剪裁的部分 */
				DBpatch_3010026_expression_move_location(&token, -(cut_loc.r - cut_loc.l + 1));
			}

			/* 剪裁值 */
			DBpatch_3010026_expression_cut_substring(expression, &cut_loc);
			cut_value = 0;
		}
		else
		{
			/* 记录上一个操作符类型 */
			prevop_type = token_type;
			op_token = token;
		}

		/* 遇到操作符或结束符，结束循环 */
		if (ZBX_3010026_TOKEN_CLOSE == token_type || ZBX_3010026_TOKEN_END == token_type)
		{
			exp_token->r = token.r;
			return SUCCEED;
		}

		/* 遇到AND或OR操作符，重置状态 */
		if (ZBX_3010026_TOKEN_AND != token_type && ZBX_3010026_TOKEN_OR != token_type)
			return FAIL;

		exp_token->r = token.r + 1;
	}

	return FAIL;
}

 * Parameters: expression - [IN] the expression to process                    *
 *             exp_token  - [IN] the current location in expression           *
 *             filter     - [IN] a list of values                             *
 *                                                                            *
 * Return value: SUCCEED - the expression was processed successfully          *
 *               FAIL    - failed to parse expression                         *
 *                                                                            *
 ******************************************************************************/
static int	DBpatch_3010026_expression_remove_values_impl(char *expression, zbx_strloc_t *exp_token,
		const zbx_vector_str_t *filter)
{
	zbx_strloc_t	token, cut_loc, op_token, value_token;
	int		token_type, cut_value = 0, state = ZBX_3010026_PARSE_VALUE,
			prevop_type = ZBX_3010026_TOKEN_UNKNOWN;

	exp_token->r = exp_token->l;

	while (ZBX_3010026_TOKEN_UNKNOWN != (token_type =
			DBpatch_3010026_expression_get_token(expression, exp_token->r, &token)))
	{
		/* parse value */
		if (ZBX_3010026_PARSE_VALUE == state)
		{
			state = ZBX_3010026_PARSE_OP;

			if (ZBX_3010026_TOKEN_OPEN == token_type)
			{
				token.l = token.r + 1;

				if (FAIL == DBpatch_3010026_expression_remove_values_impl(expression, &token, filter))
					return FAIL;

				if (')' != expression[token.r])
					return FAIL;

				if (token.r == DBpatch_3010026_expression_skip_whitespace(expression, token.l))
					cut_value = 1;

				/* include opening '(' into token */
				token.l--;

				value_token = token;
				exp_token->r = token.r + 1;

				continue;
			}
			else if (ZBX_3010026_TOKEN_VALUE != token_type)
				return FAIL;

			if (SUCCEED == DBpatch_3010026_expression_validate_value(expression, &token, filter))
				cut_value = 1;

			value_token = token;
			exp_token->r = token.r + 1;

			continue;
		}

		/* parse operator */
		state = ZBX_3010026_PARSE_VALUE;

		if (1 == cut_value)
		{
			if (ZBX_3010026_TOKEN_AND == prevop_type || (ZBX_3010026_TOKEN_OR == prevop_type &&
					(ZBX_3010026_TOKEN_CLOSE == token_type || ZBX_3010026_TOKEN_END == token_type)))
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 DBpatch_3010026 的静态函数，该函数用于处理数据库中的动作（actions）表。
主要目的是更新动作的 formula 字段，将其从原来的表达式更新为新的表达式。
同时，如果动作没有条件，则将其 evaltype 字段设置为 0（表示 AND/OR 运算符）。

输入：无

输出：成功执行更新操作（ret = SUCCEED）或失败（ret = FAIL）

*/

static int	DBpatch_3010026(void)
{
	/* 定义变量，用于存储从数据库查询到的动作信息 */
	DB_ROW			row;
	DB_RESULT		result;

	/* 定义条件ids、动作ids和过滤器（用于存储条件）的变量 */
	zbx_vector_uint64_t	conditionids, actionids;
	int			ret = FAIL, evaltype, index, i, eventsource;

	/* 定义动作id、sql 字符串和公式字符串指针 */
	zbx_uint64_t		actionid;
	char			*sql = NULL, *formula;

	/* 定义 sql 分配大小、sql_offset */
	size_t			sql_alloc = 0, sql_offset = 0;

	/* 定义过滤器（用于存储动作名称） */
	zbx_vector_str_t	filter;

	/* 创建条件ids、动作ids和过滤器 */
	zbx_vector_uint64_create(&conditionids);
	zbx_vector_uint64_create(&actionids);
	zbx_vector_str_create(&filter);

	/* 开始执行多个更新操作 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 从数据库中查询动作信息 */
	result = DBselect("select actionid,eventsource,evaltype,formula,name from actions");

	/* 遍历查询结果中的每一行，处理动作 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将动作id、事件源、评估类型和公式转换为整数类型 */
		ZBX_STR2UINT64(actionid, row[0]);
		eventsource = atoi(row[1]);
		evaltype = atoi(row[2]);

		/* 计算条件ids中的索引 */
		index = conditionids.values_num;

		/* 获取条件ids中的索引，并更新动作的 formula 字段 */
		DBpatch_3010026_get_conditionids(actionid, row[4], eventsource, &conditionids);

		/* 判断评估类型是否为 CONDITION_EVAL_TYPE_EXPRESSION（3） */
		if (3 != evaltype)
			continue;

		/* 如果没有新的条件要移除，则处理下一个动作 */
		if (index == conditionids.values_num)
			continue;

		/* 复制公式字符串 */
		formula = zbx_strdup(NULL, row[3]);

		/* 为过滤器添加条件 */
		for (i = index; i < conditionids.values_num; i++)
			zbx_vector_str_append(&filter, zbx_dsprintf(NULL, "{" ZBX_FS_UI64 "}", conditionids.values[i]));

		/* 更新动作的 formula 字段 */
		if (SUCCEED == DBpatch_3010026_expression_remove_values(formula, &filter))
		{
			/* 构造更新 sql 语句 */
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update actions set formula='%s'"
					" where actionid=" ZBX_FS_UI64 ";\
", formula, actionid);
		}

		/* 释放公式字符串占用的内存 */
		zbx_free(formula);
		zbx_vector_str_clear_ext(&filter, zbx_str_free);

		/* 执行更新操作 */
		if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
			goto out;
	}

	/* 结束多个更新操作 */
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 如果 sql_offset 大于 16，说明存在多条更新语句 */
	if (16 < sql_offset)
	{
		/* 执行多条更新语句 */
		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	/* 如果有条件要删除，则执行删除操作 */
	if (0 != conditionids.values_num)
	{
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from conditions where");

		/* 添加删除条件 */
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "conditionid", conditionids.values,
				conditionids.values_num);

		/* 执行删除操作 */
		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	/* 重置动作的 evaltype 字段为 AND/OR（0） */

	/* 释放内存 */
	DBfree_result(result);
	zbx_free(sql);
	zbx_vector_str_destroy(&filter);
	zbx_vector_uint64_destroy(&actionids);
	zbx_vector_uint64_destroy(&conditionids);

	/* 返回成功（ret = SUCCEED）或失败（ret = FAIL） */
	ret = SUCCEED;

out:
	/* 释放内存 */
	DBfree_result(result);
	zbx_free(sql);
	zbx_vector_str_destroy(&filter);
	zbx_vector_uint64_destroy(&actionids);
	zbx_vector_uint64_destroy(&conditionids);

	return ret;
}

		if (SUCCEED == DBpatch_3010026_expression_remove_values(formula, &filter))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update actions set formula='%s'"
					" where actionid=" ZBX_FS_UI64 ";\n", formula, actionid);
		}

		zbx_free(formula);
		zbx_vector_str_clear_ext(&filter, zbx_str_free);

		if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
			goto out;
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
	{
		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	if (0 != conditionids.values_num)
	{
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from conditions where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "conditionid", conditionids.values,
				conditionids.values_num);

		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	/* reset action evaltype to AND/OR if it has no more conditions left */

	DBfree_result(result);
	result = DBselect("select a.actionid,a.name,a.evaltype,count(c.conditionid)"
			" from actions a"
			" left join conditions c"
				" on a.actionid=c.actionid"
			" group by a.actionid,a.name,a.evaltype");

	while (NULL != (row = DBfetch(result)))
	{
		/* reset evaltype to AND/OR (0) if action has no more conditions and it's evaltype is not AND/OR */
		if (0 == atoi(row[3]) && 0 != atoi(row[2]))
		{
			ZBX_STR2UINT64(actionid, row[0]);
			zbx_vector_uint64_append(&actionids, actionid);

			zabbix_log(LOG_LEVEL_WARNING, "Action \"%s\" type of calculation will be changed to And/Or"
					" during database upgrade: no action conditions found", row[1]);
		}
	}

	if (0 != actionids.values_num)
	{
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update actions set evaltype=0 where");

		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids.values,
				actionids.values_num);

		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	ret = SUCCEED;

out:
	DBfree_result(result);
	zbx_free(sql);
	zbx_vector_str_destroy(&filter);
	zbx_vector_uint64_destroy(&actionids);
	zbx_vector_uint64_destroy(&conditionids);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_3010027 的静态函数，该函数用于向名为 \"triggers\" 的数据库表中添加一个字段，字段名称为 \"correlation_mode\"，字段值为 \"0\"，字段类型为整型，不允许为空。最后调用 DBadd_field 函数将字段添加到表中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010027 的静态函数，该函数为空函数
static int DBpatch_3010027(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {
		// 设置字段名称为 "correlation_mode"
		"correlation_mode",
		// 设置字段值为 "0"
		"0",
		// 设置字段类型为 ZBX_TYPE_INT（整型）
		NULL,
		// 设置字段单位为 NULL
		NULL,
		// 设置字段值为 0
		0,
		// 设置字段类型为 ZBX_TYPE_INT（整型）
		ZBX_TYPE_INT,
		// 设置字段不允许为空
		ZBX_NOTNULL,
		// 设置字段其他属性，这里为 0
		0
	};

	// 调用 DBadd_field 函数，将定义好的字段添加到名为 "triggers" 的数据库表中
	// 传入两个参数：表名（"triggers"）和字段变量（&field）
	return DBadd_field("triggers", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是在一个名为 \"triggers\" 的数据库表中添加一个新字段。具体来说，它定义了一个 ZBX_FIELD 结构体变量（field），用于表示要添加的字段，然后调用 DBadd_field 函数将这个字段添加到 \"triggers\" 表中。整个代码块的功能可以用一句话概括：添加一个新字段到数据库表 \"triggers\" 中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010028 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_3010028(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"correlation_tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到名为 "triggers" 的数据库表中
	return DBadd_field("triggers", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"problem\" 的数据库表中添加一个新字段。代码通过定义一个 ZBX_FIELD 结构体变量来描述字段的信息，包括字段名、字段值、字段类型、是否非空等。然后调用 DBadd_field 函数将这个字段添加到数据库中，并返回添加结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010029 的静态函数，该函数为空返回类型为整型的函数
static int	DBpatch_3010029(void)
{
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个名为 DBpatch_3010030 的静态函数，该函数用于向数据库中添加一个名为 \"problem\" 的字段，字段类型为整型（ZBX_TYPE_INT），非空（ZBX_NOTNULL），并设置其他相关属性。最后输出添加字段的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010030 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010030(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"ns", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到数据库中，并将返回值赋给 field
	return DBadd_field("problem", &field);
}



static int	DBpatch_3010030(void)
{
	const ZBX_FIELD	field = {"ns", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("problem", &field);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"problem\" 的数据库表中添加一个字段。具体来说，它定义了一个 ZBX_FIELD 结构体变量（field），然后将其传递给 DBadd_field 函数以将其添加到数据库中。代码中使用了 static 关键字声明了一个静态函数，这意味着该函数在程序的生命周期内只被初始化一次。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010031 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010031(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"r_eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"problem_tag\" 的表。表中包含四个字段：problemtagid、eventid、tag 和 value。其中，problemtagid 和 eventid 字段类型为 ZBX_TYPE_ID，tag 和 value 字段类型为 ZBX_TYPE_CHAR。表是非空的，并且创建表的操作由 DBcreate_table 函数完成。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010036 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010036(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包含以下字段：
			{"problem_tag", "problemtagid", 0,
				{
					// 定义第一个字段，名为 "problemtagid"，类型为 ZBX_TYPE_ID，非空，索引为 0
					{"problemtagid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 定义第二个字段，名为 "eventid"，类型为 ZBX_TYPE_ID，非空，索引为 1
					{"eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 定义第三个字段，名为 "tag"，类型为 ZBX_TYPE_CHAR，非空，索引为 2
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					// 定义第四个字段，名为 "value"，类型为 ZBX_TYPE_CHAR，非空，索引为 3
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					// 定义一个空字段，用于结束字段列表
					{0}
				},
				// 结束 ZBX_TABLE 结构体的初始化
				NULL
			};

	// 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
	return DBcreate_table(&table);
}

// 定义一个名为 DBpatch_3010032 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010032(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"r_clock", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到数据库中，返回添加结果
	return DBadd_field("problem", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"problem\" 的数据库表中添加一个字段。具体来说，它定义了一个 ZBX_FIELD 结构体变量（field），然后调用 DBadd_field 函数将这个字段添加到 \"problem\" 表中。添加的字段名称为 \"r_ns\"，类型为整型（ZBX_TYPE_INT），非空（ZBX_NOTNULL），其他参数分别为 NULL。整个过程中，函数返回值为添加操作的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010033 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010033(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"r_ns", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到数据库中，添加的表名为 "problem"
	return DBadd_field("problem", &field);
/******************************************************************************
 * *
 *整个代码块的主要目的是向 \"problem\" 表中添加一个外键约束，该约束用于关联到另一个表（根据字段名 r_eventid 确定）。添加外键约束的函数为 DBadd_foreign_key，参数包括表名、主键列索引和外键约束信息。在这里，外键约束信息为一个 ZBX_FIELD 结构体变量，包含了外键的相关信息，如字段名、类型、标签、键名等。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010035 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_3010035(void)
{
    // 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下内容：
    // 字段名：r_eventid
    // 字段类型：NULL（未知类型）
    // 字段标签：events
    // 字段键名：eventid
    // 外键约束：0（无约束）
    // 级联删除：0（不级联删除）
    // 其它未知参数：0
    const ZBX_FIELD field = {"r_eventid", NULL, "events", "eventid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

    // 调用 DBadd_foreign_key 函数，向 "problem" 表中添加外键约束
    // 参数1：要添加外键的表名："problem"
    // 参数2：主键列索引：2（假设主键名为 id，索引从0开始）
    // 参数3：外键约束信息：&field（指向上面定义的 ZBX_FIELD 结构体变量）
    return DBadd_foreign_key("problem", 2, &field);
}

 ******************************************************************************/
// 定义一个名为 DBpatch_3010034 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010034(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "problem_2" 的索引
    // 索引基于 "problem" 表，用于存储 "r_clock" 字段的值
    // 参数1：索引名，这里是 "problem_2"
    // 参数2：基于的表名，这里是 "problem"
    // 参数3：索引字段名，这里是 "r_clock"
    // 参数4：索引类型，这里是升序索引，值为 0

    // 函数返回值：创建索引的返回值，通常为 0 表示成功，非 0 表示失败
    return DBcreate_index("problem", "problem_2", "r_clock", 0);
}


static int	DBpatch_3010035(void)
{
	const ZBX_FIELD	field = {"r_eventid", NULL, "events", "eventid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("problem", 2, &field);
}

static int	DBpatch_3010036(void)
{
	const ZBX_TABLE table =
			{"problem_tag", "problemtagid", 0,
				{
					{"problemtagid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"problem_tag_1\" 的索引，基于字段 \"problem_tag\"，索引类型为 \"eventid\"，不包含前导 wildcard。如果创建成功，函数返回值为 0，否则返回错误码。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010037 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_3010037(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "problem_tag_1" 的索引
    // 索引基于的字段名为 "problem_tag"，索引类型为 "eventid"，不包含前导 wildcard
    // 函数返回值为创建索引的返回值，若创建成功则为 0，否则为错误码
    return DBcreate_index("problem_tag", "problem_tag_1", "eventid", 0);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"problem_tag_2\" 的索引，该索引包含两个列：tag 和 value。创建索引的过程使用 DBcreate_index 函数来实现。如果创建索引成功，函数返回 0，否则返回非 0 值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010038 的静态函数，该函数不接受任何参数，即 void 类型
/******************************************************************************
 * *
 *整个代码块的主要目的是向数据库中添加一个名为 \"problem_tag\" 的外键约束。该外键约束关联到主表（参数1），字段包括事件ID、问题名称、事件ID（用于创建外键约束）。当主表中的记录被删除时，级联删除关联表中的记录。最后，通过返回值判断添加外键约束的操作是否成功。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010039 的静态函数，该函数为空返回类型为整数的函数
static int DBpatch_3010039(void)
{
    // 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下字段：
    // eventid：事件ID
    // NULL：下一个字段指针（此处为空）
    // problem：问题名称
    // eventid：事件ID（与上面的事件ID重复，用于创建外键约束）
    // 0：字段类型，此处为0表示常规字段
    // 0：字段长度，此处为0表示自动计算长度
    // 0：字段精度，此处为0表示不需要精度
    // ZBX_FK_CASCADE_DELETE：删除规则，此处表示级联删除

    // 调用 DBadd_foreign_key 函数，向数据库中添加一个名为 "problem_tag"
    // 的外键约束，参数1表示主表，&field表示关联字段变量

    // 返回 DBadd_foreign_key 函数的执行结果，若添加成功则返回0，否则返回错误码
    return DBadd_foreign_key("problem_tag", 1, &field);
}

    // 返回值：创建索引的返回状态，0 表示成功，非 0 表示失败
/******************************************************************************
 * *
 *这块代码的主要目的是更新数据库中的 config 表中的 ok_period 字段，将其值设置为一天（SEC_PER_DAY 秒）。具体步骤如下：
 *
 *1. 定义一个名为 DBpatch_3010042 的静态函数，不接受任何参数，返回一个整型数据。
 *2. 使用 DBexecute 函数执行更新操作，将 config 表中的 ok_period 字段设置为 SEC_PER_DAY 秒，同时设置条件：ok_period 大于当前值。
 *3. 判断数据库操作是否成功，如果成功（ZBX_DB_OK 及以上），则返回成功（SUCCEED）；否则，返回失败（FAIL）。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010042 的静态函数，该函数不接受任何参数，返回一个整型数据
static int	DBpatch_3010042(void)
{
	// 定义两个常量：ZBX_DB_OK 表示数据库操作成功的最小值，SEC_PER_DAY 表示一天的时间秒数
	if (ZBX_DB_OK <= DBexecute("update config set ok_period=%d where ok_period>%d", SEC_PER_DAY, SEC_PER_DAY))
		// 如果数据库操作成功（ZBX_DB_OK 及以上），返回成功（SUCCEED）
		return SUCCEED;

	// 否则，返回失败（FAIL）
	return FAIL;
}

static int	DBpatch_3010039(void)
{
	const ZBX_FIELD	field = {"eventid", NULL, "problem", "eventid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("problem_tag", 1, &field);
}

static int	DBpatch_3010042(void)
{
	if (ZBX_DB_OK <= DBexecute("update config set ok_period=%d where ok_period>%d", SEC_PER_DAY, SEC_PER_DAY))
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：更新 config 表中 blink_period 字段的数据，将其值从大于一天的时间片数更新为一天的时间片数。如果更新操作成功，返回 SUCCEED，否则返回 FAIL。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010043 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_3010043(void)
{
	// 定义两个常量，分别为 SEC_PER_DAY 和 BLINK_PERIOD，表示一天的时间片数和闪烁周期
	const int SEC_PER_DAY = 24 * 60 * 60; // 24 小时 * 60 分钟 * 60 秒 = 86400 秒
	const int BLINK_PERIOD = 10;          // 闪烁周期，此处假设为 10 秒

	// 使用 DBexecute 函数执行一条 SQL 语句，更新 config 表中的 blink_period 字段
	// 更新规则：如果当前 blink_period 大于 SEC_PER_DAY，则将其更新为 SEC_PER_DAY
	if (ZBX_DB_OK <= DBexecute("update config set blink_period=%d where blink_period>%d", SEC_PER_DAY, SEC_PER_DAY))
		// 如果执行成功，返回 SUCCEED（表示成功）
		return SUCCEED;
	else
		// 否则，返回 FAIL（表示失败）
		return FAIL;
}


/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"problem\" 的表中添加一行数据，该数据为一个 ZBX_FIELD 结构体。具体来说，代码实现了以下功能：
 *
 *1. 定义一个名为 field 的 ZBX_FIELD 结构体变量，并初始化其成员变量。
 *2. 调用 DBadd_field 函数，将 field 结构体中的数据添加到名为 \"problem\" 的表中。
 *3. 返回 DBadd_field 函数的执行结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010044 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010044(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"correlationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};

	// 调用 DBadd_field 函数，将 field 结构体中的数据添加到数据库中，参数1为表名（这里是 "problem"），参数2为要添加的列数据（这里是 field 结构体）
	return DBadd_field("problem", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"event_recovery\" 的数据库表中添加一个字段。具体来说，代码首先定义了一个 ZBX_FIELD 结构体变量 field，其中包含了要添加的字段信息。然后，通过调用 DBadd_field 函数将这个字段添加到 \"event_recovery\" 表中。最后，返回 DBadd_field 函数的返回值，表示添加操作是否成功。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010045 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010045(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，其中包含了一些字段信息
	const ZBX_FIELD	field = {"c_eventid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};

	// 调用 DBadd_field 函数，将定义好的 field 结构体变量添加到数据库中的 "event_recovery" 表中
	return DBadd_field("event_recovery", &field);
}

/******************************************************************************
 * *
 *```c
 *static int\tDBpatch_3010047(void)
 *{
/******************************************************************************
 * *
 *整个代码块的主要目的是在 \"event_recovery\" 表中添加一条外键约束，约束的列索引为3，对应的字段名为 \"c_eventid\"。如果添加外键约束失败，输出错误信息。添加外键约束成功后，可以继续执行其他操作。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010048 的静态函数，该函数不接受任何参数，返回值为整型
static int DBpatch_3010048(void)
{
    // 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
    const ZBX_FIELD field = {"c_eventid", NULL, "events", "eventid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

    // 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
    // 参数1：要添加外键约束的表名，这里是 "event_recovery"
    // 参数2：外键约束的列索引，这里是 3
    // 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
    // 函数返回值：操作结果，0表示成功，非0表示失败

    // 判断返回值是否为0，如果不为0，说明添加外键约束失败，需要进行错误处理
    if (0 != DBadd_foreign_key("event_recovery", 3, &field))
    {
        // 这里可以根据实际情况进行错误处理，例如输出错误信息、记录日志等
        printf("添加外键约束失败：%s\
", zbx_strerror(errno));
    }

    // 添加外键约束成功，此处可以进行其他操作，例如更新数据、插入数据等

    return 0; // 函数执行成功，返回0
}

 *        return 0;
 *    }
 *    else
 *    {
 *        return -1;
 *    }
 *}
 *
 *// 整个代码块的主要目的是创建一个名为 \"event_recovery_2\" 的索引，基于 \"event_recovery\" 表，索引列名为 \"c_eventid\"，并判断创建索引操作是否成功。若成功，返回 0，否则返回 -1。
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_3010047 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_3010047(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "event_recovery_2" 的索引
    // 索引基于的表名为 "event_recovery"
    // 索引列名为 "c_eventid"
    // 设置索引的属性，此处为 0，表示不设置特殊属性
}

// 整个代码块的主要目的是创建一个名为 "event_recovery_2" 的索引，基于 "event_recovery" 表，索引列名为 "c_eventid"，并设置索引属性为 0。

 *
 *注释详细解释如下：
 *
 *1. 定义一个名为 DBpatch_3010046 的静态函数，该函数不接受任何参数，返回类型为 int。
 *2. 定义一个名为 field 的 ZBX_FIELD 结构体变量，其中包含以下字段：
 *   - 字段名：correlationid
 *   - 字段值：NULL
 *   - 数据类型：ZBX_TYPE_ID
 *   - 长度：0
 *   - 是否允许为空：否
 *   - 是否主键：否
 *3. 调用 DBadd_field 函数，将 field 结构体中的数据添加到数据库中的 \"event_recovery\" 表中。函数返回值为添加字段的索引。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010046 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010046(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"correlationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};

	// 调用 DBadd_field 函数，将 field 结构体中的数据添加到数据库中的 "event_recovery" 表中
	return DBadd_field("event_recovery", &field);
}


static int	DBpatch_3010047(void)
{
	return DBcreate_index("event_recovery", "event_recovery_2", "c_eventid", 0);
}

static int	DBpatch_3010048(void)
{
	const ZBX_FIELD	field = {"c_eventid", NULL, "events", "eventid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("event_recovery", 3, &field);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"correlation\" 的表格。该表格包含以下列：
 *
 *1. correlationid：唯一标识列，类型为 ZBX_TYPE_ID，非空，主键。
 *2. name：名称列，类型为 ZBX_TYPE_CHAR，非空。
 *3. description：描述列，类型为 ZBX_TYPE_SHORTTEXT，非空。
 *4. evaltype：评估类型列，类型为 ZBX_TYPE_INT，非空，默认值为 0。
 *5. status：状态列，类型为 ZBX_TYPE_INT，非空，默认值为 0。
 *6. formula：公式列，类型为 ZBX_TYPE_CHAR，非空。
 *
 *通过调用 DBcreate_table 函数，根据 table 结构创建该表格。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010049 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010049(void)
{
	// 定义一个常量 ZBX_TABLE 类型变量 table，用于存储表格结构信息
	const ZBX_TABLE table =
			{"correlation", "correlationid", 0,
				// 定义一个结构体数组，用于存储表格的列信息
				{
					{"correlationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"name", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"description", "", NULL, NULL, 255, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0},
					{"evaltype", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"status", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"formula", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				// 结束表格定义
				NULL
			};

	// 调用 DBcreate_table 函数，根据 table 创建表格，并返回创建结果
	return DBcreate_table(&table);
}


/******************************************************************************
 * *
 *这块代码的主要目的是创建一个名为 \"correlation_1\" 的索引，该索引依据的字段为 \"correlation\" 和 \"status\"。函数 DBpatch_3010050 是一个静态函数，不需要传入任何参数，直接调用 DBcreate_index 函数来完成索引的创建。在创建索引时，设置了索引的属性为 0。最后，函数返回一个整型值，表示创建索引的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010050 的静态函数，该函数不接受任何参数，返回一个整型值
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"correlation_2\" 的索引，该索引与名为 \"correlation\" 的表相关联，索引字段为 \"name\"，索引类型为1。最后返回创建索引的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010051 的静态函数，该函数不接受任何参数，返回类型为 int
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"corr_condition\" 的表。该表包含以下字段：
 *
 *1. corr_conditionid：类型为 ZBX_TYPE_ID，非空。
 *2. correlationid：类型为 ZBX_TYPE_ID，非空。
 *3. type：类型为 ZBX_TYPE_INT，非空，默认值为 0。
 *
 *通过调用 DBcreate_table 函数，将 table 结构体中的信息传递给函数，以创建所需的表。创建表的成功与否取决于 DBcreate_table 函数的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010052 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010052(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包括以下字段：
			{"corr_condition", "corr_conditionid", 0,
				{
					// 添加一个字段：corr_conditionid，非空，类型为 ZBX_TYPE_ID
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},

					// 添加一个字段：correlationid，非空，类型为 ZBX_TYPE_ID
					{"correlationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},

					// 添加一个字段：type，非空，类型为 ZBX_TYPE_INT
					{"type", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},

					// 添加一个空字段，用于结束数组
					{0}
				},
				// 结束 ZBX_TABLE 结构体的初始化
				NULL
			};

	// 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
	return DBcreate_table(&table);
}



static int	DBpatch_3010051(void)
{
	return DBcreate_index("correlation", "correlation_2", "name", 1);
}

static int	DBpatch_3010052(void)
{
	const ZBX_TABLE table =
			{"corr_condition", "corr_conditionid", 0,
				{
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"correlationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"type", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *下面是已经注释好的完整代码块：
 *
 *```c
 */**
 * * @file
 * * @brief 该文件为实现创建索引的相关功能
 * */
 *
 *#include <stdio.h>
 *
 *// 定义一个名为 DBpatch_3010053 的静态函数，该函数为空返回类型为 int
 *static int\tDBpatch_3010053(void)
 *{
 *    // 调用 DBcreate_index 函数，用于创建一个名为 \"corr_condition_1\" 的索引
 *    // 参数1：索引名称，这里是 \"corr_condition\"
 *    // 参数2：索引名称的前缀，这里是 \"corr_condition_1\"
 *    // 参数3：索引关联的列名，这里是 \"correlationid\"
 *    // 参数4：索引的属性，这里是 0，表示不设置特殊属性
 *    return DBcreate_index(\"corr_condition\", \"corr_condition_1\", \"correlationid\", 0);
 *}
 *
 *int main()
 *{
 *    int ret = DBpatch_3010053();
 *    printf(\"创建索引结果：%d\
 *\", ret);
 *    return 0;
 *}
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_3010053 的静态函数，该函数为空返回类型为 int
static int	DBpatch_3010053(void)
{
    // 调用 DBcreate_index 函数，用于创建一个名为 "corr_condition_1" 的索引
    // 参数1：索引名称，这里是 "corr_condition"
    // 参数2：索引名称的前缀，这里是 "corr_condition_1"
    // 参数3：索引关联的列名，这里是 "correlationid"
    // 参数4：索引的属性，这里是 0，表示不设置特殊属性
}

// 整个代码块的主要目的是创建一个名为 "corr_condition_1" 的索引，用于关联 "correlationid" 列


/******************************************************************************
 * *
 *整个代码块的主要目的是向数据库表 \"corr_condition\" 中添加一条外键约束。代码通过定义一个 ZBX_FIELD 结构体变量来描述外键约束的字段信息，然后调用 DBadd_foreign_key 函数将这条外键约束添加到数据库中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010054 的静态函数，该函数为空返回类型为整型的函数
static int	DBpatch_3010054(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下字段：
	// correlationid：关联ID
	// NULL：下一个字段指针，此处为空
	// correlation：关联名称
	// correlationid：关联ID，与上一个字段重复，此处用于区分不同关联
	// 0：字段类型，此处为0表示普通字段
	// 0：字段长度，此处为0表示自动计算长度
	// 0：字段小数位，此处为0表示整数类型
	// ZBX_FK_CASCADE_DELETE：外键约束类型，表示级联删除

	// 初始化 field 结构体变量
	field.correlationid = "correlationid";
	field.next = NULL;
	field.name = "correlation";
	field.type = 0;
	field.length = 0;
	field.decimals = 0;
	field.fk_cascade_delete = ZBX_FK_CASCADE_DELETE;

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：表名，此处为 "corr_condition"
	// 参数2：主键序号，此处为 1
	// 参数3：指向 ZBX_FIELD 结构体的指针，此处为 &field

	return DBadd_foreign_key("corr_condition", 1, &field);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `corr_condition_tag` 的表，表中包含两个字段：`corr_conditionid` 和 `tag`。`corr_conditionid` 字段为整型，非空；`tag` 字段为字符型，最大长度为 255，非空。创建表的操作通过调用 `DBcreate_table` 函数完成。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010055 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010055(void)
{
	// 定义一个常量 ZBX_TABLE 类型的变量 table，用于存储表结构信息
	const ZBX_TABLE table =
			// 初始化 table 结构体，包括以下字段：
			{"corr_condition_tag", "corr_conditionid", 0,
				// 定义表的字段信息，包括字段名、类型、长度、是否非空等属性
				{
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"corr_condition_tag\" 的表中添加一条外键约束。具体来说，代码实现了以下操作：
 *
 *1. 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息，包括关联表名、主键列名、外键列名、是否非空、是否唯一、是否自增以及删除规则等。
 *2. 调用 DBadd_foreign_key 函数，传入表名、主键列名和 ZBX_FIELD 结构体指针，以添加外键约束。
 *3. 判断 DBadd_foreign_key 函数的返回值，如果为0，表示添加外键约束成功；否则，表示添加失败。
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"corr_condition_group\" 的表。该表包含四个字段：corr_conditionid、operator、groupid 和一个未命名字段。表的结构通过 ZBX_TABLE 结构体进行定义。最后，调用 DBcreate_table 函数来创建表。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010057 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010057(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包含以下字段：
			{"corr_condition_group", "corr_conditionid", 0,
				{
					// 定义第一个字段：corr_conditionid，非空，类型为 ZBX_TYPE_ID
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 定义第二个字段：operator，值为 "0"，类型为 ZBX_TYPE_INT
					{"operator", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					// 定义第三个字段：groupid，非空，类型为 ZBX_TYPE_ID
					{"groupid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 定义最后一个字段，序号为 4，类型为 ZBX_TYPE_ID，后面跟一个空字段表示字段结束
					{0}
				},
				// 表名，这里为 NULL
				NULL
			};

	// 调用 DBcreate_table 函数，传入 table 参数，返回创建表的结果
	return DBcreate_table(&table);
}

	return DBcreate_table(&table);
}


static int	DBpatch_3010056(void)
{
	const ZBX_FIELD	field = {"corr_conditionid", NULL, "corr_condition", "corr_conditionid", 0, 0, 0,
			ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("corr_condition_tag", 1, &field);
}

static int	DBpatch_3010057(void)
{
	const ZBX_TABLE table =
			{"corr_condition_group", "corr_conditionid", 0,
				{
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"operator", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"groupid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"corr_condition_group_1\" 的索引，该索引关联的字段为 \"groupid\"，并且该索引不是唯一的。函数 DBpatch_3010058 调用 DBcreate_index 函数来实现这一目的，并返回新创建的索引标识符。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010058 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010058(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "corr_condition_group_1" 的索引
    // 索引的相关信息如下：
    // 索引名称： "corr_condition_group"
    // 索引字段： "groupid"
    // 是否唯一： 否（0表示非唯一）

    // 返回 DBcreate_index 函数的执行结果，即新创建的索引的标识符
    return DBcreate_index("corr_condition_group", "corr_condition_group_1", "groupid", 0);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：向名为 \"corr_condition_group\" 的表中添加一条外键约束。代码中定义了一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束的信息。然后调用 DBadd_foreign_key 函数，将 field 中的信息添加到数据库中。最后返回添加外键约束的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010059 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_3010059(void)
{
	// 定义一个名为 field 的常量 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"corr_conditionid", NULL, "corr_condition", "corr_conditionid", 0, 0, 0,
			// 设置 field 的属性，包括：
			ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：要添加外键的表名，这里是 "corr_condition_group"
	// 参数2：主键列名，这里是 "1"
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `corr_condition_tagpair` 的表，表中包含三个字段：`corr_conditionid`、`oldtag` 和 `newtag`。表的结构定义如下：
 *
 *- `corr_conditionid`：字段类型为 ZBX_TYPE_ID，非空，无默认值。
 *- `oldtag`：字段类型为 ZBX_TYPE_CHAR，非空，最大长度为 255。
 *- `newtag`：字段类型为 ZBX_TYPE_CHAR，非空，最大长度为 255。
 *
 *创建表的操作通过调用 `DBcreate_table` 函数完成。如果创建表成功，函数返回 0，否则返回 -1。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010061 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010061(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包括以下字段：
			{"corr_condition_tagpair", "corr_conditionid", 0,
				// 字段定义，包括字段名、类型、是否非空、默认值等
				{
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"oldtag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"newtag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"corr_condition_tagvalue\" 的数据库表。该表包含以下四个字段：corr_conditionid（类型为 ZBX_TYPE_ID）、tag（类型为 ZBX_TYPE_CHAR）、operator（类型为 ZBX_TYPE_INT）和 value（类型为 ZBX_TYPE_CHAR）。在创建表的过程中，还对这四个字段设置了非空约束。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010063 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010063(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包括以下字段：
			{"corr_condition_tagvalue", "corr_conditionid", 0,
				// 定义一个包含多个字段的结构体变量
				{
					// 定义第一个字段：corr_conditionid，非空，类型为 ZBX_TYPE_ID
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 定义第二个字段：tag，非空，类型为 ZBX_TYPE_CHAR
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					// 定义第三个字段：operator，非空，类型为 ZBX_TYPE_INT
					{"operator", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					// 定义第四个字段：value，非空，类型为 ZBX_TYPE_CHAR
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					// 定义最后一个字段，用空字符串表示结束
					{0}
				},
				// 结束 ZBX_TABLE 结构体
				NULL
			};

	// 调用 DBcreate_table 函数，传入 table 参数，并将返回值赋给 result
	return DBcreate_table(&table);
}



static int	DBpatch_3010061(void)
{
	const ZBX_TABLE table =
			{"corr_condition_tagpair", "corr_conditionid", 0,
				{
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"oldtag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"newtag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *这块代码的主要目的是向 \"corr_condition_tagpair\" 表中添加一个外键约束。具体来说，代码首先定义了一个 ZBX_FIELD 结构体变量 field，用于存储外键约束的信息。然后，调用 DBadd_foreign_key 函数将这个外键约束添加到 \"corr_condition_tagpair\" 表中。
 *
 *输出：
 *
 *```
 *static int DBpatch_3010062(void)
 *{
 *    // 定义一个名为 field 的 ZBX_FIELD 结构体变量
 *    const ZBX_FIELD\tfield = {\"corr_conditionid\", NULL, \"corr_condition\", \"corr_conditionid\", 0, 0, 0,
 *                            ZBX_FK_CASCADE_DELETE};
 *
 *    // 调用 DBadd_foreign_key 函数，向 \"corr_condition_tagpair\" 表中添加外键约束
 *    // 参数1：要添加外键的表名
 *    // 参数2：主键列索引，这里是 1
 *    // 参数3：指向 ZBX_FIELD 结构体的指针， containing the foreign key information
 *    return DBadd_foreign_key(\"corr_condition_tagpair\", 1, &field);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_3010062 的静态函数
static int	DBpatch_3010062(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"corr_conditionid", NULL, "corr_condition", "corr_conditionid", 0, 0, 0,
					ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向 "corr_condition_tagpair" 表中添加外键约束
	// 参数1：要添加外键的表名
	// 参数2：主键列索引，这里是 1
	// 参数3：指向 ZBX_FIELD 结构体的指针， containing the foreign key information
	return DBadd_foreign_key("corr_condition_tagpair", 1, &field);
}


static int	DBpatch_3010063(void)
{
	const ZBX_TABLE table =
			{"corr_condition_tagvalue", "corr_conditionid", 0,
				{
					{"corr_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"operator", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"corr_condition_tagvalue\" 的表中添加一条外键约束。具体来说，代码定义了一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息，然后调用 DBadd_foreign_key 函数将这条外键约束添加到数据库中。如果添加成功，函数返回0，否则返回非0值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010064 的静态函数，该函数不接受任何参数，返回一个整型值
static int DBpatch_3010064(void)
{
    // 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"corr_operation\" 的表，包含三个字段：corr_operationid、correlationid 和 type。其中，corr_operationid 和 correlationid 字段是非空的字段，type 字段是非空的整数类型字段，默认值为 \"0\"。创建表的操作通过调用 DBcreate_table 函数来实现。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010065 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010065(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体，包含以下字段：
			{"corr_operation", "corr_operationid", 0,
				{
					// 初始化第一个字段，名称为 "corr_operationid"，类型为 ZBX_TYPE_ID，非空，无默认值
					{"corr_operationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 初始化第二个字段，名称为 "correlationid"，类型为 ZBX_TYPE_ID，非空，无默认值
					{"correlationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 初始化第三个字段，名称为 "type"，类型为 ZBX_TYPE_INT，非空，默认值为 "0"
					{"type", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					// 初始化一个空字段，用于后续扩展
					{0}
				},
				// 结束 ZBX_TABLE 结构体的初始化
				NULL
			};

	// 调用 DBcreate_table 函数，传入初始化好的 table 结构体，返回创建表的结果
	return DBcreate_table(&table);
}

			{"corr_operation", "corr_operationid", 0,
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"corr_operation\" 的索引，索引文件名为 \"corr_operation_1\"，唯一标识列名为 \"correlationid\"。函数 DBpatch_3010066 调用 DBcreate_index 函数来完成这个任务，并返回创建索引的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010066 的静态函数，该函数不接受任何参数，返回一个整型值
static int DBpatch_3010066(void)
{
    // 调用 DBcreate_index 函数，创建一个名为 "corr_operation" 的索引
    // 参数1：索引名称："corr_operation"
    // 参数2：索引文件名："corr_operation_1"
    // 参数3：唯一标识列名："correlationid"
    // 参数4：索引类型，此处为普通索引，值为 0

    // 返回 DBcreate_index 函数的执行结果，即创建索引的返回值
    return DBcreate_index("corr_operation", "corr_operation_1", "correlationid", 0);
}

					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

static int	DBpatch_3010066(void)
{
	return DBcreate_index("corr_operation", "corr_operation_1", "correlationid", 0);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是向名为 \"corr_operation\" 的表中添加一条外键约束，约束的字段名为 \"correlationid\"。当添加外键约束成功时，输出一条日志表示成功添加；若失败，则输出一条日志表示添加失败。函数执行成功时，返回0。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010067 的静态函数，该函数不接受任何参数，即 void 类型
static int DBpatch_3010067(void)
{
    // 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
    const ZBX_FIELD field = {"correlationid", NULL, "correlation", "correlationid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

    // 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
    // 参数1：表名，这里是 "corr_operation"
    // 参数2：主键列名，这里是 "1"
    // 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
    // 函数返回值：操作结果，0表示成功，非0表示失败

    // 这里假设 DBadd_foreign_key 函数返回0，表示添加外键约束成功
    if (0 == DBadd_foreign_key("corr_operation", 1, &field)) {
        // 输出一条日志，表示成功添加了外键约束
        printf("成功添加了外键约束：表名=%s，主键列名=%s，外键列名=%s\
", "corr_operation", "1", "correlationid");
    } else {
        // 输出一条日志，表示添加外键约束失败
        printf("添加外键约束失败：表名=%s，主键列名=%s，外键列名=%s\
", "corr_operation", "1", "correlationid");
    }

    // 返回0，表示函数执行成功
    return 0;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据库中满足特定条件的触发器状态。具体来说，这段代码执行以下操作：
 *
 *1. 定义两个常量，分别为触发器正常状态（0）和发现原型标志（2）。
 *2. 执行更新触发器的 SQL 语句，将错误信息清空，并将状态更新为正常状态（0）。
 *3. 判断更新操作是否成功，如果成功，则返回 SUCCEED（0），否则返回 FAIL（-1）。
 ******************************************************************************/
/*
 * 定义一个名为 DBpatch_3010068 的静态函数，该函数用于更新数据库中的触发器状态
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 \"task\" 的表，表中包含一个名为 \"taskid\" 的主键字段（类型为 ZBX_TYPE_ID，非空），以及一个名为 \"type\" 的字段（类型为 ZBX_TYPE_INT，非空）。最后调用 DBcreate_table 函数来创建这个表。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010069 的静态函数，该函数为 void 类型（无返回值）
static int DBpatch_3010069(void)
{
    // 定义一个名为 table 的 ZBX_TABLE 结构体变量
    const ZBX_TABLE table =
    {
        // 表名称为 "task"
        "task",
        // 表主键名为 "taskid"，非空，类型为 ZBX_TYPE_ID
        "taskid",
        // 表的其他字段，暂时未定义，默认为 0
        0,
        // 字段定义列表，包含一个字段
        {
            // 字段名称为 "taskid"，非空，类型为 ZBX_TYPE_ID，不允许为空
            {"taskid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
            // 字段名称为 "type"，非空，类型为 ZBX_TYPE_INT，不允许为空
            {"type", NULL, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
            // 字段定义结束，使用 {} 表示
            {0}
        },
        // 表结构体结束，使用 NULL 表示
        NULL
    };

    // 调用 DBcreate_table 函数，传入 table 作为参数，返回创建表的结果
    return DBcreate_table(&table);
}

}


static int	DBpatch_3010069(void)
{
	const ZBX_TABLE table =
			{"task", "taskid", 0,
				{
					{"taskid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"type", NULL, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为 `task_close_problem` 的表，其中包含两个字段：`taskid` 和 `acknowledgeid`。表的结构通过 `ZBX_TABLE` 结构体定义，并使用 `DBcreate_table` 函数创建。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010070 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010070(void)
{
	// 定义一个名为 table 的常量 ZBX_TABLE 结构体变量
	const ZBX_TABLE table =
			// 初始化一个 ZBX_TABLE 结构体变量，包含以下字段：
			{"task_close_problem", "taskid", 0,
				// 字段定义，包括任务ID（taskid），不为空，类型为ZBX_TYPE_ID
				{
					{"taskid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 字段定义，包括确认ID（acknowledgeid），不为空，类型为ZBX_TYPE_ID
					{"acknowledgeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					// 字段定义结束，使用花括号括起来
					{0}
				},
				// 表结构定义结束，使用 NULL 结尾
				NULL
			};

	// 调用 DBcreate_table 函数，传入 table 参数，返回创建表的结果
	return DBcreate_table(&table);
}


/******************************************************************************
 * *
 *输出整个注释好的代码块：
 *
 *```c
 *static int\tDBpatch_3010071(void)
 *{
 *\t// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
 *\tconst ZBX_FIELD\tfield = {\"taskid\", NULL, \"task\", \"taskid\", 0, 0, 0, ZBX_FK_CASCADE_DELETE};
 *
 *\t// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
 *\t// 参数1：要添加外键约束的表名，这里是 \"task_close_problem\"
 *\t// 参数2：主键列名，这里是 \"1\"
 *\t// 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
 *\treturn DBadd_foreign_key(\"task_close_problem\", 1, &field);
 *}
 *
 *// 整个代码块的主要目的是向 \"task_close_problem\" 表中添加一条外键约束，约束的列名为 \"taskid\"，对应的数据库表名为 \"task\"，约束类型为 ZBX_FK_CASCADE_DELETE，即级联删除。
 *```
 ******************************************************************************/
// 定义一个名为 DBpatch_3010071 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_3010071(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量，用于存储外键约束信息
	const ZBX_FIELD	field = {"taskid", NULL, "task", "taskid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	// 调用 DBadd_foreign_key 函数，向数据库中添加一条外键约束
	// 参数1：要添加外键约束的表名，这里是 "task_close_problem"
	// 参数2：主键列名，这里是 "1"
	// 参数3：指向 ZBX_FIELD 结构体的指针，这里是 &field
	return DBadd_foreign_key("task_close_problem", 1, &field);
}

// 整个代码块的主要目的是向 "task_close_problem" 表中添加一条外键约束，约束的列名为 "taskid"，对应的数据库表名为 "task"，约束类型为 ZBX_FK_CASCADE_DELETE，即级联删除。


/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"acknowledges\" 的数据库表中添加一个新字段。具体来说，代码首先定义了一个 ZBX_FIELD 结构体变量 `field`，其中包含字段的名称为 \"action\"，字段类型为整型（ZBX_TYPE_INT），非空（ZBX_NOTNULL），其他参数为默认值。接着，调用 DBadd_field 函数将这个字段添加到名为 \"acknowledges\" 的数据库表中。最后，返回 DBadd_field 函数的返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010072 的静态函数，该函数为空函数
static int DBpatch_3010072(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD field = {"action", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	// 调用 DBadd_field 函数，将 field 结构体中的数据添加到数据库中，参数1为表名（"acknowledges"），参数2为指向 ZBX_FIELD 结构体的指针
	return DBadd_field("acknowledges", &field);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 DBpatch_3010073 的静态函数，该函数用于向名为 \"triggers\" 的数据库表中添加一个整型字段（字段名为 \"manual_close\"，字段值为 \"0\"），并确保字段非空。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010073 的静态函数，该函数为空函数
static int DBpatch_3010073(void)
{
    // 定义一个名为 field 的 ZBX_FIELD 结构体变量，包含以下内容：
    // 字段名：manual_close
    // 字段值：0
    // 字段类型：ZBX_TYPE_INT（整型）
    // 是否非空：ZBX_NOTNULL（非空）
    // 其他未知参数：0
    const ZBX_FIELD field = {"manual_close", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

    // 调用 DBadd_field 函数，将定义好的 field 结构体添加到名为 "triggers" 的数据库表中
    return DBadd_field("triggers", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是向 \"event_recovery\" 数据库表中添加一个名为 \"userid\" 的字段。具体来说，代码首先定义了一个 ZBX_FIELD 结构体变量 field，其中包含了字段的名称、类型等信息。然后，通过调用 DBadd_field 函数，将这个 field 结构体变量添加到 \"event_recovery\" 数据库表中。最后，返回 DBadd_field 函数的执行结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010074 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010074(void)
{
	// 定义一个名为 field 的 ZBX_FIELD 结构体变量
	const ZBX_FIELD	field = {"userid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};

	// 调用 DBadd_field 函数，将定义的 field 结构体变量添加到 "event_recovery" 数据库表中
	return DBadd_field("event_recovery", &field);
}


/******************************************************************************
 * *
 *这块代码的主要目的是向名为 \"problem\" 的数据库表中添加一个字段。这里的 ZBX_FIELD 结构体用于表示要添加的字段信息，包括字段名、类型、以及其他相关属性。通过调用 DBadd_field 函数，将这个字段添加到数据库中。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010075 的静态函数，该函数不接受任何参数，即 void 类型
static int	DBpatch_3010075(void)
{
/******************************************************************************
 * *
 *这块代码的主要目的是删除符合条件的索引数据。函数 DBpatch_3010076 接受一个空指针作为参数，并返回一个整数值。在函数内部，首先定义了一个常量字符指针 sql，用于存储要执行的 SQL 语句。接着，列举了要删除的索引值，并将它们用单引号括起来。然后，使用 DBexecute 函数执行 SQL 语句，判断执行结果。如果执行成功，返回 0（即 SUCCEED），否则返回 -1（即 FAIL）。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010076 的静态函数，该函数不接受任何参数，返回类型为 int
static int	DBpatch_3010076(void)
{
	// 定义一个常量字符指针 sql，存储 SQL 语句
	const char	*sql = "delete from profiles where idx in (";

	// 列举要删除的索引值，用单引号括起来
	sql += "'web.events.discovery.period',";
	sql += "'web.events.filter.state',";
	sql += "'web.events.filter.triggerid',";
	sql += "'web.events.source',";
	sql += "'web.events.timelinefixed',";
	sql += "'web.events.trigger.period'";
	sql += ")";

	// 使用 DBexecute 函数执行 SQL 语句，判断执行结果
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		// 如果执行成功，返回 SUCCEED（即 0）
		return SUCCEED;

	// 如果执行失败，返回 FAIL（即 -1）
	return FAIL;
}

			"'web.events.trigger.period'"
		")";

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 * *
 *这块代码的主要目的是修改名为 \"groups\" 的字段的类型。具体来说，它定义了一个 ZBX_FIELD 结构体变量 `field`，其中包含字段名、字段类型、是否非空等属性。然后调用 DBmodify_field_type 函数，将 \"groups\" 字段的类型进行修改。需要注意的是，这个函数没有返回值。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010077 的静态函数，该函数为空，即没有返回值
static int	DBpatch_3010077(void)
/******************************************************************************
 * *
 *整个代码块的主要目的是对 problem 表中的数据进行批量更新，更新条件是 clock=0。具体操作如下：
 *
 *1. 分配一个新的 sql 字符串，并将其指针赋给 sql。
 *2. 遍历查询结果中的每一行数据。
 *3. 对于每一行数据，构造一个新的 sql 语句，更新 problem 表中的 clock 和 ns 字段。
 *4. 执行更新操作，如果执行失败，跳转到 out 标签处。
 *5. 结束多个数据库更新操作。
 *6. 如果 sql 字符串的长度大于 16，则执行一条 SQL 语句，如果执行失败，跳转到 out 标签处。
 *7. 更新 ret 变量的值为 SUCCEED，表示操作成功。
 *8. 释放 sql 字符串占用的内存。
 *9. 返回 ret 变量的值，表示整个操作的结果。
 ******************************************************************************/
// 定义一个名为 DBpatch_3010079 的静态函数，该函数不接受任何参数，返回类型为 int
static int DBpatch_3010079(void)
{
	// 定义一个 DB_ROW 类型的变量 row，用于存储数据库查询结果的一行数据
	DB_ROW row;

	// 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果
	DB_RESULT result;

	// 定义一个整型变量 ret，初始值为 FAIL
	int ret = FAIL;

	// 定义一个字符串指针 sql，初始值为 NULL
	char *sql = NULL;

	// 定义一个大小为 0 的 size_t 类型变量 sql_alloc，用于存储 sql 字符串的长度
	size_t sql_alloc = 0;

	// 定义一个 size_t 类型变量 sql_offset，用于存储 sql 字符串的偏移量
	size_t sql_offset = 0;

	// 开始执行多个数据库更新操作，并将 sql、sql_alloc 和 sql_offset 传入
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 执行一个数据库查询操作，查询条件如下：
	// 从 problem 表中选取 eventid、clock 和 ns 字段
	// 同时，条件是 problem.clock=0 和 problem.eventid=events.eventid
	result = DBselect("select p.eventid,e.clock,e.ns"
			" from problem p,events e"
			" where p.eventid=e.eventid"
				" and p.clock=0");

	// 遍历查询结果中的每一行数据
	while (NULL != (row = DBfetch(result)))
	{
		// 分配一个新的 sql 字符串，并将其指针赋给 sql
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update problem set clock=%s,ns=%s where eventid=%s;\
",
				row[1], row[2], row[0]);

		// 执行更新操作，如果执行失败，跳转到 out 标签处
		if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
			goto out;
	}

	// 结束多个数据库更新操作，并将 sql、sql_alloc 和 sql_offset 传出
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 如果 sql_offset 大于 16，则执行以下操作：
	if (16 < sql_offset)
	{
		// 执行一条 SQL 语句，如果执行失败，跳转到 out 标签处
		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	// 更新 ret 变量的值为 SUCCEED，表示操作成功
	ret = SUCCEED;

out:
	// 释放 sql 字符串占用的内存
	DBfree_result(result);
	zbx_free(sql);

	// 返回 ret 变量的值，表示整个操作的结果
	return ret;
}

	DB_RESULT		result;
	int			ret = FAIL;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	result = DBselect("select p.eventid,e.clock,e.ns"
			" from problem p,events e"
			" where p.eventid=e.eventid"
				" and p.clock=0");

	while (NULL != (row = DBfetch(result)))
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update problem set clock=%s,ns=%s where eventid=%s;\n",
				row[1], row[2], row[0]);

		if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
			goto out;
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
	{
		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	ret = SUCCEED;
out:
	DBfree_result(result);
	zbx_free(sql);

	return ret;
}

#endif

DBPATCH_START(3010)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(3010000, 0, 1)
DBPATCH_ADD(3010001, 0, 1)
DBPATCH_ADD(3010002, 0, 1)
DBPATCH_ADD(3010003, 0, 1)
DBPATCH_ADD(3010004, 0, 1)
DBPATCH_ADD(3010005, 0, 1)
DBPATCH_ADD(3010006, 0, 1)
DBPATCH_ADD(3010007, 0, 1)
DBPATCH_ADD(3010008, 0, 1)
DBPATCH_ADD(3010009, 0, 1)
DBPATCH_ADD(3010010, 0, 1)
DBPATCH_ADD(3010011, 0, 1)
DBPATCH_ADD(3010012, 0, 1)
DBPATCH_ADD(3010013, 0, 1)
DBPATCH_ADD(3010014, 0, 1)
DBPATCH_ADD(3010015, 0, 1)
DBPATCH_ADD(3010016, 0, 1)
DBPATCH_ADD(3010017, 0, 1)
DBPATCH_ADD(3010018, 0, 1)
DBPATCH_ADD(3010019, 0, 1)
DBPATCH_ADD(3010020, 0, 1)
DBPATCH_ADD(3010021, 0, 1)
DBPATCH_ADD(3010022, 0, 1)
DBPATCH_ADD(3010023, 0, 1)
DBPATCH_ADD(3010024, 0, 1)
DBPATCH_ADD(3010025, 0, 1)
DBPATCH_ADD(3010026, 0, 1)
DBPATCH_ADD(3010027, 0, 1)
DBPATCH_ADD(3010028, 0, 1)
DBPATCH_ADD(3010029, 0, 1)
DBPATCH_ADD(3010030, 0, 1)
DBPATCH_ADD(3010031, 0, 1)
DBPATCH_ADD(3010032, 0, 1)
DBPATCH_ADD(3010033, 0, 1)
DBPATCH_ADD(3010034, 0, 1)
DBPATCH_ADD(3010035, 0, 1)
DBPATCH_ADD(3010036, 0, 1)
DBPATCH_ADD(3010037, 0, 1)
DBPATCH_ADD(3010038, 0, 1)
DBPATCH_ADD(3010039, 0, 1)
DBPATCH_ADD(3010042, 0, 1)
DBPATCH_ADD(3010043, 0, 1)
DBPATCH_ADD(3010044, 0, 1)
DBPATCH_ADD(3010045, 0, 1)
DBPATCH_ADD(3010046, 0, 1)
DBPATCH_ADD(3010047, 0, 1)
DBPATCH_ADD(3010048, 0, 1)
DBPATCH_ADD(3010049, 0, 1)
DBPATCH_ADD(3010050, 0, 1)
DBPATCH_ADD(3010051, 0, 1)
DBPATCH_ADD(3010052, 0, 1)
DBPATCH_ADD(3010053, 0, 1)
DBPATCH_ADD(3010054, 0, 1)
DBPATCH_ADD(3010055, 0, 1)
DBPATCH_ADD(3010056, 0, 1)
DBPATCH_ADD(3010057, 0, 1)
DBPATCH_ADD(3010058, 0, 1)
DBPATCH_ADD(3010059, 0, 1)
DBPATCH_ADD(3010060, 0, 1)
DBPATCH_ADD(3010061, 0, 1)
DBPATCH_ADD(3010062, 0, 1)
DBPATCH_ADD(3010063, 0, 1)
DBPATCH_ADD(3010064, 0, 1)
DBPATCH_ADD(3010065, 0, 1)
DBPATCH_ADD(3010066, 0, 1)
DBPATCH_ADD(3010067, 0, 1)
DBPATCH_ADD(3010068, 0, 0)
DBPATCH_ADD(3010069, 0, 1)
DBPATCH_ADD(3010070, 0, 1)
DBPATCH_ADD(3010071, 0, 1)
DBPATCH_ADD(3010072, 0, 1)
DBPATCH_ADD(3010073, 0, 1)
DBPATCH_ADD(3010074, 0, 1)
DBPATCH_ADD(3010075, 0, 1)
DBPATCH_ADD(3010076, 0, 0)
DBPATCH_ADD(3010077, 0, 1)
DBPATCH_ADD(3010078, 0, 1)
DBPATCH_ADD(3010079, 0, 1)

DBPATCH_END()
