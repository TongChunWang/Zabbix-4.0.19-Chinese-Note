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
#include "dbcache.h"
#include "zbxserver.h"
#include "template.h"

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		valuemapid;
	zbx_uint64_t		interfaceid;
	zbx_uint64_t		templateid;
	zbx_uint64_t		master_itemid;
	char			*name;
	char			*key;
	char			*delay;
	char			*history;
	char			*trends;
	char			*trapper_hosts;
	char			*units;
	char			*formula;
	char			*logtimefmt;
	char			*params;
	char			*ipmi_sensor;
	char			*snmp_community;
	char			*snmp_oid;
	char			*snmpv3_securityname;
	char			*snmpv3_authpassphrase;
	char			*snmpv3_privpassphrase;
	char			*snmpv3_contextname;
	char			*username;
	char			*password;
	char			*publickey;
	char			*privatekey;
	char			*description;
	char			*lifetime;
	char			*port;
	char			*jmx_endpoint;
	char			*timeout;
	char			*url;
	char			*query_fields;
	char			*posts;
	char			*status_codes;
	char			*http_proxy;
	char			*headers;
	char			*ssl_cert_file;
	char			*ssl_key_file;
	char			*ssl_key_password;
	unsigned char		verify_peer;
	unsigned char		verify_host;
	unsigned char		follow_redirects;
	unsigned char		post_type;
	unsigned char		retrieve_mode;
	unsigned char		request_method;
	unsigned char		output_format;
	unsigned char		type;
	unsigned char		value_type;
	unsigned char		status;
	unsigned char		snmpv3_securitylevel;
	unsigned char		snmpv3_authprotocol;
	unsigned char		snmpv3_privprotocol;
	unsigned char		authtype;
	unsigned char		flags;
	unsigned char		inventory_link;
	unsigned char		evaltype;
	unsigned char		allow_traps;
	zbx_vector_ptr_t	dependent_items;
}
zbx_template_item_t;

/* lld rule condition */
typedef struct
{
	zbx_uint64_t	item_conditionid;
	char		*macro;
	char		*value;
	unsigned char	op;
}
zbx_lld_rule_condition_t;

/* lld rule */
typedef struct
{
	/* discovery rule source id */
	zbx_uint64_t		templateid;
	/* discovery rule source conditions */
	zbx_vector_ptr_t	conditions;

	/* discovery rule destination id */
	zbx_uint64_t		itemid;
	/* the starting id to be used for destination condition ids */
	zbx_uint64_t		conditionid;
	/* discovery rule destination condition ids */
	zbx_vector_uint64_t	conditionids;
}
zbx_lld_rule_map_t;

/* auxiliary function for DBcopy_template_items() */
/******************************************************************************
 * *
 *整个代码块的主要目的是通过主机ID获取对应的接口ID列表。具体步骤如下：
 *
 *1. 声明必要的变量，包括查询结果、行数据和类型。
 *2. 执行SQL查询，获取主机ID对应的主接口及其类型。查询条件包括主机ID、接口类型（AGENT、SNMP、IPMI、JMX）以及主接口（main=1）。
 *3. 遍历查询结果，将类型字符串转换为无符号字符串，将接口ID字符串转换为无符号整数。
 *4. 将获取到的接口ID存储在接口ID列表中。
 *5. 释放查询结果。
 *
 *最终得到的接口ID列表可以用于后续的数据处理和分析。
 ******************************************************************************/
/* 定义一个静态函数，用于通过主机ID获取接口ID列表
 * 参数：
 *   zbx_uint64_t hostid：主机ID
 *   zbx_uint64_t *interfaceids：接口ID列表指针
 * 返回值：无
 */
