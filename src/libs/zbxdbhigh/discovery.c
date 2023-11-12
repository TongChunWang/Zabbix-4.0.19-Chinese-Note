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
#include "events.h"
#include "discovery.h"

/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的 dcheckid 和 value，查询数据库中对应的数据记录，并返回查询结果。该函数用于在 Zabbix 监控系统中获取指定主机的信息。
 ******************************************************************************/
// 定义一个名为 discovery_get_dhost_by_value 的函数，接收两个参数：zbx_uint64_t 类型的 dcheckid 和 const char *类型的 value
static DB_RESULT	discovery_get_dhost_by_value(zbx_uint64_t dcheckid, const char *value)
{
	// 定义一个名为 result 的 DB_RESULT 类型的变量，用于存储数据库操作的结果
	DB_RESULT	result;
	// 定义一个名为 value_esc 的字符指针，用于存储对 value 进行转义后的结果
	char		*value_esc;

	// 对传入的 value 进行转义，转义字符串中的特殊字符，并将结果存储在 value_esc 指向的字符数组中
	value_esc = DBdyn_escape_field("dservices", "value", value);

	// 使用 DBselect 函数执行一条 SQL 查询语句，查询与 dcheckid 和 value_esc 对应的数据库记录
	result = DBselect(
			"select dh.dhostid,dh.status,dh.lastup,dh.lastdown"
			" from dhosts dh,dservices ds"
			" where ds.dhostid=dh.dhostid"
				" and ds.dcheckid==" ZBX_FS_UI64
				" and ds.value" ZBX_SQL_STRCMP
			" order by dh.dhostid",
			dcheckid, ZBX_SQL_STRVAL_EQ(value_esc));

	// 释放 value_esc 指向的字符数组占用的内存
	zbx_free(value_esc);

	// 返回查询结果
	return result;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 discovery_get_dhost_by_ip_port 的函数，该函数根据传入的 IP 地址和端口查询数据库中的主机信息。查询结果包括主机 ID、状态、上次更新时间和上次下降时间。查询过程中，对 IP 地址进行了转义，以防止 SQL 注入攻击。最后，将查询结果返回。
 ******************************************************************************/
/* 定义一个名为 discovery_get_dhost_by_ip_port 的函数，接收三个参数：
 * zbx_uint64_t 类型的 druleid，const char * 类型的 ip，以及 int 类型的 port。
 * 该函数的主要目的是根据 IP 地址和端口查询数据库中的主机信息。
 */
static DB_RESULT	discovery_get_dhost_by_ip_port(zbx_uint64_t druleid, const char *ip, int port)
{
	/* 定义一个名为 result 的 DB_RESULT 类型的变量，用于存储查询结果。
	 * 初始化时，将其值设置为 NULL。
	 */
	DB_RESULT	result;
	/* 定义一个名为 ip_esc 的字符指针，用于存储 IP 地址的转义字符串。
	 * 初始化时，将其值设置为 NULL。
	 */
	char		*ip_esc;

	/* 对 IP 地址进行转义，防止 SQL 注入攻击。
	 * 使用 DBdyn_escape_field 函数将 IP 地址转义，并将结果存储在 ip_esc 变量中。
	 */
	ip_esc = DBdyn_escape_field("dservices", "ip", ip);

	/* 使用 DBselect 函数执行 SQL 查询，查询与给定规则关联的主机信息。
	 * 查询语句中，选择了 dhostid、status、lastup 和 lastdown 四个字段。
	 * 查询条件包括：
	 * 1. dhostid 与 ds.dhostid 相等；
	 * 2. druleid 等于传入的 druleid；
	 * 3. ds.ip 与 ip_esc 相等（转义后的 IP 地址）；
	 * 4. ds.port 等于传入的 port。
	 * 查询结果按照 dhostid 排序。
	 */
	result = DBselect(
			"select dh.dhostid,dh.status,dh.lastup,dh.lastdown"
			" from dhosts dh,dservices ds"
			" where ds.dhostid=dh.dhostid"
				" and dh.druleid=" ZBX_FS_UI64
				" and ds.ip" ZBX_SQL_STRCMP
				" and ds.port=%d"
			" order by dh.dhostid",
			druleid, ZBX_SQL_STRVAL_EQ(ip_esc), port);

	/* 释放 ip_esc 变量占用的内存。
	 */
	zbx_free(ip_esc);

	/* 返回查询结果。
	 */
	return result;
}


/******************************************************************************
 *                                                                            *
 * Function: discovery_separate_host                                          *
 *                                                                            *
 * Purpose: separate multiple-IP hosts                                        *
 *                                                                            *
 * Parameters: host ip address                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是发现一个新的主机，并将它插入到数据库中的dhosts表中。在这个过程中，首先对IP地址进行转义，然后构造SQL查询语句查找与给定IP地址匹配的主机。如果找到匹配的主机，将其插入到dhosts表中，并更新dservices表中的主机IP地址和dhostid。最后，更新主机的状态、最后一次更新时间和最后一次下降时间。整个函数执行完毕后，释放相关资源并记录日志。
 ******************************************************************************/
static void discovery_separate_host(DB_DRULE *drule, DB_DHOST *dhost, const char *ip)
{
    // 定义一个内部函数，用于发现分离的主机
    // 参数：drule（数据库中的规则结构体指针），dhost（数据库中的主机结构体指针），ip（要处理的主机IP地址）

    const char *__function_name = "discovery_separate_host";

    // 定义一些变量，用于存储查询结果和处理数据
    DB_RESULT	result;
    DB_ROW		row;
    char		*ip_esc, *sql = NULL;
    zbx_uint64_t	dhostid;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() ip:'%s'", __function_name, ip);

    // 对IP地址进行转义，以便在SQL查询中使用
    ip_esc = DBdyn_escape_field("dservices", "ip", ip);

    // 构造SQL查询语句，查询与给定IP地址匹配的主机
    sql = zbx_dsprintf(sql,
                "select dserviceid"
                " from dservices"
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义函数：discovery_register_host
 * 参数：DB_DRULE *drule：数据采集规则指针
 *          zbx_uint64_t dcheckid：检测ID
 *          DB_DHOST *dhost：主机结构指针
 *          const char *ip：主机IP地址
 *          int port：主机端口
 *          int status：主机状态
 *          const char *value：主机值
 * 返回值：无
 * 主要目的：注册发现的主机到数据库
 */
static void discovery_register_host(DB_DRULE *drule, zbx_uint64_t dcheckid, DB_DHOST *dhost,
                                  const char *ip, int port, int status, const char *value)
{
	const char *__function_name = "discovery_register_host";

	/* 定义变量，用于存储数据库操作结果、行数据和匹配值 */
	DB_RESULT	result;
	DB_ROW		row;
	int		match_value = 0;

	/* 记录日志，显示调用函数的详细信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() ip:'%s' status:%d value:'%s'",
	          __function_name, ip, status, value);

	/* 判断检测ID是否与规则的唯一ID相同，如果相同，则执行以下操作 */
	if (drule->unique_dcheckid == dcheckid)
	{
		/* 查询数据库中是否存在具有相同值的主机 */
		result = discovery_get_dhost_by_value(dcheckid, value);

		/* 如果查询结果为空，则执行以下操作 */
		if (NULL == (row = DBfetch(result)))
		{
			/* 释放查询结果内存 */
			DBfree_result(result);

			/* 根据IP和端口查询数据库中是否存在该主机 */
			result = discovery_get_dhost_by_ip_port(drule->druleid, ip, port);
			row = DBfetch(result);
		}
		else
			match_value = 1;
	}
	else
	{
		/* 根据IP和端口查询数据库中是否存在该主机 */
		result = discovery_get_dhost_by_ip_port(drule->druleid, ip, port);
		row = DBfetch(result);
	}

	/* 如果查询结果为空，根据主机状态判断是否新增主机 */
	if (NULL == row)
	{
		if (DOBJECT_STATUS_UP == status)	/* add host only if service is up */
		{
			zabbix_log(LOG_LEVEL_DEBUG, "new host discovered at %s", ip);

			/* 分配主机ID，并设置主机状态、上次上线时间、上次下线时间 */
			dhost->dhostid = DBget_maxid("dhosts");
			dhost->status = DOBJECT_STATUS_DOWN;
			dhost->lastup = 0;
			dhost->lastdown = 0;

			/* 向数据库中插入新主机记录 */
			DBexecute("insert into dhosts (dhostid,druleid)"
			          " values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ")",
			          dhost->dhostid, drule->druleid);
		}
	}
	else
	{
/******************************************************************************
 * *
 *整个代码块的主要目的是： Discovery模块发现一个新的服务，将其添加到数据库中，或者更新已存在的服务记录。 
 *
 *代码详细注释如下：
 *
 *1. 定义静态函数 discovery_register_service，接收 5 个参数：检测器 ID、主机结构指针、服务结构指针、IP 地址、主机名、端口和服务状态。
 *2. 定义变量，包括函数名、数据库查询结果、行数据、IP 地址转义字符串、主机名转义字符串。
 *3. 获取主机 ID。
 *4. 调试日志：记录进入函数的信息。
 *5. 对 IP 地址进行转义。
 *6. 从数据库中查询服务信息。
 *7. 如果没有查到服务，则表示是新发现的服务，根据服务状态进行相应操作。
 *8. 初始化服务结构体。
 *9. 将对主机名的转义后的值插入数据库。
 *10. 服务已存在于数据库中，更新服务状态。
 *11. 更新主机名。
 *12. 更新服务名。
 *13. 释放资源。
 *14. 结束调试日志。
 ******************************************************************************/
// 定义静态函数 discovery_register_service，接收 5 个参数：
// zbx_uint64_t dcheckid：检测器 ID
// DB_DHOST *dhost：主机结构指针
// DB_DSERVICE *dservice：服务结构指针
// const char *ip：IP 地址
// const char *dns：主机名
// int port：端口
// int status：服务状态
static void discovery_register_service(zbx_uint64_t dcheckid, DB_DHOST *dhost, DB_DSERVICE *dservice,
                                    const char *ip, const char *dns, int port, int status)
{
    // 定义变量
    const char *__function_name = "discovery_register_service";
    DB_RESULT result;
    DB_ROW row;
    char *ip_esc, *dns_esc;

    // 获取主机 ID
    zbx_uint64_t dhostid;

    // 调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() ip:'%s' port:%d", __function_name, ip, port);

    // 对 IP 地址进行转义
    ip_esc = DBdyn_escape_field("dservices", "ip", ip);

    // 从数据库中查询服务信息
    result = DBselect(
        "select dserviceid,dhostid,status,lastup,lastdown,value,dns"
        " from dservices"
        " where dcheckid=" ZBX_FS_UI64
            " and ip" ZBX_SQL_STRCMP
            " and port=%d",
        dcheckid, ZBX_SQL_STRVAL_EQ(ip_esc), port);

    // 如果没有查到服务，则表示是新发现的服务
    if (NULL == (row = DBfetch(result)))
    {
        if (DOBJECT_STATUS_UP == status) /* 仅在服务状态为 UP 时添加主机 */
        {
            zabbix_log(LOG_LEVEL_DEBUG, "new service discovered on port %d", port);

            // 初始化服务结构体
            dservice->dserviceid = DBget_maxid("dservices");
            dservice->status = DOBJECT_STATUS_DOWN;
            dservice->value = zbx_strdup(dservice->value, "");

            // 将对主机名的转义后的值插入数据库
            dns_esc = DBdyn_escape_field("dservices", "dns", dns);

            DBexecute("insert into dservices (dserviceid,dhostid,dcheckid,ip,dns,port,status)"
                    " values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s',%d,%d)",
                    dservice->dserviceid, dhost->dhostid, dcheckid, ip_esc, dns_esc, port,
                    dservice->status);

            zbx_free(dns_esc);
        }
    }
    else
    {
        // 服务已存在于数据库中，更新服务状态
        zabbix_log(LOG_LEVEL_DEBUG, "service is already in database");

        ZBX_STR2UINT64(dservice->dserviceid, row[0]);
        ZBX_STR2UINT64(dhostid, row[1]);
        dservice->status = atoi(row[2]);
        dservice->lastup = atoi(row[3]);
        dservice->lastdown = atoi(row[4]);
        dservice->value = zbx_strdup(dservice->value, row[5]);

        // 更新主机名
        if (dhostid != dhost->dhostid)
        {
            DBexecute("update dservices"
                    " set dhostid=" ZBX_FS_UI64
                    " where dhostid=" ZBX_FS_UI64,
                    dhost->dhostid, dhostid);

            // 删除旧的主机记录
            DBexecute("delete from dhosts"
                    " where dhostid=" ZBX_FS_UI64,
                    dhostid);
        }

        // 更新服务名
        if (0 != strcmp(row[6], dns))
        {
            dns_esc = DBdyn_escape_field("dservices", "dns", dns);

            DBexecute("update dservices"
                    " set dns='%s'"
                    " where dserviceid=" ZBX_FS_UI64,
                    dns_esc, dservice->dserviceid);

            zbx_free(dns_esc);
        }
    }

    // 释放资源
    DBfree_result(result);
    zbx_free(ip_esc);

    // 结束调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	const char	*__function_name = "discovery_register_service";

	DB_RESULT	result;
	DB_ROW		row;
	char		*ip_esc, *dns_esc;

	zbx_uint64_t	dhostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() ip:'%s' port:%d", __function_name, ip, port);

	ip_esc = DBdyn_escape_field("dservices", "ip", ip);

	result = DBselect(
			"select dserviceid,dhostid,status,lastup,lastdown,value,dns"
			" from dservices"
			" where dcheckid=" ZBX_FS_UI64
				" and ip" ZBX_SQL_STRCMP
				" and port=%d",
			dcheckid, ZBX_SQL_STRVAL_EQ(ip_esc), port);

	if (NULL == (row = DBfetch(result)))
	{
		if (DOBJECT_STATUS_UP == status)	/* add host only if service is up */
		{
			zabbix_log(LOG_LEVEL_DEBUG, "new service discovered on port %d", port);

			dservice->dserviceid = DBget_maxid("dservices");
			dservice->status = DOBJECT_STATUS_DOWN;
			dservice->value = zbx_strdup(dservice->value, "");

			dns_esc = DBdyn_escape_field("dservices", "dns", dns);

			DBexecute("insert into dservices (dserviceid,dhostid,dcheckid,ip,dns,port,status)"
					" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s',%d,%d)",
					dservice->dserviceid, dhost->dhostid, dcheckid, ip_esc, dns_esc, port,
					dservice->status);

			zbx_free(dns_esc);
		}
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "service is already in database");

		ZBX_STR2UINT64(dservice->dserviceid, row[0]);
		ZBX_STR2UINT64(dhostid, row[1]);
		dservice->status = atoi(row[2]);
		dservice->lastup = atoi(row[3]);
		dservice->lastdown = atoi(row[4]);
		dservice->value = zbx_strdup(dservice->value, row[5]);

		if (dhostid != dhost->dhostid)
		{
			DBexecute("update dservices"
					" set dhostid=" ZBX_FS_UI64
					" where dhostid=" ZBX_FS_UI64,
					dhost->dhostid, dhostid);

			DBexecute("delete from dhosts"
/******************************************************************************
 * *
 *这段代码的主要目的是实现服务发现功能，当服务状态发生变化时，更新数据库中的服务信息，并发送发现事件。具体来说，当服务从下线变为上线时，更新服务状态、主机状态以及服务值；当服务从上线变为下线时，仅更新服务状态。在整个过程中，添加发现事件以通知相关人员进行处理。
 ******************************************************************************/
// 定义静态函数 discovery_update_service_status，接收 5 个参数：DB_DHOST 结构体指针、DB_DSERVICE 结构体指针、int 类型服务状态、const char *类型值和 int 类型当前时间
static void discovery_update_service_status(DB_DHOST *dhost, const DB_DSERVICE *dservice, int service_status,
                                        const char *value, int now)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "discovery_update_service_status";

    // 定义一个 zbx_timespec_t 结构体变量 ts，用于存储时间戳
    zbx_timespec_t ts;

    // 打印调试日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 初始化时间戳 ts，秒为单位，纳秒为 0
    ts.sec = now;
    ts.ns = 0;

    // 判断服务状态，如果为 DOBJECT_STATUS_UP（即服务上线）
    if (DOBJECT_STATUS_UP == service_status)
    {
        // 判断当前主机状态是否为 DOBJECT_STATUS_DOWN（即主机下线），或者最后一次上线时间为 0
        if (DOBJECT_STATUS_DOWN == dservice->status || 0 == dservice->lastup)
        {
            // 更新服务状态和时间戳，并发送发现事件
            discovery_update_dservice(dservice->dserviceid, service_status, now, 0, value);
            zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DSERVICE, dservice->dserviceid, &ts,
                         DOBJECT_STATUS_DISCOVER, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);

            // 主机状态为 DOBJECT_STATUS_DOWN，服务状态为 DOBJECT_STATUS_UP，更新主机状态
            if (DOBJECT_STATUS_DOWN == dhost->status)
            {
                // 更新主机状态为 DOBJECT_STATUS_UP，最后一次上线时间为 now，最后一次下线时间为 0
                dhost->status = DOBJECT_STATUS_UP;
                dhost->lastup = now;
                dhost->lastdown = 0;

                // 更新主机信息
                discovery_update_dhost(dhost);
                zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts,
                              DOBJECT_STATUS_DISCOVER, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0,
                              NULL);
            }
        }
        // 服务状态不为 DOBJECT_STATUS_DOWN，且当前值与原值不相等，更新服务值
        else if (0 != strcmp(dservice->value, value))
        {
            // 更新服务值为 value
            discovery_update_dservice_value(dservice->dserviceid, value);
        }
    }
    // 服务状态为 DOBJECT_STATUS_DOWN（即服务下线）
    else
    {
        // 判断当前主机状态是否为 DOBJECT_STATUS_UP（即主机上线），或者最后一次下线时间为 0
        if (DOBJECT_STATUS_UP == dservice->status || 0 == dservice->lastdown)
        {
            // 更新服务状态为 DOBJECT_STATUS_LOST（即服务丢失），时间戳为 0
            discovery_update_dservice(dservice->dserviceid, service_status, 0, now, dservice->value);
            zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DSERVICE, dservice->dserviceid, &ts,
                         DOBJECT_STATUS_LOST, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);

            // 服务下线，不需要更新主机状态
        }
    }

    // 添加发现事件，表示服务状态变更
    zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DSERVICE, dservice->dserviceid, &ts, service_status,
                 NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);

    // 处理事件
    zbx_process_events(NULL, NULL);
    // 清理事件
    zbx_clean_events();

    // 打印调试日志，表示结束该函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: discovery_update_dservice_value                                  *
 *                                                                            *
 * Purpose: update discovered service details                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于更新 Zabbix 数据库中 dservices 表中的 value 字段。通过传入的 dserviceid 和 value 参数，对 value 字段进行 escape 处理后，使用 DBexecute 函数执行更新操作。最后释放占用的内存空间。
 ******************************************************************************/
// 定义一个名为 discovery_update_dservice_value 的静态函数，该函数用于更新 Zabbix 数据库中 dservices 表中的 value 字段
static void discovery_update_dservice_value(zbx_uint64_t dserviceid, const char *value)
{
	// 定义一个字符指针变量 value_esc，用于存储数据库字段值 escape 后的结果
	char *value_esc;

	// 使用 DBdyn_escape_field 函数对传入的 value 字符串进行 escape 处理，即将字符串中的特殊字符进行转义
	value_esc = DBdyn_escape_field("dservices", "value", value);

	// 使用 DBexecute 函数执行更新操作，将 dservices 表中的 value 字段更新为 escape 处理后的值，其中 dserviceid 为参数传递的整数
	DBexecute("update dservices set value='%s' where dserviceid=" ZBX_FS_UI64, value_esc, dserviceid);

	// 释放 value_esc 占用的内存空间
	zbx_free(value_esc);
}


/******************************************************************************
 *                                                                            *
 * Function: discovery_update_dhost                                           *
 *                                                                            *
 * Purpose: update discovered host details                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是更新数据库中的主机信息。函数`discovery_update_dhost`接收一个`DB_DHOST`结构体的指针作为参数，该结构体包含主机的状态、最后上线时间、最后下线时间和主机ID等信息。函数通过`DBexecute`函数执行更新操作，将主机的状态、最后上线时间和最后下线时间更新到数据库中。其中，`ZBX_FS_UI64`是一个宏，表示主机ID的字节大小。
 ******************************************************************************/
// 定义一个静态函数，用于更新数据库中的主机信息
static void discovery_update_dhost(const DB_DHOST *dhost)
{
    // 执行更新操作，更新主机的状态、最后上线时间和最后下线时间
    DBexecute("update dhosts set status=%d,lastup=%d,lastdown=%d where dhostid=" ZBX_FS_UI64,
              dhost->status, dhost->lastup, dhost->lastdown, dhost->dhostid);
}


/******************************************************************************
 *                                                                            *
 * Function: discovery_update_service_status                                  *
 *                                                                            *
 * Purpose: process and update the new service status                         *
 *                                                                            *
 ******************************************************************************/
static void	discovery_update_service_status(DB_DHOST *dhost, const DB_DSERVICE *dservice, int service_status,
		const char *value, int now)
{
	const char	*__function_name = "discovery_update_service_status";

	zbx_timespec_t	ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	ts.sec = now;
	ts.ns = 0;

	if (DOBJECT_STATUS_UP == service_status)
	{
		if (DOBJECT_STATUS_DOWN == dservice->status || 0 == dservice->lastup)
		{
			discovery_update_dservice(dservice->dserviceid, service_status, now, 0, value);
			zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DSERVICE, dservice->dserviceid, &ts,
					DOBJECT_STATUS_DISCOVER, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);

			if (DOBJECT_STATUS_DOWN == dhost->status)
			{
				/* Service went UP, but host status is DOWN. Update host status. */

				dhost->status = DOBJECT_STATUS_UP;
				dhost->lastup = now;
				dhost->lastdown = 0;

				discovery_update_dhost(dhost);
				zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts,
						DOBJECT_STATUS_DISCOVER, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0,
						NULL);
			}
/******************************************************************************
 * *
 *整个代码块的主要目的是更新主机状态，并根据状态变化添加相应的事件。当主机状态从 DOWN 变为 UP 时，添加 DOBJECT_STATUS_DISCOVER 事件；当主机状态从 UP 变为 DOWN 时，添加 DOBJECT_STATUS_LOST 事件。同时，将更新后的主机信息更新到数据库。
 ******************************************************************************/
/* 定义静态函数 discovery_update_host_status，接收 3 个参数：DB_DHOST 结构体指针 dhost，整型变量 status 和整型变量 now。
*/
static void discovery_update_host_status(DB_DHOST *dhost, int status, int now)
{
	/* 定义一个 zbx_timespec_t 类型的结构体变量 ts，用于存储时间戳信息。
	   将 now 赋值给 ts.sec，将 0 赋值给 ts.ns。
	*/
	zbx_timespec_t	ts;

	ts.sec = now;
	ts.ns = 0;

	/* 判断 status 是否为 DOBJECT_STATUS_UP（即主机状态为UP），如果是，执行以下操作：
	   1. 更新主机状态
	   2. 更新 lastdown 和 lastup 的时间戳
	   3. 调用 discovery_update_dhost 函数更新主机信息到数据库
	   4. 添加一个事件，事件来源为 Discovery，事件对象为 DHOST，主机 ID 为 dhost->dhostid，时间为 now，状态为 DOBJECT_STATUS_DISCOVER
	*/
	if (DOBJECT_STATUS_UP == status)
	{
		if (DOBJECT_STATUS_DOWN == dhost->status || 0 == dhost->lastup)
		{
			dhost->status = status;
			dhost->lastdown = 0;
			dhost->lastup = now;

			/* 更新主机信息到数据库 */
			discovery_update_dhost(dhost);

			/* 添加事件 */
			zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts,
					DOBJECT_STATUS_DISCOVER, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
		}
	}
	else	/* DOBJECT_STATUS_DOWN */
	{
		/* 判断 status 是否为 DOBJECT_STATUS_DOWN（即主机状态为DOWN），如果是，执行以下操作：
		   1. 更新主机状态
		   2. 更新 lastdown 和 lastup 的时间戳
		   3. 调用 discovery_update_dhost 函数更新主机信息到数据库
		   4. 添加一个事件，事件来源为 Discovery，事件对象为 DHOST，主机 ID 为 dhost->dhostid，时间为 now，状态为 DOBJECT_STATUS_LOST
		*/
		if (DOBJECT_STATUS_DOWN == dhost->status || 0 == dhost->lastdown)
		{
			dhost->status = status;
			dhost->lastdown = now;
			dhost->lastup = 0;

			/* 更新主机信息到数据库 */
			discovery_update_dhost(dhost);

			/* 添加事件 */
			zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts,
					DOBJECT_STATUS_LOST, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
		}
	}

	/* 添加一个事件，事件来源为 Discovery，事件对象为 DHOST，主机 ID 为 dhost->dhostid，时间为 now，状态为 status */
	zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts, status, NULL, NULL, NULL, 0, 0,
			NULL, 0, NULL, 0, NULL);

	/* 处理事件 */
	zbx_process_events(NULL, NULL);

	/* 清理事件 */
	zbx_clean_events();
}

	/* update host status */
	if (DOBJECT_STATUS_UP == status)
	{
		if (DOBJECT_STATUS_DOWN == dhost->status || 0 == dhost->lastup)
		{
			dhost->status = status;
			dhost->lastdown = 0;
			dhost->lastup = now;

			discovery_update_dhost(dhost);
			zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts,
					DOBJECT_STATUS_DISCOVER, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
		}
	}
	else	/* DOBJECT_STATUS_DOWN */
	{
		if (DOBJECT_STATUS_UP == dhost->status || 0 == dhost->lastdown)
		{
			dhost->status = status;
			dhost->lastdown = now;
			dhost->lastup = 0;

			discovery_update_dhost(dhost);
			zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts,
					DOBJECT_STATUS_LOST, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
		}
	}
	zbx_add_event(EVENT_SOURCE_DISCOVERY, EVENT_OBJECT_DHOST, dhost->dhostid, &ts, status, NULL, NULL, NULL, 0, 0,
			NULL, 0, NULL, 0, NULL);

	zbx_process_events(NULL, NULL);
	zbx_clean_events();
}

/******************************************************************************
 *                                                                            *
 * Function: discovery_update_host                                            *
 *                                                                            *
 * Purpose: process new host status                                           *
 *                                                                            *
 * Parameters: host - host info                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于更新主机的状态。首先，定义了一个名为`discovery_update_host`的函数，接收三个参数：一个指向`DB_DHOST`结构体的指针`dhost`，一个整数`status`和一个整数`now`。在函数内部，首先定义了一个字符串`__function_name`，用于存储函数名。接着使用`zabbix_log`函数打印日志，表示函数开始调用。然后判断主机ID是否不为0，如果不为0，则调用`discovery_update_host_status`函数更新主机状态。最后，再次使用`zabbix_log`函数打印日志，表示函数调用结束。
 ******************************************************************************/
// 定义一个函数，用于更新主机的状态
void discovery_update_host(DB_DHOST *dhost, int status, int now)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "discovery_update_host";

    // 打印日志，表示函数开始调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断主机ID是否不为0，如果不为0，则调用discovery_update_host_status函数更新主机状态
/******************************************************************************
 * *
 *整个代码块的主要目的是实现服务发现和更新功能。这段代码接收一个DB_DRULE结构体指针、一个zbx_uint64_t类型指针、一个DB_DHOST结构体指针、IP地址、DNS、端口、服务状态、服务值和一个整数now作为参数。首先，打印调试日志，记录传入的参数。然后，清空内存，为后续操作做准备。接下来，判断主机是否已注册，若未注册则注册主机；若主机已注册，则注册服务。如果服务未注册，则调用discovery_update_service_status函数更新服务状态。最后，释放dservice.value内存并打印调试日志，表示函数执行结束。
 ******************************************************************************/
/* 定义函数名 */
void discovery_update_service(DB_DRULE *drule, zbx_uint64_t dcheckid, DB_DHOST *dhost, const char *ip,
                            const char *dns, int port, int status, const char *value, int now)
{
    /* 定义日志级别 */
    const char *__function_name = "discovery_update_service";

    /* 定义数据结构 */
    DB_DSERVICE dservice;

    /* 打印调试日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() ip:'%s' dns:'%s' port:%d status:%d value:'%s'",
               __function_name, ip, dns, port, status, value);

    /* 清空内存 */
    memset(&dservice, 0, sizeof(dservice));

    /* 判断主机是否已注册，若未注册则注册主机 */
    if (0 == dhost->dhostid)
        discovery_register_host(drule, dcheckid, dhost, ip, port, status, value);

    /* 判断主机已注册，则注册服务 */
    if (0 != dhost->dhostid)
        discovery_register_service(dcheckid, dhost, &dservice, ip, dns, port, status);

    /* 服务未注册，原因是我们不添加下线服务 */
    if (0 != dservice.dserviceid)
        discovery_update_service_status(dhost, &dservice, status, value, now);

    /* 释放dservice.value内存 */
    zbx_free(dservice.value);

    /* 打印调试日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


	memset(&dservice, 0, sizeof(dservice));

	/* register host if is not registered yet */
	if (0 == dhost->dhostid)
		discovery_register_host(drule, dcheckid, dhost, ip, port, status, value);

	/* register service if is not registered yet */
	if (0 != dhost->dhostid)
		discovery_register_service(dcheckid, dhost, &dservice, ip, dns, port, status);

	/* service was not registered because we do not add down service */
	if (0 != dservice.dserviceid)
		discovery_update_service_status(dhost, &dservice, status, value, now);

	zbx_free(dservice.value);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
