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
#include "dbcache.h"
#include "daemon.h"
#include "zbxserver.h"
#include "zbxself.h"
#include "preproc.h"
#include "../events.h"

#include "poller.h"

#include "checks_agent.h"
#include "checks_aggregate.h"
#include "checks_external.h"
#include "checks_internal.h"
#include "checks_simple.h"
#include "checks_snmp.h"
#include "checks_db.h"
#include "checks_ssh.h"
#include "checks_telnet.h"
#include "checks_java.h"
#include "checks_calculated.h"
#include "checks_http.h"
#include "../../libs/zbxcrypto/tls.h"
#include "zbxjson.h"
#include "zbxhttp.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: db_host_update_availability                                      *
 *                                                                            *
 * Purpose: write host availability changes into database                     *
 *                                                                            *
 * Parameters: ha    - [IN] the host availability data                        *
 *                                                                            *
 * Return value: SUCCEED - the availability changes were written into db      *
 *               FAIL    - no changes in availability data were detected      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是更新数据库中 host_availability 的信息。首先，判断是否成功将 host_availability 添加到 SQL 语句中。如果成功，则执行 SQL 语句并提交数据库事务。最后，释放 sql 指针占用的内存，并返回成功状态。如果添加 SQL 语句失败，则返回失败状态。
 ******************************************************************************/
// 定义一个名为 db_host_update_availability 的静态函数，参数为一个指向 zbx_host_availability_t 类型的指针
static int	db_host_update_availability(const zbx_host_availability_t *ha)
{
	// 定义一个字符指针 sql，用于存储 SQL 语句
	char	*sql = NULL;
	// 定义一个 size_t 类型的变量 sql_alloc，用于存储 sql 指针分配的大小
	size_t	sql_alloc = 0, sql_offset = 0;

	// 判断是否成功将 host_availability 添加到 SQL 语句中
	if (SUCCEED == zbx_sql_add_host_availability(&sql, &sql_alloc, &sql_offset, ha))
	{
		// 开始数据库事务
		DBbegin();
		// 执行 SQL 语句
		DBexecute("%s", sql);
		// 提交数据库事务
		DBcommit();

		// 释放 sql 指针占用的内存
		zbx_free(sql);

		// 返回成功状态
		return SUCCEED;
	}

	// 返回失败状态
	return FAIL;
}


/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的DC_HOST结构体、代理类型和zbx_host_availability_t指针，解析主机各项信息，并根据代理类型分别为代理设置可用性、错误信息、错误来源和禁用时间，最后返回成功。
 ******************************************************************************/