static void DBget_interfaces_by_hostid(zbx_uint64_t hostid, zbx_uint64_t *interfaceids)
{
	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	unsigned char	type;

	/* 执行SQL查询，获取主机ID对应的主接口及其类型 */
	result = DBselect(
			"select type,interfaceid"
			" from interface"
			" where hostid=" ZBX_FS_UI64
				" and type in (%d,%d,%d,%d)"
				" and main=1",
			hostid, INTERFACE_TYPE_AGENT, INTERFACE_TYPE_SNMP, INTERFACE_TYPE_IPMI, INTERFACE_TYPE_JMX);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将类型字符串转换为无符号字符串 */
		ZBX_STR2UCHAR(type, row[0]);
		/* 将接口ID字符串转换为无符号整数 */
/******************************************************************************
 * *
 *这个代码块的主要目的是从数据库中查询模板项（template items），并将查询结果存储在一个指向指针的向量（zbx_vector_ptr_t）中。在这个过程中，代码完成了以下任务：
 *
 *1. 定义变量：声明了若干个变量，如结果（DB_RESULT）、行（DB_ROW）、字符串（char）等。
 *
 *2. 构建SQL查询语句：根据给定的条件构建了一个SQL查询语句，用于从数据库中查询模板项。
 *
 *3. 添加查询条件：将模板项的hostid作为查询条件添加到SQL查询语句中。
 *
 *4. 执行SQL查询：使用DBselect函数执行构建好的SQL查询语句。
 *
 *5. 遍历查询结果：使用DBfetch函数遍历查询结果，并在遍历过程中处理每个模板项。
 *
 *6. 分配内存存储模板项：为每个模板项分配内存，并将其存储在一个指向指针的向量（zbx_vector_ptr_t）中。
 *
 *7. 释放内存：在处理完所有模板项后，释放分配的内存。
 *
 *8. 排序输出：对存储模板项的向量进行排序，以便输出。
 *
 *整个代码块的目的是从数据库中查询并处理模板项，将其存储在一个有序的向量中，以便后续使用。
 ******************************************************************************/
static void get_template_items(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids, zbx_vector_ptr_t *items)
{
    // 定义变量
    DB_RESULT		result;
    DB_ROW			row;
    char			*sql = NULL;
    size_t			sql_alloc = 0, sql_offset = 0, i;
    unsigned char		interface_type;
    zbx_template_item_t	*item;
    zbx_uint64_t		interfaceids[4];

    // 初始化内存
    memset(&interfaceids, 0, sizeof(interfaceids));

    // 查询主机接口类型
    DBget_interfaces_by_hostid(hostid, interfaceids);

    // 构建SQL查询语句
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
            "select ti.itemid,ti.name,ti.key_,ti.type,ti.value_type,ti.delay,"
                "ti.history,ti.trends,ti.status,ti.trapper_hosts,ti.units,"
                "ti.formula,ti.logtimefmt,ti.valuemapid,ti.params,ti.ipmi_sensor,ti.snmp_community,"
                "ti.snmp_oid,ti.snmpv3_securityname,ti.snmpv3_securitylevel,ti.snmpv3_authprotocol,"
                "ti.snmpv3_authpassphrase,ti.snmpv3_privprotocol,ti.snmpv3_privpassphrase,ti.authtype,"
                "ti.username,ti.password,ti.publickey,ti.privatekey,ti.flags,ti.description,"
                "ti.inventory_link,ti.lifetime,ti.snmpv3_contextname,hi.itemid,ti.evaltype,ti.port,"
                "ti.jmx_endpoint,ti.master_itemid,ti.timeout,ti.url,ti.query_fields,ti.posts,"
                "ti.status_codes,ti.follow_redirects,ti.post_type,ti.http_proxy,ti.headers,"
                "ti.retrieve_mode,ti.request_method,ti.output_format,ti.ssl_cert_file,ti.ssl_key_file,"
                "ti.ssl_key_password,ti.verify_peer,ti.verify_host,ti.allow_traps"
            " from items ti"
            " left join items hi on hi.key_=ti.key_"
                " and hi.hostid=" ZBX_FS_UI64
            " where",
            hostid);

    // 添加查询条件
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 分配内存存储模板项
        item = (zbx_template_item_t *)zbx_malloc(NULL, sizeof(zbx_template_item_t));

        // 解析模板项信息
        ZBX_STR2UINT64(item->templateid, row[0]);
        ZBX_STR2UCHAR(item->type, row[3]);
        ZBX_STR2UCHAR(item->value_type, row[4]);
        ZBX_STR2UCHAR(item->status, row[8]);
        ZBX_DBROW2UINT64(item->valuemapid, row[13]);
        ZBX_STR2UCHAR(item->snmpv3_securitylevel, row[19]);
        ZBX_STR2UCHAR(item->snmpv3_authprotocol, row[20]);
        ZBX_STR2UCHAR(item->snmpv3_privprotocol, row[22]);
        ZBX_STR2UCHAR(item->authtype, row[24]);
        ZBX_STR2UCHAR(item->flags, row[29]);
        ZBX_STR2UCHAR(item->inventory_link, row[31]);
        ZBX_STR2UCHAR(item->evaltype, row[35]);

        // 根据接口类型分配接口ID
        switch (interface_type = get_interface_type_by_item_type(item->type))
        {
            case INTERFACE_TYPE_UNKNOWN:
                item->interfaceid = 0;
                break;
            case INTERFACE_TYPE_ANY:
                for (i = 0; INTERFACE_TYPE_COUNT > i; i++)
                {
                    if (0 != interfaceids[INTERFACE_TYPE_PRIORITY[i] - 1])
                        break;
                }
                item->interfaceid = interfaceids[INTERFACE_TYPE_PRIORITY[i] - 1];
                break;
            default:
                item->interfaceid = interfaceids[interface_type - 1];
        }

        // 分配存储模板项的内存
        item->name = zbx_strdup(NULL, row[1]);
        item->delay = zbx_strdup(NULL, row[5]);
        item->history = zbx_strdup(NULL, row[6]);
        item->trends = zbx_strdup(NULL, row[7]);
        item->trapper_hosts = zbx_strdup(NULL, row[9]);
        item->units = zbx_strdup(NULL, row[10]);
        item->formula = zbx_strdup(NULL, row[11]);
        item->logtimefmt = zbx_strdup(NULL, row[12]);
        item->params = zbx_strdup(NULL, row[14]);
        item->ipmi_sensor = zbx_strdup(NULL, row[15]);
        item->snmp_community = zbx_strdup(NULL, row[16]);
        item->snmp_oid = zbx_strdup(NULL, row[17]);
        item->snmpv3_securityname = zbx_strdup(NULL, row[18]);
        item->snmpv3_authpassphrase = zbx_strdup(NULL, row[21]);
        item->snmpv3_privpassphrase = zbx_strdup(NULL, row[23]);
        item->username = zbx_strdup(NULL, row[25]);
        item->password = zbx_strdup(NULL, row[26]);
        item->publickey = zbx_strdup(NULL, row[27]);
        item->privatekey = zbx_strdup(NULL, row[28]);
        item->description = zbx_strdup(NULL, row[30]);
        item->lifetime = zbx_strdup(NULL, row[32]);
        item->snmpv3_contextname = zbx_strdup(NULL, row[33]);
        item->port = zbx_strdup(NULL, row[36]);
        item->jmx_endpoint = zbx_strdup(NULL, row[37]);
        ZBX_DBROW2UINT64(item->master_itemid, row[38]);

        // 如果有依赖项，则添加到依赖项列表
        if (SUCCEED != DBis_null(row[34]))
        {
            item->key = NULL;
            ZBX_STR2UINT64(item->itemid, row[34]);
        }
        else
        {
            item->key = zbx_strdup(NULL, row[2]);
            item->itemid = 0;
        }

        item->timeout = zbx_strdup(NULL, row[39]);
        item->url = zbx_strdup(NULL, row[40]);
        item->query_fields = zbx_strdup(NULL, row[41]);
        item->posts = zbx_strdup(NULL, row[42]);
        item->status_codes = zbx_strdup(NULL, row[43]);
        ZBX_STR2UCHAR(item->follow_redirects, row[44]);
        ZBX_STR2UCHAR(item->post_type, row[45]);
        item->http_proxy = zbx_strdup(NULL, row[46]);
        item->headers = zbx_strdup(NULL, row[47]);
        ZBX_STR2UCHAR(item->retrieve_mode, row[48]);
        ZBX_STR2UCHAR(item->request_method, row[49]);
        ZBX_STR2UCHAR(item->output_format, row[50]);
        item->ssl_cert_file = zbx_strdup(NULL, row[51]);
        item->ssl_key_file = zbx_strdup(NULL, row[52]);
        item->ssl_key_password = zbx_strdup(NULL, row[53]);
        ZBX_STR2UCHAR(item->verify_peer, row[54]);
        ZBX_STR2UCHAR(item->verify_host, row[55]);
        ZBX_STR2UCHAR(item->allow_traps, row[56]);

        // 添加依赖项到列表
        zbx_vector_ptr_create(&item->dependent_items);
        zbx_vector_ptr_append(items, item);
    }

    // 释放内存
    DBfree_result(result);

    // 释放变量
    zbx_free(sql);

    // 排序输出
    zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

		item->url = zbx_strdup(NULL, row[40]);
		item->query_fields = zbx_strdup(NULL, row[41]);
		item->posts = zbx_strdup(NULL, row[42]);
		item->status_codes = zbx_strdup(NULL, row[43]);
		ZBX_STR2UCHAR(item->follow_redirects, row[44]);
		ZBX_STR2UCHAR(item->post_type, row[45]);
		item->http_proxy = zbx_strdup(NULL, row[46]);
		item->headers = zbx_strdup(NULL, row[47]);
		ZBX_STR2UCHAR(item->retrieve_mode, row[48]);
		ZBX_STR2UCHAR(item->request_method, row[49]);
		ZBX_STR2UCHAR(item->output_format, row[50]);
		item->ssl_cert_file = zbx_strdup(NULL, row[51]);
/******************************************************************************
 * *
 *这段代码的主要目的是从数据库中查询与给定物品相关的发现规则和条件，并将它们存储在规则映射结构中。具体来说，这段代码执行以下操作：
 *
 *1. 遍历传入的物品向量，检查每个物品的标志位，如果物品标志中包含发现规则标志（ZBX_FLAG_DISCOVERY_RULE），则创建一个新的事件规则映射结构。
 *2. 对于每个物品，构建一个SQL查询条件，其中包含物品ID，并执行查询。
 *3. 处理查询结果，将发现规则和条件添加到规则映射结构的相应向量中。
 *4. 按照规则ID对规则进行排序。
 *5. 释放占用的内存。
 *
 *整个代码块的输出结果是一个存储了发现规则和条件的信息的结构。
 ******************************************************************************/
