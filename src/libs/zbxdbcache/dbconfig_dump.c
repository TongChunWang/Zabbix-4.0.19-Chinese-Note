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
#include "log.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "mutexs.h"

#define ZBX_DBCONFIG_IMPL
#include "dbconfig.h"

/******************************************************************************
 * *
 *整个代码块的主要目的是打印出配置文件中的相关信息。函数DCdump_config()逐行解析配置文件中的各项配置，并将它们打印到日志中。这些配置包括：
 *
 *1. refresh_unsupported：刷新不被支持的配置。
 *2. discovery_groupid：发现组ID。
 *3. snmptrap_logging：SNMP陷阱日志记录。
 *4. default_inventory_mode：默认库存模式。
 *5. severity names：严重性名称。
 *6. housekeeping：housekeeping相关配置，包括事件、审计、IT服务、用户会话、历史和趋势等。
 *
 *通过这个函数，可以方便地查看和调试配置文件中的各项设置。
 ******************************************************************************/
// 定义一个静态函数DCdump_config，用于打印配置信息
static void DCdump_config(void)
{
    // 定义一个字符串指针，用于存储函数名
    const char *__function_name = "DCdump_config";

    // 定义一个整型变量i，用于循环使用
    int i;

    // 使用zabbix_log记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 判断config->config是否为空，如果为空，直接跳出函数
    if (NULL == config->config)
        goto out;

    // 使用zabbix_log记录日志，打印配置信息
    zabbix_log(LOG_LEVEL_TRACE, "refresh_unsupported:%d", config->config->refresh_unsupported);
    zabbix_log(LOG_LEVEL_TRACE, "discovery_groupid:" ZBX_FS_UI64, config->config->discovery_groupid);
    zabbix_log(LOG_LEVEL_TRACE, "snmptrap_logging:%u", config->config->snmptrap_logging);
    zabbix_log(LOG_LEVEL_TRACE, "default_inventory_mode:%d", config->config->default_inventory_mode);

    // 打印严重性名称
    zabbix_log(LOG_LEVEL_TRACE, "severity names:");
    for (i = 0; TRIGGER_SEVERITY_COUNT > i; i++)
        zabbix_log(LOG_LEVEL_TRACE, "  %s", config->config->severity_name[i]);

    // 打印housekeeping相关配置信息
    zabbix_log(LOG_LEVEL_TRACE, "housekeeping:");
    zabbix_log(LOG_LEVEL_TRACE, "  events, mode:%u period:[trigger:%d internal:%d autoreg:%d discovery:%d]",
              config->config->hk.events_mode, config->config->hk.events_trigger,
              config->config->hk.events_internal, config->config->hk.events_autoreg,
			config->config->hk.events_discovery);

	zabbix_log(LOG_LEVEL_TRACE, "  audit, mode:%u period:%d", config->config->hk.audit_mode,
			config->config->hk.audit);

	zabbix_log(LOG_LEVEL_TRACE, "  it services, mode:%u period:%d", config->config->hk.services_mode,
			config->config->hk.services);

	zabbix_log(LOG_LEVEL_TRACE, "  user sessions, mode:%u period:%d", config->config->hk.sessions_mode,
			config->config->hk.sessions);

	zabbix_log(LOG_LEVEL_TRACE, "  history, mode:%u global:%u period:%d", config->config->hk.history_mode,
			config->config->hk.history_global, config->config->hk.history);

	zabbix_log(LOG_LEVEL_TRACE, "  trends, mode:%u global:%u period:%d", config->config->hk.trends_mode,
			config->config->hk.trends_global, config->config->hk.trends);

out:
	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历主机配置，获取主机的相关信息，并将这些信息打印到日志中。这个函数可以帮助开发人员或系统管理员了解服务器上的主机状态，以便进行故障排查或性能优化。
 ******************************************************************************/
/* 定义静态函数DCdump_hosts，用于打印zbx服务器上的主机信息 */
static void DCdump_hosts(void)
{
	/* 定义常量字符串，表示函数名 */
	const char *__function_name = "DCdump_hosts";

	/* 定义主机结构体指针 */
	ZBX_DC_HOST *host;
	/* 定义zbx_hashset_iter结构体指针 */
	zbx_hashset_iter_t iter;
	/* 定义zbx_vector_ptr结构体指针 */
	zbx_vector_ptr_t index;
	/* 定义整型变量，用于循环计数 */
	int i;

	/* 打印日志，表示进入函数 */
	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	/* 创建zbx_vector_ptr，用于存储主机 */
	zbx_vector_ptr_create(&index);
	/* 重置zbx_hashset_iter，准备遍历主机配置 */
	zbx_hashset_iter_reset(&config->hosts, &iter);

	/* 遍历主机配置，将主机添加到index中 */
	while (NULL != (host = (ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, host);

	/* 对index中的主机进行排序 */
	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	/* 遍历排序后的主机，打印主机信息 */
	for (i = 0; i < index.values_num; i++)
	{
		int j;

		host = (ZBX_DC_HOST *)index.values[i];
		/* 打印主机ID、主机名、状态等信息 */
		zabbix_log(LOG_LEVEL_TRACE, "hostid:" ZBX_FS_UI64 " host:'%s' name:'%s' status:%u", host->hostid,
				host->host, host->name, host->status);

		/* 打印代理主机ID、数据预期从哪个代理获取等信息 */
		zabbix_log(LOG_LEVEL_TRACE, "  proxy_hostid:" ZBX_FS_UI64, host->proxy_hostid);
		zabbix_log(LOG_LEVEL_TRACE, "  data_expected_from:%d", host->data_expected_from);

		/* 打印主机zbix、snmp、ipmi、jmx等接口的可用性、错误等信息 */
		zabbix_log(LOG_LEVEL_TRACE, "  zabbix:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->available, host->errors_from, host->disable_until, host->error);
		zabbix_log(LOG_LEVEL_TRACE, "  snmp:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->snmp_available, host->snmp_errors_from, host->snmp_disable_until,
				host->snmp_error);
		zabbix_log(LOG_LEVEL_TRACE, "  ipmi:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->ipmi_available, host->ipmi_errors_from, host->ipmi_disable_until,
				host->ipmi_error);
		zabbix_log(LOG_LEVEL_TRACE, "  jmx:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->jmx_available, host->jmx_errors_from, host->jmx_disable_until, host->jmx_error);

		/* 打印主机上次可用性状态更改的时间戳 */
		zabbix_log(LOG_LEVEL_TRACE, "  availability_ts:%d", host->availability_ts);

		/* 打印主机维护ID、维护状态、维护类型、维护开始时间等信息 */
		zabbix_log(LOG_LEVEL_TRACE, "  maintenanceid:" ZBX_FS_UI64 " maintenance_status:%u maintenance_type:%u"
				" maintenance_from:%d", host->maintenanceid, host->maintenance_status,
				host->maintenance_type, host->maintenance_from);

		/* 打印主机物品数量等信息 */
		zabbix_log(LOG_LEVEL_TRACE, "  number of items: zabbix:%d snmp:%d ipmi:%d jmx:%d", host->items_num,
				host->snmp_items_num, host->ipmi_items_num, host->jmx_items_num);

		/* 打印主机TLS连接和接受状态等信息 */
		zabbix_log(LOG_LEVEL_TRACE, "  tls:[connect:%u accept:%u]", host->tls_connect, host->tls_accept);

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zabbix_log(LOG_LEVEL_TRACE, "  tls:[issuer:'%s' subject:'%s']", host->tls_issuer, host->tls_subject);

		if (NULL != host->tls_dc_psk)
		{
			zabbix_log(LOG_LEVEL_TRACE, "  tls:[psk_identity:'%s' psk:'%s' dc_psk:%u]",
					host->tls_dc_psk->tls_psk_identity, host->tls_dc_psk->tls_psk,
					host->tls_dc_psk->refcount);
		}
#endif
		for (j = 0; j < host->interfaces_v.values_num; j++)
		{
			ZBX_DC_INTERFACE	*interface = (ZBX_DC_INTERFACE *)host->interfaces_v.values[j];

			zabbix_log(LOG_LEVEL_TRACE, "  interfaceid:" ZBX_FS_UI64, interface->interfaceid);
		}
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历配置中的代理，将代理信息添加到索引vector中，然后对索引vector进行排序，最后输出排序后的代理信息。这个函数用于诊断和调试代理配置问题。
 ******************************************************************************/
// 定义一个静态函数DCdump_proxies，该函数没有任何返回值，也就是void类型
static void DCdump_proxies(void)
{
    // 定义一个字符串常量，表示函数名，方便日志输出
    const char *__function_name = "DCdump_proxies";

    // 定义一个ZBX_DC_PROXY类型的指针，用于指向代理结构体
    ZBX_DC_PROXY *proxy;

    // 定义一个zbx_hashset_iter_t类型的变量，用于迭代配置中的代理
    zbx_hashset_iter_t iter;

    // 定义一个zbx_vector_ptr_t类型的变量，用于存储代理索引
    zbx_vector_ptr_t index;

    // 定义一个整型变量，用于循环计数
    int i;

    // 使用zabbix_log输出日志，表示进入该函数
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 创建一个zbx_vector_ptr类型的对象，用于存储代理
    zbx_vector_ptr_create(&index);

    // 重置zbx_hashset_iter，开始迭代配置中的代理
    zbx_hashset_iter_reset(&config->proxies, &iter);

    // 遍历迭代器，将迭代到的代理添加到索引vector中
    while (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, proxy);

	// 对index vector中的ipmihost数组进行排序，使用默认的排序函数
	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	// 遍历排序后的ipmihost数组，打印每个元素的详细信息
	for (i = 0; i < index.values_num; i++)
	{
		proxy = (ZBX_DC_PROXY *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "hostid:" ZBX_FS_UI64 " location:%u", proxy->hostid, proxy->location);
		zabbix_log(LOG_LEVEL_TRACE, "  proxy_address:'%s'", proxy->proxy_address);
		zabbix_log(LOG_LEVEL_TRACE, "  compress:%d", proxy->auto_compress);

	}

	// 释放index vector内存
	zbx_vector_ptr_destroy(&index);

	// 使用zabbix_log输出日志，表示结束DCdump_ipmihosts函数
	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

static void	DCdump_ipmihosts(void)
{
	const char		*__function_name = "DCdump_ipmihosts";

	ZBX_DC_IPMIHOST		*ipmihost;
	zbx_hashset_iter_t	iter;
	zbx_vector_ptr_t	index;
	int			i;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	zbx_vector_ptr_create(&index);
	zbx_hashset_iter_reset(&config->ipmihosts, &iter);

	while (NULL != (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, ipmihost);

	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		ipmihost = (ZBX_DC_IPMIHOST *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "hostid:" ZBX_FS_UI64 " ipmi:[username:'%s' password:'%s' authtype:%d"
				" privilege:%u]", ipmihost->hostid, ipmihost->ipmi_username, ipmihost->ipmi_password,
				ipmihost->ipmi_authtype, ipmihost->ipmi_privilege);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历哈希集中的主机库存信息，并将它们添加到一个新的vector中。然后对vector进行排序，最后遍历vector，打印出每个主机库存的主机ID、库存模式以及各个字段的信息。这个过程用于诊断和调试程序。
 ******************************************************************************/
// 定义一个静态函数，用于打印主机库存信息
static void DCdump_host_inventories(void)
{
    // 定义一些变量
	const char			*__function_name = "DCdump_host_inventories"; // 函数名指针
    ZBX_DC_HOST_INVENTORY *host_inventory; // 主机库存指针
    zbx_hashset_iter_t iter; // 哈希集迭代器
    zbx_vector_ptr_t index; //  vector指针
    int i, j; // 循环变量

    // 记录日志
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 创建vector
    zbx_vector_ptr_create(&index);

    // 重置哈希集迭代器
    zbx_hashset_iter_reset(&config->host_inventories, &iter);

    // 遍历哈希集，将主机库存添加到vector中
    while (NULL != (host_inventory = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_iter_next(&iter)))
        zbx_vector_ptr_append(&index, host_inventory);

    // 对vector进行排序
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 遍历vector，打印主机库存信息
    for (i = 0; i < index.values_num; i++)
    {
        host_inventory = (ZBX_DC_HOST_INVENTORY *)index.values[i];
        zabbix_log(LOG_LEVEL_TRACE, "hostid:" ZBX_FS_UI64 " inventory_mode:%u", host_inventory->hostid,
                    host_inventory->inventory_mode);
		for (j = 0; j < HOST_INVENTORY_FIELD_COUNT; j++)
		{
			zabbix_log(LOG_LEVEL_TRACE, "  %s: '%s'", DBget_inventory_field(j + 1),
					host_inventory->values[j]);
		}
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "  End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历zbx_hashset中的所有htmpl结构体，并将它们的信息打印出来。代码首先创建一个zbx_vector_ptr结构体用于存储htmpl结构体，然后使用zbx_hashset_iter迭代器遍历zbx_hashset中的所有htmpl结构体。接下来，对遍历到的htmpl结构体进行排序，最后使用循环遍历vector中的所有htmpl结构体，并打印它们的hostid和templateids信息。在循环结束后，销毁vector并记录函数退出日志。
 ******************************************************************************/
// 定义一个静态函数DCdump_htmpls，该函数用于打印zbx_hashset中的所有htmpl结构体的信息
static void DCdump_htmpls(void)
{
    // 定义一个指向__function_name字符串的常量指针
    const char *__function_name = "DCdump_htmpls";

    // 定义一个指向zbx_dc_htmpl结构的指针
    ZBX_DC_HTMPL *htmpl = NULL;

    // 定义一个zbx_hashset迭代器
    zbx_hashset_iter_t iter;

    // 定义一个zbx_vector_ptr结构体指针，用于存储htmpl结构体
    zbx_vector_ptr_t index;

    // 定义两个整数变量i和j，用于循环计数
    int i, j;

    // 使用zabbix_log记录函数进入日志，日志级别为TRACE，打印函数名
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 创建一个zbx_vector_ptr结构体，用于存储htmpl结构体
    zbx_vector_ptr_create(&index);

    // 重置zbx_hashset_iter迭代器，准备遍历config->htmpls集合
    zbx_hashset_iter_reset(&config->htmpls, &iter);

    // 使用循环遍历zbx_hashset中的所有htmpl结构体
    while (NULL != (htmpl = (ZBX_DC_HTMPL *)zbx_hashset_iter_next(&iter)))
        // 将当前htmpl结构体添加到index vector中
        zbx_vector_ptr_append(&index, htmpl);

	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		htmpl = (ZBX_DC_HTMPL *)index.values[i];

		zabbix_log(LOG_LEVEL_TRACE, "hostid:" ZBX_FS_UI64, htmpl->hostid);

		for (j = 0; j < htmpl->templateids.values_num; j++)
			zabbix_log(LOG_LEVEL_TRACE, "  templateid:" ZBX_FS_UI64, htmpl->templateids.values[j]);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历配置文件中的全局宏，并将它们打印出来。函数`DCdump_gmacros`实现了这个功能。首先，它创建一个索引 vector，然后使用迭代器遍历全局宏列表，将每个全局宏添加到索引 vector 中。接着，对索引 vector 进行排序。最后，遍历索引 vector 中的全局宏，并打印它们的详细信息。在整个过程中，还使用了日志记录函数来记录函数的执行情况。
 ******************************************************************************/
// 定义一个静态函数，用于打印全局宏定义
static void DCdump_gmacros(void)
{
    // 定义一些变量
    const char *__function_name = "DCdump_gmacros"; // 函数名
    ZBX_DC_GMACRO *gmacro; // 全局宏指针
    zbx_hashset_iter_t iter; // 迭代器
    zbx_vector_ptr_t index; // 索引 vector
    int i; // 循环变量

    // 打印日志，表示进入函数
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 创建一个索引 vector
    zbx_vector_ptr_create(&index);

    // 重置配置文件中的全局宏迭代器
    zbx_hashset_iter_reset(&config->gmacros, &iter);

    // 遍历全局宏列表
    while (NULL != (gmacro = (ZBX_DC_GMACRO *)zbx_hashset_iter_next(&iter)))
        // 将全局宏添加到索引 vector
        zbx_vector_ptr_append(&index, gmacro);

    // 对索引 vector 进行排序
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 遍历索引 vector 中的全局宏
    for (i = 0; i < index.values_num; i++)
    {
        gmacro = (ZBX_DC_GMACRO *)index.values[i];

        // 打印全局宏信息
        zabbix_log(LOG_LEVEL_TRACE, "globalmacroid:" ZBX_FS_UI64 " macro:'%s' value:'%s' context:'%s'",
                    gmacro->globalmacroid, gmacro->macro,
                    gmacro->value, ZBX_NULL2EMPTY_STR(gmacro->context));
    }

    // 销毁索引 vector
    zbx_vector_ptr_destroy(&index);

    // 打印日志，表示函数结束
    zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是遍历配置文件中的宏定义，将其添加到vector中，并对vector进行排序，然后逐个打印宏定义的信息。
 ******************************************************************************/
/* 定义一个静态函数，用于打印配置文件中的所有宏定义 */
static void DCdump_hmacros(void)
{
    /* 定义一个指向函数名的常量字符串 */
    const char *__function_name = "DCdump_hmacros";

    /* 定义一个指向宏定义结构的指针 */
    ZBX_DC_HMACRO *hmacro;

    // 定义一个zbx_hashset迭代器
    zbx_hashset_iter_t iter;
    // 定义一个zbx_vector_ptr结构的指针，用于存储接口
    zbx_vector_ptr_t index;
    // 定义一个整数变量，用于循环计数
    int i;

    // 打印日志，表示进入DCdump_interfaces函数
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 创建一个zbx_vector_ptr结构，用于存储接口
    zbx_vector_ptr_create(&index);
    // 重置zbx_hashset迭代器，准备遍历接口
	zbx_hashset_iter_reset(&config->hmacros, &iter);

    // 遍历zbx_hashset中的接口，将其添加到zbx_vector_ptr中
	while (NULL != (hmacro = (ZBX_DC_HMACRO *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, hmacro);

    // 对zbx_vector_ptr中的接口进行排序
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 遍历排序后的zbx_vector_ptr，打印接口信息
    for (i = 0; i < index.values_num; i++)
    {
		hmacro = (ZBX_DC_HMACRO *)index.values[i];

        zabbix_log(LOG_LEVEL_TRACE, "hostmacroid:" ZBX_FS_UI64 " hostid:" ZBX_FS_UI64 " macro:'%s' value:'%s'"
                " context '%s'", hmacro->hostmacroid, hmacro->hostid, hmacro->macro, hmacro->value,
                ZBX_NULL2EMPTY_STR(hmacro->context));
    }

    /* 销毁vector迭代器 */
    zbx_vector_ptr_destroy(&index);

    /* 记录日志，表示函数执行结束 */
    zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

static void	DCdump_interfaces(void)
{
	const char	*__function_name = "DCdump_interfaces";

	ZBX_DC_INTERFACE	*interface;
	zbx_hashset_iter_t	iter;
	zbx_vector_ptr_t	index;
	int			i;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	zbx_vector_ptr_create(&index);
	zbx_hashset_iter_reset(&config->interfaces, &iter);

	while (NULL != (interface = (ZBX_DC_INTERFACE *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, interface);

	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		interface = (ZBX_DC_INTERFACE *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "interfaceid:" ZBX_FS_UI64 " hostid:" ZBX_FS_UI64 " ip:'%s' dns:'%s'"
				" port:'%s' type:%u main:%u useip:%u bulk:%u",
				interface->interfaceid, interface->hostid, interface->ip, interface->dns,
				interface->port, interface->type, interface->main, interface->useip, interface->bulk);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *代码主要目的是：定义一个名为 DCdump_numitem 的静态函数，用于输出 ZBX_DC_NUMITEM 结构中的 units 和 trends 成员。
 *
 *注释详细说明：
 *
 *1. 首先，定义一个名为 DCdump_numitem 的静态函数。
 *2. 函数参数为一个指向 ZBX_DC_NUMITEM 结构的指针，表示要操作的 ZBX_DC_NUMITEM 结构。
 *3. 使用 zabbix_log 函数输出日志，日志级别为 TRACE。
 *4. 打印 numitem 结构的 units 和 trends 成员，单位为 \"%s\" 和 \"%d\"。
 *5. units 成员表示数据的单位，trends 成员表示数据趋势。
 *
 *整个注释好的代码块如下：
 */
// 定义一个名为 DCdump_numitem 的静态函数，参数为一个指向 ZBX_DC_NUMITEM 结构的指针
static void	DCdump_numitem(const ZBX_DC_NUMITEM *numitem)
{
    // 输出日志，日志级别为 TRACE，打印 numitem 结构的 units 和 trends 成员
    zabbix_log(LOG_LEVEL_TRACE, "  units:'%s' trends:%d", numitem->units, numitem->trends);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是输出 SNMP 相关信息的日志。这个函数接收一个 ZBX_DC_SNMPITEM 结构指针作为参数，然后使用 zabbix_log 函数分别输出该结构中的 snmp、snmpv3_securityname、snmpv3_authpassphrase、snmpv3_privpassphrase、snmpv3_contextname、snmpv3_securitylevel、snmpv3_authprotocol 和 snmpv3_privprotocol 等信息。
 ******************************************************************************/
// 定义一个名为 DCdump_snmpitem 的静态函数，参数为一个指向 ZBX_DC_SNMPITEM 结构的指针
static void DCdump_snmpitem(const ZBX_DC_SNMPITEM *snmpitem)
{
    // 使用 zabbix_log 函数记录日志，LOG_LEVEL_TRACE 表示日志级别为 trace，输出 snmpitem 结构中的信息
    zabbix_log(LOG_LEVEL_TRACE, "  snmp:[oid:'%s' community:'%s' oid_type:%u]", snmpitem->snmp_oid,
                snmpitem->snmp_community, snmpitem->snmp_oid_type);

    // 使用 zabbix_log 函数记录日志，LOG_LEVEL_TRACE 表示日志级别为 trace，输出 snmpv3 结构中的信息
    zabbix_log(LOG_LEVEL_TRACE, "  snmpv3:[securityname:'%s' authpassphrase:'%s' privpassphrase:'%s']",
                snmpitem->snmpv3_securityname, snmpitem->snmpv3_authpassphrase,
                snmpitem->snmpv3_privpassphrase);

    // 使用 zabbix_log 函数记录日志，LOG_LEVEL_TRACE 表示日志级别为 trace，输出 snmpv3 结构中的信息
    zabbix_log(LOG_LEVEL_TRACE, "  snmpv3:[contextname:'%s' securitylevel:%u authprotocol:%u privprotocol:%u]",
                snmpitem->snmpv3_contextname, snmpitem->snmpv3_securitylevel, snmpitem->snmpv3_authprotocol,
                snmpitem->snmpv3_privprotocol);
}

static void	DCdump_ipmiitem(const ZBX_DC_IPMIITEM *ipmiitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  ipmi_sensor:'%s'", ipmiitem->ipmi_sensor);
}

static void	DCdump_trapitem(const ZBX_DC_TRAPITEM *trapitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  trapper_hosts:'%s'", trapitem->trapper_hosts);
}

static void	DCdump_logitem(ZBX_DC_LOGITEM *logitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  logtimefmt:'%s'", logitem->logtimefmt);
}


static void	DCdump_dbitem(const ZBX_DC_DBITEM *dbitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  db:[params:'%s' username:'%s' password:'%s']", dbitem->params,
			dbitem->username, dbitem->password);
}

static void	DCdump_sshitem(const ZBX_DC_SSHITEM *sshitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  ssh:[username:'%s' password:'%s' authtype:%u params:'%s']",
			sshitem->username, sshitem->password, sshitem->authtype, sshitem->params);
	zabbix_log(LOG_LEVEL_TRACE, "  ssh:[publickey:'%s' privatekey:'%s']", sshitem->publickey,
			sshitem->privatekey);
}

static void	DCdump_httpitem(const ZBX_DC_HTTPITEM *httpitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  http:[url:'%s']", httpitem->url);
	zabbix_log(LOG_LEVEL_TRACE, "  http:[query fields:'%s']", httpitem->query_fields);
	zabbix_log(LOG_LEVEL_TRACE, "  http:[headers:'%s']", httpitem->headers);
	zabbix_log(LOG_LEVEL_TRACE, "  http:[posts:'%s']", httpitem->posts);

	zabbix_log(LOG_LEVEL_TRACE, "  http:[timeout:'%s' status codes:'%s' follow redirects:%u post type:%u"
			" http proxy:'%s' retrieve mode:%u request method:%u output format:%u allow traps:%u"
			" trapper_hosts:'%s']",
			httpitem->timeout, httpitem->status_codes, httpitem->follow_redirects, httpitem->post_type,
			httpitem->http_proxy, httpitem->retrieve_mode, httpitem->request_method,
			httpitem->output_format, httpitem->allow_traps, httpitem->trapper_hosts);

	zabbix_log(LOG_LEVEL_TRACE, "  http:[username:'%s' password:'%s' authtype:%u]",
			httpitem->username, httpitem->password, httpitem->authtype);
	zabbix_log(LOG_LEVEL_TRACE, "  http:[publickey:'%s' privatekey:'%s' ssl key password:'%s' verify peer:%u"
			" verify host:%u]", httpitem->ssl_cert_file, httpitem->ssl_key_file, httpitem->ssl_key_password,
			httpitem->verify_peer, httpitem->verify_host);
}

/******************************************************************************
 * *
 *这块代码的主要目的是输出ZBX_DC_TELNETITEM结构体的信息，包括用户名、密码和参数。函数DCdump_telnetitem接收一个ZBX_DC_TELNETITEM结构体的指针，然后使用zabbix_log函数将结构体的信息输出到日志中。其中，日志级别设置为TRACE，表示输出调试信息。
 ******************************************************************************/
// 定义一个静态函数，用于输出ZBX_DC_TELNETITEM结构体的信息
static void DCdump_telnetitem(const ZBX_DC_TELNETITEM *telnetitem)
{
    // 设置日志级别为TRACE，表示输出调试信息
	zabbix_log(LOG_LEVEL_TRACE, "  telnet:[username:'%s' password:'%s' params:'%s']", telnetitem->username,
			telnetitem->password, telnetitem->params);
}

static void	DCdump_simpleitem(const ZBX_DC_SIMPLEITEM *simpleitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  simple:[username:'%s' password:'%s']", simpleitem->username,
			simpleitem->password);
}

static void	DCdump_jmxitem(const ZBX_DC_JMXITEM *jmxitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  jmx:[username:'%s' password:'%s' endpoint:'%s']",
			jmxitem->username, jmxitem->password, jmxitem->jmx_endpoint);
}

static void	DCdump_calcitem(const ZBX_DC_CALCITEM *calcitem)
{
	zabbix_log(LOG_LEVEL_TRACE, "  calc:[params:'%s']", calcitem->params);
}

static void	DCdump_masteritem(const ZBX_DC_MASTERITEM *masteritem)
{
	int	i;

	zabbix_log(LOG_LEVEL_TRACE, "  dependent:");
	for (i = 0; i < masteritem->dep_itemids.values_num; i++)
		zabbix_log(LOG_LEVEL_TRACE, "    " ZBX_FS_UI64, masteritem->dep_itemids.values[i]);
}

static void	DCdump_preprocitem(const ZBX_DC_PREPROCITEM *preprocitem)
{
	int	i;

	zabbix_log(LOG_LEVEL_TRACE, "  preprocessing:");

	for (i = 0; i < preprocitem->preproc_ops.values_num; i++)
	{
                zabbix_log(LOG_LEVEL_TRACE, "   
		zbx_dc_preproc_op_t *op = (zbx_dc_preproc_op_t *)preprocitem->preproc_ops.values[i];

		// 打印日志，展示当前预处理操作的详细信息
		zabbix_log(LOG_LEVEL_TRACE, "      opid:" ZBX_FS_UI64 " step:%d type:%u params:'%s'",
				op->item_preprocid, op->step, op->type, op->params);
	}
}


/* item type specific information debug logging support */

typedef void (*zbx_dc_dump_func_t)(void *);

typedef struct
{
	zbx_hashset_t		*hashset;
	zbx_dc_dump_func_t	dump_func;
}
zbx_trace_item_t;

static void	DCdump_items(void)
{
	const char		*__function_name = "DCdump_items";

	ZBX_DC_ITEM		*item;
	zbx_hashset_iter_t	iter;
	int			i, j;
	zbx_vector_ptr_t	index;
	void			*ptr;
	zbx_trace_item_t	trace_items[] =
	{
		{&config->numitems, (zbx_dc_dump_func_t)DCdump_numitem},
		{&config->snmpitems, (zbx_dc_dump_func_t)DCdump_snmpitem},
		{&config->ipmiitems, (zbx_dc_dump_func_t)DCdump_ipmiitem},
		{&config->trapitems, (zbx_dc_dump_func_t)DCdump_trapitem},
		{&config->logitems, (zbx_dc_dump_func_t)DCdump_logitem},
		{&config->dbitems, (zbx_dc_dump_func_t)DCdump_dbitem},
		{&config->sshitems, (zbx_dc_dump_func_t)DCdump_sshitem},
		{&config->telnetitems, (zbx_dc_dump_func_t)DCdump_telnetitem},
		{&config->simpleitems, (zbx_dc_dump_func_t)DCdump_simpleitem},
		{&config->jmxitems, (zbx_dc_dump_func_t)DCdump_jmxitem},
		{&config->calcitems, (zbx_dc_dump_func_t)DCdump_calcitem},
		{&config->masteritems, (zbx_dc_dump_func_t)DCdump_masteritem},
		{&config->preprocitems, (zbx_dc_dump_func_t)DCdump_preprocitem},
		{&config->httpitems, (zbx_dc_dump_func_t)DCdump_httpitem},
	};

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	zbx_vector_ptr_create(&index);
	zbx_hashset_iter_reset(&config->items, &iter);

	while (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, item);

    // 对index中的SNMP接口项进行排序
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 使用for循环遍历排序后的SNMP接口项，并打印其相关信息
    for (i = 0; i < index.values_num; i++)
    {
        // 获取index中第i个SNMP接口项
		item = (ZBX_DC_ITEM *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "itemid:" ZBX_FS_UI64 " hostid:" ZBX_FS_UI64 " key:'%s'",
				item->itemid, item->hostid, item->key);
		zabbix_log(LOG_LEVEL_TRACE, "  type:%u value_type:%u", item->type, item->value_type);
		zabbix_log(LOG_LEVEL_TRACE, "  interfaceid:" ZBX_FS_UI64 " port:'%s'", item->interfaceid, item->port);
		zabbix_log(LOG_LEVEL_TRACE, "  state:%u error:'%s'", item->state, item->error);
		zabbix_log(LOG_LEVEL_TRACE, "  flags:%u status:%u", item->flags, item->status);
		zabbix_log(LOG_LEVEL_TRACE, "  valuemapid:" ZBX_FS_UI64, item->valuemapid);
		zabbix_log(LOG_LEVEL_TRACE, "  lastlogsize:" ZBX_FS_UI64 " mtime:%d", item->lastlogsize, item->mtime);
		zabbix_log(LOG_LEVEL_TRACE, "  delay:'%s' nextcheck:%d lastclock:%d", item->delay, item->nextcheck,
				item->lastclock);
		zabbix_log(LOG_LEVEL_TRACE, "  data_expected_from:%d", item->data_expected_from);
		zabbix_log(LOG_LEVEL_TRACE, "  history:%d history_sec:%d", item->history, item->history_sec);
		zabbix_log(LOG_LEVEL_TRACE, "  poller_type:%u location:%u", item->poller_type, item->location);
		zabbix_log(LOG_LEVEL_TRACE, "  inventory_link:%u", item->inventory_link);
		zabbix_log(LOG_LEVEL_TRACE, "  priority:%u schedulable:%u", item->queue_priority, item->schedulable);

		for (j = 0; j < (int)ARRSIZE(trace_items); j++)
		{
			if (NULL != (ptr = zbx_hashset_search(trace_items[j].hashset, &item->itemid)))
				trace_items[j].dump_func(ptr);
		}

		if (NULL != item->triggers)
		{
			ZBX_DC_TRIGGER	*trigger;

			zabbix_log(LOG_LEVEL_TRACE, "  triggers:");

			for (j = 0; NULL != (trigger = item->triggers[j]); j++)
				zabbix_log(LOG_LEVEL_TRACE, "    triggerid:" ZBX_FS_UI64, trigger->triggerid);
		}
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

static void	DCdump_interface_snmpitems(void)
{
	const char			*__function_name = "DCdump_interface_snmpitems";

	ZBX_DC_INTERFACE_ITEM		*interface_snmpitem;

	// 定义一个zbx_hashset_iter_t类型的变量，用于迭代配置文件中的触发器
	zbx_hashset_iter_t iter;
	// 定义一个整型变量，用于循环计数
	int				i, j;
	// 定义一个zbx_vector_ptr_t类型的变量，用于存储触发器
	zbx_vector_ptr_t index;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	// 创建一个指向zbx_vector_ptr类型的指针
	zbx_vector_ptr_create(&index);
	// 重置zbx_hashset_iter_t类型的变量，使其指向配置文件中的第一个触发器
	zbx_hashset_iter_reset(&config->interface_snmpitems, &iter);

	// 使用while循环，遍历配置文件中的所有触发器
	while (NULL != (interface_snmpitem = (ZBX_DC_INTERFACE_ITEM *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, interface_snmpitem);

	// 对索引 vector 中的触发器进行排序
	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	// 遍历索引 vector 中的所有触发器，打印详细信息
	for (i = 0; i < index.values_num; i++)
	{
		interface_snmpitem = (ZBX_DC_INTERFACE_ITEM *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "interfaceid:" ZBX_FS_UI64, interface_snmpitem->interfaceid);

		for (j = 0; j < interface_snmpitem->itemids.values_num; j++)
			zabbix_log(LOG_LEVEL_TRACE, "  itemid:" ZBX_FS_UI64, interface_snmpitem->itemids.values[j]);
	}

	// 销毁索引 vector
	zbx_vector_ptr_destroy(&index);

	// 记录日志，打印函数结束信息
	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

static void	DCdump_functions(void)
{
	const char		*__function_name = "DCdump_functions";

	ZBX_DC_FUNCTION		*function;
    zbx_hashset_iter_t iter;
    /* 定义一个整数变量，用于循环计数 */
    int i;
    /* 定义一个zbx_vector指针 */
    zbx_vector_ptr_t index;

    /* 打印日志，表示进入该函数 */
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    /* 创建一个zbx_vector，用于存储函数信息 */
    zbx_vector_ptr_create(&index);
    /* 重置zbx_hashset的迭代器，准备遍历配置中的所有函数 */
    zbx_hashset_iter_reset(&config->functions, &iter);

    /* 遍历迭代器，将zbx_hashset中的函数添加到zbx_vector中 */
    while (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_iter_next(&iter)))
        zbx_vector_ptr_append(&index, function);

    /* 对zbx_vector中的函数进行排序 */
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    /* 遍历排序后的zbx_vector，打印每个函数的信息 */
    for (i = 0; i < index.values_num; i++)
    {
        function = (ZBX_DC_FUNCTION *)index.values[i];
        zabbix_log(LOG_LEVEL_TRACE, "functionid:" ZBX_FS_UI64 " triggerid:" ZBX_FS_UI64 " itemid:"
                ZBX_FS_UI64 " function:'%s' parameter:'%s' timer:%u", function->functionid,
                function->triggerid, function->itemid, function->function, function->parameter,
                function->timer);

    }

    /* 销毁zbx_vector */
    zbx_vector_ptr_destroy(&index);

    /* 打印日志，表示结束该函数 */
    zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历并输出DC（数据采集器）触发器的所有标签（tag）的信息，包括tagid、tag名称和value值。其中，标签信息存储在一个zbx_vector_ptr类型的索引vector中，并使用默认的uint64类型比较函数进行排序。
 ******************************************************************************/
// 定义一个静态函数，用于处理DC（数据采集器）触发器的标签（tag）
static void DCdump_trigger_tags(const ZBX_DC_TRIGGER *trigger)
{
    // 定义一个整型变量i，用于循环计数
    int i;
    // 定义一个指向zbx_vector_ptr的指针，用于存储触发器标签的索引
    zbx_vector_ptr_t index;

    // 创建一个zbx_vector_ptr类型的对象，用于存储触发器标签的索引
    zbx_vector_ptr_create(&index);

    // 将触发器的标签值添加到索引vector中，并按照默认的uint64类型比较函数进行排序
    zbx_vector_ptr_append_array(&index, trigger->tags.values, trigger->tags.values_num);
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 以日志级别TRACE记录触发器标签信息
    zabbix_log(LOG_LEVEL_TRACE, "  tags:");

    // 遍历索引vector中的每个元素（触发器标签）
    for (i = 0; i < index.values_num; i++)
    {
        // 获取当前索引vector中的标签结构体指针
        zbx_dc_trigger_tag_t *tag = (zbx_dc_trigger_tag_t *)index.values[i];
        // 以日志级别TRACE记录每个标签的信息，包括tagid、tag名称和value值
        zabbix_log(LOG_LEVEL_TRACE, "      tagid:" ZBX_FS_UI64 " tag:'%s' value:'%s'",
                tag->triggertagid, tag->tag, tag->value);
    }

    // 释放索引vector内存
    zbx_vector_ptr_destroy(&index);
}


static void	DCdump_triggers(void)
{
	const char		*__function_name = "DCdump_triggers";

	ZBX_DC_TRIGGER		*trigger;
	zbx_hashset_iter_t	iter;
	int			i;
	zbx_vector_ptr_t	index;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	zbx_vector_ptr_create(&index);
	zbx_hashset_iter_reset(&config->triggers, &iter);

	while (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, trigger);

	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		trigger = (ZBX_DC_TRIGGER *)index.values[i];

		zabbix_log(LOG_LEVEL_TRACE, "triggerid:" ZBX_FS_UI64 " description:'%s' type:%u status:%u priority:%u",
					trigger->triggerid, trigger->description, trigger->type, trigger->status,
					trigger->priority);
		zabbix_log(LOG_LEVEL_TRACE, "  expression:'%s' recovery_expression:'%s'", trigger->expression,
				trigger->recovery_expression);
		zabbix_log(LOG_LEVEL_TRACE, "  value:%u state:%u error:'%s' lastchange:%d", trigger->value,
				trigger->state, ZBX_NULL2EMPTY_STR(trigger->error), trigger->lastchange);
		zabbix_log(LOG_LEVEL_TRACE, "  correlation_tag:'%s' recovery_mode:'%u' correlation_mode:'%u'",
				trigger->correlation_tag, trigger->recovery_mode, trigger->correlation_mode);
		zabbix_log(LOG_LEVEL_TRACE, "  topoindex:%u functional:%u locked:%u", trigger->topoindex,
				trigger->functional, trigger->locked);

		if (0 != trigger->tags.values_num)
			DCdump_trigger_tags(trigger);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历触发器依赖关系集合，输出每个触发器的详细信息（包括触发器ID和引用计数），以及每个触发器的依赖关系（包括依赖的触发器ID）。在这个过程中，首先创建一个索引对象，然后遍历触发器依赖关系集合，将触发器添加到索引中。接着对索引中的元素进行排序，最后遍历索引，输出每个触发器的详细信息和依赖关系。函数执行结束后，销毁索引对象并输出结束日志。
 ******************************************************************************/
// 定义一个静态函数DCdump_trigdeps，该函数没有任何返回值，并且不需要传入任何参数
static void DCdump_trigdeps(void)
{
    // 定义一个指向字符串的指针，用于存储函数名
    const char *__function_name = "DCdump_trigdeps";

    // 定义一个指向ZBX_DC_TRIGGER_DEPLIST结构体的指针
    ZBX_DC_TRIGGER_DEPLIST *trigdep;

    // 定义一个zbx_hashset_iter_t类型的变量，用于迭代配置文件中的触发器依赖关系集合
    zbx_hashset_iter_t iter;

    // 定义一个整型变量，用于循环计数
	int			i, j;

    // 定义一个zbx_vector_ptr_t类型的变量，用于存储索引
    zbx_vector_ptr_t index;

    // 使用zabbix_log记录日志，表示进入DCdump_expressions函数
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 创建一个zbx_vector_ptr类型的向量，用于存储表达式
    zbx_vector_ptr_create(&index);
    // 重置zbx_hashset_iter_t类型的迭代器，用于迭代zbx_config->expressions哈希表
	zbx_hashset_iter_reset(&config->trigdeps, &iter);

    // 遍历哈希表中的表达式
	while (NULL != (trigdep = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, trigdep);

    // 对向量index中的表达式进行排序
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 遍历排序后的向量index，并打印每个表达式的信息
    for (i = 0; i < index.values_num; i++)
    {
		trigdep = (ZBX_DC_TRIGGER_DEPLIST *)index.values[i];
        // 打印表达式的ID、正则表达式、表达式、分隔符、类型、大小写敏感等信息
		zabbix_log(LOG_LEVEL_TRACE, "triggerid:" ZBX_FS_UI64 " refcount:%d", trigdep->triggerid,
				trigdep->refcount);

		for (j = 0; j < trigdep->dependencies.values_num; j++)
		{
			const ZBX_DC_TRIGGER_DEPLIST	*trigdep_up = (ZBX_DC_TRIGGER_DEPLIST *)trigdep->dependencies.values[j];

			zabbix_log(LOG_LEVEL_TRACE, "  triggerid:" ZBX_FS_UI64, trigdep_up->triggerid);
		}
	}

    zbx_vector_ptr_destroy(&index);

    // 使用zabbix_log函数记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}


static void	DCdump_expressions(void)
{
	const char		*__function_name = "DCdump_expressions";

	ZBX_DC_EXPRESSION	*expression;
	zbx_hashset_iter_t	iter;
	int			i;
	zbx_vector_ptr_t	index;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	zbx_vector_ptr_create(&index);
	zbx_hashset_iter_reset(&config->expressions, &iter);

	while (NULL != (expression = (ZBX_DC_EXPRESSION *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, expression);

	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		expression = (ZBX_DC_EXPRESSION *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "expressionid:" ZBX_FS_UI64 " regexp:'%s' expression:'%s delimiter:%d"
				" type:%u case_sensitive:%u", expression->expressionid, expression->regexp,
				expression->expression, expression->delimiter, expression->type,
				expression->case_sensitive);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历一个zbx_hashset中的zbx_dc_action_t结构体元素，将其添加到zbx_vector_ptr中，并对该vector进行排序。然后，遍历vector中的每一个元素，输出其actionid、formula、eventsource、evaltype和opflags等信息。对于每一个action，还遍历其conditions数组，输出相关信息。最后，释放vector对象，并输出函数执行结束的日志。
 ******************************************************************************/
static void DCdump_actions(void)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "DCdump_actions";

	// 定义一个指向zbx_dc_action_t结构体的指针
	zbx_dc_action_t *action;
	// 定义一个zbx_hashset_iter_t结构体的指针
	zbx_hashset_iter_t iter;
	// 定义一个整数变量，用于循环计数
	int i, j;
	// 定义一个指向zbx_vector_ptr_t结构体的指针
	zbx_vector_ptr_t index;

	// 使用zabbix_log记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	// 创建一个zbx_vector_ptr对象，用于存储zbx_dc_action_t结构体
	zbx_vector_ptr_create(&index);
	// 重置zbx_hashset_iter_t结构体的迭代器
	zbx_hashset_iter_reset(&config->actions, &iter);

	// 使用循环，逐个取出zbx_hashset中的元素（zbx_dc_action_t结构体）
	while (NULL != (action = (zbx_dc_action_t *)zbx_hashset_iter_next(&iter)))
		// 将取出的元素添加到zbx_vector_ptr中
		zbx_vector_ptr_append(&index, action);

	// 对zbx_vector_ptr中的元素进行排序
	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	// 遍历zbx_vector_ptr中的元素，输出相关信息
	for (i = 0; i < index.values_num; i++)
	{
		action = (zbx_dc_action_t *)index.values[i];
		// 输出actionid、formula、eventsource、evaltype和opflags
		zabbix_log(LOG_LEVEL_TRACE, "actionid:" ZBX_FS_UI64 " formula:'%s' eventsource:%u evaltype:%u"
				" opflags:%x", action->actionid, action->formula, action->eventsource, action->evaltype,
				action->opflags);

		// 遍历action的conditions数组，输出相关信息
		for (j = 0; j < action->conditions.values_num; j++)
		{
			zbx_dc_action_condition_t *condition = (zbx_dc_action_condition_t *)action->conditions.values[j];

			// 输出conditionid、conditiontype、operator、value和value2
			zabbix_log(LOG_LEVEL_TRACE, "  conditionid:" ZBX_FS_UI64 " conditiontype:%u operator:%u"
					" value:'%s' value2:'%s'", condition->conditionid, condition->conditiontype,
					condition->op, condition->value, condition->value2);
		}
	}

	// 释放zbx_vector_ptr对象
	zbx_vector_ptr_destroy(&index);

	// 使用zabbix_log记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是遍历并打印zbx_dc_correlation_t结构体中的conditions成员。在这个过程中，首先创建一个索引，然后将conditions数组添加到索引中并排序。接着遍历索引中的元素，根据元素的类型输出相应的信息。最后，销毁索引。
 ******************************************************************************/
// 定义一个静态函数，用于打印zbx_dc_correlation_t结构体中的conditions成员
static void DCdump_corr_conditions(zbx_dc_correlation_t *correlation)
{
	// 定义一个整型变量i，用于循环遍历conditions数组
	int			i;
	// 定义一个指向zbx_vector_ptr类型的指针，用于存储索引
	zbx_vector_ptr_t	index;

	// 创建一个zbx_vector_ptr类型的对象，用于存储索引
	zbx_vector_ptr_create(&index);

	// 将correlation->conditions.values数组添加到索引中，并排序
	zbx_vector_ptr_append_array(&index, correlation->conditions.values, correlation->conditions.values_num);
	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	// 打印日志，表示开始处理conditions
	zabbix_log(LOG_LEVEL_TRACE, "  conditions:");

	// 遍历索引中的元素，即correlation->conditions.values数组
	for (i = 0; i < index.values_num; i++)
	{
		// 获取索引中第i个元素，即zbx_dc_corr_condition_t类型的指针
		zbx_dc_corr_condition_t	*condition = (zbx_dc_corr_condition_t *)index.values[i];
		// 打印日志，输出condition的id和类型
		zabbix_log(LOG_LEVEL_TRACE, "      conditionid:" ZBX_FS_UI64 " type:%d",
				condition->corr_conditionid, condition->type);

		// 根据condition的类型，输出相应的信息
		switch (condition->type)
		{
			case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
				// 打印日志，输出oldtag和newtag
				zabbix_log(LOG_LEVEL_TRACE, "        oldtag:'%s' newtag:'%s'",
						condition->data.tag_pair.oldtag, condition->data.tag_pair.newtag);
				break;
			case ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
				// 打印日志，输出groupid和op
				zabbix_log(LOG_LEVEL_TRACE, "        groupid:" ZBX_FS_UI64 " op:%u",
						condition->data.group.groupid, condition->data.group.op);
				break;
			case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
			case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
				// 打印日志，输出tag
				zabbix_log(LOG_LEVEL_TRACE, "        tag:'%s'", condition->data.tag.tag);
				break;
			case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
				// 打印日志，输出tag和value
				zabbix_log(LOG_LEVEL_TRACE, "        tag:'%s' value:'%s'",
						condition->data.tag_value.tag, condition->data.tag_value.value);
				break;
		}
	}

	// 销毁索引
	zbx_vector_ptr_destroy(&index);
}


static void	DCdump_corr_operations(zbx_dc_correlation_t *correlation)
{
    // 定义一个整型变量，用于循环计数
    int i;

    // 定义一个zbx_vector_ptr_t类型的指针，用于存储关联关系索引
    zbx_vector_ptr_t index;
    // 创建一个zbx_vector_ptr类型的对象，用于存储关联关系
    zbx_vector_ptr_create(&index);

	zbx_vector_ptr_append_array(&index, correlation->operations.values, correlation->operations.values_num);
    // 对索引vector进行排序，使用默认的uint64指针比较函数
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_TRACE, "  operations:");

	for (i = 0; i < index.values_num; i++)
	{
		zbx_dc_corr_operation_t	*operation = (zbx_dc_corr_operation_t *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "      operetionid:" ZBX_FS_UI64 " type:%d",
				operation->corr_operationid, operation->type);
	}

    // 销毁索引vector
    zbx_vector_ptr_destroy(&index);
}

static void	DCdump_correlations(void)
{
	const char		*__function_name = "DCdump_correlations";

	zbx_dc_correlation_t	*correlation;
	zbx_hashset_iter_t	iter;
	int			i;
	zbx_vector_ptr_t	index;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	zbx_vector_ptr_create(&index);
	zbx_hashset_iter_reset(&config->correlations, &iter);

	while (NULL != (correlation = (zbx_dc_correlation_t *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, correlation);

	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		correlation = (zbx_dc_correlation_t *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "correlationid:" ZBX_FS_UI64 " name:'%s' evaltype:%u formula:'%s'",
				correlation->correlationid, correlation->name, correlation->evaltype,
				correlation->formula);

		DCdump_corr_conditions(correlation);
		DCdump_corr_operations(correlation);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历一个hostgroup中的hosts，并将它们的hostid存储在一个vector中，然后对vector进行排序，最后以日志的形式输出排序后的hostid列表。
 ******************************************************************************/
// 定义一个静态函数，用于输出hostgroup中的hosts信息
static void DCdump_host_group_hosts(zbx_dc_hostgroup_t *group)
{
    // 定义一个zbx_hashset_iter_t类型的迭代器，用于遍历hostids集合
    zbx_hashset_iter_t	iter;
    // 定义一个整型变量，用于计数
    int			i;
    // 定义一个zbx_vector_uint64_t类型的变量，用于存储hostids
    zbx_vector_uint64_t	index;
    // 定义一个zbx_uint64_t类型的指针，用于存储遍历到的hostid
    zbx_uint64_t		*phostid;

    // 创建一个zbx_vector_uint64_t类型的变量index，用于存储hostids
    zbx_vector_uint64_create(&index);
    // 重置zbx_hashset_iter_t类型的迭代器iter，使其从头开始遍历hostids集合
    zbx_hashset_iter_reset(&group->hostids, &iter);

    // 使用一个while循环，遍历hostids集合中的所有元素
	while (NULL != (phostid = (zbx_uint64_t *)zbx_hashset_iter_next(&iter)))
		zbx_vector_uint64_append_ptr(&index, phostid);

	zbx_vector_uint64_sort(&index, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_TRACE, "  hosts:");

	for (i = 0; i < index.values_num; i++)
		zabbix_log(LOG_LEVEL_TRACE, "    hostid:" ZBX_FS_UI64, index.values[i]);

	zbx_vector_uint64_destroy(&index);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历配置文件中的主机组，将其添加到一个 vector_ptr 类型的变量中，并对主机组进行排序。然后遍历排序后的主机组，打印主机组及其对应的主机信息。最后释放 vector_ptr 类型变量，清理资源。
 ******************************************************************************/
// 定义一个静态函数DCdump_host_groups，该函数用于打印zbx_dc_hostgroup_t结构体的信息
static void DCdump_host_groups(void)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DCdump_host_groups";

    // 定义一个指向zbx_dc_hostgroup_t结构体的指针
    zbx_dc_hostgroup_t *group;

    // 定义一个zbx_hashset_iter_t类型的迭代器，用于遍历配置文件中的主机组
    zbx_hashset_iter_t iter;

    // 定义一个整型变量，用于循环计数
    int i;

    // 定义一个zbx_vector_ptr_t类型的变量，用于存储查询到的主机组
    zbx_vector_ptr_t index;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	zbx_vector_ptr_create(&index);
	zbx_hashset_iter_reset(&config->hostgroups, &iter);

	while (NULL != (group = (zbx_dc_hostgroup_t *)zbx_hashset_iter_next(&iter)))
		zbx_vector_ptr_append(&index, group);

	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		group = (zbx_dc_hostgroup_t *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "groupid:" ZBX_FS_UI64 " name:'%s'", group->groupid, group->name);

		if (0 != group->hostids.num_data)
			DCdump_host_group_hosts(group);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数DCdump_host_group_index，用于打印配置文件中所有主机组的索引。运行这个函数后，会输出主机组的名称。
 ******************************************************************************/
// 定义一个静态函数，用于打印主机组索引
static void DCdump_host_group_index(void)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DCdump_host_group_index";

    // 定义一个指向zbx_dc_hostgroup_t类型的指针，用于存储主机组信息
    zbx_dc_hostgroup_t *group;

    // 定义一个整型变量，用于循环计数
    int i;

    // 使用zabbix_log函数打印日志，表示进入函数__function_name()
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 使用zabbix_log函数打印日志，表示开始打印主机组索引
    zabbix_log(LOG_LEVEL_TRACE, "group index:");

    // 使用for循环遍历config->hostgroups_name.values数组，数组元素为zbx_dc_hostgroup_t类型
    for (i = 0; i < config->hostgroups_name.values_num; i++)
    {
        // 获取数组元素，即主机组对象
        group = (zbx_dc_hostgroup_t *)config->hostgroups_name.values[i];

        // 使用zabbix_log函数打印日志，输出主机组名
        zabbix_log(LOG_LEVEL_TRACE, "  %s", group->name);
    }

    // 使用zabbix_log函数打印日志，表示结束打印主机组索引
    zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是处理维护组的相关操作，具体包括：
 *1. 创建一个 uint64 类型的向量 index；
 *2. 判断 maintenance 结构体中的 groupids 字段是否包含组 ID；
 *3. 如果包含组 ID，则将 groupids 字段中的组 ID 添加到 index 向量中，并对向量进行排序；
 *4. 打印 index 向量中的所有组 ID；
 *5. 释放 index 向量占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于处理维护组的相关操作
static void DCdump_maintenance_groups(zbx_dc_maintenance_t *maintenance)
{
    // 定义一个整型变量 i，用于循环计数
    int i;
    // 定义一个 uint64 类型的向量变量 index，用于存储组 ID
    zbx_vector_uint64_t index;

    // 创建一个 uint64 类型的向量 index
    zbx_vector_uint64_create(&index);

    // 判断 maintenance 结构体中的 groupids 字段是否包含组 ID
    if (0 != maintenance->groupids.values_num)
    {
        // 将 groupids 字段中的组 ID 添加到 index 向量中
        zbx_vector_uint64_append_array(&index, maintenance->groupids.values, maintenance->groupids.values_num);
        // 对 index 向量中的组 ID 进行排序
        zbx_vector_uint64_sort(&index, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    }

    // 打印日志，表示正在处理组信息
    zabbix_log(LOG_LEVEL_TRACE, "  groups:");

    // 遍历 index 向量中的组 ID，并打印日志
    for (i = 0; i < index.values_num; i++)
        // 打印组 ID
        zabbix_log(LOG_LEVEL_TRACE, "    groupid:" ZBX_FS_UI64, index.values[i]);

    // 释放 index 向量占用的内存
    zbx_vector_uint64_destroy(&index);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是处理维护主机的信息。首先判断维护主机结构体中的 hostids 字段是否有元素，如果有，则将 hostids 字段中的元素添加到一个新的 uint64 类型向量 index 中，并对向量进行排序。接着输出日志，显示主机 ID。最后释放 index 向量占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于处理维护主机的信息
static void DCdump_maintenance_hosts(zbx_dc_maintenance_t *maintenance)
{
	// 定义一个整型变量 i，用于循环计数
	int i;
	// 定义一个 uint64 类型的向量变量 index，用于存储主机 ID
	zbx_vector_uint64_t index;

	// 创建一个 uint64 类型的向量 index
	zbx_vector_uint64_create(&index);

	// 判断 maintenance 指向的结构体中的 hostids 字段是否有元素
	if (0 != maintenance->hostids.values_num)
	{
		// 将 maintenance 指向的结构体中的 hostids 字段元素添加到 index 向量中
		zbx_vector_uint64_append_array(&index, maintenance->hostids.values, maintenance->hostids.values_num);
		// 对 index 向量中的元素进行排序
		zbx_vector_uint64_sort(&index, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	// 输出日志，显示主机信息
	zabbix_log(LOG_LEVEL_TRACE, "  hosts:");

	// 遍历 index 向量中的元素，并输出日志
	for (i = 0; i < index.values_num; i++)
		zabbix_log(LOG_LEVEL_TRACE, "    hostid:" ZBX_FS_UI64, index.values[i]);

	// 释放 index 向量占用的内存
	zbx_vector_uint64_destroy(&index);
}


/******************************************************************************
 * *
 *这块代码的主要目的是比较两个维护标签（maintenance_tag）的对象，包括比较它们的标签名（tag）、值（value）和操作符（op）。如果两个维护标签的所有字段都相等，则返回 0，表示它们是相同的；否则，返回非零值，表示它们是不同的。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个维护标签（maintenance_tag）的对象
static int	maintenance_tag_compare(const void *v1, const void *v2)
{
	// 解引用指针，获取两个维护标签对象
	const zbx_dc_maintenance_tag_t	*tag1 = *(const zbx_dc_maintenance_tag_t **)v1;
	const zbx_dc_maintenance_tag_t	*tag2 = *(const zbx_dc_maintenance_tag_t **)v2;

	// 定义一个整型变量 ret，用于存储比较结果
	int				ret;

	// 比较两个维护标签的 tag 字段（即标签名）
	if (0 != (ret = (strcmp(tag1->tag, tag2->tag))))
		// 如果 tag 字段比较结果不为零，返回 ret
		return ret;

	// 如果不等于零，说明 tag 字段比较结果为零，继续比较下一个字段
	if (0 != (ret = (strcmp(tag1->value, tag2->value))))
		// 如果 value 字段比较结果不为零，返回 ret
		return ret;

	// 比较两个维护标签的 op 字段（即操作符）
	ZBX_RETURN_IF_NOT_EQUAL(tag1->op, tag2->op);

	// 所有字段比较完毕，如果都相等，返回 0
	return 0;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历并输出zbx_dc_maintenance_t结构体中的标签信息。首先创建一个索引对象，然后判断标签数量是否大于0，如果大于0，则将标签值复制到索引中并排序。接着遍历索引中的标签，获取每个标签的结构体指针，并记录日志输出标签详细信息。最后释放索引对象。
 ******************************************************************************/
// 定义一个静态函数，用于处理zbx_dc_maintenance_t结构体中的标签信息
static void DCdump_maintenance_tags(zbx_dc_maintenance_t *maintenance)
{
	// 定义一个整型变量i，用于循环计数
	int i;
	// 定义一个指向zbx_vector_ptr类型的指针，用于存储索引
	zbx_vector_ptr_t index;

	// 创建一个zbx_vector_ptr类型的对象，用于存储索引
	zbx_vector_ptr_create(&index);

	// 判断maintenance结构体中的标签数量是否大于0，如果大于0，则执行以下操作
	if (0 != maintenance->tags.values_num)
	{
		// 将maintenance结构体中的标签值复制到zbx_vector_ptr类型的索引中
		zbx_vector_ptr_append_array(&index, maintenance->tags.values, maintenance->tags.values_num);
		// 对索引中的标签进行排序，排序规则为zbx_maintenance_tag_compare函数
		zbx_vector_ptr_sort(&index, maintenance_tag_compare);
	}

	// 记录日志，输出标签信息
	zabbix_log(LOG_LEVEL_TRACE, "  tags:");

	// 遍历索引中的标签
	for (i = 0; i < index.values_num; i++)
	{
		// 获取索引中第i个标签的结构体指针
		zbx_dc_maintenance_tag_t *tag = (zbx_dc_maintenance_tag_t *)index.values[i];
		// 记录日志，输出标签详细信息
		zabbix_log(LOG_LEVEL_TRACE, "    maintenancetagid:" ZBX_FS_UI64 " operator:%u tag:'%s' value:'%s'",
				tag->maintenancetagid, tag->op, tag->tag, tag->value);
	}

	// 释放索引对象
	zbx_vector_ptr_destroy(&index);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是输出维护周期的详细信息。首先创建一个索引vector，将维护周期的值添加到vector中，并使用默认的排序函数进行排序。然后遍历索引vector中的每个元素，即维护周期，输出其详细信息，包括时间周期ID、类型、每隔时间、月份、星期、起始时间、周期、起始日期等。最后释放索引vector。
 ******************************************************************************/
/* 定义一个静态函数，用于输出维护周期的详细信息 */
static void DCdump_maintenance_periods(zbx_dc_maintenance_t *maintenance)
{
	/* 定义一个整数变量 i，用于循环计数 */
	int i;

	/* 定义一个指向zbx_vector_t的指针，用于存储维护周期的索引 */
	zbx_vector_ptr_t index;

	/* 创建一个zbx_vector_ptr_t类型的对象，用于存储维护周期的索引 */
	zbx_vector_ptr_create(&index);

	/* 将维护周期的值添加到索引vector中，并使用默认的排序函数进行排序 */
	zbx_vector_ptr_append_array(&index, maintenance->periods.values, maintenance->periods.values_num);
	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	/* 输出日志，表示正在输出维护周期信息 */
	zabbix_log(LOG_LEVEL_TRACE, "  periods:");

	/* 遍历索引vector中的每个元素，即维护周期 */
	for (i = 0; i < index.values_num; i++)
	{
		zbx_dc_maintenance_period_t	*period = (zbx_dc_maintenance_period_t *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "    timeperiodid:" ZBX_FS_UI64 " type:%u every:%d month:%d dayofweek:%d"
				" day:%d start_time:%d period:%d start_date:%d",
				period->timeperiodid, period->type, period->every, period->month, period->dayofweek,
				period->day, period->start_time, period->period, period->start_date);
	}

	zbx_vector_ptr_destroy(&index);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是遍历配置文件中的维护项，将其添加到维护ID列表中，并对列表进行排序。然后遍历列表，打印每个维护项的详细信息，包括维护ID、类型、活跃时间、状态等。此外，还递归调用子函数打印维护项的相关信息，如维护组、主机、标签和周期等。
 ******************************************************************************/
/* 定义静态函数 DCdump_maintenances，用于打印维护信息 */
static void DCdump_maintenances(void)
{
    /* 定义变量 */
	const char		*__function_name = "DCdump_maintenances"; // 函数名指针
    zbx_dc_maintenance_t *maintenance; // 维护结构体指针
    zbx_hashset_iter_t iter; // 哈希集迭代器
    int i; // 循环计数器
    zbx_vector_ptr_t index; // 维护ID列表

    /* 打印日志 */
    zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    /* 初始化维护ID列表 */
    zbx_vector_ptr_create(&index);

    /* 遍历配置文件中的维护项 */
    zbx_hashset_iter_reset(&config->maintenances, &iter);
    while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
        /* 添加维护项到列表中 */
        zbx_vector_ptr_append(&index, maintenance);

    /* 对维护ID列表进行排序 */
    zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    /* 遍历维护ID列表，打印每个维护项的信息 */
	for (i = 0; i < index.values_num; i++)
	{

		maintenance = (zbx_dc_maintenance_t *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "maintenanceid:" ZBX_FS_UI64 " type:%u tag_evaltype:%u active_since:%d"
				" active_until:%d", maintenance->maintenanceid, maintenance->type,
				maintenance->tags_evaltype, maintenance->active_since, maintenance->active_until);
		zabbix_log(LOG_LEVEL_TRACE, "  state:%u running_since:%d running_until:%d",
				maintenance->state, maintenance->running_since, maintenance->running_until);

		DCdump_maintenance_groups(maintenance);
		DCdump_maintenance_hosts(maintenance);
		DCdump_maintenance_tags(maintenance);
		DCdump_maintenance_periods(maintenance);
	}

	zbx_vector_ptr_destroy(&index);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}
/******************************************************************************
 * *
 *这段代码的主要目的是调用各个函数，打印出系统中各种配置信息，包括配置、主机、代理、IPMI主机、主机库存、模板、全局宏、主机宏、接口、项目、接口SNMP项目、触发器、触发器依赖、函数、表达式、操作、关联、主机组、主机组索引和维护等信息。
 ******************************************************************************/
// 定义一个函数，用于打印各种配置信息
void DCdump_configuration(void)
{
    // 调用DCdump_config()函数，打印配置信息
    DCdump_config();

    // 调用DCdump_hosts()函数，打印主机信息
    DCdump_hosts();

    // 调用DCdump_proxies()函数，打印代理信息
    DCdump_proxies();

    // 调用DCdump_ipmihosts()函数，打印IPMI主机信息
    DCdump_ipmihosts();

    // 调用DCdump_host_inventories()函数，打印主机库存信息
    DCdump_host_inventories();

    // 调用DCdump_htmpls()函数，打印模板信息
    DCdump_htmpls();

    // 调用DCdump_gmacros()函数，打印全局宏信息
    DCdump_gmacros();

    // 调用DCdump_hmacros()函数，打印主机宏信息
    DCdump_hmacros();

    // 调用DCdump_interfaces()函数，打印接口信息
    DCdump_interfaces();

    // 调用DCdump_items()函数，打印项目信息
    DCdump_items();

    // 调用DCdump_interface_snmpitems()函数，打印接口SNMP项目信息
    DCdump_interface_snmpitems();

    // 调用DCdump_triggers()函数，打印触发器信息
    DCdump_triggers();

    // 调用DCdump_trigdeps()函数，打印触发器依赖信息
    DCdump_trigdeps();

    // 调用DCdump_functions()函数，打印函数信息
    DCdump_functions();

    // 调用DCdump_expressions()函数，打印表达式信息
    DCdump_expressions();

    // 调用DCdump_actions()函数，打印操作信息
    DCdump_actions();

    // 调用DCdump_correlations()函数，打印关联信息
    DCdump_correlations();

    // 调用DCdump_host_groups()函数，打印主机组信息
    DCdump_host_groups();

    // 调用DCdump_host_group_index()函数，打印主机组索引信息
    DCdump_host_group_index();

    // 调用DCdump_maintenances()函数，打印维护信息
    DCdump_maintenances();
}