// 定义一个静态函数host_get_availability，接收三个参数：
// 1. 一个DC_HOST结构体的指针，用于获取主机信息；
// 2. 一个unsigned char类型的值，表示代理类型；
// 3. 一个zbx_host_availability_t类型的指针，用于存储主机可用性信息。
static int host_get_availability(const DC_HOST *dc_host, unsigned char agent, zbx_host_availability_t *ha)
{
	// 定义一个zbx_agent_availability_t类型的指针，用于存储代理的可用性信息。
	zbx_agent_availability_t *availability = &ha->agents[agent];

	// 为代理可用性信息设置标志位，表示代理状态。
	availability->flags = ZBX_FLAGS_AGENT_STATUS;

	// 根据代理类型进行switch分支判断。
	switch (agent)
	{
		// 代理类型为ZABBIX时，设置代理的可用性、错误信息、错误来源和禁用时间。
		case ZBX_AGENT_ZABBIX:
			availability->available = dc_host->available;
			availability->error = zbx_strdup(NULL, dc_host->error);
			availability->errors_from = dc_host->errors_from;
			availability->disable_until = dc_host->disable_until;
			break;

		// 代理类型为SNMP时，设置代理的可用性、错误信息、错误来源和禁用时间。
		case ZBX_AGENT_SNMP:
			availability->available = dc_host->snmp_available;
			availability->error = zbx_strdup(NULL, dc_host->snmp_error);
			availability->errors_from = dc_host->snmp_errors_from;
			availability->disable_until = dc_host->snmp_disable_until;
			break;

		// 代理类型为IPMI时，设置代理的可用性、错误信息、错误来源和禁用时间。
		case ZBX_AGENT_IPMI:
			availability->available = dc_host->ipmi_available;
			availability->error = zbx_strdup(NULL, dc_host->ipmi_error);
			availability->errors_from = dc_host->ipmi_errors_from;
			availability->disable_until = dc_host->ipmi_disable_until;
			break;

		// 代理类型为JMX时，设置代理的可用性、错误信息、错误来源和禁用时间。
		case ZBX_AGENT_JMX:
			availability->available = dc_host->jmx_available;
			availability->error = zbx_strdup(NULL, dc_host->jmx_error);
			availability->disable_until = dc_host->jmx_disable_until;
			availability->errors_from = dc_host->jmx_errors_from;
			break;

		// 默认情况，返回失败。
		default:
			return FAIL;
	}

	// 设置主机ID。
	ha->hostid = dc_host->hostid;

	// 返回成功。
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: host_set_availability                                            *
 *                                                                            *
 * Purpose: sets host availability data based on the specified agent type     *
 *                                                                            *
 * Parameters: dc_host      - [IN] the host                                   *
 *             type         - [IN] the agent type                             *
 *             availability - [IN] the host availability data                 *
 *                                                                            *
 * Return value: SUCCEED - the host availability data was set successfully    *
 *               FAIL    - failed to set host availability data,              *
 *                         invalid agent type was specified                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是设置主机的可用性信息。函数`host_set_availability`接收三个参数：`dc_host`是一个指向`DC_HOST`结构体的指针，`agent`表示主机代理的类型，`ha`是一个指向`zbx_host_availability_t`结构体的指针。根据`agent`的不同，函数会设置主机的相关信息，如可用性、错误信息、错误来源和禁用直到时间。最后，函数返回成功。
 ******************************************************************************/
// 定义一个函数，用于设置主机的可用性信息
static int host_set_availability(DC_HOST *dc_host, unsigned char agent, const zbx_host_availability_t *ha)
{
	// 定义一个指向zbx_agent_availability_t结构体的指针
	const zbx_agent_availability_t *availability = &ha->agents[agent];

	// 定义一些指针，用于存储主机的相关信息
	unsigned char *pavailable;
	int *perrors_from, *pdisable_until;
	char *perror;

	// 根据agent的不同，分别设置不同的主机信息
	switch (agent)
	{
		case ZBX_AGENT_ZABBIX:
			// 设置主机可用性信息
			pavailable = &dc_host->available;
			// 设置主机错误信息
			perror = dc_host->error;
			// 设置错误来源
			perrors_from = &dc_host->errors_from;
			// 设置主机禁用直到时间
			pdisable_until = &dc_host->disable_until;
			break;
		case ZBX_AGENT_SNMP:
			// 设置SNMP主机可用性信息
			pavailable = &dc_host->snmp_available;
			// 设置SNMP主机错误信息
			perror = dc_host->snmp_error;
			// 设置SNMP错误来源
			perrors_from = &dc_host->snmp_errors_from;
			// 设置SNMP主机禁用直到时间
			pdisable_until = &dc_host->snmp_disable_until;
			break;
		case ZBX_AGENT_IPMI:
			// 设置IPMI主机可用性信息
			pavailable = &dc_host->ipmi_available;
			// 设置IPMI主机错误信息
			perror = dc_host->ipmi_error;
			// 设置IPMI错误来源
			perrors_from = &dc_host->ipmi_errors_from;
			// 设置IPMI主机禁用直到时间
			pdisable_until = &dc_host->ipmi_disable_until;
			break;
		case ZBX_AGENT_JMX:
			// 设置JMX主机可用性信息
			pavailable = &dc_host->jmx_available;
			// 设置JMX主机错误信息
			perror = dc_host->jmx_error;
			pdisable_until = &dc_host->jmx_disable_until;
			perrors_from = &dc_host->jmx_errors_from;
			break;
		default:
			// 返回失败
			return FAIL;
	}

	// 判断availability结构体中是否包含主机可用性信息
	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_AVAILABLE))
		// 设置主机可用性
		*pavailable = availability->available;

	// 判断availability结构体中是否包含主机错误信息
	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_ERROR))
		// 复制主机错误信息到perror字符串中
		zbx_strlcpy(perror, availability->error, HOST_ERROR_LEN_MAX);

	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_ERRORS_FROM))
		*perrors_from = availability->errors_from;

	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_DISABLE_UNTIL))
		*pdisable_until = availability->disable_until;

	return SUCCEED;
}

static unsigned char	host_availability_agent_by_item_type(unsigned char type)
{
	switch (type)
	{
		case ITEM_TYPE_ZABBIX:
			return ZBX_AGENT_ZABBIX;
			break;
        // 当 type 为 ITEM_TYPE_SNMPv1、ITEM_TYPE_SNMPv2c 或 ITEM_TYPE_SNMPv3 时，返回 ZBX_AGENT_SNMP
        case ITEM_TYPE_SNMPv1:
        case ITEM_TYPE_SNMPv2c:
        case ITEM_TYPE_SNMPv3:
            return ZBX_AGENT_SNMP;
			break;
        // 当 type 为 ITEM_TYPE_IPMI 时，返回 ZBX_AGENT_IPMI
        case ITEM_TYPE_IPMI:
            return ZBX_AGENT_IPMI;
			break;
        // 当 type 为 ITEM_TYPE_JMX 时，返回 ZBX_AGENT_JMX
        case ITEM_TYPE_JMX:
            return ZBX_AGENT_JMX;
			break;
        // 当 type 不是以上任何一种情况时，返回 ZBX_AGENT_UNKNOWN
        default:
            return ZBX_AGENT_UNKNOWN;
    }
}


