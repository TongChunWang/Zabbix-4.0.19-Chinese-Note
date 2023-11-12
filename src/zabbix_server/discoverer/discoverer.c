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
#include "zbxicmpping.h"
#include "discovery.h"
#include "zbxserver.h"
#include "zbxself.h"

#include "daemon.h"
#include "discoverer.h"
#include "../poller/checks_agent.h"
#include "../poller/checks_snmp.h"
#include "../../libs/zbxcrypto/tls.h"

extern int		CONFIG_DISCOVERER_FORKS;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

#define ZBX_DISCOVERER_IPRANGE_LIMIT	(1 << 16)

/******************************************************************************
 *                                                                            *
 * Function: proxy_update_service                                             *
 *                                                                            *
 * Purpose: process new service status                                        *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新代理监控数据到数据库。首先，对输入的IP、DNS和监控值进行SQL注入防范处理，然后将处理后的数据插入到数据库的proxy_dhistory表中。最后，释放内存。
 ******************************************************************************/
/* 定义一个名为 proxy_update_service 的静态函数，该函数用于更新代理监控数据到数据库。
 * 输入参数：
 *   druleid：监控项规则ID
 *   dcheckid：检查ID
 *   ip：目标IP地址
 *   dns：目标域名
 *   port：目标端口
 *   status：状态码
 *   value：监控值
 *   now：当前时间
 * 输出结果：
 *   None
 */
