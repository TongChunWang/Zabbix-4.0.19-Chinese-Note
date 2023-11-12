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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "lld.h"
#include "db.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxserver.h"

typedef struct
{
	zbx_uint64_t	hostmacroid;
	char		*macro;
	char		*value;
}
zbx_lld_hostmacro_t;

/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_lld_hostmacro_t结构体中的内存空间。该结构体包含两个成员，分别是macro和value，分别存储着两个不同的数据。通过调用lld_hostmacro_free函数，可以安全地释放这两个成员以及hostmacro本身所占用的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_hostmacro_t结构体中的内存空间
static void	lld_hostmacro_free(zbx_lld_hostmacro_t *hostmacro)
{
	// 释放hostmacro指向的第一个成员macro的内存空间
	zbx_free(hostmacro->macro);

	// 释放hostmacro指向的第二个成员value的内存空间
	zbx_free(hostmacro->value);

	// 释放hostmacro本身所占用的内存空间
	zbx_free(hostmacro);
}


typedef struct
{
	zbx_uint64_t	interfaceid;
	zbx_uint64_t	parent_interfaceid;
	char		*ip;
	char		*dns;
	char		*port;
	unsigned char	main;
	unsigned char	main_orig;
	unsigned char	type;
	unsigned char	type_orig;
	unsigned char	useip;
	unsigned char	bulk;
#define ZBX_FLAG_LLD_INTERFACE_UPDATE_TYPE	__UINT64_C(0x00000001)	/* interface.type field should be updated  */
#define ZBX_FLAG_LLD_INTERFACE_UPDATE_MAIN	__UINT64_C(0x00000002)	/* interface.main field should be updated */
#define ZBX_FLAG_LLD_INTERFACE_UPDATE_USEIP	__UINT64_C(0x00000004)	/* interface.useip field should be updated */
#define ZBX_FLAG_LLD_INTERFACE_UPDATE_IP	__UINT64_C(0x00000008)	/* interface.ip field should be updated */
#define ZBX_FLAG_LLD_INTERFACE_UPDATE_DNS	__UINT64_C(0x00000010)	/* interface.dns field should be updated */
#define ZBX_FLAG_LLD_INTERFACE_UPDATE_PORT	__UINT64_C(0x00000020)	/* interface.port field should be updated */
#define ZBX_FLAG_LLD_INTERFACE_UPDATE_BULK	__UINT64_C(0x00000040)	/* interface.bulk field should be updated */
#define ZBX_FLAG_LLD_INTERFACE_UPDATE								\
		(ZBX_FLAG_LLD_INTERFACE_UPDATE_TYPE | ZBX_FLAG_LLD_INTERFACE_UPDATE_MAIN |	\
		ZBX_FLAG_LLD_INTERFACE_UPDATE_USEIP | ZBX_FLAG_LLD_INTERFACE_UPDATE_IP |	\
		ZBX_FLAG_LLD_INTERFACE_UPDATE_DNS | ZBX_FLAG_LLD_INTERFACE_UPDATE_PORT |	\
		ZBX_FLAG_LLD_INTERFACE_UPDATE_BULK)
#define ZBX_FLAG_LLD_INTERFACE_REMOVE		__UINT64_C(0x00000080)	/* interfaces which should be deleted */
	zbx_uint64_t	flags;
}
zbx_lld_interface_t;

/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_lld_interface_t结构体所占用的内存。在这个结构体中，包含了port、dns、ip等成员变量，通过调用lld_interface_free函数，依次释放这些成员变量以及结构体本身所占用的内存。这是一个内存管理函数，确保在使用完zbx_lld_interface_t结构体后，能够正确地释放其占用的内存资源。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_interface_t结构体的内存
static void lld_interface_free(zbx_lld_interface_t *interface)
{
    // 释放interface指向的port内存
    zbx_free(interface->port);
    // 释放interface指向的dns内存
    zbx_free(interface->dns);
    // 释放interface指向的ip内存
    zbx_free(interface->ip);
    // 释放interface本身所占用的内存
    zbx_free(interface);
}


typedef struct
{
	zbx_uint64_t		hostid;
	zbx_vector_uint64_t	new_groupids;		/* host groups which should be added */
	zbx_vector_uint64_t	lnk_templateids;	/* templates which should be linked */
	zbx_vector_uint64_t	del_templateids;	/* templates which should be unlinked */
	zbx_vector_ptr_t	new_hostmacros;		/* host macros which should be added */
	zbx_vector_ptr_t	interfaces;
	char			*host_proto;
	char			*host;
	char			*host_orig;
	char			*name;
	char			*name_orig;
	int			lastcheck;
	int			ts_delete;

#define ZBX_FLAG_LLD_HOST_DISCOVERED			__UINT64_C(0x00000001)	/* hosts which should be updated or added */
#define ZBX_FLAG_LLD_HOST_UPDATE_HOST			__UINT64_C(0x00000002)	/* hosts.host and host_discovery.host fields should be updated */
#define ZBX_FLAG_LLD_HOST_UPDATE_NAME			__UINT64_C(0x00000004)	/* hosts.name field should be updated */
#define ZBX_FLAG_LLD_HOST_UPDATE_PROXY			__UINT64_C(0x00000008)	/* hosts.proxy_hostid field should be updated */
#define ZBX_FLAG_LLD_HOST_UPDATE_IPMI_AUTH		__UINT64_C(0x00000010)	/* hosts.ipmi_authtype field should be updated */
#define ZBX_FLAG_LLD_HOST_UPDATE_IPMI_PRIV		__UINT64_C(0x00000020)	/* hosts.ipmi_privilege field should be updated */
#define ZBX_FLAG_LLD_HOST_UPDATE_IPMI_USER		__UINT64_C(0x00000040)	/* hosts.ipmi_username field should be updated */
#define ZBX_FLAG_LLD_HOST_UPDATE_IPMI_PASS		__UINT64_C(0x00000080)	/* hosts.ipmi_password field should be updated */
/******************************************************************************
 * *
 *这块代码的主要目的是释放zbx_lld_host_t结构体指针指向的内存空间。在这个函数中，逐个释放了该结构体中的各个成员变量，包括整数类型变量、指针类型变量等。这个过程是通过调用相应的内存释放函数（如zbx_free）来完成的。在整个过程中，遵循了内存释放的规范，确保了内存的合理释放。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_host_t结构体指针指向的内存空间
static void lld_host_free(zbx_lld_host_t *host)
{
    // 释放host->new_groupids指向的内存空间
    zbx_vector_uint64_destroy(&host->new_groupids);

    // 释放host->lnk_templateids指向的内存空间
    zbx_vector_uint64_destroy(&host->lnk_templateids);

    // 释放host->del_templateids指向的内存空间
    zbx_vector_uint64_destroy(&host->del_templateids);

    // 清除host->new_hostmacros指向的内存空间，并释放内存
    zbx_vector_ptr_clear_ext(&host->new_hostmacros, (zbx_clean_func_t)lld_hostmacro_free);

    // 释放host->new_hostmacros指向的内存空间
    zbx_vector_ptr_destroy(&host->new_hostmacros);

    // 清除host->interfaces指向的内存空间，并释放内存
    zbx_vector_ptr_clear_ext(&host->interfaces, (zbx_clean_func_t)lld_interface_free);

    // 释放host->interfaces指向的内存空间
    zbx_vector_ptr_destroy(&host->interfaces);

    // 释放host->host_proto指向的内存空间
    zbx_free(host->host_proto);

    // 释放host->host指向的内存空间
    zbx_free(host->host);

    // 释放host->host_orig指向的内存空间
    zbx_free(host->host_orig);

    // 释放host->name指向的内存空间
    zbx_free(host->name);

    // 释放host->name_orig指向的内存空间
    zbx_free(host->name_orig);

    // 释放host指向的内存空间
    zbx_free(host);
}

	zbx_uint64_t		flags;
	char			inventory_mode;
}
zbx_lld_host_t;

static void	lld_host_free(zbx_lld_host_t *host)
{
	zbx_vector_uint64_destroy(&host->new_groupids);
	zbx_vector_uint64_destroy(&host->lnk_templateids);
	zbx_vector_uint64_destroy(&host->del_templateids);
	zbx_vector_ptr_clear_ext(&host->new_hostmacros, (zbx_clean_func_t)lld_hostmacro_free);
	zbx_vector_ptr_destroy(&host->new_hostmacros);
	zbx_vector_ptr_clear_ext(&host->interfaces, (zbx_clean_func_t)lld_interface_free);
	zbx_vector_ptr_destroy(&host->interfaces);
	zbx_free(host->host_proto);
	zbx_free(host->host);
	zbx_free(host->host_orig);
	zbx_free(host->name);
	zbx_free(host->name_orig);
	zbx_free(host);
}

typedef struct
{
	zbx_uint64_t	group_prototypeid;
	char		*name;
}
zbx_lld_group_prototype_t;

/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx_lld_group_prototype_t结构体类型的内存。在这个结构体中，有两个成员：name和group_prototype。name成员是一个字符串，需要使用zbx_free()函数释放；group_prototype成员是一个指向该结构体的指针，也需要使用zbx_free()函数释放。这个函数的作用是在程序运行过程中，确保不再使用已经分配的内存，以防止内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_group_prototype_t结构体类型的内存
static void lld_group_prototype_free(zbx_lld_group_prototype_t *group_prototype)
{
    // 释放group_prototype指向的结构体中的name成员所占用的内存
    zbx_free(group_prototype->name);
    // 释放group_prototype指向的结构体本身所占用的内存
    zbx_free(group_prototype);
}


typedef struct
{
	zbx_uint64_t		groupid;
	zbx_uint64_t		group_prototypeid;
	zbx_vector_ptr_t	hosts;
	char			*name_proto;
	char			*name;
	char			*name_orig;
	int			lastcheck;
	int			ts_delete;
#define ZBX_FLAG_LLD_GROUP_DISCOVERED		__UINT64_C(0x00000001)	/* groups which should be updated or added */
#define ZBX_FLAG_LLD_GROUP_UPDATE_NAME		__UINT64_C(0x00000002)	/* groups.name field should be updated */
#define ZBX_FLAG_LLD_GROUP_UPDATE		ZBX_FLAG_LLD_GROUP_UPDATE_NAME
	zbx_uint64_t		flags;
}
zbx_lld_group_t;

/******************************************************************************
/******************************************************************************
 * 以下是我为您注释的代码块：
 *
 *
 *
 *这个代码块的主要目的是从数据库中获取符合条件的主机列表，并将这些主机的信息存储在一个结构体数组中。在这个函数中，首先执行数据库查询，然后遍历查询结果，对每个主机进行解析和处理，最后将处理后的主机添加到主机列表中。
 ******************************************************************************/
/* 静态函数 lld_hosts_get，用于获取符合条件的主机列表
 * 参数：
 *   parent_hostid：父主机ID
 *   hosts：主机列表指针
 *   proxy_hostid：代理主机ID
 *   ipmi_authtype：IPMI认证类型
 *   ipmi_privilege：IPMI权限
 *   ipmi_username：IPMI用户名
 *   ipmi_password：IPMI密码
 *   tls_connect：TLS连接状态
 *   tls_accept：TLS接受状态
 *   tls_issuer：TLS发行者
 *   tls_subject：TLS主题
 *   tls_psk_identity：TLS预共享密钥身份
 *   tls_psk：TLS预共享密钥
 */