void	zbx_activate_item_host(DC_ITEM *item, zbx_timespec_t *ts)
{
	const char		*__function_name = "zbx_activate_item_host";
	zbx_host_availability_t	in, out;
	unsigned char		agent_type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() hostid:" ZBX_FS_UI64 " itemid:" ZBX_FS_UI64 " type:%d",
			__function_name, item->host.hostid, item->itemid, (int)item->type);

	zbx_host_availability_init(&in, item->host.hostid);
	zbx_host_availability_init(&out, item->host.hostid);

	if (ZBX_AGENT_UNKNOWN == (agent_type = host_availability_agent_by_item_type(item->type)))
		goto out;

	if (FAIL == host_get_availability(&item->host, agent_type, &in))
		goto out;

	if (FAIL == DChost_activate(item->host.hostid, agent_type, ts, &in.agents[agent_type], &out.agents[agent_type]))
		goto out;

	if (FAIL == db_host_update_availability(&out))
		goto out;

	host_set_availability(&item->host, agent_type, &out);

	if (HOST_AVAILABLE_TRUE == in.agents[agent_type].available)
	{
		zabbix_log(LOG_LEVEL_WARNING, "resuming %s checks on host \"%s\": connection restored",
				zbx_agent_type_string(item->type), item->host.host);
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "enabling %s checks on host \"%s\": host became available",
				zbx_agent_type_string(item->type), item->host.host);
	}
out:
	zbx_host_availability_clean(&out);
	zbx_host_availability_clean(&in);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

void	zbx_deactivate_item_host(DC_ITEM *item, zbx_timespec_t *ts, const char *error)
{
	const char		*__function_name = "zbx_deactivate_item_host";
	zbx_host_availability_t	in, out;
	unsigned char		agent_type;
	//（1）使用日志记录函数调用信息，包括函数名、主机ID、物品ID和类型。

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() hostid:" ZBX_FS_UI64 " itemid:" ZBX_FS_UI64 " type:%d",
			__function_name, item->host.hostid, item->itemid, (int)item->type);

	zbx_host_availability_init(&in, item->host.hostid);
	zbx_host_availability_init(&out,item->host.hostid);

	//（2）根据物品类型获取代理类型。

	if (ZBX_AGENT_UNKNOWN == (agent_type = host_availability_agent_by_item_type(item->type)))
		goto out;

	//（3）获取主机的状态。

	if (FAIL == host_get_availability(&item->host, agent_type, &in))
		goto out;

	//（4）调用DChost_deactivate函数，尝试停用主机上的代理。

	if (FAIL == DChost_deactivate(item->host.hostid, agent_type, ts, &in.agents[agent_type],
			&out.agents[agent_type], error))
	{
		goto out;
	}

	//（5）更新主机的状态。

	if (FAIL == db_host_update_availability(&out))
		goto out;

	host_set_availability(&item->host, agent_type, &out);

	//（6）检查代理是否存在错误，如果没有错误，则记录日志并等待一段时间。

	if (0 == in.agents[agent_type].errors_from)
	{
		zabbix_log(LOG_LEVEL_WARNING, "%s item \"%s\" on host \"%s\" failed:"
				" first network error, wait for %d seconds",
				zbx_agent_type_string(item->type), item->key_orig, item->host.host,
				out.agents[agent_type].disable_until - ts->sec);
	}
	else
	{
		//（7）如果代理之前存在错误，但当前可用，则记录日志。

		if (HOST_AVAILABLE_FALSE != in.agents[agent_type].available)
		{
			if (HOST_AVAILABLE_FALSE != out.agents[agent_type].available)
			{
				zabbix_log(LOG_LEVEL_WARNING, "%s item \"%s\" on host \"%s\" failed:"
						" another network error, wait for %d seconds",
						zbx_agent_type_string(item->type), item->key_orig, item->host.host,
						out.agents[agent_type].disable_until - ts->sec);
			}
			else
			{
				zabbix_log(LOG_LEVEL_WARNING, "temporarily disabling %s checks on host \"%s\":"
						" host unavailable",
						zbx_agent_type_string(item->type), item->host.host);
			}
		}
	}

	//（8）记录日志，输出代理的状态。
	zabbix_log(LOG_LEVEL_DEBUG, "%s() errors_from:%d available:%d", __function_name,
			out.agents[agent_type].errors_from, out.agents[agent_type].available);
out:
	zbx_host_availability_clean(&out);
	zbx_host_availability_clean(&in);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

static void	free_result_ptr(AGENT_RESULT *result)
{
	free_result(result);
	zbx_free(result);
}

