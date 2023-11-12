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
#include "daemon.h"
#include "zbxserver.h"
#include "zbxself.h"
#include "zbxtasks.h"

#include "escalator.h"
#include "../operations.h"
#include "../actions.h"
#include "../events.h"
#include "../scripts/scripts.h"
#include "../../libs/zbxcrypto/tls.h"
#include "comms.h"

extern int	CONFIG_ESCALATOR_FORKS;

#define CONFIG_ESCALATOR_FREQUENCY	3

#define ZBX_ESCALATION_SOURCE_DEFAULT	0
#define ZBX_ESCALATION_SOURCE_ITEM	1
#define ZBX_ESCALATION_SOURCE_TRIGGER	2

#define ZBX_ESCALATION_CANCEL		0
#define ZBX_ESCALATION_DELETE		1
#define ZBX_ESCALATION_SKIP		2
#define ZBX_ESCALATION_PROCESS		3
#define ZBX_ESCALATION_SUPPRESS		4

#define ZBX_ESCALATIONS_PER_STEP	1000

typedef struct
{
	zbx_uint64_t	userid;
	zbx_uint64_t	mediatypeid;
	zbx_uint64_t	ackid;
	char		*subject;
	char		*message;
	void		*next;
}
ZBX_USER_MSG;

typedef struct
{
	zbx_uint64_t	hostgroupid;
	char		*tag;
	char		*value;
}
zbx_tag_filter_t;

/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx_tag_filter_t类型结构体所占用的内存空间。这个函数接收一个zbx_tag_filter_t类型的指针作为参数，依次释放该指针指向的tag、value以及tag_filter本身所占用的内存空间。
 ******************************************************************************/
// 定义一个函数zbx_tag_filter_free，参数为一个zbx_tag_filter_t类型的指针
static void zbx_tag_filter_free(zbx_tag_filter_t *tag_filter)
{
    // 释放tag_filter指向的内存空间
    zbx_free(tag_filter->tag);
    // 释放tag_filter指向的内存空间
/******************************************************************************
 * *
 *整个代码块的主要目的是添加一个消息警报事件到数据库。具体步骤如下：
 *
 *1. 检查用户权限，如果用户无权访问系统，则直接返回，不执行添加消息警报操作。
 *2. 构建插入消息警报的SQL语句，包括动作ID、逃避步骤、用户ID、媒体类型ID、主题、消息内容和确认ID。
 *3. 执行SQL插入操作。
 *4. 检查插入结果，如果插入成功，获取插入后的ID并释放查询结果。
 *5. 如果没有获取到插入后的ID，则抛出异常。
 *6. 如果插入操作失败，则抛出异常。
 *7. 返回插入的消息警报ID。
 ******************************************************************************/
/******************************************************************************
 *                                                                            *
 * Function: add_message_alert                                               *
 *                                                                            *
 * Purpose: Add a message alert event to the database                         *
 *                                                                            *
 * Parameters: event - pointer to the DB_EVENT structure                       *
 *               r_event - pointer to the DB_EVENT structure                   *
 *              actionid - unique identifier of the action                     *
 *              esc_step - escape step                                       *
 *             userid - user ID                                             *
 *             mediatypeid - media type ID                                   *
 *             subject - subject of the message alert                        *
 *             message - message body                                       *
 *              acknowledgement ID (optional)                                   *
 * Return value: None                                                       *
 *                                                                            *
 ******************************************************************************/
static void add_message_alert(const DB_EVENT *event, const DB_EVENT *r_event, zbx_uint64_t actionid, int esc_step,
                             zbx_uint64_t userid, zbx_uint64_t mediatypeid, const char *subject, const char *message,
                             zbx_uint64_t ackid)
{
    // 声明变量
    DB_RESULT result;
    DB_ROW row;
    zbx_uint64_t inserted_id;

    // 检查用户权限
    if (FAIL == check_perm2system(userid))
    {
        // 用户无权访问系统，直接返回，不执行添加消息警报操作
        return;
    }

    // 构建插入消息警报的SQL语句
    char sql[256];
    snprintf(sql, sizeof(sql), "INSERT INTO msg_alerts (actionid, esc_step, userid, mediatypeid, subject, message, ackid)
                             VALUES (%llu, %d, %llu, %llu, '%s', '%s', %llu)",
               actionid, esc_step, userid, mediatypeid, subject, message, ackid);

    // 执行SQL插入操作
    result = DBexecute(sql);

    // 检查插入结果
    if (DBIS_OK(result))
    {
        // 获取插入后的ID
        if (NULL != (row = DBfetch(result)))
        {
            inserted_id = row[0];
        }
        else
        {
            // 如果没有获取到插入后的ID，则抛出异常
            zbx_raise_error(ZBX_ERROR_DB_FAILED, "Failed to insert message alert");
        }

        // 释放查询结果
        DBfree_result(result);
    }
    else
    {
        // 如果插入操作失败，则抛出异常
        zbx_raise_error(ZBX_ERROR_DB_FAILED, "Failed to insert message alert");
    }

    // 返回插入的消息警报ID
    zbx_uint64_t msg_alert_id = inserted_id;
    zbx_printf("Message alert added with ID: %llu\
", msg_alert_id);
}

			userid, GROUP_STATUS_DISABLED);

	if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]) && atoi(row[0]) > 0)
		res = FAIL;

	DBfree_result(result);

	return res;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查询数据库中指定 userid 的用户类型，并将查询结果存储在 user_type 变量中，最后返回 user_type 变量。
 ******************************************************************************/
// 定义一个名为 get_user_type 的静态函数，参数为一个 zbx_uint64_t 类型的 userid
static int get_user_type(zbx_uint64_t userid)
{
    // 定义一个整型变量 user_type，初始值为 -1，用于存储用户类型
    int user_type = -1;
    // 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果
    DB_RESULT result;
    // 定义一个 DB_ROW 类型的变量 row，用于存储数据库查询的一行数据
    DB_ROW row;

    // 使用 DBselect 函数执行数据库查询，查询用户类型，查询条件为 userid
    result = DBselect("select type from users where userid=" ZBX_FS_UI64, userid);

    // 判断查询结果是否不为空，且第一条数据不为空（即 userid 存在）
    if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
    {
        // 如果查询结果不为空，将第一条数据（索引为 0 的数据）转换为整型，并存储在 user_type 变量中
        user_type = atoi(row[0]);
    }

    // 释放查询结果内存
    DBfree_result(result);

    // 返回 user_type 变量，即查询到的用户类型
/******************************************************************************
 * *
 *整个代码块的主要目的是获取用户在指定主机组中的最低权限。函数`get_hostgroups_permission`接收两个参数，一个是用户ID，另一个是指向主机组ID的指针。函数首先判断主机组ID的数量，如果为0则直接返回。接着构造SQL查询语句，查询用户在各个主机组中的最低权限。从查询结果中获取用户在各个主机组中的最低权限，并释放查询结果和SQL语句占用的内存。最后输出日志，记录权限，并返回用户的最低权限。
 ******************************************************************************/
/* 定义一个函数，用于获取用户在指定主机组中的权限 */
static int get_hostgroups_permission(zbx_uint64_t userid, zbx_vector_uint64_t *hostgroupids)
{
    /* 定义一些变量 */
    const char *__function_name = "get_hostgroups_permission";
    int perm = PERM_DENY;
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;
    DB_RESULT result;
    DB_ROW row;

    /* 记录函数进入日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    /* 如果主机组数量为0，直接返回 */
    if (0 == hostgroupids->values_num)
    {
        goto out;
    }

    /* 构造SQL查询语句，查询用户在各个主机组中的最低权限 */
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                "select min(r.permission)"
                " from rights r"
                " join users_groups ug on ug.usrgrpid=r.groupid"
                    " where ug.userid=" ZBX_FS_UI64 " and", userid);
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "r.id",
                hostgroupids->values, hostgroupids->values_num);
    result = DBselect("%s", sql);

    /* 从查询结果中获取用户在各个主机组中的最低权限 */
    if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
    {
        perm = atoi(row[0]);
    }

    /* 释放查询结果和SQL语句占用的内存 */
    DBfree_result(result);
    zbx_free(sql);

    /* 输出日志，记录权限 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_permission_string(perm));

    /* 返回用户的最低权限 */
    return perm;
/******************************************************************************
 * *
 *这段代码的主要目的是检查用户是否有权限执行特定的事件。它根据用户ID、主机组ID和事件信息来判断用户是否具有相应的标签过滤器，如果满足条件，则返回SUCCEED，否则返回FAIL。在整个过程中，代码使用了C语言的基本语法和一些常用数据结构，如数组、链表和指针。
 ******************************************************************************/
// 定义一个静态函数，用于检查基于标签的权限
static int check_tag_based_permission(zbx_uint64_t userid, zbx_vector_uint64_t *hostgroupids, const DB_EVENT *event)
{
	// 定义一些变量，如日志级别、字符串指针、缓冲区大小等
	const char *__function_name = "check_tag_based_permission";
	char *sql = NULL, hostgroupid[ZBX_MAX_UINT64_LEN + 1];
	size_t sql_alloc = 0, sql_offset = 0;
	DB_RESULT result;
	DB_ROW row;
	int ret = FAIL, i;
	zbx_vector_ptr_t tag_filters;
	zbx_tag_filter_t *tag_filter;
	DB_CONDITION condition;

	// 开启日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个内存池，用于存储标签过滤器
	zbx_vector_ptr_create(&tag_filters);

	// 构造SQL查询语句，查询与用户关联的标签过滤器
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select tf.groupid,tf.tag,tf.value from tag_filter tf"
			" join users_groups ug on ug.usrgrpid=tf.usrgrpid"
				" where ug.userid=" ZBX_FS_UI64, userid);
	result = DBselect("%s order by tf.groupid", sql);

	// 遍历查询结果，将标签过滤器添加到内存池中
	while (NULL != (row = DBfetch(result)))
	{
		tag_filter = (zbx_tag_filter_t *)zbx_malloc(NULL, sizeof(zbx_tag_filter_t));
		ZBX_STR2UINT64(tag_filter->hostgroupid, row[0]);
		tag_filter->tag = zbx_strdup(NULL, row[1]);
		tag_filter->value = zbx_strdup(NULL, row[2]);
		zbx_vector_ptr_append(&tag_filters, tag_filter);
	}
	zbx_free(sql);
	DBfree_result(result);

	// 判断内存池中的标签过滤器数量是否大于0，如果大于0，则继续执行后续操作
	if (0 < tag_filters.values_num)
		condition.op = CONDITION_OPERATOR_EQUAL;
	else
		ret = SUCCEED;

	// 遍历标签过滤器，检查事件是否满足条件
	for (i = 0; i < tag_filters.values_num && SUCCEED != ret; i++)
	{
		tag_filter = (zbx_tag_filter_t *)tag_filters.values[i];

		// 检查主机组是否包含指定的标签
		if (FAIL == zbx_vector_uint64_search(hostgroupids, tag_filter->hostgroupid,
				ZBX_DEFAULT_UINT64_COMPARE_FUNC))
		{
			continue;
		}

		// 如果标签和值都不为空，则检查事件是否满足条件
		if (NULL != tag_filter->tag && 0 != strlen(tag_filter->tag) &&
			NULL != tag_filter->value && 0 != strlen(tag_filter->value))
		{
			zbx_snprintf(hostgroupid, sizeof(hostgroupid), ZBX_FS_UI64, tag_filter->hostgroupid);

			// 构造条件对象
			condition.conditiontype = CONDITION_TYPE_EVENT_TAG_VALUE;
			condition.value2 = tag_filter->tag;
			condition.value = tag_filter->value;

			// 检查事件是否满足条件
			ret = check_action_condition(event, &condition);
		}
		else
			ret = SUCCEED;
	}
	// 释放内存池中的标签过滤器
	zbx_vector_ptr_clear_ext(&tag_filters, (zbx_clean_func_t)zbx_tag_filter_free);
	zbx_vector_ptr_destroy(&tag_filters);

	// 结束日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回结果
	return ret;
}

		if (FAIL == zbx_vector_uint64_search(hostgroupids, tag_filter->hostgroupid,
				ZBX_DEFAULT_UINT64_COMPARE_FUNC))
		{
			continue;
		}

		if (NULL != tag_filter->tag && 0 != strlen(tag_filter->tag))
		{
			zbx_snprintf(hostgroupid, sizeof(hostgroupid), ZBX_FS_UI64, tag_filter->hostgroupid);

			if (NULL != tag_filter->value && 0 != strlen(tag_filter->value))
			{
				condition.conditiontype = CONDITION_TYPE_EVENT_TAG_VALUE;
				condition.value2 = tag_filter->tag;
				condition.value = tag_filter->value;
			}
			else
			{
				condition.conditiontype = CONDITION_TYPE_EVENT_TAG;
				condition.value = tag_filter->tag;
			}

			ret = check_action_condition(event, &condition);
		}
		else
			ret = SUCCEED;
	}
	zbx_vector_ptr_clear_ext(&tag_filters, (zbx_clean_func_t)zbx_tag_filter_free);
	zbx_vector_ptr_destroy(&tag_filters);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_trigger_permission                                           *
 *                                                                            *
 * Purpose: Return user permissions for access to trigger                     *
 *                                                                            *
 * Return value: PERM_DENY - if host or user not found,                       *
 *                   or permission otherwise                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 get_trigger_permission 的静态函数，参数分别为 zbx_uint64_t 类型的 userid 和 const DB_EVENT *类型的 event。
   这个函数的主要目的是获取用户对触发器的权限。 */
static int	get_trigger_permission(zbx_uint64_t userid, const DB_EVENT *event)
{
	/* 定义一个名为 __function_name 的字符指针，用于存储函数名。 */
	const char		*__function_name = "get_trigger_permission";
	/* 定义一个名为 perm 的整型变量，初始值为 PERM_DENY。 */
	int			perm = PERM_DENY;
	/* 定义一个名为 result 的 DB_RESULT 类型变量。 */
	DB_RESULT		result;
	/* 定义一个名为 row 的 DB_ROW 类型变量。 */
	DB_ROW			row;
	/* 定义一个名为 hostgroupids 的 zbx_vector_uint64_t 类型变量。 */
	zbx_vector_uint64_t	hostgroupids;
	/* 定义一个名为 hostgroupid 的 zbx_uint64_t 类型变量。 */
	zbx_uint64_t		hostgroupid;

	/* 使用 zabbix_log 记录调试信息，表示函数开始执行。 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 判断用户类型是否为超级管理员，如果是，则权限设置为 PERM_READ_WRITE。 */
	if (USER_TYPE_SUPER_ADMIN == get_user_type(userid))
	{
		perm = PERM_READ_WRITE;
		/* 使用 goto 语句跳转到 out 标签，结束函数执行。 */
		goto out;
	}

	/* 创建一个 zbx_vector_uint64_t 类型的 hostgroupids 变量。 */
	zbx_vector_uint64_create(&hostgroupids);

	/* 执行一个数据库查询操作，查询与给定事件关联的各个主机组。 */
	result = DBselect(
			"select distinct hg.groupid from items i"
			" join functions f on i.itemid=f.itemid"
			" join hosts_groups hg on hg.hostid = i.hostid"
				" and f.triggerid=" ZBX_FS_UI64,
			event->objectid);

	/* 遍历查询结果，将每个主机组的 groupid 添加到 hostgroupids 向量中。 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将 row[0] 转换为 zbx_uint64_t 类型，并将其添加到 hostgroupids 向量中。 */
		ZBX_STR2UINT64(hostgroupid, row[0]);
		zbx_vector_uint64_append(&hostgroupids, hostgroupid);
	}
	/* 释放查询结果内存。 */
	DBfree_result(result);

	/* 对 hostgroupids 向量进行排序。 */
	zbx_vector_uint64_sort(&hostgroupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 判断用户对主机组的权限，如果 PERM_DENY < perm 且 check_tag_based_permission 函数返回 FAIL，则将 perm 设置为 PERM_DENY。 */
	if (PERM_DENY < (perm = get_hostgroups_permission(userid, &hostgroupids)) &&
			FAIL == check_tag_based_permission(userid, &hostgroupids, event))
	{
		perm = PERM_DENY;
	}

	/* 释放 hostgroupids 向量占用的内存。 */
	zbx_vector_uint64_destroy(&hostgroupids);
out:
	/* 使用 zabbix_log 记录调试信息，表示函数执行结束。 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_permission_string(perm));

	/* 返回 perm 变量，表示用户对触发器的权限。 */
	return perm;
}