static void get_template_lld_rule_map(const zbx_vector_ptr_t *items, zbx_vector_ptr_t *rules)
{
	/* 定义变量，这里不再赘述，后面会详细解释 */

	zbx_vector_uint64_t		itemids; // 用于存储物品ID的向量
	DB_RESULT			result; // 数据库操作结果
	DB_ROW				row; // 数据库查询结果的一行
	char				*sql = NULL; // 用于存储SQL语句的字符串指针
	size_t				sql_alloc = 0, sql_offset = 0; // 用于存储SQL语句的长度和偏移量
	zbx_uint64_t			itemid, item_conditionid; // 物品ID和物品条件ID

	/* 创建物品ID向量 */
	zbx_vector_uint64_create(&itemids);

	/* 准备发现规则 */
	for (int i = 0; i < items->values_num; i++)
	{
		zbx_template_item_t *item = (zbx_template_item_t *)items->values[i];

		/* 如果物品标志中没有发现规则，跳过 */
		if (0 == (ZBX_FLAG_DISCOVERY_RULE & item->flags))
			continue;

		zbx_lld_rule_map_t *rule = (zbx_lld_rule_map_t *)zbx_malloc(NULL, sizeof(zbx_lld_rule_map_t));

		rule->itemid = item->itemid;
		rule->templateid = item->templateid;
		rule->conditionid = 0;
		zbx_vector_uint64_create(&rule->conditionids);
		zbx_vector_ptr_create(&rule->conditions);

		/* 将规则添加到规则向量中 */
		zbx_vector_ptr_append(rules, rule);

		if (0 != rule->itemid)
			zbx_vector_uint64_append(&itemids, rule->itemid);
		zbx_vector_uint64_append(&itemids, rule->templateid);
	}

	/* 如果物品ID向量不为空，进行以下操作： */
	if (0 != itemids.values_num)
	{
		zbx_vector_ptr_sort(rules, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		/* 构建SQL语句，用于查询条件 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select item_conditionid,itemid,operator,macro,value from item_condition where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids.values, itemids.values_num);

		/* 执行SQL查询 */
		result = DBselect("%s", sql);

		/* 处理查询结果 */
		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(itemid, row[1]);

			/* 在规则向量中查找物品 */
			int index = zbx_vector_ptr_bsearch(rules, &itemid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			if (FAIL != index)
			{
				/* 读取模板发现条件 */

				zbx_lld_rule_map_t *rule = (zbx_lld_rule_map_t *)rules->values[index];

				zbx_lld_rule_condition_t *condition = (zbx_lld_rule_condition_t *)zbx_malloc(NULL, sizeof(zbx_lld_rule_condition_t));

				ZBX_STR2UINT64(condition->item_conditionid, row[0]);
				ZBX_STR2UCHAR(condition->op, row[2]);
				condition->macro = zbx_strdup(NULL, row[3]);
				condition->value = zbx_strdup(NULL, row[4]);

				/* 将条件添加到规则的条件向量中 */
				zbx_vector_ptr_append(&rule->conditions, condition);
			}
			else
			{
				/* 读取主机发现条件的标识符 */

				for (int i = 0; i < rules->values_num; i++)
				{
					zbx_lld_rule_map_t *rule = (zbx_lld_rule_map_t *)rules->values[i];

					if (itemid != rule->itemid)
						continue;

					ZBX_STR2UINT64(item_conditionid, row[0]);
					zbx_vector_uint64_append(&rule->conditionids, item_conditionid);

					break;
				}

				if (i == rules->values_num)
					THIS_SHOULD_NEVER_HAPPEN;
			}
		}
		DBfree_result(result);

		/* 释放SQL语句占用的内存 */
		zbx_free(sql);
	}

	/* 销毁物品ID向量 */
	zbx_vector_uint64_destroy(&itemids);
}

				/* read host lld conditions identifiers */

				for (i = 0; i < rules->values_num; i++)
				{
					rule = (zbx_lld_rule_map_t *)rules->values[i];

					if (itemid != rule->itemid)
						continue;

					ZBX_STR2UINT64(item_conditionid, row[0]);
					zbx_vector_uint64_append(&rule->conditionids, item_conditionid);

					break;
				}

				if (i == rules->values_num)
					THIS_SHOULD_NEVER_HAPPEN;
			}
		}
		DBfree_result(result);

		zbx_free(sql);
	}

	zbx_vector_uint64_destroy(&itemids);
}

/******************************************************************************
 *                                                                            *
 * Function: calculate_template_lld_rule_conditionids                         *
 *                                                                            *
 * Purpose: calculate identifiers for new item conditions                     *
 *                                                                            *
 * Parameters: rules - [IN] the ldd rule mapping                              *
 *                                                                            *
 * Return value: The number of new item conditions to be inserted.            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算并分配新的条件ID给LLD规则。输出如下：
 *
 *1. 遍历规则数组，计算新条件的数量。
 *2. 为新条件分配ID，并将ID分配给LLD规则。
 *3. 返回分配的新条件ID的数量。
 ******************************************************************************/