static void	proxy_update_service(zbx_uint64_t druleid, zbx_uint64_t dcheckid, const char *ip,
		const char *dns, int port, int status, const char *value, int now)
{
	/* 声明字符串指针变量，用于存储处理后的IP、DNS和监控值 */
	char	*ip_esc, *dns_esc, *value_esc;

	/* 对IP地址进行转义处理，防止SQL注入 */
	ip_esc = DBdyn_escape_field("proxy_dhistory", "ip", ip);

	/* 对DNS进行转义处理，防止SQL注入 */
/******************************************************************************
 * *
 *整个代码块的主要目的是向 proxy_dhistory 表中插入一条记录，记录内容包括时钟、druleid、ip、dns 和 status。在这个过程中，首先对 ip 和 dns 进行转义，然后执行插入操作，最后释放内存。
 ******************************************************************************/
// 定义一个名为 proxy_update_host 的静态函数，参数包括 zbx_uint64_t 类型的 druleid，以及 const char * 类型的 ip、dns 和两个 int 类型的 status 和 now。
static void proxy_update_host(zbx_uint64_t druleid, const char *ip, const char *dns, int status, int now)
{
	// 定义两个字符指针 ip_esc 和 dns_esc，用于存储动态库中 ip 和 dns 字段的转义结果。
	char *ip_esc, *dns_esc;

	// 对传入的 ip 参数进行转义，转义后的结果存储在 ip_esc 指针指向的内存区域。
	ip_esc = DBdyn_escape_field("proxy_dhistory", "ip", ip);
	// 对传入的 dns 参数进行转义，转义后的结果存储在 dns_esc 指针指向的内存区域。
	dns_esc = DBdyn_escape_field("proxy_dhistory", "dns", dns);

	// 执行 DB 操作，向 proxy_dhistory 表中插入一条记录，记录内容包括：时钟、druleid、ip、dns 和 status。
	// 插入记录的 SQL 语句模板，%d 表示时钟，%zu 表示 druleid，'%s' 表示 ip，'%s' 表示 dns，%d 表示 status。
	DBexecute("insert into proxy_dhistory (clock,druleid,ip,dns,status)"
			" values (%d," ZBX_FS_UI64 ",'%s','%s',%d)",
			now, druleid, ip_esc, dns_esc, status);

	// 释放 dns_esc 和 ip_esc 指向的内存空间。
	zbx_free(dns_esc);
	zbx_free(ip_esc);
}

/******************************************************************************
 * *
 *这个函数的主要目的是对给定的服务、IP地址和端口进行探测，并根据不同的服务类型调用相应的检查方法。具体来说，它实现了以下功能：
 *
 *1. 初始化结果结构体。
 *2. 根据服务类型切换，对不同的服务进行相应的检查。
 *   - 如果是简单检查（如TCP、HTTP等），则调用process()函数处理检查项，并分配内存存储结果。
 *   - 如果是代理和SNMP检查，则设置检查项的key、接口、地址、端口等参数，并调用get_value_snmp()函数进行SNMP检查。
 *   - 如果是ICMP检查，则调用do_ping()函数进行ping检查。
 *3. 释放分配的内存。
 *4. 关闭报警。
 *5. 释放结果结构体。
 *6. 输出结果。
 ******************************************************************************/
static int discover_service(const DB_DCHECK *dcheck, char *ip, int port, char **value, size_t *value_alloc)
{
	const char *__function_name = "discover_service";
	int		ret = SUCCEED;
	const char	*service = NULL;
	AGENT_RESULT 	result;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化结果结构体
	init_result(&result);

	// 清空值指针指向的内存
	**value = '\0';

	// 根据dcheck中的类型进行切换
	switch (dcheck->type)
	{
		// 简单检查
		case SVC_SSH:
		case SVC_LDAP:
		case SVC_SMTP:
		case SVC_FTP:
		case SVC_HTTP:
		case SVC_POP:
		case SVC_NNTP:
		case SVC_IMAP:
		case SVC_TCP:
		case SVC_HTTPS:
		case SVC_TELNET:
			// 构建key
			zbx_snprintf(key, sizeof(key), "net.tcp.service[%s,%s,%d]", service, ip, port);

			// 处理检查项
			if (SUCCEED != process(key, 0, &result) || NULL == GET_UI64_RESULT(&result) ||
					0 == result.ui64)
			{
				ret = FAIL;
			}
			break;
		// 代理和SNMP检查
		case SVC_AGENT:
		case SVC_SNMPv1:
		case SVC_SNMPv2c:
		case SVC_SNMPv3:
			// 初始化检查项结构体
			memset(&item, 0, sizeof(DC_ITEM));

			// 设置检查项的key、接口、地址、端口等参数
			strscpy(item.key_orig, dcheck->key_);
			item.key = item.key_orig;
			item.interface.useip = 1;
			item.interface.addr = ip;
			item.interface.port = port;

			// 设置值类型为字符串
			item.value_type	= ITEM_VALUE_TYPE_STR;

			// 根据类型设置不同的SNMP参数
			switch (dcheck->type)
			{
				case SVC_SNMPv1:
					item.type = ITEM_TYPE_SNMPv1;
					break;
				case SVC_SNMPv2c:
					item.type = ITEM_TYPE_SNMPv2c;
					break;
				case SVC_SNMPv3:
					item.type = ITEM_TYPE_SNMPv3;
					break;
			}

			// 进行SNMP检查
			if (SUCCEED == get_value_snmp(&item, &result) &&
					NULL != (pvalue = GET_TEXT_RESULT(&result)))
			{
				// 分配内存并复制值
				zbx_strcpy_alloc(value, value_alloc, &value_offset, *pvalue);
			}
			else
				ret = FAIL;

			// 释放内存
			zbx_free(item.snmp_community);
			zbx_free(item.snmp_oid);

			// 如果是SNMPv3，还需要释放以下内存
			if (ITEM_TYPE_SNMPv3 == item.type)
			{
				zbx_free(item.snmpv3_securityname);
				zbx_free(item.snmpv3_authpassphrase);
				zbx_free(item.snmpv3_privpassphrase);
				zbx_free(item.snmpv3_contextname);
			}
			break;
		// ICMP检查
		case SVC_ICMPPING:
			// 初始化主机结构体
			memset(&host, 0, sizeof(ZBX_FPING_HOST));
			host.addr = strdup(ip);

			// 进行ping检查
			if (SUCCEED != do_ping(&host, 1, 3, 0, 0, 0, error, sizeof(error)) || 0 == host.rcv)
				ret = FAIL;

			// 释放内存
			zbx_free(host.addr);
			break;
		default:
			break;
	}

	// 关闭报警
	zbx_alarm_off();

	// 释放结果结构体
	free_result(&result);

	// 输出结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

					{
						zbx_free(item.snmpv3_securityname);
						zbx_free(item.snmpv3_authpassphrase);
						zbx_free(item.snmpv3_privpassphrase);
						zbx_free(item.snmpv3_contextname);
					}
				}
#else
					ret = FAIL;
#endif	/* HAVE_NETSNMP */

				if (FAIL == ret && ISSET_MSG(&result))
				{
					zabbix_log(LOG_LEVEL_DEBUG, "discovery: item [%s] error: %s",
							item.key, result.msg);
				}
				break;
			case SVC_ICMPPING:
				memset(&host, 0, sizeof(host));
				host.addr = strdup(ip);

				if (SUCCEED != do_ping(&host, 1, 3, 0, 0, 0, error, sizeof(error)) || 0 == host.rcv)
					ret = FAIL;

				zbx_free(host.addr);
				break;
			default:
				break;
		}

		zbx_alarm_off();
	}
	free_result(&result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_check                                                    *
 *                                                                            *
 * Purpose: check if service is available and update database                 *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 DNS 解析请求，根据给定的 IP 地址和端口列表，查询服务状态，并更新主机状态。在这个过程中，首先解析端口列表，然后遍历每个端口，调用 discover_service 函数查询服务状态，并根据主机类型更新服务信息。最后，释放内存并结束函数调用。
 ******************************************************************************/
/* 定义静态函数 process_check，接收 5 个参数：
 * DB_DRULE 类型的指针 drule，
 * DB_DCHECK 类型的指针 dcheck，
 * DB_DHOST 类型的指针 dhost，
 * int 类型的指针 host_status，
 * 字符串指针 ip，
 * 以及一个整型变量 now。
 */
static void process_check(DB_DRULE *drule, DB_DCHECK *dcheck, DB_DHOST *dhost, int *host_status, char *ip,
                        const char *dns, int now)
{
    /* 定义一个常量字符串，表示函数名称 */
    const char *__function_name = "process_check";
    /* 定义一个指向 NULL 的字符指针，用于存储解析后的值 */
    char *value = NULL;
    /* 定义一个大小为 128 的内存空间，用于存储解析后的值 */
    size_t value_alloc = 128;

    /* 记录函数调用日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    /* 分配内存空间，用于存储解析后的值 */
    value = (char *)zbx_malloc(value, value_alloc);

    /* 遍历 dcheck 中的端口列表 */
    for (char *start = dcheck->ports; '\0' != *start;)
    {
        /* 解析端口列表中的逗号分隔符 */
        char *comma, *last_port;
        int port, first, last;

        if (NULL != (comma = strchr(start, ',')))
            *comma = '\0';

        if (NULL != (last_port = strchr(start, '-')))
        {
            *last_port = '\0';
            first = atoi(start);
            last = atoi(last_port + 1);
            *last_port = '-';
        }
        else
            first = last = atoi(start);

        /* 遍历端口列表中的每个端口 */
        for (port = first; port <= last; port++)
        {
            int service_status;

            /* 记录日志，显示当前端口 */
            zabbix_log(LOG_LEVEL_DEBUG, "%s() port:%d", __function_name, port);

            /* 调用 discover_service 函数，查询服务状态 */
            service_status = (SUCCEED == discover_service(dcheck, ip, port, &value, &value_alloc) ?
                            DOBJECT_STATUS_UP : DOBJECT_STATUS_DOWN);

            /* 更新主机状态 */
            if (-1 == *host_status || DOBJECT_STATUS_UP == service_status)
                *host_status = service_status;

            /* 开始事务操作 */
            DBbegin();

            /* 如果是服务器类型，更新服务信息 */
            if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
            {
                discovery_update_service(drule, dcheck->dcheckid, dhost, ip, dns, port, service_status,
                                       value, now);
            }
            /* 如果是代理类型，更新服务信息 */
            else if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
            {
                proxy_update_service(drule->druleid, dcheck->dcheckid, ip, dns, port, service_status,
                                    value, now);
            }

            /* 提交事务 */
            DBcommit();
        }

        /* 如果是逗号分隔符，跳过它 */
        if (NULL != comma)
        {
            *comma = ',';
            start = comma + 1;
        }
        else
            break;
    }

    /* 释放内存 */
    zbx_free(value);

    /* 记录函数调用结束日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: process_checks                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是查询dchecks表中的相关信息，然后调用process_check函数处理每个查询到的CHECKS。在这个过程中，代码完成了以下操作：
 *
 *1. 拼接SQL查询语句，从dchecks表中查询相关信息。
 *2. 添加查询条件，如果unique参数为真，则查询dcheckid唯一的数据。
 *3. 执行SQL查询，并遍历查询结果。
 *4. 对每个查询到的CHECKS，将其信息赋值给dcheck结构体。
 *5. 调用process_check函数处理每个查询到的CHECKS。
 *6. 释放查询结果。
 ******************************************************************************/
// 定义一个静态函数，用于处理CHECKS
static void process_checks(DB_DRULE *drule, DB_DHOST *dhost, int *host_status,
                         char *ip, const char *dns, int unique, int now)
{
    // 定义一些变量，用于存储查询结果和处理数据
    DB_RESULT	result;
    DB_ROW		row;
    DB_DCHECK	dcheck;
    char		sql[MAX_STRING_LEN];
    size_t		offset = 0;

    // 拼接SQL查询语句，从dchecks表中查询相关信息
    offset += zbx_snprintf(sql + offset, sizeof(sql) - offset,
                        "select dcheckid,type,key_,snmp_community,snmpv3_securityname,snmpv3_securitylevel,"
                        "snmpv3_authpassphrase,snmpv3_privpassphrase,snmpv3_authprotocol,snmpv3_privprotocol,"
                        "ports,snmpv3_contextname"
                    " from dchecks"
                    " where druleid=" ZBX_FS_UI64,
                    drule->druleid);

    // 如果unique参数为真，则添加查询条件dcheckid唯一
    if (0 != drule->unique_dcheckid)
    {
        offset += zbx_snprintf(sql + offset, sizeof(sql) - offset, " and dcheckid%s" ZBX_FS_UI64,
                            unique ? "=" : "<>", drule->unique_dcheckid);
    }

    // 添加查询排序条件
    zbx_snprintf(sql + offset, sizeof(sql) - offset, " order by dcheckid");

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 初始化dcheck结构体
        memset(&dcheck, 0, sizeof(dcheck));

        // 将row中的数据赋值给dcheck
        ZBX_STR2UINT64(dcheck.dcheckid, row[0]);
        dcheck.type = atoi(row[1]);
        dcheck.key_ = row[2];
        dcheck.snmp_community = row[3];
        dcheck.snmpv3_securityname = row[4];
/******************************************************************************
 * 
 ******************************************************************************/
// 定义静态函数process_rule，传入参数为DB_DRULE结构体指针
static void process_rule(DB_DRULE *drule)
{
    // 定义日志标签
    const char *__function_name = "process_rule";

    // 定义DB_DHOST结构体变量
    DB_DHOST dhost;

    // 定义整型变量，存储主机状态和当前时间
    int host_status, now;

    // 定义字符串变量，存储IP地址和DNS名称
    char ip[INTERFACE_IP_LEN_MAX], *start, *comma, dns[INTERFACE_DNS_LEN_MAX];

    // 定义整型数组，存储IP地址
    int ipaddress[8];

    // 定义zbx_iprange_t结构体变量，存储IP范围信息
    zbx_iprange_t iprange;

    // 输出日志，记录函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() rule:'%s' range:'%s'", __function_name, drule->name, drule->iprange);

    // 遍历IP范围字符串
    for (start = drule->iprange; '\0' != *start;)
    {
        // 查找字符串中的逗号位置
        if (NULL != (comma = strchr(start, ',')))
        {
            *comma = '\0';
        }

        // 输出日志，记录当前IP范围
        zabbix_log(LOG_LEVEL_DEBUG, "%s() range:'%s'", __function_name, start);

        // 解析IP范围
        if (SUCCEED != iprange_parse(&iprange, start))
        {
            // 输出警告日志，记录错误的IP范围格式
            zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": wrong format of IP range \"%s\"",
                        drule->name, start);
            // 跳到下一个IP范围
            goto next;
        }

        // 检查IP范围体积是否超过限制
        if (ZBX_DISCOVERER_IPRANGE_LIMIT < iprange_volume(&iprange))
        {
            // 输出警告日志，记录超过地址限制的IP范围
            zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": IP range \"%s\" exceeds %d address limit",
                        drule->name, start, ZBX_DISCOVERER_IPRANGE_LIMIT);
            // 跳到下一个IP范围
            goto next;
        }

        // 跳过已解析的IP范围
        if (ZBX_IPRANGE_V6 == iprange.type)
        {
            goto next;
        }

        // 解析IP地址
        iprange_first(&iprange, ipaddress);

        // 遍历IP地址
        do
        {
            // 格式化IP地址字符串
            zbx_snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (unsigned int)ipaddress[0],
                        (unsigned int)ipaddress[1], (unsigned int)ipaddress[2],
                        (unsigned int)ipaddress[3]);

            // 初始化DB_DHOST结构体
            memset(&dhost, 0, sizeof(dhost));
            host_status = -1;

            // 获取当前时间
            now = time(NULL);

            // 输出日志，记录当前IP地址
            zabbix_log(LOG_LEVEL_DEBUG, "%s() ip:'%s'", __function_name, ip);

            // 设置 alarm，超时后触发
            zbx_alarm_on(CONFIG_TIMEOUT);
            // 通过IP地址获取主机名和DNS名称
            zbx_gethost_by_ip(ip, dns, sizeof(dns));
            // 关闭 alarm
            zbx_alarm_off();

            // 判断是否需要执行独特的主机检查
            if (0 != drule->unique_dcheckid)
            {
                // 处理主机检查
                process_checks(drule, &dhost, &host_status, ip, dns, 1, now);
            }
            // 执行主机检查
            process_checks(drule, &dhost, &host_status, ip, dns, 0, now);

            // 开始事务
            DBbegin();

            // 检查主机记录是否已存在
            if (SUCCEED != DBlock_druleid(drule->druleid))
            {
                // 回滚事务
                DBrollback();

                // 输出日志，记录主机记录已删除
                zabbix_log(LOG_LEVEL_DEBUG, "discovery rule '%s' was deleted during processing,"
                           " stopping", drule->name);

                // 跳过此规则，继续处理下一个
                goto out;
            }

            // 更新主机记录
            if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
            {
                // 如果是服务器类型，更新主机记录
                discovery_update_host(&dhost, host_status, now);
            }
            else if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
            {
                // 如果是代理类型，更新主机记录
                proxy_update_host(drule->druleid, ip, dns, host_status, now);
            }

            // 提交事务
            DBcommit();

        }
        while (SUCCEED == iprange_next(&iprange, ipaddress));

        // 跳到下一个IP范围
        next:
        if (NULL != comma)
        {
            // 重置逗号位置
            *comma = ',';
            start = comma + 1;
        }
        else
        {
            // 结束循环
            break;
        }
    }

    // 输出日志，记录函数结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


				goto out;
			}

			if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
				discovery_update_host(&dhost, host_status, now);
			else if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
				proxy_update_host(drule->druleid, ip, dns, host_status, now);