/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个 AGENT_RESULT 类型的指针所指向的内存空间。
 *
 *代码解释：
 *1. 定义一个静态函数 free_result_ptr，表示这个函数在整个程序运行期间只被初始化一次，而非每次调用时重新创建。
/******************************************************************************
 * *
 *这个代码块的主要目的是根据传入的item类型，调用相应的函数获取item的价值，并将结果存储在result中。在这个过程中，会对不同的item类型进行超时控制，并在日志中记录相关操作。如果获取值失败，会设置相应的错误消息并记录日志。最后，返回获取值的结果。
 ******************************************************************************/
// 定义一个静态函数，用于获取不同类型item的价值
static int get_value(DC_ITEM *item, AGENT_RESULT *result, zbx_vector_ptr_t *add_results)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "get_value";
	int		res = FAIL;

	// 记录日志，表示进入函数，参数为函数名和item的key
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __function_name, item->key_orig);

	// 根据item的类型进行切换
	switch (item->type)
	{
		// 如果是ZABBIX类型，设置超时，调用get_value_agent获取值，然后取消超时
		case ITEM_TYPE_ZABBIX:
			zbx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_agent(item, result);
			zbx_alarm_off();
			break;
		// 如果是简单类型，使用自己的超时，调用get_value_simple获取值
		case ITEM_TYPE_SIMPLE:
			/* 简单检查使用自己的超时 */
			res = get_value_simple(item, result, add_results);
			break;
		// 如果是内部类型，调用get_value_internal获取值
		case ITEM_TYPE_INTERNAL:
			res = get_value_internal(item, result);
			break;
		// 如果是数据库监控类型，根据是否有unixodbc支持，调用get_value_db获取值
		case ITEM_TYPE_DB_MONITOR:
#ifdef HAVE_UNIXODBC
			res = get_value_db(item, result);
#else
			SET_MSG_RESULT(result,
					zbx_strdup(NULL, "Support for Database monitor checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		// 如果是聚合类型，调用get_value_aggregate获取值
		case ITEM_TYPE_AGGREGATE:
			res = get_value_aggregate(item, result);
			break;
		// 如果是外部类型，使用自己的超时，调用get_value_external获取值
		case ITEM_TYPE_EXTERNAL:
			/* 外部检查使用自己的超时 */
			res = get_value_external(item, result);
			break;
		// 如果是SSH类型，设置超时，调用get_value_ssh获取值，然后取消超时
		case ITEM_TYPE_SSH:
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
			zbx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_ssh(item, result);
			zbx_alarm_off();
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for SSH checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		// 如果是TELNET类型，设置超时，调用get_value_telnet获取值，然后取消超时
		case ITEM_TYPE_TELNET:
			zbx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_telnet(item, result);
			zbx_alarm_off();
			break;
		// 如果是计算类型，调用get_value_calculated获取值
		case ITEM_TYPE_CALCULATED:
			res = get_value_calculated(item, result);
			break;
		// 如果是HTTPAGENT类型，根据是否有libcurl支持，调用get_value_http获取值
		case ITEM_TYPE_HTTPAGENT:
#ifdef HAVE_LIBCURL
			res = get_value_http(item, result);
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for HTTP agent checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		// 默认情况，设置结果消息，返回配置错误
		default:
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Not supported item type:%d", item->type));
			res = CONFIG_ERROR;
	}

	if (SUCCEED != res)
	{
		if (!ISSET_MSG(result))
			SET_MSG_RESULT(result, zbx_strdup(NULL, ZBX_NOTSUPPORTED_MSG));

		zabbix_log(LOG_LEVEL_DEBUG, "Item [%s:%s] error: %s", item->host.host, item->key_orig, result->msg);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(res));

	return res;
}

/******************************************************************************
 * *
 *这段代码的主要目的是解析 JSON 格式的查询字段，并将解析结果存储在分配的内存中。在整个代码块中，首先判断查询字段是否为空，如果为空则直接复制原始字段。如果不为空，则解析 JSON 文件并逐个遍历数组中的元素。对于每个元素，提取其名称和值，并替换简单宏定义。最后，将解析得到的查询字段拼接成字符串并存储在分配的内存中。整个过程完成后，返回成功。
 ******************************************************************************/
// 定义一个静态函数，用于解析查询字段
static int	parse_query_fields(const DC_ITEM *item, char **query_fields)
{
	// 定义一个结构体，用于存储 JSON 解析的结果
	struct zbx_json_parse	jp_array, jp_object;
	char			name[MAX_STRING_LEN], value[MAX_STRING_LEN];
	const char		*member, *element = NULL;
	size_t			alloc_len, offset;

	if ('\0' == *item->query_fields_orig)
	{
		ZBX_STRDUP(*query_fields, item->query_fields_orig);
		return SUCCEED;
	}

	if (SUCCEED != zbx_json_open(item->query_fields_orig, &jp_array))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot parse query fields: %s", zbx_json_strerror());
		return FAIL;
	}

	if (NULL == (element = zbx_json_next(&jp_array, element)))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot parse query fields: array is empty");
		return FAIL;
	}

	do
	{
		char	*data = NULL;

		if (SUCCEED != zbx_json_brackets_open(element, &jp_object) ||
				NULL == (member = zbx_json_pair_next(&jp_object, NULL, name, sizeof(name))) ||
				NULL == zbx_json_decodevalue(member, value, sizeof(value), NULL))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot parse query fields: %s", zbx_json_strerror());
			return FAIL;
		}

		if (NULL == *query_fields && NULL == strchr(item->url, '?'))
			zbx_chrcpy_alloc(query_fields, &alloc_len, &offset, '?');
		else
			zbx_chrcpy_alloc(query_fields, &alloc_len, &offset, '&');

		data = zbx_strdup(data, name);
		substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL, &data,
				MACRO_TYPE_HTTP_RAW, NULL, 0);
		zbx_http_url_encode(data, &data);
		zbx_strcpy_alloc(query_fields, &alloc_len, &offset, data);
		zbx_chrcpy_alloc(query_fields, &alloc_len, &offset, '=');

		data = zbx_strdup(data, value);
		substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL, &data,
				MACRO_TYPE_HTTP_RAW, NULL, 0);
		zbx_http_url_encode(data, &data);
		zbx_strcpy_alloc(query_fields, &alloc_len, &offset, data);

		free(data);
	}
	while (NULL != (element = zbx_json_next(&jp_array, element)));

	return SUCCEED;
}
/******************************************************************************
 *                                                                            *
 * Function: get_values                                                       *
 *                                                                            *
 * Purpose: retrieve values of metrics from monitored hosts                   *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (ZBX_POLLER_TYPE_...)           *
 *                                                                            *
 * Return value: number of items processed                                    *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: processes single item at a time except for Java, SNMP items,     *
 *           see DCconfig_get_poller_items()                                  *
 *                                                                            *
 ******************************************************************************/
