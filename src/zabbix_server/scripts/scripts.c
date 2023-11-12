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
#include "../poller/checks_agent.h"
#include "../ipmi/ipmi.h"
#include "../poller/checks_ssh.h"
#include "../poller/checks_telnet.h"
#include "zbxexec.h"
#include "zbxserver.h"
#include "db.h"
#include "log.h"
#include "zbxtasks.h"
#include "scripts.h"

extern int	CONFIG_TRAPPER_TIMEOUT;

/******************************************************************************
 * *
 *整个代码块的主要目的是在指定的主机上执行一个命令，并获取执行结果。函数接收5个参数，分别是主机信息、命令字符串、执行结果指针、错误信息字符串和错误信息字符串的最大长度。函数首先获取主机对应的代理接口，然后获取接口的端口号并替换端口号中的宏。接下来，给命令字符串添加引号，并构建键值对。然后执行命令并获取结果，最后释放资源并返回执行结果。如果在执行过程中出现错误，函数会打印错误信息并返回失败。
 ******************************************************************************/
// 定义一个静态函数zbx_execute_script_on_agent，接收5个参数：
// 1. const DC_HOST *host：主机信息指针
// 2. const char *command：要执行的命令字符串
// 3. char **result：存储执行结果的指针
// 4. char *error：存储错误信息的字符串
// 5. size_t max_error_len：error字符串的最大长度
static int zbx_execute_script_on_agent(const DC_HOST *host, const char *command, char **result, char *error, size_t max_error_len)
{
	// 定义一些常量和变量
	const char *__function_name = "zbx_execute_script_on_agent";
	int ret;
	AGENT_RESULT agent_result;
	char *param = NULL, *port = NULL;
	DC_ITEM item;

	// 打印调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化错误字符串和主机信息
	*error = '\0';
	memset(&item, 0, sizeof(item));
	memcpy(&item.host, host, sizeof(item.host));

	// 获取主机对应的代理接口
	if (SUCCEED != (ret = DCconfig_get_interface_by_type(&item.interface, host->hostid, INTERFACE_TYPE_AGENT)))
	{
		// 处理错误情况
		zbx_snprintf(error, max_error_len, "Zabbix agent interface is not defined for host [%s]", host->host);
		goto fail;
	}

	// 获取接口的端口号
	port = zbx_strdup(port, item.interface.port_orig);
	// 替换端口号中的宏
	substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
			&port, MACRO_TYPE_COMMON, NULL, 0);

	// 检查端口号是否合法
	if (SUCCEED != (ret = is_ushort(port, &item.interface.port)))
	{
		// 处理错误情况
		zbx_snprintf(error, max_error_len, "Invalid port number [%s]", item.interface.port_orig);
		goto fail;
	}

	// 复制命令字符串
	param = zbx_strdup(param, command);
	// 给参数添加引号
	if (SUCCEED != (ret = quote_key_param(&param, 0)))
	{
		// 处理错误情况
		zbx_snprintf(error, max_error_len, "Invalid param [%s]", param);
		goto fail;
	}

	// 构建键值对
	item.key = zbx_dsprintf(item.key, "system.run[%s,%s]", param, NULL == result ? "nowait" : "wait");
	item.value_type = ITEM_VALUE_TYPE_TEXT;

	// 初始化结果结构体
	init_result(&agent_result);

	// 开启报警器
	zbx_alarm_on(CONFIG_TIMEOUT);

/******************************************************************************
 * *
 *整个代码块的主要目的是在终端上执行Zabbix代理脚本。代码首先根据脚本类型（SSH或TELNET）设置相应的参数，然后获取接口信息。接着，构建item.key，并根据脚本类型调用不同的获取值方法。在执行过程中，如果出现错误，记录错误信息并返回失败。否则，将执行结果的文本拷贝给result，并释放相关资源。最后，返回执行结果。
 ******************************************************************************/