/******************************************************************************
 *                                                                            *
 * Function: get_item_permission                                              *
 *                                                                            *
 * Purpose: Return user permissions for access to item                        *
 *                                                                            *
 * Return value: PERM_DENY - if host or user not found,                       *
 *                   or permission otherwise                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取用户在指定物品上的权限。函数`get_item_permission`接收两个参数，分别是用户ID和物品ID。首先，根据用户ID判断用户类型，如果是超级管理员，则直接赋予读写权限。然后，查询与指定物品相关的主机组信息，并将查询结果中的主机组ID添加到主机组ID列表中。接着，获取主机组的权限，并根据用户ID和主机组权限计算出最终的权限值。最后，返回最终的权限值。
 ******************************************************************************/
static int	get_item_permission(zbx_uint64_t userid, zbx_uint64_t itemid)
{
    // 定义常量字符串，表示函数名
    const char *__function_name = "get_item_permission";
    // 定义数据库操作结果变量
    DB_RESULT result;
    // 定义数据库行变量
    DB_ROW row;
    // 定义权限变量，默认值为拒绝
    int perm = PERM_DENY;
    // 定义主机组ids向量
    zbx_vector_uint64_t hostgroupids;
    // 定义主机组id变量
    zbx_uint64_t hostgroupid;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建主机组ids向量
    zbx_vector_uint64_create(&hostgroupids);
    // 对主机组ids向量进行排序
    zbx_vector_uint64_sort(&hostgroupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 判断用户类型，如果是超级管理员，则直接赋予读写权限
    if (USER_TYPE_SUPER_ADMIN == get_user_type(userid))
    {
        perm = PERM_READ_WRITE;
        // 跳转到out标签，结束函数执行
        goto out;
    }

    // 从数据库中查询与指定物品相关的主机组信息
    result = DBselect(
            "select hg.groupid from items i"
            " join hosts_groups hg on hg.hostid=i.hostid"
            " where i.itemid=" ZBX_FS_UI64,
            itemid);

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为add_user_msg的函数，用于向用户消息列表中添加消息。该函数接收5个参数：用户ID、媒体类型ID、用户消息指针、主题字符串、消息字符串和确认ID。在函数中，首先根据媒体类型ID判断是否需要查找已存在的用户消息列表，然后遍历列表，查找符合条件的消息。如果找到符合条件的消息，则释放该消息的内存并继续遍历。如果没有找到符合条件的消息，则为新的消息分配内存并添加到用户消息列表中。最后，更新用户消息列表指针并结束函数。
 ******************************************************************************/
// 定义静态函数add_user_msg，接收5个参数：userid（用户ID），mediatypeid（媒体类型ID），user_msg指针，主题字符串，消息字符串，ackid（确认ID）
static void add_user_msg(zbx_uint64_t userid, zbx_uint64_t mediatypeid, ZBX_USER_MSG **user_msg,
                       const char *subject, const char *message, zbx_uint64_t ackid)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "add_user_msg";
    ZBX_USER_MSG	*p, **pNext;

    // 记录日志，表示进入add_user_msg函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断mediatypeid是否为0，如果为0，则表示需要查找已存在的用户消息列表
    if (0 == mediatypeid)
    {
        // 遍历用户消息列表，查找相同用户ID、确认ID、主题和消息的消息
        for (pNext = user_msg, p = *user_msg; NULL != p; p = *pNext)
        {
            // 判断消息是否符合条件，如果符合条件，则释放当前消息的内存，并将指针指向下一个消息
            if (p->userid == userid && p->ackid == ackid && 0 == strcmp(p->subject, subject) &&
                    0 == strcmp(p->message, message) && 0 != p->mediatypeid)
            {
                *pNext = (ZBX_USER_MSG *)p->next;

                zbx_free(p->subject);
                zbx_free(p->message);
                zbx_free(p);
            }
            else
                pNext = (ZBX_USER_MSG **)&p->next;
        }
    }

    // 遍历用户消息列表，查找相同用户ID、确认ID和媒体类型ID的消息
    for (p = *user_msg; NULL != p; p = (ZBX_USER_MSG *)p->next)
    {
        // 判断消息是否符合条件，如果符合条件，则跳出循环
        if (p->userid == userid && p->ackid == ackid && 0 == strcmp(p->subject, subject) &&
                0 == strcmp(p->message, message) &&
                (0 == p->mediatypeid || mediatypeid == p->mediatypeid))
        {
            break;
        }
    }

    // 如果没有找到符合条件的消息，则为新消息分配内存
    if (NULL == p)
    {
        // 为新消息分配内存，并初始化消息结构体
        p = (ZBX_USER_MSG *)zbx_malloc(p, sizeof(ZBX_USER_MSG));

        // 设置消息参数
        p->userid = userid;
        p->mediatypeid = mediatypeid;
        p->ackid = ackid;
        p->subject = zbx_strdup(NULL, subject);
        p->message = zbx_strdup(NULL, message);
        p->next = *user_msg;

        // 更新用户消息列表指针
        *user_msg = p;
    }

    // 记录日志，表示结束add_user_msg函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	{
		if (p->userid == userid && p->ackid == ackid && 0 == strcmp(p->subject, subject) &&
				0 == strcmp(p->message, message) &&
				(0 == p->mediatypeid || mediatypeid == p->mediatypeid))
		{
			break;
		}
	}
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义函数：add_sentusers_msg
 * 参数：
 *   ZBX_USER_MSG 指针，用于存储用户消息的结构体
 *   zbx_uint64_t actionid，动作ID
 *   const DB_EVENT *event，事件结构体
 *   const DB_EVENT *r_event，回复事件结构体（可选）
 *   const char *subject，主题
 *   const char *message，消息内容
 *   const DB_ACKNOWLEDGE *ack，确认信息（可选）
 * 返回值：无
 * 主要目的：根据给定的事件和消息，将消息添加到用户消息列表中，并将结果存储在传入的ZBX_USER_MSG指针指向的结构体中。
 */
static void add_sentusers_msg(ZBX_USER_MSG **user_msg, zbx_uint64_t actionid, const DB_EVENT *event,
                            const DB_EVENT *r_event, const char *subject, const char *message, const DB_ACKNOWLEDGE *ack)
{
	/* 定义日志级别 */
	const char *__function_name = "add_sentusers_msg";
	char		*subject_dyn, *message_dyn, *sql = NULL;
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	userid, mediatypeid;
	int		message_type;
	size_t		sql_alloc = 0, sql_offset = 0;

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 构建SQL查询语句 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct userid,mediatypeid"
			" from alerts"
			" where actionid=%s"
			" and mediatypeid is not null"
			" and alerttype=%d"
			" and acknowledgeid is null"
			" and (eventid=%s)",
			zbx_fs_ui64(actionid), ALERT_TYPE_MESSAGE, zbx_fs_ui64(event->eventid));

	/* 判断是否需要添加回复事件的条件 */
	if (NULL != r_event)
	{
		message_type = MACRO_TYPE_MESSAGE_RECOVERY;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " or eventid=%s", zbx_fs_ui64(r_event->eventid));
	}
	else
		message_type = MACRO_TYPE_MESSAGE_NORMAL;

	zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');

	/* 根据ack判断消息类型 */
	if (NULL != ack)
		message_type = MACRO_TYPE_MESSAGE_ACK;

	/* 执行SQL查询 */
	result = DBselect("%s", sql);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_DBROW2UINT64(userid, row[0]);

		/* 排除确认作者 from 收件人列表 */
		if (NULL != ack && ack->userid == userid)
			continue;

		/* 检查用户权限 */
		if (SUCCEED != check_perm2system(userid))
			continue;

		ZBX_STR2UINT64(mediatypeid, row[1]);

		/* 根据事件类型检查权限 */
		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				if (PERM_READ > get_trigger_permission(userid, event))
					continue;
				break;
			case EVENT_OBJECT_ITEM:
			case EVENT_OBJECT_LLDRULE:
				if (PERM_READ > get_item_permission(userid, event->objectid))
					continue;
				break;
		}

		/* 准备消息主题和内容 */
		subject_dyn = zbx_strdup(NULL, subject);
		message_dyn = zbx_strdup(NULL, message);

		/* 替换简单宏 */
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL,
				ack, &subject_dyn, message_type, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL,
				ack, &message_dyn, message_type, NULL, 0);

		/* 添加用户消息 */
		add_user_msg(userid, mediatypeid, user_msg, subject_dyn, message_dyn,
				(NULL != ack ? ack->acknowledgeid : 0));

		/* 释放内存 */
		zbx_free(subject_dyn);
		zbx_free(message_dyn);
	}
	/* 释放查询结果 */
	DBfree_result(result);

	/* 释放SQL字符串 */
	zbx_free(sql);

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL, ack,
				&subject_dyn, macro_type, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL, ack,
				&message_dyn, macro_type, NULL, 0);

		// 添加用户消息
		add_user_msg(userid, mediatypeid, user_msg, subject_dyn, message_dyn,
				(NULL != ack ? ack->acknowledgeid : 0));

		// 释放内存
		zbx_free(subject_dyn);
		zbx_free(message_dyn);
	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 关闭日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: add_sentusers_msg                                                *
 *                                                                            *
 * Purpose: adds message to be sent to all recipients of messages previously  *
 *          generated by action operations or acknowledgement operations,     *
 *          which is related with an event or recovery event                  *
 *                                                                            *
 * Parameters: user_msg - [IN/OUT] the message list                           *
 *             actionid - [IN] the action identifier                          *
 *             event    - [IN] the event                                      *
 *             r_event  - [IN] the recover event (optional, can be NULL)      *
 *             subject  - [IN] the message subject                            *
 *             message  - [IN] the message body                               *
 *             ack      - [IN] the acknowledge (optional, can be NULL)        *
 *                                                                            *
 ******************************************************************************/
static void	add_sentusers_msg(ZBX_USER_MSG **user_msg, zbx_uint64_t actionid, const DB_EVENT *event,
		const DB_EVENT *r_event, const char *subject, const char *message, const DB_ACKNOWLEDGE *ack)
{
	const char	*__function_name = "add_sentusers_msg";
	char		*subject_dyn, *message_dyn, *sql = NULL;
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	userid, mediatypeid;
	int		message_type;
	size_t		sql_alloc = 0, sql_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct userid,mediatypeid"
			" from alerts"
			" where actionid=" ZBX_FS_UI64
				" and mediatypeid is not null"
				" and alerttype=%d"
				" and acknowledgeid is null"
				" and (eventid=" ZBX_FS_UI64,
				actionid, ALERT_TYPE_MESSAGE, event->eventid);

	if (NULL != r_event)
	{
		message_type = MACRO_TYPE_MESSAGE_RECOVERY;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " or eventid=" ZBX_FS_UI64, r_event->eventid);
	}
	else
		message_type = MACRO_TYPE_MESSAGE_NORMAL;

	zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');

	if (NULL != ack)
		message_type = MACRO_TYPE_MESSAGE_ACK;

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_DBROW2UINT64(userid, row[0]);

		/* exclude acknowledgement author from the recipient list */
		if (NULL != ack && ack->userid == userid)
			continue;

		if (SUCCEED != check_perm2system(userid))
			continue;

		ZBX_STR2UINT64(mediatypeid, row[1]);

		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				if (PERM_READ > get_trigger_permission(userid, event))
					continue;
				break;
			case EVENT_OBJECT_ITEM:
			case EVENT_OBJECT_LLDRULE:
				if (PERM_READ > get_item_permission(userid, event->objectid))
					continue;
				break;
		}

		subject_dyn = zbx_strdup(NULL, subject);
		message_dyn = zbx_strdup(NULL, message);

		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL,
				ack, &subject_dyn, message_type, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL, NULL,
				ack, &message_dyn, message_type, NULL, 0);

		add_user_msg(userid, mediatypeid, user_msg, subject_dyn, message_dyn,
				(NULL != ack ? ack->acknowledgeid : 0));

		zbx_free(subject_dyn);
		zbx_free(message_dyn);
	}
	DBfree_result(result);

	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: add_sentusers_ack_msg                                            *
 *                                                                            *
 * Purpose: adds message to be sent to all who added acknowlegment and        *
 *          involved in discussion                                            *
 *                                                                            *
 * Parameters: user_msg    - [IN/OUT] the message list                        *
 *             actionid    - [IN] the action identifie                        *
 *             mediatypeid - [IN] the media type id defined for the operation *
 *             event       - [IN] the event                                   *
 *             ack         - [IN] the acknowlegment                           *
 *             subject     - [IN] the message subject                         *
 *             message     - [IN] the message body                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个数据库查询中获取用户ID列表，然后检查每个用户是否具有发送消息的权限。如果满足条件，则为每个用户创建一个消息并将它们添加到数据库中。这个过程是通过遍历查询结果、动态分配内存、替换宏以及调用`add_user_msg()`函数来完成的。最后，释放分配的内存和日志记录。
 ******************************************************************************/
/* 定义函数：add_sentusers_ack_msg
 * 参数：
 *   ZBX_USER_MSG **user_msg：用户消息指针
 *   zbx_uint64_t actionid：动作ID
 *   zbx_uint64_t mediatypeid：媒体类型ID
 *   const DB_EVENT *event：事件指针
 *   const DB_EVENT *r_event：回复事件指针
 *   const DB_ACKNOWLEDGE *ack：确认指针
 *   const char *subject：主题
 *   const char *message：消息
 * 返回值：无
 * 功能：向数据库中添加发送给用户的消息
 */