// 定义一个名为get_values的函数，该函数接受两个参数：一个表示轮询器类型的无符号字符和一个指向检查下一个时间戳的整型指针。
static int get_values(unsigned char poller_type, int *nextcheck)
{
    // 定义一个常量字符串，表示当前函数的名称
    const char *__function_name = "get_values";

    // 定义一个数组，用于存储DC配置中的所有轮询器项
    DC_ITEM items[MAX_POLLER_ITEMS];

    // 定义一个数组，用于存储轮询器项的结果
    AGENT_RESULT results[MAX_POLLER_ITEMS];

    // 定义一个数组，用于存储轮询器项的错误代码
    int errcodes[MAX_POLLER_ITEMS];

    // 定义一个结构体，用于存储时间戳
    zbx_timespec_t timespec;

    // 定义一个字符串数组，用于存储错误信息
	char			*port = NULL, error[ITEM_ERROR_LEN_MAX];

    // 定义一个整型变量，用于存储最后一个可用设备的状态
	int			i, num, last_available = HOST_AVAILABLE_UNKNOWN;

    // 定义一个结构体数组，用于存储轮询器项的结果
    zbx_vector_ptr_t add_results;

    // 记录当前函数的日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 获取DC配置中的所有轮询器项
    num = DCconfig_get_poller_items(poller_type, items);

    // 如果获取到的轮询器项数量为0，则返回检查下一个时间戳
    if (0 == num)
    {
        *nextcheck = DCconfig_get_poller_nextcheck(poller_type);
        goto exit;
    }

    // 初始化所有轮询器项的结果
    for (i = 0; i < num; i++)
    {
        init_result(&results[i]);
        errcodes[i] = SUCCEED;

        // 对轮询器项中的键进行处理
        ZBX_STRDUP(items[i].key, items[i].key_orig);
        if (SUCCEED != substitute_key_macros(&items[i].key, NULL, &items[i], NULL,
                    MACRO_TYPE_ITEM_KEY, error, sizeof(error)))
        {
            SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
            errcodes[i] = CONFIG_ERROR;
            continue;
        }

		switch (items[i].type)
		{
			case ITEM_TYPE_ZABBIX:
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
			case ITEM_TYPE_SNMPv3:
			case ITEM_TYPE_JMX:
				ZBX_STRDUP(port, items[i].interface.port_orig);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &port, MACRO_TYPE_COMMON, NULL, 0);
				if (FAIL == is_ushort(port, &items[i].interface.port))
				{
					SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "Invalid port number [%s]",
								items[i].interface.port_orig));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}
				break;
		}
        // 根据轮询器项的类型进行处理
        switch (items[i].type)
        {
            // 处理SNMPv3类型的轮询器项
            case ITEM_TYPE_SNMPv3:
                // 处理SNMPv3类型的轮询器项
                ZBX_STRDUP(items[i].snmpv3_securityname, items[i].snmpv3_securityname_orig);
                ZBX_STRDUP(items[i].snmpv3_authpassphrase, items[i].snmpv3_authpassphrase_orig);
                ZBX_STRDUP(items[i].snmpv3_privpassphrase, items[i].snmpv3_privpassphrase_orig);
                ZBX_STRDUP(items[i].snmpv3_contextname, items[i].snmpv3_contextname_orig);

                // 处理SNMPv3类型的轮询器项
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].snmpv3_securityname,
                                MACRO_TYPE_COMMON, NULL, 0);
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].snmpv3_authpassphrase,
                                MACRO_TYPE_COMMON, NULL, 0);
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].snmpv3_privpassphrase,
                                MACRO_TYPE_COMMON, NULL, 0);
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].snmpv3_contextname,
                                MACRO_TYPE_COMMON, NULL, 0);
                ZBX_FALLTHROUGH;
            // 处理SNMPv1和SNMPv2c类型的轮询器项
            case ITEM_TYPE_SNMPv1:
            case ITEM_TYPE_SNMPv2c:
                // 处理SNMPv1和SNMPv2c类型的轮询器项
                ZBX_STRDUP(items[i].snmp_community, items[i].snmp_community_orig);
                ZBX_STRDUP(items[i].snmp_oid, items[i].snmp_oid_orig);

                // 处理SNMPv1和SNMPv2c类型的轮询器项
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].snmp_community, MACRO_TYPE_COMMON, NULL, 0);
                if (SUCCEED != substitute_key_macros(&items[i].snmp_oid, &items[i].host.hostid, NULL,
                                NULL, MACRO_TYPE_SNMP_OID, error, sizeof(error)))
                {
                    SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
                    errcodes[i] = CONFIG_ERROR;
                    continue;
                }
                break;
            // 处理SSH类型的轮询器项
            case ITEM_TYPE_SSH:
                // 处理SSH类型的轮询器项
                ZBX_STRDUP(items[i].publickey, items[i].publickey_orig);
                ZBX_STRDUP(items[i].privatekey, items[i].privatekey_orig);

                // 处理SSH类型的轮询器项
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].publickey, MACRO_TYPE_COMMON, NULL, 0);
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].privatekey, MACRO_TYPE_COMMON, NULL, 0);
                ZBX_FALLTHROUGH;
            // 处理TELNET类型的轮询器项
            case ITEM_TYPE_TELNET:
            case ITEM_TYPE_DB_MONITOR:
                // 处理TELNET和DB_MONITOR类型的轮询器项
                substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &items[i],
                                NULL, NULL, &items[i].params, MACRO_TYPE_PARAMS_FIELD, NULL, 0);
                ZBX_FALLTHROUGH;
            // 处理SIMPLE类型的轮询器项
            case ITEM_TYPE_SIMPLE:
                // 处理SIMPLE类型的轮询器项
                items[i].username = zbx_strdup(items[i].username, items[i].username_orig);
                items[i].password = zbx_strdup(items[i].password, items[i].password_orig);

                // 处理SIMPLE类型的轮询器项
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
                break;
            // 处理JMX类型的轮询器项
            case ITEM_TYPE_JMX:
                // 处理JMX类型的轮询器项
                items[i].username = zbx_strdup(items[i].username, items[i].username_orig);
                items[i].password = zbx_strdup(items[i].password, items[i].password_orig);
                items[i].jmx_endpoint = zbx_strdup(items[i].jmx_endpoint, items[i].jmx_endpoint_orig);

                // 处理JMX类型的轮询器项
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &items[i],
						NULL, NULL, &items[i].jmx_endpoint, MACRO_TYPE_JMX_ENDPOINT, NULL, 0);
                break;
            // 处理HTTPAGENT类型的轮询器项
            case ITEM_TYPE_HTTPAGENT:
                // 处理HTTPAGENT类型的轮询器项
                ZBX_STRDUP(items[i].timeout, items[i].timeout_orig);
                ZBX_STRDUP(items[i].url, items[i].url_orig);
                ZBX_STRDUP(items[i].status_codes, items[i].status_codes_orig);
                ZBX_STRDUP(items[i].http_proxy, items[i].http_proxy_orig);
                ZBX_STRDUP(items[i].ssl_cert_file, items[i].ssl_cert_file_orig);
                ZBX_STRDUP(items[i].ssl_key_file, items[i].ssl_key_file_orig);
                ZBX_STRDUP(items[i].ssl_key_password, items[i].ssl_key_password_orig);
                ZBX_STRDUP(items[i].username, items[i].username_orig);
                ZBX_STRDUP(items[i].password, items[i].password_orig);

                // 处理HTTPAGENT类型的轮询器项
                substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
                                NULL, NULL, NULL, &items[i].timeout, MACRO_TYPE_COMMON, NULL, 0);
                substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
                                NULL, &items[i].url, MACRO_TYPE_HTTP_RAW, NULL, 0);

                // 处理HTTPAGENT类型的轮询器项
                if (SUCCEED != zbx_http_punycode_encode_url(&items[i].url))
                {
                    SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Cannot encode URL into punycode"));
                    errcodes[i] = CONFIG_ERROR;
                    continue;
                }

                // 处理HTTPAGENT类型的轮询器项
                if (FAIL == parse_query_fields(&items[i], &items[i].query_fields))
                {
                    SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Invalid query fields"));
                    errcodes[i] = CONFIG_ERROR;
                    continue;
                }

				switch (items[i].post_type)
				{
					case ZBX_POSTTYPE_XML:
						if (SUCCEED != substitute_macros_xml(&items[i].posts, &items[i], NULL,
								error, sizeof(error)))
						{
							SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "%s.", error));
							errcodes[i] = CONFIG_ERROR;
							continue;
						}
						break;
					case ZBX_POSTTYPE_JSON:
						substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host,
								&items[i], NULL, NULL, &items[i].posts,
								MACRO_TYPE_HTTP_JSON, NULL, 0);
						break;
					default:
						substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host,
								&items[i], NULL, NULL, &items[i].posts,
								MACRO_TYPE_HTTP_RAW, NULL, 0);
						break;
				}

				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].headers, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].status_codes, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].http_proxy, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].ssl_cert_file, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].ssl_key_file, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL, NULL,
						NULL, NULL, &items[i].ssl_key_password, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				break;
		}
	}

	zbx_free(port);

	zbx_vector_ptr_create(&add_results);

	/* retrieve item values */
	if (SUCCEED == is_snmp_type(items[0].type))
	{
#ifdef HAVE_NETSNMP
		/* SNMP checks use their own timeouts */
		get_values_snmp(items, results, errcodes, num);
#else
		for (i = 0; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Support for SNMP checks was not compiled in."));
			errcodes[i] = CONFIG_ERROR;
		}
#endif
	}
	else if (ITEM_TYPE_JMX == items[0].type)
	{
		zbx_alarm_on(CONFIG_TIMEOUT);
		get_values_java(ZBX_JAVA_GATEWAY_REQUEST_JMX, items, results, errcodes, num);
		zbx_alarm_off();
	}
	else if (1 == num)
	{
		if (SUCCEED == errcodes[0])
			errcodes[0] = get_value(&items[0], &results[0], &add_results);
	}
	else
		THIS_SHOULD_NEVER_HAPPEN;

	zbx_timespec(&timespec);

	/* process item values */
	for (i = 0; i < num; i++)
	{
		switch (errcodes[i])
		{
			case SUCCEED:
			case NOTSUPPORTED:
			case AGENT_ERROR:
				if (HOST_AVAILABLE_TRUE != last_available)
				{
					zbx_activate_item_host(&items[i], &timespec);
					last_available = HOST_AVAILABLE_TRUE;
				}
				break;
			case NETWORK_ERROR:
			case GATEWAY_ERROR:
			case TIMEOUT_ERROR:
				if (HOST_AVAILABLE_FALSE != last_available)
				{
					zbx_deactivate_item_host(&items[i], &timespec, results[i].msg);
					last_available = HOST_AVAILABLE_FALSE;
				}
				break;
			case CONFIG_ERROR:
				/* nothing to do */
				break;
			default:
				zbx_error("unknown response code returned: %d", errcodes[i]);
				THIS_SHOULD_NEVER_HAPPEN;
		}

		if (SUCCEED == errcodes[i])
		{
			if (0 == add_results.values_num)
			{
				items[i].state = ITEM_STATE_NORMAL;
				zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags,
						&results[i], &timespec, items[i].state, NULL);
			}
			else
			{
				/* vmware.eventlog item returns vector of AGENT_RESULT representing events */

				int		j;
				zbx_timespec_t	ts_tmp = timespec;

				for (j = 0; j < add_results.values_num; j++)
				{
					AGENT_RESULT	*add_result = (AGENT_RESULT *)add_results.values[j];

					if (ISSET_MSG(add_result))
					{
						items[i].state = ITEM_STATE_NOTSUPPORTED;
						zbx_preprocess_item_value(items[i].itemid, items[i].value_type,
								items[i].flags, NULL, &ts_tmp, items[i].state,
								add_result->msg);
					}
					else
					{
						items[i].state = ITEM_STATE_NORMAL;
						zbx_preprocess_item_value(items[i].itemid, items[i].value_type,
								items[i].flags, add_result, &ts_tmp, items[i].state,
								NULL);
					}

					/* ensure that every log item value timestamp is unique */
					if (++ts_tmp.ns == 1000000000)
					{
						ts_tmp.sec++;
						ts_tmp.ns = 0;
					}
				}
			}
		}
		else if (NOTSUPPORTED == errcodes[i] || AGENT_ERROR == errcodes[i] || CONFIG_ERROR == errcodes[i])
		{
			items[i].state = ITEM_STATE_NOTSUPPORTED;
			zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags, NULL, &timespec,
					items[i].state, results[i].msg);
		}

		DCpoller_requeue_items(&items[i].itemid, &items[i].state, &timespec.sec, &errcodes[i], 1, poller_type,
				nextcheck);

		zbx_free(items[i].key);

		switch (items[i].type)
		{
			case ITEM_TYPE_SNMPv3:
				zbx_free(items[i].snmpv3_securityname);
				zbx_free(items[i].snmpv3_authpassphrase);
				zbx_free(items[i].snmpv3_privpassphrase);
				zbx_free(items[i].snmpv3_contextname);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
				zbx_free(items[i].snmp_community);
				zbx_free(items[i].snmp_oid);
				break;
			case ITEM_TYPE_HTTPAGENT:
				zbx_free(items[i].timeout);
				zbx_free(items[i].url);
				zbx_free(items[i].query_fields);
				zbx_free(items[i].status_codes);
				zbx_free(items[i].http_proxy);
				zbx_free(items[i].ssl_cert_file);
				zbx_free(items[i].ssl_key_file);
				zbx_free(items[i].ssl_key_password);
				zbx_free(items[i].username);
				zbx_free(items[i].password);
				break;
			case ITEM_TYPE_SSH:
				zbx_free(items[i].publickey);
				zbx_free(items[i].privatekey);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_TELNET:
			case ITEM_TYPE_DB_MONITOR:
			case ITEM_TYPE_SIMPLE:
				zbx_free(items[i].username);
				zbx_free(items[i].password);
				break;
			case ITEM_TYPE_JMX:
				zbx_free(items[i].username);
				zbx_free(items[i].password);
				zbx_free(items[i].jmx_endpoint);
				break;
		}

		free_result(&results[i]);
	}

	zbx_preprocessor_flush();
	zbx_vector_ptr_clear_ext(&add_results, (zbx_mem_free_func_t)free_result_ptr);
	zbx_vector_ptr_destroy(&add_results);

	DCconfig_clean_items(items, NULL, num);