/******************************************************************************
 * *
 *这段代码的主要目的是清理 discovery 相关的数据，包括删除不符合条件的 dhosts（主机）和 ds
 ******************************************************************************/
static void discovery_clean_services(zbx_uint64_t druleid)
{
	const char *__function_name = "discovery_clean_services";

	/* 声明变量 */
	DB_RESULT		result;
	DB_ROW			row;
	char			*iprange = NULL;
	zbx_vector_uint64_t	keep_dhostids, del_dhostids, del_dserviceids;
	zbx_uint64_t		dhostid, dserviceid;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 查询数据库获取iprange */
	result = DBselect("select iprange from drules where druleid=" ZBX_FS_UI64, druleid);

	/* 获取iprange */
	if (NULL != (row = DBfetch(result)))
		iprange = zbx_strdup(iprange, row[0]);

	/* 释放结果集 */
	DBfree_result(result);

	/* 初始化vector */
	zbx_vector_uint64_create(&keep_dhostids);
	zbx_vector_uint64_create(&del_dhostids);
	zbx_vector_uint64_create(&del_dserviceids);

	/* 查询数据库获取dhosts和dservices */
	result = DBselect(
			"select dh.dhostid,ds.dserviceid,ds.ip"
			" from dhosts dh"
				" left join dservices ds"
					" on dh.dhostid=ds.dhostid"
			" where dh.druleid=" ZBX_FS_UI64,
			druleid);

	/* 遍历结果集，分类处理dhosts和dservices */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(dhostid, row[0]);

		/* 判断dservice是否为空，如果是，则删除dhost */
		if (SUCCEED == DBis_null(row[1]))
		{
			zbx_vector_uint64_append(&del_dhostids, dhostid);
		}
		else /* 判断ip是否在iprange中，如果不在，则删除dhost和dservice */
		{
			ZBX_STR2UINT64(dserviceid, row[1]);

			zbx_vector_uint64_append(&del_dhostids, dhostid);
			zbx_vector_uint64_append(&del_dserviceids, dserviceid);
		}
		else /* 如果在iprange中，则保留dhost */
		{
			zbx_vector_uint64_append(&keep_dhostids, dhostid);
		}
	}
	/* 释放结果集 */
	DBfree_result(result);

	/* 释放iprange */
	zbx_free(iprange);

	/* 如果del_dserviceids不为空，则执行删除操作 */
	if (0 != del_dserviceids.values_num)
	{
		int	i;

		/* 删除dservices */

		zbx_vector_uint64_sort(&del_dserviceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from dservices where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "dserviceid",
				del_dserviceids.values, del_dserviceids.values_num);

		DBexecute("%s", sql);

		/* 删除dhosts */

		zbx_vector_uint64_sort(&keep_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&keep_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		zbx_vector_uint64_sort(&del_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&del_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		for (i = 0; i < del_dhostids.values_num; i++)
		{
			dhostid = del_dhostids.values[i];

			/* 如果在keep_dhostids中，则删除dhosts */
			if (FAIL != zbx_vector_uint64_bsearch(&keep_dhostids, dhostid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				zbx_vector_uint64_remove_noorder(&del_dhostids, i--);
		}
	}

	/* 如果del_dhostids不为空，则执行删除操作 */
	if (0 != del_dhostids.values_num)
	{
		zbx_vector_uint64_sort(&del_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from dhosts where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "dhostid",
				del_dhostids.values, del_dhostids.values_num);

		DBexecute("%s", sql);
	}

	/* 释放资源 */
	zbx_free(sql);

	/* 销毁vector */
	zbx_vector_uint64_destroy(&del_dserviceids);
	zbx_vector_uint64_destroy(&del_dhostids);
	zbx_vector_uint64_destroy(&keep_dhostids);

out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

				zbx_vector_uint64_remove_noorder(&del_dhostids, i--);
		}
	}

	if (0 != del_dhostids.values_num)
	{
		zbx_vector_uint64_sort(&del_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from dhosts where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "dhostid",
				del_dhostids.values, del_dhostids.values_num);

		DBexecute("%s", sql);
	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&del_dserviceids);
	zbx_vector_uint64_destroy(&del_dhostids);
	zbx_vector_uint64_destroy(&keep_dhostids);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *这段代码的主要目的是处理监控中的发现规则。它从数据库中查询满足条件的发现规则，然后对每个规则进行处理。处理过程中包括检查延迟字符串的合法性、更新规则的下次检查时间以及清理相关服务。最后，返回处理的规则数量作为性能指标。
 ******************************************************************************/
static int	process_discovery(void)
{
	/* 定义变量 */
	DB_RESULT	result;
	DB_ROW		row;
	int		rule_count = 0;
	char		*delay_str = NULL;

	/* 从数据库中查询规则 */
	result = DBselect(
			"select distinct r.druleid,r.iprange,r.name,c.dcheckid,r.proxy_hostid,r.delay"
			" from drules r"
				" left join dchecks c"
					" on c.druleid=r.druleid"
						" and c.uniq=1"
			" where r.status=%d"
				" and r.nextcheck<=%d"
				" and " ZBX_SQL_MOD(r.druleid,%d) "=%d",
			DRULE_STATUS_MONITORED,
			(int)time(NULL),
			CONFIG_DISCOVERER_FORKS,
			process_num - 1);

	/* 遍历查询结果 */
	while (ZBX_IS_RUNNING() && NULL != (row = DBfetch(result)))
	{
		int		now, delay;
		zbx_uint64_t	druleid;

		/* 计数器加一 */
		rule_count++;

		/* 将字符串转换为整数 */
		ZBX_STR2UINT64(druleid, row[0]);

		/* 复制延迟字符串 */
		delay_str = zbx_strdup(delay_str, row[5]);
		/* 替换简单宏 */
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &delay_str,
				MACRO_TYPE_COMMON, NULL, 0);

		/* 检查延迟字符串是否合法 */
		if (SUCCEED != is_time_suffix(delay_str, &delay, ZBX_LENGTH_UNLIMITED))
		{
			/* 记录日志 */
			zbx_config_t	cfg;

			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": invalid update interval \"%s\"",
					row[2], delay_str);

			/* 获取配置信息 */
			zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_REFRESH_UNSUPPORTED);

			/* 更新当前时间 */
			now = (int)time(NULL);

			/* 更新规则下次检查时间 */
			DBexecute("update drules set nextcheck=%d where druleid=" ZBX_FS_UI64,
					(0 == cfg.refresh_unsupported || 0 > now + cfg.refresh_unsupported ?
					ZBX_JAN_2038 : now + cfg.refresh_unsupported), druleid);

			/* 清理配置信息 */
			zbx_config_clean(&cfg);
			/* 继续处理下一个规则 */
			continue;
		}

		/* 如果检查项为空，则处理规则 */
		if (SUCCEED == DBis_null(row[4]))
		{
			DB_DRULE	drule;

			/* 清空结构体 */
			memset(&drule, 0, sizeof(drule));

			/* 填充规则信息 */
			drule.druleid = druleid;
			drule.iprange = row[1];
			drule.name = row[2];
			ZBX_DBROW2UINT64(drule.unique_dcheckid, row[3]);

			/* 处理规则 */
			process_rule(&drule);
		}

		/* 如果程序类型包含服务器，则清理服务 */
		if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
			discovery_clean_services(druleid);

		/* 更新当前时间 */
		now = (int)time(NULL);
		/* 检查下次检查时间是否合理 */
		if (0 > now + delay)
		{
			/* 记录日志 */
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": nextcheck update causes overflow",
					row[2]);

			/* 更新规则下次检查时间 */
			DBexecute("update drules set nextcheck=%d where druleid=" ZBX_FS_UI64, ZBX_JAN_2038, druleid);
		}
		else
			/* 更新规则下次检查时间 */
			DBexecute("update drules set nextcheck=%d where druleid=" ZBX_FS_UI64, now + delay, druleid);
	}

	/* 释放资源 */
	DBfree_result(result);
	/* 释放延迟字符串 */
	zbx_free(delay_str);

	/* 返回计数器值作为性能指标 */
	return rule_count;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是从一个数据库表（名为drules）中查询出最小nextcheck值。查询条件为：状态为DRULE_STATUS_MONITORED，druleidmod值为CONFIG_DISCOVERER_FORKS，且process_num - 1。如果查询结果不为空，则返回最小nextcheck值。如果查询结果为空，则记录一条调试日志并返回FAIL。
 ******************************************************************************/