// 定义一个静态函数，用于在终端上执行Zabbix代理脚本
static int zbx_execute_script_on_terminal(const DC_HOST *host, const zbx_script_t *script, char **result,
                                         char *error, size_t max_error_len)
{
    // 定义一个常量，表示函数名
    const char *__function_name = "zbx_execute_script_on_terminal";

    // 定义一些变量
    int ret = FAIL, i;
    AGENT_RESULT agent_result;
    DC_ITEM item;
    int             (*function)(DC_ITEM *, AGENT_RESULT *);

    // 判断脚本类型，如果是SSH或TELNET类型
    #if defined(HAVE_SSH2) || defined(HAVE_SSH)
        assert(ZBX_SCRIPT_TYPE_SSH == script->type || ZBX_SCRIPT_TYPE_TELNET == script->type);
    #else
        assert(ZBX_SCRIPT_TYPE_TELNET == script->type);
    #endif

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 初始化错误字符串为空
    *error = '\0';
    // 初始化DC_ITEM结构体
    memset(&item, 0, sizeof(item));
    // 拷贝主机信息
    memcpy(&item.host, host, sizeof(item.host));

    // 循环获取接口类型
    for (i = 0; INTERFACE_TYPE_COUNT > i; i++)
    {
        // 获取接口失败，跳出循环
        if (SUCCEED == (ret = DCconfig_get_interface_by_type(&item.interface, host->hostid,
                                                            INTERFACE_TYPE_PRIORITY[i])))
        {
            break;
        }
    }

    // 如果没有获取到接口，返回失败
    if (FAIL == ret)
    {
        zbx_snprintf(error, max_error_len, "No interface defined for host [%s]", host->host);
        goto fail;
    }

    // 切换脚本类型执行
    switch (script->type)
    {
        case ZBX_SCRIPT_TYPE_SSH:
            // 设置SSH相关参数
            item.authtype = script->authtype;
            item.publickey = script->publickey;
            item.privatekey = script->privatekey;
            //  fallthru，继续执行下一个case
        case ZBX_SCRIPT_TYPE_TELNET:
            // 设置TELNET相关参数
            item.username = script->username;
            item.password = script->password;
            break;
    }

    // 构建item.key
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
    if (ZBX_SCRIPT_TYPE_SSH == script->type)
    {
        item.key = zbx_dsprintf(item.key, "ssh.run[,,%s]", script->port);
        // 获取值的方法
        function = get_value_ssh;
    }
    else
    {
#endif
        item.key = zbx_dsprintf(item.key, "telnet.run[,,%s]", script->port);
        // 获取值的方法
        function = get_value_telnet;
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
    }
#endif
    // 设置item的值类型为文本
    item.value_type = ITEM_VALUE_TYPE_TEXT;
    // 设置item的参数
    item.params = zbx_strdup(item.params, script->command);

    // 初始化结果结构体
    init_result(&agent_result);

    // 开启超时报警
    zbx_alarm_on(CONFIG_TIMEOUT);

    // 执行获取值的方法
    if (SUCCEED != (ret = function(&item, &agent_result)))
    {
        // 如果结果中有错误信息
        if (ISSET_MSG(&agent_result))
            zbx_strlcpy(error, agent_result.msg, max_error_len);
        // 返回失败
        ret = FAIL;
    }
    else if (NULL != result && ISSET_TEXT(&agent_result))
    {
        // 拷贝结果文本
        *result = zbx_strdup(*result, agent_result.text);
    }

    // 关闭超时报警
    zbx_alarm_off();

    // 释放资源
    free_result(&agent_result);

    // 释放item占用的内存
    zbx_free(item.params);
    zbx_free(item.key);

fail:
    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回结果
    return ret;
}

#endif
		item.key = zbx_dsprintf(item.key, "telnet.run[,,%s]", script->port);
		function = get_value_telnet;
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
	}
#endif
	item.value_type = ITEM_VALUE_TYPE_TEXT;
	item.params = zbx_strdup(item.params, script->command);

	init_result(&agent_result);

	zbx_alarm_on(CONFIG_TIMEOUT);

	if (SUCCEED != (ret = function(&item, &agent_result)))
	{
		if (ISSET_MSG(&agent_result))
			zbx_strlcpy(error, agent_result.msg, max_error_len);
		ret = FAIL;
	}
	else if (NULL != result && ISSET_TEXT(&agent_result))
		*result = zbx_strdup(*result, agent_result.text);

	zbx_alarm_off();

	free_result(&agent_result);

	zbx_free(item.params);
	zbx_free(item.key);
fail:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是通过给定的scriptid从数据库中查询对应的script信息，并将查询结果中的信息存储到传入的zbx_script_t结构体和zbx_uint64_t类型的groupid变量中。如果查询成功，返回SUCCEED，否则返回FAIL。在整个过程中，还对函数执行过程进行了日志记录。
 ******************************************************************************/