// 定义一个静态函数，用于计算并分配新的条件ID给LLD规则
static int	calculate_template_lld_rule_conditionids(zbx_vector_ptr_t *rules)
{
	// 定义一个指向zbx_lld_rule_map_t结构体的指针
	zbx_lld_rule_map_t	*rule;
	// 定义一个循环变量，用于遍历规则数组
	int			i, conditions_num = 0;
	// 定义一个指向zbx_uint64_t类型的指针，用于存储条件ID
	zbx_uint64_t		conditionid;

	/* 计算需要插入的新条件的数量 */
	for (i = 0; i < rules->values_num; i++)
	{
		// 获取当前规则
		rule = (zbx_lld_rule_map_t *)rules->values[i];

		// 判断当前规则中的条件数量是否大于条件ID数量，如果是，则将差值加到conditions_num中
		if (rule->conditions.values_num > rule->conditionids.values_num)
			conditions_num += rule->conditions.values_num - rule->conditionids.values_num;
	}

	/* 为新条件分配ID，并将ID分配给LLD规则 */
	if (0 == conditions_num)
		goto out;

	// 获取最大的条件ID
	conditionid = DBget_maxid_num("item_condition", conditions_num);

	for (i = 0; i < rules->values_num; i++)
	{
		// 获取当前规则
		rule = (zbx_lld_rule_map_t *)rules->values[i];

		// 如果当前规则中的条件数量小于等于条件ID数量，则跳过该循环
		if (rule->conditions.values_num <= rule->conditionids.values_num)
			continue;

		// 将条件ID分配给当前规则
		rule->conditionid = conditionid;
/******************************************************************************
 * *
 *整个代码块的主要目的是更新模板中的 LLD 规则公式。具体来说，该函数接收两个参数：一个包含模板项的向量（items）和一个包含 LLD 规则的向量（rules）。函数遍历模板项向量，检查每个模板项的 discovery_rule 标志位和 evaltype。如果满足条件，则在规则向量中查找对应的规则，并使用二分查找算法。找到规则后，遍历规则中的每个条件，并替换公式中的特定字符串。最后，释放原有公式内存，并将更新后的公式赋值给模板项。
 ******************************************************************************/
// 定义一个静态函数，用于更新模板中的 LLD 规则公式
static void update_template_lld_rule_formulas(zbx_vector_ptr_t *items, zbx_vector_ptr_t *rules)
{
    // 定义一个指向 zbx_lld_rule_map_t 结构的指针
    zbx_lld_rule_map_t *rule;

    // 定义一些变量，用于遍历和操作
    int i, j, index;
    char *formula;
    zbx_uint64_t conditionid;

    // 遍历 items 中的每个元素
    for (i = 0; i < items->values_num; i++)
    {
        // 获取 items 中的当前元素，类型为 zbx_template_item_t
        zbx_template_item_t *item = (zbx_template_item_t *)items->values[i];

        // 如果当前元素的 discovery_rule 标志位未设置或 evaltype 不是 expression，则跳过
        if (0 == (ZBX_FLAG_DISCOVERY_RULE & item->flags) || CONDITION_EVAL_TYPE_EXPRESSION != item->evaltype)
            continue;

        // 在 rules 向量中查找与当前元素对应的规则，使用二分查找算法
        index = zbx_vector_ptr_bsearch(rules, &item->templateid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

        // 如果查找失败，表示规则未找到，打印错误信息并跳过当前元素
        if (FAIL == index)
        {
            THIS_SHOULD_NEVER_HAPPEN;
            continue;
        }

        // 获取找到的规则
        rule = (zbx_lld_rule_map_t *)rules->values[index];

        // 复制当前元素的公式到 formula 字符串
        formula = zbx_strdup(NULL, item->formula);

        // 初始化 conditionid
        conditionid = rule->conditionid;

        // 遍历规则中的每个条件
        for (j = 0; j < rule->conditions.values_num; j++)
        {
            // 获取当前条件
            zbx_lld_rule_condition_t *condition = (zbx_lld_rule_condition_t *)rule->conditions.values[j];

            // 如果当前条件有对应的 item_conditionid，则替换公式中的 srcid
            if (j < rule->conditionids.values_num)
            {
                id = rule->conditionids.values[j];
            }
            else
            {
                id = conditionid++;
            }

            // 构造 srcid 和 dstid 字符串
            zbx_snprintf(srcid, sizeof(srcid), "{" ZBX_FS_UI64 "}", condition->item_conditionid);
            zbx_snprintf(dstid, sizeof(dstid), "{" ZBX_FS_UI64 "}", id);

            // 查找公式中包含的 srcid 并替换为 dstid
            size_t pos = 0, len;
            while (NULL != (ptr = strstr(formula + pos, srcid)))
            {
                // 计算替换位置
                pos = ptr - formula + len - 1;

                // 替换字符串
                zbx_replace_string(&formula, ptr - formula, &pos, dstid);
            }
        }

        // 释放 item->formula 内存，并将更新后的 formula 赋值给 item->formula
        zbx_free(item->formula);
        item->formula = formula;
    }
}

			if (j < rule->conditionids.values_num)
				id = rule->conditionids.values[j];
			else
				id = conditionid++;

			zbx_snprintf(srcid, sizeof(srcid), "{" ZBX_FS_UI64 "}", condition->item_conditionid);
			zbx_snprintf(dstid, sizeof(dstid), "{" ZBX_FS_UI64 "}", id);

			len = strlen(srcid);

			while (NULL != (ptr = strstr(formula + pos, srcid)))
			{
				pos = ptr - formula + len - 1;
				zbx_replace_string(&formula, ptr - formula, &pos, dstid);
			}
		}

		zbx_free(item->formula);
		item->formula = formula;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_item                                               *
 *                                                                            *
 * Purpose: save (insert or update) template item                             *
 *                                                                            *
 * Parameters: hostid     - [IN] parent host id                               *
 *             itemid     - [IN/OUT] item id used for insert operations       *
 *             item       - [IN] item to be saved                             *
 *             db_insert  - [IN] prepared item bulk insert                    *
 *             sql        - [IN/OUT] sql buffer pointer used for update       *
 *                                   operations                               *
 *             sql_alloc  - [IN/OUT] sql buffer already allocated memory      *
 *             sql_offset - [IN/OUT] offset for writing within sql buffer     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这
 ******************************************************************************/是一段 C 语言代码，主要目的是用于保存模板项目（template item）的信息到数据库。代码中定义了一个名为 `save_template_item` 的函数，该函数接收 6 个参数，分别是：

- `zbx_uint64_t hostid`：字符串类型，表示主机 ID。
- `zbx_uint64_t *itemid`：字符串数组指针，表示项目 ID 数组。
- `zbx_template_item_t *item`：模板项目结构体数组指针，表示模板项目数组。
- `zbx_db_insert_t *db_insert`：数据库插入操作结构体指针，用于执行插入操作。
- `char **sql`：字符串指针数组，用于存储插入语句。
- `size_t *sql_alloc`：存储字符串指针数组的大小。
- `size_t *sql_offset`：存储字符串指针数组中当前插入位置。

代码的主要流程如下：

1. 首先判断模板项目数组中的每个项目是否已经存在，如果存在，则使用 SQL 语句更新项目信息；如果不存在，则使用 SQL 语句插入新项目。
2. 遍历模板项目数组中的每个项目，将其依赖项（dependent items）的信息也插入到数据库中。

代码中使用了大量的字符串操作和 SQL 语句，用于处理和保存模板项目的各种信息。

/******************************************************************************
 *                                                                            *
 * Function: save_template_items                                              *
 *                                                                            *
 * Purpose: saves template items to the target host in database               *
 *                                                                            *
 * Parameters:  hostid - [IN] the target host                                 *
 *              items  - [IN] the template items                              *
 *              rules  - [IN] the ldd rule mapping                            *
 *                                                                            *
 ******************************************************************************/
static void	save_template_items(zbx_uint64_t hostid, zbx_vector_ptr_t *items)
{
	char			*sql = NULL;
	size_t			sql_alloc = 16 * ZBX_KIBIBYTE, sql_offset = 0;
	int			new_items = 0, upd_items = 0, i;
	zbx_uint64_t		itemid = 0;
	zbx_db_insert_t		db_insert;
	zbx_template_item_t	*item;

	if (0 == items->values_num)
		return;

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_template_item_t *)items->values[i];

		if (NULL == item->key)
			upd_items++;
		else
			new_items++;
	}

	if (0 != new_items)
	{
		itemid = DBget_maxid_num("items", new_items);

		zbx_db_insert_prepare(&db_insert, "items", "itemid", "name", "key_", "hostid", "type", "value_type",
				"delay", "history", "trends", "status", "trapper_hosts", "units",
				"formula", "logtimefmt", "valuemapid", "params", "ipmi_sensor",
				"snmp_community", "snmp_oid", "snmpv3_securityname", "snmpv3_securitylevel",
				"snmpv3_authprotocol", "snmpv3_authpassphrase", "snmpv3_privprotocol",
				"snmpv3_privpassphrase", "authtype", "username", "password", "publickey", "privatekey",
				"templateid", "flags", "description", "inventory_link", "interfaceid", "lifetime",
				"snmpv3_contextname", "evaltype", "port", "jmx_endpoint", "master_itemid",
				"timeout", "url", "query_fields", "posts", "status_codes", "follow_redirects",
				"post_type", "http_proxy", "headers", "retrieve_mode", "request_method",
				"output_format", "ssl_cert_file", "ssl_key_file", "ssl_key_password", "verify_peer",
				"verify_host", "allow_traps", NULL);
	}

	if (0 != upd_items)
	{
		sql = (char *)zbx_malloc(sql, sql_alloc);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_template_item_t *)items->values[i];

		/* dependent items are saved within recursive save_template_item calls while saving master */
		if (0 == item->master_itemid)
			save_template_item(hostid, &itemid, item, &db_insert, &sql, &sql_alloc, &sql_offset);
	}

	if (0 != new_items)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	if (0 != upd_items)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)
			DBexecute("%s", sql);

		zbx_free(sql);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_lld_rules                                          *
 *                                                                            *
 * Purpose: saves template lld rule item conditions to the target host in     *
 *          database                                                          *
 *                                                                            *
 * Parameters:  items          - [IN] the template items                      *
 *              rules          - [IN] the ldd rule mapping                    *
 *              new_conditions - [IN] the number of new item conditions to    *
 *                                    be inserted                             *
 *                                                                            *
 ******************************************************************************/