static void add_sentusers_ack_msg(ZBX_USER_MSG **user_msg, zbx_uint64_t actionid, zbx_uint64_t mediatypeid,
                                const DB_EVENT *event, const DB_EVENT *r_event, const DB_ACKNOWLEDGE *ack, const char *subject,
                                const char *message)
{
	/* 定义变量 */
	const char *__function_name = "add_sentusers_ack_msg";
	char *subject_dyn, *message_dyn;
	DB_RESULT result;
	DB_ROW row;
	zbx_uint64_t userid;

	/* 开启日志记录 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 从数据库中查询用户ID列表 */
	result = DBselect(
			"select distinct userid"
			" from acknowledges"
			" where eventid=" ZBX_FS_UI64,
			event->eventid);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将DB_ROW转换为zbx_uint64_t类型 */
		ZBX_DBROW2UINT64(userid, row[0]);

		/* 排除确认作者 from 收件人列表 */
		if (ack->userid == userid)
			continue;

		/* 检查用户权限 */
		if (SUCCEED != check_perm2system(userid) || PERM_READ > get_trigger_permission(userid, event))
			continue;

		/* 动态分配字符串内存 */
		subject_dyn = zbx_strdup(NULL, subject);
		message_dyn = zbx_strdup(NULL, message);

		/* 替换简单宏 */
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL,
				NULL, ack, &subject_dyn, MACRO_TYPE_MESSAGE_ACK, NULL, 0);
		substitute_simple_macros(&actionid, event, r_event, &userid, NULL, NULL, NULL,
				NULL, ack, &message_dyn, MACRO_TYPE_MESSAGE_ACK, NULL, 0);

		/* 向数据库中添加用户消息 */
		add_user_msg(userid, mediatypeid, user_msg, subject_dyn, message_dyn, ack->acknowledgeid);

		/* 释放内存 */
		zbx_free(subject_dyn);
		zbx_free(message_dyn);
	}
	/* 释放数据库查询结果 */
	DBfree_result(result);

	/* 结束日志记录 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是遍历一个用户消息链表，将链表中的每个用户消息添加到警报消息队列中，并释放链表中每个用户消息的相关内存。
 ******************************************************************************/
/* 定义一个静态函数 flush_user_msg，接收五个参数：
 * ZBX_USER_MSG类型的指针（用户消息链表的头指针）、
 * int类型的esc_step（退出步骤）、
 * const DB_EVENT类型的指针（事件）、
 * const DB_EVENT类型的指针（回复事件）、
 * zbx_uint64_t类型的actionid（操作ID）
 */