// 定义一个静态函数，用于通过scriptid获取对应的script信息
static int	DBget_script_by_scriptid(zbx_uint64_t scriptid, zbx_script_t *script, zbx_uint64_t *groupid)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "DBget_script_by_scriptid";

	// 声明一个DB_RESULT类型的变量result，用于存储数据库操作的结果
	DB_RESULT	result;
	// 声明一个DB_ROW类型的变量row，用于存储数据库查询的每一行数据
	DB_ROW		row;
	// 定义一个整型变量ret，初始值为FAIL，用于存储函数执行结果
	int		ret = FAIL;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 执行数据库查询，从scripts表中获取指定scriptid的script信息
	result = DBselect(
			"select type,execute_on,command,groupid,host_access"
			" from scripts"
			" where scriptid=" ZBX_FS_UI64,
			scriptid);

	// 如果查询结果不为空，即找到了对应的script信息
	if (NULL != (row = DBfetch(result)))
	{
		// 将查询结果中的type字段转换为unsigned char类型，存储到script结构体的type成员变量中
		ZBX_STR2UCHAR(script->type, row[0]);
		// 将查询结果中的execute_on字段转换为unsigned char类型，存储到script结构体的execute_on成员变量中
		ZBX_STR2UCHAR(script->execute_on, row[1]);
		// 将查询结果中的command字段复制到script结构体的command成员变量中
		script->command = zbx_strdup(script->command, row[2]);
		// 将查询结果中的groupid字段转换为uint64类型，存储到groupid指针所指向的变量中
		ZBX_DBROW2UINT64(*groupid, row[3]);
		// 将查询结果中的host_access字段转换为unsigned char类型，存储到script结构体的host_access成员变量中
		ZBX_STR2UCHAR(script->host_access, row[4]);
		// 更新ret变量值为SUCCEED，表示获取script信息成功
		ret = SUCCEED;
	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回ret变量值，表示函数执行结果
	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是检查用户组和主机之间的权限。函数`check_script_permissions`接收两个参数：用户组ID和主机ID，然后执行一系列操作，包括创建vector、获取分组内的主机ID列表、分配并拼接SQL语句、添加查询条件、执行SQL查询等。最后，根据查询结果判断权限是否满足要求，如果满足，返回成功，否则返回失败。
 ******************************************************************************/
/*
 * 函数名：check_script_permissions
 * 功能：检查脚本权限
 * 参数：
 *   zbx_uint64_t groupid：用户组ID
 *   zbx_uint64_t hostid：主机ID
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	check_script_permissions(zbx_uint64_t groupid, zbx_uint64_t hostid)
{
	/* 定义函数名 */
	const char		*__function_name = "check_script_permissions";

	/* 定义变量 */
	DB_RESULT		result;
	int			ret = SUCCEED;
	zbx_vector_uint64_t	groupids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() groupid:" ZBX_FS_UI64 " hostid:" ZBX_FS_UI64,
			__function_name, groupid, hostid);

	/* 判断groupid是否为0，如果是，直接退出 */
	if (0 == groupid)
		goto exit;

	/* 创建一个uint64类型的 vector */
	zbx_vector_uint64_create(&groupids);

	/* 获取分组内的主机ID列表 */
	zbx_dc_get_nested_hostgroupids(&groupid, 1, &groupids);

	/* 分配并拼接SQL语句 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select hostid"
			" from hosts_groups"
			" where hostid=" ZBX_FS_UI64
				" and",
			hostid);

	/* 添加查询条件：分组ID */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids.values,
			groupids.values_num);

	/* 执行SQL查询 */
	result = DBselect("%s", sql);

	/* 释放SQL语句内存 */
	zbx_free(sql);

	/* 销毁vector */
	zbx_vector_uint64_destroy(&groupids);

	/* 判断查询结果是否为空，如果是，返回失败 */
	if (NULL == DBfetch(result))
		ret = FAIL;

	/* 释放查询结果 */
	DBfree_result(result);

exit:
	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	/* 返回结果 */
	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是检查用户是否有权限执行指定的主机和脚本。函数check_user_permissions接收三个参数，分别是用户ID、主机对象和脚本对象。函数首先记录日志，显示调用函数的参数。然后执行一个数据库查询，查询主机分组及其权限信息。查询结果中，筛选出最小权限大于PERM_DENY且最大权限大于等于script->host_access的记录。如果查询结果为空，说明查询失败，返回FAIL。最后，记录函数执行结果的日志，并返回函数执行结果。
 ******************************************************************************/