// 定义一个静态函数get_minnextcheck，无返回值
static int get_minnextcheck(void)
{
    // 定义一个DB_RESULT类型的变量result，用于存储数据库查询结果
    DB_RESULT	result;
    // 定义一个DB_ROW类型的变量row，用于存储数据库查询的一行数据
    DB_ROW		row;
    // 定义一个整型变量res，初始值为FAIL
    int		res = FAIL;

    // 执行数据库查询，查询语句如下：
    // "select count(*),min(nextcheck) from drules
    // where status=%d and ZBX_SQL_MOD(druleid,%d)=%d"
    // 参数分别为：DRULE_STATUS_MONITORED、CONFIG_DISCOVERER_FORKS、process_num - 1
    result = DBselect(
            "select count(*),min(nextcheck)"
            " from drules"
            " where status=%d"
            " and " ZBX_SQL_MOD(druleid,%d) "=%d",
            DRULE_STATUS_MONITORED, CONFIG_DISCOVERER_FORKS, process_num - 1);

    // 从数据库查询结果中获取一行数据，存储在row变量中
    row = DBfetch(result);

    // 判断row是否为空，或者row[0]或row[1]是否为空
    if (NULL == row || DBis_null(row[0]) == SUCCEED || DBis_null(row[1]) == SUCCEED)
    {
        // 如果满足以上条件，表示没有需要更新的项目，记录日志并返回
        zabbix_log(LOG_LEVEL_DEBUG, "get_minnextcheck(): no items to update");
        return res;
    }
    else if (0 != atoi(row[0]))
    {
        // 如果row[0]不为空，表示找到了最小nextcheck值，将其转换为整型并赋值给res
        res = atoi(row[1]);
    }

/******************************************************************************
 * *
 *整个代码块的主要目的是运行一个后台进程，连接数据库，执行发现操作，并定期更新进程状态。其中，使用了循环结构来实现这个功能。在循环内部，首先连接数据库，然后执行发现操作，并更新规则计数和总处理时间。同时，设置了进程标题来显示当前进程的状态。每隔一段时间，就会更新一次进程状态，并计算睡眠时间。当程序运行完毕后，关闭数据库连接并等待程序退出。
 ******************************************************************************/
// 定义日志级别，打印启动信息
zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
           server_num, get_process_type_string(process_type), process_num);