static void	save_template_lld_rules(zbx_vector_ptr_t *items, zbx_vector_ptr_t *rules, int new_conditions)
{
	char				*macro_esc, *value_esc;
	int				i, j, index;
	zbx_db_insert_t			db_insert;
	zbx_lld_rule_map_t		*rule;
	zbx_lld_rule_condition_t	*condition;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t		item_conditionids;

	if (0 == rules->values_num)
		return;

	zbx_vector_uint64_create(&item_conditionids);

	if (0 != new_conditions)
	{
		zbx_db_insert_prepare(&db_insert, "item_condition", "item_conditionid", "itemid", "operator", "macro",
				"value", NULL);

		/* insert lld rule conditions for new items */
		for (i = 0; i < items->values_num; i++)
		{
			zbx_template_item_t	*item = (zbx_template_item_t *)items->values[i];

			if (NULL == item->key)
				continue;

			if (0 == (ZBX_FLAG_DISCOVERY_RULE & item->flags))
				continue;

			index = zbx_vector_ptr_bsearch(rules, &item->templateid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			if (FAIL == index)
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

/******************************************************************************
 * *
 *这段代码的主要目的是保存模板项到数据库。首先，它遍历输入的物品列表，然后根据物品的键和主物品ID来进行新建和更新操作。在保存物品时，它会准备插入或更新语句，并将数据递归地传递给`save_template_item`函数。最后，执行插入或更新操作，并释放相关资源。
 ******************************************************************************/
/* 定义一个函数，用于保存模板项到数据库 */
static void save_template_items(zbx_uint64_t hostid, zbx_vector_ptr_t *items)
{
	/* 定义一些变量 */
	char *sql = NULL;
	size_t sql_alloc = 16 * ZBX_KIBIBYTE, sql_offset = 0;
	int new_items = 0, upd_items = 0, i;
	zbx_uint64_t itemid = 0;
	zbx_db_insert_t db_insert;
	zbx_template_item_t *item;

	/* 如果物品数量为0，则返回 */
	if (0 == items->values_num)
		return;

	/* 遍历物品 */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_template_item_t *)items->values[i];

		/* 如果物品键为空，则更新物品 */
		if (NULL == item->key)
			upd_items++;
		else
			new_items++;
	}

	/* 如果新建物品不为0，则获取最大物品ID */
	if (0 != new_items)
	{
		itemid = DBget_maxid_num("items", new_items);

		/* 准备数据库插入语句 */
		zbx_db_insert_prepare(&db_insert, "items", "itemid", "name", "key_", "hostid", "type", "value_type",
				"delay", "history", "trends", "status", "trapper_hosts", "units",
				"formula", "logtimefmt", "valuemapid", "params", "ipmi_sensor",
				"snmp_community", "snmp_oid", "snmpv3_securityname", "snmpv3_securitylevel",
				"snmpv3_authprotocol", "snmpv3_authpassphrase", "snmpv3_privprotocol",
				"snmpv3_privpassphrase", "authtype", "username", "password", "publickey", "privatekey",
				"templateid", "flags", "description", "inventory_link", "interfaceid", "lifetime",
				"snmpv3_contextname", "evaltype", "port", "jmx_endpoint", "master_itemid",
				"timeout", "url", "query_fields", "posts", "status_codes", "follow_redirects",
				"post_type", "http_proxy", "headers", "retrieve_mode", "request_method",
				"output_format", "ssl_cert_file", "ssl_key_file", "ssl_key_password", "verify_peer",
				"verify_host", "allow_traps", NULL);
	}

	/* 如果更新物品不为0，则准备更新语句 */
	if (0 != upd_items)
	{
		sql = (char *)zbx_malloc(sql, sql_alloc);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	/* 遍历物品，逐个进行保存 */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_template_item_t *)items->values[i];

		/* 如果不存在主物品，则递归调用 save_template_item 保存子物品 */
		if (0 == item->master_itemid)
			save_template_item(hostid, &itemid, item, &db_insert, &sql, &sql_alloc, &sql_offset);
	}

	/* 如果新建物品不为0，则执行插入操作 */
	if (0 != new_items)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	/* 如果更新物品不为0，则执行更新操作 */
	if (0 != upd_items)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		/* 如果更新语句长度大于16，则直接执行更新操作 */
		if (16 < sql_offset)
			DBexecute("%s", sql);

		/* 释放内存 */
		zbx_free(sql);
	}
}


	if (0 != new_conditions)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	zbx_free(sql);
	zbx_vector_uint64_destroy(&item_conditionids);
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_item_applications                                  *
/******************************************************************************
 * *
 *这个代码块的主要目的是对zbx_vector_ptr_t类型的指针变量items和rules进行操作，其中items存储了模板中的物品，rules存储了lld规则。代码首先检查rules中的条件数量，如果为0，则直接返回。接下来，代码针对new_conditions不为0的情况进行以下操作：
 *
 *1. 预处理数据库插入操作，定义db_insert结构体变量。
 *2. 遍历items中的每个物品，检查物品的键是否为空，如果为空则跳过。
 *3. 如果物品没有discovery rule标志，则跳过。
 *4. 在rules中查找物品的模板ID。
 *5. 遍历lld规则的条件，对每个条件进行转义处理（防止SQL注入），然后拼接SQL语句并执行。
 *6. 更新现有物品的lld规则条件，首先找到交集条件，然后更新条件操作符、宏和值。
 *7. 删除不再使用的条件，如果条件数量不为0，则插入新条件。
 *8. 删除不再使用的物品条件。
 *9. 执行新条件插入操作。
 *10. 释放内存。
 *
 *整个代码块的主要目的是为zbx_vector_ptr_t类型的指针变量items和rules中的lld规则插入和更新条件。
 ******************************************************************************/
