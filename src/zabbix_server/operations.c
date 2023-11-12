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
#include "comms.h"
#include "db.h"
#include "log.h"
#include "dbcache.h"

#include "operations.h"

/******************************************************************************
 *                                                                            *
 * Function: select_discovered_host                                           *
 *                                                                            *
 * Purpose: select hostid of discovered host                                  *
 *                                                                            *
 * Parameters: dhostid - discovered host id                                   *
 *                                                                            *
 * Return value: hostid - existing hostid, 0 - if not found                   *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static zbx_uint64_t	select_discovered_host(const DB_EVENT *event)
{
	const char	*__function_name = "select_discovered_host";
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	hostid = 0, proxy_hostid;
	char		*sql = NULL, *ip_esc;
/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的事件对象（event->object）和事件ID（event->objectid），查询对应的主机ID（hostid）。其中，针对DHOST和DSERVICE对象，通过构造查询语句根据IP地址查找主机；针对ZABBIX_ACTIVE对象，直接查询注册的主机ID。最后，释放内存并返回主机ID。
 ******************************************************************************/
// 定义日志级别，DEBUG表示调试信息
zabbix_log(LOG_LEVEL_DEBUG, "In %s() eventid:" ZBX_FS_UI64, __function_name, event->eventid);

	switch (event->object)
	{
		case EVENT_OBJECT_DHOST:
		case EVENT_OBJECT_DSERVICE:
			result = DBselect(
					"select dr.proxy_hostid,ds.ip"
					" from drules dr,dchecks dc,dservices ds"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds.%s=" ZBX_FS_UI64,
					EVENT_OBJECT_DSERVICE == event->object ? "dserviceid" : "dhostid",
					event->objectid);

        // 如果没有查询到结果，释放资源并跳出循环
        if (NULL == (row = DBfetch(result)))
        {
            DBfree_result(result);
            goto exit;
        }

        // 解析查询结果
        ZBX_DBROW2UINT64(proxy_hostid, row[0]);
        ip_esc = DBdyn_escape_string(row[1]);
        DBfree_result(result);

        // 构造查询语句，根据IP地址查找主机
        sql = zbx_dsprintf(sql,
                "select h.hostid"
                " from hosts h,interface i"
                " where h.hostid=i.hostid"
                    " and i.ip='%s'"
                    " and i.useip=1"
                    " and h.status in (%d,%d)"
                    " and h.proxy_hostid%s"
                " order by i.hostid",
                ip_esc,
                HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
                DBsql_id_cmp(proxy_hostid));

        // 释放ip_esc内存
        zbx_free(ip_esc);
        break;
    // 处理ZABBIX_ACTIVE对象
    case EVENT_OBJECT_ZABBIX_ACTIVE:
        sql = zbx_dsprintf(sql,
                "select h.hostid"
                " from hosts h,autoreg_host a"
                " where h.host=a.host"
                    " and a.autoreg_hostid=" ZBX_FS_UI64
                    " and h.status in (%d,%d)",
                event->objectid,
                HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);
        break;
    // 默认情况，直接跳出循环
    default:
        goto exit;
}

// 执行查询语句
result = DBselectN(sql, 1);

	zbx_free(sql);

	if (NULL != (row = DBfetch(result)))
		ZBX_STR2UINT64(hostid, row[0]);
	DBfree_result(result);
exit:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():" ZBX_FS_UI64, __function_name, hostid);

	return hostid;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：根据给定的hostid，查询与之关联的groupid，然后将这些groupid插入到指定的vector中。如果vector不为空，则执行数据库插入操作，创建新的hostgroup。
 ******************************************************************************/