static void lld_hosts_get(zbx_uint64_t parent_hostid, zbx_vector_ptr_t *hosts, zbx_uint64_t proxy_hostid,
                         char ipmi_authtype, unsigned char ipmi_privilege, const char *ipmi_username, const char *ipmi_password,
                         unsigned char tls_connect, unsigned char tls_accept, const char *tls_issuer,
                         const char *tls_subject, const char *tls_psk_identity, const char *tls_psk)
{
    /* 定义常量 */
    static const char *__function_name = "lld_hosts_get";

    /* 变量声明 */
    DB_RESULT	result;
    DB_ROW		row;
    zbx_lld_host_t	*host;
    zbx_uint64_t	db_proxy_hostid;

    /* 打印日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "进入 %s()"， __function_name);

    /* 执行数据库查询 */
    result = DBselect(
        "select hd.hostid, hd.host, hd.lastcheck, hd.ts_delete, h.host, h.name, h.proxy_hostid,
             h.ipmi_authtype, h.ipmi_privilege, h.ipmi_username, h.ipmi_password,
             h.tls_connect, h.tls_accept, h.tls_issuer, h.tls_subject, h.tls_psk_identity, h.tls_psk
        from host_discovery hd
        join hosts h
            on hd.hostid = h.hostid
        left join host_inventory hi
            on hd.hostid = hi.hostid
        where hd.parent_hostid = %u",
        parent_hostid);

    /* 循环处理查询结果 */
    while (NULL != (row = DBfetch(result)))
    {
        /* 分配内存，创建主机结构体 */
        host = (zbx_lld_host_t *)zbx_malloc(NULL, sizeof(zbx_lld_host_t));

        /* 解析主机ID、主机名、最后检查时间、删除时间等字段 */
        ZBX_STR2UINT64(host->hostid, row[0]);
        host->host_proto = zbx_strdup(NULL, row[1]);
        host->lastcheck = atoi(row[2]);
        host->ts_delete = atoi(row[3]);
        host->host = zbx_strdup(NULL, row[4]);
        host->host_orig = NULL;
        host->name = zbx_strdup(NULL, row[5]);
        host->name_orig = NULL;
/******************************************************************************
 * *
 *这段代码的主要目的是对一个名为`lld_hosts_validate`的函数进行逐行注释。这个函数用于验证主机名、可见主机名和主机ID是否合法。如果验证失败，函数会返回一个错误信息。
 *
 *以下是逐行注释：
 *
 *1. 定义一个名为`__function_name`的常量，表示函数名。
 *2. 定义一个`DB_RESULT`类型的变量`result`，用于存储数据库查询结果。
 *3. 定义一个`DB_ROW`类型的变量`row`，用于存储数据库查询的每一行数据。
 *4. 定义两个整数变量`i`和`j`，用于遍历`hosts`数组。
 *5. 定义两个字符指针变量`ch_error`，用于存储错误信息。
 *6. 定义一个`zbx_vector_ptr_t`类型的变量`hosts`，这是一个指向主机结构体的指针数组。
 *7. 定义一个`char **`类型的变量`error`，用于存储错误信息。
 *8. 初始化`hostids`、`tnames`和`vnames`三个vector字符串指针数组。
 *9. 使用`zabbix_log`函数记录函数的进入日志。
 *10. 遍历`hosts`数组，对每个主机进行验证。
 *11. 检查主机名是否合法，如果合法则继续验证。
 *12. 检查可见主机名是否合法，如果合法则继续验证。
 *13. 检查主机ID是否合法，如果合法则继续验证。
 *14. 验证主机名和可见主机名是否重复，如果重复则返回错误信息。
 *15. 验证主机ID是否重复，如果重复则返回错误信息。
 *16. 如果主机名、可见主机名和主机ID都合法，则继续执行后续操作。
 *17. 遍历`hosts`数组，检查主机是否已经存在。
 *18. 如果主机不存在，则检查主机名是否已经存在。
 *19. 如果主机名存在，则返回错误信息。
 *20. 如果主机名不存在，则检查可见主机名是否已经存在。
 *21. 如果可见主机名存在，则返回错误信息。
 *22. 如果可见主机名不存在，则继续执行后续操作。
 *23. 调用`zbx_is_utf8`函数检查主机名是否为UTF-8编码。
 *24. 如果主机名为UTF-8编码，则检查其长度是否符合要求。
 *25. 如果长度符合要求，则继续验证。
 *26. 否则，返回错误信息。
 *27. 遍历`hosts`数组，检查主机是否已经存在。
 *28. 如果主机不存在，则检查主机名是否已经存在。
 *29. 如果主机名存在，则返回错误信息。
 *30. 如果主机名不存在，则检查可见主机名是否已经存在。
 *31. 如果可见主机名存在，则返回错误信息。
 *32. 如果可见主机名不存在，则继续执行后续操作。
 *33. 调用`lld_field_str_rollback`函数回滚主机名和可见主机名的更改。
 *34. 更新主机和可见主机的错误信息。
 *35. 遍历`hosts`数组，检查主机ID是否已经存在。
 *36. 如果主机ID存在，则返回错误信息。
 *37. 否则，继续执行后续操作。
 *38. 调用`lld_host_t`结构体的`zbx_strdcatf`函数，将错误信息拼接成字符串。
 *39. 释放`sql`指
 ******************************************************************************/
static void lld_hosts_validate(zbx_vector_ptr_t *hosts, char **error)
{
	const char *__function_name = "lld_hosts_validate";

	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	zbx_lld_host_t		*host, *host_b;
	zbx_vector_uint64_t	hostids;
	zbx_vector_str_t	tnames, vnames;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&hostids);
	zbx_vector_str_create(&tnames);		/* list of technical host names */
	zbx_vector_str_create(&vnames);		/* list of visible host names */

	/* checking a host name validity */
	for (i = 0; i < hosts->values_num; i++)
	{
		char	*ch_error;

		host = (zbx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed host name will be validated */
		if (0 != host->hostid && 0 == (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_HOST))
			continue;

		/* host name is valid? */
		if (SUCCEED == zbx_check_hostname(host->host, &ch_error))
			continue;

		*error = zbx_strdcatf(*error, "Cannot %s host \"%s\": %s.\
",
				(0 != host->hostid ? "update" : "create"), host->host, ch_error);

		zbx_free(ch_error);

		if (0 != host->hostid)
		{
			lld_field_str_rollback(&host->host, &host->host_orig, &host->flags,
					ZBX_FLAG_LLD_HOST_UPDATE_HOST);
		}
		else
			host->flags &= ~ZBX_FLAG_LLD_HOST_DISCOVERED;
	}

	/* checking a visible host name validity */
	for (i = 0; i < hosts->values_num; i++)
	{
		host = (zbx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed visible name will be validated */
		if (0 != host->hostid && 0 == (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_NAME))
			continue;

		/* visible host name is valid utf8 sequence and has a valid length */
		if (SUCCEED == zbx_is_utf8(host->name) && '\0' != *host->name &&
				HOST_NAME_LEN >= zbx_strlen_utf8(host->name))
		{
			continue;
		}

		zbx_replace_invalid_utf8(host->name);
		*error = zbx_strdcatf(*error, "Cannot %s host: invalid visible host name \"%s\".\
",
				(0 != host->hostid ? "update" : "create"), host->name);

		if (0 != host->hostid)
		{
			lld_field_str_rollback(&host->name, &host->name_orig, &host->flags,
					ZBX_FLAG_LLD_HOST_UPDATE_NAME);
		}
		else
			host->flags &= ~ZBX_FLAG_LLD_HOST_DISCOVERED;
	}

	/* checking duplicated host names */
	for (i = 0; i < hosts->values_num; i++)
	{
		host = (zbx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed host name will be validated */
		if (0 != host->hostid && 0 == (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_HOST))
			continue;

		for (j = 0; j < hosts->values_num; j++)
		{
			host_b = (zbx_lld_host_t *)hosts->values[j];

			if (0 == (host_b->flags & ZBX_FLAG_LLD_HOST_DISCOVERED) || i == j)
				continue;

			if (0 != strcmp(host->host, host_b->host))
				continue;

			*error = zbx_strdcatf(*error, "Cannot %s host:"
					" host with the same name \"%s\" already exists.\
",
					(0 != host->hostid ? "update" : "create"), host->host);

			if (0 != host->hostid)
			{
				lld_field_str_rollback(&host->host, &host->host_orig, &host->flags,
						ZBX_FLAG_LLD_HOST_UPDATE_HOST);
			}
			else
				host->flags &= ~ZBX_FLAG_LLD_HOST_DISCOVERED;
		}
	}

	/* checking duplicated visible host names */
	for (i = 0; i < hosts->values_num; i++)
	{
		host = (zbx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* only new hosts or hosts with changed visible name will be validated */
		if (0 != host->hostid && 0 == (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_NAME))
			continue;

		for (j = 0; j < hosts->values_num; j++)
		{
			host_b = (zbx_lld_host_t *)hosts->values[j];

			if (0 == (host_b->flags & ZBX_FLAG_LLD_HOST_DISCOVERED) || i == j)
				continue;

			if (0 != strcmp(host->name, host_b->name))
				continue;

			*error = zbx_strdcatf(*error, "Cannot %s host:"
					" host with the same visible name \"%s\" already exists.\
",
					(0 != host->hostid ? "update" : "create"), host->name);

			if (0 != host->hostid)
			{
				lld_field_str_rollback(&host->name, &host->name_orig, &host->flags,
						ZBX_FLAG_LLD_HOST_UPDATE_NAME);
			}
			else
				host->flags &= ~ZBX_FLAG_LLD_HOST_DISCOVERED;
		}
	}

	/* checking duplicated host names and visible host names in DB */

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (zbx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		if (0 != host->hostid)
			zbx_vector_uint64_append(&hostids, host->hostid);

		if (0 == host->hostid || 0 != (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_HOST))
			zbx_vector_str_append(&tnames, host->host);

		if (0 == host->hostid || 0 != (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_NAME))
			zbx_vector_str_append(&vnames, host->name);
	}

	if (0 != tnames.values_num || 0 != vnames.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select host,name"
				" from hosts"
				" where status in (%d,%d,%d)"
					" and flags<>%d"
					" and",
				HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, HOST_STATUS_TEMPLATE,
				ZBX_FLAG_DISCOVERY_PROTOTYPE);

		if (0 != tnames.values_num && 0 != vnames.values_num)
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " (");

		if (0 != tnames.values_num)
		{
			DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "host",
					(const char **)tnames.values, tnames.values_num);
		}

		if (0 != tnames.values_num && 0 != vnames.values_num)
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " or");

		if (0 != vnames.values_num)
		{
			DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
					(const char **)vnames.values, vnames.values_num);
		}

		if (0 != tnames.values_num && 0 != vnames.values_num)
			zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < hosts->values_num; i++)
			{
				host = (zbx_lld_host_t *)hosts->values[i];

				if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
					continue;

				if (0 != strcmp(host->host, row[0]))
					continue;

				*error = zbx_strdcatf(*error, "Cannot %s host:"
						" host with the same name \"%s\" already exists.\
",
						(0 != host->hostid ? "update" : "create"), host->host);

				if (0 != host->hostid)
				{
					lld_field_str_rollback(&host->host, &host->host_orig, &host->flags,
							ZBX_FLAG_LLD_HOST_UPDATE_HOST);
				}
				else
					host->flags &= ~ZBX_FLAG_LLD_HOST_DISCOVERED;
			}
		}
		DBfree_result(result);

		zbx_free(sql);
	}

	zbx_vector_str_destroy(&vnames);
	zbx_vector_str_destroy(&tnames);
	zbx_vector_uint64_destroy(&hostids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

			else
				host->flags &= ~ZBX_FLAG_LLD_HOST_DISCOVERED;
		}
	}

	/* checking duplicated host names and visible host names in DB */

	for (i = 0; i < hosts->values_num; i++)
	{
		host = (zbx_lld_host_t *)hosts->values[i];

		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		if (0 != host->hostid)
			zbx_vector_uint64_append(&hostids, host->hostid);

		if (0 == host->hostid || 0 != (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_HOST))
			zbx_vector_str_append(&tnames, host->host);

		if (0 == host->hostid || 0 != (host->flags & ZBX_FLAG_LLD_HOST_UPDATE_NAME))
			zbx_vector_str_append(&vnames, host->name);
	}

	if (0 != tnames.values_num || 0 != vnames.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select host,name"
				" from hosts"
				" where status in (%d,%d,%d)"
					" and flags<>%d"
					" and",
				HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, HOST_STATUS_TEMPLATE,
				ZBX_FLAG_DISCOVERY_PROTOTYPE);

		if (0 != tnames.values_num && 0 != vnames.values_num)
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " (");

		if (0 != tnames.values_num)
		{
			DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "host",
					(const char **)tnames.values, tnames.values_num);
		}

		if (0 != tnames.values_num && 0 != vnames.values_num)
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " or");

		if (0 != vnames.values_num)
		{
			DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
					(const char **)vnames.values, vnames.values_num);
		}

		if (0 != tnames.values_num && 0 != vnames.values_num)
			zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');

		if (0 != hostids.values_num)
		{
			zbx_vector_uint64_sort(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
					hostids.values, hostids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < hosts->values_num; i++)
			{
				host = (zbx_lld_host_t *)hosts->values[i];

				if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
					continue;

				if (0 == strcmp(host->host, row[0]))
				{
					*error = zbx_strdcatf(*error, "Cannot %s host:"
							" host with the same name \"%s\" already exists.\n",
							(0 != host->hostid ? "update" : "create"), host->host);

					if (0 != host->hostid)
					{
						lld_field_str_rollback(&host->host, &host->host_orig, &host->flags,
								ZBX_FLAG_LLD_HOST_UPDATE_HOST);
					}
					else
						host->flags &= ~ZBX_FLAG_LLD_HOST_DISCOVERED;
				}

/******************************************************************************
 * 
 ******************************************************************************/
/* 定义静态函数 lld_host_make，该函数用于从zbx_vector_t类型的hosts向量中查找符合给定条件的主机，
 * 并根据需要创建新主机或更新现有主机的信息。
 * 输入参数：
 *   zbx_vector_ptr_t *hosts：包含主机的zbx_vector_t类型指针
 *   const char *host_proto：主机协议字符串
 *   const char *name_proto：主机显示名称协议字符串
 *   const struct zbx_json_parse *jp_row：zbx_json_parse结构指针，用于解析主机信息
 * 返回值：
 *   指向zbx_lld_host_t类型结构的指针，为新创建的主机或已找到并更新过的主机
 */
static zbx_lld_host_t *lld_host_make(zbx_vector_ptr_t *hosts, const char *host_proto, const char *name_proto,
                                       const struct zbx_json_parse *jp_row)
{
	/* 定义变量 */
	const char	*__function_name = "lld_host_make";

	char		*buffer = NULL;
	int		i;
	zbx_lld_host_t	*host = NULL;

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 遍历hosts向量中的所有主机 */
	for (i = 0; i < hosts->values_num; i++)
	{
		host = (zbx_lld_host_t *)hosts->values[i];

		/* 如果主机已被发现，跳过 */
		if (0 != (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* 从主机协议字符串创建缓冲区 */
		buffer = zbx_strdup(buffer, host->host_proto);

		/* 替换LLD主机协议中的宏 */
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);

		/* 去除缓冲区两端的白空格 */
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		/* 检查缓冲区内容是否与主机的host字段相等，如果是，则 break循环 */
		if (0 == strcmp(host->host, buffer))
			break;
	}

	/* 如果没有找到主机，则执行以下操作：
	 * 1. 分配新主机结构内存
	 * 2. 初始化主机结构
	 * 3. 将新主机添加到hosts向量中
	 */
	if (i == hosts->values_num)	/* no host found */
	{
		host = (zbx_lld_host_t *)zbx_malloc(NULL, sizeof(zbx_lld_host_t));

		host->hostid = 0;
		host->host_proto = NULL;
		host->lastcheck = 0;
		host->ts_delete = 0;
		host->host = zbx_strdup(NULL, host_proto);
		host->host_orig = NULL;
		substitute_lld_macros(&host->host, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(host->host, ZBX_WHITESPACE);
		host->name = zbx_strdup(NULL, name_proto);
		substitute_lld_macros(&host->name, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(host->name, ZBX_WHITESPACE);
		host->name_orig = NULL;
		zbx_vector_uint64_create(&host->new_groupids);
		zbx_vector_uint64_create(&host->lnk_templateids);
		zbx_vector_uint64_create(&host->del_templateids);
		zbx_vector_ptr_create(&host->new_hostmacros);
		zbx_vector_ptr_create(&host->interfaces);
		host->flags = ZBX_FLAG_LLD_HOST_DISCOVERED;

		/* 将新主机添加到hosts向量中 */
		zbx_vector_ptr_append(hosts, host);
	}
	else
	{
		/* 更新主机技术名称 */
		if (0 != strcmp(host->host_proto, host_proto))	/* the new host prototype differs */
		{
			host->host_orig = host->host;
			host->host = zbx_strdup(NULL, host_proto);
			substitute_lld_macros(&host->host, jp_row, ZBX_MACRO_ANY, NULL, 0);
			zbx_lrtrim(host->host, ZBX_WHITESPACE);
			host->flags |= ZBX_FLAG_LLD_HOST_UPDATE_HOST;
		}

		/* 更新主机显示名称 */
		buffer = zbx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(host->name, buffer))
		{
			host->name_orig = host->name;
			host->name = buffer;
			buffer = NULL;
			host->flags |= ZBX_FLAG_LLD_HOST_UPDATE_NAME;
		}

		host->flags |= ZBX_FLAG_LLD_HOST_DISCOVERED;
	}

	/* 释放缓冲区 */
	zbx_free(buffer);

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)host);

	/* 返回主机指针 */
	return host;
}

			host->host_orig = host->host;
			host->host = zbx_strdup(NULL, host_proto);
			substitute_lld_macros(&host->host, jp_row, ZBX_MACRO_ANY, NULL, 0);
			zbx_lrtrim(host->host, ZBX_WHITESPACE);
			host->flags |= ZBX_FLAG_LLD_HOST_UPDATE_HOST;
		}

		/* host visible name */
		buffer = zbx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(host->name, buffer))
		{
			host->name_orig = host->name;
			host->name = buffer;
			buffer = NULL;
			host->flags |= ZBX_FLAG_LLD_HOST_UPDATE_NAME;
		}

		host->flags |= ZBX_FLAG_LLD_HOST_DISCOVERED;
	}

	zbx_free(buffer);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)host);

	return host;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_simple_groups_get                                            *
 *                                                                            *
 * Purpose: retrieve list of host groups which should be present on the each  *
 *          discovered host                                                   *
 *                                                                            *
 * Parameters: parent_hostid - [IN] host prototype identifier                 *
 *             groupids      - [OUT] sorted list of host groups               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取父主机下的所有子分组。通过数据库查询，将查询结果中的子分组ID添加到一个新的向量中，最后对向量进行排序。输出结果为一个包含子分组ID的向量。
 ******************************************************************************/