// 定义一个静态函数，用于保存 lld 规则到数据库
static void save_template_lld_rules(zbx_vector_ptr_t *items, zbx_vector_ptr_t *rules, int new_conditions)
{
	// 定义一些变量，用于后续操作
	char *macro_esc, *value_esc;
	int i, j, index;
	zbx_db_insert_t db_insert;
	zbx_lld_rule_map_t *rule;
	zbx_lld_rule_condition_t *condition;
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t item_conditionids;

	// 如果 rules 中的条件数为0，则直接返回
	if (0 == rules->values_num)
		return;

	// 创建一个 uint64 类型的 vector，用于存储物品条件 ID
	zbx_vector_uint64_create(&item_conditionids);

	// 如果 new_conditions 不为0，则执行以下操作：
	if (0 != new_conditions)
	{
		// 预处理数据库插入操作
		zbx_db_insert_prepare(&db_insert, "item_condition", "item_conditionid", "itemid", "operator", "macro",
				"value", NULL);

		// 为新物品插入 lld 规则条件
		for (i = 0; i < items->values_num; i++)
		{
			zbx_template_item_t *item = (zbx_template_item_t *)items->values[i];

			// 如果物品的键为空，则跳过
			if (NULL == item->key)
				continue;

			// 如果物品没有 discovery rule 标志，则跳过
			if (0 == (ZBX_FLAG_DISCOVERY_RULE & item->flags))
				continue;

			// 在 rules 中查找物品的模板 ID
			index = zbx_vector_ptr_bsearch(rules, &item->templateid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			// 如果找不到，则跳过
			if (FAIL == index)
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			// 获取 lld 规则
			rule = (zbx_lld_rule_map_t *)rules->values[index];

			// 遍历 lld 规则的条件
			for (j = 0; j < rule->conditions.values_num; j++)
			{
				condition = (zbx_lld_rule_condition_t *)rule->conditions.values[j];

				// 转义条件中的宏和值
				macro_esc = DBdyn_escape_string(condition->macro);
				value_esc = DBdyn_escape_string(condition->value);

				// 拼接 SQL 语句
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "insert into item_condition (item_conditionid, itemid, operator, macro, value)"
						" values (" ZBX_FS_UI64 ", %d, %d, '%s', '%s');\
",
						rule->conditionid++, item->itemid, (int)condition->op, macro_esc, value_esc);

				// 执行 SQL 语句
				DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);

				// 释放内存
				zbx_free(value_esc);
				zbx_free(macro_esc);
			}
		}
	}

	// 开始执行多个数据库更新操作
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 更新现有物品的 lld 规则条件
	for (i = 0; i < rules->values_num; i++)
	{
		rule = (zbx_lld_rule_map_t *)rules->values[i];

		// 跳过新物品的 lld 规则
		if (0 == rule->itemid)
			continue;

		index = MIN(rule->conditions.values_num, rule->conditionids.values_num);

		// 更新交集条件
		for (j = 0; j < index; j++)
		{
			condition = (zbx_lld_rule_condition_t *)rule->conditions.values[j];

			macro_esc = DBdyn_escape_string(condition->macro);
			value_esc = DBdyn_escape_string(condition->value);

			// 拼接 SQL 语句
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update item_condition"
					" set operator=%d,macro='%s',value='%s'"
					" where item_conditionid=" ZBX_FS_UI64 ";\
",
					(int)condition->op, macro_esc, value_esc, rule->conditionids.values[j]);

			// 执行 SQL 语句
			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);

			// 释放内存
			zbx_free(value_esc);
			zbx_free(macro_esc);
		}

		// 删除不再使用的条件
		for (j = index; j < rule->conditionids.values_num; j++)
			zbx_vector_uint64_append(&item_conditionids, rule->conditionids.values[j]);

		// 插入新条件
		for (j = index; j < rule->conditions.values_num; j++)
		{
			condition = (zbx_lld_rule_condition_t *)rule->conditions.values[j];

			zbx_db_insert_add_values(&db_insert, rule->conditionid++, rule->itemid,
					(int)condition->op, condition->macro, condition->value);
		}
	}

	// 删除不再使用的物品条件
	if (0 != item_conditionids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_condition where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "item_conditionid", item_conditionids.values,
				item_conditionids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	// 结束多个数据库更新操作
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 如果 SQL 偏移量大于16，则执行 SQL 语句
	if (16 < sql_offset)
		DBexecute("%s", sql);

	// 执行新条件插入操作
	if (0 != new_conditions)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	// 释放内存
	zbx_free(sql);
	zbx_vector_uint64_destroy(&item_conditionids);
}

			" where i.templateid=id.itemid"
				" and id.parent_itemid=r.templateid"
				" and r.hostid=" ZBX_FS_UI64
				" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.itemid", itemids.values, itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		proto = (zbx_proto_t *)zbx_malloc(NULL, sizeof(zbx_proto_t));

		ZBX_STR2UINT64(proto->itemid, row[0]);
		ZBX_STR2UINT64(proto->parent_itemid, row[1]);

		zbx_vector_ptr_append(&prototypes, proto);
	}
	DBfree_result(result);

	if (0 == prototypes.values_num)
		goto out;

	zbx_db_insert_prepare(&db_insert, "item_discovery", "itemdiscoveryid", "itemid",
					"parent_itemid", NULL);

	for (i = 0; i < prototypes.values_num; i++)
	{
		proto = (zbx_proto_t *)prototypes.values[i];

		zbx_db_insert_add_values(&db_insert, __UINT64_C(0), proto->itemid, proto->parent_itemid);
	}

	zbx_db_insert_autoincrement(&db_insert, "itemdiscoveryid");
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
out:
	zbx_free(sql);

	zbx_vector_uint64_destroy(&itemids);

	zbx_vector_ptr_clear_ext(&prototypes, zbx_ptr_free);
	zbx_vector_ptr_destroy(&prototypes);
}

/******************************************************************************
 *                                                                            *
 * Function: free_template_item                                               *
 *                                                                            *
 * Purpose: frees template item                                               *
 *                                                                            *
 * Parameters:  item  - [IN] the template item                                *
 *                                                                            *
 ******************************************************************************/
static void	free_template_item(zbx_template_item_t *item)
{
	zbx_free(item->timeout);
	zbx_free(item->url);
	zbx_free(item->query_fields);
	zbx_free(item->posts);
	zbx_free(item->status_codes);
	zbx_free(item->http_proxy);
	zbx_free(item->headers);
	zbx_free(item->ssl_cert_file);
	zbx_free(item->ssl_key_file);
	zbx_free(item->ssl_key_password);
	zbx_free(item->jmx_endpoint);
	zbx_free(item->port);
	zbx_free(item->snmpv3_contextname);
	zbx_free(item->lifetime);
	zbx_free(item->description);
	zbx_free(item->privatekey);
	zbx_free(item->publickey);
	zbx_free(item->password);
	zbx_free(item->username);
	zbx_free(item->snmpv3_privpassphrase);
	zbx_free(item->snmpv3_authpassphrase);
	zbx_free(item->snmpv3_securityname);
	zbx_free(item->snmp_oid);
	zbx_free(item->snmp_community);
	zbx_free(item->ipmi_sensor);
	zbx_free(item->params);
	zbx_free(item->logtimefmt);
	zbx_free(item->formula);
	zbx_free(item->units);
	zbx_free(item->trapper_hosts);
	zbx_free(item->trends);
	zbx_free(item->history);
	zbx_free(item->delay);
	zbx_free(item->name);
	zbx_free(item->key);

	zbx_vector_ptr_destroy(&item->dependent_items);

	zbx_free(item);
}

/******************************************************************************
 *                                                                            *
 * Function: free_lld_rule_condition                                          *
 *                                                                            *
 * Purpose: frees lld rule condition                                          *
 *                                                                            *
 * Parameters:  item  - [IN] the lld rule condition                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放一个zbx_lld_rule_condition结构体对象所占用的内存空间。这里分别释放了该结构体中的macro、value成员变量以及condition本身所占用的内存。这是一个用于清理资源的后置处理函数。
 ******************************************************************************/
// 定义一个静态函数 free_lld_rule_condition，参数为一个zbx_lld_rule_condition_t类型的指针condition
static void free_lld_rule_condition(zbx_lld_rule_condition_t *condition)
{
    // 释放condition指向的macro内存空间
    zbx_free(condition->macro);
    // 释放condition指向的value内存空间
    zbx_free(condition->value);
    // 释放condition本身所占用的内存空间
    zbx_free(condition);
}


/******************************************************************************
 *                                                                            *
 * Function: free_lld_rule_map                                                *
 *                                                                            *
 * Purpose: frees lld rule mapping                                            *
 *                                                                            *
 * Parameters:  item  - [IN] the lld rule mapping                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 `template_item_hash_func` 的静态函数，该函数用于计算zbx模板项（zbx_template_item_t类型）的哈希值。函数接受一个void类型的指针作为参数d，解引用指针d并将其转换为const zbx_template_item_t类型的指针，然后调用ZBX_DEFAULT_UINT64_HASH_FUNC函数，将指针item的templateid成员变量作为参数，最终返回计算得到的哈希值。
 ******************************************************************************/