static void add_discovered_host_groups(zbx_uint64_t hostid, zbx_vector_uint64_t *groupids)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "add_discovered_host_groups";

    // 声明一些变量
    DB_RESULT	result;
    DB_ROW		row;
    zbx_uint64_t	groupid;
    char		*sql = NULL;
    size_t		sql_alloc = 256, sql_offset = 0;
    int		i;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 分配内存用于存储SQL语句
    sql = (char *)zbx_malloc(sql, sql_alloc);

    // 构造SQL查询语句，查询hosts_groups表中hostid等于给定hostid的记录
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                "select groupid"
                " from hosts_groups"
                " where hostid=" ZBX_FS_UI64
                    " and",
                hostid);
    // 添加条件，查询包含给定groupid的记录
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids->values, groupids->values_num);

    // 执行查询
    result = DBselect("%s", sql);

    // 释放SQL语句内存
    zbx_free(sql);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 将字符串转换为uint64类型
        ZBX_STR2UINT64(groupid, row[0]);

        // 检查给定的groupid是否已经在groupids vector中
        if (FAIL == (i = zbx_vector_uint64_search(groupids, groupid, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
        {
            // 这种情况不应该发生，继续处理下一个记录
            THIS_SHOULD_NEVER_HAPPEN;
            continue;
        }

        // 从vector中移除该groupid
        zbx_vector_uint64_remove_noorder(groupids, i);
    }
    // 释放查询结果
    DBfree_result(result);

    // 如果groupids vector不为空，说明有新的hostgroup需要创建
    if (0 != groupids->values_num)
    {
        zbx_uint64_t	hostgroupid;
        zbx_db_insert_t	db_insert;

        // 获取hosts_groups表中最大的hostgroupid
        hostgroupid = DBget_maxid_num("hosts_groups", groupids->values_num);

        // 准备数据库插入操作
        zbx_db_insert_prepare(&db_insert, "hosts_groups", "hostgroupid", "hostid", "groupid", NULL);

        // 对groupids vector进行排序
        zbx_vector_uint64_sort(groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

        // 遍历groupids vector，执行数据库插入操作
        for (i = 0; i < groupids->values_num; i++)
        {
            zbx_db_insert_add_values(&db_insert, hostgroupid++, hostid, groupids->values[i]);
        }

        // 执行数据库插入操作
        zbx_db_insert_execute(&db_insert);
        // 清理数据库插入操作
        zbx_db_insert_clean(&db_insert);
    }

    // 记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: add_discovered_host                                              *
 *                                                                            *
 * Purpose: add discovered host if it was not added already                   *
 *                                                                            *
 * Parameters: dhostid - discovered host id                                   *
 *                                                                            *
 * Return value: hostid - new/existing hostid                                 *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
static zbx_uint64_t	add_discovered_host(const DB_EVENT *event)
{
	const char		*__function_name = "add_discovered_host";

	DB_RESULT		result;
	DB_RESULT		result2;
	DB_ROW			row;
	DB_ROW			row2;
	zbx_uint64_t		dhostid, hostid = 0, proxy_hostid;
	char			*host = NULL, *host_esc, *host_unique;
	unsigned short		port;
	zbx_vector_uint64_t	groupids;
	unsigned char		svc_type, interface_type;
	zbx_config_t		cfg;
	zbx_db_insert_t		db_insert;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() eventid:" ZBX_FS_UI64, __function_name, event->eventid);

    // 创建一个 vector 用于存储组 ID
    zbx_vector_uint64_create(&groupids);

    // 获取配置信息
    zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_DISCOVERY_GROUPID | ZBX_CONFIG_FLAGS_DEFAULT_INVENTORY_MODE);

    // 如果发现的主机或服务的配置未定义，打印警告信息并退出
    if (ZBX_DISCOVERY_GROUPID_UNDEFINED == cfg.discovery_groupid)
    {
        zabbix_log(LOG_LEVEL_WARNING, "cannot add discovered host: group for discovered hosts is not defined");
        goto clean;
    }

    // 向 vector 中添加组 ID
    zbx_vector_uint64_append(&groupids, cfg.discovery_groupid);

	if (EVENT_OBJECT_DHOST == event->object || EVENT_OBJECT_DSERVICE == event->object)
	{
		if (EVENT_OBJECT_DHOST == event->object)
		{
			result = DBselect(
					"select ds.dhostid,dr.proxy_hostid,ds.ip,ds.dns,ds.port,dc.type"
					" from drules dr,dchecks dc,dservices ds"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds.dhostid=" ZBX_FS_UI64
					" order by ds.dserviceid",
					event->objectid);
		}
		else
		{
			result = DBselect(
					"select ds.dhostid,dr.proxy_hostid,ds.ip,ds.dns,ds.port,dc.type"
					" from drules dr,dchecks dc,dservices ds,dservices ds1"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds1.dhostid=ds.dhostid"
						" and ds1.dserviceid=" ZBX_FS_UI64
					" order by ds.dserviceid",
					event->objectid);
		}

		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(dhostid, row[0]);
			ZBX_DBROW2UINT64(proxy_hostid, row[1]);
			svc_type = (unsigned char)atoi(row[5]);

                // 根据服务类型进行不同处理
                switch (svc_type)
                {
                    case SVC_AGENT:
                        port = (unsigned short)atoi(row[4]);
                        interface_type = INTERFACE_TYPE_AGENT;
                        break;
                    case SVC_SNMPv1:
                    case SVC_SNMPv2c:
                    case SVC_SNMPv3:
                        port = (unsigned short)atoi(row[4]);
                        interface_type = INTERFACE_TYPE_SNMP;
                        break;
                    default:
                        port = ZBX_DEFAULT_AGENT_PORT;
                        interface_type = INTERFACE_TYPE_AGENT;
                }

                // 如果没有找到主机，则新建主机
                if (0 == hostid)
                {
                    result2 = DBselect(
                            "select distinct h.hostid"
                            " from hosts h,interface i,dservices ds"
                            " where h.hostid=i.hostid"
                            " and i.ip=ds.ip"
                            " and h.status in (%d,%d)"
                            " and h.proxy_hostid%s"
                            " and ds.dhostid=" ZBX_FS_UI64
                            " order by h.hostid",
                            HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
                            DBsql_id_cmp(proxy_hostid), dhostid);

				if (NULL != (row2 = DBfetch(result2)))
					ZBX_STR2UINT64(hostid, row2[0]);

				DBfree_result(result2);
			}

			if (0 == hostid)
			{
				hostid = DBget_maxid("hosts");

				/* for host uniqueness purposes */
				host = zbx_strdup(host, '\0' != *row[3] ? row[3] : row[2]);

				make_hostname(host);	/* replace not-allowed symbols */
				host_unique = DBget_unique_hostname_by_sample(host);
				zbx_free(host);

				zbx_db_insert_prepare(&db_insert, "hosts", "hostid", "proxy_hostid", "host", "name",
						NULL);
				zbx_db_insert_add_values(&db_insert, hostid, proxy_hostid, host_unique, host_unique);
				zbx_db_insert_execute(&db_insert);
				zbx_db_insert_clean(&db_insert);

				if (HOST_INVENTORY_DISABLED != cfg.default_inventory_mode)
					DBadd_host_inventory(hostid, cfg.default_inventory_mode);

                    // 添加接口
                    DBadd_interface(hostid, interface_type, 1, row[2], row[3], port);

				zbx_free(host_unique);

				add_discovered_host_groups(hostid, &groupids);
			}
			else
			{
				DBadd_interface(hostid, interface_type, 1, row[2], row[3], port);
			}
		}
		DBfree_result(result);
	}
	else if (EVENT_OBJECT_ZABBIX_ACTIVE == event->object)
	{
		result = DBselect(
				"select proxy_hostid,host,listen_ip,listen_dns,listen_port"
				" from autoreg_host"
				" where autoreg_hostid=" ZBX_FS_UI64,
				event->objectid);

		if (NULL != (row = DBfetch(result)))
		{
			char		*sql = NULL;
			zbx_uint64_t	host_proxy_hostid;

			ZBX_DBROW2UINT64(proxy_hostid, row[0]);
			host_esc = DBdyn_escape_field("hosts", "host", row[1]);
			port = (unsigned short)atoi(row[4]);

			result2 = DBselect(
					"select null"
					" from hosts"
					" where host='%s'"
						" and status=%d",
					host_esc, HOST_STATUS_TEMPLATE);

			if (NULL != (row2 = DBfetch(result2)))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot add discovered host \"%s\":"
						" template with the same name already exists", row[1]);
				DBfree_result(result2);
				goto out;
			}
			DBfree_result(result2);

			sql = zbx_dsprintf(sql,
					"select hostid,proxy_hostid"
					" from hosts"
					" where host='%s'"
						" and flags<>%d"
						" and status in (%d,%d)"
					" order by hostid",
					host_esc, ZBX_FLAG_DISCOVERY_PROTOTYPE,
					HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);

			result2 = DBselectN(sql, 1);

			zbx_free(sql);

			if (NULL == (row2 = DBfetch(result2)))
			{
				hostid = DBget_maxid("hosts");

				zbx_db_insert_prepare(&db_insert, "hosts", "hostid", "proxy_hostid", "host", "name",
						NULL);
				zbx_db_insert_add_values(&db_insert, hostid, proxy_hostid, row[1], row[1]);
				zbx_db_insert_execute(&db_insert);
				zbx_db_insert_clean(&db_insert);

				if (HOST_INVENTORY_DISABLED != cfg.default_inventory_mode)
					DBadd_host_inventory(hostid, cfg.default_inventory_mode);

				DBadd_interface(hostid, INTERFACE_TYPE_AGENT, 1, row[2], row[3], port);

				add_discovered_host_groups(hostid, &groupids);
			}
			else
			{
				ZBX_STR2UINT64(hostid, row2[0]);
				ZBX_DBROW2UINT64(host_proxy_hostid, row2[1]);

				if (host_proxy_hostid != proxy_hostid)
				{
					DBexecute("update hosts"
							" set proxy_hostid=%s"
							" where hostid=" ZBX_FS_UI64,
							DBsql_id_ins(proxy_hostid), hostid);
				}

				DBadd_interface(hostid, INTERFACE_TYPE_AGENT, 1, row[2], row[3], port);
			}
			DBfree_result(result2);
out:
			zbx_free(host_esc);
		}
		DBfree_result(result);
	}