// 更新进程状态计数器
update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 初始化SNMP
#ifdef HAVE_NETSNMP
    zbx_init_snmp();
#endif

// 定义状态更新间隔
#define STAT_INTERVAL 5

// 初始化TLS
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    zbx_tls_init_child();
#endif

// 设置进程标题
zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);

// 记录上次状态更新时间
last_stat_time = time(NULL);

// 连接数据库
DBconnect(ZBX_DB_CONNECT_NORMAL);

// 循环运行，直到程序退出
while (ZBX_IS_RUNNING())
{
    // 获取当前时间
    sec = zbx_time();
    // 更新环境变量
    zbx_update_env(sec);

    // 如果设置了睡眠时间
    if (0 != sleeptime)
    {
        // 设置进程标题
        zbx_setproctitle("%s #%d [processed %d rules in " ZBX_FS_DBL " sec, performing discovery]",
                        get_process_type_string(process_type), process_num, old_rule_count,
                        old_total_sec);
    }

    // 执行发现操作，并更新规则计数和总处理时间
    rule_count += process_discovery();
    total_sec += zbx_time() - sec;

    // 获取下一次检查的时间
    nextcheck = get_minnextcheck();
    // 计算睡眠时间
    sleeptime = calculate_sleeptime(nextcheck, DISCOVERER_DELAY);

    // 如果设置了睡眠时间或者间隔时间到达
    if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
    {
        // 打印进程状态信息
        if (0 == sleeptime)
        {
            zbx_setproctitle("%s #%d [processed %d rules in " ZBX_FS_DBL " sec, performing "
                            "discovery]", get_process_type_string(process_type), process_num,
                            rule_count, total_sec);
        }
        else
        {
            zbx_setproctitle("%s #%d [processed %d rules in " ZBX_FS_DBL " sec, idle %d sec]",
                            get_process_type_string(process_type), process_num, rule_count,
                            total_sec, sleeptime);
            old_rule_count = rule_count;
            old_total_sec = total_sec;
        }
        // 重置规则计数和总处理时间
        rule_count = 0;
        total_sec = 0.0;
        // 更新上次状态更新时间
        last_stat_time = time(NULL);
    }

    // 睡眠一段时间
    zbx_sleep_loop(sleeptime);
}

// 设置进程标题，表示程序已终止
zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

// 循环等待，直到程序退出
while (1)
    zbx_sleep(SEC_PER_MIN);

						rule_count, total_sec);
			}
			else
			{
				zbx_setproctitle("%s #%d [processed %d rules in " ZBX_FS_DBL " sec, idle %d sec]",
						get_process_type_string(process_type), process_num, rule_count,
						total_sec, sleeptime);
				old_rule_count = rule_count;
				old_total_sec = total_sec;
			}
			rule_count = 0;
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