// 定义一个名为 template_item_hash_func 的静态函数，该函数接受一个 void 类型的指针作为参数 d
static zbx_hash_t	template_item_hash_func(const void *d)
{
	// 解引用指针 d，将其转换为 const zbx_template_item_t 类型的指针，并将其赋值给变量 item
	const zbx_template_item_t	*item = *(const zbx_template_item_t **)d;

	// 调用 ZBX_DEFAULT_UINT64_HASH_FUNC 函数，传入 item 指针的 templateid 成员变量作为参数
	return ZBX_DEFAULT_UINT64_HASH_FUNC(&item->templateid);
}

    // 首先，清理rule指向的条件链表（conditions）
    zbx_vector_ptr_clear_ext(&rule->conditions, (zbx_clean_func_t)free_lld_rule_condition);
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中获取模板发现的原型项，并将它们保存到数据库中。具体来说，这段代码执行以下操作：
 *
 *1. 遍历传入的items数组，查找具有ZBX_FLAG_DISCOVERY_PROTOTYPE标志的模板项。
 *2. 将找到的模板项的itemid添加到itemids向量中。
 *3. 对itemids进行排序。
 *4. 构造SQL查询语句，用于从数据库中获取与给定hostid相关的模板项。
 *5. 执行SQL查询并遍历查询结果中的每一行。
 *6. 对于每一行，提取数据并创建一个zbx_proto_t结构体实例。
 *7. 将zbx_proto_t结构体实例添加到prototypes向量中。
 *8. 预处理数据库插入操作。
 *9. 将每个zbx_proto_t结构体实例添加到数据库插入操作中。
 *10. 执行数据库插入操作并自动递增主键。
 *11. 释放分配的内存，并销毁itemids和prototypes结构体。
 ******************************************************************************/
static void save_template_discovery_prototypes(zbx_uint64_t hostid, zbx_vector_ptr_t *items)
{
    // 定义一个zbx_proto_t结构体，包含两个zbx_uint64_t类型的成员：itemid和parent_itemid
    typedef struct
    {
        zbx_uint64_t	itemid;
        zbx_uint64_t	parent_itemid;
    }
    zbx_proto_t;

    // 声明一些变量，包括数据库操作所需的数据结构以及指向它们的指针
    DB_RESULT		result;
    DB_ROW			row;
    char			*sql = NULL;
    size_t			sql_alloc = 0, sql_offset = 0;
    zbx_vector_uint64_t	itemids;
    zbx_vector_ptr_t	prototypes;
    zbx_proto_t		*proto;
    int				i;
    zbx_db_insert_t		db_insert;

    // 初始化zbx_vector_ptr和zbx_vector_uint64结构体
    zbx_vector_ptr_create(&prototypes);
    zbx_vector_uint64_create(&itemids);

    // 遍历items数组中的每个元素，处理其中的模板项
    for (i = 0; i < items->values_num; i++)
    {
        zbx_template_item_t	*item = (zbx_template_item_t *)items->values[i];

        // 仅处理新原型项
        if (NULL == item->key || 0 == (ZBX_FLAG_DISCOVERY_PROTOTYPE & item->flags))
            continue;

        // 将当前模板项的itemid添加到itemids中
        zbx_vector_uint64_append(&itemids, item->itemid);
    }

    // 如果itemids为空，则直接退出函数
    if (0 == itemids.values_num)
        goto out;

    // 对itemids进行排序
    zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 构造SQL查询语句
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                "select i.itemid,r.itemid"
                " from items i,item_discovery id,items r"
                " where i.templateid=id.itemid"
                " and id.parent_itemid=r.templateid"
                " and r.hostid=%zu"
                " and",
                hostid);
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.itemid", itemids.values, itemids.values_num);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 遍历查询结果中的每一行，提取数据并创建zbx_proto_t结构体实例
    while (NULL != (row = DBfetch(result)))
    {
        proto = (zbx_proto_t *)zbx_malloc(NULL, sizeof(zbx_proto_t));

        ZBX_STR2UINT64(proto->itemid, row[0]);
        ZBX_STR2UINT64(proto->parent_itemid, row[1]);

        // 将proto添加到prototypes中
        zbx_vector_ptr_append(&prototypes, proto);
    }
    DBfree_result(result);

    // 如果prototypes为空，则直接退出函数
    if (0 == prototypes.values_num)
        goto out;

    // 预处理数据库插入操作
    zbx_db_insert_prepare(&db_insert, "item_discovery", "itemdiscoveryid", "itemid",
                        "parent_itemid", NULL);
/******************************************************************************
 * *
 *这块代码的主要目的是释放zbx_template_item结构体所占用的内存。函数free_template_item()接收一个zbx_template_item_t类型的指针作为参数，逐个释放该结构体中的成员变量，包括字符串、数组等数据类型。在释放完所有成员变量后，再释放vector中的依赖项（dependent_items），最后释放整个zbx_template_item结构体。这样可以确保程序在处理完这个结构体后，不再占用任何内存空间。
 ******************************************************************************/