// 定义一个静态函数，用于获取父主机下所有子分组
static void lld_simple_groups_get(zbx_uint64_t parent_hostid, zbx_vector_uint64_t *groupids)
{
	// 声明变量
	DB_RESULT	result;
/******************************************************************************
 * *
 *这段代码的主要目的是对已发现的主机和组进行处理，将主机添加到新的组ID列表中，并删除已添加的组ID。在这个过程中，还会记录待删除的组ID列表。以下是代码的详细注释：
 *
 *1. 定义静态函数`lld_hostgroups_make`，接收四个参数：组ID列表、主机列表、组列表、待删除的组ID列表。
 *2. 记录进入函数调用。
 *3. 创建主机ID列表。
 *4. 遍历主机列表，将已发现的主机添加到新的组ID列表中。
 *5. 遍历组列表，将已发现的组添加到主机的新组ID列表中。
 *6. 对主机的新组ID列表进行排序。
 *7. 如果主机ID列表不为空，执行以下操作：
 *   - 构建SQL查询语句。
 *   - 添加主机ID条件。
 *   - 执行查询。
 *   - 处理查询结果。
 *   - 查找主机。
 *   - 查找主机的新组ID列表中的组ID。
 *   - 判断是否待删除的组ID，如果是，则添加到待删除的组ID列表中；否则，从主机的新组ID列表中删除该组ID。
 *8. 释放查询结果。
 *9. 对待删除的组ID列表进行排序。
 *10. 销毁主机ID列表。
 *11. 记录函数调用结束。
 ******************************************************************************/
// 定义静态函数lld_hostgroups_make，参数包括组ID列表、主机列表、组列表、待删除的组ID列表
static void lld_hostgroups_make(const zbx_vector_uint64_t *groupids, zbx_vector_ptr_t *hosts,
                              const zbx_vector_ptr_t *groups, zbx_vector_uint64_t *del_hostgroupids)
{
    // 定义日志级别
    const char *__function_name = "lld_hostgroups_make";

    // 定义数据库操作结果、行数据、循环变量、组ID列表、主机、组、待删除的组ID
    DB_RESULT result;
    DB_ROW row;
    int i, j;
    zbx_vector_uint64_t hostids;
    zbx_uint64_t hostgroupid, hostid, groupid;
    zbx_lld_host_t *host;
    const zbx_lld_group_t *group;

    // 记录进入函数调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建主机ID列表
    zbx_vector_uint64_create(&hostids);

    // 遍历主机列表，将已发现的主机添加到新的组ID列表中
    for (i = 0; i < hosts->values_num; i++)
    {
        host = (zbx_lld_host_t *)hosts->values[i];

        // 如果主机尚未被发现，跳过
        if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
            continue;

        // 为主机分配新的组ID列表空间
        zbx_vector_uint64_reserve(&host->new_groupids, groupids->values_num);
        // 将组ID添加到主机的新组ID列表中
        for (j = 0; j < groupids->values_num; j++)
            zbx_vector_uint64_append(&host->new_groupids, groupids->values[j]);

        // 如果主机ID不为0，将其添加到主机ID列表中
        if (0 != host->hostid)
            zbx_vector_uint64_append(&hostids, host->hostid);
    }

    // 遍历组列表，将已发现的组添加到主机的新组ID列表中
    for (i = 0; i < groups->values_num; i++)
    {
        group = (zbx_lld_group_t *)groups->values[i];

        // 如果组尚未被发现或组ID为0，跳过
        if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED) || 0 == group->groupid)
            continue;

        // 遍历组的 hosts 结构中的主机，为它们分配新的组ID
        for (j = 0; j < group->hosts.values_num; j++)
        {
            host = (zbx_lld_host_t *)group->hosts.values[j];

            // 将组ID添加到主机的新组ID列表中
            zbx_vector_uint64_append(&host->new_groupids, group->groupid);
        }
    }

    // 对主机的新组ID列表进行排序
    for (i = 0; i < hosts->values_num; i++)
    {
        host = (zbx_lld_host_t *)hosts->values[i];
        zbx_vector_uint64_sort(&host->new_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    }

    // 如果主机ID列表不为空，执行以下操作：
    if (0 != hostids.values_num)
    {
        // 构建SQL查询语句
        char *sql = NULL;
        size_t sql_alloc = 0, sql_offset = 0;

        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                        "select hostid,groupid,hostgroupid"
                        " from hosts_groups"
                        " where");
        // 添加主机ID条件
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);

        // 执行查询
        result = DBselect("%s", sql);

        // 释放SQL字符串
        zbx_free(sql);

        // 处理查询结果
        while (NULL != (row = DBfetch(result)))
        {
            // 解析主机ID、组ID
            ZBX_STR2UINT64(hostid, row[0]);
            ZBX_STR2UINT64(groupid, row[1]);

            // 查找主机
            if (FAIL == (i = zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
            {
                // 找不到主机，异常情况
                THIS_SHOULD_NEVER_HAPPEN;
                continue;
            }

            host = (zbx_lld_host_t *)hosts->values[i];

            // 查找主机的新组ID列表中的组ID
            if (FAIL == (i = zbx_vector_uint64_bsearch(&host->new_groupids, groupid,
                                                       ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
            {
                // 待删除的组ID
                ZBX_STR2UINT64(hostgroupid, row[2]);
                zbx_vector_uint64_append(del_hostgroupids, hostgroupid);
            }
            else
            {
                // 已添加的组ID
                zbx_vector_uint64_remove(&host->new_groupids, i);
            }
        }
        // 释放查询结果
        DBfree_result(result);

        // 对待删除的组ID列表进行排序
        zbx_vector_uint64_sort(del_hostgroupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    }

    // 销毁主机ID列表
    zbx_vector_uint64_destroy(&hostids);

    // 记录函数调用结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);

		result = DBselect("%s", sql);

		zbx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(hostid, row[0]);
			ZBX_STR2UINT64(groupid, row[1]);

			if (FAIL == (i = zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			host = (zbx_lld_host_t *)hosts->values[i];

			if (FAIL == (i = zbx_vector_uint64_bsearch(&host->new_groupids, groupid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				/* host groups which should be unlinked */
				ZBX_STR2UINT64(hostgroupid, row[2]);
				zbx_vector_uint64_append(del_hostgroupids, hostgroupid);
			}
			else
			{
				/* host groups which are already added */
				zbx_vector_uint64_remove(&host->new_groupids, i);
			}
		}
		DBfree_result(result);

		zbx_vector_uint64_sort(del_hostgroupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	zbx_vector_uint64_destroy(&hostids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_group_prototypes_get                                         *
 *                                                                            *
 * Purpose: retrieve list of group prototypes                                 *
 *                                                                            *
 * Parameters: parent_hostid    - [IN] host prototype identifier              *
 *             group_prototypes - [OUT] sorted list of group prototypes       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中获取指定父主机ID的组别原型，并将获取到的组别原型添加到一个集合中。最后对集合进行排序并返回。
 ******************************************************************************/
// 定义一个静态函数，用于获取组别原型
static void lld_group_prototypes_get(zbx_uint64_t parent_hostid, zbx_vector_ptr_t *group_prototypes)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "lld_group_prototypes_get";

    // 声明一些变量
    DB_RESULT			result;
    DB_ROW				row;
    zbx_lld_group_prototype_t	*group_prototype;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 从数据库中查询组别原型
    result = DBselect(
            "select group_prototypeid,name"
            " from group_prototype"
            " where groupid is null"
            " and hostid=" ZBX_FS_UI64,
            parent_hostid);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 为每个组别原型分配内存
        group_prototype = (zbx_lld_group_prototype_t *)zbx_malloc(NULL, sizeof(zbx_lld_group_prototype_t));

        // 解析组别原型ID和名称
        ZBX_STR2UINT64(group_prototype->group_prototypeid, row[0]);
        group_prototype->name = zbx_strdup(NULL, row[1]);

        // 将组别原型添加到集合中
        zbx_vector_ptr_append(group_prototypes, group_prototype);
    }
    // 释放数据库查询结果
    DBfree_result(result);

    // 对集合进行排序
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个数据库查询结果中解析出分组（group）的信息，并将这些信息存储在一个指向指针数组的指针（groups）中。这个过程是通过循环解析查询结果，并将每个分组的信息填充到一个zbx_lld_group_t结构体中，然后将这个结构体添加到指针数组中实现的。最后，对指针数组进行排序，以便后续使用。
 ******************************************************************************/
static void lld_groups_get(zbx_uint64_t parent_hostid, zbx_vector_ptr_t *groups)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "lld_groups_get";

    // 声明一些变量
    DB_RESULT	result;
    DB_ROW		row;
    zbx_lld_group_t	*group;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 执行数据库查询
    result = DBselect(
        "select gd.groupid,gp.group_prototypeid,gd.name,gd.lastcheck,gd.ts_delete,g.name"
        " from group_prototype gp,group_discovery gd"
            " join hstgrp g"
            " on gd.groupid=g.groupid"
    );

    // 循环获取查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 分配内存，保存查询结果
        group = (zbx_lld_group_t *)zbx_malloc(NULL, sizeof(zbx_lld_group_t));

        // 解析查询结果，填充group结构体
        ZBX_STR2UINT64(group->groupid, row[0]);
        ZBX_STR2UINT64(group->group_prototypeid, row[1]);
        zbx_vector_ptr_create(&group->hosts);
        group->name_proto = zbx_strdup(NULL, row[2]);
        group->lastcheck = atoi(row[3]);
        group->ts_delete = atoi(row[4]);
        group->name = zbx_strdup(NULL, row[5]);
        group->name_orig = NULL;
        group->flags = 0x00;

        // 将group添加到结果集
        zbx_vector_ptr_append(groups, group);
    }
    // 释放查询结果
    DBfree_result(result);
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * lld_group_make 函数用于根据给定的参数创建或查找一个已存在的 LLDP 组。
 * 参数：
 *   groups：组列表指针
 *   group_prototypeid：组原型 ID
 *   name_proto：组名称原型
 *   jp_row：JSON 解析结构指针
 * 返回值：
 *   若组已存在，返回指向该组的指针；若创建新组，返回新组的指针；若找不到组，返回 NULL。
 */
static zbx_lld_group_t *lld_group_make(zbx_vector_ptr_t *groups, zbx_uint64_t group_prototypeid,
                                         const char *name_proto, const struct zbx_json_parse *jp_row)
{
	const char	*__function_name = "lld_group_make";

	char		*buffer = NULL;
	int		i;
	zbx_lld_group_t	*group = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 遍历组列表，查找具有给定组原型 ID 的组 */
	for (i = 0; i < groups->values_num; i++)
	{
		group = (zbx_lld_group_t *)groups->values[i];

		if (group->group_prototypeid != group_prototypeid)
			continue;

		/* 如果组已被发现，跳过 */
		if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		buffer = zbx_strdup(buffer, group->name_proto);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		if (0 == strcmp(group->name, buffer))
			break;
	}

	/* 如果没有找到组，尝试查找已存在的组 */
	if (i == groups->values_num)
	{
		buffer = zbx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		/* 遍历组列表，查找具有相同名称的组 */
		for (i = 0; i < groups->values_num; i++)
		{
			group = (zbx_lld_group_t *)groups->values[i];

			if (group->group_prototypeid != group_prototypeid)
				continue;

			if (0 == strcmp(group->name, buffer))
				goto out;
		}
	}

	/* 否则创建新组 */
	group = (zbx_lld_group_t *)zbx_malloc(NULL, sizeof(zbx_lld_group_t));

	group->groupid = 0;
	group->group_prototypeid = group_prototypeid;
	zbx_vector_ptr_create(&group->hosts);
	group->name_proto = NULL;
	group->name = zbx_strdup(NULL, name_proto);
	substitute_lld_macros(&group->name, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(group->name, ZBX_WHITESPACE);
	group->name_orig = NULL;
	group->lastcheck = 0;
	group->ts_delete = 0;
	group->flags = 0x00;
	group->flags |= ZBX_FLAG_LLD_GROUP_DISCOVERED;

	zbx_vector_ptr_append(groups, group);

out:
	zbx_free(buffer);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)group);

	return group;
}

		group->ts_delete = 0;
		group->flags = 0x00;
		group->flags = ZBX_FLAG_LLD_GROUP_DISCOVERED;

		zbx_vector_ptr_append(groups, group);
	}
	else
	{
		/* update an already existing group */

		/* group name */
		buffer = zbx_strdup(buffer, name_proto);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(group->name, buffer))
		{
			group->name_orig = group->name;
			group->name = buffer;
			buffer = NULL;
			group->flags |= ZBX_FLAG_LLD_GROUP_UPDATE_NAME;
		}

		group->flags |= ZBX_FLAG_LLD_GROUP_DISCOVERED;
	}
out:
	zbx_free(buffer);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)group);

	return group;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_make                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的group_prototypes，为每个group_prototype创建一个对应的zbx_lld_group_t对象，并将host添加到对应的group的hosts vector中。最后，输出调试信息表示函数执行完毕。
 ******************************************************************************/
// 定义一个静态函数lld_groups_make，接收四个参数：
// zbx_lld_host_t类型的指针host，zbx_vector_ptr_t类型的指针groups，
// const zbx_vector_ptr_t类型的指针group_prototypes，
// 以及const struct zbx_json_parse类型的指针jp_row
static void lld_groups_make(zbx_lld_host_t *host, zbx_vector_ptr_t *groups,
                           const zbx_vector_ptr_t *group_prototypes,
                           const struct zbx_json_parse *jp_row)
{
    // 定义一个内部函数名__function_name，值为"lld_groups_make"
    const char *__function_name = "lld_groups_make";

    // 定义一个整型变量i，用于循环计数
    int i;

    // 使用zabbix_log记录调试信息，显示当前函数名
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 遍历group_prototypes中的每个元素
    for (i = 0; i < group_prototypes->values_num; i++)
    {
        // 获取group_prototypes中的第i个元素，类型为zbx_lld_group_prototype_t
        const zbx_lld_group_prototype_t *group_prototype;
        group_prototype = (zbx_lld_group_prototype_t *)group_prototypes->values[i];
/******************************************************************************
 * *
 *这个代码块的主要目的是对一组组（包含组名和组ID）进行验证，确保组名不重复，并对不符合要求的组名进行回滚。在整个过程中，还会检查组ID是否重复。如果发现重复的组名或组ID，将打印错误信息并回滚相应的组名和标志。
 ******************************************************************************/
static void lld_groups_validate(zbx_vector_ptr_t *groups, char **error)
{
    // 定义函数名
    const char *__function_name = "lld_groups_validate";

    // 声明变量
    DB_RESULT result;
    DB_ROW row;
    int i, j;
    zbx_lld_group_t *group, *group_b;
    zbx_vector_uint64_t groupids;
    zbx_vector_str_t names;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建vector用于存储groupids和names
    zbx_vector_uint64_create(&groupids);
    zbx_vector_str_create(&names);		/* 存储组名 */

    // 检查group名称的有效性
    for (i = 0; i < groups->values_num; i++)
    {
        group = (zbx_lld_group_t *)groups->values[i];

        // 跳过已经发现但未更新名称的组
        if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
            continue;

        // 仅对新组或更改了名称的组进行验证
        if (0 != group->groupid && 0 == (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
            continue;

        // 如果组名称验证失败，处理并回滚
        if (SUCCEED == lld_validate_group_name(group->name))
            continue;

        zbx_replace_invalid_utf8(group->name);
        *error = zbx_strdcatf(*error, "Cannot %s group: invalid group name \"%s\".\
",
                            (0 != group->groupid ? "update" : "create"), group->name);

        // 如果组ID不为0，回滚组名称和标志
        if (0 != group->groupid)
        {
            lld_field_str_rollback(&group->name, &group->name_orig, &group->flags,
                                ZBX_FLAG_LLD_GROUP_UPDATE_NAME);
        }
        else
            group->flags &= ~ZBX_FLAG_LLD_GROUP_DISCOVERED;
    }

    // 检查重复的组名称
    for (i = 0; i < groups->values_num; i++)
    {
        group = (zbx_lld_group_t *)groups->values[i];

        // 跳过已发现但未更新名称的组
        if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
            continue;

        // 仅对新组或更改了名称的组进行验证
        if (0 != group->groupid && 0 == (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
            continue;

        // 遍历所有组，检查是否有重复名称
        for (j = 0; j < groups->values_num; j++)
        {
            group_b = (zbx_lld_group_t *)groups->values[j];

            // 跳过未发现或已更新的组
            if (0 == (group_b->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED) || i == j)
                continue;

            // 如果不存在相同的名称，继续遍历
            if (0 != strcmp(group->name, group_b->name))
                continue;

            *error = zbx_strdcatf(*error, "Cannot %s group:"
                                " group with the same name \"%s\" already exists.\
",
                                (0 != group->groupid ? "update" : "create"), group->name);

            // 如果组ID不为0，回滚组名称和标志
            if (0 != group->groupid)
            {
                lld_field_str_rollback(&group->name, &group->name_orig, &group->flags,
                                ZBX_FLAG_LLD_GROUP_UPDATE_NAME);
            }
            else
                group->flags &= ~ZBX_FLAG_LLD_GROUP_DISCOVERED;
        }
    }

    // 检查DB中是否存在重复的组名称
    for (i = 0; i < groups->values_num; i++)
    {
        group = (zbx_lld_group_t *)groups->values[i];

        // 跳过已发现但未更新名称的组
        if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
            continue;

        // 仅对新组或更改了名称的组进行验证
        if (0 != group->groupid && 0 == (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
            continue;

        // 如果组ID不为0，存储组ID
        if (0 != group->groupid)
            zbx_vector_uint64_append(&groupids, group->groupid);

        // 如果组ID为0或已更新名称，存储组名
        if (0 == group->groupid || 0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
            zbx_vector_str_append(&names, group->name);
    }

    // 如果存在名称重复的组，打印错误信息并回滚
    if (0 != names.values_num)
    {
        char *sql = NULL;
        size_t sql_alloc = 0, sql_offset = 0;

        // 构建SQL查询条件
        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select name from hstgrp where");
        DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
                                (const char **)names.values, names.values_num);

        if (0 != groupids.values_num)
        {
            // 对组ID进行排序
            zbx_vector_uint64_sort(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

            // 添加非条件
            zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
            DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid",
                                groupids.values, groupids.values_num);
        }

        result = DBselect("%s", sql);

        while (NULL != (row = DBfetch(result)))
        {
            for (i = 0; i < groups->values_num; i++)
            {
                group = (zbx_lld_group_t *)groups->values[i];

                // 跳过已发现但未更新名称的组
                if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
                    continue;

                // 仅对新组或更改了名称的组进行验证
                if (0 != group->groupid && 0 == (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
                    continue;

                // 比较组名称和DB中的组名称
                if (0 != strcmp(group->name, row[0]))
                    continue;

                /* 如果组ID不为0，打印错误信息并回滚组名称和标志 */
                if (0 != group->groupid)
                {
                    *error = zbx_strdcatf(*error, "Cannot %s group:"
                                    " group with the same name \"%s\" already exists.\
",
                                    (0 != group->groupid ? "update" : "create"), group->name);

                    lld_field_str_rollback(&group->name, &group->name_orig, &group->flags,
                                    ZBX_FLAG_LLD_GROUP_UPDATE_NAME);
                }
            }
        }
        DBfree_result(result);

        zbx_free(sql);
    }

    // 释放vector
    zbx_vector_str_destroy(&names);
    zbx_vector_uint64_destroy(&groupids);

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


	/* checking duplicated group names and group names in DB */

	for (i = 0; i < groups->values_num; i++)
	{
		group = (zbx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		if (0 != group->groupid)
			zbx_vector_uint64_append(&groupids, group->groupid);

		if (0 == group->groupid || 0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
			zbx_vector_str_append(&names, group->name);
	}

	if (0 != names.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select name from hstgrp where");
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
				(const char **)names.values, names.values_num);

		if (0 != groupids.values_num)
		{
			zbx_vector_uint64_sort(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid",
					groupids.values, groupids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < groups->values_num; i++)
			{
				group = (zbx_lld_group_t *)groups->values[i];

				if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
					continue;

				if (0 == strcmp(group->name, row[0]))
				{
					*error = zbx_strdcatf(*error, "Cannot %s group:"
							" group with the same name \"%s\" already exists.\n",
							(0 != group->groupid ? "update" : "create"), group->name);

					if (0 != group->groupid)
					{
						lld_field_str_rollback(&group->name, &group->name_orig, &group->flags,
								ZBX_FLAG_LLD_GROUP_UPDATE_NAME);
					}
					else
						group->flags &= ~ZBX_FLAG_LLD_GROUP_DISCOVERED;
				}
			}
		}
		DBfree_result(result);

		zbx_free(sql);
	}

	zbx_vector_str_destroy(&names);
	zbx_vector_uint64_destroy(&groupids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_group_rights_compare                                         *
 *                                                                            *
 * Purpose: sorting function to sort group rights vector by name              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_lld_group_rights结构体对象的名称字符串，根据字符串的大小关系返回一个整数。输出结果如下：
 *
 *```
 *0：表示两个对象的名称相等。
 *大于0：表示第一个对象的名称大于第二个对象。
 *小于0：表示第一个对象的名称小于第二个对象。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_lld_group_rights结构体对象的字符串名称
static int	lld_group_rights_compare(const void *d1, const void *d2)
{
	// 解引用指针，获取zbx_lld_group_rights结构体对象的指针
	const zbx_lld_group_rights_t	*r1 = *(const zbx_lld_group_rights_t **)d1;
	const zbx_lld_group_rights_t	*r2 = *(const zbx_lld_group_rights_t **)d2;

	// 使用strcmp函数比较两个对象的名称字符串，返回0表示相等，大于0表示r1大于r2，小于0表示r1小于r2
	return strcmp(r1->name, r2->name);
}

/******************************************************************************
 * *
 *这个代码块的主要目的是对给定的组（`groups`）中的新组分配权限。它首先创建一个组名称列表（`group_names`）和一个存储组权限的指针向量（`group_rights`）。然后，它遍历组列表，查找直接父组名称和新的组权限。对于每个新组，它将检查其父组权限列表，并将匹配的项分配给新组。最后，它将新组的权限保存到数据库中。
 ******************************************************************************/
static void lld_groups_save_rights(zbx_vector_ptr_t *groups)
{
    // 定义变量
    const char *__function_name = "lld_groups_save_rights";
    int i, j;
    DB_ROW row;
    DB_RESULT result;
    char *ptr, *name, *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0, offset;
    zbx_lld_group_t *group;
    zbx_vector_str_t group_names;
    zbx_vector_ptr_t group_rights;
    zbx_db_insert_t db_insert;
    zbx_lld_group_rights_t *rights, rights_local, *parent_rights;
    zbx_uint64_pair_t pair;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "Enter %s()", __function_name);

    // 创建字符串向量
    zbx_vector_str_create(&group_names);
    zbx_vector_ptr_create(&group_rights);

    // 获取直接父组名称列表和新组权限列表
    for (i = 0; i < groups->values_num; i++)
    {
        group = (zbx_lld_group_t *)groups->values[i];

        // 查找组名中最后一个'/'的位置
        if (NULL == (ptr = strrchr(group->name, '/')))
            continue;

        // 添加组权限
        lld_group_rights_append(&group_rights, group->name);

        // 获取组名子字符串
        name = zbx_strdup(NULL, group->name);
        name[ptr - group->name] = '\0';

        // 查找已存在的组名
        if (FAIL != zbx_vector_str_search(&group_names, name, ZBX_DEFAULT_STR_COMPARE_FUNC))
        {
            zbx_free(name);
            continue;
        }

        // 添加组名
        zbx_vector_str_append(&group_names, name);
    }

    // 如果组名列表为空，则退出
    if (0 == group_names.values_num)
        goto out;

    // 读取父组权限
    zbx_db_insert_prepare(&db_insert, "rights", "rightid", "id", "permission", "groupid", NULL);
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select g.name,r.permission,r.groupid from hstgrp g,rights r"
                " where r.id=g.groupid"
                " and");

    // 添加组名条件
    DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.name", (const char **)group_names.values,
                               group_names.values_num);
    result = DBselect("%s", sql);

    // 处理查询结果
    while (NULL != (row = DBfetch(result)))
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为`lld_groups_save`的函数，该函数用于保存一组组（`zbx_vector_ptr_t *groups`）及其相关联的组原型（`const zbx_vector_ptr_t *group_prototypes`）的信息到数据库。
 *
 *以下是代码的详细注释：
 *
 *1. 定义函数名`__function_name`，用于打印调试信息。
 *2. 定义变量，包括循环变量`i`、`j`，以及`upd_groups_num`，用于统计需要更新的组数量。
 *3. 定义组指针`group`和组原型指针`group_prototype`，用于遍历组和组原型。
 *4. 定义主机指针`host`，用于处理组关联的主机。
 *5. 定义组ID列表`new_group_prototype_ids`，用于存储新分配的组ID。
 *6. 初始化组ID列表。
 *7. 遍历组列表，检查每个组是否已发现。
 *8. 如果组尚未被发现，则为新组分配ID，并保存组名称和组原型信息到数据库。
 *9. 如果组已被发现，则更新组名称和组原型信息。
 *10. 遍历新组列表，将组信息插入到数据库。
 *11. 如果组更新数量不为0，则提交数据库更新并清理缓存。
 *12. 清理新组列表。
 *13. 提交数据库事务。
 *14. 销毁组ID列表。
 *15. 打印调试信息。
 *
 *整个代码块的主要目的是将组及其相关联的组原型信息保存到数据库，以便后续处理和查询。
 ******************************************************************************/
static void lld_groups_save(zbx_vector_ptr_t *groups, const zbx_vector_ptr_t *group_prototypes)
{
    // 定义函数名
    const char *__function_name = "lld_groups_save";

    // 定义变量
    int i, j, upd_groups_num = 0;
    zbx_lld_group_t *group;
    const zbx_lld_group_prototype_t *group_prototype;
    zbx_lld_host_t *host;
    zbx_uint64_t groupid = 0;
    char *sql = NULL, *name_esc, *name_proto_esc;
    size_t sql_alloc = 0, sql_offset = 0;
    zbx_db_insert_t db_insert, db_insert_gdiscovery;
    zbx_vector_ptr_t new_groups;
    zbx_vector_uint64_t new_group_prototype_ids;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "Enter %s()", __function_name);

    // 创建一个新的组ID列表
    zbx_vector_uint64_create(&new_group_prototype_ids);

    // 遍历组列表
    for (i = 0; i < groups->values_num; i++)
    {
        group = (zbx_lld_group_t *)groups->values[i];

        // 如果组尚未被发现，跳过
        if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
            continue;

        // 如果组ID为0，则为新组分配ID
        if (0 == group->groupid)
        {
            group->groupid = groupid++;

            // 保存新组的信息到数据库
            zbx_db_insert_prepare(&db_insert, "hstgrp", "groupid", "name", "flags", NULL);

            // 查找组原型并在数据库中插入新组
            if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE))
            {
                if (FAIL != (j = zbx_vector_ptr_bsearch(group_prototypes, &group->group_prototypeid,
                                                       ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
                {
                    group_prototype = (zbx_lld_group_prototype_t *)group_prototypes->values[j];

                    // 保存组原型信息到数据库
                    zbx_db_insert_add_values(&db_insert, group->groupid, group->name,
                                            (int)ZBX_FLAG_DISCOVERY_CREATED);

                    // 将新组添加到新组列表
                    zbx_vector_ptr_append(&new_groups, group);
                }
                else
                    THIS_SHOULD_NEVER_HAPPEN;
            }

            // 保存主机到新组
            for (j = 0; j < group->hosts.values_num; j++)
            {
                host = (zbx_lld_host_t *)group->hosts.values[j];

                // 将主机添加到新组
                zbx_vector_uint64_append(&host->new_groupids, group->groupid);
            }
        }
        else
        {
            // 如果组ID不为0，则更新组信息
            if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE))
            {
                // 更新组名称和组原型
                zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update hstgrp set ");
                if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
                {
                    name_esc = DBdyn_escape_string(group->name);

                    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "name='%s'", name_esc);

                    zbx_free(name_esc);
                }
                zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                                " where groupid=" ZBX_FS_UI64 ";\
", group->groupid);
            }

            // 查找组原型并更新组名称
            if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
            {
                if (FAIL != (j = zbx_vector_ptr_bsearch(group_prototypes, &group->group_prototypeid,
                                                       ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
                {
                    group_prototype = (zbx_lld_group_prototype_t *)group_prototypes->values[j];

                    name_proto_esc = DBdyn_escape_string(group_prototype->name);

                    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                                "update group_discovery"
                                " set name='%s'"
                                " where groupid=" ZBX_FS_UI64 ";\
",
                                name_proto_esc, group->groupid);

                    zbx_free(name_proto_esc);
                }
                else
                    THIS_SHOULD_NEVER_HAPPEN;
            }
            // 提交数据库更新
            if (0 != upd_groups_num)
            {
                DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
                DBexecute("%s", sql);
                zbx_free(sql);
            }
        }
    }

    // 插入新组到数据库
    if (0 != new_group_prototype_ids.values_num)
    {
        zbx_db_insert_prepare(&db_insert, "hstgrp", "groupid", "name", "flags", NULL);

        // 遍历新组列表并插入组信息
        for (i = 0; i < new_groups.values_num; i++)
        {
            group = (zbx_lld_group_t *)new_groups.values[i];

            zbx_db_insert_add_values(&db_insert, group->groupid, group->name,
                                    (int)ZBX_FLAG_DISCOVERY_CREATED);

            // 查找组原型并插入组
            if (FAIL != zbx_vector_ptr_bsearch(group_prototypes, &group->group_prototypeid,
                                               ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC))
            {
                group_prototype = (zbx_lld_group_prototype_t *)group_prototypes->values[j];

                // 保存组原型信息到数据库
                zbx_db_insert_add_values(&db_insert, group->groupid,
                                        group_prototype->name, NULL);
            }
            else
                THIS_SHOULD_NEVER_HAPPEN;
        }

        // 提交数据库插入
        zbx_db_insert_execute(&db_insert);
        zbx_db_insert_clean(&db_insert);

        // 添加权限
        lld_groups_save_rights(&new_groups);

        // 销毁新组列表
        zbx_vector_ptr_destroy(&new_groups);
    }

    // 提交数据库事务
    DBcommit();

    // 销毁组ID列表
    zbx_vector_uint64_destroy(&new_group_prototype_ids);

    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	zbx_vector_ptr_destroy(&group_rights);
	zbx_vector_str_destroy(&group_names);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_save                                                  *
 *                                                                            *
 * Parameters: groups           - [IN/OUT] list of groups; should be sorted   *
 *                                         by groupid                         *
 *             group_prototypes - [IN] list of group prototypes; should be    *
 *                                     sorted by group_prototypeid            *
 *                                                                            *
 ******************************************************************************/
static void	lld_groups_save(zbx_vector_ptr_t *groups, const zbx_vector_ptr_t *group_prototypes)
{
	const char			*__function_name = "lld_groups_save";

	int				i, j, upd_groups_num = 0;
	zbx_lld_group_t			*group;
	const zbx_lld_group_prototype_t	*group_prototype;
	zbx_lld_host_t			*host;
	zbx_uint64_t			groupid = 0;
	char				*sql = NULL, *name_esc, *name_proto_esc;
	size_t				sql_alloc = 0, sql_offset = 0;
	zbx_db_insert_t			db_insert, db_insert_gdiscovery;
	zbx_vector_ptr_t		new_groups;
	zbx_vector_uint64_t		new_group_prototype_ids;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&new_group_prototype_ids);

	for (i = 0; i < groups->values_num; i++)
	{
		group = (zbx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		if (0 == group->groupid)
			zbx_vector_uint64_append(&new_group_prototype_ids, group->group_prototypeid);
		else if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE))
			upd_groups_num++;
	}

	if (0 == new_group_prototype_ids.values_num && 0 == upd_groups_num)
		goto out;

	DBbegin();

	if (SUCCEED != DBlock_group_prototypeids(&new_group_prototype_ids))
	{
		/* the host group prototype was removed while processing lld rule */
		DBrollback();
		goto out;
	}

	if (0 != new_group_prototype_ids.values_num)
	{
		groupid = DBget_maxid_num("hstgrp", new_group_prototype_ids.values_num);

		zbx_db_insert_prepare(&db_insert, "hstgrp", "groupid", "name", "flags", NULL);

		zbx_db_insert_prepare(&db_insert_gdiscovery, "group_discovery", "groupid", "parent_group_prototypeid",
				"name", NULL);

		zbx_vector_ptr_create(&new_groups);
	}

	if (0 != upd_groups_num)
	{
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	for (i = 0; i < groups->values_num; i++)
	{
		group = (zbx_lld_group_t *)groups->values[i];

		if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
			continue;

		if (0 == group->groupid)
		{
			group->groupid = groupid++;

			zbx_db_insert_add_values(&db_insert, group->groupid, group->name,
					(int)ZBX_FLAG_DISCOVERY_CREATED);

			if (FAIL != (j = zbx_vector_ptr_bsearch(group_prototypes, &group->group_prototypeid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				group_prototype = (zbx_lld_group_prototype_t *)group_prototypes->values[j];

				zbx_db_insert_add_values(&db_insert_gdiscovery, group->groupid,
						group->group_prototypeid, group_prototype->name);
			}
			else
				THIS_SHOULD_NEVER_HAPPEN;

			for (j = 0; j < group->hosts.values_num; j++)
			{
				host = (zbx_lld_host_t *)group->hosts.values[j];

				/* hosts will be linked to a new host groups */
				zbx_vector_uint64_append(&host->new_groupids, group->groupid);
			}

			zbx_vector_ptr_append(&new_groups, group);
		}
		else
		{
			if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE))
			{
				zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update hstgrp set ");
				if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
				{
					name_esc = DBdyn_escape_string(group->name);

					zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "name='%s'", name_esc);

					zbx_free(name_esc);
				}
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						" where groupid=" ZBX_FS_UI64 ";\n", group->groupid);
			}

			if (0 != (group->flags & ZBX_FLAG_LLD_GROUP_UPDATE_NAME))
			{
				if (FAIL != (j = zbx_vector_ptr_bsearch(group_prototypes, &group->group_prototypeid,
						ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
				{
					group_prototype = (zbx_lld_group_prototype_t *)group_prototypes->values[j];

					name_proto_esc = DBdyn_escape_string(group_prototype->name);

					zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
							"update group_discovery"
							" set name='%s'"
							" where groupid=" ZBX_FS_UI64 ";\n",
							name_proto_esc, group->groupid);

					zbx_free(name_proto_esc);
				}
				else
					THIS_SHOULD_NEVER_HAPPEN;
			}
		}
	}

	if (0 != upd_groups_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		DBexecute("%s", sql);
		zbx_free(sql);
	}

	if (0 != new_group_prototype_ids.values_num)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);

		zbx_db_insert_execute(&db_insert_gdiscovery);
		zbx_db_insert_clean(&db_insert_gdiscovery);

		lld_groups_save_rights(&new_groups);
		zbx_vector_ptr_destroy(&new_groups);
	}

	DBcommit();
out:
	zbx_vector_uint64_destroy(&new_group_prototype_ids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hostmacros_get                                               *
 *                                                                            *
 * Purpose: retrieve list of host macros which should be present on the each  *
 *          discovered host                                                   *
 *                                                                            *
 * Parameters: hostmacros - [OUT] list of host macros                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定 `lld_ruleid` 的主机宏（主机宏是指与特定主机关联的宏），并将获取到的主机宏添加到 `hostmacros` 指向的 vector 中。输出结果为一个包含主机宏的 vector。
 ******************************************************************************/
// 定义一个静态函数，用于获取指定 lld_ruleid 的主机宏
static void lld_hostmacros_get(zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *hostmacros)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "lld_hostmacros_get";

    // 声明变量
    DB_RESULT		result;
    DB_ROW			row;
    zbx_lld_hostmacro_t	*hostmacro;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
/******************************************************************************
 * *
 *该代码的主要目的是对zbx_vector_t类型的指针hostmacros中的主机宏进行处理。具体来说，它会将这些主机宏与hosts中的主机进行匹配，并将匹配到的主机宏的值更新为从数据库中查询到的值。同时，如果查询到的主机宏尚未被添加到主机的新主机宏数组中，会将它们添加到该数组中。在处理完成后，还会释放主机宏结构体占用的内存。最后，对需要删除的主机宏ID进行排序，以便后续操作。
 ******************************************************************************/
// 定义静态函数lld_hostmacros_make，参数包括一个zbx_vector_ptr_t类型的指针，一个zbx_vector_ptr_t类型的指针，和一个zbx_vector_uint64_t类型的指针
static void lld_hostmacros_make(const zbx_vector_ptr_t *hostmacros, zbx_vector_ptr_t *hosts, zbx_vector_uint64_t *del_hostmacroids)
{
	// 定义变量，包括一个指向函数名的指针，一个DB_RESULT类型的变量，一个DB_ROW类型的变量，以及两个int类型的变量
	const char *__function_name = "lld_hostmacros_make";
	DB_RESULT result;
	DB_ROW row;
	int i, j;

	// 定义一个zbx_vector_uint64_t类型的变量hostids，用于存储主机的ID
	zbx_vector_uint64_create(&hostids);

	// 遍历hosts中的所有主机
	for (i = 0; i < hosts->values_num; i++)
	{
		// 获取主机结构体指针
		host = (zbx_lld_host_t *)hosts->values[i];

		// 如果主机尚未被探测到，跳过
		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		// 为主机的新主机宏数组分配空间
		zbx_vector_ptr_reserve(&host->new_hostmacros, hostmacros->values_num);

		// 遍历hostmacros中的所有主机宏
		for (j = 0; j < hostmacros->values_num; j++)
		{
			// 分配一个新的主机宏结构体
			hostmacro = (zbx_lld_hostmacro_t *)zbx_malloc(NULL, sizeof(zbx_lld_hostmacro_t));

			// 初始化主机宏的结构体成员
			hostmacro->hostmacroid = 0;
			hostmacro->macro = zbx_strdup(NULL, ((zbx_lld_hostmacro_t *)hostmacros->values[j])->macro);
			hostmacro->value = zbx_strdup(NULL, ((zbx_lld_hostmacro_t *)hostmacros->values[j])->value);

			// 将主机宏添加到主机的新主机宏数组中
			zbx_vector_ptr_append(&host->new_hostmacros, hostmacro);
		}

		// 如果主机ID不为0，将其添加到hostids中
		if (0 != host->hostid)
			zbx_vector_uint64_append(&hostids, host->hostid);
	}

	// 如果hostids中的元素数量不为0，执行以下操作：
	if (0 != hostids.values_num)
	{
		// 分配一个字符串，用于存储SQL语句
		char *sql = NULL;
		size_t sql_alloc = 0, sql_offset = 0;

		// 构建SQL语句，查询主机宏表中与给定主机ID匹配的记录
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hostmacroid,hostid,macro,value"
				" from hostmacro"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);

		// 执行SQL查询
		result = DBselect("%s", sql);

		// 释放SQL字符串
		zbx_free(sql);

		// 遍历查询结果
		while (NULL != (row = DBfetch(result)))
		{
			// 将主机ID转换为整数类型
			ZBX_STR2UINT64(hostid, row[1]);

			// 在hosts中查找对应的主机
			if (FAIL == zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC))
			{
				// 这种情况不应该发生，跳过
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			// 获取对应主机结构体指针
			host = (zbx_lld_host_t *)hosts->values[zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)];

			// 遍历主机的新主机宏数组
			for (i = 0; i < host->new_hostmacros.values_num; i++)
			{
				// 获取主机宏结构体指针
				hostmacro = (zbx_lld_hostmacro_t *)host->new_hostmacros.values[i];

				// 如果主机宏的宏名与查询结果中的宏名相同，执行以下操作：
				if (0 == strcmp(hostmacro->macro, row[2]))
				{
					// 如果主机宏的值与查询结果中的值相同，执行以下操作：
					if (0 == strcmp(hostmacro->value, row[3]))
					{
						// 释放主机宏结构体占用的内存
						lld_hostmacro_free(hostmacro);

						// 从主机的新主机宏数组中移除该主机宏
						zbx_vector_ptr_remove(&host->new_hostmacros, i);
					}
					else
					{
						// 更新主机宏的值
						ZBX_STR2UINT64(hostmacroid, row[0]);
					}
				}
			}
		}
		// 释放查询结果
		DBfree_result(result);

		// 对需要删除的主机宏ID进行排序
		zbx_vector_uint64_sort(del_hostmacroids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	// 释放hostids占用的内存
	zbx_vector_uint64_destroy(&hostids);

	// 输出调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

			if (FAIL == (i = zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			host = (zbx_lld_host_t *)hosts->values[i];

			for (i = 0; i < host->new_hostmacros.values_num; i++)
			{
				hostmacro = (zbx_lld_hostmacro_t *)host->new_hostmacros.values[i];

				if (0 == strcmp(hostmacro->macro, row[2]))
					break;
			}

			if (i == host->new_hostmacros.values_num)
			{
				/* host macros which should be deleted */
				ZBX_STR2UINT64(hostmacroid, row[0]);
				zbx_vector_uint64_append(del_hostmacroids, hostmacroid);
			}
			else
			{
				/* host macros which are already added */
				if (0 == strcmp(hostmacro->value, row[3]))	/* value doesn't changed */
				{
					lld_hostmacro_free(hostmacro);
/******************************************************************************
 * *
 *主要目的：这个代码块用于处理父主机（parent_hostid）下的主机（hosts）与模板（templates）的关联关系。具体来说，它执行以下操作：
 *
 *1. 查询数据库，获取与父主机关联的所有模板。
 *2. 为每个主机分配关联模板，并将它们存储在主机对象的lnk_templateids向量中。
 *3. 查询已关联的主机和模板，以确定哪些模板需要解绑（del_templateids）。
 *4. 对解绑的模板进行排序，以便后续处理。
 *
 *整个代码块的主要目的是在父主机和主机之间建立正确的模板关联关系。
 ******************************************************************************/
static void lld_templates_make(zbx_uint64_t parent_hostid, zbx_vector_ptr_t *hosts)
{
    // 定义函数名
    const char *__function_name = "lld_templates_make";

    // 声明变量
    DB_RESULT result;
    DB_ROW row;
    zbx_vector_uint64_t templateids, hostids;
    zbx_uint64_t templateid, hostid;
    zbx_lld_host_t *host;
    int i, j;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建vector
    zbx_vector_uint64_create(&templateids);
    zbx_vector_uint64_create(&hostids);

    /* 查询需要关联的模板 */

    result = DBselect("select templateid from hosts_templates where hostid=" ZBX_FS_UI64, parent_hostid);

    while (NULL != (row = DBfetch(result)))
    {
        // 将字符串转换为uint64_t类型
        ZBX_STR2UINT64(templateid, row[0]);
        // 向vector中添加元素
        zbx_vector_uint64_append(&templateids, templateid);
    }
    // 释放数据库查询结果
    DBfree_result(result);

    // 对vector进行排序
    zbx_vector_uint64_sort(&templateids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    /* 查询已创建的host列表 */

    for (i = 0; i < hosts->values_num; i++)
    {
        host = (zbx_lld_host_t *)hosts->values[i];

        // 如果host尚未被发现，跳过
        if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
            continue;

        // 为host分配内存
        zbx_vector_uint64_reserve(&host->lnk_templateids, templateids.values_num);
        // 添加关联模板
        for (j = 0; j < templateids.values_num; j++)
            zbx_vector_uint64_append(&host->lnk_templateids, templateids.values[j]);

        // 如果host具有hostid，则将其添加到hostids中
        if (0 != host->hostid)
            zbx_vector_uint64_append(&hostids, host->hostid);
    }

    // 如果hostids不为空，则执行以下操作：
    if (0 != hostids.values_num)
    {
        char *sql = NULL;
        size_t sql_alloc = 0, sql_offset = 0;

        /* 查询已关联的模板 */

        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                        "select hostid,templateid"
                        " from hosts_templates"
                        " where");
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);

        result = DBselect("%s", sql);

        zbx_free(sql);

        while (NULL != (row = DBfetch(result)))
        {
            // 将字符串转换为uint64_t类型
            ZBX_STR2UINT64(hostid, row[0]);
            ZBX_STR2UINT64(templateid, row[1]);

            // 如果在hosts中找不到该host，跳过
            if (FAIL == (i = zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
            {
                THIS_SHOULD_NEVER_HAPPEN;
                continue;
            }

            host = (zbx_lld_host_t *)hosts->values[i];

            // 如果在host的关联模板中找不到该模板，跳过
            if (FAIL == (i = zbx_vector_uint64_bsearch(&host->lnk_templateids, templateid,
                                                       ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
            {
                /* 需要解绑的模板 */
                zbx_vector_uint64_append(&host->del_templateids, templateid);
            }
            else
            {
                /* 已经关联的模板 */
                zbx_vector_uint64_remove(&host->lnk_templateids, i);
            }
        }
        DBfree_result(result);

        // 对host的del_templateids进行排序
        for (i = 0; i < hosts->values_num; i++)
        {
            host = (zbx_lld_host_t *)hosts->values[i];

            if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
                continue;

            zbx_vector_uint64_sort(&host->del_templateids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
        }
    }

    // 释放内存
    zbx_vector_uint64_destroy(&hostids);
    zbx_vector_uint64_destroy(&templateids);

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

				/* templates which should be unlinked */
				zbx_vector_uint64_append(&host->del_templateids, templateid);
			}
			else
			{
				/* templates which are already linked */
				zbx_vector_uint64_remove(&host->lnk_templateids, i);
			}
		}
		DBfree_result(result);

		for (i = 0; i < hosts->values_num; i++)
		{
			host = (zbx_lld_host_t *)hosts->values[i];

			if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
				continue;

			zbx_vector_uint64_sort(&host->del_templateids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		}
	}

	zbx_vector_uint64_destroy(&hostids);
	zbx_vector_uint64_destroy(&templateids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_hosts_save                                                   *
 *                                                                            *
 * Parameters: hosts            - [IN] list of hosts;                         *
 *                                     should be sorted by hostid             *
 *             status           - [IN] initial host status                    *
 *             del_hostgroupids - [IN] host groups which should be deleted    *
 *             del_hostmacroids - [IN] host macros which should be deleted    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是处理主机信息，包括新增、更新、删除等操作。首先遍历传入的主机数组，获取每个主机的信息，然后根据主机的状态进行相应的处理。最后将处理后的结果保存到数据库中。
 ******************************************************************************/
// 函数声明，用于保存主机信息
static void	lld_hosts_save(...)
{
    // 定义一些变量和结构体，用于存储和处理主机信息
    // ...

    // 遍历传入的主机数组
    for (i = 0; i < hosts->values_num; i++)
    {
        // 获取当前主机的信息
        host = (zbx_lld_host_t *)hosts->values[i];

        // 判断当前主机是否是新发现的主机
        if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
        {
            // 如果主机是新的，则进行一些处理
            new_hosts++;
            if (HOST_INVENTORY_DISABLED != inventory_mode)
                new_host_inventories++;
        }
        // 如果主机不是新的，则进行其他处理
        else
        {
            // 如果主机需要更新，则进行一些处理
            if (0 != (host->flags & ZBX_FLAG_LLD_HOST_UPDATE))
                upd_hosts++;

            // 如果主机的库存模式发生了变化，则进行一些处理
            if (host->inventory_mode != inventory_mode)
            {
                if (HOST_INVENTORY_DISABLED == inventory_mode)
                    zbx_vector_uint64_append(&del_host_inventory_hostids, host->hostid);
                else if (HOST_INVENTORY_DISABLED == host->inventory_mode)
                    new_host_inventories++;
                else
                    zbx_vector_uint64_append(&upd_host_inventory_hostids, host->hostid);
            }
        }

        // 计算新增的主机组数
        new_hostgroups += host->new_groupids.values_num;

        // 遍历主机的接口数组
        for (j = 0; j < host->interfaces.values_num; j++)
        {
            // 获取当前接口的信息
            interface = (zbx_lld_interface_t *)host->interfaces.values[j];

            // 如果接口是新的，则进行一些处理
            if (0 == interface->interfaceid)
                new_interfaces++;
            // 如果接口需要更新，则进行一些处理
            else if (0 != (interface->flags & ZBX_FLAG_LLD_INTERFACE_UPDATE))
                upd_interfaces++;
            // 如果接口需要删除，则进行一些处理
            else if (0 != (interface->flags & ZBX_FLAG_LLD_INTERFACE_REMOVE))
                zbx_vector_uint64_append(&del_interfaceids, interface->interfaceid);
        }

        // 遍历主机的新宏数组
        for (j = 0; j < host->new_hostmacros.values_num; j++)
        {
            // 获取当前宏的信息
            hostmacro = (zbx_lld_hostmacro_t *)host->new_hostmacros.values[j];

            // 如果宏是新的，则进行一些处理
            if (0 == hostmacro->hostmacroid)
                new_hostmacros++;
            // 如果宏需要更新，则进行一些处理
            else
                upd_hostmacros++;
        }
    }

    // 如果没有任何需要处理的主机或接口，则直接退出
    if (0 == new_hosts && 0 == new_host_inventories && 0 == upd_hosts && 0 == upd_interfaces &&
        0 == upd_hostmacros && 0 == new_hostgroups && 0 == new_hostmacros && 0 == new_interfaces &&
        0 == del_hostgroupids->values_num && 0 == del_hostmacroids->values_num &&
        0 == upd_host_inventory_hostids.values_num && 0 == del_host_inventory_hostids.values_num &&
        0 == del_interfaceids.values_num)
    {
        goto out;
    }

    // 接下来进行数据库的操作
    DBbegin();

    // 如果父主机不存在，则进行一些处理
    if (SUCCEED != DBlock_hostid(parent_hostid))
    {
        // 如果父主机不存在，则回滚数据库
        DBrollback();
        goto out;
    }

    // 如果需要新增主机，则进行一些处理
    if (0 != new_hosts)
    {
        // 获取最大的主机ID
        hostid = DBget_maxid_num("hosts", new_hosts);

        // 进行数据库的插入操作
        // ...
    }

    // 如果需要新增库存，则进行一些处理
    if (0 != new_host_inventories)
    {
        // 进行数据库的插入操作
        // ...
    }

    // 如果需要更新主机，则进行一些处理
    if (0 != upd_hosts || 0 != upd_interfaces || 0 != upd_hostmacros)
    {
        // 进行数据库的更新操作
        // ...
    }

    // 如果需要新增组，则进行一些处理
    if (0 != new_hostgroups)
    {
        // 获取最大的组ID
        hostgroupid = DBget_maxid_num("hosts_groups", new_hostgroups);

        // 进行数据库的插入操作
        // ...
    }

    // 如果需要新增宏，则进行一些处理
    if (0 != new_hostmacros)
    {
        // 获取最大的宏ID
        hostmacroid = DBget_maxid_num("hostmacro", new_hostmacros);

        // 进行数据库的插入操作
        // ...
    }

    // 如果需要新增接口，则进行一些处理
    if (0 != new_interfaces)
    {
        // 获取最大的接口ID
        interfaceid = DBget_maxid_num("interface", new_interfaces);

        // 进行数据库的插入操作
        // ...
    }

    // 如果需要更新接口，则进行一些处理
    if (0 != upd_interfaces)
    {
        // 进行数据库的更新操作
        // ...
    }

    // 如果需要删除组，则进行一些处理
    if (0 != del_hostgroupids->values_num)
    {
        // 进行数据库的删除操作
        // ...
    }

    // 如果需要删除宏，则进行一些处理
    if (0 != del_hostmacroids->values_num)
    {
        // 进行数据库的删除操作
        // ...
    }

    // 如果需要更新库存，则进行一些处理
    if (0 != upd_host_inventory_hostids.values_num)
    {
        // 进行数据库的更新操作
        // ...
    }

    // 如果需要删除接口，则进行一些处理
    if (0 != del_interfaceids.values_num)
    {
        // 进行数据库的删除操作
        // ...
    }

    // 提交数据库操作
    DBcommit();

    // 退出函数
    goto out;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_templates_link                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是遍历一个主机列表，对于每个主机，判断其是否需要解绑或绑定模板。如果需要解绑模板，调用DBdelete_template_elements函数；如果需要绑定模板，调用DBcopy_template_elements函数。在此过程中，如果遇到错误，将错误信息拼接至全局错误信息数组中，并释放err内存。
 ******************************************************************************/
// 定义一个静态函数，用于链接或解绑主机和模板
static void lld_templates_link(const zbx_vector_ptr_t *hosts, char **error)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "lld_templates_link";

    // 定义一个整型变量，用于循环计数
    int i;

    // 定义一个指向zbx_lld_host_t结构体的指针，用于遍历主机列表
    zbx_lld_host_t *host;

    // 定义一个字符串指针，用于存储错误信息
    char *err;

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 遍历主机列表
    for (i = 0; i < hosts->values_num; i++)
    {
        // 获取当前主机
        host = (zbx_lld_host_t *)hosts->values[i];

        // 如果主机未被发现，跳过此次循环
        if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
            continue;

        // 如果主机需要解绑模板，执行DBdelete_template_elements函数
        if (0 != host->del_templateids.values_num)
        {
            if (SUCCEED != DBdelete_template_elements(host->hostid, &host->del_templateids, &err))
            {
                // 拼接错误信息
                *error = zbx_strdcatf(*error, "Cannot unlink template: %s.\
", err);
                // 释放err内存
                zbx_free(err);
            }
        }

        // 如果主机需要绑定模板，执行DBcopy_template_elements函数
        if (0 != host->lnk_templateids.values_num)
/******************************************************************************
 * *
 *这段代码的主要目的是删除不符合条件的hosts（发现和未发现的）。具体来说，它会执行以下操作：
 *
 *1. 遍历hosts vector中的每个元素。
 *2. 判断host是否已经发现，并根据条件将其hostid添加到不同的集合（del_hostids、lc_hostids或ts_hostids）。
 *3. 如果满足一定条件，更新host_discovery表中的lastcheck和ts_delete字段。
 *4. 执行数据库操作，首先执行multiple_update语句，然后提交事务。
 *5. 释放sql内存。
 *6. 如果del_hostids中的元素数量大于0，则删除hosts并提交事务。
 *7. 销毁暂存的数据结构。
 ******************************************************************************/
static void lld_hosts_remove(const zbx_vector_ptr_t *hosts, int lifetime, int lastcheck)
{
	/* 定义字符指针和大小，用于存储SQL语句 */
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;

	/* 定义三个zbx_vector_uint64类型变量，用于存储不同类型的hostid集合 */
	zbx_vector_uint64_t del_hostids, lc_hostids, ts_hostids;

	/* 遍历hosts vector中的每个元素 */
	for (int i = 0; i < hosts->values_num; i++)
	{
		/* 获取host结构体指针 */
		const zbx_lld_host_t *host = (zbx_lld_host_t *)hosts->values[i];

		/* 如果hostid为0，跳过此次循环 */
		if (0 == host->hostid)
			continue;

		/* 判断host是否已经发现，如果没有发现，则执行以下操作：
		 * 1. 计算ts_delete值
		 * 2. 如果lastcheck大于ts_delete，将hostid添加到del_hostids中
		 * 3. 否则，如果host的ts_delete值与计算出的ts_delete值不同，将hostid添加到ts_hostids中 */
		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
		{
			int ts_delete = lld_end_of_life(host->lastcheck, lifetime);

			if (lastcheck > ts_delete)
			{
				zbx_vector_uint64_append(&del_hostids, host->hostid);
			}
			else if (host->ts_delete != ts_delete)
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"update host_discovery"
						" set ts_delete=%d"
						" where hostid=" ZBX_FS_UI64 ";\
",
						ts_delete, host->hostid);
			}
		}
		/* 如果host已经发现，则执行以下操作：
		 * 1. 将hostid添加到lc_hostids中
		 * 2. 如果host的ts_delete值不为0，将hostid添加到ts_hostids中 */
		else
		{
			zbx_vector_uint64_append(&lc_hostids, host->hostid);
			if (0 != host->ts_delete)
				zbx_vector_uint64_append(&ts_hostids, host->hostid);
		}
	}

	/* 如果lc_hostids中的元素数量大于0，则执行以下操作：
	 * 1. 更新host_discovery表中的lastcheck字段 */
	if (0 != lc_hostids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update host_discovery set lastcheck=%d where",
				lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				lc_hostids.values, lc_hostids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 如果ts_hostids中的元素数量大于0，则执行以下操作：
	 * 1. 将ts_delete字段更新为0 */
	if (0 != ts_hostids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update host_discovery set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				ts_hostids.values, ts_hostids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 如果sql_offset大于16，则执行以下操作：
	 * 1. 执行multiple_update语句
	 * 2. 提交数据库事务 */
	if (16 < sql_offset)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		DBbegin();

		DBexecute("%s", sql);

		DBcommit();
	}

	/* 释放sql内存 */
	zbx_free(sql);

	/* 如果del_hostids中的元素数量大于0，则执行以下操作：
	 * 1. 对del_hostids进行排序
	 * 2. 删除hosts */
	if (0 != del_hostids.values_num)
	{
		zbx_vector_uint64_sort(&del_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		DBbegin();

		DBdelete_hosts(&del_hostids);

		DBcommit();
	}

	/* 销毁暂存的数据结构 */
	zbx_vector_uint64_destroy(&ts_hostids);
	zbx_vector_uint64_destroy(&lc_hostids);
	zbx_vector_uint64_destroy(&del_hostids);
}


		DBbegin();

		DBexecute("%s", sql);

		DBcommit();
	}

	zbx_free(sql);

	if (0 != del_hostids.values_num)
	{
		zbx_vector_uint64_sort(&del_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		DBbegin();

		DBdelete_hosts(&del_hostids);

		DBcommit();
	}

	zbx_vector_uint64_destroy(&ts_hostids);
	zbx_vector_uint64_destroy(&lc_hostids);
	zbx_vector_uint64_destroy(&del_hostids);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_groups_remove                                                *
 *                                                                            *
 * Purpose: updates group_discovery.lastcheck and group_discovery.ts_delete   *
 *          fields; removes lost resources                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是删除不需要的组发现数据，并对组进行重新排序。具体来说，它执行以下操作：
 *
 *1. 遍历组向量，检查每个组的状态。
 *2. 如果组未被发现，计算其结束寿命，并将需要删除的组ID添加到删除列表。
 *3. 如果组已被发现，将组ID添加到最近检查列表和结束寿命列表。
 *4. 如果存在需要更新的组，构造SQL语句并更新数据库。
 *5. 如果删除列表不为空，执行删除操作。
 *6. 释放分配的内存。
 ******************************************************************************/
static void lld_groups_remove(const zbx_vector_ptr_t *groups, int lifetime, int lastcheck)
{
	/* 定义变量，用于存储SQL语句、内存分配大小、SQL语句偏移量 */
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	/* 定义一个指向zbx_lld_group_t结构体的指针 */
	const zbx_lld_group_t *group;
	/* 定义三个uint64类型的向量，用于存储删除的组ID、最近检查的组ID、结束寿命的组ID */
	zbx_vector_uint64_t del_groupids, lc_groupids, ts_groupids;
	/* 定义一个循环变量 */
	int i;

	/* 如果组向量为空，直接返回 */
	if (0 == groups->values_num)
		return;

	/* 初始化三个uint64类型的向量 */
	zbx_vector_uint64_create(&del_groupids);
	zbx_vector_uint64_create(&lc_groupids);
	zbx_vector_uint64_create(&ts_groupids);

	/* 开始一个DB的多更新操作，用于更新组发现数据 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 遍历组向量中的每个组 */
	for (i = 0; i < groups->values_num; i++)
	{
		group = (zbx_lld_group_t *)groups->values[i];

		/* 如果组ID为0，跳过这个组 */
		if (0 == group->groupid)
			continue;

		/* 如果组未被发现，跳过这个组 */
		if (0 == (group->flags & ZBX_FLAG_LLD_GROUP_DISCOVERED))
		{
			int ts_delete = lld_end_of_life(group->lastcheck, lifetime);

			/* 如果lastcheck大于ts_delete，将组ID添加到删除列表 */
			if (lastcheck > ts_delete)
			{
				zbx_vector_uint64_append(&del_groupids, group->groupid);
			}
			/* 否则，如果组ID对应的ts_delete与当前计算的ts_delete不同，更新组发现数据 */
			else if (group->ts_delete != ts_delete)
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"update group_discovery"
						" set ts_delete=%d"
						" where groupid=" ZBX_FS_UI64 ";\
",
						ts_delete, group->groupid);
			}
		}
		/* 如果组已经被发现，将组ID添加到最近检查列表和结束寿命列表 */
		else
		{
			zbx_vector_uint64_append(&lc_groupids, group->groupid);
			if (0 != group->ts_delete)
				zbx_vector_uint64_append(&ts_groupids, group->groupid);
		}
	}

	/* 如果最近检查的组ID列表不为空，更新数据库 */
	if (0 != lc_groupids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update group_discovery set lastcheck=%d where",
				lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid",
				lc_groupids.values, lc_groupids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 如果结束寿命的组ID列表不为空，更新数据库 */
	if (0 != ts_groupids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update group_discovery set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid",
				ts_groupids.values, ts_groupids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 如果SQL语句偏移量大于16，执行DB的多更新操作 */
	if (16 < sql_offset)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		DBbegin();

		DBexecute("%s", sql);

		DBcommit();
	}

	/* 释放SQL语句内存 */
	zbx_free(sql);

	/* 如果删除列表不为空，执行删除操作 */
	if (0 != del_groupids.values_num)
	{
		zbx_vector_uint64_sort(&del_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		DBbegin();

		DBdelete_groups(&del_groupids);

		DBcommit();
	}

	/* 释放内存 */
	zbx_vector_uint64_destroy(&ts_groupids);
	zbx_vector_uint64_destroy(&lc_groupids);
	zbx_vector_uint64_destroy(&del_groupids);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_interfaces_get                                               *
 *                                                                            *
 * Purpose: retrieves list of interfaces from the lld rule's host             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取 LLDP 规则对应的接口信息，并将这些接口信息存储在一个接口列表中。为了实现这个目的，代码首先从数据库中查询 LLDP 规则对应的接口信息，然后逐行解析这些接口信息，并将它们添加到一个接口列表中。最后，对接口列表进行排序。
 ******************************************************************************/
// 定义一个静态函数，用于获取 LLDP 接口信息
static void lld_interfaces_get(zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *interfaces)
{
	// 声明变量
	DB_RESULT		result;
	DB_ROW			row;
	zbx_lld_interface_t	*interface;

	// 从数据库中查询 LLDP 规则对应的接口信息
	result = DBselect(
			"select hi.interfaceid,hi.type,hi.main,hi.useip,hi.ip,hi.dns,hi.port,hi.bulk"
			" from interface hi,items i"
			" where hi.hostid=i.hostid"
				" and i.itemid=" ZBX_FS_UI64,
			lld_ruleid);

	// 逐行处理查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 分配内存存储接口信息
		interface = (zbx_lld_interface_t *)zbx_malloc(NULL, sizeof(zbx_lld_interface_t));

		// 解析接口信息
		ZBX_STR2UINT64(interface->interfaceid, row[0]);
		interface->type = (unsigned char)atoi(row[1]);
		interface->main = (unsigned char)atoi(row[2]);
		interface->useip = (unsigned char)atoi(row[3]);
		interface->ip = zbx_strdup(NULL, row[4]);
		interface->dns = zbx_strdup(NULL, row[5]);
		interface->port = zbx_strdup(NULL, row[6]);
		interface->bulk = (unsigned char)atoi(row[7]);

		// 将接口信息添加到接口列表中
		zbx_vector_ptr_append(interfaces, interface);
/******************************************************************************
 * *
 *整个代码块的主要目的是用于创建或更新一个已存在的接口。该函数接收多个参数，包括接口列表、父接口ID、接口ID、接口类型、主接口、是否使用IP、IP、DNS、端口、批量传输等。遍历接口列表，查找符合条件的接口。如果找不到符合条件的接口，则创建一个新的接口并添加到接口列表中。如果找到符合条件的接口，则更新接口的类型、主接口、使用IP、IP、DNS、端口和批量传输等属性。最后，更新接口的接口ID。
 ******************************************************************************/
/* 定义一个函数，用于创建或更新一个已存在的接口 */
static void lld_interface_make(zbx_vector_ptr_t *interfaces, zbx_uint64_t parent_interfaceid,
                              zbx_uint64_t interfaceid, unsigned char type, unsigned char main, unsigned char useip,
                              const char *ip, const char *dns, const char *port, unsigned char bulk)
{
	/* 定义一个指向接口的指针 */
	zbx_lld_interface_t *interface = NULL;
	/* 定义一个循环变量 */
	int i;

	/* 遍历接口列表，查找符合条件的接口 */
	for (i = 0; i < interfaces->values_num; i++)
	{
		interface = (zbx_lld_interface_t *)interfaces->values[i];

		/* 如果接口ID不为0，则跳过此次循环 */
		if (0 != interface->interfaceid)
			continue;

		/* 如果接口的父接口ID与传入的parent_interfaceid相等，则找到目标接口，跳出循环 */
		if (interface->parent_interfaceid == parent_interfaceid)
			break;
	}

	/* 如果没有找到符合条件的接口，则创建一个新的接口 */
	if (i == interfaces->values_num)
	{
		/* 分配内存用于存储新的接口结构体 */
		interface = (zbx_lld_interface_t *)zbx_malloc(NULL, sizeof(zbx_lld_interface_t));

		/* 设置接口ID、父接口ID、类型、主接口、是否使用IP、IP、DNS、端口、是否启用批量传输等属性 */
		interface->interfaceid = interfaceid;
		interface->parent_interfaceid = 0;
		interface->type = type;
		interface->main = main;
		interface->useip = 0;
		interface->ip = NULL;
		interface->dns = NULL;
		interface->port = NULL;
		interface->bulk = SNMP_BULK_ENABLED;
		interface->flags = ZBX_FLAG_LLD_INTERFACE_REMOVE;

		/* 将新接口添加到接口列表中 */
		zbx_vector_ptr_append(interfaces, interface);
	}
	else
	{
		/* 更新已存在的接口 */
		/* 判断接口类型是否发生改变，若发生改变则更新接口类型 */
		if (interface->type != type)
		{
			interface->type_orig = type;
			interface->flags |= ZBX_FLAG_LLD_INTERFACE_UPDATE_TYPE;
		}
		/* 判断主接口是否发生改变，若发生改变则更新主接口 */
		if (interface->main != main)
		{
			interface->main_orig = main;
			interface->flags |= ZBX_FLAG_LLD_INTERFACE_UPDATE_MAIN;
		}
		/* 判断是否使用IP是否发生改变，若发生改变则更新使用IP属性 */
		if (interface->useip != useip)
			interface->flags |= ZBX_FLAG_LLD_INTERFACE_UPDATE_USEIP;
		/* 判断IP是否发生改变，若发生改变则更新IP */
		if (0 != strcmp(interface->ip, ip))
			interface->flags |= ZBX_FLAG_LLD_INTERFACE_UPDATE_IP;
		/* 判断DNS是否发生改变，若发生改变则更新DNS */
		if (0 != strcmp(interface->dns, dns))
			interface->flags |= ZBX_FLAG_LLD_INTERFACE_UPDATE_DNS;
		/* 判断端口是否发生改变，若发生改变则更新端口 */
/******************************************************************************
 * *
 *整个代码块的主要目的是遍历主机 vector，为每个主机分配接口 vector，然后根据主机 ID 查询数据库以获取已发现的接口信息。接着，将这些接口与主机关联，并调用 lld_interface_make 函数完成接口创建。最后，释放内存并输出调试信息。
 ******************************************************************************/
/* 静态函数 lld_interfaces_make，用于创建 LLDP 接口并将其与主机关联 */
static void lld_interfaces_make(const zbx_vector_ptr_t *interfaces, zbx_vector_ptr_t *hosts)
{
	/* 定义变量，包括结果变量 result，行变量 row，以及循环变量 i、j 等 */
	int i, j;

	/* 创建一个 uint64 类型的 vector 以存储主机 ID */
	zbx_vector_uint64_create(&hostids);

	/* 遍历主机 vector */
	for (i = 0; i < hosts->values_num; i++)
	{
		/* 获取当前主机 */
		zbx_lld_host_t *host = (zbx_lld_host_t *)hosts->values[i];

		/* 如果主机尚未被发现，跳过 */
		if (0 == (host->flags & ZBX_FLAG_LLD_HOST_DISCOVERED))
			continue;

		/* 为主机分配接口 vector */
		zbx_vector_ptr_reserve(&host->interfaces, interfaces->values_num);

		/* 遍历接口 vector */
		for (j = 0; j < interfaces->values_num; j++)
		{
			/* 获取当前接口 */
			zbx_lld_interface_t *interface = (zbx_lld_interface_t *)interfaces->values[j];

			/* 创建新的接口，并将其添加到主机的接口 vector 中 */
			zbx_lld_interface_t *new_interface = (zbx_lld_interface_t *)zbx_malloc(NULL, sizeof(zbx_lld_interface_t));

			new_interface->interfaceid = 0;
			new_interface->parent_interfaceid = interface->interfaceid;
			new_interface->type = interface->type;
			new_interface->main = interface->main;
			new_interface->useip = interface->useip;
			new_interface->ip = zbx_strdup(NULL, interface->ip);
			new_interface->dns = zbx_strdup(NULL, interface->dns);
			new_interface->port = zbx_strdup(NULL, interface->port);
			new_interface->bulk = interface->bulk;
			new_interface->flags = 0x00;

			zbx_vector_ptr_append(&host->interfaces, new_interface);
		}

		/* 如果主机已分配 ID，将其添加到 hostids vector 中 */
		if (0 != host->hostid)
			zbx_vector_uint64_append(&hostids, host->hostid);
	}

	/* 如果 hostids vector 非空，执行以下操作：
	 * 1. 构建 SQL 查询语句
	 * 2. 执行查询，获取结果
	 * 3. 遍历结果，根据主机 ID 查找主机，并调用 lld_interface_make 函数关联接口
	 */
	if (0 != hostids.values_num)
	{
		char *sql = NULL;
		size_t sql_alloc = 0, sql_offset = 0;

		/* 构建 SQL 查询语句 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hi.hostid,id.parent_interfaceid,hi.interfaceid,hi.type,hi.main,hi.useip,hi.ip,"
					"hi.dns,hi.port,hi.bulk"
				" from interface hi"
					" left join interface_discovery id"
						" on hi.interfaceid=id.interfaceid"
				" where");

		/* 添加条件，过滤出已分配主机 ID 的接口 */
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hi.hostid", hostids.values, hostids.values_num);

		/* 执行查询 */
		result = DBselect("%s", sql);

		/* 释放 SQL 语句内存 */
		zbx_free(sql);

		/* 遍历查询结果，关联主机与接口 */
		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(hostid, row[0]);
			ZBX_DBROW2UINT64(parent_interfaceid, row[1]);
			ZBX_DBROW2UINT64(interfaceid, row[2]);

			/* 查找主机 */
			if (FAIL == zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			/* 关联接口 */
			lld_interface_make(&host->interfaces, parent_interfaceid, interfaceid,
					(unsigned char)atoi(row[3]), (unsigned char)atoi(row[4]),
					(unsigned char)atoi(row[5]), row[6], row[7], row[8],
					(unsigned char)atoi(row[9]));
		}

		/* 释放查询结果 */
		DBfree_result(result);
	}

	/* 释放 hostids vector */
	zbx_vector_uint64_destroy(&hostids);

	/* 输出调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

				" from interface hi"
					" left join interface_discovery id"
						" on hi.interfaceid=id.interfaceid"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hi.hostid", hostids.values, hostids.values_num);

		result = DBselect("%s", sql);

		zbx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(hostid, row[0]);
			ZBX_DBROW2UINT64(parent_interfaceid, row[1]);
			ZBX_DBROW2UINT64(interfaceid, row[2]);

			if (FAIL == (i = zbx_vector_ptr_bsearch(hosts, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

/******************************************************************************
 * 以下是对代码的详细中文注释：
 *
 *
 *
 *这个函数的主要目的是验证和管理Zabbix监控系统中的接口。它检查接口的类型、主接口标志位以及删除接口等操作。如果在验证过程中发现任何问题，它会记录错误信息并更新接口状态。
 *
 *以下是代码的详细注释：
 *
 *1. 定义一个常量，表示函数名。
 *2. 定义一些变量，包括数据库查询结果、行、主机结构体指针、接口结构体指针、接口ID、类型等。
 *3. 打印调试信息，表示函数开始执行。
 *4. 遍历主机结构体数组，获取每个主机上的接口。
 *5. 遍历接口结构体数组，检查接口类型和标志位。
 *6. 如果接口类型为ZBX_FLAG_LLD_INTERFACE_UPDATE_TYPE，则将接口ID添加到interfaceids vector中。
 *7. 如果interfaceids vector不为空，对其进行排序。
 *8. 构建SQL查询语句，查询使用接口的items。
 *9. 执行SQL查询，遍历查询结果。
 *10. 转换接口ID为字符串，遍历主机结构体数组。
 *11. 获取主机上的每个接口，检查接口ID和类型。
 *12. 记录错误信息，表示不能删除接口，因为接口被其他项目使用。
 *13. 处理接口类型和标志位，设置主接口标志位。
 *14. 释放内存，清理vector和字符串。
 *15. 打印调试信息，表示函数执行完毕。
 ******************************************************************************/
static void lld_interfaces_validate(zbx_vector_ptr_t *hosts, char **error)
{
	// 定义一个常量，表示函数名
	const char *__function_name = "lld_interfaces_validate";

	// 定义一些变量
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	zbx_vector_uint64_t	interfaceids;
	zbx_uint64_t		interfaceid;
	zbx_lld_host_t		*host;
	zbx_lld_interface_t	*interface;
	unsigned char		type;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 验证更改后的类型 */

	// 创建一个uint64类型的vector，用于存储接口ID
	zbx_vector_uint64_create(&interfaceids);

	// 遍历hosts中的每个主机
	for (i = 0; i < hosts->values_num; i++)
	{
		// 获取主机结构体指针
		host = (zbx_lld_host_t *)hosts->values[i];

		// 遍历主机上的每个接口
		for (j = 0; j < host->interfaces.values_num; j++)
		{
			// 获取接口结构体指针
			interface = (zbx_lld_interface_t *)host->interfaces.values[j];

			// 如果接口的标志位中没有ZBX_FLAG_LLD_INTERFACE_UPDATE_TYPE，跳过
			if (0 == (interface->flags & ZBX_FLAG_LLD_INTERFACE_UPDATE_TYPE))
				continue;

			// 将接口ID添加到interfaceids中
			zbx_vector_uint64_append(&interfaceids, interface->interfaceid);
		}
	}

	// 如果interfaceids不为空，对其进行排序
	if (0 != interfaceids.values_num)
	{
		zbx_vector_uint64_sort(&interfaceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		// 构建SQL查询语句，查询使用接口的items
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select interfaceid,type from items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "interfaceid",
				interfaceids.values, interfaceids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " group by interfaceid,type");

		// 执行SQL查询
		result = DBselect("%s", sql);

		// 遍历查询结果
		while (NULL != (row = DBfetch(result)))
		{
			// 转换接口ID为字符串
/******************************************************************************
 * 
 ******************************************************************************/
void lld_update_hosts(zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows, char **error, int lifetime,
                     int lastcheck)
{
    // 定义函数名
    const char *__function_name = "lld_update_hosts";

    // 声明变量
    DB_RESULT		result;
    DB_ROW			row;
    zbx_vector_ptr_t	hosts, group_prototypes, groups, interfaces, hostmacros;
    zbx_vector_uint64_t	groupids;		/* 记录要添加的 host 组 */
    zbx_vector_uint64_t	del_hostgroupids;	/* 记录要删除的 host 组 */
    zbx_vector_uint64_t	del_hostmacroids;	/* 记录要删除的 host 宏 */
    zbx_uint64_t		proxy_hostid;
    char			*ipmi_username = NULL, *ipmi_password, *tls_issuer, *tls_subject, *tls_psk_identity,
                        *tls_psk;
    char			ipmi_authtype, inventory_mode;
    unsigned char		ipmi_privilege, tls_connect, tls_accept;

    // 进入函数，记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "Enter %s()", __function_name);

    // 查询数据库，获取 hosts 信息
    result = DBselect(
        "select h.proxy_hostid,h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password,"
            "h.tls_connect,h.tls_accept,h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
        " from hosts h,items i"
        " where h.hostid=i.hostid"
            " and i.itemid=" ZBX_FS_UI64,
        lld_ruleid);

    // 读取数据库数据，存储到 row 变量中
    if (NULL != (row = DBfetch(result)))
    {
        // 解析数据，获取 proxy_hostid、ipmi_authtype、ipmi_privilege、ipmi_username、ipmi_password
        ZBX_DBROW2UINT64(proxy_hostid, row[0]);
        ipmi_authtype = (char)atoi(row[1]);
        ZBX_STR2UCHAR(ipmi_privilege, row[2]);
        ipmi_username = zbx_strdup(NULL, row[3]);
        ipmi_password = zbx_strdup(NULL, row[4]);

        // 获取 tls_connect、tls_accept、tls_issuer、tls_subject、tls_psk_identity、tls_psk
        ZBX_STR2UCHAR(tls_connect, row[5]);
        ZBX_STR2UCHAR(tls_accept, row[6]);
        tls_issuer = zbx_strdup(NULL, row[7]);
        tls_subject = zbx_strdup(NULL, row[8]);
        tls_psk_identity = zbx_strdup(NULL, row[9]);
        tls_psk = zbx_strdup(NULL, row[10]);
    }
    DBfree_result(result);

    // 如果没有找到数据，返回错误信息
    if (NULL == row)
    {
        *error = zbx_strdcatf(*error, "Cannot process host prototypes: a parent host not found.\
");
        return;
    }

    // 初始化 hosts 变量
    zbx_vector_ptr_create(&hosts);
    // 初始化 groupids 变量，记录要添加的 host 组
    zbx_vector_uint64_create(&groupids);
    // 初始化 group_prototypes 变量，记录 host 组原型
    zbx_vector_ptr_create(&group_prototypes);
    // 初始化 groups 变量，记录 host 组
    zbx_vector_ptr_create(&groups);
    // 初始化 del_hostgroupids 变量，记录要删除的 host 组
    zbx_vector_uint64_create(&del_hostgroupids);
    // 初始化 del_hostmacroids 变量，记录要删除的 host 宏
    zbx_vector_uint64_create(&del_hostmacroids);
    // 初始化 interfaces 变量，记录主机接口信息
    zbx_vector_ptr_create(&interfaces);
    // 初始化 hostmacros 变量，记录主机宏信息
    zbx_vector_ptr_create(&hostmacros);

    // 获取 lld_rows 指向的数据，并遍历
    lld_interfaces_get(lld_ruleid, &interfaces);
    lld_hostmacros_get(lld_ruleid, &hostmacros);

    // 根据 ruleid 查询数据库，获取 host 信息
    result = DBselect(
        "select h.hostid,h.host,h.name,h.status,hi.inventory_mode"
        " from hosts h,host_discovery hd"
        " left join host_inventory hi"
        " on hd.hostid=hi.hostid"
        " where h.hostid=hd.hostid"
            " and hd.parent_itemid=" ZBX_FS_UI64,
        lld_ruleid);

    // 遍历查询结果，处理每个 host
    while (NULL != (row = DBfetch(result)))
    {
        // 解析数据，获取 parent_hostid、host_proto、name_proto
        zbx_uint64_t	parent_hostid;
        const char	*host_proto, *name_proto;
        zbx_lld_host_t	*host;
        unsigned char	status;
        int		i;

        // 解析 parent_hostid
        ZBX_STR2UINT64(parent_hostid, row[0]);

        // 解析 host_proto、name_proto
        host_proto = row[1];
        name_proto = row[2];
        status = (unsigned char)atoi(row[3]);

        // 如果有错误，返回错误信息
        if (SUCCEED == DBis_null(row[4]))
            inventory_mode = HOST_INVENTORY_DISABLED;
        else
            inventory_mode = (char)atoi(row[4]);

        // 获取 host 信息，并添加到 hosts 变量中
        lld_hosts_get(parent_hostid, &hosts, proxy_hostid, ipmi_authtype, ipmi_privilege, ipmi_username,
                     ipmi_password, tls_connect, tls_accept, tls_issuer, tls_subject,
                     tls_psk_identity, tls_psk);

        // 获取 host 组信息，并添加到 groups 变量中
        lld_simple_groups_get(parent_hostid, &group_prototypes, &groups);

        // 处理 host 组
        for (i = 0; i < lld_rows->values_num; i++)
        {
            const zbx_lld_row_t	*lld_row = (zbx_lld_row_t *)lld_rows->values[i];

            host = lld_host_make(&hosts, host_proto, name_proto, &lld_row->jp_row);
            lld_groups_make(host, &groups, &group_prototypes, &lld_row->jp_row);
        }

        // 排序 hosts 变量
        zbx_vector_ptr_sort(&hosts, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

        // 验证 host 组和 host 是否合法
        lld_groups_validate(&groups, error);
        lld_hosts_validate(&hosts, error);

        // 处理主机接口信息
        lld_interfaces_make(&interfaces, &hosts);
        // 处理主机宏信息
        lld_hostmacros_make(&hostmacros, &hosts, &del_hostmacroids);

        // 保存 host 组和 host 信息
        lld_groups_save(&groups, &group_prototypes);
        lld_hosts_save(parent_hostid, &hosts, host_proto, proxy_hostid, ipmi_authtype, ipmi_privilege,
                       ipmi_username, ipmi_password, status, inventory_mode, tls_connect, tls_accept,
                       tls_issuer, tls_subject, tls_psk_identity, tls_psk, &del_hostgroupids,
                       &del_hostmacroids);

        /* 链接模板 */
        lld_templates_link(&hosts, error);

        // 删除已处理的 host 组和 host 信息
        lld_hosts_remove(&hosts, lifetime, lastcheck);
        lld_groups_remove(&groups

				{
					interface = (zbx_lld_interface_t *)host->interfaces.values[j];

					if (0 == (interface->flags & ZBX_FLAG_LLD_INTERFACE_REMOVE))
						continue;

					if (interface->interfaceid != interfaceid)
						continue;

					*error = zbx_strdcatf(*error, "Cannot delete \"%s\" interface on host \"%s\":"
							" the interface is used by items.\n",
							zbx_interface_type_string(interface->type), host->host);

					/* drop the correspond flag */
					interface->flags &= ~ZBX_FLAG_LLD_INTERFACE_REMOVE;

					if (SUCCEED == another_main_interface_exists(&host->interfaces, interface))
					{
						if (1 == interface->main)
						{
							/* drop main flag */
							interface->main_orig = interface->main;
							interface->main = 0;
							interface->flags |= ZBX_FLAG_LLD_INTERFACE_UPDATE_MAIN;
						}
					}
					else if (1 != interface->main)
					{
						/* set main flag */
						interface->main_orig = interface->main;
						interface->main = 1;
						interface->flags |= ZBX_FLAG_LLD_INTERFACE_UPDATE_MAIN;
					}
				}
			}
		}
		DBfree_result(result);
	}

	zbx_vector_uint64_destroy(&interfaceids);

	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_update_hosts                                                 *
 *                                                                            *
 * Purpose: add or update low-level discovered hosts                          *
 *                                                                            *
 ******************************************************************************/
void	lld_update_hosts(zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows, char **error, int lifetime,
		int lastcheck)
{
	const char		*__function_name = "lld_update_hosts";

	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_ptr_t	hosts, group_prototypes, groups, interfaces, hostmacros;
	zbx_vector_uint64_t	groupids;		/* list of host groups which should be added */
	zbx_vector_uint64_t	del_hostgroupids;	/* list of host groups which should be deleted */
	zbx_vector_uint64_t	del_hostmacroids;	/* list of host macros which should be deleted */
	zbx_uint64_t		proxy_hostid;
	char			*ipmi_username = NULL, *ipmi_password, *tls_issuer, *tls_subject, *tls_psk_identity,
				*tls_psk;
	char			ipmi_authtype, inventory_mode;
	unsigned char		ipmi_privilege, tls_connect, tls_accept;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	result = DBselect(
			"select h.proxy_hostid,h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password,"
				"h.tls_connect,h.tls_accept,h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
			" from hosts h,items i"
			" where h.hostid=i.hostid"
				" and i.itemid=" ZBX_FS_UI64,
			lld_ruleid);

	if (NULL != (row = DBfetch(result)))
	{
		ZBX_DBROW2UINT64(proxy_hostid, row[0]);
		ipmi_authtype = (char)atoi(row[1]);
		ZBX_STR2UCHAR(ipmi_privilege, row[2]);
		ipmi_username = zbx_strdup(NULL, row[3]);
		ipmi_password = zbx_strdup(NULL, row[4]);

		ZBX_STR2UCHAR(tls_connect, row[5]);
		ZBX_STR2UCHAR(tls_accept, row[6]);
		tls_issuer = zbx_strdup(NULL, row[7]);
		tls_subject = zbx_strdup(NULL, row[8]);
		tls_psk_identity = zbx_strdup(NULL, row[9]);
		tls_psk = zbx_strdup(NULL, row[10]);
	}
	DBfree_result(result);

	if (NULL == row)
	{
		*error = zbx_strdcatf(*error, "Cannot process host prototypes: a parent host not found.\n");
		return;
	}

	zbx_vector_ptr_create(&hosts);
	zbx_vector_uint64_create(&groupids);
	zbx_vector_ptr_create(&group_prototypes);
	zbx_vector_ptr_create(&groups);
	zbx_vector_uint64_create(&del_hostgroupids);
	zbx_vector_uint64_create(&del_hostmacroids);
	zbx_vector_ptr_create(&interfaces);
	zbx_vector_ptr_create(&hostmacros);

	lld_interfaces_get(lld_ruleid, &interfaces);
	lld_hostmacros_get(lld_ruleid, &hostmacros);

	result = DBselect(
			"select h.hostid,h.host,h.name,h.status,hi.inventory_mode"
			" from hosts h,host_discovery hd"
				" left join host_inventory hi"
					" on hd.hostid=hi.hostid"
			" where h.hostid=hd.hostid"
				" and hd.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	parent_hostid;
		const char	*host_proto, *name_proto;
		zbx_lld_host_t	*host;
		unsigned char	status;
		int		i;

		ZBX_STR2UINT64(parent_hostid, row[0]);
		host_proto = row[1];
		name_proto = row[2];
		status = (unsigned char)atoi(row[3]);
		if (SUCCEED == DBis_null(row[4]))
			inventory_mode = HOST_INVENTORY_DISABLED;
		else
			inventory_mode = (char)atoi(row[4]);

		lld_hosts_get(parent_hostid, &hosts, proxy_hostid, ipmi_authtype, ipmi_privilege, ipmi_username,
				ipmi_password, tls_connect, tls_accept, tls_issuer, tls_subject,
				tls_psk_identity, tls_psk);

		lld_simple_groups_get(parent_hostid, &groupids);

		lld_group_prototypes_get(parent_hostid, &group_prototypes);
		lld_groups_get(parent_hostid, &groups);

		for (i = 0; i < lld_rows->values_num; i++)
		{
			const zbx_lld_row_t	*lld_row = (zbx_lld_row_t *)lld_rows->values[i];

			host = lld_host_make(&hosts, host_proto, name_proto, &lld_row->jp_row);
			lld_groups_make(host, &groups, &group_prototypes, &lld_row->jp_row);
		}

		zbx_vector_ptr_sort(&hosts, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		lld_groups_validate(&groups, error);
		lld_hosts_validate(&hosts, error);

		lld_interfaces_make(&interfaces, &hosts);
		lld_interfaces_validate(&hosts, error);

		lld_hostgroups_make(&groupids, &hosts, &groups, &del_hostgroupids);
		lld_templates_make(parent_hostid, &hosts);
		lld_hostmacros_make(&hostmacros, &hosts, &del_hostmacroids);

		lld_groups_save(&groups, &group_prototypes);
		lld_hosts_save(parent_hostid, &hosts, host_proto, proxy_hostid, ipmi_authtype, ipmi_privilege,
				ipmi_username, ipmi_password, status, inventory_mode, tls_connect, tls_accept,
				tls_issuer, tls_subject, tls_psk_identity, tls_psk, &del_hostgroupids,
				&del_hostmacroids);

		/* linking of the templates */
		lld_templates_link(&hosts, error);

		lld_hosts_remove(&hosts, lifetime, lastcheck);
		lld_groups_remove(&groups, lifetime, lastcheck);

		zbx_vector_ptr_clear_ext(&groups, (zbx_clean_func_t)lld_group_free);
		zbx_vector_ptr_clear_ext(&group_prototypes, (zbx_clean_func_t)lld_group_prototype_free);
		zbx_vector_ptr_clear_ext(&hosts, (zbx_clean_func_t)lld_host_free);

		zbx_vector_uint64_clear(&groupids);
		zbx_vector_uint64_clear(&del_hostgroupids);
		zbx_vector_uint64_clear(&del_hostmacroids);
	}
	DBfree_result(result);

	zbx_vector_ptr_clear_ext(&hostmacros, (zbx_clean_func_t)lld_hostmacro_free);
	zbx_vector_ptr_clear_ext(&interfaces, (zbx_clean_func_t)lld_interface_free);

	zbx_vector_ptr_destroy(&hostmacros);
	zbx_vector_ptr_destroy(&interfaces);
	zbx_vector_uint64_destroy(&del_hostmacroids);
	zbx_vector_uint64_destroy(&del_hostgroupids);
	zbx_vector_ptr_destroy(&groups);
	zbx_vector_ptr_destroy(&group_prototypes);
	zbx_vector_uint64_destroy(&groupids);
	zbx_vector_ptr_destroy(&hosts);

	zbx_free(tls_psk);
	zbx_free(tls_psk_identity);
	zbx_free(tls_subject);
	zbx_free(tls_issuer);
	zbx_free(ipmi_password);
	zbx_free(ipmi_username);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