static void flush_user_msg(ZBX_USER_MSG **user_msg, int esc_step, const DB_EVENT *event, const DB_EVENT *r_event,
                         zbx_uint64_t actionid)
{
    /* 定义一个指向ZBX_USER_MSG类型的指针p，用于遍历用户消息链表 */
    ZBX_USER_MSG	*p;

    /* 使用while循环，当用户消息链表不为空时循环执行以下操作：
      取出链表中的一个元素（即一个用户消息结构体），将其指向下一个元素的指针指向NULL */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`add_command_alert`的静态函数，该函数用于向数据库中插入一个新的命令警报条目。函数接收多个参数，包括数据库插入结构体指针、警报数量、警报ID、主机结构体指针、数据库事件结构体指针（用于更新警报状态）、操作ID、逃脱步骤、命令字符串、警报状态和错误信息。在函数内部，首先进行日志记录，然后判断是否为第一条警报，预处理插入语句。接下来，获取当前时间，拼接主机名和命令字符串，判断是否有更新事件，最后插入新警报条目并释放临时字符串。
 ******************************************************************************/
/*
 * 函数名：add_command_alert
 * 功能：向数据库中插入一个新的命令警报条目
 * 参数：
 *   db_insert：数据库插入结构体指针
 *   alerts_num：警报数量，用于判断是否为第一条警报
 *   alertid：警报ID
 *   host：主机结构体指针
 *   event：数据库事件结构体指针
 *   r_event：数据库事件结构体指针，用于更新警报状态
 *   actionid：操作ID
 *   esc_step：逃脱步骤
 *   command：命令字符串
 *   status：警报状态
 *   error：错误信息
 */
static void add_command_alert(zbx_db_insert_t *db_insert, int alerts_num, zbx_uint64_t alertid, const DC_HOST *host,
                            const DB_EVENT *event, const DB_EVENT *r_event, zbx_uint64_t actionid, int esc_step,
                            const char *command, zbx_alert_status_t status, const char *error)
{
    /* 定义常量 */
    static const char *__function_name = "add_command_alert";
    static const int ALERT_TYPE_COMMAND = 1;

    /* 变量声明 */
    int now, alerttype = ALERT_TYPE_COMMAND, alert_status = status;
    char *tmp = NULL;

    /* 日志记录 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    /* 判断是否为第一条警报 */
    if (0 == alerts_num)
    {
        /* 预处理插入语句 */
        zbx_db_insert_prepare(db_insert, "alerts", "alertid", "actionid", "eventid", "clock", "message",
                             "status", "error", "esc_step", "alerttype", (NULL != r_event ? "p_eventid" : NULL),
                             NULL);
    }
/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *这个代码块的主要目的是从一个数据库查询中获取动态主机信息，并根据不同的事件来源和对象补充相应的查询条件。在遍历查询结果时，提取主机ID、代理主机ID、主机名、TLS连接等信息，并补充IPMI、TLS相关信息。最后判断是否成功获取到主机信息，并返回相应的错误信息或主机信息。
 ******************************************************************************/
static int get_dynamic_hostid(const DB_EVENT *event, DC_HOST *host, char *error, size_t max_error_len)
{
	/* 定义一个内部函数名，方便调试 */
	const char *__function_name = "get_dynamic_hostid";
	DB_RESULT	result;
	DB_ROW		row;
	char		sql[512];	/* 预留足够大的空间以备SQL语句变化 */
	size_t		offset;
	int		ret = SUCCEED;

	/* 记录日志，表示函数开始调用 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 构建SQL查询语句 */
	offset = zbx_snprintf(sql, sizeof(sql), "select distinct h.hostid,h.proxy_hostid,h.host,h.tls_connect");

	/* 根据事件来源不同，补充SQL查询语句中的条件 */
	switch (event->source)
	{
		case EVENT_SOURCE_TRIGGERS:
			zbx_snprintf(sql + offset, sizeof(sql) - offset,
					" from functions f,items i,hosts h"
					" where f.itemid=i.itemid"
						" and i.hostid=h.hostid"
						" and h.status=%d"
						" and f.triggerid=" ZBX_FS_UI64,
					HOST_STATUS_MONITORED, event->objectid);

			break;
		case EVENT_SOURCE_DISCOVERY:
			offset += zbx_snprintf(sql + offset, sizeof(sql) - offset,
					" from hosts h,interface i,dservices ds"
					" where h.hostid=i.hostid"
						" and i.ip=ds.ip"
						" and i.useip=1"
						" and h.status=%d",
						HOST_STATUS_MONITORED);

			/* 根据事件对象不同，补充SQL查询语句中的条件 */
			switch (event->object)
			{
				case EVENT_OBJECT_DHOST:
					zbx_snprintf(sql + offset, sizeof(sql) - offset,
							" and ds.dhostid=" ZBX_FS_UI64, event->objectid);
					break;
				case EVENT_OBJECT_DSERVICE:
					zbx_snprintf(sql + offset, sizeof(sql) - offset,
							" and ds.dserviceid=" ZBX_FS_UI64, event->objectid);
					break;
			}
			break;
		case EVENT_SOURCE_AUTO_REGISTRATION:
			zbx_snprintf(sql + offset, sizeof(sql) - offset,
					" from autoreg_host a,hosts h"
					" where " ZBX_SQL_NULLCMP("a.proxy_hostid", "h.proxy_hostid")
						" and a.host=h.host"
						" and h.status=%d"
						" and h.flags<>%d"
						" and a.autoreg_hostid=" ZBX_FS_UI64,
					HOST_STATUS_MONITORED, ZBX_FLAG_DISCOVERY_PROTOTYPE, event->objectid);
			break;
		default:
			zbx_snprintf(error, max_error_len, "Unsupported event source [%d]", event->source);
			return FAIL;
	}

	/* 初始化主机结构体中的hostid为0 */
	host->hostid = 0;

	/* 执行SQL查询语句 */
	result = DBselect("%s", sql);

	/* 遍历查询结果，提取所需信息 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 防止一个触发器中包含多个主机 */
		if (0 != host->hostid)
		{
			switch (event->source)
			{
				case EVENT_SOURCE_TRIGGERS:
					zbx_strlcpy(error, "Too many hosts in a trigger expression", max_error_len);
					break;
				case EVENT_SOURCE_DISCOVERY:
					zbx_strlcpy(error, "Too many hosts with same IP addresses", max_error_len);
					break;
			}
			ret = FAIL;
			break;
		}

		/* 提取主机ID、代理主机ID、主机名、TLS连接等信息 */
		ZBX_STR2UINT64(host->hostid, row[0]);
		ZBX_DBROW2UINT64(host->proxy_hostid, row[1]);
		strscpy(host->host, row[2]);
		ZBX_STR2UCHAR(host->tls_connect, row[3]);

		/* 补充IPMI、TLS相关信息 */
		switch (event->source)
		{
/******************************************************************************
 * *
 *这段代码的主要目的是执行远程命令。首先，它从数据库中获取主机列表，然后遍历这些主机，根据远程命令的类型和执行方式来执行相应的操作。具体来说，这段代码实现了以下功能：
 *
 *1. 定义函数`execute_commands`，输入参数包括远程命令的相关信息（如操作ID、目标主机ID等）。
 *2. 获取目标主机列表，根据主机ID和远程命令的类型来构建查询语句。
 *3. 执行查询语句，获取主机列表及其相关信息。
 *4. 遍历主机列表，根据远程命令的类型和执行方式来执行相应的操作。
 *   - 对于SSH和TELNET类型的远程命令，执行`zbx_script_execute`函数。
 *   - 对于GLOBAL类型的远程命令，执行`zbx_script_prepare`和`zbx_script_execute`函数。
 *   - 如果主机不是Zabbix服务器，则创建一个任务并执行。
 *5. 完成后，清理资源和日志。
 *
 *整个代码块的逻辑清晰，分为两部分：获取主机列表和执行远程命令。在获取主机列表的过程中，使用了`zbx_vector_uint64_create`、`get_operation_groupids`等函数来处理数据结构。在执行远程命令的过程中，根据不同的远程命令类型，调用相应的函数来执行。最后，清理资源和日志，确保程序正常结束。
 ******************************************************************************/
static void execute_commands(const DB_EVENT *event, const DB_EVENT *r_event, const DB_ACKNOWLEDGE *ack,
                           zbx_uint64_t actionid, zbx_uint64_t operationid, int esc_step, int macro_type)
{
	const char *__function_name = "execute_commands";
	DB_RESULT		result;
	DB_ROW			row;
	zbx_db_insert_t		db_insert;
	int			alerts_num = 0;
	char			*buffer = NULL;
	size_t			buffer_alloc = 2 * ZBX_KIBIBYTE, buffer_offset = 0;
	zbx_vector_uint64_t	executed_on_hosts, groupids;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	buffer = (char *)zbx_malloc(buffer, buffer_alloc);

	/* get hosts operation's hosts */

	zbx_vector_uint64_create(&groupids);
	get_operation_groupids(operationid, &groupids);

	if (0 != groupids.values_num)
	{
		zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				/* the 1st 'select' works if remote command target is "Host group" */
				"select distinct h.hostid,h.proxy_hostid,h.host,o.type,o.scriptid,o.execute_on,o.port"
					",o.authtype,o.username,o.password,o.publickey,o.privatekey,o.command,h.tls_connect"
#ifdef HAVE_OPENIPMI
				",h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password"
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				",h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
#endif
				);

		zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
				" from opcommand o,hosts_groups hg,hosts h"
				" where o.operationid=hg.operationid"
					" and hg.hostid=h.hostid"
					" and o.operationid=%llu"
					" and h.status=%d"
				" union "
				/* the 2nd 'select' works if remote command target is "Host" */
				"select distinct 0,0,null,o.type,o.scriptid,o.execute_on,o.port"
					",o.authtype,o.username,o.password,o.publickey,o.privatekey,o.command,%d",
				operationid, HOST_STATUS_MONITORED, ZBX_TCP_SEC_UNENCRYPTED);
#ifdef HAVE_OPENIPMI
		zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
					",0,2,null,null");
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
					",null,null,null,null");
#endif
		zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
				" from opcommand o,opcommand_hst oh"
				" where o.operationid=oh.operationid"
					" and o.operationid=%llu"
					" and oh.hostid is null",
				operationid);

		result = DBselect("%s", buffer);

		zbx_free(buffer);
		zbx_vector_uint64_destroy(&groupids);

		while (NULL != (row = DBfetch(result)))
	{
		int			rc = SUCCEED;
		char			error[ALERT_ERROR_LEN_MAX];
		DC_HOST			host;
		zbx_script_t		script;
		zbx_alert_status_t	status = ALERT_STATUS_NOT_SENT;
		zbx_uint64_t		alertid;

		*error = '\0';
		memset(&host, 0, sizeof(host));
		zbx_script_init(&script);

		script.type = (unsigned char)atoi(row[3]);

		if (ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT != script.type)
		{
			script.command = zbx_strdup(script.command, row[12]);
			substitute_simple_macros(&actionid, event, r_event, NULL, NULL,
					NULL, NULL, NULL, ack, &script.command, macro_type, NULL, 0);
		}

		if (ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT == script.type)
			script.execute_on = (unsigned char)atoi(row[5]);

		ZBX_STR2UINT64(host.hostid, row[0]);
		ZBX_DBROW2UINT64(host.proxy_hostid, row[1]);

		if (ZBX_SCRIPT_EXECUTE_ON_SERVER != script.execute_on)
		{
			if (0 != host.hostid)
			{
				if (FAIL != zbx_vector_uint64_search(&executed_on_hosts, host.hostid,
						ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					goto skip;
				}

				zbx_vector_uint64_append(&executed_on_hosts, host.hostid);
				strscpy(host.host, row[2]);
				host.tls_connect = (unsigned char)atoi(row[13]);
#ifdef HAVE_OPENIPMI
				host.ipmi_authtype = (signed char)atoi(row[14]);
				host.ipmi_privilege = (unsigned char)atoi(row[15]);
				strscpy(host.ipmi_username, row[16]);
				strscpy(host.ipmi_password, row[17]);
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				strscpy(host.tls_issuer, row[14 + ZBX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_subject, row[15 + ZBX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_psk_identity, row[16 + ZBX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_psk, row[17 + ZBX_IPMI_FIELDS_NUM]);
#endif
			}
			else if (SUCCEED == (rc = get_dynamic_hostid((NULL != r_event ? r_event : event), &host, error,
						sizeof(error))))
			{
				if (FAIL != zbx_vector_uint64_search(&executed_on_hosts, host.hostid,
						ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					goto skip;
				}

				zbx_vector_uint64_append(&executed_on_hosts, host.hostid);
			}
		}
		else
			zbx_strlcpy(host.host, "Zabbix server", sizeof(host.host));

		alertid = DBget_maxid("alerts");

		if (SUCCEED == rc)
		{
			switch (script.type)
			{
				case ZBX_SCRIPT_TYPE_SSH:
					script.authtype = (unsigned char)atoi(row[7]);
					script.publickey = zbx_strdup(script.publickey, row[10]);
					script.privatekey = zbx_strdup(script.privatekey, row[11]);
					ZBX_FALLTHROUGH;
				case ZBX_SCRIPT_TYPE_TELNET:
					script.port = zbx_strdup(script.port, row[6]);
					script.username = zbx_strdup(script.username, row[8]);
					script.password = zbx_strdup(script.password, row[9]);
					break;
				case ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT:
					ZBX_DBROW2UINT64(script.scriptid, row[4]);
					break;
			}

			if (SUCCEED == (rc = zbx_script_prepare(&script, &host, NULL, error, sizeof(error))))
			{
				if (0 == host.proxy_hostid || ZBX_SCRIPT_EXECUTE_ON_SERVER == script.execute_on)
				{
					rc = zbx_script_execute(&script, &host, NULL, error, sizeof(error));
					status = ALERT_STATUS_SENT;
				}
				else
				{
					if (0 == zbx_script_create_task(&script, &host, alertid, time(NULL)))
						rc = FAIL;
				}
			}
		}

		if (FAIL == rc)
			status = ALERT_STATUS_FAILED;

		add_command_alert(&db_insert, alerts_num++, alertid, &host, event, r_event, actionid, esc_step,
				script.command, status, error);
skip:
		zbx_script_clean(&script);
	}
	DBfree_result(result);
	zbx_vector_uint64_destroy(&executed_on_hosts);

	if (0 < alerts_num)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
			/* the 2nd 'select' works if remote command target is "Host" */
			"select distinct h.hostid,h.proxy_hostid,h.host,o.type,o.scriptid,o.execute_on,o.port"
				",o.authtype,o.username,o.password,o.publickey,o.privatekey,o.command,h.tls_connect"
#ifdef HAVE_OPENIPMI
			",h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password"
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
			",h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
#endif
			);
	zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
			" from opcommand o,opcommand_hst oh,hosts h"
			" where o.operationid=oh.operationid"
				" and oh.hostid=h.hostid"
				" and o.operationid=" ZBX_FS_UI64
				" and h.status=%d"
			" union "
			/* the 3rd 'select' works if remote command target is "Current host" */
			"select distinct 0,0,null,o.type,o.scriptid,o.execute_on,o.port"
				",o.authtype,o.username,o.password,o.publickey,o.privatekey,o.command,%d",
			operationid, HOST_STATUS_MONITORED, ZBX_TCP_SEC_UNENCRYPTED);
#ifdef HAVE_OPENIPMI
	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				",0,2,null,null");
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				",null,null,null,null");
#endif
	zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
			" from opcommand o,opcommand_hst oh"
			" where o.operationid=oh.operationid"
				" and o.operationid=" ZBX_FS_UI64
				" and oh.hostid is null",
			operationid);

	result = DBselect("%s", buffer);

	zbx_free(buffer);
	zbx_vector_uint64_create(&executed_on_hosts);

	while (NULL != (row = DBfetch(result)))
	{
		int			rc = SUCCEED;
		char			error[ALERT_ERROR_LEN_MAX];
		DC_HOST			host;
		zbx_script_t		script;
		zbx_alert_status_t	status = ALERT_STATUS_NOT_SENT;
		zbx_uint64_t		alertid;

		*error = '\0';
		memset(&host, 0, sizeof(host));
		zbx_script_init(&script);

		script.type = (unsigned char)atoi(row[3]);

		if (ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT != script.type)
		{
			script.command = zbx_strdup(script.command, row[12]);
			substitute_simple_macros(&actionid, event, r_event, NULL, NULL,
					NULL, NULL, NULL, ack, &script.command, macro_type, NULL, 0);
		}

		if (ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT == script.type)
			script.execute_on = (unsigned char)atoi(row[5]);

		ZBX_STR2UINT64(host.hostid, row[0]);
		ZBX_DBROW2UINT64(host.proxy_hostid, row[1]);

		if (ZBX_SCRIPT_EXECUTE_ON_SERVER != script.execute_on)
		{
			if (0 != host.hostid)
			{
				if (FAIL != zbx_vector_uint64_search(&executed_on_hosts, host.hostid,
						ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					goto skip;
				}

				zbx_vector_uint64_append(&executed_on_hosts, host.hostid);
				strscpy(host.host, row[2]);
				host.tls_connect = (unsigned char)atoi(row[13]);
#ifdef HAVE_OPENIPMI
				host.ipmi_authtype = (signed char)atoi(row[14]);
				host.ipmi_privilege = (unsigned char)atoi(row[15]);
				strscpy(host.ipmi_username, row[16]);
				strscpy(host.ipmi_password, row[17]);
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				strscpy(host.tls_issuer, row[14 + ZBX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_subject, row[15 + ZBX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_psk_identity, row[16 + ZBX_IPMI_FIELDS_NUM]);
				strscpy(host.tls_psk, row[17 + ZBX_IPMI_FIELDS_NUM]);
#endif
			}
			else if (SUCCEED == (rc = get_dynamic_hostid((NULL != r_event ? r_event : event), &host, error,
						sizeof(error))))
			{
				if (FAIL != zbx_vector_uint64_search(&executed_on_hosts, host.hostid,
						ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					goto skip;
				}

				zbx_vector_uint64_append(&executed_on_hosts, host.hostid);
			}
		}
		else
			zbx_strlcpy(host.host, "Zabbix server", sizeof(host.host));

		alertid = DBget_maxid("alerts");

		if (SUCCEED == rc)
		{
			switch (script.type)
			{
				case ZBX_SCRIPT_TYPE_SSH:
					script.authtype = (unsigned char)atoi(row[7]);
					script.publickey = zbx_strdup(script.publickey, row[10]);
					script.privatekey = zbx_strdup(script.privatekey, row[11]);
					ZBX_FALLTHROUGH;
				case ZBX_SCRIPT_TYPE_TELNET:
					script.port = zbx_strdup(script.port, row[6]);
					script.username = zbx_strdup(script.username, row[8]);
					script.password = zbx_strdup(script.password, row[9]);
/******************************************************************************
 * 以下是对代码的详细中文注释：
 *
 *
 *
 *该代码的主要目的是处理用户媒体警报。当触发器触发事件时，程序会根据用户配置的媒体类型和严重性来发送消息。如果媒体类型为0，则查询所有媒体类型；如果媒体类型不为0，则根据给定的媒体类型查询相应的媒体信息。然后，程序会检查媒体状态、严重性和周期，并根据条件发送消息。最后，将发送的消息存储在数据库中。
 ******************************************************************************/
static void	add_message_alert(const DB_EVENT *event, const DB_EVENT *r_event, zbx_uint64_t actionid, int esc_step,
		zbx_uint64_t userid, zbx_uint64_t mediatypeid, const char *subject, const char *message,
		zbx_uint64_t ackid)
{
	const char	*__function_name = "add_message_alert";

	DB_RESULT	result;
	DB_ROW		row;
	int		now, priority, have_alerts = 0, res;
	zbx_db_insert_t	db_insert;

	// 开启调试模式，记录函数调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	now = time(NULL);

	// 如果mediatypeid为0，查询所有媒体类型
	if (0 == mediatypeid)
	{
		result = DBselect(
				"select m.mediatypeid,m.sendto,m.severity,m.period,mt.status,m.active"
				" from media m,media_type mt"
				" where m.mediatypeid=mt.mediatypeid"
					" and m.userid=" ZBX_FS_UI64,
				userid);
	}
	// 如果mediatypeid不为0，根据mediatypeid查询对应媒体类型
	else
	{
		result = DBselect(
				"select m.mediatypeid,m.sendto,m.severity,m.period,mt.status,m.active"
				" from media m,media_type mt"
				" where m.mediatypeid=mt.mediatypeid"
					" and m.userid=" ZBX_FS_UI64
					" and m.mediatypeid=" ZBX_FS_UI64,
				userid, mediatypeid);
	}

	// 初始化媒体类型为0
	mediatypeid = 0;
	// 初始化优先级
	priority = EVENT_SOURCE_TRIGGERS == event->source ? event->trigger.priority : TRIGGER_SEVERITY_NOT_CLASSIFIED;

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 获取严重性、周期等信息
		int		severity, status;
		const char	*perror;
		char		*period = NULL;

		// 将查询结果中的数据转换为整数
		ZBX_STR2UINT64(mediatypeid, row[0]);
		severity = atoi(row[2]);
		period = zbx_strdup(period, row[3]);
		// 替换周期中的简单宏
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &period,
				MACRO_TYPE_COMMON, NULL, 0);

		// 记录日志
		zabbix_log(LOG_LEVEL_DEBUG, "severity:%d, media severity:%d, period:'%s', userid:" ZBX_FS_UI64,
				priority, severity, period, userid);

		// 如果媒体状态为禁用，跳过
		if (MEDIA_STATUS_DISABLED == atoi(row[5]))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "will not send message (user media disabled)");
			continue;
		}

		// 检查严重性是否匹配
		if (((1 << priority) & severity) == 0)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "will not send message (severity)");
			continue;
		}

		// 检查周期是否有效
		if (SUCCEED != zbx_check_time_period(period, time(NULL), &res))
		{
			status = ALERT_STATUS_FAILED;
			perror = "Invalid media activity period";
		}
		// 检查周期有效，但发送失败
		else if (SUCCEED != res)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "will not send message (period)");
			continue;
		}
		// 媒体状态为启用，发送消息
		else if (MEDIA_TYPE_STATUS_ACTIVE == atoi(row[4]))
		{
			status = ALERT_STATUS_NEW;
			perror = "";
		}
		// 媒体状态为禁用，发送失败消息
		else
		{
			status = ALERT_STATUS_FAILED;
			perror = "Media type disabled.";
		}

		// 如果还没有发送过警报，则发送
		if (0 == have_alerts)
		{
			have_alerts = 1;
			// 预处理数据库插入
			zbx_db_insert_prepare(&db_insert, "alerts", "alertid", "actionid", "eventid", "userid",
					"clock", "mediatypeid", "sendto", "subject", "message", "status", "error",
					"esc_step", "alerttype", "acknowledgeid",
					(NULL != r_event ? "p_eventid" : NULL), NULL);
		}

		// 有发送过警报，发送消息
		if (NULL != r_event)
		{
			// 添加消息到数据库
			zbx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, r_event->eventid, userid,
					now, mediatypeid, row[1], subject, message, status, perror, esc_step,
					(int)ALERT_TYPE_MESSAGE, ackid, event->eventid);
		}
		else
		{
			// 添加消息到数据库
			zbx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, event->eventid, userid,
					now, mediatypeid, row[1], subject, message, status, perror, esc_step,
					(int)ALERT_TYPE_MESSAGE, ackid);
		}

		// 释放周期字符串
		zbx_free(period);
	}

	// 释放查询结果
	DBfree_result(result);

	// 如果没有发送过警报，发送默认警报
	if (0 == have_alerts)
	{
		// 定义错误信息
		char	error[MAX_STRING_LEN];

		have_alerts = 1;

		// 构建错误信息
		zbx_snprintf(error, sizeof(error), "No media defined for user.");

		// 发送默认警报
		zbx_db_insert_prepare(&db_insert, "alerts", "alertid", "actionid", "eventid", "userid", "clock",
				"subject", "message", "status", "retries", "error", "esc_step", "alerttype",
				"acknowledgeid", (NULL != r_event ? "p_eventid" : NULL), NULL);

		// 添加默认警报消息
		zbx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, r_event->eventid, userid,
				now, 0, row[1], subject, message, (int)ALERT_STATUS_FAILED, error,
				esc_step, (int)ALERT_TYPE_MESSAGE, ackid);
	}

	// 执行数据库插入
	if (0 != have_alerts)
	{
		zbx_db_insert_autoincrement(&db_insert, "alertid");
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	// 结束调试模式
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

					(int)ALERT_TYPE_MESSAGE, ackid);
		}

		zbx_free(period);
	}

	DBfree_result(result);

	if (0 == mediatypeid)
	{
		char	error[MAX_STRING_LEN];

		have_alerts = 1;

		zbx_snprintf(error, sizeof(error), "No media defined for user.");

		zbx_db_insert_prepare(&db_insert, "alerts", "alertid", "actionid", "eventid", "userid", "clock",
				"subject", "message", "status", "retries", "error", "esc_step", "alerttype",
				"acknowledgeid", (NULL != r_event ? "p_eventid" : NULL), NULL);

		if (NULL != r_event)
		{
			zbx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, r_event->eventid, userid,
					now, subject, message, (int)ALERT_STATUS_FAILED, (int)ALERT_MAX_RETRIES, error,
					esc_step, (int)ALERT_TYPE_MESSAGE, ackid, event->eventid);
		}
		else
		{
			zbx_db_insert_add_values(&db_insert, __UINT64_C(0), actionid, event->eventid, userid,
					now, subject, message, (int)ALERT_STATUS_FAILED, (int)ALERT_MAX_RETRIES, error,
					esc_step, (int)ALERT_TYPE_MESSAGE, ackid);
		}
	}

	if (0 != have_alerts)
	{
		zbx_db_insert_autoincrement(&db_insert, "alertid");
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: check_operation_conditions                                       *
 *                                                                            *
 * Purpose:                                                                   *
 *                                                                            *
 * Parameters: event    - event to check                                      *
 *             actionid - action ID for matching                              *
 *                                                                            *
 * Return value: SUCCEED - matches, FAIL - otherwise                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查操作条件是否满足。该函数接收三个参数：一个数据库事件指针、一个操作ID和一个评估类型。评估类型可以是AND、OR或AND_OR。函数首先从数据库中查询与给定操作ID相关联的条件，然后逐个检查这些条件是否满足。根据评估类型，函数处理条件的方式不同。如果所有条件都满足，函数返回SUCCEED；否则，返回FAIL。在整个过程中，函数还打印了相应的日志以记录操作和结果。
 ******************************************************************************/
/* 定义一个函数，用于检查操作条件是否满足 */
static int	check_operation_conditions(const DB_EVENT *event, zbx_uint64_t operationid, unsigned char evaltype)
{
	/* 定义一些变量 */
	const char	*__function_name = "check_operation_conditions";

	DB_RESULT	result;
	DB_ROW		row;
	DB_CONDITION	condition;

	int		ret = SUCCEED; /* SUCCEED required for CONDITION_EVAL_TYPE_AND_OR */
	int		cond, exit = 0;
	unsigned char	old_type = 0xff;

	/* 打印日志，记录操作id */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() operationid:" ZBX_FS_UI64, __function_name, operationid);

	/* 从数据库中查询操作条件 */
	result = DBselect("select conditiontype,operator,value"
				" from opconditions"
				" where operationid=" ZBX_FS_UI64
				" order by conditiontype",
			operationid);

	/* 遍历查询结果，直到遍历完成或退出循环 */
	while (NULL != (row = DBfetch(result)) && 0 == exit)
	{
		/* 初始化条件结构体 */
		memset(&condition, 0, sizeof(condition));
		condition.conditiontype	= (unsigned char)atoi(row[0]);
		condition.op = (unsigned char)atoi(row[1]);
		condition.value = row[2];

		/* 根据评估类型切换条件处理方式 */
		switch (evaltype)
		{
			case CONDITION_EVAL_TYPE_AND_OR:
				/* 如果上一个条件为OR类型，则检查当前条件是否满足 */
				if (old_type == condition.conditiontype)	/* OR conditions */
				{
					if (SUCCEED == check_action_condition(event, &condition))
						ret = SUCCEED;
				}
				else							/* AND conditions */
				{
					/* break if PREVIOUS AND condition is FALSE */
					if (ret == FAIL)
						exit = 1;
					else if (FAIL == check_action_condition(event, &condition))
						ret = FAIL;
				}
				old_type = condition.conditiontype;
				break;
			case CONDITION_EVAL_TYPE_AND:
				/* 检查动作条件是否满足，并记录结果 */
				cond = check_action_condition(event, &condition);
				/* 如果有AND条件为假，则退出循环并返回FAIL */
				if (cond == FAIL)
				{
					ret = FAIL;
					exit = 1;
				}
				else
					ret = SUCCEED;
				break;
			case CONDITION_EVAL_TYPE_OR:
				/* 检查动作条件是否满足，并记录结果 */
				cond = check_action_condition(event, &condition);
				/* 如果有OR条件为真，则退出循环并返回SUCCEED */
				if (cond == SUCCEED)
				{
					ret = SUCCEED;
					exit = 1;
				}
				else
					ret = FAIL;
/******************************************************************************
 * *
 *这段代码的主要目的是对数据库中的操作进行逐个查询和执行。根据传入的action、event和escalation参数，查询满足条件的操作，并根据操作类型执行相应的操作。同时，更新escalation的状态和下一个检查时间。以下是代码的详细注释：
 *
 *1. 定义函数名和输入参数：`escalation_execute_operations` 函数接收三个参数，分别是 `DB_ESCALATION` 类型的 `escalation`、`DB_EVENT` 类型的 `event` 和 `DB_ACTION` 类型的 `action`。
 *2. 开启日志记录：使用 `zabbix_log` 函数记录函数调用的日志。
 *3. 获取默认的 `esc_period`：如果 `action` 中的 `esc_period` 为0，则使用 `SEC_PER_HOUR` 作为默认值。
 *4. 递增 `escalation` 的 `esc_step`：表示当前执行的操作步数。
 *5. 查询数据库，获取满足条件的操作：使用 `DBselect` 函数查询数据库，根据 `action` 的 `actionid`、`operationtype`（消息或命令）、`esc_step` 和 `recovery` 字段筛选操作。
 *6. 遍历查询结果：使用 `DBfetch` 函数逐行解析查询结果。
 *7. 解析操作类型、评估类型、`esc_period` 等数据：根据操作类型执行相应操作。
 *8. 添加对象消息：将操作执行结果添加到用户消息队列中。
 *9. 检查操作条件是否满足：使用 `check_operation_conditions` 函数检查操作条件是否满足。
 *10. 执行操作：根据操作类型执行相应操作（发送消息或执行命令）。
 *11. 释放查询结果：调用 `DBfree_result` 函数释放查询结果。
 *12. 输出用户消息，并清空用户消息队列：调用 `flush_user_msg` 函数输出用户消息。
 *13. 如果是动作源或内部事件，则查询下一个操作周期：使用 `DBselect` 函数查询下一个操作周期。
 *14. 更新 `escalation` 的下一个检查时间为当前时间加上下一个操作周期。
 *15. 如果是其他事件源，则设置 `escalation` 的 `status` 为 `COMPLETED`。
 *16. 结束日志记录：使用 `zabbix_log` 函数记录函数执行结束的日志。
 ******************************************************************************/
static void escalation_execute_operations(DB_ESCALATION *escalation, const DB_EVENT *event, const DB_ACTION *action)
{
	const char	*__function_name = "escalation_execute_operations";
	DB_RESULT	result;
	DB_ROW		row;
	int		next_esc_period = 0, esc_period, default_esc_period;
	ZBX_USER_MSG	*user_msg = NULL;

	// 开启日志记录，记录函数调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 获取默认的esc_period，如果action中的esc_period为0，则使用SEC_PER_HOUR作为默认值
	default_esc_period = 0 == action->esc_period ? SEC_PER_HOUR : action->esc_period;
	// 递增escalation的esc_step
	escalation->esc_step++;

	// 查询数据库，获取满足条件的操作
	result = DBselect(
			"select o.operationid,o.operationtype,o.esc_period,o.evaltype,m.operationid,m.default_msg,"
				"m.subject,m.message,m.mediatypeid"
			" from operations o"
				" left join opmessage m"
					" on m.operationid=o.operationid"
			" where o.actionid=" ZBX_FS_UI64
				" and o.operationtype in (%d,%d)"
				" and o.esc_step_from<=%d"
				" and (o.esc_step_to=0 or o.esc_step_to>=%d)"
				" and o.recovery=%d",
			action->actionid,
			OPERATION_TYPE_MESSAGE, OPERATION_TYPE_COMMAND,
			escalation->esc_step,
			escalation->esc_step,
			ZBX_OPERATION_MODE_NORMAL);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 将字符串转换为整数
		ZBX_STR2UINT64(operationid, row[0]);

		// 解析操作类型、评估类型、esc_period等数据
		// 如果esc_period为0，或者下一个esc_period大于当前esc_period，则更新next_esc_period为当前esc_period
		if (0 == esc_period || next_esc_period > esc_period)
			next_esc_period = esc_period;

		// 检查操作条件是否满足，如果满足，则执行操作
		if (SUCCEED == check_operation_conditions(event, operationid, (unsigned char)atoi(row[3])))
		{
			char		*subject, *message;
			zbx_uint64_t	mediatypeid;

			// 调试日志
			zabbix_log(LOG_LEVEL_DEBUG, "Conditions match our event. Execute operation.");

			// 根据操作类型执行相应操作
			switch (atoi(row[1]))
			{
				case OPERATION_TYPE_MESSAGE:
					// 如果消息字段为空，则使用默认消息
					if (SUCCEED == DBis_null(row[4]))
						break;

					// 获取媒体类型ID
					ZBX_DBROW2UINT64(mediatypeid, row[8]);

					// 构建消息主题和内容
					subject = row[6];
					message = row[7];

					// 添加对象消息
					add_object_msg(action->actionid, operationid, mediatypeid, &user_msg,
							subject, message, event, NULL, NULL, MACRO_TYPE_MESSAGE_NORMAL);
					break;
				case OPERATION_TYPE_COMMAND:
					// 执行命令
					execute_commands(event, NULL, NULL, action->actionid, operationid,
							escalation->esc_step, MACRO_TYPE_MESSAGE_NORMAL);
					break;
			}
		}
		else
			// 调试日志
			zabbix_log(LOG_LEVEL_DEBUG, "Conditions do not match our event. Do not execute operation.");
	}
	// 释放查询结果
	DBfree_result(result);

	// 输出用户消息，并清空用户消息队列
	flush_user_msg(&user_msg, escalation->esc_step, event, NULL, action->actionid);

	// 如果是动作源或内部事件，则执行以下操作
	if (EVENT_SOURCE_TRIGGERS == action->eventsource || EVENT_SOURCE_INTERNAL == action->eventsource)
	{
		// 查询下一个操作周期
		char	*sql;

		sql = zbx_dsprintf(NULL,
				"select null"
				" from operations"
				" where actionid=" ZBX_FS_UI64
					" and (esc_step_to>%d or esc_step_to=0)"
					" and recovery=%d",
					action->actionid, escalation->esc_step, ZBX_OPERATION_MODE_NORMAL);
		result = DBselectN(sql, 1);

		// 如果下一个操作周期存在，则更新escalation的下一个检查时间为当前时间加上下一个操作周期
		if (NULL != DBfetch(result))
		{
			next_esc_period = (0 != next_esc_period) ? next_esc_period : default_esc_period;
			escalation->nextcheck = time(NULL) + next_esc_period;
		}
		// 否则，根据恢复模式更新escalation的状态和下一个检查时间
		else if (ZBX_ACTION_RECOVERY_OPERATIONS == action->recovery)
		{
			escalation->status = ESCALATION_STATUS_SLEEP;
			escalation->nextcheck = time(NULL) + default_esc_period;
		}
		else
			// 设置 escalation->status 为 COMPLETED
			escalation->status = ESCALATION_STATUS_COMPLETED;

		// 释放查询结果
		DBfree_result(result);
		// 释放 sql 字符串
		zbx_free(sql);
	}
	// 如果是其他事件源，则设置 escalation->status 为 COMPLETED
	else
		escalation->status = ESCALATION_STATUS_COMPLETED;

	// 结束日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

			escalation->nextcheck = time(NULL) + next_esc_period;
		}
		else if (ZBX_ACTION_RECOVERY_OPERATIONS == action->recovery)
		{
			escalation->status = ESCALATION_STATUS_SLEEP;
			escalation->nextcheck = time(NULL) + default_esc_period;
		}
		else
			escalation->status = ESCALATION_STATUS_COMPLETED;

		DBfree_result(result);
		zbx_free(sql);
	}
	else
		escalation->status = ESCALATION_STATUS_COMPLETED;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_execute_recovery_operations                           *
 *                                                                            *
 * Purpose: execute escalation recovery operations                            *
 *                                                                            *
 * Parameters: event      - [IN] the event                                    *
 *             r_event    - [IN] the recovery event                           *
 *             action     - [IN] the action                                   *
 *                                                                            *
 * Comments: Action recovery operations have a single escalation step, so     *
 *           alerts created by escalation recovery operations must have       *
 *           esc_step field set to 1.                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义静态函数 escalation_execute_recovery_operations，参数包括 DB_EVENT 类型的 event、DB_EVENT 类型的 r_event 和 DB_ACTION 类型的 action。
   这个函数的主要目的是处理恢复操作，具体包括发送恢复消息、执行命令等。
*/
static void escalation_execute_recovery_operations(const DB_EVENT *event, const DB_EVENT *r_event,
                                               const DB_ACTION *action)
{
	/* 定义一个字符串指针，用于存储函数名 */
	const char *__function_name = "escalation_execute_recovery_operations";
	DB_RESULT	result;
	DB_ROW		row;
	ZBX_USER_MSG	*user_msg = NULL;
	zbx_uint64_t	operationid;
	unsigned char	operationtype, default_msg;
	char		*subject, *message;
	zbx_uint64_t	mediatypeid;

	/* 打印调试信息，表示进入函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 从数据库中查询操作的相关信息 */
	result = DBselect(
			"select o.operationid,o.operationtype,"
				"m.operationid,m.default_msg,m.subject,m.message,m.mediatypeid"
			" from operations o"
				" left join opmessage m"
					" on m.operationid=o.operationid"
			" where o.actionid=" ZBX_FS_UI64
				" and o.operationtype in (%d,%d,%d)"
				" and o.recovery=%d",
			action->actionid,
			OPERATION_TYPE_MESSAGE, OPERATION_TYPE_COMMAND, OPERATION_TYPE_RECOVERY_MESSAGE,
			ZBX_OPERATION_MODE_RECOVERY);

	/* 遍历查询结果，处理每个操作 */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(operationid, row[0]);
		operationtype = (unsigned char)atoi(row[1]);

		/* 根据操作类型进行不同处理 */
		switch (operationtype)
		{
			case OPERATION_TYPE_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				/* 解析默认消息、主题和媒体类型ID */
				ZBX_STR2UCHAR(default_msg, row[3]);
				ZBX_DBROW2UINT64(mediatypeid, row[6]);

				/* 如果没有设置默认消息，使用原始消息 */
				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					/* 使用关联的短消息和长消息 */
					subject = action->r_shortdata;
					message = action->r_longdata;
				}

				/* 添加对象消息 */
				add_object_msg(action->actionid, operationid, mediatypeid, &user_msg, subject,
						message, event, r_event, NULL, MACRO_TYPE_MESSAGE_RECOVERY);
				break;
			case OPERATION_TYPE_RECOVERY_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				/* 解析默认消息、主题 */
				ZBX_STR2UCHAR(default_msg, row[3]);

				/* 如果没有设置默认消息，使用原始消息 */
				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					/* 使用关联的短消息和长消息 */
					subject = action->r_shortdata;
					message = action->r_longdata;
				}

				/* 添加发送给用户的消息 */
				add_sentusers_msg(&user_msg, action->actionid, event, r_event, subject, message, NULL);
				break;
			case OPERATION_TYPE_COMMAND:
				/* 执行命令 */
				execute_commands(event, r_event, NULL, action->actionid, operationid, 1,
						MACRO_TYPE_MESSAGE_RECOVERY);
				break;
		}
	}
	/* 释放查询结果 */
	DBfree_result(result);
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 escalation 操作后的消息发送和命令执行。具体来说，它完成以下任务：
 *
 *1. 根据传入的 action 和 ack 信息，查询数据库中对应的操作信息。
 *2. 遍历查询结果，根据操作类型（MESSAGE、ACK_MESSAGE 和 COMMAND）进行不同处理。
 *   - 对于 MESSAGE 和 ACK_MESSAGE 类型的操作：
 *     - 如果默认消息为空，使用操作对应的 subject 和 message；
 *     - 否则，使用 action 中的默认短消息和长消息。
 *   
 *   - 对于 COMMAND 类型的操作，执行相应的命令。
 *3. 添加已发送用户消息和确认消息，以便后续发送给目标用户。
 *4. 释放查询结果，避免内存泄漏。
 *5. 刷新用户消息，将其发送给目标用户。
 *6. 记录日志，表示函数执行完毕。
 ******************************************************************************/