exit:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, num);

	return num;
}

ZBX_THREAD_ENTRY(poller_thread, args)
{
	// 定义一些变量，用于记录程序运行状态和时间信息
	int		nextcheck, sleeptime = -1, processed = 0, old_processed = 0;
	double		sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t		last_stat_time;
	unsigned char	poller_type;

	// 定义一个宏，表示状态更新间隔时间，单位为秒
	#define	STAT_INTERVAL	5

	// 解析传入的参数，获取 poller_type、process_type、server_num 和 process_num
	poller_type = *(unsigned char *)((zbx_thread_args_t *)args)->args;
	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 打印日志，表示程序启动
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自我监控计数器，表示程序处于忙碌状态
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 如果程序支持 Net-SNMP，则初始化 SNMP 库
	#ifdef HAVE_NETSNMP
	if (ZBX_POLLER_TYPE_NORMAL == poller_type || ZBX_POLLER_TYPE_UNREACHABLE == poller_type)
		zbx_init_snmp();
	#endif

	// 初始化 TLS 库
	#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_child();
	#endif

	// 设置进程标题，表示程序正在连接数据库
	zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	// 连接数据库
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	// 循环执行以下操作，直到程序退出
	while (ZBX_IS_RUNNING())
	{
		// 获取当前时间，并更新环境变量
		sec = zbx_time();
		zbx_update_env(sec);

		// 如果 sleeptime 不为零，表示程序需要休眠一段时间
		if (0 != sleeptime)
		{
			// 设置进程标题，表示程序正在获取数据
			zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, old_processed,
					old_total_sec);
		}

		// 获取数据，并更新 processed 和 total_sec 变量
		processed += get_values(poller_type, &nextcheck);
		total_sec += zbx_time() - sec;

		// 计算休眠时间
		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

		// 如果 sleeptime 不为零或当前时间距离上次状态更新时间超过 STAT_INTERVAL 秒，则更新状态信息
		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, processed, total_sec);
			}
			else
			{
				zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, idle %d sec]",
					get_process_type_string(process_type), process_num, processed, total_sec,
					sleeptime);
				old_processed = processed;
				old_total_sec = total_sec;
			}
			processed = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		zbx_sleep_loop(sleeptime);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