clean:
	zbx_config_clean(&cfg);

	zbx_vector_uint64_destroy(&groupids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return hostid;
}

/******************************************************************************
 *                                                                            *
 * Function: is_discovery_or_auto_registration                                *
 *                                                                            *
 * Purpose: checks if the event is discovery or auto registration event       *
 *                                                                            *
 * Return value: SUCCEED - it's discovery or auto registration event          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是判断一个数据库事件（DB_EVENT）的类型，根据事件来源和事件对象的不同，判断是否为发现或自动注册事件。如果满足条件，返回成功（SUCCEED），否则返回失败（FAIL）。
 ******************************************************************************/
// 定义一个静态函数，用于判断事件类型是否为发现或自动注册
static int	is_discovery_or_auto_registration(const DB_EVENT *event)
{
	if (event->source == EVENT_SOURCE_DISCOVERY && (event->object == EVENT_OBJECT_DHOST ||
			event->object == EVENT_OBJECT_DSERVICE))
	{
		return SUCCEED;
	}

	if (event->source == EVENT_SOURCE_AUTO_REGISTRATION && event->object == EVENT_OBJECT_ZABBIX_ACTIVE)
		return SUCCEED;

	// 如果以上条件都不满足，返回失败（FAIL）
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: op_host_add                                                      *
 *                                                                            *
 * Purpose: add discovered host                                               *
 *                                                                            *
 * Parameters: trigger - trigger data                                         *
 *             action  - action data                                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个DB_EVENT结构体的指针作为参数，判断该事件是否为发现或自动注册的新主机，如果是，则添加到主机列表中。在执行过程中，使用zabbix_log记录日志，表示函数的执行状态。
 ******************************************************************************/
void	op_host_add(const DB_EVENT *event)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "op_host_add";

	// 使用zabbix_log记录日志，表示函数开始执行，日志级别为DEBUG，输出函数名
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断is_discovery_or_auto_registration(event)函数返回值是否为FAIL，如果是，则直接返回，不再执行后续代码
	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	// 如果判断结果为真，说明是发现或自动注册的新主机，执行add_discovered_host(event)函数，添加新主机
	add_discovered_host(event);

	// 使用zabbix_log记录日志，表示函数执行完毕，日志级别为DEBUG，输出函数名
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_del                                                      *
 *                                                                            *
 * Purpose: delete host                                                       *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除具有特定原型的主机。函数 op_host_del 接收一个 DB_EVENT 类型的指针作为参数，通过判断事件类型、获取 hostid、创建 hostids 向量、删除具有特定原型的主机和销毁向量等操作完成主机删除任务。在整个过程中，使用了调试日志记录函数进入和退出情况。
 ******************************************************************************/
// 定义一个名为 op_host_del 的函数，参数为一个指向 DB_EVENT 结构的指针
void op_host_del(const DB_EVENT *event)
{
    // 定义一个字符串指针，用于存储函数名
    const char *__function_name = "op_host_del";

    // 定义一个整数向量指针，用于存储 hostids
    zbx_vector_uint64_t hostids;

    // 定义一个 zbx_uint64_t 类型的变量，用于存储 hostid
    zbx_uint64_t hostid;

    // 使用 zabbix_log 记录调试信息，表示进入 op_host_del 函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断参数是否为 discovery 或 auto_registration，如果不是则返回
    if (FAIL == is_discovery_or_auto_registration(event))
        return;

    // 从事件中获取 hostid，如果为 0 则返回
    if (0 == (hostid = select_discovered_host(event)))
        return;

    // 创建一个 zbx_vector_uint64 类型的对象，用于存储 hostids
    zbx_vector_uint64_create(&hostids);

    // 将 hostid 添加到 hostids 向量中
    zbx_vector_uint64_append(&hostids, hostid);

    // 使用 DBdelete_hosts_with_prototypes 函数删除具有特定原型的主机
    DBdelete_hosts_with_prototypes(&hostids);

    // 销毁 hostids 向量
    zbx_vector_uint64_destroy(&hostids);

    // 使用 zabbix_log 记录调试信息，表示结束 op_host_del 函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_enable                                                   *
 *                                                                            *
 * Purpose: enable discovered                                                 *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个DB_EVENT类型的指针作为参数，判断该事件是否为发现或自动注册事件，如果是则将主机添加到数据库，并更新主机的status为HOST_STATUS_MONITORED。整个过程通过多个函数和SQL语句完成。
 ******************************************************************************/
// 定义一个函数void op_host_enable(const DB_EVENT *event)，接收一个DB_EVENT类型的指针作为参数
void	op_host_enable(const DB_EVENT *event)
{
	// 定义一个字符串指针__function_name，用于存储函数名
	const char	*__function_name = "op_host_enable";
	// 定义一个zbx_uint64_t类型的变量hostid，用于存储主机ID
	zbx_uint64_t	hostid;

	// 使用zabbix_log记录调试信息，表示进入op_host_enable函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 调用is_discovery_or_auto_registration函数，判断event是否为发现或自动注册事件，如果是则继续执行，否则返回
	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	// 调用add_discovered_host函数，添加发现的主机到数据库，如果添加成功则返回主机ID，否则返回0
	if (0 == (hostid = add_discovered_host(event)))
		return;

	DBexecute(
			"update hosts"
			" set status=%d"
			" where hostid=" ZBX_FS_UI64,
			HOST_STATUS_MONITORED,
			hostid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_disable                                                  *
 *                                                                            *
 * Purpose: disable host                                                      *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是禁用一个被发现的主机。当调用此函数时，首先判断传入的事件是否为发现或自动注册类型，然后添加发现的主机到数据库中。接着执行SQL更新语句，将主机状态改为未监控。最后记录调试信息表示函数执行完毕。
 ******************************************************************************/
// 定义一个函数void op_host_disable(const DB_EVENT *event)，接收一个DB_EVENT结构体的指针作为参数
void	op_host_disable(const DB_EVENT *event)
{
	// 定义一个字符串指针__function_name，用于存储函数名
	const char	*__function_name = "op_host_disable";
	// 定义一个zbx_uint64_t类型的变量hostid，用于存储主机ID
	zbx_uint64_t	hostid;

	// 使用zabbix_log记录调试信息，显示当前函数名
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 调用is_discovery_or_auto_registration函数判断事件是否为发现或自动注册类型，如果失败则直接返回
	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	// 调用add_discovered_host函数添加发现的主机，如果添加失败则直接返回
	if (0 == (hostid = add_discovered_host(event)))
		return;

	// 使用DBexecute执行SQL更新语句，将主机状态改为未监控
	DBexecute(
			"update hosts"
			" set status=%d"
			" where hostid=" ZBX_FS_UI64,
			HOST_STATUS_NOT_MONITORED,
			hostid);

	// 使用zabbix_log记录调试信息，显示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: op_host_inventory_mode                                           *
 *                                                                            *
 * Purpose: sets host inventory mode                                          *
 *                                                                            *
 * Parameters: event          - [IN] the source event                         *
 *             inventory_mode - [IN] the new inventory mode, see              *
 *                              HOST_INVENTORY_ defines                       *
 *                                                                            *
 * Comments: This function does not allow disabling host inventory - only     *
 *           setting manual or automatic host inventory mode is supported.    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的DB_EVENT结构体指针和库存模式，执行一系列操作，包括判断事件类型、添加发现的主机、设置主机库存信息等。输出调试日志表示函数的执行过程。
 ******************************************************************************/
void	op_host_inventory_mode(const DB_EVENT *event, int inventory_mode)
{
    // 定义一个名为__function_name的常量字符指针，用于记录当前函数名
    const char	*__function_name = "op_host_inventory_mode";
    // 定义一个名为hostid的zbx_uint64_t类型变量，用于存储主机ID
    zbx_uint64_t	hostid;

    // 使用zabbix_log记录调试日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 调用is_discovery_or_auto_registration函数，判断事件是否为发现或自动注册类型
    if (FAIL == is_discovery_or_auto_registration(event))
        // 如果判断为失败，直接返回，不再执行后续操作
        return;

    // 调用add_discovered_host函数，添加发现的主机
    if (0 == (hostid = add_discovered_host(event)))
        // 如果添加失败，直接返回，不再执行后续操作
        return;

    // 调用DBset_host_inventory函数，设置主机库存信息
    DBset_host_inventory(hostid, inventory_mode);

    // 使用zabbix_log记录调试日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: op_groups_add                                                    *
 *                                                                            *
 * Purpose: add groups to discovered host                                     *
 *                                                                            *
 * Parameters: event    - [IN] event data                                     *
 *             groupids - [IN] IDs of groups to add                           *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个 DB_EVENT 类型的指针和一个 zbx_vector_uint64_t 类型的指针，判断输入的事件类型是否为发现或自动注册类型，如果是，则将发现的主机添加到数据库中，并为该主机添加分组。最后输出调试日志，表示函数执行完毕。
 ******************************************************************************/
// 定义一个名为 op_groups_add 的函数，参数为一个指向 DB_EVENT 结构的指针和一个指向 zbx_vector_uint64_t 结构的指针。
void op_groups_add(const DB_EVENT *event, zbx_vector_uint64_t *groupids)
{
	// 定义一个名为 __function_name 的常量字符串，表示函数名
	const char *__function_name = "op_groups_add";
	// 定义一个名为 hostid 的 zbx_uint64_t 类型的变量，用于存储主机 ID
	zbx_uint64_t hostid;

	// 输出调试日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	add_discovered_host_groups(hostid, groupids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_groups_del                                                    *
 *                                                                            *
 * Purpose: delete groups from discovered host                                *
 *                                                                            *
 * Parameters: event    - [IN] event data                                     *
 *             groupids - [IN] IDs of groups to delete                        *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除一个已发现的主机及其所属的所有主机组。具体操作如下：
 *
 *1. 判断传入的事件是否为 discovery 或 auto_registration 类型，如果不是则返回。
 *2. 查询已发现的主机 ID。
 *3. 分配 SQL 语句内存。
 *4. 构造 SQL 语句，确保主机至少属于一个主机组。
 *5. 添加条件，筛选出指定主机 ID 且不属于指定主机组的主机组 ID。
 *6. 执行 SQL 查询，获取不属于指定主机组的主机组 ID 列表。
 *7. 判断查询结果是否为空，如果为空则警告提示，否则执行删除操作。
 *8. 释放 SQL 语句内存。
 *9. 打印日志，表示函数执行完毕。
 ******************************************************************************/
// 定义一个名为 op_groups_del 的 void 类型函数，传入参数为一个 DB_EVENT 类型的指针和一个 zbx_vector_uint64_t 类型的指针
void op_groups_del(const DB_EVENT *event, zbx_vector_uint64_t *groupids)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "op_groups_del";

	// 定义一个 DB_RESULT 类型的变量 result，用于存储数据库操作结果
	DB_RESULT result;
	// 定义一个 zbx_uint64_t 类型的变量 hostid，用于存储主机 ID
	zbx_uint64_t hostid;
	// 定义一个 char 类型的变量 sql，用于存储 SQL 语句
	char *sql = NULL;
	// 定义一个 size_t 类型的变量 sql_alloc，用于存储 SQL 语句分配的大小
	size_t sql_alloc = 256, sql_offset = 0;

	// 打印日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断是否为 discovery 或 auto_registration 事件，如果不是则返回
	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	// 查询已发现的主机 ID
	if (0 == (hostid = select_discovered_host(event)))
		return;

	// 分配 SQL 语句内存
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 确保主机至少属于一个主机组
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select groupid"
			" from hosts_groups"
			" where hostid=" ZBX_FS_UI64
				" and not",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids->values, groupids->values_num);

	result = DBselectN(sql, 1);

	if (NULL == DBfetch(result))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot remove host \"%s\" from all host groups:"
				" it must belong to at least one", zbx_host_string(hostid));
	}
	else
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"delete from hosts_groups"
				" where hostid=" ZBX_FS_UI64
					" and",
				hostid);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids->values, groupids->values_num);

		DBexecute("%s", sql);
	}
	DBfree_result(result);

	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_template_add                                                  *
 *                                                                            *
 * Purpose: link host with template                                           *
 *                                                                            *
 * Parameters: event           - [IN] event data                              *
 *             lnk_templateids - [IN] array of template IDs                   *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据输入的 DB_EVENT 结构信息和链接模板 ID 列表，添加发现的主机并将其与链接模板关联。如果添加失败，记录警告日志并释放错误信息。
 ******************************************************************************/
// 定义一个名为 op_template_add 的 void 类型函数，接收两个参数：一个指向 DB_EVENT 结构的指针 event 和一个指向 zbx_vector_uint64_t 结构的指针 lnk_templateids。
void op_template_add(const DB_EVENT *event, zbx_vector_uint64_t *lnk_templateids)
{
	// 定义一个名为 __function_name 的常量字符串，值为 "op_template_add"。
	const char *__function_name = "op_template_add";
	// 定义一个名为 hostid 的 zbx_uint64_t 类型变量，用于存储主机 ID。
	zbx_uint64_t hostid;
	// 定义一个名为 error 的字符指针，用于存储错误信息。
	char *error;

	// 使用 zabbix_log 函数记录调试日志，表示进入 op_template_add 函数。
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断 is_discovery_or_auto_registration 函数返回值是否为 FAIL，如果是，则直接返回。
	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	if (SUCCEED != DBcopy_template_elements(hostid, lnk_templateids, &error))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot link template(s) %s", error);
		zbx_free(error);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_template_del                                                  *
 *                                                                            *
 * Purpose: unlink and clear host from template                               *
 *                                                                            *
 * Parameters: event           - [IN] event data                              *
 *             lnk_templateids - [IN] array of template IDs                   *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
void	op_template_del(const DB_EVENT *event, zbx_vector_uint64_t *del_templateids)
{
	const char	*__function_name = "op_template_del";
	zbx_uint64_t	hostid;
	char		*error;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = select_discovered_host(event)))
		return;

	if (SUCCEED != DBdelete_template_elements(hostid, del_templateids, &error))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot unlink template: %s", error);
		zbx_free(error);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
