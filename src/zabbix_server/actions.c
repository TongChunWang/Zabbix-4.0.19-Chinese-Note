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
#include "zbxserver.h"

#include "actions.h"
#include "operations.h"
#include "events.h"

/******************************************************************************
 *                                                                            *
 * Function: check_condition_event_tag                                        *
 *                                                                            *
 * Purpose: check event tag condition                                         *
 *                                                                            *
 * Parameters: event     - the event                                          *
 *             condition - condition for matching                             *
 *                                                                            *
 * Return value: SUCCEED - matches, FAIL - otherwise                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查事件标签是否满足给定的条件。函数`check_condition_event_tag`接收两个参数，分别是事件指针`event`和条件指针`condition`。函数首先判断条件操作符是否为不等于或不相似，如果是，则继续执行的返回值为成功。接着初始化返回值，并遍历事件中的标签。对于每个标签，调用`zbx_strmatch_condition`函数判断标签是否满足条件。最后返回最终的返回值。
 ******************************************************************************/
// 定义一个函数，用于检查事件标签是否满足条件
static int	check_condition_event_tag(const DB_EVENT *event, const DB_CONDITION *condition)
{
	// 定义变量，用于循环计数和存储函数返回值
	int	i, ret, ret_continue;

	// 判断条件操作符是否为不等于或不相似
	if (CONDITION_OPERATOR_NOT_EQUAL == condition->op || CONDITION_OPERATOR_NOT_LIKE == condition->op)
		// 如果条件操作符为不等于或不相似，则继续执行的返回值为成功
		ret_continue = SUCCEED;
	else
		// 否则，继续执行的返回值为失败
		ret_continue = FAIL;

	// 初始化返回值
	ret = ret_continue;

	// 遍历事件中的标签
	for (i = 0; i < event->tags.values_num && ret == ret_continue; i++)
	{
		// 获取当前标签指针
		zbx_tag_t	*tag = (zbx_tag_t *)event->tags.values[i];

		// 调用zbx_strmatch_condition函数，判断标签是否满足条件
		ret = zbx_strmatch_condition(tag->tag, condition->value, condition->op);
	}

	// 返回最终的返回值
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: check_condition_event_tag_value                                  *
 *                                                                            *
 * Purpose: check event tag value condition                                   *
 *                                                                            *
 * Parameters: event     - the event                                          *
 *             condition - condition for matching                             *
 *                                                                            *
 * Return value: SUCCEED - matches, FAIL - otherwise                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查给定的事件中的标签值是否满足条件。函数`check_condition_event_tag_value`接收两个参数，一个是指向DB_EVENT结构体的指针，另一个是指向DB_CONDITION结构体的指针。在函数中，首先判断条件的操作符，如果是不等于或不匹配，则设置继续处理的返回值为成功。然后设置初始返回值为继续处理的返回值。接下来，遍历事件中的标签值，判断条件中的值2是否与标签值相等，如果是，则执行字符串匹配条件。最后，返回最终的返回值。
 ******************************************************************************/
// 定义一个静态函数，用于检查事件标签值是否满足条件
static int	check_condition_event_tag_value(const DB_EVENT *event, DB_CONDITION *condition)
{
	// 定义变量，用于循环计数和返回值
	int	i, ret, ret_continue;

	// 判断条件的操作符，如果是不等于或不匹配，设置继续处理的返回值为成功
	if (CONDITION_OPERATOR_NOT_EQUAL == condition->op || CONDITION_OPERATOR_NOT_LIKE == condition->op)
		ret_continue = SUCCEED;
	else
		ret_continue = FAIL;

	// 设置初始返回值为继续处理的返回值
	ret = ret_continue;

	// 遍历事件中的标签值
	for (i = 0; i < event->tags.values_num && ret == ret_continue; i++)
	{
		// 转换指针，获取当前标签值
		zbx_tag_t	*tag = (zbx_tag_t *)event->tags.values[i];

		// 判断条件中的值2是否与标签值相等，如果是，则执行字符串匹配条件
		if (0 == strcmp(condition->value2, tag->tag))
			ret = zbx_strmatch_condition(tag->value, condition->value, condition->op);
	}

	// 返回最终的返回值
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: check_trigger_condition                                          *
 *                                                                            *
 * Purpose: check if event matches single condition                           *
 *                                                                            *
 * Parameters: event - trigger event to check                                 *
/******************************************************************************
 * 这段代码的主要目的是检查触发器（trigger）的条件（condition），并返回匹配的结果。
 *
 *以下是逐行注释的代码：
 *
 *
 *
 *这段代码的主要目的是检查触发器的条件，并根据条件类型和触发器类型进行处理。最后返回匹配的结果。
 ******************************************************************************/
static int	check_trigger_condition(const DB_EVENT *event, DB_CONDITION *condition)
{
    // 定义函数名和返回值
    const char	*__function_name = "check_trigger_condition";
    DB_RESULT	result;
    DB_ROW		row;
    zbx_uint64_t	condition_value;
    char		*tmp_str = NULL;
    int		ret = FAIL; // 定义返回值

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 根据触发器类型和条件类型进行处理
    if (CONDITION_TYPE_HOST_GROUP == condition->conditiontype)
    {
        // 处理 host_group 类型的条件
    }
    // ...其他类型的条件处理

    // 返回匹配结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: check_discovery_condition                                        *
 *                                                                            *
 * Purpose: check if event matches single condition                           *
 *                                                                            *
 * Parameters: event - discovery event to check                               *
 *                                 (event->source == EVENT_SOURCE_DISCOVERY)  *
 *             condition - condition for matching                             *
 *                                                                            *
 * Return value: SUCCEED - matches, FAIL - otherwise                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static int	check_discovery_condition(const DB_EVENT *event, DB_CONDITION *condition)
{
	const char	*__function_name = "check_discovery_condition";
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	condition_value;
	int		tmp_int, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (CONDITION_TYPE_DRULE == condition->conditiontype)
	{
		ZBX_STR2UINT64(condition_value, condition->value);

		if (EVENT_OBJECT_DHOST == event->object)
		{
			result = DBselect(
					"select druleid"
					" from dhosts"
					" where druleid=" ZBX_FS_UI64
						" and dhostid=" ZBX_FS_UI64,
					condition_value,
					event->objectid);
		}
		else	/* EVENT_OBJECT_DSERVICE */
		{
			result = DBselect(
					"select h.druleid"
					" from dhosts h,dservices s"
					" where h.dhostid=s.dhostid"
						" and h.druleid=" ZBX_FS_UI64
						" and s.dserviceid=" ZBX_FS_UI64,
					condition_value,
					event->objectid);
		}

		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				if (NULL != DBfetch(result))
					ret = SUCCEED;
				break;
			case CONDITION_OPERATOR_NOT_EQUAL:
				if (NULL == DBfetch(result))
					ret = SUCCEED;
				break;
			default:
				ret = NOTSUPPORTED;
		}
		DBfree_result(result);
	}
	else if (CONDITION_TYPE_DCHECK == condition->conditiontype)
	{
		if (EVENT_OBJECT_DSERVICE == event->object)
		{
			ZBX_STR2UINT64(condition_value, condition->value);

			result = DBselect(
					"select dcheckid"
					" from dservices"
					" where dcheckid=" ZBX_FS_UI64
						" and dserviceid=" ZBX_FS_UI64,
					condition_value,
					event->objectid);

			switch (condition->op)
			{
				case CONDITION_OPERATOR_EQUAL:
					if (NULL != DBfetch(result))
						ret = SUCCEED;
					break;
				case CONDITION_OPERATOR_NOT_EQUAL:
					if (NULL == DBfetch(result))
						ret = SUCCEED;
					break;
				default:
					ret = NOTSUPPORTED;
			}
			DBfree_result(result);
		}
	}
	else if (CONDITION_TYPE_DOBJECT == condition->conditiontype)
	{
		int	condition_value_i = atoi(condition->value);

		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				if (event->object == condition_value_i)
					ret = SUCCEED;
				break;
			default:
				ret = NOTSUPPORTED;
		}
	}
	else if (CONDITION_TYPE_PROXY == condition->conditiontype)
	{
		ZBX_STR2UINT64(condition_value, condition->value);

		if (EVENT_OBJECT_DHOST == event->object)
		{
			result = DBselect(
					"select r.proxy_hostid"
					" from drules r,dhosts h"
					" where r.druleid=h.druleid"
						" and r.proxy_hostid=" ZBX_FS_UI64
						" and h.dhostid=" ZBX_FS_UI64,
					condition_value,
					event->objectid);
		}
		else	/* EVENT_OBJECT_DSERVICE */
		{
			result = DBselect(
					"select r.proxy_hostid"
					" from drules r,dhosts h,dservices s"
					" where r.druleid=h.druleid"
						" and h.dhostid=s.dhostid"
						" and r.proxy_hostid=" ZBX_FS_UI64
						" and s.dserviceid=" ZBX_FS_UI64,
					condition_value,
					event->objectid);
		}

		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				if (NULL != DBfetch(result))
					ret = SUCCEED;
				break;
			case CONDITION_OPERATOR_NOT_EQUAL:
				if (NULL == DBfetch(result))
					ret = SUCCEED;
				break;
			default:
				ret = NOTSUPPORTED;
		}
		DBfree_result(result);
	}
	else if (CONDITION_TYPE_DVALUE == condition->conditiontype)
	{
		if (EVENT_OBJECT_DSERVICE == event->object)
		{
			result = DBselect(
					"select value"
					" from dservices"
					" where dserviceid=" ZBX_FS_UI64,
					event->objectid);

			if (NULL != (row = DBfetch(result)))
			{
				switch (condition->op)
				{
					case CONDITION_OPERATOR_EQUAL:
						if (0 == strcmp(condition->value, row[0]))
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_NOT_EQUAL:
						if (0 != strcmp(condition->value, row[0]))
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_MORE_EQUAL:
						if (0 <= strcmp(row[0], condition->value))
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_LESS_EQUAL:
						if (0 >= strcmp(row[0], condition->value))
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_LIKE:
						if (NULL != strstr(row[0], condition->value))
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_NOT_LIKE:
						if (NULL == strstr(row[0], condition->value))
							ret = SUCCEED;
						break;
					default:
						ret = NOTSUPPORTED;
				}
			}
			DBfree_result(result);
		}
	}
	else if (CONDITION_TYPE_DHOST_IP == condition->conditiontype)
	{
		if (EVENT_OBJECT_DHOST == event->object)
		{
			result = DBselect(
					"select distinct ip"
					" from dservices"
					" where dhostid=" ZBX_FS_UI64,
					event->objectid);
		}
		else
		{
			result = DBselect(
					"select ip"
					" from dservices"
					" where dserviceid=" ZBX_FS_UI64,
					event->objectid);
		}

		while (NULL != (row = DBfetch(result)) && FAIL == ret)
		{
			switch (condition->op)
			{
				case CONDITION_OPERATOR_EQUAL:
					if (SUCCEED == ip_in_list(condition->value, row[0]))
						ret = SUCCEED;
					break;
				case CONDITION_OPERATOR_NOT_EQUAL:
					if (SUCCEED != ip_in_list(condition->value, row[0]))
						ret = SUCCEED;
					break;
				default:
					ret = NOTSUPPORTED;
			}
		}
		DBfree_result(result);
	}
	else if (CONDITION_TYPE_DSERVICE_TYPE == condition->conditiontype)
	{
		if (EVENT_OBJECT_DSERVICE == event->object)
		{
			int	condition_value_i = atoi(condition->value);

			result = DBselect(
					"select dc.type"
					" from dservices ds,dchecks dc"
					" where ds.dcheckid=dc.dcheckid"
						" and ds.dserviceid=" ZBX_FS_UI64,
					event->objectid);

			if (NULL != (row = DBfetch(result)))
			{
				tmp_int = atoi(row[0]);

				switch (condition->op)
				{
					case CONDITION_OPERATOR_EQUAL:
						if (condition_value_i == tmp_int)
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_NOT_EQUAL:
						if (condition_value_i != tmp_int)
							ret = SUCCEED;
						break;
					default:
						ret = NOTSUPPORTED;
				}
			}
			DBfree_result(result);
		}
	}
	else if (CONDITION_TYPE_DSTATUS == condition->conditiontype)
	{
		int	condition_value_i = atoi(condition->value);

		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				if (condition_value_i == event->value)
					ret = SUCCEED;
				break;
			case CONDITION_OPERATOR_NOT_EQUAL:
				if (condition_value_i != event->value)
					ret = SUCCEED;
				break;
			default:
				ret = NOTSUPPORTED;
		}
	}
	else if (CONDITION_TYPE_DUPTIME == condition->conditiontype)
	{
		int	condition_value_i = atoi(condition->value);

		if (EVENT_OBJECT_DHOST == event->object)
		{
			result = DBselect(
					"select status,lastup,lastdown"
					" from dhosts"
					" where dhostid=" ZBX_FS_UI64,
					event->objectid);
		}
		else
		{
			result = DBselect(
					"select status,lastup,lastdown"
					" from dservices"
					" where dserviceid=" ZBX_FS_UI64,
					event->objectid);
		}

		if (NULL != (row = DBfetch(result)))
		{
			int	now;

			now = time(NULL);
			tmp_int = DOBJECT_STATUS_UP == atoi(row[0]) ? atoi(row[1]) : atoi(row[2]);

			switch (condition->op)
			{
				case CONDITION_OPERATOR_LESS_EQUAL:
					if (0 != tmp_int && (now - tmp_int) <= condition_value_i)
						ret = SUCCEED;
					break;
				case CONDITION_OPERATOR_MORE_EQUAL:
					if (0 != tmp_int && (now - tmp_int) >= condition_value_i)
						ret = SUCCEED;
					break;
				default:
					ret = NOTSUPPORTED;
			}
		}
		DBfree_result(result);
	}
	else if (CONDITION_TYPE_DSERVICE_PORT == condition->conditiontype)
	{
		if (EVENT_OBJECT_DSERVICE == event->object)
		{
			result = DBselect(
					"select port"
					" from dservices"
					" where dserviceid=" ZBX_FS_UI64,
					event->objectid);

			if (NULL != (row = DBfetch(result)))
			{
				switch (condition->op)
				{
					case CONDITION_OPERATOR_EQUAL:
						if (SUCCEED == int_in_list(condition->value, atoi(row[0])))
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_NOT_EQUAL:
						if (SUCCEED != int_in_list(condition->value, atoi(row[0])))
							ret = SUCCEED;
						break;
					default:
						ret = NOTSUPPORTED;
				}
			}
			DBfree_result(result);
		}
	}
	else
	{
		zabbix_log(LOG_LEVEL_ERR, "unsupported condition type [%d] for condition id [" ZBX_FS_UI64 "]",
				(int)condition->conditiontype, condition->conditionid);
	}