/* 定义静态函数 escalation_execute_acknowledge_operations，用于处理 escalation 操作后的消息发送和命令执行
 * 传入参数：
 *   event：DB_EVENT 结构体指针，表示触发的事件
 *   r_event：DB_EVENT 结构体指针，表示恢复的事件
 *   action：DB_ACTION 结构体指针，表示操作信息
 *   ack：DB_ACKNOWLEDGE 结构体指针，表示确认信息
 */
static void escalation_execute_acknowledge_operations(const DB_EVENT *event, const DB_EVENT *r_event,
                                                   const DB_ACTION *action, const DB_ACKNOWLEDGE *ack)
{
	/* 定义变量，用于存储查询结果、行数据、用户消息链表指针等 */
	DB_RESULT	result;
	DB_ROW		row;
	ZBX_USER_MSG	*user_msg = NULL;
	zbx_uint64_t	operationid, mediatypeid;
	unsigned char	operationtype, default_msg;
	char		*subject, *message;

	/* 记录日志，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 执行数据库查询，获取操作信息 */
	result = DBselect(
			"select o.operationid,o.operationtype,m.operationid,m.default_msg,"
				"m.subject,m.message,m.mediatypeid"
			" from operations o"
				" left join opmessage m"
					" on m.operationid=o.operationid"
			" where o.actionid=" ZBX_FS_UI64
				" and o.operationtype in (%d,%d,%d)"
				" and o.recovery=%d",
			action->actionid,
			OPERATION_TYPE_MESSAGE, OPERATION_TYPE_COMMAND, OPERATION_TYPE_ACK_MESSAGE,
			ZBX_OPERATION_MODE_ACK);

	/* 遍历查询结果，处理每个操作 */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(operationid, row[0]);
		operationtype = (unsigned char)atoi(row[1]);

		/* 根据操作类型进行不同处理 */
		switch (operationtype)
		{
			case OPERATION_TYPE_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				ZBX_STR2UCHAR(default_msg, row[3]);
				ZBX_DBROW2UINT64(mediatypeid, row[6]);

				/* 如果默认消息为空，使用操作对应的 subject 和 message */
				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					subject = action->ack_shortdata;
					message = action->ack_longdata;
				}

				/* 添加对象消息 */
				add_object_msg(action->actionid, operationid, mediatypeid, &user_msg, subject,
						message, event, r_event, ack, MACRO_TYPE_MESSAGE_ACK);
				break;
			case OPERATION_TYPE_ACK_MESSAGE:
				if (SUCCEED == DBis_null(row[2]))
					break;

				ZBX_STR2UCHAR(default_msg, row[3]);
				ZBX_DBROW2UINT64(mediatypeid, row[6]);

				/* 如果默认消息为空，使用操作对应的 subject 和 message */
				if (0 == default_msg)
				{
					subject = row[4];
					message = row[5];
				}
				else
				{
					subject = action->ack_shortdata;
					message = action->ack_longdata;
				}

				/* 添加已发送用户消息 */
				add_sentusers_msg(&user_msg, action->actionid, event, r_event, subject, message, ack);
				/* 添加已发送确认消息 */
				add_sentusers_ack_msg(&user_msg, action->actionid, mediatypeid, event, r_event, ack,
						subject, message);
				break;
			case OPERATION_TYPE_COMMAND:
				/* 执行命令 */
				execute_commands(event, r_event, ack, action->actionid, operationid, 1,
						MACRO_TYPE_MESSAGE_ACK);
				break;
		}
	}

	/* 释放查询结果，避免内存泄漏 */
	DBfree_result(result);

	/* 刷新用户消息，将其发送给目标用户 */
	flush_user_msg(&user_msg, 1, event, NULL, action->actionid);

	/* 记录日志，表示函数执行完毕 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

					message = row[5];
				}
				else
				{
					subject = action->ack_shortdata;
					message = action->ack_longdata;
				}

				add_sentusers_msg(&user_msg, action->actionid, event, r_event, subject, message, ack);
				add_sentusers_ack_msg(&user_msg, action->actionid, mediatypeid, event, r_event, ack,
						subject, message);
				break;
			case OPERATION_TYPE_COMMAND:
				execute_commands(event, r_event, ack, action->actionid, operationid, 1,
						MACRO_TYPE_MESSAGE_ACK);
				break;
		}
	}
	DBfree_result(result);

	flush_user_msg(&user_msg, 1, event, NULL, action->actionid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: check_escalation_trigger                                         *
 *                                                                            *
 * Purpose: check whether the escalation trigger and related items, hosts are *
 *          not deleted or disabled.                                          *
 *                                                                            *
 * Parameters: triggerid   - [IN] the id of trigger to check                  *
 *             source      - [IN] the escalation event source                 *
 *             ignore      - [OUT] 1 - the escalation must be ignored because *
 *                                     of dependent trigger being in PROBLEM  *
 *                                     state,                                 *
 *                                 0 - otherwise                              *
 *             error       - [OUT] message in case escalation is cancelled    *
 *                                                                            *
 * Return value: FAIL if dependent trigger is in PROBLEM state                *
 *               SUCCEED otherwise                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是检查升级触发器。它接收一个触发器ID、一个事件源、一个忽略指针和一个错误指针。在代码中，首先检查触发器的状态，如果已禁用或已删除，则返回错误。接下来，根据事件源判断是否需要检查依赖关系。如果需要检查，则获取触发器表达式引用的项目和主机信息，并检查这些项目和主机的状态。如果过程中遇到任何错误，例如项目或主机被禁用，则返回错误。最后，检查触发器依赖关系，并设置忽略值。如果整个过程没有遇到错误，返回成功。
 ******************************************************************************/