// 定义一个函数，用于释放zbx_template_item结构体的内存
static void free_template_item(zbx_template_item_t *item)
{
    // 释放item->timeout的内存
    zbx_free(item->timeout);
    // 释放item->url的内存
    zbx_free(item->url);
    // 释放item->query_fields的内存
    zbx_free(item->query_fields);
    // 释放item->posts的内存
    zbx_free(item->posts);
    // 释放item->status_codes的内存
    zbx_free(item->status_codes);
    // 释放item->http_proxy的内存
    zbx_free(item->http_proxy);
    // 释放item->headers的内存
    zbx_free(item->headers);
    // 释放item->ssl_cert_file的内存
    zbx_free(item->ssl_cert_file);
    // 释放item->ssl_key_file的内存
    zbx_free(item->ssl_key_file);
    // 释放item->ssl_key_password的内存
    zbx_free(item->ssl_key_password);
    // 释放item->jmx_endpoint的内存
    zbx_free(item->jmx_endpoint);
    // 释放item->port的内存
    zbx_free(item->port);
    // 释放item->snmpv3_contextname的内存
    zbx_free(item->snmpv3_contextname);
    // 释放item->lifetime的内存
    zbx_free(item->lifetime);
    // 释放item->description的内存
    zbx_free(item->description);
    // 释放item->privatekey的内存
    zbx_free(item->privatekey);
    // 释放item->publickey的内存
    zbx_free(item->publickey);
    // 释放item->password的内存
    zbx_free(item->password);
    // 释放item->username的内存
    zbx_free(item->username);
    // 释放item->snmpv3_privpassphrase的内存
    zbx_free(item->snmpv3_privpassphrase);
    // 释放item->snmpv3_authpassphrase的内存
    zbx_free(item->snmpv3_authpassphrase);
    // 释放item->snmpv3_securityname的内存
    zbx_free(item->snmpv3_securityname);
    // 释放item->snmp_oid的内存
    zbx_free(item->snmp_oid);
    // 释放item->snmp_community的内存
    zbx_free(item->snmp_community);
    // 释放item->ipmi_sensor的内存
    zbx_free(item->ipmi_sensor);
    // 释放item->params的内存
    zbx_free(item->params);
    // 释放item->logtimefmt的内存
    zbx_free(item->logtimefmt);
    // 释放item->formula的内存
    zbx_free(item->formula);
    // 释放item->units的内存
    zbx_free(item->units);
    // 释放item->trapper_hosts的内存
    zbx_free(item->trapper_hosts);
    // 释放item->trends的内存
    zbx_free(item->trends);
    // 释放item->history的内存
    zbx_free(item->history);
    // 释放item->delay的内存
    zbx_free(item->delay);
    // 释放item->name的内存
    zbx_free(item->name);
    // 释放item->key的内存
    zbx_free(item->key);

    // 销毁item->dependent_items vector
    zbx_vector_ptr_destroy(&item->dependent_items);

    // 释放item本身的空间
    zbx_free(item);
}

			"select ip.itemid,ip.step,ip.type,ip.params"
				" from item_preproc ip,items ti"
				" where ip.itemid=ti.itemid"
				" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

	result = DBselect("%s", sql);
	while (NULL != (row = DBfetch(result)))
	{
		zbx_template_item_t	item_local, *pitem_local = &item_local;

		ZBX_STR2UINT64(item_local.templateid, row[0]);
		if (NULL == (pitem = (const zbx_template_item_t **)zbx_hashset_search(&items_t, &pitem_local)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_db_insert_add_values(&db_insert, __UINT64_C(0), (*pitem)->itemid, atoi(row[1]), atoi(row[2]),
				row[3]);

	}
	DBfree_result(result);

	zbx_db_insert_autoincrement(&db_insert, "item_preprocid");
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	zbx_free(sql);
	zbx_hashset_destroy(&items_t);
	zbx_vector_uint64_destroy(&itemids);
}

/******************************************************************************
 *                                                                            *
 * Function: compare_template_items                                           *
 *                                                                            *
 * Purpose: compare templateid of two template items                          *
 *                                                                            *
 * Parameters: d1 - [IN] first template item                                  *
 *             d2 - [IN] second template item                                 *
 *                                                                            *
 * Return value: compare result (-1 for d1<d2, 1 for d1>d2, 0 for d1==d2)     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为compare_template_items的静态函数，用于比较两个zbx_template_item_t结构体实例的优先级。具体实现方式是通过调用zbx_default_uint64_compare_func函数，比较两个实例的templateid字段值。函数返回一个整数值，表示两个参数的大小关系。
 ******************************************************************************/
// 定义一个静态函数 compare_template_items，用于比较两个zbx_template_item_t结构体实例的优先级
static int	compare_template_items(const void *d1, const void *d2)
{
	// 解引用指针，获取zbx_template_item_t结构体实例
	const zbx_template_item_t	*i1 = *(const zbx_template_item_t **)d1;
	const zbx_template_item_t	*i2 = *(const zbx_template_item_t **)d2;

	// 调用zbx_default_uint64_compare_func函数，比较两个实例的templateid字段值
	// 该函数接收两个uint64类型的参数，返回一个整数值，表示两个参数的大小关系
	return zbx_default_uint64_compare_func(&i1->templateid, &i2->templateid);
}


/******************************************************************************
 * *
 *这块代码的主要目的是将zbx_vector（items）中的模板依赖项链接到相应的模板（master）中。具体步骤如下：
 *
 *1. 创建一个指向zbx_vector的指针（template_index）。
 *2. 将items中的元素添加到template_index中，并对其进行排序。
 *3. 遍历items中的每个元素。
 *4. 如果当前元素有主项（master_itemid），则在template_index中查找对应的主项。
 *5. 如果找不到主项，则删除当前元素（dependent item）并释放内存。
 *6. 找到主项后，将当前元素添加到主项的依赖项列表中。
 *7. 销毁template_index。
 *8. 记录函数调用日志。
 ******************************************************************************/
static void link_template_dependent_items(zbx_vector_ptr_t *items)
{
    // 定义一个静态函数，用于将模板依赖项链接到模板

    const char *__function_name = "link_template_dependent_items";
    zbx_template_item_t *item, *master, item_local;
    int i, index;
    zbx_vector_ptr_t template_index;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个指向zbx_vector的指针
    zbx_vector_ptr_create(&template_index);

    // 将items中的元素添加到template_index中，并排序
    zbx_vector_ptr_append_array(&template_index, items->values, items->values_num);
    zbx_vector_ptr_sort(&template_index, compare_template_items);

    // 遍历items中的每个元素
    for (i = items->values_num - 1; i >= 0; i--)
    {
        // 获取当前元素
        item = (zbx_template_item_t *)items->values[i];

        // 如果当前元素有主项，则进行以下操作
        if (0 != item->master_itemid)
        {
            // 设置当前元素的局部模板ID
            item_local.templateid = item->master_itemid;

            // 在template_index中查找对应的 master 项
            if (FAIL == (index = zbx_vector_ptr_bsearch(&template_index, &item_local,
                                                      compare_template_items)))
            {
                // 如果找不到主项，则删除当前元素
                /* dependent item without master item should be removed */
                THIS_SHOULD_NEVER_HAPPEN;
                free_template_item(item);
                zbx_vector_ptr_remove(items, i);
            }
            else
            {
                // 找到主项，将当前元素添加到主项的依赖项列表中
                master = (zbx_template_item_t *)template_index.values[index];
                zbx_vector_ptr_append(&master->dependent_items, item);
            }
        }
    }

    // 销毁template_index
    zbx_vector_ptr_destroy(&template_index);

    // 记录函数调用结束日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

				zbx_vector_ptr_append(&master->dependent_items, item);
			}
		}
	}

	zbx_vector_ptr_destroy(&template_index);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_items                                            *
 *                                                                            *
 * Purpose: copy template items to host                                       *
 *                                                                            *
 * Parameters: hostid      - [IN] host id                                     *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个模板 ID 数组（templateids）中复制模板项（items）和相关数据，包括 LLDP 规则、条件 ID、公式、应用和发现原型等。这个过程涉及到向数据库中保存和更新数据，以及预处理模板项。最后，清理不再使用的内存空间。
 ******************************************************************************/
// 定义一个名为 DBcopy_template_items 的 void 类型函数，接收两个参数：一个 zbx_uint64_t 类型的 hostid 和一个 const zbx_vector_uint64_t 类型的指针 templateids。
void DBcopy_template_items(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "DBcopy_template_items";

	// 创建两个 zbx_vector_ptr_t 类型的变量：items 和 lld_rules，用于存储数据
	zbx_vector_ptr_t items, lld_rules;

	// 定义一个整型变量 new_conditions，用于存储新的条件数量
	int new_conditions = 0;

	// 打印调试日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建 items 和 lld_rules 向量
	zbx_vector_ptr_create(&items);
	zbx_vector_ptr_create(&lld_rules);

	// 调用 get_template_items 函数，根据 hostid 和 templateids 获取模板项，并将结果存储在 items 向量中
	get_template_items(hostid, templateids, &items);

	// 如果 items 向量中的元素数量为 0，则退出函数
	if (0 == items.values_num)
		goto out;

	// 调用 get_template_lld_rule_map 函数，根据 items 获取模板 LLDP 规则映射，并将结果存储在 lld_rules 向量中
	get_template_lld_rule_map(&items, &lld_rules);

	// 调用 calculate_template_lld_rule_conditionids 函数，计算 LLDP 规则的条件 ID，并将结果存储在 new_conditions 变量中
	new_conditions = calculate_template_lld_rule_conditionids(&lld_rules);

	// 更新模板 LLDP 规则公式
	update_template_lld_rule_formulas(&items, &lld_rules);

	// 链接模板相关项
	link_template_dependent_items(&items);

	// 保存模板项到数据库
	save_template_items(hostid, &items);

	// 保存模板 LLDP 规则到数据库，并将新的条件 ID 一起保存
	save_template_lld_rules(&items, &lld_rules, new_conditions);

	// 保存模板项应用到数据库
	save_template_item_applications(&items);

	// 保存模板发现原型到数据库
	save_template_discovery_prototypes(hostid, &items);

	// 调用 copy_template_items_preproc 函数，预处理模板项
	copy_template_items_preproc(templateids, &items);

out:
	// 清理 lld_rules 向量，释放 LLDP 规则映射内存
	zbx_vector_ptr_clear_ext(&lld_rules, (zbx_clean_func_t)free_lld_rule_map);
	zbx_vector_ptr_destroy(&lld_rules);

	// 清理 items 向量，释放模板项内存
	zbx_vector_ptr_clear_ext(&items, (zbx_clean_func_t)free_template_item);
	zbx_vector_ptr_destroy(&items);

	// 打印调试日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