// 定义一个静态函数check_user_permissions，接收三个参数：userid（用户ID），host（主机对象），script（脚本对象）
static int check_user_permissions(zbx_uint64_t userid, const DC_HOST *host, zbx_script_t *script)
{
	// 定义一个局部字符串变量，存储函数名
	const char *__function_name = "check_user_permissions";

	// 定义一个整型变量，存储函数返回值
	int ret = SUCCEED;

	// 定义一个DB_RESULT类型的变量，用于存储数据库查询结果
	DB_RESULT result;

	// 定义一个DB_ROW类型的变量，用于存储数据库查询的一行数据
	DB_ROW row;

	// 记录日志，显示调用函数的 userid、hostid 和 scriptid
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() userid:%s hostid:%s scriptid:%s", __function_name, userid, host->hostid, script->scriptid);

	// 执行数据库查询，查询主机分组及其权限信息
	result = DBselect(
		"select null"
			" from hosts_groups hg,rights r,users_groups ug"
		" where hg.groupid=r.id"
			" and r.groupid=ug.usrgrpid"
			" and hg.hostid=" ZBX_FS_UI64
			" and ug.userid=" ZBX_FS_UI64
		" group by hg.hostid"
		" having min(r.permission)>%d"
			" and max(r.permission)>=%d",
		host->hostid,
		userid,
		PERM_DENY,
		script->host_access);

	// 检查查询结果是否为空，如果为空，说明查询失败，返回FAIL
/******************************************************************************
 * *
 *这个代码块的主要目的是对传入的zbx_script_t结构体进行预处理，根据脚本类型执行相应的操作，包括替换宏、检查端口、用户权限等。最后判断脚本是否准备成功，返回0表示成功，非0表示失败。
 ******************************************************************************/
// 定义函数名和函数属性
int zbx_script_prepare(zbx_script_t *script, const DC_HOST *host, const zbx_user_t *user, char *error, size_t max_error_len)
{
	// 定义日志级别
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化返回值和变量
	int		ret = FAIL;
	zbx_uint64_t	groupid;

	// 根据脚本类型进行不同操作
	switch (script->type)
	{
		case ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT:
			// 将Windows风格的换行符（CR+LF）转换为Unix风格的换行符（LF）
			dos2unix(script->command);
			break;
		case ZBX_SCRIPT_TYPE_SSH:
			// 替换常用宏，包括主机ID、公钥、私钥等
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->publickey, MACRO_TYPE_COMMON, NULL, 0);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->privatekey, MACRO_TYPE_COMMON, NULL, 0);
			// 切换到TELNET类型，继续处理
		case ZBX_SCRIPT_TYPE_TELNET:
			// 替换常用宏，包括主机ID、端口、用户名、密码等
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->port, MACRO_TYPE_COMMON, NULL, 0);

			// 检查端口是否合法
			if ('\0' != *script->port && SUCCEED != (ret = is_ushort(script->port, NULL)))
			{
				zbx_snprintf(error, max_error_len, "Invalid port number \"%s\"", script->port);
				goto out;
			}

			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->username, MACRO_TYPE_COMMON, NULL, 0);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->password, MACRO_TYPE_COMMON, NULL, 0);
			break;
		case ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT:
			// 查询全局脚本的组ID
			if (SUCCEED != DBget_script_by_scriptid(script->scriptid, script, &groupid))
			{
				zbx_strlcpy(error, "Unknown script identifier.", max_error_len);
				goto out;
			}

			// 检查组ID是否大于0，以及脚本是否具有执行权限
			if (groupid > 0 && SUCCEED != check_script_permissions(groupid, host->hostid))
			{
				zbx_strlcpy(error, "Script does not have permission to be executed on the host.",
						max_error_len);
				goto out;
			}

			// 检查用户权限
			if (user != NULL && USER_TYPE_SUPER_ADMIN != user->type &&
				SUCCEED != check_user_permissions(user->userid, host, script))
			{
				zbx_strlcpy(error, "User does not have permission to execute this script on the host.",
						max_error_len);
				goto out;
			}

			// 替换简单宏，包括主机ID、脚本ID等
			if (SUCCEED != substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL,
					NULL, &script->command, MACRO_TYPE_SCRIPT, error, max_error_len))
			{
				goto out;
			}

			// 递归调用自身，处理全局脚本
			if (ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT == script->type)
			{
				THIS_SHOULD_NEVER_HAPPEN;
				goto out;
			}

			// 限制递归深度
			if (FAIL == zbx_script_prepare(script, host, user, error, max_error_len))
				goto out;

			break;
		case ZBX_SCRIPT_TYPE_IPMI:
			// 暂不处理IPMI类型脚本
			break;
		default:
			// 非法脚本类型，输出错误信息
			zbx_snprintf(error, max_error_len, "Invalid command type \"%d\".", (int)script->type);
			goto out;
	}

	// 设置返回值
	ret = SUCCEED;