/* 静态函数：检查升级触发器 */
static int	check_escalation_trigger(zbx_uint64_t triggerid, unsigned char source, unsigned char *ignore,
                                     char **error)
{
    /* 声明变量 */
    DC_TRIGGER		trigger;
    zbx_vector_uint64_t	functionids, itemids;
    DC_ITEM			*items = NULL;
    DC_FUNCTION		*functions = NULL;
    int				i, errcode, *errcodes = NULL, ret = FAIL;

    /* 获取触发器信息 */
    DCconfig_get_triggers_by_triggerids(&trigger, &triggerid, &errcode, 1);

    /* 判断错误码是否成功 */
    if (SUCCEED != errcode)
    {
        goto out;
    }
    /* 判断触发器状态是否为已禁用或已删除 */
    else if (TRIGGER_STATUS_DISABLED == trigger.status)
    {
        *error = zbx_dsprintf(*error, "trigger \"%s\" disabled.", trigger.description);
        goto out;
    }

    /* 判断事件源是否为触发器事件 */
    if (EVENT_SOURCE_TRIGGERS != source)
    {
        /* 不检查内部触发器事件的依赖关系 */
        ret = SUCCEED;
        goto out;
    }

    /* 获取触发器表达式引用的项目和主机信息 */
    zbx_vector_uint64_create(&functionids);
    zbx_vector_uint64_create(&itemids);

    get_functionids(&functionids, trigger.expression_orig);

    functions = (DC_FUNCTION *)zbx_malloc(functions, sizeof(DC_FUNCTION) * functionids.values_num);
    errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * functionids.values_num);

    DCconfig_get_functions_by_functionids(functions, functionids.values, errcodes, functionids.values_num);

    for (i = 0; i < functionids.values_num; i++)
    {
        if (SUCCEED == errcodes[i])
            zbx_vector_uint64_append(&itemids, functions[i].itemid);
    }

    DCconfig_clean_functions(functions, errcodes, functionids.values_num);
    zbx_free(functions);

    zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_vector_uint64_uniq(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    items = (DC_ITEM *)zbx_malloc(items, sizeof(DC_ITEM) * itemids.values_num);
    errcodes = (int *)zbx_realloc(errcodes, sizeof(int) * itemids.values_num);

    DCconfig_get_items_by_itemids(items, itemids.values, errcodes, itemids.values_num);

    for (i = 0; i < itemids.values_num; i++)
    {
        if (SUCCEED != errcodes[i])
        {
            *error = zbx_dsprintf(*error, "item id:" ZBX_FS_UI64 " deleted.", itemids.values[i]);
            break;
        }

        if (ITEM_STATUS_DISABLED == items[i].status)
        {
            *error = zbx_dsprintf(*error, "item \"%s\" disabled.", items[i].key_orig);
            break;
        }
        if (HOST_STATUS_NOT_MONITORED == items[i].host.status)
        {
            *error = zbx_dsprintf(*error, "host \"%s\" disabled.", items[i].host.host);
            break;
        }
    }

    DCconfig_clean_items(items, errcodes, itemids.values_num);
    zbx_free(items);
    zbx_free(errcodes);

    zbx_vector_uint64_destroy(&itemids);
    zbx_vector_uint64_destroy(&functionids);

    /* 如果错误信息不为空，跳转到out标签 */
    if (NULL != *error)
        goto out;

    /* 检查触发器依赖关系 */
    *ignore = (SUCCEED == DCconfig_check_trigger_dependencies(trigger.triggerid) ? 0 : 1);

    /* 设置返回值 */
    ret = SUCCEED;

out:
    /* 清理触发器信息 */
    DCconfig_clean_triggers(&trigger, &errcode, 1);

    return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的整数参数 result，将其对应的操作结果转换为字符串并返回。输入参数 result 可以是以下五种类型：
 *
 *1. ZBX_ESCALATION_CANCEL：取消操作
 *2. ZBX_ESCALATION_DELETE：删除操作
 *3. ZBX_ESCALATION_SKIP：跳过操作
 *4. ZBX_ESCALATION_PROCESS：处理操作
 *5. ZBX_ESCALATION_SUPPRESS：抑制操作
 *
 *如果 input 不是以上五种类型，则默认返回字符串 \"unknown\"。
 ******************************************************************************/
// 定义一个名为 check_escalation_result_string 的静态常量字符指针函数，该函数接收一个整数参数 result
static const char *check_escalation_result_string(int result)
{
	// 使用 switch 语句根据 result 的值进行分支处理
	switch (result)
	{
		// 分支：result 为 ZBX_ESCALATION_CANCEL
		case ZBX_ESCALATION_CANCEL:
			// 返回字符串 "cancel"
			return "cancel";

		// 分支：result 为 ZBX_ESCALATION_DELETE
		case ZBX_ESCALATION_DELETE:
			// 返回字符串 "delete"
			return "delete";

		// 分支：result 为 ZBX_ESCALATION_SKIP
		case ZBX_ESCALATION_SKIP:
			// 返回字符串 "skip"
			return "skip";

		// 分支：result 为 ZBX_ESCALATION_PROCESS
		case ZBX_ESCALATION_PROCESS:
			// 返回字符串 "process"
			return "process";

		// 分支：result 为 ZBX_ESCALATION_SUPPRESS
		case ZBX_ESCALATION_SUPPRESS:
			// 返回字符串 "suppress"
			return "suppress";
/******************************************************************************
 * *
 *这段代码的主要目的是检查 escalation 是否可以执行。根据不同的事件类型和条件，对 escalation 进行相应的处理，如删除、抑制或跳过。代码首先记录日志，显示 escalation 的状态。然后根据事件类型和条件，检查触发器是否可以执行 escalation，并记录错误信息。接下来，判断动作来源、动作暂停状态、主机维护状态、确认ID是否为0，如果满足条件，则执行相应的操作。最后，如果有跳过标志，则表示有触发器依赖项处于问题状态，稍后处理升级。如果没有错误，则处理升级。在整个过程中，代码不断记录日志，显示处理结果。
 ******************************************************************************/
// 定义一个静态函数，用于检查 escalation 是否可以执行
static int	check_escalation(const DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event, char **error)
{
	/* 定义一个日志标签 */
	const char	*__function_name = "check_escalation";
	/* 定义一个 DC_ITEM 结构体变量 */
	DC_ITEM		item;
	/* 定义错误码和返回值 */
	int		errcode, ret = ZBX_ESCALATION_CANCEL;
	/* 定义维护状态和跳过标志 */
	unsigned char	maintenance = HOST_MAINTENANCE_STATUS_OFF, skip = 0;

	/* 记录日志，显示 escalation 的状态 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" ZBX_FS_UI64 " status:%s",
			__function_name, escalation->escalationid, zbx_escalation_status_string(escalation->status));

	/* 判断事件类型，如果是触发器事件 */
	if (EVENT_OBJECT_TRIGGER == event->object)
	{
		/* 检查触发器是否可以执行 escalation，并记录错误信息 */
		if (SUCCEED != check_escalation_trigger(escalation->triggerid, event->source, &skip, error))
			goto out;

		/* 更新维护状态 */
		maintenance = (ZBX_PROBLEM_SUPPRESSED_TRUE == event->suppressed ? HOST_MAINTENANCE_STATUS_ON :
				HOST_MAINTENANCE_STATUS_OFF);
	}
	/* 如果是内部事件，判断事件类型为物品或LLDRULE */
	else if (EVENT_SOURCE_INTERNAL == event->source)
	{
		if (EVENT_OBJECT_ITEM == event->object || EVENT_OBJECT_LLDRULE == event->object)
		{
			/* 检查物品是否禁用或删除 */
			DCconfig_get_items_by_itemids(&item, &escalation->itemid, &errcode, 1);

			/* 记录错误信息 */
			if (SUCCEED != errcode)
			{
				*error = zbx_dsprintf(*error, "item id:" ZBX_FS_UI64 " deleted.", escalation->itemid);
			}
			else if (ITEM_STATUS_DISABLED == item.status)
			{
				*error = zbx_dsprintf(*error, "item \"%s\" disabled.", item.key_orig);
			}
			else if (HOST_STATUS_NOT_MONITORED == item.host.status)
			{
				*error = zbx_dsprintf(*error, "host \"%s\" disabled.", item.host.host);
			}
			else
				maintenance = item.host.maintenance_status;

			/* 清理物品信息 */
			DCconfig_clean_items(&item, &errcode, 1);

			if (NULL != *error)
				goto out;
		}
	}

	/* 判断动作来源、动作暂停状态、主机维护状态、确认ID是否为0，如果满足条件，则执行以下操作：
	 * 1. 删除创建并恢复的暂停升级
	 * 2. 抑制在维护期间创建的暂停升级，直到维护结束或升级被恢复
	 */
	if (EVENT_SOURCE_TRIGGERS == action->eventsource &&
			ACTION_PAUSE_SUPPRESSED_TRUE == action->pause_suppressed &&
			HOST_MAINTENANCE_STATUS_ON == maintenance &&
			escalation->acknowledgeid == 0)
	{
		/* 如果是升级步骤为0且恢复事件ID不为0，则删除升级 */
		if (0 == escalation->esc_step && 0 != escalation->r_eventid)
		{
			ret = ZBX_ESCALATION_DELETE;
			goto out;
		}

		/* 如果是恢复事件ID为0，则抑制升级，直到维护结束或升级被恢复 */
		if (0 == escalation->r_eventid)
		{
			ret = ZBX_ESCALATION_SUPPRESS;
			goto out;
		}
	}

	/* 如果有跳过标志，则表示有触发器依赖项处于问题状态，稍后处理升级 */
	if (0 != skip)
	{
		ret = ZBX_ESCALATION_SKIP;
		goto out;
	}

	/* 如果没有错误，则处理升级 */
	ret = ZBX_ESCALATION_PROCESS;

out:

	/* 记录日志，显示处理结果 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __function_name, check_escalation_result_string(ret),
			ZBX_NULL2EMPTY_STR(*error));

	/* 返回处理结果 */
	return ret;
}

		/* during maintenance period                                 */
		if (0 == escalation->esc_step && 0 != escalation->r_eventid)
		{
			ret = ZBX_ESCALATION_DELETE;
			goto out;
		}

		/* suppress paused escalations created before maintenance period */
		/* until maintenance ends or the escalations are recovered       */
		if (0 == escalation->r_eventid)
		{
			ret = ZBX_ESCALATION_SUPPRESS;
			goto out;
		}
	}

	if (0 != skip)
	{
		/* one of trigger dependencies is in PROBLEM state, process escalation later */
		ret = ZBX_ESCALATION_SKIP;
		goto out;
	}

	ret = ZBX_ESCALATION_PROCESS;
out:

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __function_name, check_escalation_result_string(ret),
			ZBX_NULL2EMPTY_STR(*error));


	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_log_cancel_warning                                    *
 *                                                                            *
 * Purpose: write escalation cancellation warning message into log file       *
 *                                                                            *
 * Parameters: escalation - [IN] the escalation                               *
 *             error      - [IN] the error message                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：判断 DB_ESCALATION 结构体中的 escal_step 字段是否不为零，如果满足条件，则使用 zabbix_log 函数记录一条警告日志，日志内容为 \"escalation cancelled: %s\"，其中 %s 位置由 error 参数填充。
 ******************************************************************************/
// 定义一个静态函数，用于取消 escalation 警告
static void escalation_log_cancel_warning(const DB_ESCALATION *escalation, const char *error)
{
    // 判断 escalation 结构体中的 escal_step 字段是否不为零
    if (0 != escalation->esc_step)
    {
        // 使用 zabbix_log 函数记录日志，日志级别为警告，日志内容为 "escalation cancelled: %s"，其中 %s 位置由 error 参数填充
        zabbix_log(LOG_LEVEL_WARNING, "escalation cancelled: %s", error);
    }
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是：
 *
 *1. 检查 DB_ESCALATION 类型的指针 escalation 中的 esc_step 是否不为 0，如果不为 0，则表示有未完成的 escalation 步骤需要处理。
 *2. 生成一条用户消息，内容包括错误信息和行动详细数据，并将消息添加到用户消息队列中。
 *3. 发送用户消息，将其推送给用户。
 *4. 记录 escalation 取消警告。
 *5. 修改 escalation 的状态为 ESCALATION_STATUS_COMPLETED。
 *6. 记录函数执行结束的调试信息。
 ******************************************************************************/
/* 定义一个静态函数 escalation_cancel，接收 4 个参数：
 * DB_ESCALATION 类型的指针 escalation，
 * const DB_ACTION 类型的指针 action，
 * const DB_EVENT 类型的指针 event，
 * 以及一个 const char 类型的指针 error。
 */
static void escalation_cancel(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event, const char *error)
{
	/* 定义一个 const char 类型的变量 __function_name，用于存储函数名 */
	const char *__function_name = "escalation_cancel";
	/* 定义一个 ZBX_USER_MSG 类型的指针 user_msg，用于存储用户消息 */
	ZBX_USER_MSG *user_msg = NULL;

	/* 使用 zabbix_log 记录调试信息，内容包括：
	 * 函数名、 escalationid、 escalation 的状态（zbx_escalation_status_string 转换为字符串）
	 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" ZBX_FS_UI64 " status:%s", __function_name, escalation->escalationid, zbx_escalation_status_string(escalation->status));

	/* 如果 escalation 的 esc_step 不为 0，说明有步骤需要处理 */
	if (0 != escalation->esc_step)
	{
		/* 分配一个 char 类型的内存空间，用于存储错误信息和行动详细数据 */
		char *message;

		/* 使用 zbx_dsprintf 格式化字符串，内容包括：
		 * 错误信息、行动详细数据
		 */
		message = zbx_dsprintf(NULL, "NOTE: Escalation cancelled: %s\
%s", error, action->longdata);

		/* 添加用户消息，内容包括：
		 * 行动 ID、事件、来源、行动简短数据、错误信息和详细数据
		 */
		add_sentusers_msg(&user_msg, action->actionid, event, NULL, action->shortdata, message, NULL);

		/* 刷新用户消息，将其发送给用户 */
		flush_user_msg(&user_msg, escalation->esc_step, event, NULL, action->actionid);

		/* 释放 message 内存 */
		zbx_free(message);
	}

	/* 记录 escalation 取消警告 */
	escalation_log_cancel_warning(escalation, error);

	/* 修改 escalation 的状态为 ESCALATION_STATUS_COMPLETED */
	escalation->status = ESCALATION_STATUS_COMPLETED;

	/* 使用 zabbix_log 记录调试信息，表示函数执行结束 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		flush_user_msg(&user_msg, escalation->esc_step, event, NULL, action->actionid);

		zbx_free(message);
	}

	escalation_log_cancel_warning(escalation, error);
	escalation->status = ESCALATION_STATUS_COMPLETED;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: escalation_execute                                               *
 *                                                                            *
 * Purpose: execute next escalation step                                      *
 *                                                                            *
 * Parameters: escalation - [IN/OUT] the escalation to execute                *
 *             action     - [IN]     the action                               *
 *             event      - [IN]     the event                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是执行 escalation、event 和 action 对应的操作。首先，通过 zabbix_log 函数记录调试日志，输出函数名、escalationid 和 escalation 的状态信息。然后调用 escalation_execute_operations 函数执行相关操作。最后，再次使用 zabbix_log 函数记录调试日志，输出函数执行结束的信息。
 ******************************************************************************/
// 定义一个名为 escalation_execute 的静态函数，参数为一个 DB_ESCALATION 类型的指针、一个 DB_ACTION 类型的指针和一个 DB_EVENT 类型的指针
static void escalation_execute(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event)
{
    // 定义一个名为 __function_name 的常量字符指针，指向当前函数名
    const char *__function_name = "escalation_execute";

    // 使用 zabbix_log 函数记录调试日志，输出函数名、 escalationid 和 escalation 的状态信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" ZBX_FS_UI64 " status:%s",
               __function_name, escalation->escalationid, zbx_escalation_status_string(escalation->status));

    // 调用 escalation_execute_operations 函数，执行 escalation、event 和 action 对应的操作
    escalation_execute_operations(escalation, event, action);

    // 使用 zabbix_log 函数记录调试日志，输出函数执行结束的信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 escalation（升级）的恢复操作。首先，记录进入函数的日志，传入的参数包括 escalationid（升级ID）、状态等。然后调用 escalation_execute_recovery_operations 函数执行恢复操作。接着，将 escalation 的状态设置为 ESCALATION_STATUS_COMPLETED，表示恢复操作已完成。最后，记录结束函数的日志。
 ******************************************************************************/
/* 定义一个名为 escalation_recover 的静态函数，参数为一个 DB_ESCALATION 类型的指针、一个 DB_ACTION 类型的指针、一个 DB_EVENT 类型的指针和一个 DB_EVENT 类型的指针。
*/
static void	escalation_recover(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event,
                                const DB_EVENT *r_event)
{
	/* 定义一个名为 __function_name 的常量字符指针，指向当前函数名。
	*/
	const char	*__function_name = "escalation_recover";

	/* 使用 zabbix_log 函数记录日志，日志级别为 DEBUG，日志内容为：进入 escalation_recover 函数， escalationid：，状态：。
	*/
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" ZBX_FS_UI64 " status:%s",
			__function_name, escalation->escalationid, zbx_escalation_status_string(escalation->status));
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 escalation 对象的确认操作。首先，打印调试信息表示函数的开始。然后，查询 acknowedges 表中指定 acknowledgeid 的事件信息。如果查询结果不为空，则解析查询结果并填充 DB_ACKNOWLEDGE 结构体变量 ack。接着，调用 escalation_execute_acknowledge_operations 函数执行确认操作。完成后，释放查询结果，更新 escalation 的状态为 ESCALATION_STATUS_COMPLETED，并打印调试信息表示函数的结束。
 ******************************************************************************/
// 定义一个静态函数 escalation_acknowledge，接收 4 个参数：
// escalation： escalation 结构体的指针，用于存储 escalation 信息
// action： DB_ACTION 结构体的指针，用于存储动作信息
// event： DB_EVENT 结构体的指针，用于存储事件信息
// r_event： DB_EVENT 结构体的指针，用于存储原始事件信息
static void escalation_acknowledge(DB_ESCALATION *escalation, const DB_ACTION *action, const DB_EVENT *event,
                                 const DB_EVENT *r_event)
{
    // 定义一个字符串指针 __function_name，用于存储函数名
    const char *__function_name = "escalation_acknowledge";

    // 定义一个 DB_ROW 结构体变量 row，用于存储数据库查询结果
    DB_ROW row;

    // 定义一个 DB_RESULT 结构体变量 result，用于存储数据库操作结果
    DB_RESULT result;

    // 使用 zabbix_log 打印调试信息，表示 escalation_acknowledge 函数的开始
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:%s acknowledgeid:%s status:%s",
               __function_name, escalation->escalationid, escalation->acknowledgeid,
               zbx_escalation_status_string(escalation->status));

    // 执行 DBselect 操作，查询 acknowedges 表中指定 acknowledgeid 的事件信息
    result = DBselect(
            "select message,userid,clock,action,old_severity,new_severity from acknowledges"
            " where acknowledgeid=" ZBX_FS_UI64,
            escalation->acknowledgeid);

    // 如果查询结果不为空，则获取查询结果的第一行数据
    if (NULL != (row = DBfetch(result)))
    {
        // 定义一个 DB_ACKNOWLEDGE 结构体变量 ack，用于存储查询结果
        DB_ACKNOWLEDGE ack;

        // 解析查询结果，填充 ack 结构体
        ack.message = row[0];
        ZBX_STR2UINT64(ack.userid, row[1]);
        ack.clock = atoi(row[2]);
        ack.acknowledgeid = escalation->acknowledgeid;
        ack.action = atoi(row[3]);
        ack.old_severity = atoi(row[4]);
        ack.new_severity = atoi(row[5]);

        // 调用 escalation_execute_acknowledge_operations 函数，执行确认操作
        escalation_execute_acknowledge_operations(event, r_event, action, &ack);
    }

    // 释放查询结果
    DBfree_result(result);

    // 更新 escalation 的状态为 ESCALATION_STATUS_COMPLETED
    escalation->status = ESCALATION_STATUS_COMPLETED;

    // 使用 zabbix_log 打印调试信息，表示 escalation_acknowledge 函数的结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		const DB_EVENT *r_event)
{
	const char	*__function_name = "escalation_acknowledge";

	DB_ROW		row;
	DB_RESULT	result;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() escalationid:" ZBX_FS_UI64 " acknowledgeid:" ZBX_FS_UI64 " status:%s",
			__function_name, escalation->escalationid, escalation->acknowledgeid,
			zbx_escalation_status_string(escalation->status));

	result = DBselect(
			"select message,userid,clock,action,old_severity,new_severity from acknowledges"
			" where acknowledgeid=" ZBX_FS_UI64,
			escalation->acknowledgeid);

	if (NULL != (row = DBfetch(result)))
	{
		DB_ACKNOWLEDGE	ack;

		ack.message = row[0];
		ZBX_STR2UINT64(ack.userid, row[1]);
		ack.clock = atoi(row[2]);
		ack.acknowledgeid = escalation->acknowledgeid;
		ack.action = atoi(row[3]);
		ack.old_severity = atoi(row[4]);
		ack.new_severity = atoi(row[5]);

		escalation_execute_acknowledge_operations(event, r_event, action, &ack);
	}

	DBfree_result(result);

	escalation->status = ESCALATION_STATUS_COMPLETED;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

typedef struct
{
	zbx_uint64_t		escalationid;

	int			nextcheck;
	int			esc_step;
	zbx_escalation_status_t	status;

#define ZBX_DIFF_ESCALATION_UNSET			__UINT64_C(0x0000)
#define ZBX_DIFF_ESCALATION_UPDATE_NEXTCHECK		__UINT64_C(0x0001)
#define ZBX_DIFF_ESCALATION_UPDATE_ESC_STEP		__UINT64_C(0x0002)
#define ZBX_DIFF_ESCALATION_UPDATE_STATUS		__UINT64_C(0x0004)
#define ZBX_DIFF_ESCALATION_UPDATE 								\
		(ZBX_DIFF_ESCALATION_UPDATE_NEXTCHECK | ZBX_DIFF_ESCALATION_UPDATE_ESC_STEP |	\
		ZBX_DIFF_ESCALATION_UPDATE_STATUS)
	zbx_uint64_t		flags;
}
zbx_escalation_diff_t;

/******************************************************************************
 * *
 *这块代码的主要目的是创建一个名为 `escalation_create_diff` 的函数，该函数接收一个 `DB_ESCALATION` 类型的指针作为参数。函数内部首先分配一块内存空间用于存储差异数据结构，然后将传入的 `DB_ESCALATION` 结构中的数据拷贝到新的差异数据结构中，并返回该差异数据结构的指针。整个函数的作用是将数据库中的 escalation 数据转换为差异数据结构，以便于后续的处理和操作。
 ******************************************************************************/
// 定义一个函数，用于创建 escalation 的差异数据结构
static zbx_escalation_diff_t *escalation_create_diff(const DB_ESCALATION *escalation)
{
	// 定义一个指向差异数据结构的指针 diff
	zbx_escalation_diff_t	*diff;

	// 分配一块内存空间，用于存储差异数据结构
	diff = (zbx_escalation_diff_t *)zbx_malloc(NULL, sizeof(zbx_escalation_diff_t));
	
	// 拷贝 escalation 结构中的数据到差异数据结构中
	diff->escalationid = escalation->escalationid;
	diff->nextcheck = escalation->nextcheck;
	diff->esc_step = escalation->esc_step;
	diff->status = escalation->status;
	diff->flags = ZBX_DIFF_ESCALATION_UNSET;

	// 返回创建好的差异数据结构指针
	return diff;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是比较 `escalation` 结构体和 `diff` 结构体中的数据，如果发现有差异，则更新 `diff` 结构体中的相应字段，并设置相应的标志位。输出结果为一个更新后的 `diff` 结构体。
 ******************************************************************************/
/* 定义一个静态函数，用于更新 escalation（升级）差异数据 */
static void escalation_update_diff(const DB_ESCALATION *escalation, zbx_escalation_diff_t *diff)
{
    /* 判断 nextcheck 字段是否需要更新 */
    if (escalation->nextcheck != diff->nextcheck)
    {
        /* 更新 diff 结构的 nextcheck 字段 */
        diff->nextcheck = escalation->nextcheck;
        /* 设置 ZBX_DIFF_ESCALATION_UPDATE_NEXTCHECK 标志位，表示 nextcheck 已更新 */
        diff->flags |= ZBX_DIFF_ESCALATION_UPDATE_NEXTCHECK;
    }

    /* 判断 esc_step 字段是否需要更新 */
    if (escalation->esc_step != diff->esc_step)
    {
        /* 更新 diff 结构的 esc_step 字段 */
        diff->esc_step = escalation->esc_step;
        /* 设置 ZBX_DIFF_ESCALATION_UPDATE_ESC_STEP 标志位，表示 esc_step 已更新 */
        diff->flags |= ZBX_DIFF_ESCALATION_UPDATE_ESC_STEP;
    }

    /* 判断 status 字段是否需要更新 */
    if (escalation->status != diff->status)
    {
        /* 更新 diff 结构的 status 字段 */
        diff->status = escalation->status;
        /* 设置 ZBX_DIFF_ESCALATION_UPDATE_STATUS 标志位，表示 status 已更新 */
        diff->flags |= ZBX_DIFF_ESCALATION_UPDATE_STATUS;
    }
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历escalations向量中的每个元素，将其确认ID不为0的事件添加到ack_eventids向量中。然后，如果ack_eventids向量中的元素数量大于0，从数据库中获取对应的r_eventids，并将r_eventids向量中的元素添加到eventids向量中。最后，释放内存。
 ******************************************************************************/
// 定义一个静态函数，用于添加确认和 escalation 事件ID
static void add_ack_escalation_r_eventids(zbx_vector_ptr_t *escalations, zbx_vector_uint64_t *eventids, zbx_vector_uint64_pair_t *event_pairs)
{
	// 定义变量
	int i;
	zbx_vector_uint64_t ack_eventids, r_eventids;

	// 创建两个vector，一个用于存储确认事件ID，另一个用于存储r_eventids
	zbx_vector_uint64_create(&ack_eventids);
	zbx_vector_uint64_create(&r_eventids);

	// 遍历escalations中的每个元素
	for (i = 0; i < escalations->values_num; i++)
	{
		// 获取escalation结构体指针
		DB_ESCALATION *escalation;

		// 解引用escalations->values[i]，获取escalation结构体
		escalation = (DB_ESCALATION *)escalations->values[i];

		// 如果确认ID不为0，将其添加到ack_eventids中
		if (0 != escalation->acknowledgeid)
			zbx_vector_uint64_append(&ack_eventids, escalation->eventid);
	}

	// 如果ack_eventids中的元素数量大于0，执行以下操作：
	if (0 < ack_eventids.values_num)
	{
		// 从数据库中获取ack_eventids与r_eventid的对应关系
		zbx_db_get_eventid_r_eventid_pairs(&ack_eventids, event_pairs, &r_eventids);

		// 如果r_eventids中的元素数量大于0，将其添加到eventids中
		if (0 < r_eventids.values_num)
			zbx_vector_uint64_append_array(eventids, r_eventids.values, r_eventids.values_num);
	}

	// 释放内存
	zbx_vector_uint64_destroy(&ack_eventids);
	zbx_vector_uint64_destroy(&r_eventids);
}

		if (0 != escalation->acknowledgeid)
			zbx_vector_uint64_append(&ack_eventids, escalation->eventid);
	}

	if (0 < ack_eventids.values_num)
	{
		zbx_db_get_eventid_r_eventid_pairs(&ack_eventids, event_pairs, &r_eventids);

		if (0 < r_eventids.values_num)
			zbx_vector_uint64_append_array(eventids, r_eventids.values, r_eventids.values_num);
	}

	zbx_vector_uint64_destroy(&ack_eventids);
	zbx_vector_uint64_destroy(&r_eventids);
}

/******************************************************************************
 * 这
 ******************************************************************************/段C语言代码的主要目的是处理数据库中的 escalations（升级），具体包括以下几个步骤：

1. 初始化变量，包括 escalations、diffs、actions、events 等。
2. 遍历传入的 escalations 数组，对每个 escalation 进行检查。
3. 检查 action 的状态，如果已启用，则继续检查相关事件和升级。
4. 根据检查结果，对升级进行相应的处理，如取消、删除、跳过、执行等。
5. 将处理后的升级保存到 diffs 数组中。
6. 如果有新的升级，则更新 escalations 数据库表。
7. 删除已取消或完成的升级。
8. 释放内存，清理变量。

整个代码块的主要目的是对数据库中的升级进行处理，包括检查、更新和删除等操作。输出结果为处理后的升级数量。

/******************************************************************************
 *                                                                            *
 * Function: process_escalations                                              *
 *                                                                            *
 * Purpose: execute escalation steps and recovery operations;                 *
 *          postpone escalations during maintenance and due to trigger dep.;  *
 *          delete completed escalations from the database;                   *
 *          cancel escalations due to changed configuration, etc.             *
 *                                                                            *
 * Parameters: now               - [IN] the current time                      *
 *             nextcheck         - [IN/OUT] time of the next invocation       *
 *             escalation_source - [IN] type of escalations to be handled     *
 *                                                                            *
 * Return value: the count of deleted escalations                             *
 *                                                                            *
 * Comments: actions.c:process_actions() creates pseudo-escalations also for  *
 *           EVENT_SOURCE_DISCOVERY, EVENT_SOURCE_AUTO_REGISTRATION events,   *
 *           this function handles message and command operations for these   *
 *           events while host, group, template operations are handled        *
 *           in process_actions().                                            *
 *                                                                            *
 ******************************************************************************/
static int	process_escalations(int now, int *nextcheck, unsigned int escalation_source)
{
	const char		*__function_name = "process_escalations";

	int			ret = 0;
	DB_RESULT		result;
	DB_ROW			row;
	char			*filter = NULL;
	size_t			filter_alloc = 0, filter_offset = 0;

	zbx_vector_ptr_t	escalations;
	zbx_vector_uint64_t	actionids, eventids;

	DB_ESCALATION		*escalation;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_ptr_create(&escalations);
	zbx_vector_uint64_create(&actionids);
	zbx_vector_uint64_create(&eventids);

	/* Selection of escalations to be processed:                                                          */
	/*                                                                                                    */
	/* e - row in escalations table, E - escalations table, S - ordered* set of escalations to be proc.   */
	/*                                                                                                    */
	/* ZBX_ESCALATION_SOURCE_TRIGGER: S = {e in E | e.triggerid    mod process_num == 0}                  */
	/* ZBX_ESCALATION_SOURCE_ITEM::   S = {e in E | e.itemid       mod process_num == 0}                  */
	/* ZBX_ESCALATION_SOURCE_DEFAULT: S = {e in E | e.escalationid mod process_num == 0}                  */
	/*                                                                                                    */
	/* Note that each escalator always handles all escalations from the same triggers and items.          */
	/* The rest of the escalations (e.g. not trigger or item based) are spread evenly between escalators. */
	/*                                                                                                    */
	/* * by e.actionid, e.triggerid, e.itemid, e.escalationid                                             */
	switch (escalation_source)
	{
		case ZBX_ESCALATION_SOURCE_TRIGGER:
			zbx_strcpy_alloc(&filter, &filter_alloc, &filter_offset, "triggerid is not null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " ZBX_SQL_MOD(triggerid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
		case ZBX_ESCALATION_SOURCE_ITEM:
			zbx_strcpy_alloc(&filter, &filter_alloc, &filter_offset, "triggerid is null and"
					" itemid is not null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " ZBX_SQL_MOD(itemid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
		case ZBX_ESCALATION_SOURCE_DEFAULT:
			zbx_strcpy_alloc(&filter, &filter_alloc, &filter_offset,
					"triggerid is null and itemid is null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " ZBX_SQL_MOD(escalationid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
	}

	result = DBselect("select escalationid,actionid,triggerid,eventid,r_eventid,nextcheck,esc_step,status,itemid,"
					"acknowledgeid"
				" from escalations"
				" where %s and nextcheck<=%d"
				" order by actionid,triggerid,itemid,escalationid", filter,
				now + CONFIG_ESCALATOR_FREQUENCY);
	zbx_free(filter);

	while (NULL != (row = DBfetch(result)) && ZBX_IS_RUNNING())
	{
		int	esc_nextcheck;

		esc_nextcheck = atoi(row[5]);

		/* skip escalations that must be checked in next CONFIG_ESCALATOR_FREQUENCY period */
		if (esc_nextcheck > now)
		{
			if (esc_nextcheck < *nextcheck)
				*nextcheck = esc_nextcheck;

			continue;
		}

		escalation = (DB_ESCALATION *)zbx_malloc(NULL, sizeof(DB_ESCALATION));
		escalation->nextcheck = esc_nextcheck;
		ZBX_DBROW2UINT64(escalation->r_eventid, row[4]);
		ZBX_STR2UINT64(escalation->escalationid, row[0]);
		ZBX_STR2UINT64(escalation->actionid, row[1]);
		ZBX_DBROW2UINT64(escalation->triggerid, row[2]);
		ZBX_DBROW2UINT64(escalation->eventid, row[3]);
		escalation->esc_step = atoi(row[6]);
		escalation->status = atoi(row[7]);
		ZBX_DBROW2UINT64(escalation->itemid, row[8]);
		ZBX_DBROW2UINT64(escalation->acknowledgeid, row[9]);

		zbx_vector_ptr_append(&escalations, escalation);
		zbx_vector_uint64_append(&actionids, escalation->actionid);
		zbx_vector_uint64_append(&eventids, escalation->eventid);

		if (0 < escalation->r_eventid)
			zbx_vector_uint64_append(&eventids, escalation->r_eventid);

		if (escalations.values_num >= ZBX_ESCALATIONS_PER_STEP)
		{
			ret += process_db_escalations(now, nextcheck, &escalations, &eventids, &actionids);
			zbx_vector_ptr_clear_ext(&escalations, zbx_ptr_free);
			zbx_vector_uint64_clear(&actionids);
			zbx_vector_uint64_clear(&eventids);
		}
	}
	DBfree_result(result);

	if (0 < escalations.values_num)
	{
		ret += process_db_escalations(now, nextcheck, &escalations, &eventids, &actionids);
		zbx_vector_ptr_clear_ext(&escalations, zbx_ptr_free);
	}

	zbx_vector_ptr_destroy(&escalations);
	zbx_vector_uint64_destroy(&actionids);
	zbx_vector_uint64_destroy(&eventids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret; /* performance metric */
}

/******************************************************************************
 *                                                                            *
 * Function: main_escalator_loop                                              *
 *                                                                            *
 * Purpose: periodically check table escalations and generate alerts          *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(escalator_thread, args)
{
	int	now, nextcheck, sleeptime = -1, escalations_count = 0, old_escalations_count = 0;
	double	sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t	last_stat_time;

/******************************************************************************
 * *
 *这个代码块的主要目的是处理 escalation_source 类型的升级。首先，根据不同的 escalation_source 类型，设置相应的过滤器。然后，执行数据库查询，获取满足条件的升级信息。接着，逐个处理查询结果中的升级，将其添加到升级列表、动作列表和事件列表中。当处理完一定数量的升级后，调用 process_db_escalations 函数处理这些升级。最后，释放所有分配的内存，并返回处理过的升级总数。
 ******************************************************************************/
static int process_escalations(int now, int *nextcheck, unsigned int escalation_source)
{
	// 定义常量、变量和结构体
	const char *__function_name = "process_escalations";
	int			ret = 0;
	DB_RESULT		result;
	DB_ROW			row;
	char			*filter = NULL;
	size_t			filter_alloc = 0, filter_offset = 0;

	zbx_vector_ptr_t	escalations;
	zbx_vector_uint64_t	actionids, eventids;

	DB_ESCALATION		*escalation;

	// 调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化数据结构
	zbx_vector_ptr_create(&escalations);
	zbx_vector_uint64_create(&actionids);
	zbx_vector_uint64_create(&eventids);

	/* 选择待处理的升级：
	 * ZBX_ESCALATION_SOURCE_TRIGGER：基于触发器的升级
	 * ZBX_ESCALATION_SOURCE_ITEM：基于物品的升级
	 * ZBX_ESCALATION_SOURCE_DEFAULT：默认升级
	 */
	switch (escalation_source)
	{
		case ZBX_ESCALATION_SOURCE_TRIGGER:
			// 分配并设置过滤器
			zbx_strcpy_alloc(&filter, &filter_alloc, &filter_offset, "triggerid is not null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " ZBX_SQL_MOD(triggerid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
		case ZBX_ESCALATION_SOURCE_ITEM:
			// 分配并设置过滤器
			zbx_strcpy_alloc(&filter, &filter_alloc, &filter_offset, "triggerid is null and"
					" itemid is not null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " ZBX_SQL_MOD(itemid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
		case ZBX_ESCALATION_SOURCE_DEFAULT:
			// 分配并设置过滤器
			zbx_strcpy_alloc(&filter, &filter_alloc, &filter_offset,
					"triggerid is null and"
					" itemid is null");
			if (1 < CONFIG_ESCALATOR_FORKS)
			{
				zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						" and " ZBX_SQL_MOD(escalationid, %d) "=%d",
						CONFIG_ESCALATOR_FORKS, process_num - 1);
			}
			break;
	}

	// 执行数据库查询
	result = DBselect("select escalationid,actionid,triggerid,eventid,r_eventid,nextcheck,esc_step,status,itemid,"
				"acknowledgeid"
			" from escalations"
			" where %s and nextcheck<=%d"
			" order by actionid,triggerid,itemid,escalationid", filter,
			now + CONFIG_ESCALATOR_FREQUENCY);
	// 释放过滤器
	zbx_free(filter);

	// 处理查询结果
	while (NULL != (row = DBfetch(result)) && ZBX_IS_RUNNING())
	{
		int	esc_nextcheck;

		esc_nextcheck = atoi(row[5]);

		// 跳过下一个检查时间大于现在的升级
		if (esc_nextcheck > now)
		{
			if (esc_nextcheck < *nextcheck)
				*nextcheck = esc_nextcheck;

			continue;
		}

		// 分配并填充升级结构体
		escalation = (DB_ESCALATION *)zbx_malloc(NULL, sizeof(DB_ESCALATION));
		escalation->nextcheck = esc_nextcheck;
		ZBX_DBROW2UINT64(escalation->r_eventid, row[4]);
		ZBX_STR2UINT64(escalation->escalationid, row[0]);
		ZBX_STR2UINT64(escalation->actionid, row[1]);
		ZBX_DBROW2UINT64(escalation->triggerid, row[2]);
		ZBX_DBROW2UINT64(escalation->eventid, row[3]);
		escalation->esc_step = atoi(row[6]);
		escalation->status = atoi(row[7]);
		ZBX_DBROW2UINT64(escalation->itemid, row[8]);
		ZBX_DBROW2UINT64(escalation->acknowledgeid, row[9]);

		// 将升级添加到升级列表
		zbx_vector_ptr_append(&escalations, escalation);
		// 将升级关联的动作、触发器和事件添加到相应的事件列表
		zbx_vector_uint64_append(&actionids, escalation->actionid);
		zbx_vector_uint64_append(&eventids, escalation->eventid);

		// 如果升级关联的事件ID大于0，则将其添加到事件ID列表
		if (escalation->r_eventid > 0)
			zbx_vector_uint64_append(&eventids, escalation->r_eventid);

		// 处理达到最大数量的升级
		if (escalations.values_num >= ZBX_ESCALATIONS_PER_STEP)
		{
			ret += process_db_escalations(now, nextcheck, &escalations, &eventids, &actionids);
			// 重置升级列表、事件ID列表和动作列表
			zbx_vector_ptr_clear_ext(&escalations, zbx_ptr_free);
			zbx_vector_uint64_clear(&actionids);
			zbx_vector_uint64_clear(&eventids);
		}
	}
	// 释放查询结果
	DBfree_result(result);

	// 处理剩余的升级
	if (0 < escalations.values_num)
	{
		ret += process_db_escalations(now, nextcheck, &escalations, &eventids, &actionids);
	}

	// 释放升级、动作和事件列表
	zbx_vector_ptr_destroy(&escalations);
	zbx_vector_uint64_destroy(&actionids);
	zbx_vector_uint64_destroy(&eventids);

	// 结束调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret; /* 性能指标 */
}

    }

    // 计算下次检查时间
    nextcheck = time(NULL) + CONFIG_ESCALATOR_FREQUENCY;

    // 处理 escalations
    escalations_count += process_escalations(time(NULL), &nextcheck, ZBX_ESCALATION_SOURCE_TRIGGER);
    escalations_count += process_escalations(time(NULL), &nextcheck, ZBX_ESCALATION_SOURCE_ITEM);
    escalations_count += process_escalations(time(NULL), &nextcheck, ZBX_ESCALATION_SOURCE_DEFAULT);

    // 累计处理时间
    total_sec += zbx_time() - sec;

    // 计算睡眠时间
    sleeptime = calculate_sleeptime(nextcheck, CONFIG_ESCALATOR_FREQUENCY);

    // 获取当前时间
    now = time(NULL);

    // 如果睡眠时间不为0或状态更新间隔时间到达，更新状态
    if (0 != sleeptime || STAT_INTERVAL <= now - last_stat_time)
    {
        if (0 == sleeptime)
        {
            // 设置进程标题，显示处理过的 escalations 数量和总时间
            zbx_setproctitle("%s #%d [processed %d escalations in " ZBX_FS_DBL
                            " sec, processing escalations]", get_process_type_string(process_type),
                            process_num, escalations_count, total_sec);
        }
        else
        {
            // 设置进程标题，显示处理过的 escalations 数量、总时间和空闲时间
            zbx_setproctitle("%s #%d [processed %d escalations in " ZBX_FS_DBL " sec, idle %d sec]",
                            get_process_type_string(process_type), process_num, escalations_count,
                            total_sec, sleeptime);

            // 记录旧的 escalations 数量和总时间
            old_escalations_count = escalations_count;
            old_total_sec = total_sec;
        }

        // 清空 escalations 数量和总时间
        escalations_count = 0;
        total_sec = 0.0;
        last_stat_time = now;
    }

    // 睡眠
    zbx_sleep_loop(sleeptime);
}

// 设置进程标题，显示进程已终止
zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

// 循环等待，直到程序退出
while (1)
    zbx_sleep(SEC_PER_MIN);