/******************************************************************************
 * 以下是对给定C语言代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是处理来自EVENT_SOURCE_DISCOVERY事件源的数据，并根据传入的条件进行匹配。匹配成功返回SUCCEED，匹配失败返回FAIL。
 ******************************************************************************/
/* 
 * (event->source == EVENT_SOURCE_DISCOVERY)  
 * 
 * condition - condition for matching
 * 
 * Return value: SUCCEED - matches, FAIL - otherwise
 * 
 * Author: Alexei Vladishev
 * 
 * 
 * 该代码块的目的是处理来自EVENT_SOURCE_DISCOVERY事件源的数据，并进行匹配。
 * 
 */
static int	check_discovery_condition(const DB_EVENT *event, DB_CONDITION *condition)
{
	/* 
	 * __function_name - 函数名称
	 * result - 查询结果
	 * row - 查询结果中的一行数据
	 * condition_value - 条件值
	 * tmp_int - 临时整数变量
	 * ret - 返回值
	 * 
	 * 该函数的主要目的是根据传入的条件对事件进行匹配，并返回匹配结果。
	 */
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	condition_value;
	int		tmp_int, ret = FAIL;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 判断事件源是否为EVENT_SOURCE_DISCOVERY */
	if (CONDITION_TYPE_DRULE == condition->conditiontype)
	{
		/* 将字符串转换为uint64_t类型 */
		ZBX_STR2UINT64(condition_value, condition->value);

		/* 判断事件对象是否为EVENT_OBJECT_DHOST */
		if (EVENT_OBJECT_DHOST == event->object)
		{
			/* 查询数据库，判断条件是否匹配 */
			result = DBselect(
					"select druleid"
					" from dhosts"
					" where druleid=" ZBX_FS_UI64
						" and dhostid=" ZBX_FS_UI64,
					condition_value,
					event->objectid);
		}
		else	/* EVENT_OBJECT_DSERVICE */
		{
			/* 查询数据库，判断条件是否匹配 */
			result = DBselect(
					"select h.druleid"
					" from dhosts h,dservices s"
					" where h.dhostid=s.dhostid"
						" and h.druleid=" ZBX_FS_UI64
						" and s.dserviceid=" ZBX_FS_UI64,
					condition_value,
					event->objectid);
		}

		/* 根据条件操作符进行匹配 */
		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				if (NULL != DBfetch(result))
					ret = SUCCEED;
				break;
			case CONDITION_OPERATOR_NOT_EQUAL:
				if (NULL == DBfetch(result))
					ret = SUCCEED;
				break;
			default:
				ret = NOTSUPPORTED;
		}
		DBfree_result(result);
	}
	/* 其他条件类型的处理逻辑... */

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	/* 返回匹配结果 */
	return ret;
}

							" and i.itemid=f.itemid"
							" and f.triggerid=t.triggerid"
							" and t.triggerid=" ZBX_FS_UI64,
						event->objectid);
				break;
			default:
				result = DBselect(
						"select distinct a.name"
						" from applications a,items_applications i"
						" where a.applicationid=i.applicationid"
							" and i.itemid=" ZBX_FS_UI64,
						event->objectid);
		}

		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				while (NULL != (row = DBfetch(result)))
				{
					if (0 == strcmp(row[0], condition->value))
					{
						ret = SUCCEED;
						break;
					}
				}
				break;
			case CONDITION_OPERATOR_LIKE:
				while (NULL != (row = DBfetch(result)))
				{
					if (NULL != strstr(row[0], condition->value))
					{
						ret = SUCCEED;
						break;
					}
				}
				break;
			case CONDITION_OPERATOR_NOT_LIKE:
				ret = SUCCEED;
				while (NULL != (row = DBfetch(result)))
				{
					if (NULL != strstr(row[0], condition->value))
					{
						ret = FAIL;
						break;
					}
				}
				break;
			default:
				ret = NOTSUPPORTED;
		}
		DBfree_result(result);
	}
	else
	{
		zabbix_log(LOG_LEVEL_ERR, "unsupported condition type [%d] for condition id [" ZBX_FS_UI64 "]",
				(int)condition->conditiontype, condition->conditionid);
	}

	if (NOTSUPPORTED == ret)
	{
		zabbix_log(LOG_LEVEL_ERR, "unsupported operator [%d] for condition id [" ZBX_FS_UI64 "]",
				(int)condition->op, condition->conditionid);
		ret = FAIL;
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: check_action_condition                                           *
 *                                                                            *
 * Purpose: check if event matches single condition                           *
 *                                                                            *
 * Parameters: event - event to check                                         *
 *             condition - condition for matching                             *
 *                                                                            *
 * Return value: SUCCEED - matches, FAIL - otherwise                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查不同来源的事件对应的条件是否满足，并根据检查结果返回相应的状态码。具体来说，该函数根据传入的事件和条件对象，判断事件来源是否支持，然后调用相应的条件检查函数（如check_trigger_condition、check_discovery_condition等），并将返回结果赋值给ret。最后，根据ret的值输出检查结果。
 ******************************************************************************/
// 定义一个函数，用于检查动作条件是否满足
int check_action_condition(const DB_EVENT *event, DB_CONDITION *condition)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "check_action_condition";
    // 定义一个整型变量，用于存储函数返回值
    int ret = FAIL;

    // 记录日志，显示函数调用信息，包括动作ID、条件ID和条件值
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() actionid:" ZBX_FS_UI64 " conditionid:" ZBX_FS_UI64 " cond.value:'%s'"
                " cond.value2:'%s'", __function_name, condition->actionid, condition->conditionid,
                ZBX_NULL2STR(condition->value), ZBX_NULL2STR(condition->value2));

    // 根据事件来源进行switch分支处理
    switch (event->source)
    {
        // 如果是触发器事件来源
        case EVENT_SOURCE_TRIGGERS:
            // 调用检查触发器条件函数，并将返回值赋给ret
            ret = check_trigger_condition(event, condition);
            break;
        // 如果是发现事件来源
        case EVENT_SOURCE_DISCOVERY:
            // 调用检查发现条件函数，并将返回值赋给ret
            ret = check_discovery_condition(event, condition);
            break;
        // 如果是自动注册事件来源
        case EVENT_SOURCE_AUTO_REGISTRATION:
            // 调用检查自动注册条件函数，并将返回值赋给ret
            ret = check_auto_registration_condition(event, condition);
            break;
        // 如果是内部事件来源
        case EVENT_SOURCE_INTERNAL:
            // 调用检查内部条件函数，并将返回值赋给ret
/******************************************************************************
 * /
 ******************************************************************************/* 检查动作条件是否满足 */
/*
 * 函数签名：static int check_action_conditions(zbx_action_eval_t *action)
 * 参数：action - 动作评估结构体指针
 *
 * 返回值：SUCCEED - 条件匹配，FAIL - 否则
 *
 * 作者：Alexei Vladishev
 *
 ******************************************************************************/

static int	check_action_conditions(zbx_action_eval_t *action)
{
	/* 保存函数名 */
	const char	*__function_name = "check_action_conditions";

	/* 指向条件的指针 */
	DB_CONDITION	*condition;
	/* 条件结果，用于判断当前条件是否满足 */
	int		condition_result, ret = SUCCEED, id_len, i;
	/* 用于存储旧条件类型的变量 */
	unsigned char	old_type = 0xff;
	/* 表达式指针 */
	char		*expression = NULL, tmp[ZBX_MAX_UINT64_LEN + 2], *ptr, error[256];
	/* 评估结果 */
	double		eval_result;

	/* 记录动作ID */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() actionid:" ZBX_FS_UI64, __function_name, action->actionid);

	/* 如果评估类型为表达式 */
	if (CONDITION_EVAL_TYPE_EXPRESSION == action->evaltype)
	{
		/* 复制表达式到内存中 */
		expression = zbx_strdup(expression, action->formula);
	}

	/* 遍历条件 */
	for (i = 0; i < action->conditions.values_num; i++)
	{
		/* 获取条件指针 */
		condition = (DB_CONDITION *)action->conditions.values[i];

		/* 如果评估类型为AND或OR，且旧条件类型与当前条件类型相同且满足条件 */
		if (CONDITION_EVAL_TYPE_AND_OR == action->evaltype && old_type == condition->conditiontype &&
				SUCCEED == ret)
		{
			/* 跳过本次循环，继续下一个条件 */
			continue;
		}

		/* 获取条件结果 */
		condition_result = condition->condition_result;

		/* 根据评估类型不同，进行不同的判断逻辑 */
		switch (action->evaltype)
		{
			case CONDITION_EVAL_TYPE_AND_OR:
				/* 如果旧条件类型与当前条件类型相同 */
				if (old_type == condition->conditiontype)
				{
					/* 如果条件满足，更新ret为SUCCEED */
					if (SUCCEED == condition_result)
						ret = SUCCEED;
				}
				else
				{
					/* 如果ret为FAIL，跳出循环 */
					if (FAIL == ret)
						goto clean;

					/* 更新ret为当前条件的结果 */
					ret = condition_result;
					old_type = condition->conditiontype;
				}

				break;
			case CONDITION_EVAL_TYPE_AND:
				/* 如果当前条件为False，跳出循环 */
				if (FAIL == condition_result)
				{
					ret = FAIL;
					goto clean;
				}

				break;
			case CONDITION_EVAL_TYPE_OR:
				/* 如果当前条件为True，跳出循环 */
				if (SUCCEED == condition_result)
				{
					ret = SUCCEED;
					goto clean;
				}
				ret = FAIL;

				break;
			case CONDITION_EVAL_TYPE_EXPRESSION:
				/* 拼接条件ID字符串 */
				zbx_snprintf(tmp, sizeof(tmp), "{" ZBX_FS_UI64 "}", condition->conditionid);
				id_len = strlen(tmp);

				/* 替换表达式中的条件ID为满足条件的字符 '1'，并保留结果 */
				for (ptr = expression; NULL != (ptr = strstr(ptr, tmp)); ptr += id_len)
				{
					*ptr = (SUCCEED == condition_result ? '1' : '0');
					memset(ptr + 1, ' ', id_len - 1);
				}

				break;
			default:
				/* 返回FAIL */
				ret = FAIL;
				goto clean;
		}
	}
/******************************************************************************
 * *
 *这个代码块的主要目的是处理数据库中查询到的操作数据，根据不同的操作类型执行相应的操作。具体来说，它完成了以下任务：
 *
 *1. 解析查询结果中的数据，包括操作类型、主机ID、模板ID和库存模式。
 *2. 根据操作类型，执行相应的操作，如添加主机、删除主机、启用主机、禁用主机、添加组、删除组等。
 *3. 在执行操作后，对相关的vector进行排序、去重和释放内存。
 *
 *整个代码块的逻辑清晰，逐行注释详细，可以帮助您更好地理解代码的功能和执行过程。
 ******************************************************************************/
static void execute_operations(const DB_EVENT *event, zbx_uint64_t actionid)
{
	/* 定义变量 */
	const char *__function_name = "execute_operations";
	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		groupid, templateid;
	zbx_vector_uint64_t	lnk_templateids, del_templateids,
				new_groupids, del_groupids;
	int			i;

	/* 开启日志记录 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() actionid:%u", __function_name, actionid);

	/* 创建 vector 用于存储数据 */
	zbx_vector_uint64_create(&lnk_templateids);
	zbx_vector_uint64_create(&del_templateids);
	zbx_vector_uint64_create(&new_groupids);
	zbx_vector_uint64_create(&del_groupids);

	/* 从数据库中查询操作数据 */
	result = DBselect(
			"select o.operationtype,g.groupid,t.templateid,oi.inventory_mode"
			" from operations o"
				" left join opgroup g on g.operationid=o.operationid"
				" left join optemplate t on t.operationid=o.operationid"
				" left join opinventory oi on o.operationid=o.operationid"
			" where o.actionid=%u"
			" order by o.operationid",
			actionid);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
/******************************************************************************
 * *
 *这个代码块的主要目的是检查自动注册条件是否满足。它接收一个`DB_EVENT`结构体和一个`DB_CONDITION`结构体作为输入参数，然后根据条件类型和操作符进行相应的判断。如果条件满足，函数返回`SUCCEED`，否则返回`FAIL`。在整个过程中，函数还记录了日志以方便调试。
 ******************************************************************************/
/* 定义一个静态函数，用于检查自动注册条件是否满足 */
static int	check_auto_registration_condition(const DB_EVENT *event, DB_CONDITION *condition)
{
	/* 定义一个日志标签 */
	const char	*__function_name = "check_auto_registration_condition";
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	condition_value, id;
	int		ret = FAIL;
	const char	*condition_field;

	/* 记录日志，表示进入函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 判断条件类型 */
	switch (condition->conditiontype)
	{
		case CONDITION_TYPE_HOST_NAME:
		case CONDITION_TYPE_HOST_METADATA:
			if (CONDITION_TYPE_HOST_NAME == condition->conditiontype)
				condition_field = "host";
			else
				condition_field = "host_metadata";

			/* 查询数据库，判断条件值是否满足 */
			result = DBselect(
					"select %s"
					" from autoreg_host"
					" where autoreg_hostid=" ZBX_FS_UI64,
					condition_field, event->objectid);

			if (NULL != (row = DBfetch(result)))
			{
				/* 判断条件操作符 */
				switch (condition->op)
				{
					case CONDITION_OPERATOR_LIKE:
						if (NULL != strstr(row[0], condition->value))
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_NOT_LIKE:
						if (NULL == strstr(row[0], condition->value))
							ret = SUCCEED;
						break;
					default:
						ret = NOTSUPPORTED;
				}
			}
			DBfree_result(result);

			break;
		case CONDITION_TYPE_PROXY:
			/* 将条件值转换为整数 */
			ZBX_STR2UINT64(condition_value, condition->value);

			/* 查询数据库，判断条件值是否满足 */
			result = DBselect(
					"select proxy_hostid"
					" from autoreg_host"
					" where autoreg_hostid=" ZBX_FS_UI64,
					event->objectid);

			if (NULL != (row = DBfetch(result)))
			{
				/* 判断条件操作符 */
				switch (condition->op)
				{
					case CONDITION_OPERATOR_EQUAL:
						if (id == condition_value)
							ret = SUCCEED;
						break;
					case CONDITION_OPERATOR_NOT_EQUAL:
						if (id != condition_value)
							ret = SUCCEED;
						break;
					default:
						ret = NOTSUPPORTED;
				}
			}
			DBfree_result(result);
/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是检查给定的条件是否满足。首先，它检查事件类型是否支持当前条件。如果支持，它将根据条件类型和操作符执行相应的查询和判断。如果不支持，它会输出错误日志并返回失败。
 *
 *以下是整个代码块的注释：
 *
 *```c
 *// 定义一个检查内部条件函数
 *static int check_internal_condition(const DB_EVENT *event, DB_CONDITION *condition)
 *{
 *\t// 定义函数名和日志级别
 *\tconst char\t*__function_name = \"check_internal_condition\";
 *\t// 初始化日志级别
 *\tzabbix_log_init(LOG_LEVEL_DEBUG, NULL);
 *
 *\t// 解析条件类型和值
 *\tzbx_uint64_t\tcondition_value;
 *\tZBX_STR2UINT64(condition->value, &condition_value);
 *
 *\t// 初始化返回值
 *\tint\t\tret = FAIL;
 *
 *\t// 根据条件类型和操作符进行判断
 *\tswitch (condition->conditiontype)
 *\t{
 *\t\tcase CONDITION_TYPE_EVENT_TYPE:
 *\t\t\t// 判断事件类型是否支持条件
 *\t\t\tif (EVENT_OBJECT_TRIGGER != event->object && EVENT_OBJECT_ITEM != event->object &&
 *\t\t\t\t\tEVENT_OBJECT_LLDRULE != event->object)
 *\t\t{
 *\t\t\tzabbix_log(LOG_LEVEL_ERR, \"unsupported event object [%d] for condition id [\" ZBX_FS_UI64 \"]\",
 *\t\t\t\t\tevent->object, condition->conditionid);
 *\t\t\tgoto out;
 *\t\t}
 *
 *\t\t// 判断条件是否满足
 *\t\tswitch (condition->op)
 *\t\t{
 *\t\t\tcase CONDITION_OPERATOR_EQUAL:
 *\t\t\t\t// 查询满足条件的主机
 *\t\t\t\tresult = DBselect(
 *\t\t\t\t\t\t\"select distinct i.hostid\"
 *\t\t\t\t\t\t\" from items i,functions f,triggers t\"
 *\t\t\t\t\t\t\" where i.itemid=f.itemid\"
 *\t\t\t\t\t\t\t\" and f.triggerid=t.triggerid\"
 *\t\t\t\t\t\t\t\" and t.triggerid=\" ZBX_FS_UI64
 *\t\t\t\t\t\t\t\" and\",
 *\t\t\t\t\t\tevent->objectid);
 *
 *\t\t\t\t// 遍历查询结果，判断条件是否满足
 *\t\t\t\twhile (NULL != (row = DBfetch(result)))
 *\t\t\t\t{
 *\t\t\t\t\tif (0 == strcmp(row[0], condition->value))
 *\t\t\t\t\t{
 *\t\t\t\t\t\tret = SUCCEED;
 *\t\t\t\t\t\tbreak;
 *\t\t\t\t\t}
 *\t\t\t\t}
 *\t\t\t\tbreak;
 *\t\t\tcase CONDITION_OPERATOR_NOT_EQUAL:
 *\t\t\t\t// 查询不满足条件的主机
 *\t\t\t\tresult = DBselect(
 *\t\t\t\t\t\t\"select distinct i.hostid\"
 *\t\t\t\t\t\t\" from items i,functions f,triggers t\"
 *\t\t\t\t\t\t\" where i.itemid=f.itemid\"
 *\t\t\t\t\t\t\t\" and f.triggerid=t.triggerid\"
 *\t\t\t\t\t\t\t\" and t.triggerid=\" ZBX_FS_UI64
 *\t\t\t\t\t\t\t\" and\",
 *\t\t\t\t\t\tevent->objectid);
 *
 *\t\t\t\t// 遍历查询结果，判断条件是否满足
 *\t\t\t\twhile (NULL != (row = DBfetch(result)))
 *\t\t\t\t{
 *\t\t\t\t\tif (NULL != strstr(row[0], condition->value))
 *\t\t\t\t\t{
 *\t\t\t\t\t\tret = FAIL;
 *\t\t\t\t\t\tbreak;
 *\t\t\t\t\t}
 *\t\t\t\t}
 *\t\t\t\tbreak;
 *\t\t\tdefault:
 *\t\t\t\tret = NOTSUPPORTED;
 *\t\t}
 *\t\tDBfree_result(result);
 *\t\tbreak;
 *\t\t// 其他条件类型，如HOST_GROUP、HOST_TEMPLATE等，可以类似地处理
 *\t}
 *
 *\t// 输出结果
 *\tzabbix_log(LOG_LEVEL_DEBUG, \"End of %s():%s\", __function_name, zbx_result_string(ret));
 *
 *\t// 返回结果
 *\treturn ret;
 *}
 *```
 ******************************************************************************/
static int check_internal_condition(const DB_EVENT *event, DB_CONDITION *condition)
{
	const char	*__function_name = "check_internal_condition";
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	condition_value;
	int		ret = FAIL;
	char		sql[256];

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 检查事件类型是否支持当前条件
	if (EVENT_OBJECT_TRIGGER != event->object && EVENT_OBJECT_ITEM != event->object &&
			EVENT_OBJECT_LLDRULE != event->object)
	{
		zabbix_log(LOG_LEVEL_ERR, "unsupported event object [%d] for condition id [" ZBX_FS_UI64 "]",
				event->object, condition->conditionid);
		goto out;
	}

	// 判断条件类型是否为事件类型
	if (CONDITION_TYPE_EVENT_TYPE == condition->conditiontype)
	{
		condition_value = atoi(condition->value);

		// 根据条件值判断是否满足条件
		switch (condition_value)
		{
			case EVENT_TYPE_ITEM_NOTSUPPORTED:
				if (EVENT_OBJECT_ITEM == event->object && ITEM_STATE_NOTSUPPORTED == event->value)
					ret = SUCCEED;
				break;
			case EVENT_TYPE_TRIGGER_UNKNOWN:
				if (EVENT_OBJECT_TRIGGER == event->object && TRIGGER_STATE_UNKNOWN == event->value)
					ret = SUCCEED;
				break;
			case EVENT_TYPE_LLDRULE_NOTSUPPORTED:
				if (EVENT_OBJECT_LLDRULE == event->object && ITEM_STATE_NOTSUPPORTED == event->value)
					ret = SUCCEED;
				break;
			default:
				ret = NOTSUPPORTED;
		}
	}
	else if (CONDITION_TYPE_HOST_GROUP == condition->conditiontype)
	{
		// 解析条件值
		zbx_vector_uint64_t	groupids;
		char			*sqlcond = NULL;
		size_t			sqlcond_alloc = 0, sqlcond_offset = 0;

		ZBX_STR2UINT64(condition_value, condition->value);

		// 解析条件中的主机组
		zbx_vector_uint64_create(&groupids);
		zbx_dc_get_nested_hostgroupids(&condition_value, 1, &groupids);

		// 根据事件类型查询主机组
		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				zbx_snprintf_alloc(&sqlcond, &sqlcond_alloc, &sqlcond_offset,
						"select null"
						" from hosts_groups hg,hosts h,items i,functions f,triggers t"
						" where hg.hostid=h.hostid"
							" and h.hostid=i.hostid"
							" and i.itemid=f.itemid"
							" and f.triggerid=t.triggerid"
							" and t.triggerid=" ZBX_FS_UI64
							" and",
						event->objectid);
				break;
			default:
				zbx_snprintf_alloc(&sqlcond, &sqlcond_alloc, &sqlcond_offset,
						"select null"
						" from hosts_groups hg,hosts h,items i"
						" where hg.hostid=h.hostid"
							" and h.hostid=i.hostid"
							" and i.itemid=" ZBX_FS_UI64
							" and",
						event->objectid);
		}

		// 查询主机组
		result = DBselectN(sql, 1);

		// 判断条件操作符
		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				while (NULL != (row = DBfetch(result)))
				{
					// 判断条件是否满足
					if (0 == strcmp(row[0], condition->value))
					{
						ret = SUCCEED;
						break;
					}
				}
				break;
			case CONDITION_OPERATOR_NOT_EQUAL:
				ret = SUCCEED;
				while (NULL != (row = DBfetch(result)))
				{
					// 判断条件是否满足
					if (NULL != strstr(row[0], condition->value))
					{
						ret = FAIL;
						break;
					}
				}
				break;
			default:
				ret = NOTSUPPORTED;
		}
		DBfree_result(result);
	}
	else if (CONDITION_TYPE_HOST_TEMPLATE == condition->conditiontype)
	{
		// 解析条件值
		zbx_uint64_t	hostid, objectid;

		ZBX_STR2UINT64(condition_value, condition->value);

		// 判断条件操作符
		switch (condition->op)
		{
			case CONDITION_OPERATOR_EQUAL:
				// 查询主机
				result = DBselect(
						"select distinct a.name"
						" from applications a,items_applications i,functions f,triggers t"
						" where a.applicationid=i.applicationid"
							" and i.itemid=f.itemid"
							" and f.triggerid=t.triggerid"
							" and t.triggerid=" ZBX_FS_UI64
							" and",
						event->objectid);

				// 判断条件是否满足
				while (NULL != (row = DBfetch(result)))
				{
					if (0 == strcmp(row[0], condition->value))
					{
						ret = SUCCEED;
						break;
					}
				}
				break;
			default:
				ret = NOTSUPPORTED;
		}
		DBfree_result(result);
	}
	else if (CONDITION_TYPE_APPLICATION == condition->conditiontype)
	{
		// 解析条件值
		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				result = DBselect(
						"select distinct a.name"
						" from applications a,items_applications i,functions f,triggers t"
						" where a.applicationid=i.applicationid"
							" and i.itemid=f.itemid"
							" and f.triggerid=t.triggerid"
							" and t.triggerid=" ZBX_FS_UI64,
						event->objectid);

				// 判断条件是否满足
				while (NULL != (row = DBfetch(result)))
				{
					if (0 == strcmp(row[0], condition->value))
					{
						ret = SUCCEED;
						break;
					}
				}
				break;
			default:
				ret = NOTSUPPORTED;
		}
		DBfree_result(result);
	}
	else
	{
		zabbix_log(LOG_LEVEL_ERR, "unsupported condition type [%d] for condition id [" ZBX_FS_UI64 "]",
				(int)condition->conditiontype, condition->conditionid);
	}

	if (NOTSUPPORTED == ret)
	{
		zabbix_log(LOG_LEVEL_ERR, "unsupported operator [%d] for condition id [" ZBX_FS_UI64 "]",
				(int)condition->op, condition->conditionid);
		ret = FAIL;
	}

out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}


	zbx_vector_ptr_create(&actions);
	zbx_dc_get_actions_eval(&actions, uniq_conditions, ZBX_ACTION_OPCLASS_NORMAL | ZBX_ACTION_OPCLASS_RECOVERY);

	/* 1. All event sources: match PROBLEM events to action conditions, add them to 'new_escalations' list.      */
	/* 2. EVENT_SOURCE_DISCOVERY, EVENT_SOURCE_AUTO_REGISTRATION: execute operations (except command and message */
	/*    operations) for events that match action conditions.                                                   */
	for (i = 0; i < events->values_num; i++)
	{
		int		j;
		const DB_EVENT	*event;

		event = (const DB_EVENT *)events->values[i];

		/* OK events can't start escalations - skip them */
		if (SUCCEED == is_recovery_event(event))
			continue;

		if (0 != (event->flags & ZBX_FLAGS_DB_EVENT_NO_ACTION) ||
				0 == (event->flags & ZBX_FLAGS_DB_EVENT_CREATE))
		{
			continue;
		}

		if (SUCCEED == check_event_conditions(event, uniq_conditions))
		{
			for (j = 0; j < actions.values_num; j++)
			{
				zbx_action_eval_t	*action = (zbx_action_eval_t *)actions.values[j];

				if (action->eventsource != event->source)
					continue;

				if (SUCCEED == check_action_conditions(action))
				{
					zbx_escalation_new_t	*new_escalation;

					/* command and message operations handled by escalators even for    */
					/* EVENT_SOURCE_DISCOVERY and EVENT_SOURCE_AUTO_REGISTRATION events */
					new_escalation = (zbx_escalation_new_t *)zbx_malloc(NULL, sizeof(zbx_escalation_new_t));
					new_escalation->actionid = action->actionid;
					new_escalation->event = event;
					zbx_vector_ptr_append(&new_escalations, new_escalation);

					if (EVENT_SOURCE_DISCOVERY == event->source ||
							EVENT_SOURCE_AUTO_REGISTRATION == event->source)
					{
						execute_operations(event, action->actionid);
					}
				}
			}
		}
	}

	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
	{
		zbx_conditions_eval_clean(&uniq_conditions[i]);
		zbx_hashset_destroy(&uniq_conditions[i]);
	}

	zbx_vector_ptr_clear_ext(&actions, (zbx_clean_func_t)zbx_action_eval_free);
	zbx_vector_ptr_destroy(&actions);

	/* 3. Find recovered escalations and store escalationids in 'rec_escalation' by OK eventids. */
	if (0 != closed_events->values_num)
	{
		char			*sql = NULL;
		size_t			sql_alloc = 0, sql_offset = 0;
		zbx_vector_uint64_t	eventids;
		DB_ROW			row;
		DB_RESULT		result;
		int			j, index;

		zbx_vector_uint64_create(&eventids);

		/* 3.1. Store PROBLEM eventids of recovered events in 'eventids'. */
		for (j = 0; j < closed_events->values_num; j++)
			zbx_vector_uint64_append(&eventids, closed_events->values[j].first);

		/* 3.2. Select escalations that must be recovered. */
		zbx_vector_uint64_sort(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select eventid,escalationid"
				" from escalations"
				" where");

		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);
		result = DBselect("%s", sql);

		zbx_vector_uint64_pair_reserve(&rec_escalations, eventids.values_num);

		/* 3.3. Store the escalationids corresponding to the OK events in 'rec_escalations'. */
		while (NULL != (row = DBfetch(result)))
		{
			zbx_uint64_pair_t	pair;

			ZBX_STR2UINT64(pair.first, row[0]);

			if (FAIL == (index = zbx_vector_uint64_pair_bsearch(closed_events, pair,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			pair.second = closed_events->values[index].second;
			ZBX_DBROW2UINT64(pair.first, row[1]);
			zbx_vector_uint64_pair_append(&rec_escalations, pair);
		}

		DBfree_result(result);
		zbx_free(sql);
		zbx_vector_uint64_destroy(&eventids);
	}

	/* 4. Create new escalations in DB. */
	if (0 != new_escalations.values_num)
	{
		zbx_db_insert_t	db_insert;
		int		j;

		zbx_db_insert_prepare(&db_insert, "escalations", "escalationid", "actionid", "status", "triggerid",
					"itemid", "eventid", "r_eventid", "acknowledgeid", NULL);

		for (j = 0; j < new_escalations.values_num; j++)
		{
			zbx_uint64_t		triggerid = 0, itemid = 0;
			zbx_escalation_new_t	*new_escalation;

			new_escalation = (zbx_escalation_new_t *)new_escalations.values[j];

			switch (new_escalation->event->object)
			{
				case EVENT_OBJECT_TRIGGER:
					triggerid = new_escalation->event->objectid;
					break;
				case EVENT_OBJECT_ITEM:
/******************************************************************************
 * 
 ******************************************************************************/
int process_actions_by_acknowledgements(const zbx_vector_ptr_t *ack_tasks)
{
	// 定义变量
	const char		*__function_name = "process_actions_by_acknowledgements";
	zbx_vector_ptr_t	actions;
	zbx_hashset_t		uniq_conditions[EVENT_SOURCE_COUNT];
	int			i, j, k, processed_num = 0, knext = 0;
	zbx_vector_uint64_t	eventids;
	zbx_ack_task_t		*ack_task;
	zbx_vector_ptr_t	ack_escalations, events;
	zbx_ack_escalation_t	*ack_escalation;

	// 开启日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个动作列表
	zbx_vector_ptr_create(&ack_escalations);

	// 初始化uniq_conditions数组，用于存储唯一条件
	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
	{
		zbx_hashset_create(&uniq_conditions[i], 0, uniq_conditions_hash_func, uniq_conditions_compare_func);
	}

	// 创建一个动作列表
	zbx_vector_ptr_create(&actions);
	// 获取动作列表中的所有动作，并根据唯一条件筛选
	zbx_dc_get_actions_eval(&actions, uniq_conditions, ZBX_ACTION_OPCLASS_ACKNOWLEDGE);

	// 如果动作列表为空，则退出
	if (0 == actions.values_num)
		goto out;

	// 创建一个事件ID列表
	zbx_vector_uint64_create(&eventids);

	// 将ack_tasks中的事件ID添加到eventids列表中
	for (i = 0; i < ack_tasks->values_num; i++)
	{
		ack_task = (zbx_ack_task_t *)ack_tasks->values[i];
		zbx_vector_uint64_append(&eventids, ack_task->eventid);
	}

	// 对事件ID列表进行排序和去重
	zbx_vector_uint64_sort(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 创建一个事件列表
	zbx_vector_ptr_create(&events);

	// 从数据库中获取事件列表
	zbx_db_get_events_by_eventids(&eventids, &events);

	// 遍历事件列表，根据动作和条件进行检查
	for (i = 0; i < eventids.values_num; i++)
	{
		int 		kcurr = knext;
		DB_EVENT	*event = (DB_EVENT *)events.values[i];

		// 遍历动作列表，检查条件
		while (knext < ack_tasks->values_num)
		{
			ack_task = (zbx_ack_task_t *)ack_tasks->values[knext];
			if (ack_task->eventid != event->eventid)
				break;
			knext++;
		}

		// 如果事件ID或触发器ID为空，跳过
		if (0 == event->eventid || 0 == event->trigger.triggerid)
			continue;

		// 检查事件条件
		if (SUCCEED != check_event_conditions(event, uniq_conditions))
			continue;

		// 检查动作条件
		for (j = 0; j < actions.values_num; j++)
		{
			zbx_action_eval_t	*action = (zbx_action_eval_t *)actions.values[j];

			// 检查动作与事件源是否匹配
			if (action->eventsource != event->source)
				continue;

			// 检查动作条件
			if (SUCCEED != check_action_conditions(action))
				continue;

			// 处理动作
			for (k = kcurr; k < knext; k++)
			{
				ack_task = (zbx_ack_task_t *)ack_tasks->values[k];

				// 为 escalation 结构分配内存
				ack_escalation = (zbx_ack_escalation_t *)zbx_malloc(NULL, sizeof(zbx_ack_escalation_t));
				ack_escalation->taskid = ack_task->taskid;
				ack_escalation->acknowledgeid = ack_task->acknowledgeid;
				ack_escalation->actionid = action->actionid;
				ack_escalation->eventid = event->eventid;
				ack_escalation->triggerid = event->trigger.triggerid;
				// 将 escalation 结构添加到列表中
				zbx_vector_ptr_append(&ack_escalations, ack_escalation);
			}
		}
	}

	// 如果 escalations 列表不为空，执行以下操作：
	if (0 != ack_escalations.values_num)
	{
		// 准备数据库插入操作
		zbx_db_insert_t	db_insert;

		// 初始化 db_insert 结构
		zbx_db_insert_prepare(&db_insert, "escalations", "escalationid", "actionid", "status", "triggerid",
						"itemid", "eventid", "r_eventid", "acknowledgeid", NULL);

		// 对 escalations 列表进行排序
		zbx_vector_ptr_sort(&ack_escalations, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		// 逐个插入 escalations 到数据库
		for (i = 0; i < ack_escalations.values_num; i++)
		{
			ack_escalation = (zbx_ack_escalation_t *)ack_escalations.values[i];

			// 执行插入操作
			zbx_db_insert_add_values(&db_insert, __UINT64_C(0), ack_escalation->actionid,
				(int)ESCALATION_STATUS_ACTIVE, ack_escalation->triggerid, __UINT64_C(0),
				ack_escalation->eventid, __UINT64_C(0), ack_escalation->acknowledgeid);
		}

		// 自动递增 escalationid
		zbx_db_insert_autoincrement(&db_insert, "escalationid");
		// 执行插入操作
		zbx_db_insert_execute(&db_insert);
		// 清理 db_insert 结构
		zbx_db_insert_clean(&db_insert);

		// 更新已处理动作数量
		processed_num = ack_escalations.values_num;
	}

	// 清理并释放内存
	zbx_vector_ptr_clear_ext(&events, (zbx_clean_func_t)zbx_db_free_event);
	zbx_vector_ptr_destroy(&events);

	// 清理并释放内存
	zbx_vector_uint64_destroy(&eventids);

out:
	// 遍历并释放uniq_conditions数组中的所有条件
	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
	{
		zbx_conditions_eval_clean(&uniq_conditions[i]);
		zbx_hashset_destroy(&uniq_conditions[i]);
	}

	// 清理并释放动作列表
	zbx_vector_ptr_clear_ext(&actions, (zbx_clean_func_t)zbx_action_eval_free);
	zbx_vector_ptr_destroy(&actions);

	// 清理并释放 escalations 列表
	zbx_vector_ptr_clear_ext(&ack_escalations, zbx_ptr_free);
	zbx_vector_ptr_destroy(&ack_escalations);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() processed_num:%d", __function_name, processed_num);

	// 返回已处理动作数量
	return processed_num;
}

			for (k = kcurr; k < knext; k++)
			{
				ack_task = (zbx_ack_task_t *)ack_tasks->values[k];

				ack_escalation = (zbx_ack_escalation_t *)zbx_malloc(NULL, sizeof(zbx_ack_escalation_t));
				ack_escalation->taskid = ack_task->taskid;
				ack_escalation->acknowledgeid = ack_task->acknowledgeid;
				ack_escalation->actionid = action->actionid;
				ack_escalation->eventid = event->eventid;
				ack_escalation->triggerid = event->trigger.triggerid;
				zbx_vector_ptr_append(&ack_escalations, ack_escalation);
			}
		}
	}

	if (0 != ack_escalations.values_num)
	{
		zbx_db_insert_t	db_insert;

		zbx_db_insert_prepare(&db_insert, "escalations", "escalationid", "actionid", "status", "triggerid",
						"itemid", "eventid", "r_eventid", "acknowledgeid", NULL);

		zbx_vector_ptr_sort(&ack_escalations, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		for (i = 0; i < ack_escalations.values_num; i++)
		{
			ack_escalation = (zbx_ack_escalation_t *)ack_escalations.values[i];

			zbx_db_insert_add_values(&db_insert, __UINT64_C(0), ack_escalation->actionid,
				(int)ESCALATION_STATUS_ACTIVE, ack_escalation->triggerid, __UINT64_C(0),
				ack_escalation->eventid, __UINT64_C(0), ack_escalation->acknowledgeid);
		}

		zbx_db_insert_autoincrement(&db_insert, "escalationid");
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);

		processed_num = ack_escalations.values_num;
	}

	zbx_vector_ptr_clear_ext(&events, (zbx_clean_func_t)zbx_db_free_event);
	zbx_vector_ptr_destroy(&events);

	zbx_vector_uint64_destroy(&eventids);
out:
	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
	{
		zbx_conditions_eval_clean(&uniq_conditions[i]);
		zbx_hashset_destroy(&uniq_conditions[i]);
	}

	zbx_vector_ptr_clear_ext(&actions, (zbx_clean_func_t)zbx_action_eval_free);
	zbx_vector_ptr_destroy(&actions);

	zbx_vector_ptr_clear_ext(&ack_escalations, zbx_ptr_free);
	zbx_vector_ptr_destroy(&ack_escalations);
/******************************************************************************
 * *
 *该代码主要目的是从数据库中查询动作（包括动作ID、名称、状态、事件源、暂停抑制、确认数据等）的信息，并将查询到的动作添加到actions向量中。同时，还会查询并处理恢复动作，更新动作的恢复类型。
 ******************************************************************************/
// 定义一个函数，用于获取数据库中的动作信息
void get_db_actions_info(zbx_vector_uint64_t *actionids, zbx_vector_ptr_t *actions)
{
	// 声明变量
	DB_RESULT	result;
	DB_ROW		row;
	char		*filter = NULL;
	size_t		filter_alloc = 0, filter_offset = 0;
	DB_ACTION	*action;

	// 对actionids向量进行排序和去重
	zbx_vector_uint64_sort(actionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(actionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 为filter分配内存，并填充条件
	DBadd_condition_alloc(&filter, &filter_alloc, &filter_offset, "actionid", actionids->values,
			actionids->values_num);

	// 从数据库中查询动作信息
	result = DBselect("select actionid,name,status,eventsource,esc_period,def_shortdata,def_longdata,r_shortdata,"
				"r_longdata,pause_suppressed,ack_shortdata,ack_longdata"
				" from actions"
				" where%s order by actionid", filter);

	// 遍历查询结果，解析动作信息
	while (NULL != (row = DBfetch(result)))
	{
		char	*tmp;

		// 分配内存，用于存储动作信息
		action = (DB_ACTION *)zbx_malloc(NULL, sizeof(DB_ACTION));
		// 解析动作ID
		ZBX_STR2UINT64(action->actionid, row[0]);
		// 解析动作状态
		ZBX_STR2UCHAR(action->status, row[2]);
		// 解析事件源
		ZBX_STR2UCHAR(action->eventsource, row[3]);

		// 解析并处理动作的持续时间
		tmp = zbx_strdup(NULL, row[4]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &tmp, MACRO_TYPE_COMMON,
				NULL, 0);
		if (SUCCEED != is_time_suffix(tmp, &action->esc_period, ZBX_LENGTH_UNLIMITED))
		{
			// 警告：动作的默认操作步骤时间无效
			zabbix_log(LOG_LEVEL_WARNING, "Invalid default operation step duration \"%s\" for action"
					" \"%s\", using default value of 1 hour", tmp, row[1]);
			action->esc_period = SEC_PER_HOUR;
		}
		zbx_free(tmp);

		// 解析并存储动作的简要数据和详细数据
		action->shortdata = zbx_strdup(NULL, row[5]);
		action->longdata = zbx_strdup(NULL, row[6]);
		action->r_shortdata = zbx_strdup(NULL, row[7]);
		action->r_longdata = zbx_strdup(NULL, row[8]);
		// 解析并存储动作的暂停抑制和确认数据
		ZBX_STR2UCHAR(action->pause_suppressed, row[9]);
		action->ack_shortdata = zbx_strdup(NULL, row[10]);
		action->ack_longdata = zbx_strdup(NULL, row[11]);
		// 存储动作名称
		action->name = zbx_strdup(NULL, row[1]);
		action->recovery = ZBX_ACTION_RECOVERY_NONE;

		// 将解析好的动作添加到actions向量中
		zbx_vector_ptr_append(actions, action);
	}
	// 释放查询结果
	DBfree_result(result);

	// 查询并处理恢复动作
	result = DBselect("select actionid from operations where recovery=%d and%s",
			ZBX_OPERATION_MODE_RECOVERY, filter);

	// 遍历恢复动作，更新动作的恢复类型
	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	actionid;
		int		index;

		// 解析动作ID
		ZBX_STR2UINT64(actionid, row[0]);
		// 在actions向量中查找动作
		if (FAIL != (index = zbx_vector_ptr_bsearch(actions, &actionid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			// 更新动作的恢复类型
			action = (DB_ACTION *)actions->values[index];
			action->recovery = ZBX_ACTION_RECOVERY_OPERATIONS;
		}
	}
	// 释放查询结果
	DBfree_result(result);

	// 释放filter分配的内存
	zbx_free(filter);
}

			ZBX_OPERATION_MODE_RECOVERY, filter);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	actionid;
		int		index;

		ZBX_STR2UINT64(actionid, row[0]);
		if (FAIL != (index = zbx_vector_ptr_bsearch(actions, &actionid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			action = (DB_ACTION *)actions->values[index];
			action->recovery = ZBX_ACTION_RECOVERY_OPERATIONS;
		}
	}
	DBfree_result(result);

	zbx_free(filter);
}
/******************************************************************************
 * *
 *这块代码的主要目的是释放一个 DB_ACTION 结构体及其内部指针所指向的内存空间。函数名为 free_db_action，接收一个 DB_ACTION 类型的指针作为参数。在函数内部，依次释放 action 指向的 shortdata、longdata、r_shortdata、r_longdata、ack_shortdata、ack_longdata 和 name 内存空间。最后，释放整个 DB_ACTION 结构体的内存空间。
 ******************************************************************************/
void	free_db_action(DB_ACTION *action) // 定义一个名为 free_db_action 的函数，参数为一个 DB_ACTION 类型的指针 action
{
	// 释放 action 指向的 shortdata 内存空间
	zbx_free(action->shortdata);
	// 释放 action 指向的 longdata 内存空间
	zbx_free(action->longdata);
	// 释放 action 指向的 r_shortdata 内存空间
	zbx_free(action->r_shortdata);
	// 释放 action 指向的 r_longdata 内存空间
	zbx_free(action->r_longdata);
	// 释放 action 指向的 ack_shortdata 内存空间
	zbx_free(action->ack_shortdata);
	// 释放 action 指向的 ack_longdata 内存空间
	zbx_free(action->ack_longdata);
	// 释放 action 指向的 name 内存空间
	zbx_free(action->name);

	// 释放 action 指向的内存空间，即释放整个 DB_ACTION 结构体
	zbx_free(action);
}