out:
	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
	return ret;
}


	int		ret = FAIL;
	zbx_uint64_t	groupid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	switch (script->type)
	{
		case ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT:
			dos2unix(script->command);	/* CR+LF (Windows) => LF (Unix) */
			break;
		case ZBX_SCRIPT_TYPE_SSH:
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->publickey, MACRO_TYPE_COMMON, NULL, 0);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->privatekey, MACRO_TYPE_COMMON, NULL, 0);
			ZBX_FALLTHROUGH;
		case ZBX_SCRIPT_TYPE_TELNET:
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->port, MACRO_TYPE_COMMON, NULL, 0);

			if ('\0' != *script->port && SUCCEED != (ret = is_ushort(script->port, NULL)))
			{
				zbx_snprintf(error, max_error_len, "Invalid port number \"%s\"", script->port);
				goto out;
			}

			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->username, MACRO_TYPE_COMMON, NULL, 0);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->password, MACRO_TYPE_COMMON, NULL, 0);
			break;
		case ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT:
			if (SUCCEED != DBget_script_by_scriptid(script->scriptid, script, &groupid))
			{
				zbx_strlcpy(error, "Unknown script identifier.", max_error_len);
				goto out;
			}
			if (groupid > 0 && SUCCEED != check_script_permissions(groupid, host->hostid))
			{
				zbx_strlcpy(error, "Script does not have permission to be executed on the host.",
						max_error_len);
				goto out;
			}
			if (user != NULL && USER_TYPE_SUPER_ADMIN != user->type &&
				SUCCEED != check_user_permissions(user->userid, host, script))
			{
				zbx_strlcpy(error, "User does not have permission to execute this script on the host.",
						max_error_len);
				goto out;
			}

			if (SUCCEED != substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL,
					NULL, &script->command, MACRO_TYPE_SCRIPT, error, max_error_len))
			{
				goto out;
			}

			/* DBget_script_by_scriptid() may overwrite script type with anything but global script... */
			if (ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT == script->type)
			{
				THIS_SHOULD_NEVER_HAPPEN;
/******************************************************************************
 * *
 *该代码块的主要目的是定义一个名为`zbx_script_execute`的函数，该函数根据传入的脚本类型和执行目标，执行相应的脚本并在执行完成后返回结果。具体来说，该函数执行以下操作：
 *
 *1. 判断脚本类型和执行目标；
 *2. 根据脚本类型和执行目标，调用相应的函数执行脚本；
 *3. 如果在执行过程中出现错误，设置错误信息；
 *4. 执行完成后，返回结果。
 ******************************************************************************/
// 定义一个函数，用于执行zbx脚本
int zbx_script_execute(const zbx_script_t *script, const DC_HOST *host, char **result, char *error, size_t max_error_len)
{
	// 定义一个日志标签
	const char *__function_name = "zbx_script_execute";

	// 初始化返回值
	int		ret = FAIL;

	// 记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 清空错误信息
	*error = '\0';

	// 根据脚本类型进行切换
	switch (script->type)
	{
		// 如果是自定义脚本，且在代理上执行
		case ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT:
			switch (script->execute_on)
			{
				case ZBX_SCRIPT_EXECUTE_ON_AGENT:
					// 调用另一个函数，在代理上执行脚本
					ret = zbx_execute_script_on_agent(host, script->command, result, error, max_error_len);
					break;
				// 如果是自定义脚本，且在服务器或代理上执行
				case ZBX_SCRIPT_EXECUTE_ON_SERVER:
				case ZBX_SCRIPT_EXECUTE_ON_PROXY:
					// 调用另一个函数，在服务器或代理上执行脚本
					ret = zbx_execute(script->command, result, error, max_error_len, CONFIG_TRAPPER_TIMEOUT, ZBX_EXIT_CODE_CHECKS_ENABLED);
					break;
				// 如果是无效的执行选项
				default:
					// 拼接错误信息
					zbx_snprintf(error, max_error_len, "Invalid 'Execute on' option \"%d\".", (int)script->execute_on);
			}
			break;
		// 如果是IPMI脚本
		case ZBX_SCRIPT_TYPE_IPMI:
#ifdef HAVE_OPENIPMI
			// 如果成功执行IPMI命令，则设置结果
			if (SUCCEED == (ret = zbx_ipmi_execute_command(host, script->command, error, max_error_len)))
			{
				// 如果result不为空，设置结果字符串
				if (NULL != result)
					*result = zbx_strdup(*result, "IPMI command successfully executed.");
			}
#else
			// 如果没有编译支持IPMI命令，则设置错误信息
			zbx_strlcpy(error, "Support for IPMI commands was not compiled in.", max_error_len);
#endif
			break;
		// 如果是SSH脚本
		case ZBX_SCRIPT_TYPE_SSH:
#if !defined(HAVE_SSH2) && !defined(HAVE_SSH)
			// 如果没有编译支持SSH脚本，则设置错误信息
			zbx_strlcpy(error, "Support for SSH script was not compiled in.", max_error_len);
#endif
			break;
		// 如果是TELNET脚本
		case ZBX_SCRIPT_TYPE_TELNET:
			// 在终端上执行脚本，并返回结果
			ret = zbx_execute_script_on_terminal(host, script, result, error, max_error_len);
			break;
		// 如果是其他类型的脚本
		default:
			// 拼接错误信息
			zbx_snprintf(error, max_error_len, "Invalid command type \"%d\".", (int)script->type);
	}

	// 如果执行失败且result不为空，设置结果字符串
	if (SUCCEED != ret && NULL != result)
		*result = zbx_strdup(*result, "");

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回执行结果
	return ret;
}

			}
			break;
		case ZBX_SCRIPT_TYPE_IPMI:
#ifdef HAVE_OPENIPMI
			if (SUCCEED == (ret = zbx_ipmi_execute_command(host, script->command, error, max_error_len)))
			{
				if (NULL != result)
					*result = zbx_strdup(*result, "IPMI command successfully executed.");
			}
#else
			zbx_strlcpy(error, "Support for IPMI commands was not compiled in.", max_error_len);
#endif
			break;
		case ZBX_SCRIPT_TYPE_SSH:
#if !defined(HAVE_SSH2) && !defined(HAVE_SSH)
			zbx_strlcpy(error, "Support for SSH script was not compiled in.", max_error_len);
			break;
#endif
		case ZBX_SCRIPT_TYPE_TELNET:
			ret = zbx_execute_script_on_terminal(host, script, result, error, max_error_len);
			break;
		default:
			zbx_snprintf(error, max_error_len, "Invalid command type \"%d\".", (int)script->type);
	}

	if (SUCCEED != ret && NULL != result)
		*result = zbx_strdup(*result, "");

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_script_create_task                                           *
 *                                                                            *
 * Purpose: creates remote command task from a script                         *
 *                                                                            *
 * Return value:  the identifier of the created task or 0 in the case of      *
 *                error                                                       *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	zbx_script_create_task(const zbx_script_t *script, const DC_HOST *host, zbx_uint64_t alertid, int now)
{
	zbx_tm_task_t	*task;
	unsigned short	port;
	zbx_uint64_t	taskid;

	if (NULL != script->port && '\0' != script->port[0])
		is_ushort(script->port, &port);
	else
		port = 0;

	taskid = DBget_maxid("task");

	task = zbx_tm_task_create(taskid, ZBX_TM_TASK_REMOTE_COMMAND, ZBX_TM_STATUS_NEW, now,
			ZBX_REMOTE_COMMAND_TTL, host->proxy_hostid);

	task->data = zbx_tm_remote_command_create(script->type, script->command, script->execute_on, port,
			script->authtype, script->username, script->password, script->publickey, script->privatekey,
			taskid, host->hostid, alertid);

	DBbegin();

	if (FAIL == zbx_tm_save_task(task))
		taskid = 0;

	DBcommit();

	zbx_tm_task_free(task);

	return taskid;
}
