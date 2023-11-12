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
#include "zbxserver.h"
#include "evalfunc.h"
#include "db.h"
#include "log.h"
#include "zbxalgo.h"
#include "valuecache.h"
#include "macrofunc.h"
#include "zbxregexp.h"
#ifdef HAVE_LIBXML2
#	include <libxml/parser.h>
#	include <libxml/tree.h>
#	include <libxml/xpath.h>
#	include <libxml/xmlerror.h>

typedef struct
{
	char	*buf;
	size_t	len;
}
zbx_libxml_error_t;
#endif

/* The following definitions are used to identify the request field */
/* for various value getters grouped by their scope:                */

/* DBget_item_value(), get_interface_value() */
#define ZBX_REQUEST_HOST_IP		1
#define ZBX_REQUEST_HOST_DNS		2
#define ZBX_REQUEST_HOST_CONN		3
#define ZBX_REQUEST_HOST_PORT		4

/* DBget_item_value() */
#define ZBX_REQUEST_HOST_ID		101
#define ZBX_REQUEST_HOST_HOST		102
#define ZBX_REQUEST_HOST_NAME		103
#define ZBX_REQUEST_HOST_DESCRIPTION	104
#define ZBX_REQUEST_ITEM_ID		105
#define ZBX_REQUEST_ITEM_NAME		106
#define ZBX_REQUEST_ITEM_NAME_ORIG	107
#define ZBX_REQUEST_ITEM_KEY		108
#define ZBX_REQUEST_ITEM_KEY_ORIG	109
#define ZBX_REQUEST_ITEM_DESCRIPTION	110
#define ZBX_REQUEST_PROXY_NAME		111
#define ZBX_REQUEST_PROXY_DESCRIPTION	112

/* DBget_history_log_value() */
#define ZBX_REQUEST_ITEM_LOG_DATE	201
#define ZBX_REQUEST_ITEM_LOG_TIME	202
#define ZBX_REQUEST_ITEM_LOG_AGE	203
#define ZBX_REQUEST_ITEM_LOG_SOURCE	204
#define ZBX_REQUEST_ITEM_LOG_SEVERITY	205
#define ZBX_REQUEST_ITEM_LOG_NSEVERITY	206
#define ZBX_REQUEST_ITEM_LOG_EVENTID	207

/******************************************************************************
 *                                                                            *
 * Function: get_N_functionid                                                 *
 *                                                                            *
 * Parameters: expression   - [IN] null terminated trigger expression         *
 *                            '{11}=1 & {2346734}>5'                          *
 *             N_functionid - [IN] number of function in trigger expression   *
 *             functionid   - [OUT] ID of an N-th function in expression      *
 *             end          - [OUT] a pointer to text following the extracted *
 *                            function id (can be NULL)                       *
 *                                                                            *
 ******************************************************************************/
int	get_N_functionid(const char *expression, int N_functionid, zbx_uint64_t *functionid, const char **end)
{
	enum state_t {NORMAL, ID}	state = NORMAL;
	int				num = 0, ret = FAIL;
	const char			*c, *p_functionid = NULL;

	for (c = expression; '\0' != *c; c++)
	{
		if ('{' == *c)
		{
			/* skip user macros */
			if ('$' == c[1])
			{
				int	macro_r, context_l, context_r;

				if (SUCCEED == zbx_user_macro_parse(c, &macro_r, &context_l, &context_r))
					c += macro_r;
				else
					c++;

				continue;
			}

			state = ID;
			p_functionid = c + 1;
		}
		else if ('}' == *c && ID == state && NULL != p_functionid)
		{
			if (SUCCEED == is_uint64_n(p_functionid, c - p_functionid, functionid))
			{
				if (++num == N_functionid)
				{
					if (NULL != end)
						*end = c + 1;

					ret = SUCCEED;
					break;
				}
			}

			state = NORMAL;
		}
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_functionids                                                  *
 *                                                                            *
 * Purpose: get identifiers of the functions used in expression               *
 *                                                                            *
 * Parameters: functionids - [OUT] the resulting vector of function ids       *
 *             expression  - [IN] null terminated trigger expression          *
 *                           '{11}=1 & {2346734}>5'                           *
 *                                                                            *
 ******************************************************************************/
void	get_functionids(zbx_vector_uint64_t *functionids, const char *expression)
{
	zbx_token_t	token;
	int		pos = 0;
	zbx_uint64_t	functionid;

	if ('\0' == *expression)
		return;

	for (; SUCCEED == zbx_token_find(expression, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		switch (token.type)
		{
			case ZBX_TOKEN_OBJECTID:
				is_uint64_n(expression + token.loc.l + 1, token.loc.r - token.loc.l - 1,
						&functionid);
				zbx_vector_uint64_append(functionids, functionid);
				ZBX_FALLTHROUGH;
			case ZBX_TOKEN_USER_MACRO:
			case ZBX_TOKEN_SIMPLE_MACRO:
			case ZBX_TOKEN_MACRO:
				pos = token.loc.r;
				break;
		}
	}

	zbx_vector_uint64_sort(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: get_N_itemid                                                     *
 *                                                                            *
 * Parameters: expression   - [IN] null terminated trigger expression         *
 *                            '{11}=1 & {2346734}>5'                          *
 *             N_functionid - [IN] number of function in trigger expression   *
 *             itemid       - [OUT] ID of an item of N-th function in         *
 *                            expression                                      *
 *                                                                            *
 ******************************************************************************/
static int	get_N_itemid(const char *expression, int N_functionid, zbx_uint64_t *itemid)
{
	const char	*__function_name = "get_N_itemid";

	zbx_uint64_t	functionid;
	DC_FUNCTION	function;
	int		errcode, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s' N_functionid:%d",
			__function_name, expression, N_functionid);

	if (SUCCEED == get_N_functionid(expression, N_functionid, &functionid, NULL))
	{
		DCconfig_get_functions_by_functionids(&function, &functionid, &errcode, 1);

		if (SUCCEED == errcode)
		{
			*itemid = function.itemid;
			ret = SUCCEED;
		}

		DCconfig_clean_functions(&function, &errcode, 1);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_expanded_expression                                          *
 *                                                                            *
 * Purpose: get trigger expression with expanded user macros                  *
 *                                                                            *
 * Comments: removes ' ', '\r', '\n' and '\t' for easier number search        *
 *                                                                            *
 ******************************************************************************/
static char	*get_expanded_expression(const char *expression)
{
	char	*expression_ex;

	if (NULL != (expression_ex = DCexpression_expand_user_macros(expression)))
		zbx_remove_whitespace(expression_ex);

	return expression_ex;
}

/******************************************************************************
 *                                                                            *
 * Function: get_trigger_expression_constant                                  *
 *                                                                            *
 * Purpose: get constant from a trigger expression corresponding a given      *
 *          reference from trigger name                                       *
 *                                                                            *
 * Parameters: expression - [IN] trigger expression, source of constants      *
 *             reference  - [IN] reference from a trigger name ($1, $2, ...)  *
 *             constant   - [OUT] pointer to the constant's location in       *
 *                            trigger expression or empty string if there is  *
 *                            no corresponding constant                       *
 *             length     - [OUT] length of constant                          *
 *                                                                            *
 ******************************************************************************/
static void	get_trigger_expression_constant(const char *expression, const zbx_token_reference_t *reference,
		const char **constant, size_t *length)
{
	size_t		pos;
	zbx_strloc_t	number;
	int		index;

	for (pos = 0, index = 1; SUCCEED == zbx_number_find(expression, pos, &number); pos = number.r + 1, index++)
	{
		if (index < reference->index)
			continue;

		*length = number.r - number.l + 1;
		*constant = expression + number.l;
		return;
	}

	*length = 0;
	*constant = "";
}

static void	DCexpand_trigger_expression(char **expression)
{
	const char	*__function_name = "DCexpand_trigger_expression";

	char		*tmp = NULL;
	size_t		tmp_alloc = 256, tmp_offset = 0, l, r;
	DC_FUNCTION	function;
	DC_ITEM		item;
	zbx_uint64_t	functionid;
	int		errcode[2];

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __function_name, *expression);

	tmp = (char *)zbx_malloc(tmp, tmp_alloc);

	for (l = 0; '\0' != (*expression)[l]; l++)
	{
		if ('{' != (*expression)[l])
		{
			zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, (*expression)[l]);
			continue;
		}

		/* skip user macros */
		if ('$' == (*expression)[l + 1])
		{
			int	macro_r, context_l, context_r;

			if (SUCCEED == zbx_user_macro_parse(*expression + l, &macro_r, &context_l, &context_r))
			{
				zbx_strncpy_alloc(&tmp, &tmp_alloc, &tmp_offset, *expression + l, macro_r + 1);
				l += macro_r;
				continue;
			}

			zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, '{');
			zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, '$');
			l++;
			continue;
		}

		for (r = l + 1; 0 != isdigit((*expression)[r]); r++)
			;

		if ('}' != (*expression)[r])
		{
			zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, (*expression)[l]);
			continue;
		}

		(*expression)[r] = '\0';

		if (SUCCEED == is_uint64(&(*expression)[l + 1], &functionid))
		{
			DCconfig_get_functions_by_functionids(&function, &functionid, &errcode[0], 1);

			if (SUCCEED == errcode[0])
			{
				DCconfig_get_items_by_itemids(&item, &function.itemid, &errcode[1], 1);

				if (SUCCEED == errcode[1])
				{
					zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, '{');
					zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, item.host.host);
					zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, ':');
					zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, item.key_orig);
					zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, '.');
					zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, function.function);
					zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, '(');
					zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, function.parameter);
					zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, ")}");
				}

				DCconfig_clean_items(&item, &errcode[1], 1);
			}

			DCconfig_clean_functions(&function, &errcode[0], 1);

			if (SUCCEED != errcode[0] || SUCCEED != errcode[1])
				zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, "*ERROR*");

			l = r;
		}
		else
			zbx_chrcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, (*expression)[l]);

		(*expression)[r] = '}';
	}

	zbx_free(*expression);
	*expression = tmp;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() expression:'%s'", __function_name, *expression);
}

/******************************************************************************
 *                                                                            *
 * Function: get_trigger_severity_name                                        *
 *                                                                            *
 * Purpose: get trigger severity name                                         *
 *                                                                            *
 * Parameters: trigger    - [IN] a trigger data with priority field;          *
 *                               TRIGGER_SEVERITY_*                           *
 *             replace_to - [OUT] pointer to a buffer that will receive       *
 *                          a null-terminated trigger severity string         *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	get_trigger_severity_name(unsigned char priority, char **replace_to)
{
	zbx_config_t	cfg;

	if (TRIGGER_SEVERITY_COUNT <= priority)
		return FAIL;

	zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_SEVERITY_NAME);

	*replace_to = zbx_strdup(*replace_to, cfg.severity_name[priority]);

	zbx_config_clean(&cfg);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: get_problem_update_actions                                       *
 *                                                                            *
 * Purpose: get human readable list of problem update actions                 *
 *                                                                            *
 * Parameters: ack     - [IN] the acknowledge (problem update) data           *
 *             actions - [IN] the required action flags                       *
 *             out     - [OUT] the output buffer                              *
 *                                                                            *
 * Return value: SUCCEED - successfully returned list of problem update       *
 *               FAIL    - no matching actions were made                      *
 *                                                                            *
 ******************************************************************************/
static int	get_problem_update_actions(const DB_ACKNOWLEDGE *ack, int actions, char **out)
{
	char	*buf = NULL, *prefixes[] = {"", ", ", ", ", ", "};
	size_t	buf_alloc = 0, buf_offset = 0;
	int	i, index, flags;

	if (0 == (flags = ack->action & actions))
		return FAIL;

	for (i = 0, index = 0; i < ZBX_PROBLEM_UPDATE_ACTION_COUNT; i++)
	{
		if (0 != (flags & (1 << i)))
			index++;
	}

	if (1 < index)
		prefixes[index - 1] = " and ";

	index = 0;

	if (0 != (flags & ZBX_PROBLEM_UPDATE_ACKNOWLEDGE))
	{
		zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, "acknowledged");
		index++;
	}

	if (0 != (flags & ZBX_PROBLEM_UPDATE_MESSAGE))
	{
		zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, prefixes[index++]);
		zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, "commented");
	}

	if (0 != (flags & ZBX_PROBLEM_UPDATE_SEVERITY))
	{
		zbx_config_t	cfg;
		const char	*from = "unknown", *to = "unknown";

		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_SEVERITY_NAME);

		if (TRIGGER_SEVERITY_COUNT > ack->old_severity && 0 <= ack->old_severity)
			from = cfg.severity_name[ack->old_severity];

		if (TRIGGER_SEVERITY_COUNT > ack->new_severity && 0 <= ack->new_severity)
			to = cfg.severity_name[ack->new_severity];

		zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, prefixes[index++]);
		zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "changed severity from %s to %s",
				from, to);

		zbx_config_clean(&cfg);
	}

	if (0 != (flags & ZBX_PROBLEM_UPDATE_CLOSE))
	{
		zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, prefixes[index++]);
		zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, "closed");
	}

	zbx_free(*out);
	*out = buf;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: item_description                                                 *
 *                                                                            *
 * Purpose: substitute key parameters and user macros in                      *
 *          the item description string with real values                      *
 *                                                                            *
 ******************************************************************************/
static void	item_description(char **data, const char *key, zbx_uint64_t hostid)
{
	AGENT_REQUEST	request;
	const char	*param;
	char		c, *p, *m, *n, *str_out = NULL, *replace_to = NULL;
	int		macro_r, context_l, context_r;

	init_request(&request);

	if (SUCCEED != parse_item_key(key, &request))
		goto out;

	p = *data;

	while (NULL != (m = strchr(p, '$')))
	{
		if (m > p && '{' == *(m - 1) && FAIL != zbx_user_macro_parse(m - 1, &macro_r, &context_l, &context_r))
		{
			/* user macros */

			n = m + macro_r;
			c = *n;
			*n = '\0';
			DCget_user_macro(&hostid, 1, m - 1, &replace_to);

			if (NULL != replace_to)
			{
				*(m - 1) = '\0';
				str_out = zbx_strdcat(str_out, p);
				*(m - 1) = '{';

				str_out = zbx_strdcat(str_out, replace_to);
				zbx_free(replace_to);
			}
			else
				str_out = zbx_strdcat(str_out, p);

			*n = c;
			p = n;
		}
		else if ('1' <= *(m + 1) && *(m + 1) <= '9')
		{
			/* macros $1, $2, ... */

			*m = '\0';
			str_out = zbx_strdcat(str_out, p);
			*m++ = '$';

			if (NULL != (param = get_rparam(&request, *m - '0' - 1)))
				str_out = zbx_strdcat(str_out, param);

			p = m + 1;
		}
		else
		{
			/* just a dollar sign */

			c = *++m;
			*m = '\0';
			str_out = zbx_strdcat(str_out, p);
			*m = c;
			p = m;
		}
	}

	if (NULL != str_out)
	{
		str_out = zbx_strdcat(str_out, p);
		zbx_free(*data);
		*data = str_out;
	}
out:
	free_request(&request);
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_host_value                                                 *
 *                                                                            *
 * Purpose: request host name by hostid                                       *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	DBget_host_value(zbx_uint64_t hostid, char **replace_to, const char *field_name)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	result = DBselect(
			"select %s"
			" from hosts"
			" where hostid=" ZBX_FS_UI64,
			field_name, hostid);

	if (NULL != (row = DBfetch(result)))
	{
		*replace_to = zbx_strdup(*replace_to, row[0]);
		ret = SUCCEED;
	}
	DBfree_result(result);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_templateid_by_triggerid                                    *
 *                                                                            *
 * Purpose: get template trigger ID from which the trigger is inherited       *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	DBget_templateid_by_triggerid(zbx_uint64_t triggerid, zbx_uint64_t *templateid)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	result = DBselect(
			"select templateid"
			" from triggers"
			" where triggerid=" ZBX_FS_UI64,
			triggerid);

	if (NULL != (row = DBfetch(result)))
	{
		ZBX_DBROW2UINT64(*templateid, row[0]);
		ret = SUCCEED;
	}
	DBfree_result(result);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_trigger_template_name                                      *
 *                                                                            *
 * Purpose: get comma-space separated trigger template names in which         *
 *          the trigger is defined                                            *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 * Comments: based on the patch submitted by Hmami Mohamed                    *
 *                                                                            *
 ******************************************************************************/
static int	DBget_trigger_template_name(zbx_uint64_t triggerid, const zbx_uint64_t *userid, char **replace_to)
{
	const char	*__function_name = "DBget_trigger_template_name";

	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;
	zbx_uint64_t	templateid;
	char		*sql = NULL;
	size_t		replace_to_alloc = 64, replace_to_offset = 0,
			sql_alloc = 256, sql_offset = 0;
	int		user_type = -1;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (NULL != userid)
	{
		result = DBselect("select type from users where userid=" ZBX_FS_UI64, *userid);

		if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
			user_type = atoi(row[0]);
		DBfree_result(result);

		if (-1 == user_type)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot check permissions", __function_name);
			goto out;
		}
	}

	/* use parent trigger ID for lld generated triggers */
	result = DBselect(
			"select parent_triggerid"
			" from trigger_discovery"
			" where triggerid=" ZBX_FS_UI64,
			triggerid);

	if (NULL != (row = DBfetch(result)))
		ZBX_STR2UINT64(triggerid, row[0]);
	DBfree_result(result);

	if (SUCCEED != DBget_templateid_by_triggerid(triggerid, &templateid) || 0 == templateid)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trigger not found or not templated", __function_name);
		goto out;
	}

	do
	{
		triggerid = templateid;
	}
	while (SUCCEED == (ret = DBget_templateid_by_triggerid(triggerid, &templateid)) && 0 != templateid);

	if (SUCCEED != ret)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trigger not found", __function_name);
		goto out;
	}

	*replace_to = (char *)zbx_realloc(*replace_to, replace_to_alloc);
	**replace_to = '\0';

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct h.name"
			" from hosts h,items i,functions f"
			" where h.hostid=i.hostid"
				" and i.itemid=f.itemid"
				" and f.triggerid=" ZBX_FS_UI64,
			triggerid);
	if (NULL != userid && USER_TYPE_SUPER_ADMIN != user_type)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				" and exists("
					"select null"
					" from hosts_groups hg,rights r,users_groups ug"
					" where h.hostid=hg.hostid"
						" and hg.groupid=r.id"
						" and r.groupid=ug.usrgrpid"
						" and ug.userid=" ZBX_FS_UI64
					" group by hg.hostid"
					" having min(r.permission)>=%d"
				")",
				*userid, PERM_READ);
	}
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by h.name");

	result = DBselect("%s", sql);

	zbx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		if (0 != replace_to_offset)
			zbx_strcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, ", ");
		zbx_strcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, row[0]);
	}
	DBfree_result(result);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_trigger_hostgroup_name                                     *
 *                                                                            *
 * Purpose: get comma-space separated host group names in which the trigger   *
 *          is defined                                                        *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	DBget_trigger_hostgroup_name(zbx_uint64_t triggerid, const zbx_uint64_t *userid, char **replace_to)
{
	const char	*__function_name = "DBget_trigger_hostgroup_name";

	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;
	char		*sql = NULL;
	size_t		replace_to_alloc = 64, replace_to_offset = 0,
			sql_alloc = 256, sql_offset = 0;
	int		user_type = -1;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (NULL != userid)
	{
		result = DBselect("select type from users where userid=" ZBX_FS_UI64, *userid);

		if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
			user_type = atoi(row[0]);
		DBfree_result(result);

		if (-1 == user_type)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot check permissions", __function_name);
			goto out;
		}
	}

	*replace_to = (char *)zbx_realloc(*replace_to, replace_to_alloc);
	**replace_to = '\0';

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct g.name"
			" from hstgrp g,hosts_groups hg,items i,functions f"
			" where g.groupid=hg.groupid"
				" and hg.hostid=i.hostid"
				" and i.itemid=f.itemid"
				" and f.triggerid=" ZBX_FS_UI64,
			triggerid);
	if (NULL != userid && USER_TYPE_SUPER_ADMIN != user_type)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				" and exists("
					"select null"
					" from rights r,users_groups ug"
					" where g.groupid=r.id"
						" and r.groupid=ug.usrgrpid"
						" and ug.userid=" ZBX_FS_UI64
					" group by r.id"
					" having min(r.permission)>=%d"
				")",
				*userid, PERM_READ);
	}
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by g.name");

	result = DBselect("%s", sql);

	zbx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		if (0 != replace_to_offset)
			zbx_strcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, ", ");
		zbx_strcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, row[0]);
		ret = SUCCEED;
	}
	DBfree_result(result);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_interface_value                                              *
 *                                                                            *
 * Purpose: retrieve a particular value associated with the interface         *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	get_interface_value(zbx_uint64_t hostid, zbx_uint64_t itemid, char **replace_to, int request)
{
	int		res;
	DC_INTERFACE	interface;

	if (SUCCEED != (res = DCconfig_get_interface(&interface, hostid, itemid)))
		return res;

	switch (request)
	{
		case ZBX_REQUEST_HOST_IP:
			*replace_to = zbx_strdup(*replace_to, interface.ip_orig);
			break;
		case ZBX_REQUEST_HOST_DNS:
			*replace_to = zbx_strdup(*replace_to, interface.dns_orig);
			break;
		case ZBX_REQUEST_HOST_CONN:
			*replace_to = zbx_strdup(*replace_to, interface.addr);
			break;
		case ZBX_REQUEST_HOST_PORT:
			*replace_to = zbx_strdup(*replace_to, interface.port_orig);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			res = FAIL;
	}

	return res;
}

static int	get_host_value(zbx_uint64_t itemid, char **replace_to, int request)
{
	int	ret;
	DC_HOST	host;

	DCconfig_get_hosts_by_itemids(&host, &itemid, &ret, 1);

	if (FAIL == ret)
		return FAIL;

	switch (request)
	{
		case ZBX_REQUEST_HOST_ID:
			*replace_to = zbx_dsprintf(*replace_to, ZBX_FS_UI64, host.hostid);
			break;
		case ZBX_REQUEST_HOST_HOST:
			*replace_to = zbx_strdup(*replace_to, host.host);
			break;
		case ZBX_REQUEST_HOST_NAME:
			*replace_to = zbx_strdup(*replace_to, host.name);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			ret = FAIL;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_substitute_item_name_macros                                  *
 *                                                                            *
 * Purpose: substitute key macros and use it to substitute item name macros if*
 *          item name is specified                                            *
 *                                                                            *
 * Parameters: dc_item    - [IN] item information used in substitution        *
 *             name       - [IN] optional item name to substitute             *
 *             replace_to - [OUT] expanded item name or key if name is absent *
 *                                                                            *
 ******************************************************************************/
int	zbx_substitute_item_name_macros(DC_ITEM *dc_item, const char *name, char **replace_to)
{
	int	ret;
	char	*key;

	if (INTERFACE_TYPE_UNKNOWN == dc_item->interface.type)
		ret = DCconfig_get_interface(&dc_item->interface, dc_item->host.hostid, 0);
	else
		ret = SUCCEED;

	if (ret == FAIL)
		return FAIL;

	key = zbx_strdup(NULL, dc_item->key_orig);
	substitute_key_macros(&key, NULL, dc_item, NULL, MACRO_TYPE_ITEM_KEY,
			NULL, 0);

	if (NULL != name)
	{
		*replace_to = zbx_strdup(*replace_to, name);
		item_description(replace_to, key, dc_item->host.hostid);
		zbx_free(key);
	}
	else	/* ZBX_REQUEST_ITEM_KEY */
	{
		zbx_free(*replace_to);
		*replace_to = key;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_item_value                                                 *
 *                                                                            *
 * Purpose: retrieve a particular value associated with the item              *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	DBget_item_value(zbx_uint64_t itemid, char **replace_to, int request)
{
	const char	*__function_name = "DBget_item_value";
	DB_RESULT	result;
	DB_ROW		row;
	DC_ITEM		dc_item;
	zbx_uint64_t	proxy_hostid;
	int		ret = FAIL, errcode;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	switch (request)
	{
		case ZBX_REQUEST_HOST_IP:
		case ZBX_REQUEST_HOST_DNS:
		case ZBX_REQUEST_HOST_CONN:
		case ZBX_REQUEST_HOST_PORT:
			return get_interface_value(0, itemid, replace_to, request);
		case ZBX_REQUEST_HOST_ID:
		case ZBX_REQUEST_HOST_HOST:
		case ZBX_REQUEST_HOST_NAME:
			return get_host_value(itemid, replace_to, request);
	}

	result = DBselect(
			"select h.proxy_hostid,h.description,i.itemid,i.name,i.key_,i.description"
			" from items i"
				" join hosts h on h.hostid=i.hostid"
			" where i.itemid=" ZBX_FS_UI64, itemid);

	if (NULL != (row = DBfetch(result)))
	{
		switch (request)
		{
			case ZBX_REQUEST_HOST_DESCRIPTION:
				*replace_to = zbx_strdup(*replace_to, row[1]);
				ret = SUCCEED;
				break;
			case ZBX_REQUEST_ITEM_ID:
				*replace_to = zbx_strdup(*replace_to, row[2]);
				ret = SUCCEED;
				break;
			case ZBX_REQUEST_ITEM_NAME:
				DCconfig_get_items_by_itemids(&dc_item, &itemid, &errcode, 1);

				if (SUCCEED == errcode)
					ret = zbx_substitute_item_name_macros(&dc_item, row[3], replace_to);

				DCconfig_clean_items(&dc_item, &errcode, 1);
				break;
			case ZBX_REQUEST_ITEM_KEY:
				DCconfig_get_items_by_itemids(&dc_item, &itemid, &errcode, 1);

				if (SUCCEED == errcode)
					ret = zbx_substitute_item_name_macros(&dc_item, NULL, replace_to);

				DCconfig_clean_items(&dc_item, &errcode, 1);
				break;
			case ZBX_REQUEST_ITEM_NAME_ORIG:
				*replace_to = zbx_strdup(*replace_to, row[3]);
				ret = SUCCEED;
				break;
			case ZBX_REQUEST_ITEM_KEY_ORIG:
				*replace_to = zbx_strdup(*replace_to, row[4]);
				ret = SUCCEED;
				break;
			case ZBX_REQUEST_ITEM_DESCRIPTION:
				*replace_to = zbx_strdup(*replace_to, row[5]);
				ret = SUCCEED;
				break;
			case ZBX_REQUEST_PROXY_NAME:
				ZBX_DBROW2UINT64(proxy_hostid, row[0]);

				if (0 == proxy_hostid)
				{
					*replace_to = zbx_strdup(*replace_to, "");
					ret = SUCCEED;
				}
				else
					ret = DBget_host_value(proxy_hostid, replace_to, "host");
				break;
			case ZBX_REQUEST_PROXY_DESCRIPTION:
				ZBX_DBROW2UINT64(proxy_hostid, row[0]);

				if (0 == proxy_hostid)
				{
					*replace_to = zbx_strdup(*replace_to, "");
					ret = SUCCEED;
				}
				else
					ret = DBget_host_value(proxy_hostid, replace_to, "description");
				break;
		}
	}
	DBfree_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_trigger_value                                              *
 *                                                                            *
 * Purpose: retrieve a particular value associated with the trigger's         *
 *          N_functionid'th function                                          *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	DBget_trigger_value(const char *expression, char **replace_to, int N_functionid, int request)
{
	const char	*__function_name = "DBget_trigger_value";

	zbx_uint64_t	itemid;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (SUCCEED == get_N_itemid(expression, N_functionid, &itemid))
		ret = DBget_item_value(itemid, replace_to, request);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_trigger_event_count                                        *
 *                                                                            *
 * Purpose: retrieve number of events (acknowledged or unacknowledged) for a  *
 *          trigger (in an OK or PROBLEM state) which generated an event      *
 *                                                                            *
 * Parameters: triggerid    - [IN] trigger identifier from database           *
 *             replace_to   - [IN/OUT] pointer to result buffer               *
 *             problem_only - [IN] selected trigger status:                   *
 *                             0 - TRIGGER_VALUE_PROBLEM and TRIGGER_VALUE_OK *
 *                             1 - TRIGGER_VALUE_PROBLEM                      *
 *             acknowledged - [IN] acknowledged event or not                  *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是查询触发器对应的事件计数。根据传入的参数，判断是否仅查找问题，并将查询结果存储到 replace_to 指向的字符串中。最后返回查询成功与否的标志。
 ******************************************************************************/
// 定义一个名为 DBget_trigger_event_count 的静态函数，该函数接收 4 个参数：
// zbx_uint64_t 类型的 triggerid，用于标识触发器；
// 字符指针类型的 replace_to，用于存储查询结果；
// 整型类型的 problem_only，表示是否仅查找问题；
// 整型类型的 acknowledged，表示是否已确认。
static int	DBget_trigger_event_count(zbx_uint64_t triggerid, char **replace_to, int problem_only, int acknowledged)
{
	// 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果；
	// 定义一个 DB_ROW 类型 * Function: DBget_dhost_value_by_event                                       *
 *                                                                            *
 * Purpose: retrieve discovered host value by event and field name            *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static int	DBget_dhost_value_by_event(const DB_EVENT *event, char **replace_to, const char *fieldname)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;
	char		sql[MAX_STRING_LEN];

	switch (event->object)
	{
		case EVENT_OBJECT_DHOST:
			zbx_snprintf(sql, sizeof(sql),
					"select %s"
					" from drules r,dhosts h,dservices s"
					" where r.druleid=h.druleid"
						" and h.dhostid=s.dhostid"
						" and h.dhostid=" ZBX_FS_UI64
					" order by s.dserviceid",
					fieldname,
					event->objectid);
			break;
		case EVENT_OBJECT_DSERVICE:
			zbx_snprintf(sql, sizeof(sql),
					"select %s"
					" from drules r,dhosts h,dservices s"
					" where r.druleid=h.druleid"
						" and h.dhostid=s.dhostid"
						" and s.dserviceid=" ZBX_FS_UI64,
					fieldname,
					event->objectid);
			break;
		default:
			return ret;
	}

	result = DBselectN(sql, 1);

	if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
	{
		*replace_to = zbx_strdup(*replace_to, row[0]);
		ret = SUCCEED;
	}
	DBfree_result(result);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBget_dchecks_value_by_event                                     *
 *                                                                            *
 * Purpose: retrieve discovery rule check value by event and field name       *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个静态函数 DBget_dchecks_value_by_event，接收三个参数：
 * 1. const DB_EVENT *event：数据库事件结构体指针，包含事件相关信息；
 * 2. char **replace_to：指向字符指针的指针，用于存储查询结果；
 * 3. const char *fieldname：字符串指针，表示需要查询的字段名。
 *
 * 函数主要目的是根据事件类型和字段名查询数据库，并将查询结果存储在 replace_to 指向的字符串中。
 * 返回值是查询结果是否成功，成功返回 SUCCEED，失败返回 FAIL。
 */
static int	DBget_dchecks_value_by_event(const DB_EVENT *event, char **replace_to, const char *fieldname)
{
	/* 定义变量 result、row 和 ret，分别为 DB_RESULT 类型、DB_ROW 类型和 int 类型，初始值分别为 NULL、NULL 和 FAIL。
 * 后续根据事件类型和字段名执行不同的查询操作。
 */
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	/* 根据事件类型进行switch分支判断：
 * 1. 当事件类型为 EVENT_OBJECT_DSERVICE 时，执行以下查询操作：
 *      使用 DBselect 函数查询数据库，将查询结果存储在 result 变量中。
 *      查询语句为：select %s from dchecks c,dservices s
 *      其中，%s 为字段名占位符，event->objectid 为条件值。
 * 2. 当事件类型不为 EVENT_OBJECT_DSERVICE 时，直接返回初始值 ret（即 FAIL）。
 */
	switch (event->object)
	{
		case EVENT_OBJECT_DSERVICE:
			result = DBselect("select %s from dchecks c,dservices s"
					" where c.dcheckid=s.dcheckid and s.dserviceid=" ZBX_FS_UI64,
					fieldname, event->objectid);
			break;
		default:
			return ret;
	}

	/* 使用 DBfetch 函数获取查询结果，并将结果存储在 row 变量中。
 * 如果 row 变量不为空且 DBis_null(row[0]) 不等于 SUCCEED，则执行以下操作：
 * 1. 将查询结果复制到 replace_to 指向的字符串中；
 * 2. 更新 ret 值为 SUCCEED；
 */
	if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
	{
		*replace_to = zbx_strdup(*replace_to, row[0]);
		ret = SUCCEED;
	}

	/* 释放查询结果占用资源，防止内存泄漏。 */
	DBfree_result(result);

	/* 返回查询结果，即 ret 变量值。 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: DBget_dservice_value_by_event                                    *
 *                                                                            *
 * Purpose: retrieve discovered service value by event and field name         *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的事件（event）和字段名（fieldname），查询DService表中对应的值，并将查询结果复制到replace_to指向的内存空间。如果查询成功，函数返回SUCCEED，否则返回FAIL。
 **********************************************		// 当object为EVENT_OBJECT_DSERVICE时，执行以下代码
		case EVENT_OBJECT_DSERVICE:
			// 执行数据库查询，从dservices表中获取指定DService的值
			result = DBselect("select %s from dservices s where s.dserviceid=" ZBX_FS_UI64,
					fieldname, event->objectid);
			break;

		// 当object不为EVENT_OBJECT_DSERVICE时，直接返回初始状态
		default:
			return ret;
	}

	// 判断查询结果是否为空，如果不为空，则执行以下代码
	if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
	{
		// 将查询结果的值复制到replace_to指向的内存空间
		*replace_to = zbx_strdup(*replace_to, row[0]);
		// 将函数执行结果设置为SUCCEED
		ret = SUCCEED;
	}

	// 释放查询结果占用的内存
	DBfree_result(result);

	// 返回函数执行结果
	return ret;
}


/***************************************************************************	// 判断查询结果是否为空，如果不为空，则执行以下代码
	if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
	{
		// 将查询结果的值复制到replace_to指向的内存空间
		*replace_to = zbx_strdup(*replace_to, row[0]);
		// 将函数执行结果设置为SUCCEED
		ret = SUCCEED;
	}

	// 释放查询结果占用的内存
	DBfree_result(result);

	// 返回函数执行结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: DBget_drule_value_by_event                                       *
 *                                                                            *
 * Purpose: retrieve discovery rule value by event and field name             *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的事件、字段名和替换指针，查询动态规则表（drules）中对应的记录，并将查询结果中的指定字段值复制到替换指针指向的内存空间。如果查询成功，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于根据事件获取动态规则字段值
static int	DBget_drule_value_by_event(const DB_EVENT *event, char **replace_to, const char *fieldname)
{
	// 定义一个DB_RESULT类型的变量result，用于存储查询结果
	// 定义一个DB_ROW类型的变量row，用于存储查询结果的一行数据
	// 定义一个整型变量ret，用于存储函数返回值，初始值为FAIL

	// 判断event中的source字段值是否为EVENT_SOURCE_DISCOVERY，如果不是，则直接返回FAIL
	// 如果event中的object字段值为EVENT_OBJECT_DHOST，则执行以下操作：
	// 1. 执行一个SQL查询，查询drules表中druleid与给定事件对象id相等，且dhostid与给定事件对象id相等的记录
	// 2. 将查询结果存储在result变量中
	// 否则，如果event中的object字段值不为EVENT_OBJECT_DHOST和EVENT_OBJECT_DSERVICE，则直接返回ret（初始值为FAIL）

	// 如果查询结果不为空，且第一行数据不为空，则执行以下操作：
	// 1. 将第一行数据中的指定字段值（fieldname）复制到replace_to指向的内存空间
	// 2. 将ret变量值设置为SUCCEED
	// 3. 释放result变量占用的内存

	// 最后，返回ret变量值
}


/******************************************************************************
 *                                                                            *
 * Function: DBget_history_log_value                                          *
 *                                                                            *
 * Purpose: retrieve a particular attribute of a log value                    *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
static int	DBget_history_log_value(zbx_uint64_t itemid, char **replace_to, int request, int clock, int ns)
{
	const char		*__function_name = "DBget_history_log_value";

	DC_ITEM			item;
	int			ret = FAIL, errcode = FAIL;
	zbx_timespec_t		ts = {clock, ns};
	zbx_history_record_t	value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);

	if (SUCCEED != errcode || ITEM_VALUE_TYPE_LOG != item.value_type)
		goto out;

	if (SUCCEED != zbx_vc_get_value(itemid, item.value_type, &ts, &value))
		goto out;

	switch (request)
	{
		case ZBX_REQUEST_ITEM_LOG_DATE:
			*replace_to = zbx_strdup(*replace_to, zbx_date2str((time_t)value.value.log->timestamp));
			goto success;
		case ZBX_REQUEST_ITEM_LOG_TIME:
			*replace_to = zbx_strdup(*replace_to, zbx_time2str((time_t)value.value.log->timestamp));
			goto success;
		case ZBX_REQUEST_ITEM_LOG_AGE:
			*replace_to = zbx_strdup(*replace_to, zbx_age2str(time(NULL) - value.value.log->timestamp));
			goto success;
	}

	/* the following attributes are set only for windows eventlog items */
	if (0 != strncmp(item.key_orig, "eventlog[", 9))
		goto clean;

	switch (request)
	{
		case ZBX_REQUEST_ITEM_LOG_SOURCE:
			*replace_to = zbx_strdup(*replace_to, (NULL == value.value.log->source ? "" :
					value.value.log->source));
			break;
		case ZBX_REQUEST_ITEM_LOG_SEVERITY:
			*replace_to = zbx_strdup(*replace_to,
					zbx_item_logtype_string((unsigned char)value.value.log->severity));
			break;
		case ZBX_REQUEST_ITEM_LOG_NSEVERITY:
			*replace_to = zbx_dsprintf(*replace_to, "%d", value.value.log->severity);
			break;
		case ZBX_REQUEST_ITEM_LOG_EVENTID:
			*replace_to = zbx_dsprintf(*replace_to, "%d", value.value.log->logeventid);
			break;
	}
success:
	ret = SUCCEED;
clean:
	zbx_history_record_clear(&value, ITEM_VALUE_TYPE_LOG);
out:
	DCconfig_clean_items(&item, &errcode, 1);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_history_log_value                                            *
 *                                                                            *
 * Purpose: retrieve a particular attribute of a log value                    *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
/*****************************************// 定义一个静态函数get_history_log_value，接收5个参数：
// 1. 一个字符串指针expression，表示要查询的表达式；
// 2. 一个字符指针指针replace_to，用于存储替换后的值；
// 3. 一个整数N_functionid，表示函数ID；
// 4. 两个整数request和clock，分别表示请求类型和时间戳；
// 5. 一个整数ns，表示毫秒数。
static int get_history_log_value(const char *expression, char **replace_to, int N_functionid,
                                int request, int clock, int ns)
{
    // 定义一个常量字符串__function_name，用于打印日志时表示函数名；

    zbx_uint64_t	itemid; // 定义一个zbx_uint64_t类型的变量itemid，用于存储查询结果的itemid；
    int		ret = FAIL; // 定义一个整数变量ret，初始值为FAIL，表示操作失败；

    // 打印调试日志，表示进入get_history_log_value�static int get_history_log_value(const char *expression, char **replace_to, int N_functionid,
                                int request, int clock, int ns)
{
    // 定义一个常量字符串__function_name，用于打印日志时表示函数名；

    zbx_uint64_t	itemid; // 定义一个zbx_uint64_t类型的变量itemid，用于存储查询结果的itemid；
    int		ret = FAIL; // 定义一个整数变量ret，初始值为FAIL，表示操作失败；

    // 打印调试日志，表示进入get_history_log_value函数；
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 调用get_N_itemid函数，根据表达式和N_functionid获取itemid，如果成功，将ret设置为SUCCEED，否则为FAIL；
    if (SUCCEED == get_N_itemid(expression, N_functionid, &itemid))
    {
        ret = DBget_history_log_value(itemid, replace_to, request, clock, ns); // 调用DBget_history_log_value函数，根据itemid、替换值指针、请求类型、时间戳和毫秒数获取历史日志值，并将结果存储在replace_to指向的字符串中；
    }

    // 打印调试日志，表示结束get_history_log_value函数，并输出结果；
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回ret，表示操作结果。
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: DBitem_lastvalue                                                 *
 *                                                                            *
 * Purpose: retrieve item lastvalue by trigger expression                     *
 *          and number of function                                            *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个给定的表达式、功能ID和时间戳获取Zabbix数据库中的最后一个值，并将结果保存到指定的`lastvalue`指针指向的字符串中。如果不需要原始数据，则对数据进行格式化并存储。如果需要原始数据，则直接保存原始数据。函数返回成功或失败的状态。
 ******************************************************************************/
// 定义一个名为 DBitem_lastvalue 的静态函数，该函数用于获取 Zabbix 数据库中指定表达式、功能ID和时间戳的最后一个值
static int DBitem_lastvalue(const char *expression, char **lastvalue, int N_functionid, int raw)
{
	// 定义一些常量和变量
	const char *__function_name = "DBitem_lastvalue"; // 函数名
	zbx_uint64_t	itemid; // 物品ID
	int		ret = FAIL; // 函数返回值

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 获取物品ID
	if (FAIL == get_N_itemid(expression, N_functionid, &itemid))
		goto out; // 获取失败则退出函数

	// 从数据库中查询物品信息
	result = DBselect(
			"select value_type,valuemapid,units"
			" from items"
			" where itemid=" ZBX_FS_UI64,
			itemid);

	// 如果有数据返回
	if (NULL != (row = DBfetch(result)))
	{
		// 解析数据
		unsigned char		value_type;
		zbx_uint64_t		valuemapid;
		zbx_history_record_t	vc_value;
		zbx_timespec_t		ts;

		// 获取当前时间
		ts.sec = time(NULL);
		ts.ns = 999999999;

		value_type = (unsigned char)atoi(row[0]); // 解析值类型
		ZBX_DBROW2UINT64(valuemapid, row[1]); // 解析值映射ID

		// 获取物品最后一个值
		if (SUCCEED == zbx_vc_get_value(itemid, value_type, &ts, &vc_value))
		{
			char	tmp[MAX_BUFFER_LEN];

			// 将历史值转换为字符串
			zbx_history_value2str(tmp, sizeof(tmp), &vc_value.value, value_type);
			zbx_history_record_clear(&vc_value, value_type);

			// 处理原始数据
			if (0 == raw)
				zbx_format_value(tmp, sizeof(tmp), valuemapid, row[2], value_type);

			// 保存最后一个值
			*lastvalue = zbx_strdup(*lastvalue, tmp);

			ret = SUCCEED; // 返回成功
		}
	}
	// 释放数据库查询结果
	DBfree_result(result);

out:
	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数结果
	return ret;
}

		}
	}
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个给定的表达式和参数中，查询数据库中的item信息，并根据raw参数输出相应的值。以下是详细注释：
 *
 *1. 定义一个C语言函数`DBitem_value`，接收6个参数，其中两个指针参数`expression`和`value`用于输入表达式和输出值，四个整数参数`N_functionid`、`clock`、`ns`和`raw`用于控制查询行为。
 *2. 定义一个常量字符串`__function_name`，用于记录函数名。
 *3. 声明变量`result`、`row`、`itemid`、`ret`，分别用于存储数据库查询结果、查询结果行、itemid和函数返回值。
 *4. 使用`zabbix_log`打印调试信息，表示进入函数。
 *5. 调用`get_N_itemid`函数获取itemid，如果失败则跳转至`out`标签。
 *6. 使用`DBselect`查询数据库，提取查询结果。
 *7. 提取查询结果中的value_type、valuemapid和units。
 *8. 调用`zbx_vc_get_value`函数获取itemid的数据，如果成功，则继续处理。
 *9. 将历史记录值转换为字符串，并清除历史记录。
 *10. 根据`raw`参数处理输出值，并格式化输出。
 *11. 保存结果到`value`指针参数。
 *12. 释放查询结果。
 *13. 使用`zabbix_log`打印调试信息，表示函数执行结束。
 *14. 返回函数结果。
 *
 *整个代码块主要用于查询数据库中的item信息，并根据不同的参数处理输出值，最终返回处理后的值。
 ******************************************************************************/
static int	DBitem_value(const char *expression, char **value, int N_functionid, int clock, int ns, int raw)
{
	/* 定义函数名 */
	const char	*__function_name = "DBitem_value";

	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	itemid;
	int		ret = FAIL;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 获取itemid */
	if (FAIL == get_N_itemid(expression, N_functionid, &itemid))
		goto out;

	/* 从数据库中查询item信息 */
	result = DBselect(
			"select value_type,valuemapid,units"
			" from items"
			" where itemid=" ZBX_FS_UI64,
			itemid);

	/* 提取查询结果 */
	if (NULL != (row = DBfetch(result)))
	{
		/* 获取value_type */
		unsigned char		value_type;
		zbx_uint64_t		valuemapid;
		zbx_timespec_t		ts = {clock, ns};
		zbx_history_record_t	vc_value;

		value_type = (unsigned char)atoi(row[0]);
		ZBX_DBROW2UINT64(valuemapid, row[1]);

		/* 获取itemid的数据 */
		if (SUCCEED == zbx_	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	/* 返回结果 */
	return ret;
}

			zbx_history_value2str(tmp, sizeof(tmp), &vc_value.value, value_type);
			zbx_history_record_clear(&vc_value, value_type);

			if (0 == raw)
				zbx_format_value(tmp, sizeof(tmp), valuemapid, row[2], value_type);

			*value = zbx_strdup(*value, tmp);

			ret = SUCCEED;
		}
	}
	DBfree_result(result);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_escalation_history                                           *
 *                                                                            *			*value = zbx_strdup(*value, tmp);

			ret = SUCCEED;
		}
	}
	DBfree_result(result);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_escalation_history                                           *
 *                                                                            *
 * Purpose: retrieve escalation history                                       *
 *                                                                            *
 ******************************************************************************/
static void	get_escalation_history(zbx_uint64_t actionid, const DB_EVENT *event, const DB_EVENT *r_event,
			char **replace_to, const zbx_uint64_t *recipient_userid)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*buf = NULL, *p;
	size_t		buf_alloc = ZBX_KIBIBYTE, buf_offset = 0;
	int		esc_step;
	unsigned char	type, status;
	time_t		now;
	zbx_uint64_t	userid;

	buf = (char *)zbx_malloc(buf, buf_alloc);

	zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "Problem started: %s %s Age: %s\n",
			zbx_date2str(event->clock), zbx_time2str(event->clock),
			zbx_age2str(time(NULL) - event->clock));

	result = DBselect("select a.clock,a.alerttype,a.status,mt.description,a.sendto"
				",a.error,a.esc_step,a.userid,a.message"
			" from alerts a"
			" left join media_type mt"
				" on mt.mediatypeid=a.mediatypeid"
			" where a.eventid=" ZBX_FS_UI64
				" and a.actionid=" ZBX_FS_UI64
			" order by a.clock",
			event->eventid, actionid);

	while (NULL != (row = DBfetch(result)))
	{
		int	user_permit;

		now = atoi(row[0]);
		type = (unsigned char)atoi(row[1]);
		status = (unsigned char)atoi(row[2]);
		esc_step = atoi(row[6]);
		ZBX_DBROW2UINT64(userid, row[7]);
		user_permit = zbx_check_user_permissions(&userid, recipient_userid);

		if (0 != esc_step)
			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "%d. ", esc_step);

		zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "%s %s %-7s %-11s",
				zbx_date2str(now), zbx_time2str(now),	/* date, time */
				zbx_alert_type_string(type),		/* alert type */
				zbx_alert_status_string(type, status));	/* alert status */

		if (ALERT_TYPE_COMMAND == type)
		{
			if (NULL != (p = strchr(row[8], ':')))
			{
				*p = '\0';
				zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, " \"%s\"", row[8]);	/* host */
				*p = ':';
			}
		}
		else
		{
			const char	*description, *send_to, *user_name;

			description = (SUCCEED == DBis_null(row[3]) ? "" : row[3]);

			if (SUCCEED == user_permit)
			{
				send_to = row[4];
				user_name = zbx_user_string(userid);
			}
			else
			{
				send_to = "\"Inaccessible recipient details\"";
				user_name = "Inaccessible user";
			}

			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, " %s %s \"%s\"",
					description,	/* media type description */
					send_to,	/* historical recipient */
					user_name);	/* alert user full name */
		}

		if (ALERT_STATUS_FAILED == status)
		{
			/* alert error can be generated by SMTP Relay or other media and contain sensitive details */
			if (SUCCEED == user_permit)
				zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, " %s", row[5]);
			else
				zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, " \"Inaccessible error message\"");
		}

		zbx_chrcpy_alloc(&buf, &buf_alloc, &buf_offset, '\n');
	}
	DBfree_result(result);

	if (NULL != r_event)
	{
		zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "Problem ended: %s %s\n",
				zbx_date2str(r_event->clock), zbx_time2str(r_event->clock));
	}

	if (0 != buf_offset)
		buf[--buf_offset] = '\0';
/******************************************************************************
 * *
 *整个代码块的主要目的是获取某个事件（event）的更新历史，并将结果输出到一个字符串（replace_to）中。具体操作如下：
 *
 *1. 分配内存存储查询结果。
 *2. 执行数据库查询，获取事件id对应的事件更新记录。
 *3. 遍历查询结果，解析每条记录的数据。
 *4. 检查用户权限，获取用户名。
 *5. 格式化输出事件更新记录，包括时间、用户名、操作、旧严重性、新严重性。
 *6. 获取问题更新操作，并添加到输出缓冲区。
 *7. 如果消息不为空，添加消息到输出缓冲区。
 *8. 添加换行符，分离每条记录。
 *9. 释放查询结果。
 *10. 构造输出字符串，并返回。
 ******************************************************************************/
/* 定义一个函数，获取事件更新历史 */
static void get_event_update_history(const DB_EVENT *event, char **replace_to, const zbx_uint64_t *recipient_userid)
{
	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	char		*buf = NULL;
	size_t		buf_alloc = ZBX_KIBIBYTE, buf_offset = 0;

	/* 分配内存存储查询结果 */
	buf = (char *)zbx_malloc(buf, buf_alloc);
	*buf = '\0';

	/* 执行数据库查询 */
	result = DBselect("select clock,userid,message,action,old_severity,new_severity"
			" from acknowledges"
			" where eventid=" ZBX_FS_UI64 " order by clock",
			event->eventid);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 解析数据 */
		const char	*user_name;
		char		*actions = NULL;
		DB_ACKNOWLEDGE	ack;

		ack.clock = atoi(row[0]);
		ZBX_STR2UINT64(ack.userid, row[1]);
		ack.message = row[2];
		ack.acknowledgeid = 0;
		ack.action = atoi(row[3]);
		ack.old_severity = atoi(row[4]);
		ack.new_severity = atoi(row[5]);

		/* 检查用户权限 */
		if (SUCCEED == zbx_check_user_permissions(&ack.userid, recipient_userid))
			user_name = zbx_user_string(ack.userid);
		else
			user_name = "Inaccessible user";

		/* 格式化输出 */
		zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset,
				"%s %s \"%s\"\
",
				zbx_date2str(ack.clock),
				zbx_time2str(ack.clock),
				user_name);

		/* 获取问题更新操作 */
		if (SUCCEED == get_problem_update_actions(&ack, ZBX_PROBLEM_UPDATE_ACKNOWLEDGE |
					ZBX_PROBLEM_UPDATE_CLOSE | ZBX_PROBLEM_UPDATE_SEVERITY, &actions))
		{
			/* 添加操作到输出缓冲区 */
			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "Actions: %s.\
", actions);
			zbx_free(actions);
		}

		/* 如果消息不为空，添加到输出缓冲区 */
		if ('\0' != *ack.message)
			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "%s\
", ack.message);

		/* 添加换行符 */
		zbx_chrcpy_alloc(&buf, &buf_alloc, &buf_offset, '\
');
	}
	/* 释放查询结果 */
	DBfree_result(result);

	/* 如果输出缓冲区有内容，构造字符串 */
	if (0 != buf_offset)
	{
		buf_offset -= 2;
		buf[buf_offset] = '\0';
	}

	/* 返回结果 */
	*replace_to = buf;
}

		}

		if ('\0' != *ack.message)
			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "%s\n", ack.message);

		zbx_chrcpy_alloc(&buf, &buf_alloc, &buf_offset, '\n');
	}
	DBfree_result(result);

	if (0 != buf_offset)
	{
		buf_offset -= 2;
		buf[buf_off * Parameters:                                                                *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************** *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是通过给定的数据库事件（DB_EVENT）和字段名（fieldname），查询自动注册表（autoreg_host）中的相应值（autoreg_value），并将查询结果存储在replace_to指向的内存中。如果查询结果为空，则释放replace_to指向的内存。函数返回0表示成功，非0表示失败。
 ******************************************************************************/
/* 定义一个函数，通过事件（event）获取自动注册值（autoreg_value）
 * 参数：
 *   const DB_EVENT *event：数据库事件指针
 *   char **replace_to：用于存储查询结果的指针
 *   const char *fieldname：字段名
 * 返回值：
 *   int：0表示成功，非0表示失败
 */
static int	get_autoreg_value_by_event(const DB_EVENT *event, char **replace_to, const char *fieldname)
{
	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	/* 执行数据库查询 */
	result = DBselect(
			"select %s"
			" from autoreg_host"
			" where autoreg_hostid=" ZBX_FS_UI64, fieldname, event->objectid);

	/* 判断查询结果是否有数据 */
	if (NULL != (row = DBfetch(result)))
	{
		/* 判断第一个字段是否为空 */
		if (SUCCEED == DBis_null(row[0]))
		{
			/* 如果为空，释放replace_to指向的内存 */
			zbx_free(*replace_to);
		}
		else
		{
			/* 如果不为空，复制查询结果到replace_to指向的内存 */
			*replace_to = zbx_strdup(*replace_to, row[0]);
		}

		/* 设置返回值為成功 */
		ret = SUCCEED;
	}

	/* 释放查询结果 */
	DBfree_result(result);

	/* 返回结果 */
	return ret;
}


#define MVAR_ACTION			"{ACTION."			/* a prefix for all action macros */
#define MVAR_ACTION_ID			MVAR_ACTION "ID}"
#define MVAR_ACTION_NAME		MVAR_ACTION "NAME}"
#define MVAR_DATE			"{DATE}"
#define MVAR_EVENT			"{EVENT."			/* a prefix for all event macros */
#define MVAR_EVENT_ACK_HISTORY		MVAR_EVENT "ACK.HISTORY}"	/* deprecated */
#define MVAR_EVENT_ACK_STATUS		MVAR_EVENT "ACK.STATUS}"
#define MVAR_EVENT_AGE			MVAR_EVENT "AGE}"
#define MVAR_EVENT_DATE			MVAR_EVENT "DATE}"
#define MVAR_EVENT_ID			MVAR_EVENT "ID}"
#define MVAR_EVENT_NAME			MVAR_EVENT "NAME}"
#define MVAR_EVENT_STATUS		MVAR_EVENT "STATUS}"
#define MVAR_EVENT_TAGS			MVAR_EVENT "TAGS}"
#define MVAR_EVENT_TIME			MVAR_EVENT "TIME}"
#define MVAR_EVENT_VALUE		MVAR_EVENT "VALUE}"
#define MVAR_EVENT_SEVERITY		MVAR_EVENT "SEVERITY}"
#define MVAR_EVENT_NSEVERITY		MVAR_EVENT "NSEVERITY}"
#define MVAR_EVENT_RECOVERY		MVAR_EVENT "RECOVERY."		/* a prefix for all recovery event macros */
#define MVAR_EVENT_RECOVERY_DATE	MVAR_EVENT_RECOVERY "DATE}"
#define MVAR_EVENT_RECOVERY_ID		MVAR_EVENT_RECOVERY "ID}"
#define MVAR_EVENT_RECOVERY_STATUS	MVAR_EVENT_RECOVERY "STATUS}"	/* deprecated */
#define MVAR_EVENT_RECOVERY_TAGS	MVAR_EVENT_RECOVERY "TAGS}"
#define MVAR_EVENT_RECOVERY_TIME	MVAR_EVENT_RECOVERY "TIME}"
#define MVAR_EVENT_RECOVERY_VALUE	MVAR_EVENT_RECOVERY "VALUE}"	/* deprecated */
#define MVAR_EVENT_RECOVERY_NAME	MVAR_EVENT_RECOVERY "NAME}"
#define MVAR_EVENT_UPDATE		MVAR_EVENT "UPDATE."
#define MVAR_EVENT_UPDATE_ACTION	MVAR_EVENT_UPDATE "ACTION}"
#define MVAR_EVENT_UPDATE_DATE		MVAR_EVENT_UPDATE "DATE}"
#define MVAR_EVENT_UPDATE_HISTORY	MVAR_EVENT_UPDATE "HISTORY}"
#define MVAR_EVENT_UPDATE_MESSAGE	MVAR_EVENT_UPDATE "MESSAGE}"
#define MVAR_EVENT_UPDATE_TIME		MVAR_EVENT_UPDATE "TIME}"

#define MVAR_ESC_HISTORY		"{ESC.HISTORY}"
#define MVAR_PROXY_NAME			"{PROXY.NAME}"
#define MVAR_PROXY_DESCRIPTION		"{PROXY.DESCRIPTION}"
#define MVAR_HOST_DNS			"{HOST.DNS}"
#define MVAR_HOST_CONN			"{HOST.CONN}"
#define MVAR_HOST_HOST			"{HOST.HOST}"
#define MVAR_HOST_ID			"{HOST.ID}"
#define MVAR_HOST_IP			"{HOST.IP}"
#define MVAR_IPADDRESS			"{IPADDRESS}"			/* deprecated */
#define MVAR_HOST_METADATA		"{HOST.METADATA}"
#define MVAR_HOST_NAME			"{HOST.NAME}"
#define MVAR_HOSTNAME			"{HOSTNAME}"			/* deprecated */
#define MVAR_HOST_DESCRIPTION		"{HOST.DESCRIPTION}"
#define MVAR_HOST_PORT			"{HOST.PORT}"
#define MVAR_TIME			"{TIME}"
#define MVAR_ITEM_LASTVALUE		"{ITEM.LASTVALUE}"
#define MVAR_ITEM_VALUE			"{ITEM.VALUE}"
#define MVAR_ITEM_ID			"{ITEM.ID}"
#define MVAR_ITEM_NAME			"{ITEM.NAME}"
#define MVAR_ITEM_NAME_ORIG		"{ITEM.NAME.ORIG}"
#define MVAR_ITEM_KEY			"{ITEM.KEY}"
#define MVAR_ITEM_KEY_ORIG		"{ITEM.KEY.ORIG}"
#define MVAR_ITEM_STATE			"{ITEM.STATE}"
#define MVAR_TRIGGER_KEY		"{TRIGGER.KEY}"			/* deprecated */
#define MVAR_ITEM_DESCRIPTION		"{ITEM.DESCRIPTION}"
#define MVAR_ITEM_LOG_DATE		"{ITEM.LOG.DATE}"
#define MVAR_ITEM_LOG_TIME		"{ITEM.LOG.TIME}"
#define MVAR_ITEM_LOG_AGE		"{ITEM.LOG.AGE}"
#define MVAR_ITEM_LOG_SOURCE		"{ITEM.LOG.SOURCE}"
#define MVAR_ITEM_LOG_SEVERITY		"{ITEM.LOG.SEVERITY}"
#define MVAR_ITEM_LOG_NSEVERITY		"{ITEM.LOG.NSEVERITY}"
#define MVAR_ITEM_LOG_EVENTID		"{ITEM.LOG.EVENTID}"

#define MVAR_TRIGGER_DESCRIPTION		"{TRIGGER.DESCRIPTION}"
#define MVAR_TRIGGER_COMMENT			"{TRIGGER.COMMENT}"		/* deprecated */
#define MVAR_TRIGGER_ID				"{TRIGGER.ID}"
#define MVAR_TRIGGER_NAME			"{TRIGGER.NAME}"
#define MVAR_TRIGGER_NAME_ORIG			"{TRIGGER.NAME.ORIG}"
#define MVAR_TRIGGER_EXPRESSION			"{TRIGGER.EXPRESSION}"
#define MVAR_TRIGGER_EXPRESSION_RECOVERY	"{TRIGGER.EXPRESSION.RECOVERY}"
#define MVAR_TRIGGER_SEVERITY			"{TRIGGER.SEVERITY}"
#define MVAR_TRIGGER_NSEVERITY			"{TRIGGER.NSEVERITY}"
#define MVAR_TRIGGER_STATUS			"{TRIGGER.STATUS}"
#define MVAR_TRIGGER_STATE			"{TRIGGER.STATE}"
#define MVAR_TRIGGER_TEMPLATE_NAME		"{TRIGGER.TEMPLATE.NAME}"
#define MVAR_TRIGGER_HOSTGROUP_NAME		"{TRIGGER.HOSTGROUP.NAME}"
#define MVAR_STATUS				"{STATUS}"			/* deprecated */
#define MVAR_TRIGGER_VALUE			"{TRIGGER.VALUE}"
#define MVAR_TRIGGER_URL			"{TRIGGER.URL}"

#define MVAR_TRIGGER_EVENTS_ACK			"{TRIGGER.EVENTS.ACK}"
#define MVAR_TRIGGER_EVENTS_UNACK		"{TRIGGER.EVENTS.UNACK}"
#define MVAR_TRIGGER_EVENTS_PROBLEM_ACK		"{TRIGGER.EVENTS.PROBLEM.ACK}"
#define MVAR_TRIGGER_EVENTS_PROBLEM_UNACK	"{TRIGGER.EVENTS.PROBLEM.UNACK}"

#define MVAR_LLDRULE_DESCRIPTION		"{LLDRULE.DESCRIPTION}"
#define MVAR_LLDRULE_ID				"{LLDRULE.ID}"
#define MVAR_LLDRULE_KEY			"{LLDRULE.KEY}"
#define MVAR_LLDRULE_KEY_ORIG			"{LLDRULE.KEY.ORIG}"
#define MVAR_LLDRULE_NAME			"{LLDRULE.NAME}"
#define MVAR_LLDRULE_NAME_ORIG			"{LLDRULE.NAME.ORIG}"
#define MVAR_LLDRULE_STATE			"{LLDRULE.STATE}"

#define MVAR_INVENTORY				"{INVENTORY."			/* a prefix for all inventory macros */
#define MVAR_INVENTORY_TYPE			MVAR_INVENTORY "TYPE}"
#define MVAR_INVENTORY_TYPE_FULL		MVAR_INVENTORY "TYPE.FULL}"
#define MVAR_INVENTORY_NAME			MVAR_INVENTORY "NAME}"
#define MV#define MVAR_INVENTORY_MACADDRESS_B		MVAR_INVENTORY "MACADDRESS.B}"
#define MVAR_INVENTORY_HARDWARE			MVAR_INVENTORY "HARDWARE}"
#define MVAR_INVENTORY_HARDWARE_FULL		MVAR_INVENTORY "HARDWARE.FULL}"
#define MVAR_INVENTORY_SOFTWARE			MVAR_INVENTORY "SOFTWARE}"
#define MVAR_INVENTORY_SOFTWARE_FULL		MVAR_INVENTORY "SOFTWARE.FULL}"
#define MVAR_INVENTORY_SOFTWARE_APP_A		MVAR_INVENTORY "SOFTWARE.APP.A}"
#define MVAR_INVENTORY_SOFTWARE_APP_B		MVAR_INVENTORY "SOFTWARE.APP.B}"
#define MVAR_INVENTORY_SOFTWARE_APP_C		MVAR_INVENTORY "SOFTWARE.APP.C}"
#define MVAR_INVENTORY_SOFTWARE_APP_D		MVAR_INVENTORY "SOFTWARE.APP.D}"
#define MVAR_INVENTORY_SOFTWARE_APP_E		MVAR_INVENTORY "SOFTWARE.APP.E}"
#define MVAR_INVENTORY_CONTACT			MVAR_INVENTORY "CONTACT}"
#define MVAR_INVENTORY_LOCATION			MVAR_INVENTORY "LOCATION}"
#define MVAR_INVENTORY_LOCATION_LAT		MVAR_INV#define MVAR_INVENTORY_SOFTWARE_APP_A		MVAR_INVENTORY "SOFTWARE.APP.A}"
#define MVAR_INVENTORY_SOFTWARE_APP_B		MVAR_INVENTORY "SOFTWARE.APP.B}"
#define MVAR_INVENTORY_SOFTWARE_APP_C		MVAR_INVENTORY "SOFTWARE.APP.C}"
#define MVAR_INVENTORY_SOFTWARE_APP_D		MVAR_INVENTORY "SOFTWARE.APP.D}"
#define MVAR_INVENTORY_SOFTWARE_APP_E		MVAR_INVENTORY "SOFTWARE.APP.E}"
#define MVAR_INVENTORY_CONTACT			MVAR_INVENTORY "CONTACT}"
#define MVAR_INVENTORY_LOCATION			MVAR_INVENTORY "LOCATION}"
#define MVAR_INVENTORY_LOCATION_LAT		MVAR_INVENTORY "LOCATION.LAT}"
#define MVAR_INVENTORY_LOCATION_LON		MVAR_INVENTORY "LOCATION.LON}"
#define MVAR_INVENTORY_NOTES			MVAR_INVENTORY "NOTES}"
#define MVAR_INVENTORY_CHASSIS			MVAR_INVENTORY "CHASSIS}"
#define MVAR_INVENTORY_MODEL			MVAR_INVENTORY "MODEL}"
#define MVAR_INVENTORY_HW_ARCH			MVAR_INVENTORY "HW.ARCH}"
#define MVAR_INVENTORY_VENDOR			MVAR_INVENTORY "VENDOR}"
#define MVAR_INVENTORY_CONTRACT_NUMBER		MVAR_INVENTORY "CONTRACT.NUMBER}"
#define MVAR_INVENTORY_INSTALLER_NAME		MVAR_INVENTORY "INSTALLER.NAME}"
#define MVAR_INVENTORY_DEPLOYMENT_STATUS	MVAR_INVENTORY "DEPLOYMENT.STATUS}"
#define MVAR_INVENTORY_URL_A			MVAR_INVENTORY "URL.A}"
#define MVAR_INVENTORY_URL_B			MVAR_INVENTORY "URL.B}"
#define MVAR_INVENTORY_URL_C			MVAR_INVENTORY "URL.C}"
#define MVAR_INVENTORY_HOST_NETWORKS		MVAR_INVENTORY "HOST.NETWORKS}"
#define MVAR_INVENTORY_HOST_NETMASK		MVAR_INVENTORY "HOST.NETMASK}"
#define MVAR_INVENTORY_HOST_ROUTER		MVAR_INVENTORY "HOST.ROUTER}"
#define MVAR_INVENTORY_OOB_IP			MVAR_INVENTORY "OOB.IP}"
#define MVAR_INVENTORY_OOB_NETMASK		MVAR_INVENTORY "OOB.NETMASK}"
#define MVAR_INVENTORY_OOB_ROUTER		MVAR_INVENTORY "OOB.ROUTER}"
#define MVAR_INVENTORY_HW_DATE_PURCHASE		MVAR_INVENTORY "HW.DATE.PURCHASE}"
#define MVAR_INVENTORY_HW_DATE_INSTALL		MVAR_INVENTORY "HW.DATE.INSTALL}"
#define MVAR_INVENTORY_HW_DATE_EXPIRY		MVAR_INVENTORY "HW.DATE.EXPIRY}"
#define MVAR_INVENTORY_HW_DATE_DECOMM		MVAR_INVENTORY "HW.DATE.DECOMM}"
#define MVAR_INVENTORY_SITE_ADDRESS_A		MVAR_INVENTORY "SITE.ADDRESS.A}"
#define MVAR_INVENTORY_SITE_ADDRESS_B		MVAR_INVENTORY "SITE.ADDRESS.B}"
#define MVAR_INVENTORY_SITE_ADDRESS_C		MVAR_INVENTORY "SITE.ADDRESS.C}"
#define MVAR_INVENTORY_SITE_CITY		MVAR_INVENTORY "SITE.CITY}"
#define MVAR_INVENTORY_SITE_STATE		MVAR_INVENTORY "SITE.STATE}"
#define MVAR_INVENTORY_SITE_COUNTRY		MVAR_INVENTORY "SITE.COUNTRY}"
#define MVAR_INVENTORY_SITE_ZIP			MVAR_INVENTORY "SITE.ZIP}"
#define MVAR_INVENTORY_SITE_RACK		MVAR_INVENTORY "SITE.RACK}"
#define MVAR_INVENTORY_SITE_NOTES		MVAR_INVENTORY "SITE.NOTES}"
#define MVAR_INVENTORY_POC_PRIMARY_NAME		MVAR_INVENTORY "POC.PRIMARY.NAME}"
#define MVAR_INVENTORY_POC_PRIMARY_EMAIL	MVAR_INVENTORY "POC.PRIMARY.EMAIL}"
#define MVAR_INVENTORY_POC_PRIMARY_PHONE_A	MVAR_INVENTORY "POC.PRIMARY.PHONE.A}"
#define MVAR_INVENTORY_POC_PRIMARY_PHONE_B	MVAR_INVENTORY "POC.PRIMARY.PHONE.B}"
#define MVAR_INVENTORY_POC_PRIMARY_CELL		MVAR_INVENTORY "POC.PRIMARY.CELL}"
#define MVAR_INVENTORY_POC_PRIMARY_SCREEN	MVAR_INVENTORY "POC.PRIMARY.SCREEN}"
#define MVAR_INVENTORY_POC_PRIMARY_NOTES	MVAR_INVENTORY "POC.PRIMARY.NOTES}"
#define MVAR_INVENTORY_POC_SECONDARY_NAME	MVAR_INVENTORY "POC.SECONDARY.NAME}"
#define MVAR_INVENTORY_POC_SECONDARY_EMAIL	MVAR_INVENTORY "POC.SECONDARY.EMAIL}"
#define MVAR_INVENTORY_POC_SECONDARY_PHONE_A	MVAR_INVENTORY "POC.SECONDARY.PHONE.A}"
#define MVAR_INVENTORY_POC_SECONDARY_PHONE_B	MVAR_INVENTORY "POC.SECONDARY.PHONE.B}"
#define MVAR_INVENTORY_POC_SECONDARY_CELL	MVAR_INVENTORY "POC.SECONDARY.CELL}"
#define MVAR_INVENTORY_POC_SECONDARY_SCREEN	MVAR_INVENTORY "POC.SECONDARY.SCREEN}"
#define MVAR_INVENTORY_POC_SECONDARY_NOTES	MVAR_INVENTORY "POC.SECONDARY.NOTES}"

/* PROFILE.* is deprecated, use INVENTORY.* instead */
#define MVAR_PROFILE			"{PROFILE."			/* prefix for profile macros */
#define MVAR_PROFILE_DEVICETYPE		MVAR_PROFILE "DEVICETYPE}"
#define MVAR_PROFILE_NAME		MVAR_PROFILE "NAME}"
#define MVAR_PROFILE_OS			MVAR_PROFILE "OS}"
#define MVAR_PROFILE_SERIALNO		MVAR_PROFILE "SERIALNO}"
#define MVAR_PROFILE_TAG		MVAR_PROFILE "TAG}"
#define MVAR_PROFILE_MACADDRESS		MVAR_PROFILE "MACADDRESS}"
#define MVAR_PROFILE_HARDWARE		MVAR_PROFILE "HARDWARE}"
#define MVAR_PROFILE_SOFTWARE		MVAR_PROFILE "SOFTWARE}"
#define MVAR_PROFILE_CONTACT		MVAR_PROFILE "CONTACT}"
#define MVAR_PROFILE_LOCATION		MVAR_PROFILE "LOCATION}"
#define MVAR_PROFILE_NOTES		MVAR_PROFILE "NOTES}"

#define MVAR_DISCOVERY_RULE_NAME	"{DISCOVERY.RULE.NAME}"
#define MVAR_DISCOVERY_SERVICE_NAME	"{DISCOVERY.SERVICE.NAME}"
#define MVAR_DISCOVERY_SERVICE_PORT	"{DISCOVERY.SERVICE.PORT}"
#define MVAR_DISCOVERY_SERVICE_STATUS	"{DISCOVERY.SERVICE.STATUS}"
#define MVAR_DISCOVERY_SERVICE_UPTIME	"{DISCOVERY.SERVICE.UPTIME}"
#define MVAR_DISCOVERY_DEVICE_IPADDRESS	"{DISCOVERY.DEVICE.IPADDRESS}"
#define MVAR_DISCOVERY_DEVICE_DNS	"{DISCOVERY.DEVICE.DNS}"
#define MVAR_DISCOVERY_DEVICE_STATUS	"{DISCOVERY.DEVICE.STATUS}"
#define MVAR_DISCOVERY_DEVICE_UPTIME	"{DISCOVERY.DEVICE.UPTIME}"

#define MVAR_ALERT_SENDTO		"{ALERT.SENDTO}"
#define MVAR_ALERT_SUBJECT		"{ALERT.SUBJECT}"
#define MVAR_ALERT_MESSAGE		"{ALERT.MESSAGE}"

#define MVAR_ACK_MESSAGE                "{ACK.MESSAGE}"			/* deprecated */
#define MVAR_ACK_TIME	                "{ACK.TIME}"			/* deprecated */
#define MVAR_ACK_DATE	                "{ACK.DATE}"			/* deprecated */
#define MVAR_USER_FULLNAME          	"{USER.FULLNAME}"

#define STR_UNKNOWN_VARIABLE		"*UNKNOWN*"

/* macros that can be indexed */
static const char	*ex_macros[] =
{
	MVAR_INVENTORY_TYPE, MVAR_INVENTORY_TYPE_FULL,
	MVAR_INVENTORY_NAME, MVAR_INVENTORY_ALIAS, MVAR_INVENTORY_OS, MVAR_INVENTORY_OS_FULL, MVAR_INVENTORY_OS_SHORT,
	MVAR_INVENTORY_SERIALNO_A, MVAR_INVENTORY_SERIALNO_B, MVAR_INVENTORY_TAG,
	MVAR_INVENTORY_ASSET_TAG, MVAR_INVENTORY_MACADDRESS_A, MVAR_INVENTORY_MACADDRESS_B,
	MVAR_INVENTORY_HARDWARE, MVAR_INVENTORY_HARDWARE_FULL, MVAR_INVENTORY_SOFTWARE, MVAR_INVENTORY_SOFTWARE_FULL,
	MVAR_INVENTORY_SOFTWARE_APP_A, MVAR_INVENTORY_SOFTWARE_APP_B, MVAR_INVENTORY_SOFTWARE_APP_C,
	MVAR_INVENTORY_SOFTWARE_APP_D, MVAR_INVENTORY_SOFTWARE_APP_E, MVAR_INVENTORY_CONTACT, MVAR_INVENTORY_LOCATION,
	MVAR_INVENTORY_LOCATION_LAT, MVAR_INVENTORY_LOCATION_LON, MVAR_INVENTORY_NOTES, MVAR_INVENTORY_CHASSIS,
	MVAR_INVENTORY_MODEL, MVAR_INVENTORY_HW_ARCH, MVAR_INVENTORY_VENDOR, MVAR_INVENTORY_CONTRACT_NUMBER,
	MVAR_INVENTORY_INSTALLER_NAME, MVAR_INVENTORY_DEPLOYMENT_STATUS, MVAR_INVENTORY_URL_A, MVAR_INVENTORY_URL_B,
	MVAR_INVENTORY_URL_C, MVAR_INVENTORY_HOST_NETWORKS, MVAR_INVENTORY_HOST_NETMASK, MVAR_INVENTORY_HOST_ROUTER,
	MVAR_INVENTORY_OOB_IP, MVAR_INVENTORY_OOB_NETMASK, MVAR_INVENTORY_OOB_ROUTER, MVAR_INVENTORY_HW_DATE_PURCHASE,
	MVAR_INVENTORY_HW_DATE_INSTALL, MVAR_INVENTORY_HW_DATE_EXPIRY, MVAR_INVENTORY_HW_DATE_DECOMM,
	MVAR_INVENTORY_SITE_ADDRESS_A, MVAR_INVENTORY_SITE_ADDRESS_B, MVAR_INVENTORY_SITE_ADDRESS_C,
	MVAR_INVENTORY_SITE_CITY, MVAR_INVENTORY_SITE_STATE, MVAR_INVENTORY_SITE_COUNTRY, MVAR_INVENTORY_SITE_ZIP,
	MVAR_INVENTORY_SITE_RACK, MVAR_INVENTORY_SITE_NOTES, MVAR_INVENTORY_POC_PRIMARY_NAME,
	MVAR_INVENTORY_POC_PRIMARY_EMAIL, MVAR_INVENTORY_POC_PRIMARY_PHONE_A, MVAR_INVENTOR	MVAR_PROFILE_TAG, MVAR_PROFILE_MACADDRESS, MVAR_PROFILE_HARDWARE, MVAR_PROFILE_SOFTWARE,
	MVAR_PROFILE_CONTACT, MVAR_PROFILE_LOCATION, MVAR_PROFILE_NOTES,
	MVAR_HOST_HOST, MVAR_HOSTNAME, MVAR_HOST_NAME, MVAR_HOST_DESCRIPTION, MVAR_PROXY_NAME, MVAR_PROXY_DESCRIPTION,
	MVAR_HOST_CONN, MVAR_HOST_DNS, MVAR_HOST_IP, MVAR_HOST_PORT, MVAR_IPADDRESS, MVAR_HOST_ID,
	MVAR_ITEM_ID, MVAR_ITEM_NAME, MVAR_ITEM_NAME_ORIG, MVAR_ITEM_DESCRIPTION,
	MVAR_ITEM_KEY, MVAR_ITEM_KEY_ORIG, MVAR_TRIGGER_KEY,
	MVAR_ITEM_LASTVALUE,
	MVAR_ITEM_STATE,
	MVAR_ITEM_VALUE,
	MVAR_ITEM_LOG_DATE, MVAR_ITEM_LOG_TIME, MVAR_ITEM_LOG_AGE, MVAR_ITEM_LOG_SOURCE,
	MVAR_ITEM_LOG_SEVERITY, MVAR_ITEM_LOG_NSEVERITY, MVAR_ITEM_LOG_EVENTID,
	NULL
};

/* macros that are supported as simple macro host and item key */
static const char	*simple_host_macros[] = {MVAR_HOST_HOST, MVAR_HOSTNAME, NULL};
static const char	*si	MVAR_ITEM_ID, MVAR_ITEM_NAME, MVAR_ITEM_NAME_ORIG, MVAR_ITEM_DESCRIPTION,
	MVAR_ITEM_KEY, MVAR_ITEM_KEY_ORIG, MVAR_TRIGGER_KEY,
	MVAR_ITEM_LASTVALUE,
	MVAR_ITEM_STATE,
	MVAR_ITEM_VALUE,
	MVAR_ITEM_LOG_DATE, MVAR_ITEM_LOG_TIME, MVAR_ITEM_LOG_AGE, MVAR_ITEM_LOG_SOURCE,
	MVAR_ITEM_LOG_SEVERITY, MVAR_ITEM_LOG_NSEVERITY, MVAR_ITEM_LOG_EVENTID,
	NULL
};

/* macros that are supported as simple macro host and item key */
static const char	*simple_host_macros[] = {MVAR_HOST_HOST, MVAR_HOSTNAME, NULL};
static const char	*simple_key_macros[] = {MVAR_ITEM_KEY, MVAR_TRIGGER_KEY, NULL};

/* macros that can be modified using macro functions */
static const char	*mod_macros[] = {MVAR_ITEM_VALUE, MVAR_ITEM_LASTVALUE, NULL};

typedef struct
{
	const char	*macro;
	int		idx;
} inventory_field_t;

static inventory_field_t	inventory_fields[] =
{
	{MVAR_INVENTORY_TYPE, 0},
	{MVAR_PROFILE_DEVICETYPE, 0},	/* deprecated */
	{MVAR_INVENTORY_TYPE_FULL, 1},
	{MVAR_INVENTORY_NAME, 2},
	{MVAR_PROFILE_NAME, 2},	/* deprecated */
	{MVAR_INVENTORY_ALIAS, 3},
	{MVAR_INVENTORY_OS, 4},
	{MVAR_PROFILE_OS, 4},	/* deprecated */
	{MVAR_INVENTORY_OS_FULL, 5},
	{MVAR_INVENTORY_OS_SHORT, 6},
	{MVAR_INVENTORY_SERIALNO_A, 7},
	{MVAR_PROFILE_SERIALNO, 7},	/* deprecated */
	{MVAR_INVENTORY_SERIALNO_B, 8},
	{MVAR_INVENTORY_TAG, 9},
	{MVAR_PROFILE_TAG, 9},	/* deprecated */
	{MVAR_INVENTORY_ASSET_TAG, 10},
	{MVAR_INVENTORY_MACADDRESS_A, 11},
	{MVAR_PROFILE_MACADDRESS, 11},	/* deprecated */
	{MVAR_INVENTORY_MACADDRESS_B, 12},
	{MVAR_INVENTORY_HARDWARE, 13},
	{MVAR_PROFILE_HARDWARE, 13},	/* deprecated */
	{MVAR_INVENTORY_HARDWARE_FULL, 14},
	{MVAR_INVENTORY_SOFTWARE, 15},
	{MVAR_PROFILE_SOFTWARE, 15},	/* deprecated */
	{MVAR_INVENTORY_SOFTWARE_FULL, 16},
	{MVAR_INVENTORY_SOFTWARE_APP_A, 17},
	{MVAR_INVENTORY_SOFTWARE_APP_B, 18},
	{MVAR_INVENTORY_SOFTWARE_APP_C, 19},
	{MVAR_INVENTORY_SOFTWARE_APP_D, 20},
	{MVAR_INVENTORY_SOFTWARE_APP_E, 21},
	{MVAR_INVENTORY_CONTACT, 22},
	{MVAR_PROFILE_CONTACT, 22},	/* deprecated */
	{MVAR_INVENTORY_LOCATION, 23},
	{MVAR_PROFILE_LOCATION, 23},	/* deprecated */
	{MVAR_INVENTORY_LOCATION_LAT, 24},
	{MVAR_INVENTORY_LOCATION_LON, 25},
	{MVAR_INVENTORY_NOTES, 26},
	{MVAR_PROFILE_NOTES, 26},	/* deprecated */
	{MVAR_INVENTORY_CHASSIS, 27},
	{MVAR_INVENTORY_MODEL, 28},
	{MVAR_INVENTORY_HW_ARCH, 29},
	{MVAR_INVENTORY_VENDOR, 30},
	{MVAR_INVENTORY_CONTRACT_NUMBER, 31},
	{MVAR_INVENTORY_INSTALLER_NAME, 32},
	{MVAR_INVENTORY_DEPLOYMENT_STATUS, 33},
	{MVAR_INVENTORY_URL_A, 34},
	{MVAR_INVENTORY_URL_B, 35},
	{MVAR_INVENTORY_URL_C, 36},
	{MVAR_INVENTORY_HOST_NETWORKS, 37},
	{MVAR_INVENTORY_HOST_NETMASK, 38},
	{MVAR_INVENTORY_HOST_ROUTER, 39},
	{MVAR_INVENTORY_OOB_IP, 40},
	{MVAR_INVENTORY_OOB_NETMASK, 41},
	{MVAR_INVENTORY_OOB_ROUTER, 42},
	{MVAR_INVENTORY_HW_DATE_PURCHASE, 43},
	{MVAR_INVENTORY_HW_DATE_INSTALL, 44},
	{MVAR_INVENTORY_HW_DATE_EXPIRY, 45},
	{MVAR_INVENTORY_HW_DATE_DECOMM, 46},
	{MVAR_INVENTORY_SITE_ADDRESS_A, 47},
	{MVAR_INVENTORY_SITE_ADDRESS_B, 48},
	{MVAR_INVENTORY_SITE_ADDRESS_C, 49},
	{MVAR_INVENTORY_SITE_CITY, 50},
	{MVAR_INVENTORY_SITE_STATE, 51},
	{MVAR_INVENTORY_SITE_COUNTRY, 52},
	{MVAR_INVENTORY_SITE_ZIP, 53},
	{MVAR_INVENTORY_SITE_RACK, 54},
	{MVAR_INVENTORY_SITE_NOTES, 55},
	{MVAR_INVENTORY_POC_PRIMARY_NAME, 56},
	{MVAR_INVENTORY_POC_PRIMARY_EMAIL, 57},
	{MVAR_INVENTORY_POC_PRIMARY_PHONE_A, 58},
	{MVAR_INVENTORY_POC_PRIMARY_PHONE_B, 59},
	{MVAR_INVENTORY_POC_PRIMARY_CELL, 60},
	{MVAR_INVENTORY_POC_PRIMARY_SCREEN, 61},
	{MVAR_INVENTORY_POC_PRIMARY_NOTES, 62},
	{MVAR_INVENTORY_POC_SECONDARY_NAME, 63},
	{MVAR_INVENTORY_POC_SECONDARY_EMAIL, 64},
	{MVAR_INVENTORY_POC_SECONDARY_PHONE_A, 65},
	{MVAR_INVENTORY_POC_SECONDARY_PHONE_B, 66},
	{MVAR_INVENTORY_POC_SECONDARY_CELL, 67},
	{MVAR_INVENTORY_POC_SECONDARY_SCREEN, 68},
	{MVAR_INVENTORY_POC_SECONDARY_NOTES, 69},
	{NULL}
};

/******************************************************************************
 *                                                                            *
 * Function: get_action_value                                                 *
 *                                                                            *
 * Purpose: request action value by macro                                     *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的宏（macro）和动作ID（actionid），查询动作名称（action name），并将结果存储到replace_to指向的内存空间中。如果查询失败，返回失败（FAIL）。
 ******************************************************************************/
// 定义一个静态函数get_action_value，接收三个参数：
// 1. 一个字符串指针macro，表示要处理的宏；
// 2. 一个zbx_uint64_t类型的actionid，表示动作ID；
// 3. 一个字符指针指针replace_to，用于存储替换后的结果。
static int get_action_value(const char *macro, zbx_uint64_t actionid, char **replace_to)
{
    // 定义一个整型变量ret，初始值为成功（SUCCEED）。
    int ret = SUCCEED;

    // 判断macro是否等于MVAR_ACTION_ID宏，如果是，则执行以下操作：
    if (0 == strcmp(macro, MVAR_ACTION_ID))
    {
        // 将actionid转换为字符串，并存储到replace_to指向的内存空间中。
        *replace_to = zbx_dsprintf(*replace_to, ZBX_FS_UI64, actionid);
    }
    // 判断macro是否等于MVAR_ACTION_NAME宏，如果是，则执行以下操作：
    else if (0 == strcmp(macro, MVAR_ACTION_NAME))
    {
        // 执行数据库查询，从actions表中获取动作名称。
        DB_RESULT	result;
        DB_ROW		row;

        // 执行SQL查询：select name from actions where actionid=actionid
        result = DBselect("select name from actions where actionid=" ZBX_FS_UI64, actionid);

        // 如果查询结果不为空，则将结果中的名称复制到replace_to指向的内存空间中。
        if (NULL != (row = DBfetch(result)))
            *replace_to = zbx_strdup(*replace_to, row[0]);
        // 如果没有查询到结果，则返回失败。
        else
            ret = FAIL;

        // 释放查询结果内存。
        DBfree_result(result);
    }

    // 返回操作结果。
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: get_host_inventory                                               *
 *                                                                            *
 * Purpose: request host inventory value b *这块代码的主要目的是根据传入的 macro、expression 和 N_functionid 参数，查询对应的库存字段值。代码首先遍历库存字段数组，找到匹配的 macro。如果找到匹配的 macro，则调用 get_N_itemid 函数获取 itemid，接着调用 DCget_host_inventory_value_by_itemid 函数根据 itemid 获取库存值，并将结果存储在 replace_to 指向的内存空间。如果没有找到匹配的 macro，则返回 FAIL。
 ******************************************************************* *这块代码的主要目的是根据传入的 macro、expression 和 N_functionid 参数，查询对应的库存字段值。代码首先遍历库存字段数组，找到匹配的 macro。如果找到匹配的 macro，则调用 get_N_itemid 函数获取 itemid，接着调用 DCget_host_inventory_value_by_itemid 函数根据 itemid 获取库存值，并将结果存储在 replace_to 指向的内存空间。如果没有找到匹配的 macro，则返回 FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于获取主机库存数据
static int get_host_inventory(const char *macro, const char *expression, char **replace_to, int N_functionid)
{
	// 定义一个循环变量 i
	int i;

	// 遍历库存字段数组
	for (i = 0; NULL != inventory_fields[i].macro; i++)
	{
		// 如果传入的 macro 与库存字段数组中的 macro 相等
		if (0 == strcmp(macro, inventory_fields[i].macro))
		{
			// 获取 itemid，用于后续查询库存值
			zbx_uint64_t itemid;

			// 如果获取 itemid 失败，返回 FAIL
			if (SUCCEED != get_N_itemid(expression, N_functionid, &itemid))
				return FAIL;

			// 调用另一个函数，根据 itemid 获取库存值，并将结果存储在 replace_to 指向的内存空间
			return DCget_host_inventory_value_by_itemid(itemid, replace_to, inventory_fields[i].idx);
		}
	}

	// 如果没有找到匹配的 macro，返回 FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: get_host_inventory_by_itemid                                     *
 *                                                                            *
 * Purpose: request host inventory value by macro and itemid                  *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *               otherwise FAIL                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的macro和itemid，查找并获取对应的库存信息。如果找到匹配的macro，则调用DCget_host_inventory_value_by_itemid函数获取库存值，并将结果存储在replace_to指向的字符串数组中。如果没有找到匹配的macro，则返回FAIL表示失败。
 ******************************************************************************/
// 定义一个静态函数，用于根据itemid获取主机库存信息
static int	get_host_inventory_by_itemid(const char *macro, zbx_uint64_t itemid, char **replace_to)
{
	// 定义一个循环变量i，用于遍历库存字段数组
	int	i;

	// 使用for循环遍历库存字段数组，直到找到匹配的macro
	for (i = 0; NULL != inventory_fields[i].macro; i++)
	{
		// 如果当前macro与传入的macro相等，则执行以下操作
		if (0 == strcmp(macro, inventory_fields[i].macro))
		{
			// 调用DCget_host_inventory_value_by_itemid函数，根据itemid获取库存值
			return DCget_host_inventory_value_by_itemid(itemid, replace_to, inventory_fields[i].idx);
		}
	}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 compare_tags 的函数，该函数用于比较两个zbx_tag_t结构体（可能是用于存储数据的数据结构）的标签名称和值。函数采用自然排序算法进行比较，当标签名称相同的情况下，才会比较标签值。最后返回比较结果。
 ******************************************************************************/
// 定义一个名为 compare_tags 的静态函数，该函数接收两个参数，都是指向zbx_tag_t结构体的指针
static int	compare_tags(const void *d1, const void *d2)
{
	// 定义一个整型变量 ret，用于存储比较结果
	int	ret;

	// 解引用指针 d1，获取指向的结构体 zbx_tag_t 指针
/******************************************************************************
 * *
 *整个代码块的主要目的是处理事件的标签，将事件中的标签按照指定的规则排序，并将排序后的标签字符串存储在传入的字符串指针中。具体操作如下：
 *
 *1. 判断事件中的标签数量，如果为0，直接返回空字符串。
 *2. 释放原有字符串占用的内存。
 *3. 创建一个临时vector，用于存储事件中的标签，并预先分配足够的空间。
 *4. 遍历事件的标签，并将标签添加到临时vector中。
 *5. 对临时vector中的标签进行排序，排序规则如下：
 *   1. 按照标签名称的字典顺序排序；
 *   2. 如果有多个标签名称相同，则按照标签值的顺序排序。
 *6. 遍历排序后的标签，构建新的字符串。
 *7. 释放临时vector，清理内存。
 ******************************************************************************/
/* 定义一个静态函数，用于处理事件（event）的标签（tags）
 * 输入参数：
 *   event：事件结构体指针，包含事件的标签信息
 *   replace_to：指向一个字符串指针，用于存储处理后的标签字符串
 * 返回值：
 *   无
 */
static void get_event_tags(const DB_EVENT *event, char **replace_to)
{
	/* 定义一些变量，用于存储操作过程中的信息 */
	size_t			replace_to_offset = 0, replace_to_alloc = 0;
	int			i;
	zbx_vector_ptr_t	tags;

	/* 判断事件的标签数量是否为0，如果是，直接返回空字符串 */
	if (0 == event->tags.values_num)
	{
		*replace_to = zbx_strdup(*replace_to, "");
		return;
	}

	/* 释放原有字符串占用的内存 */
	zbx_free(*replace_to);

	/* 创建一个临时vector，用于存储标签，并进行排序 */

	zbx_vector_ptr_create(&tags);
	zbx_vector_ptr_reserve(&tags, event->tags.values_num);

	/* 遍历事件的标签，并将标签添加到临时vector中 */
	for (i = 0; i < event->tags.values_num; i++)
		zbx_vector_ptr_append(&tags, event->tags.values[i]);

	/* 对临时vector中的标签进行排序，排序规则如下：
	 * 1. 按照标签名称的字典顺序排序
	 * 2. 如果有多个标签名称相同，则按照标签值的顺序排序
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为`get_recovery_event_value`的静态函数，该函数根据传入的宏和数据库事件（DB_EVENT结构体）获取相应的恢复事件值，并将结果复制到传入的字符指针（replace_to）指向的字符串中。函数支持以下几种宏：
 *
 *1. MVAR_EVENT_RECOVERY_DATE：获取恢复事件的日期字符串。
 *2. MVAR_EVENT_RECOVERY_ID：获取恢复事件ID的字符串。
 *3. MVAR_EVENT_RECOVERY_STATUS：获取恢复事件状态的字符串。
 *4. MVAR_EVENT_RECOVERY_TIME：获取恢复事件时间的字符串。
 *5. MVAR_EVENT_RECOVERY_VALUE：获取恢复事件值的字符串。
 *6. MVAR_EVENT_RECOVERY_TAGS：获取恢复事件的标签字符串。
 *7. MVAR_EVENT_RECOVERY_NAME：获取恢复事件名称的字符串。
 *
 *此外，该函数还根据事件来源（EVENT_SOURCE_TRIGGERS）和宏（MVAR_EVENT_RECOVERY_T    /* 判断宏是否为MVAR_EVENT_RECOVERY_DATE，如果是，则执行以下操作：
     * 将r_event->clock转换为字符串，并复制到replace_to指向的字符串中
     */
    if (0 == strcmp(macro, MVAR_EVENT_RECOVERY_DATE))
    {
        *replace_to = zbx_strdup(*replace_to, zbx_date2str(r_event->clock));
    }
    /* 判断宏是否为MVAR_EVENT_RECOVERY_ID，如果是，则执行以下操作：
     * 将r_event->eventid转换为字符串，并复制到replace_to指向的字符串中
     */
    els    /* 判断宏是否为MVAR_EVENT_RECOVERY_DATE，如果是，则执行以下操作：
     * 将r_event->clock转换为字符串，并复制到replace_to指向的字符串中
     */
    if (0 == strcmp(macro, MVAR_EVENT_RECOVERY_DATE))
    {
        *replace_to = zbx_strdup(*replace_to, zbx_date2str(r_event->clock));
    }
    /* 判断宏是否为MVAR_EVENT_RECOVERY_ID，如果是，则执行以下操作：
     * 将r_event->eventid转换为字符串，并复制到replace_to指向的字符串中
     */
    else if (0 == strcmp(macro, MVAR_EVENT_RECOVERY_ID))
    {
        *replace_to = zbx_dsprintf(*replace_to, ZBX_FS_UI64, r_event->eventid);
    }
    /* 判断宏是否为MVAR_EVENT_RECOVERY_STATUS，如果是，则执行以下操作：
     * 使用zbx_event_value_string()函数将r_event->source、r_event->object和r_event->value
     * 转换为字符串，并复制到replace_to指向的字符串中
     */
    else if (0 == strcmp(macro, MVAR_EVENT_RECOVERY_STATUS))
    {
        *replace_to = zbx_strdup(*replace_to,
                                 zbx_event_value_string(r_event->source, r_event->object, r_event->value));
    }
    /* 判断宏是否为MVAR_EVENT_RECOVERY_TIME，如果是，则执行以下操作：
     * 将r_event->clock转换为字符串，并复制到replace_to指向的字符串中
     */
    else if (0 == strcmp(macro, MVAR_EVENT_RECOVERY_TIME))
    {
        *replace_to = zbx_strdup(*replace_to, zbx_time2str(r_event->clock));
    }
    /* 判断宏是否为MVAR_EVENT_RECOVERY_VALUE，如果是，则执行以下操作：
     * 将r_event->value转换为字符串，并复制到replace_to指向的字符串中
     */
    else if (0 == strcmp(macro, MVAR_EVENT_RECOVERY_VALUE))
    {
        *replace_to = zbx_dsprintf(*replace_to, "%d", r_event->value);
    }
    /* 判断宏是否为MVAR_EVENT_RECOVERY_TAGS，如果是，则执行以下操作：
     * 调用get_event_tags()函数获取事件标签，并将结果复制到replace_to指向的字符串中
     */
    else if (EVENT_SOURCE_TRIGGERS == r_event->source && 0 == strcmp(macro, MVAR_EVENT_RECOVERY_TAGS))
    {
        get_event_tags(r_event, replace_to);
    }
    /* 判断宏是否为MVAR_EVENT_RECOVERY_NAME，如果是，则执行以下操作：
     * 将r_event->name转换为字符串，并复制到replace_to指向的字符串中
     */
    else if (0 == strcmp(macro, MVAR_EVENT_RECOVERY_NAME))
    {
        *replace_to = zbx_dsprintf(*replace_to, "%s", r_event->name);
    }
}

		return;
	}

	zbx_free(*replace_to);

	/* copy tags to temporary vector for sorting */

	zbx_vector_ptr_create(&tags);
	zbx_vector_ptr_reserve(&tags, event->tags.values_num);

	for (i = 0; i < event->tags.values_num; i++)
		zbx_vector_ptr_append(&tags, event->tags.values[i]);

	zbx_vector_ptr_sort(&tags, compare_tags);

	for (i = 0; i < tags.values_num; i++)
	{
		const zbx_tag_t	*tag = (const zbx_tag_t *)tags.values[i];

		if (0 != i)
			zbx_strcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, ", ");

		zbx_strcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, tag->tag);

		if ('\0' != *tag->value)
		{
			zbx_chrcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, ':');
			zbx_strcpy_alloc(replace_to, &replace_to_alloc, &replace_to_offset, tag->value);
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 get_event_value 的静态函数，接收 5 个参数：
 * 1. 一个字符串指针 macro，表示事件宏名称；
 * 2. 一个 DB_EVENT 结构体指针 event，表示事件信息；
 * 3. 一个字符指针指针 replace_to，用于存储替换后的字符串；
 * 4. 一个 zbx_uint64_t 类型指针 recipient_userid，表示接收者用户 ID。
 *
 * 该函数的主要目的是根据事件宏名称的不同，对事件信息进行相应的处理，并将处理后的结果存储在 replace_to 指向的字符串中。
 */
static void get_event_value(const char *macro, const DB_EVENT *event, char **replace_to,
                          const zbx_uint64_t *recipient_userid)
{
    /* 判断 macro 是否等于 MVAR_EVENT_AGE，如果是，则执行以下操作：
     * 将当前时间减去事件创建时间的结果转换为字符串，并使用 zbx_strdup 复制到 replace_to 指向的字符串中。
     */
    if (0 == strcmp(macro, MVAR_EVENT_AGE))
    {
        *replace_to = zbx_strdup(*replace_to, zbx_age2str(time(NULL) - event->clock));
    }
    /* 判断 macro 是否等于 MVAR_EVENT_DATE，如果是，则执行以下操作：
     * 将事件创建时间转换为字符串，并使用 zbx_strdup 复制到 replace_to 指向的字符串中。
     */
    else if (0 == strcmp(macro, MVAR_EVENT_DATE))
    {
        *replace_to = zbx_strdup(*replace_to, zbx_date2str(event->clock));
    }
    /* 判断 macro 是否等于 MVAR_EVENT_ID，如果是，则执行以下操作：
     * 使用 zbx_dsprintf 将事件 ID 转换为字符串，并使用 zbx_strdup 复制到 replace_to 指向的字符串中。
     */
    else if (0 == strcmp(macro, MVAR_EVENT_ID))
    {
        *replace_to = zbx_dsprintf(*replace_to, ZBX_FS_UI64, event->eventid);
    }
    /* 判断 macro 是否等于 MVAR_EVENT_TIME，如果是，则执行以下操作：
     * 将事件创建时间转换为字符串，并使用 zbx_strdup 复制到 replace_to 指向的字符串中。
     */
    else if (0 == strcmp(macro, MVAR_EVENT_TIME))
    {
        *replace_to = zbx_strdup(*replace_to, zbx_time2str(event->clock));
    }
    /* 判断 macro 是否等于 MVAR_EVENT_SOURCE_TRIGGERS，如果是，则执行以下操作：
     * 判断 replace_to 是否为空，如果不为空，则根据 macro 的值执行相应的操作：
     * 1. 如果 macro 等于 MVAR_EVENT_ACK_HISTORY 或 MVAR_EVENT_UPDATE_HISTORY，则调用 get_event_update_history 函数处理事件，并将结果存储在 replace_to 指向的字符串中；
     * 2. 如果 macro 等于 MVAR_EVENT_ACK_STATUS，则根据事件是否已确认执行以下操作：
     *    - 如果事件已确认，则将 "Yes" 复制到 replace_to 指向的字符串中；
     *    - 否则，将 "No" 复制到 replace_to 指向的字符串中。
     * 3. 如果 macro 等于 MVAR_EVENT_TAGS，则调用 get_event_tags 函数处理事件，并将结果存储在 replace_to 指向的字符串中；
     * 4. 如果 macro 等于 MVAR_EVENT_NSEVERITY，则将事件严重性转换为字符串，并使用 zbx_strdup 复制到 replace_to 指向的字符串中；
     * 5. 如果 macro 等于 MVAR_EVENT_SEVERITY，则根据事件严重性调用 get_trigger_severity_name 函数获取严重性名称，并将结果存储在 replace_to 指向的字符串中。
     * 如果 replace_to 为空，则执行以下操作：
     * 将 "unknown" 复制到 replace_to 指向的字符串中。
 */
    else if (EVENT_SOURCE_TRIGGERS == event->source)
    {
        if (0 == strcmp(macro, MVAR_EVENT_ACK_HISTORY) || 0 == strcmp(macro, MVAR_EVENT_UPDATE_HISTORY))
        {
            get_event_update_history(event, replace_to, recipient_userid);
        }
        else if (0 == strcmp(macro, MVAR_EVENT_ACK_STATUS))
        {
            *replace_to = zbx_strdup(*replace_to, event->acknowledged ? "Yes" : "No");
        }
        else if (0 == strcmp(macro, MVAR_EVENT_TAGS))
        {
            get_ev *                                                                            *
 * Function: get_current_event_value                                          *
 *                                                                            *
 * Purpose: request current event value by macro                              *
 *                                                                            *
 ******************************************************************************/
/******************************************** *                                                                            *
 * Function: get_current_event_value                                          *
 *                                                                            *
 * Purpose: request current event value by macro                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数`get_current_event_value`，接收三个参数，分别为`macro`、`event`和`replace_to`。根据`macro`的值，分别获取数据库事件中的状态值或数值值，并将结果复制到`replace_to`指向的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于获取数据库事件中的特定值
static void get_current_event_value(const char *macro, const DB_EVENT *event, char **replace_to)
{
    // 判断macro是否为MVAR_EVENT_STATUS，如果是，则执行以下操作
    if (0 == strcmp(macro, MVAR_EVENT_STATUS))
    {
        // 使用zbx_strdup复制字符串，将事件状态字符串赋值给replace_to指向的内存空间
        *replace_to = zbx_strdup(*replace_to,
                                 zbx_event_value_string(event->source, event->object, event->value));
    }
    // 判断macro是否为MVAR_EVENT_VALUE，如果是，则执行以下操作
    else if (0 == strcmp(macro, MVAR_EVENT_VALUE))
    {
        // 使用zbx_dsprintf格式化字符串，将事件值转换为字符串并赋值给replace_to指向的内存空间
        *replace_to = zbx_dsprintf(*replace_to, "%d", event->value);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: get_event_value                                                  *
 *                                                                            *
 * Purpose: request event value by macro                                      *
 *                                                                            *
 ******************************************************************************/
static void	get_event_value(const char *macro, const DB_EVENT *event, char **replace_to,
			const zbx_uint64_t *recipient_userid)
{
	if (0 == strcmp(macro, MVAR_EVENT_AGE))
	{
		*replace_to = zbx_strdup(*replace_to, zbx_age2str(time(NULL) - event->clock));
	}
	else if (0 == strcmp(macro, MVAR_EVENT_DATE))
	{
		*replace_to = zbx_strdup(*replace_to, zbx_date2str(event->clock));
	}
	else if (0 == strcmp(macro, MVAR_EVENT_ID))
	{
		*replace_to = zbx_dsprintf(*replace_to, ZBX_FS_UI64, event->eventid);
	}
	else if (0 == strcmp(macro, MVAR_EVENT_TIME))
	{
		*replace_to = zbx_strdup(*replace_to, zbx_time2str(event->clock));
	}
	else if (EVENT_SOURCE_TRIGGERS == event->source)
	{
		if (0 == strcmp(macro, MVAR_EVENT_ACK_HISTORY) || 0 == strcmp(macro, MVAR_EVENT_UPDATE_HISTORY))
		{
			get_event_update_history(event, replace_to, recipient_userid);
		}
		else if (0 == strcmp(macro, MVAR_EVENT_ACK_STATUS))
		{
			*replace_to = zbx_strdup(*replace_to, event->acknowledged ? "Yes" : "No");
		}
		else if (0 == strcmp(macro, MVAR_EVENT_TAGS))
		{
			get_event_tags(event, replace_to);
		}
		else if (0 == strcmp(macro, MVAR_EVENT_NSEVERITY))
		{
			*replace_to = zbx_dsprintf(*replace_to, "%d", (int)event->severity);
		}
		else if (0 == strcmp(macro, MVAR_EVENT_SEVERITY))
		{
			if (FAIL == get_trigger_severity_name(event->severity, replace_to))
				*replace_to = zbx_strdup(*replace_to, "unknown");
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: is_indexed_macro                                                 *
 *                                                                            *
 * Purpose: check if a token contains indexed macro                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的字符串 str 中的某个位置（由 p 指针指向）的字符是否为数字。如果是数字，返回 1，否则返回 0。这个函数主要用于判断一个宏是否具有索引属性。
 ******************************************************************************/
// 定义一个名为 is_indexed_macro 的静态函数，接收两个参数：一个字符串指针 str 和一个 zbx_token_t 类型的指针 token。
static int is_indexed_macro(const char *str, const zbx_token_t *token)
{
	// 定义一个字符指针 p，用于存储字符串 str 中的某个位置。
	const char *p;

	// 使用 switch 语句根据 token 的类型进行分支处理。
	switch (token->type)
	{
		// 当 token 的类型为 ZBX_TOKEN_MACRO 时，即遇到了一个宏。
		case ZBX_TOKEN_MACRO:
			// 计算字符串 str 中宏的位置，将其存储在 p 指针中。
			p = str + token->loc.r - 1;
			break;

		// 当 token 的类型为 ZBX_TOKEN_FUNC_MACRO 时，即遇到了一个函数宏。
		case ZBX_TOKEN_FUNC_MACRO:
			// 计算字符串 str 中函数宏的位置，将其存储在 p 指针中。
			p = str + token->data.func_macro.macro.r - 1;
			break;

		// 当 token 的类型不为 ZBX_TOKEN_MACRO 和 ZBX_TOKEN_FUNC_MACRO 时，
		// 表示不应该发生这种情况，程序异常，返回 FAIL。
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			// 返回失败状态码 FAIL。
			return FAIL;
	}

	// 判断 p 指针所指向的字符是否为数字，如果是数字，返回 1，否则返回 0。
	return '1' <= *p && *p <= '9' ? 1 : 0;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从一个字符串（str）和一个字符串范围（strloc）中查找一个匹配的宏（macros），并返回匹配到的宏。同时，如果找到匹配的宏，记录匹配的函数ID（N_functionid）。
 ******************************************************************************/
// 定义一个静态常量指针，用于存储宏列表
static const char *macro_in_list(const char *str, zbx_strloc_t strloc, const char **macros, int *N_functionid)
{
	// 定义一个指针变量，用于遍历宏列表
	const char **macro;
	// 定义一个字符指针，用于存储当前遍历到的宏
	const char *m;
	// 定义一个size_t类型的变量，用于记录遍历次数
	size_t i;

	// 遍历宏列表
	for (macro = macros; NULL != *macro; macro++)
	{
		// 遍历当前宏的字符串
		for (m = *macro, i = strloc.l; '\0' != *m && i <= strloc.r && str[i] == *m; m++, i++)
			;

		/* 检查宏是否结束，而strloc还没有结束，或者相反 */
		if (('\0' == *m && i <= strloc.r) || ('\0' != *m && i > strloc.r))
			continue;

		/* 如果strloc完全匹配宏 */
		if ('\0' == *m)
		{
			if (NULL != N_functionid)
				*N_functionid = 1;

			// 找到匹配的宏，跳出循环
			break;
		}

		/* 如果是最后一个字符不匹配，且是一个索引 */
		else if (i == strloc.		{
			if (NULL != N_functionid)
				*N_functionid = str[i] - '0';

			break;
		}
	}

	return *macro;
}

/******************************************************************************
 *                                                                            *
 * Function: get_trigger_function_value                                       *
 *                                                                            *
 * Purpose: trying to evaluate a trigger function                             *
 *         		{
			if (NULL != N_functionid)
				*N_functionid = str[i] - '0';

			break;
		}
	}

	return *macro;
}

/******************************************************************************
 *                                                                            *
 * Function: get_trigger_function_value                                       *
 *                                                                            *
 * Purpose: trying to evaluate a trigger function                             *
 *                                                                            *
 * Parameters: expression - [IN] trigger expression, source of hostnames and  *
 *                            item keys for {HOST.HOST} and {ITEM.KEY} macros *
 *             replace_to - [OUT] evaluation result                           *
 *             data       - [IN] string containing simple macro               *
 *             macro      - [IN] simple macro token location in string        *
 *                                                                            *
 * Return value: SUCCEED - successfully evaluated or invalid macro(s) in host *
 *                           and/or item key positions (in the latter case    *
 *                           replace_to remains unchanged and simple macro    *
 *                           shouldn't be replaced with anything)             *
 *               FAIL    - evaluation failed and macro has to be replaced     *
 *                           with STR_UNKNOWN_VARIABLE ("*UNKNOWN*")          *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: example: " {Zabbix server:{ITEM.KEY1}.last(0)} " to " 1.34 "     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码的主要目的是获取触发器函数的值。它接收一个表达式、一个用于替换的指针、一个数据指针以及一个简单宏结构体指针。首先，它检查数据中是否存在列表中的宏（host和key），如果存在，则从数据库中获取对应的触发器值。接着，处理宏替换，将替换后的数据传递给evaluate_macro_function函数，评估宏函数的替换结果。最后，恢复数据中的宏，并释放内存，返回评估结果。
 ******************************************************************************/
// 定义一个静态函数，用于获取触发器函数的值
static int get_trigger_function_value(const char *expression, char **replace_to, char *data,
                                     const zbx_token_simple_macro_t *simple_macro)
{
    // 定义一些变量
    char *host = NULL, *key = NULL;
    int N_functionid, ret = FAIL;

    // 检查数据中是否存在列表中的宏
    if (NULL != macro_in_list(data, simple_macro->host, simple_host_macros, &N_functionid))
    {
        // 如果存在host宏，则从数据库中获取触发器值
        if (SUCCEED != DBget_trigger_value(expression, &host, N_functionid, ZBX_REQUEST_HOST_HOST))
            goto out;
    }

    // 如果存在key宏，则从数据库中获取触发器值
    if (NULL != macro_in_list(data, simple_macro->key, simple_key_macros, &N_functionid))
    {
        if (SUCCEED != DBget_trigger_value(expression, &key, N_functionid, ZBX_REQUEST_ITEM_KEY_ORIG))
            goto out;
    }

    // 处理宏替换
    data[simple_macro->host.r + 1] = '\0';
    data[simple_macro->key.r + 1] = '\0';
    data[simple_macro->func_param.l] = '\0';
    data[simple_macro->func_param.r] = '\0';

    // 调用evaluate_macro_function函数，评估宏函数替换
    ret = evaluate_macro_function(replace_to, (NULL == host ? data + simple_macro->host.l : host),
                                 (NULL == key ? data + simple_macro->key.l : key), data + simple_macro->func.l,
                                 data + simple_macro->func_param.l + 1);

    // 恢复数据中的宏
    data[simple_macro->host.r + 1] = ':';
    data[simple_macro->key.r + 1] = '.';
    data[simple_macro->func_param.l] = '(';
    data[simple_macro->func_param.r] = ')';

    // 结束
    zbx_free(host);
    zbx_free(key);

    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: cache_trigger_hostids                                            *
 *                                                                            *
 * Purpose: cache host identifiers referenced by trigger expression           *
 *                                                                            *
 * Parameters: hostids             - [OUT] the host identifier cache          *
 *             expression          - [IN] the trigger expression              *
 *             recovery_expression - [IN] the trigger recovery expression     *
/******************************************************************************
 * *
 *整个代码块的主要目的是：当传入的空主机ID列表不为空时，根据给定的表达式和恢复表达式获取对应的函数ID列表，然后根据这些函数ID列表获取与之关联的主机ID列表，并将这些主机ID存储在传入的hostids向量中。
 ******************************************************************************/
/* 定义一个静态函数，用于触发缓存更新的主机ID列表 */
static void cache_trigger_hostids(zbx_vector_uint64_t *hostids, const char *expression,
                                const char *recovery_expression)
{
    /* 如果主机ID列表为空，则执行以下操作 */
    if (0 == hostids->values_num)
    {
        zbx_vector_uint64_t	functionids; // 创建一个存储函数ID的向量

        /* 创建函数ID向量 */
        zbx_vector_uint64_create(&functionids);

        /* 获取表达式对应的函数ID列表，存储在functionids向量中 */
        get_functionids(&functionids, expression);

        /* 获取恢复表达式对应的函数ID列表，存储在functionids向量中 */
        get_functionids(&functionids, recovery_expression);

        /* 根据functionids向量获取与之关联的主机ID列表，存储在hostids中 */
        DCget_hostids_by_functionids(&functionids, hostids);

        /* 释放functionids向量 */
        zbx_vector_uint64_destroy(&functionids);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: cache_item_hostid                                                *
 *                                                                            *
 * Purpose: cache host identifier referenced by an item or a lld-rule         *
 *                                                                            *
 * Parameters: hostids - [OUT] the host identifier cache                      *
 *             itemid  - [IN]  the item identifier                            *
 *                                                                            *
 ******************************************************************************/
/ *   a. 定义一个DC_ITEM结构体变量item，用于存储从配置文件中读取的item信息。
 *   b. 定义一个错误码变量errcode，用于存储从配置文件中读取item时的错误信息。
 *   c. 使用DCconfig_get_items_by_itemids函数从配置文件中读取指定itemid的item信息，并将读取到的item信息存储在item变量中。
 *   d. 判断错误码是否为SUCCEED，即读取操作是否成功。
 *   e. 如果读取成功，使用zbx_vector_uint64_append函数将item中的主机id添� *   a. 定义一个DC_ITEM结构体变量item，用于存储从配置文件中读取的item信息。
 *   b. 定义一个错误码变量errcode，用于存储从配置文件中读取item时的错误信息。
 *   c. 使用DCconfig_get_items_by_itemids函数从配置文件中读取指定itemid的item信息，并将读取到的item信息存储在item变量中。
 *   d. 判断错误码是否为SUCCEED，即读取操作是否成功。
 *   e. 如果读取成功，使用zbx_vector_uint64_append函数将item中的主机id添加到hostids vector中。
 *   f. 清理从配置文件中读取的item信息。
 ******************************************************************************/
static void	cache_item_hostid(zbx_vector_uint64_t *hostids, zbx_uint64_t itemid)
{
    // 定义一个静态函数，用于缓存itemid对应的主机id

    // 判断hostids中的元素个数是否为0，如果为0，则执行以下操作
    if (0 == hostids->values_num)
    {
        // 定义一个DC_ITEM结构体变量，用于存储从配置文件中读取的item信息
        DC_ITEM	item;
        // 定义一个错误码变量，用于存储从配置文件中读取item时的错误信息
        int	errcode;

        // 使用DCconfig_get_items_by_itemids函数从配置文件中读取指定itemid的item信息
        DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);
/******************************************************************************
 * *
 *整个代码块的主要目的是：处理负数双精度浮点数的后缀，将其转换为括号包裹的形式。具体来说，当传入的替换字符串表示的数为负数时，会将该字符串的第一个字符移动到第二个位置，并在末尾添加两个空格，形成括号包裹的形式。如果分配给替换字符串的空间不足，则会动态分配一个新的字符串缓冲区，并按相同规则进行处理。最后，将新分配的替换字符串的第一个字符设置为左括号，最后一个字符设置为右括号，并在末尾添加一个空字符串结束符。
 ******************************************************************************/
// 定义一个静态函数，用于处理负数双精度浮点数的后缀
static void	wrap_negative_double_suffix(char **replace_to, size_t *replace_to_alloc)
{
	// 定义一个size_t类型的变量replace_to_len，用于存储替换字符串的长度
	size_t	replace_to_len;

	// 判断替换字符串的第一个字符是否为负号('-')，如果不是，则直接返回，不进行处理
	if ('-' != (*replace_to)[0])
		return;

	// 获取替换字符串的长度
	replace_to_len = strlen(*replace_to);

	// 判断分配给替换字符串的空间是否足够，如果足够，则进行以下操作：
	if (NULL != replace_to_alloc && *replace_to_alloc >= replace_to_len + 3)
	{
		// 将替换字符串的负号移动到第二个位置，并在末尾添加两个空格，形成括号包裹的形式
		memmove(*replace_to + 1, *replace_to, replace_to_len);
	}
	else
	{
		// 如果分配给替换字符串的空间不足，则进行以下操作：
		char	*buffer;

		// 如果指定了分配空间的大小，则将其设置为替换字符串的长度+3
		if (NULL != replace_to_alloc)
			*replace_to_alloc = replace_to_len + 3;

		// 动态分配一个新的字符串缓冲区
		buffer = (char *)zbx_malloc(NULL, replace_to_len + 3);

		// 将原替换字符串的内容复制到新缓冲区的第二个位置，并在末尾添加两个空格，形成括号包裹的形式
		memcpy(buffer + 1, *replace_to, replace_to_len);

		// 释放原替换字符串的空间
		zbx_free(*replace_to);
		*replace_to = buffer;
	}

	// 将新分配的替换字符串的第一个字符设置为左括号('(')，最后一个字符设置为右括号(')'，并在末尾添加一个空字符串结束符'\0'
	(*replace_to)[0] = '(';
	(*replace_to)[replace_to_len + 1] = ')';
	(*replace_to)[replace_to_len + 2] = '\0';
}

	replace_to_len = strlen(*replace_to);

	if (NULL != replace_to_alloc && *replace_to_alloc >= replace_to_len + 3)
	{
		memmove(*replace_to + 1, *replace_to, replace_to_len);
	}
	else
	{
		char	*buffer;

		if (NULL != replace_to_alloc)
			*replace_to_alloc = replace_to_len + 3;

		buffer = (char *)zbx_malloc(NULL, replace_to_len + 3);

		memcpy(buffer + 1, *replace_to, replace_to_len);

		zbx_free(*replace_to);
		*replace_to = buffer;
	}

	(*replace_to)[0] = '(';
	(*replace_to)[replace_to_len + 1] = ')';
	(*replace_to)[replace_to_len + 2] = '\0';
}

/******************************************************************************
 * *
 *这块代码的主要目的是将一个整数（表示对象的状态）转换为对应的字符串表示。通过switch语句，根据传入的整数st，匹配到对应的状态，并返回相应的字符串。如果st的值不在上述情况下，返回字符串\"UNKNOWN\"。
 ******************************************************************************/
// 定义一个静态常量指针，用于存储字符串常量
static const char *zbx_dobject_status2str(int st)
{
	// 定义一个switch语句，根据传入的int值st，匹配对应的 case
	switch (st)
	{
		// 匹配DOCTYPE_STATUS_UP，即st等于0的情况，返回字符串"UP"
		case DOBJECT_STATUS_UP:
			return "UP";
		// 匹配DOCTYPE_STATUS_DOWN，即st等于1的情况，返回字符串"DOWN"
		case DOBJECT_STATUS_DOWN:
			return "DOWN";
		// 匹配DOBJECT_STATUS_DISCOVER，即st等于2的情况，返回字符串"DISCOVERED"
		case DOBJECT_STATUS_DISCOVER:
			return "DISCOVERED";
		// 匹配DOBJECT_STATUS_LOST，即st等于3的情况，返回字符串"LOST"
		case DOBJECT_STATUS_LOST:
			return "LOST";
		// 如果没有匹配到上述情况，即st为其他值，返回字符串"UNKNOWN"
		default:
			return "UNKNOWN";
	}
}


/******************************************************************************
 *                                                                            *
 * Function: substitute_simple_macros                                         *
 *                                                                            *
 * Purpose: substitute simple macros in data string with real values          *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * T
 ******************************************************************************/his function is used to substitute macros in a string. It takes the following parameters:

- `data`: A string containing the text to be processed.
- `data_alloc`: A pointer to the allocated memory for the `data` string.
- `data_len`: The length of the `data` string.
- `macro_type`: A bitmap indicating the types of macros to be processed (e.g., user macros, trigger macros, etc.).
- `error`: A pointer to an error message buffer.
- `maxerrlen`: The maximum length of the error message buffer.
- `replace`: A pointer to a buffer containing the replacement for a macro.
- `replace_len`: The length of the `replace` buffer.
- `userid`: The ID of the user performing the operation.
- `event`: An event object.
- `r_event`: An optional recovery event object.
- `actionid`: The ID of an action.
- `dc_host`: A/******************************************************************************
 * *
 *这段代码的主要目的是从给定的C语言表达式中提取所有的函数ID，并将它们存储在一个uint64类型的向量中。输出结果为一个成功或失败的标志。
 ******************************************************************************/
// 定义一个静态函数，用于提取表达式中的函数ID列表
static int	extract_expression_functionids(zbx_vector_uint64_t *functionids, const char *expressio/******************************************************************************
 * *
 *这段代码的主要目的是从给定的C语言表达式中提取所有的函数ID，并将它们存储在一个uint64类型的向量中。输出结果为一个成功或失败的标志。
 ******************************************************************************/
// 定义一个静态函数，用于提取表达式中的函数ID列表
static int	extract_expression_functionids(zbx_vector_uint64_t *functionids, const char *expression)
{
	// 定义两个指针，分别指向表达式中的左括号 '{' 和右括号 '}'
	const char	*bl, *br;

	// 初始化一个uint64类型的变量，用于存储提取到的函数ID
	zbx_uint64_t	functionid;

	// 使用一个for循环，遍历表达式中的所有 '{' 括号
	for (bl = strchr(expression, '{'); NULL != bl; bl = strchr(bl, '{'))
	{
		// 寻找表达式中第一个 '}' 括号的位置
		if (NULL == (br = strchr(bl, '}')))
			break;

		// 检查 bl+1 到 br-1 之间的字符是否为有效的uint64数值
		if (SUCCEED != is_uint64_n(bl + 1, br - bl - 1, &functionid))
			break;

		// 将提取到的函数ID添加到 functionids 向量中
		zbx_vector_uint64_append(functionids, functionid);

		// 更新 bl 指针的位置，继续查找下一个 '{' 括号
		bl = br + 1;
	}

	// 返回函数ID提取结果，若表达式中不存在 '{' 括号，则返回成功；否则返回失败
	return (NULL == bl ? SUCCEED : FAIL);
}


static void	zbx_extract_functionids(zbx_vector_uint64_t *functionids, zbx_vector_ptr_t *triggers)
{
	const char	*__function_name = "zbx_extract_functionids";

	DC_TRIGGER	*tr;
	int		i, values_num_save;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() tr_num:%d", __function_name, triggers->values_num);

	for (i = 0; i < triggers->values_num; i++)
	{
		const char	*error_expression = NULL;

		tr = (DC_TRIGGER *)triggers->values[i];

		if (NULL != tr->new_error)
			continue;

		values_num_save = functionids->values_num;

		if (SUCCEED != extract_expression_functionids(functionids, tr->expression))
		{
			error_expression = tr->expression;
		}
		else if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode &&
				SUCCEED != extract_expression_functionids(functionids, tr->recovery_expression))
		{
			error_expression = tr->recovery_expression;
		}

		if (NULL != error_expression)
		{
			tr->new_error = zbx_dsprintf(tr->new_error, "Invalid expression [%s]", error_expression);
			tr->new_value = TRIGGER_VALUE_UNKNOWN;
			functionids->values_num = values_num_save;
		}
	}

	zbx_vector_uint64_sort(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() functionids_num:%d", __function_name, functionids->values_num);
}

typedef struct
{
	DC_TRIGGER	*trigger;
	int		start_index;
	int		count;
}
zbx_trigger_func_position_t;

/******************************************************************************
 *                                                                            *
 * Function: expand_trigger_macros                                            *
 *                                                                            *
 * Purpose: expand macros in a trigger expression                             *
 *                                                                            *
 * Parameters: event - The trigger event structure                            *
 *             trigger - The trigger where to expand macros in                *
 *                                                                            *
 * Author: Andrea Biscuola                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于扩展触发器的宏。首先判断是否需要替换触发器的表达式宏，如果需要，则调用`substitute_simple_macros`函数进行替换。如果替换失败，返回FAIL。接下来，判断触发器的恢复模式是否为恢复表达式，如果是，则再次调用`substitute_simple_macros`函数进行恢复表达式的替换。替换成功后，返回SUCCEED。
 ******************************************************************************/
// 定义一个函数，用于扩展触发器的宏
static int expand_trigger_macros(DB_EVENT *event, DC_TRIGGER *trigger, char *error, size_t maxerrlen)
{
    // 判断是否需要替换简单宏
    if (FAIL == substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                         &trigger->expression, MACRO_TYPE_TRIGGER_EXPRESSION, error, maxerrlen))
    {
        // 如果替换失败，返回FAIL
        return FAIL;
    }

    // 判断触发器的恢复模式是否为恢复表达式
    if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == trigger->recovery_mode)
    {
        // 如果需要替换恢复表达式，调用替换函数
        if (FAIL == substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                            &trigger->recovery_expression, MACRO_TYPE_TRIGGER_EXPRESSION, error, maxerrlen))
        {
            // 替换失败，返回FAIL
            return FAIL;
        }
    }

    // 替换成功，返回SUCCEED
    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_link_triggers_with_functions                                 *
 *                                                                            *
 * Purpose: triggers links with functions                                     *
 *                                                                            *
 * Parameters: triggers_func_pos - [IN/OUT] pointer to the list of triggers   *
 *                                 with functions position in functionids     *
 *                                 array                                      *
 *             functionids       - [IN/OUT] array of function IDs             *
 *             trigger_order     - [IN] array of triggers                     *
 *                                                                            *
 ******************************************************************************/
static void	zbx_link_triggers_with_functions(zbx_vector_ptr_t *triggers_func_pos, zbx_vector_uint64_t *functionids,
		zbx_vector_ptr_t *trigger_order)
{
	const char		*__function_name = "zbx_link_triggers_with_functions";

	zbx_vector_uint64_t	funcids;
	DC_TRIGGER		*tr;
	DB_EVENT		ev;
	int			i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() trigger_order_num:%d", __function_name, trigger_order->values_num);

	zbx_vector_uint64_create(&funcids);
	zbx_vector_uint64_reserve(&funcids, functionids->values_num);

	ev.object = EVENT_OBJECT_TRIGGER;

	for (i = 0; i < trigger_order->values_num; i++)
	{
		zbx_trigger_func_position_t	*tr_func_pos;

		tr = (DC_TRIGGER *)trigger_order->values[i];

		if (NULL != tr->new_error)
			continue;

		ev.value = tr->value;

		expand_trigger_macros(&ev, tr, NULL, 0);

		if (SUCCEED == extract_expression_functionids(&funcids, tr->expression))
		{
			tr_func_pos = (zbx_trigger			triggers_func_pos->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_determine_items_in_expressions                               *
 *                                                                            *
 * Purpose: mark triggers that use one of the items in problem expression     *
 *          with ZBX_DC_TRIGGER_PROBLEM_EXPRESSION flag                       *
 *   			triggers_func_pos->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_determine_items_in_expressions                               *
 *                                                                            *
 * Purpose: mark triggers that use one of the items in problem expression     *
 *          with ZBX_DC_TRIGGER_PROBLEM_EXPRESSION flag                       *
 *                                                                            *
 * Parameters: trigger_order - [IN/OUT] pointer to the list of triggers       *
 *             itemids       - [IN] array of item IDs                         *
 *             item_num      - [IN] number of items                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_determine_items_in_expressions(zbx_vector_ptr_t *trigger_order, const zbx_uint64_t *itemids, int item_num)
{
	zbx_vector_ptr_t	triggers_func_pos;
	zbx_vector_uint64_t	functionids, itemids_sorted;
	DC_FUNCTION		*functions = NULL;
	int			*errcodes = NULL, t, f;

	zbx_vector_uint64_create(&itemids_sorted);
	zbx_vector_uint64_append_array(&itemids_sorted, itemids, item_num);
	zbx_vector_uint64_sort(&itemids_sorted, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_vector_ptr_create(&triggers_func_pos);
	zbx_vector_ptr_reserve(&triggers_func_pos, trigger_order->values_num);

	zbx_vector_uint64_create(&functionids);
	zbx_vector_uint64_reserve(&functionids, item_num);

	zbx_link_triggers_with_functions(&triggers_func_pos, &functionids, trigger_order);

	functions = (DC_FUNCTION *)zbx_malloc(functions, sizeof(DC_FUNCTION) * functionids.values_num);
	errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * functionids.values_num);

	DCconfig_get_functions_by_functionids(functions, functionids.values, errcodes, functionids.values_num);

	for (t = 0; t < triggers_func_pos.values_num; t++)
	{
		zbx_trigger_func_position_t	*func_pos = (zbx_trigger_func_position_t *)triggers_func_pos.values[t];

		for (f = func_pos->start_index; f < func_pos->start_index + func_pos->count; f++)
		{
			if (FAIL != zbx_vector_uint64_bsearch(&itemids_sorted, functions[f].itemid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				func_pos->trigger->flags |= ZBX_DC_TRIGGER_PROBLEM_EXPRESSION;
				break;
			}
		}
	}

	DCconfig_clean_functions(functions, errcodes, functionids.values_num);
	zbx_free(errcodes);
	zbx_free(functions);

	zbx_vector_ptr_clear_ext(&triggers_func_pos, zbx_ptr_free);
	zbx_vector_ptr_destroy(&triggers_func_pos);

	zbx_vector_uint64_clear(&functionids);
	zbx_vector_uint64_destroy(&functionids);

	zbx_vector_uint64_clear(&itemids_sorted);
	zbx_vector_uint64_destroy(&itemids_sorted);
}

typedef struct
{
	/* input data */
	zbx_uint64_t	itemid;
	char		*function;
	char		*parameter;
	zbx_timespec_t	timespec;

	/* output data */
	char		*value;
	char		*error;
}
zbx_func_t;

typedef struct
{
	zbx_uint64_t	functionid;
	zbx_func_t	*func;
}
zbx_ifunc_t;

/******************************************************************************
 * *
 *这块代码的主要目的是计算一个名为 func_hash_func 的哈希值。函数接受一个 void 类型的指针作为参数，然后对该指针所指向的结构体（zbx_func_t）的各个字段进行哈希值计算。最后返回计算得到的哈希值。在这个过程中，使用了 ZBX_DEFAULT_UINT64_HASH_FUNC、ZBX_DEFAULT_STRING_HASH_ALGO 和 ZBX_DEFAULT_HASH_ALGO 函数来计算不同类型字段的哈希值。
 ******************************************************************************/
// 定义一个名为 func_hash_func 的静态函数，该函数接受一个 void 类型的指针作为参数
static zbx_hash_t	func_hash_func(const void *data)
{
	// 转换指针类型，将 void 类型指针转换为 zbx_func_t 类型的指针，以便后续操作
	const zbx_func_t	*func = (const zbx_func_t *)data;
	zbx_hash_t		hash; // 声明一个 zbx_hash_t 类型的变量 hash，用于存储计算得到的哈希值

	// 使用 ZBX_DEFAULT_UINT64_HASH_FUNC 计算 func->itemid 的哈希值，并将其存储在 hash 变量中
	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&func->itemid);

	// 使用 ZBX_DEFAULT_STRING_HASH_ALGO 计算 func->function 的哈希值，并将结果与 hash 变量进行拼接
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(func->function, strlen(func->function), hash);

	// 使用 ZBX_DEFAULT_STRING_HASH_ALGO 计算 func->parameter 的哈希值，并将结果与 hash 变量进行拼接
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(func->parameter, strlen(func->parameter), hash);

	// 使用 ZBX_DEFAULT_HASH_ALGO 计算 func->timespec.sec 的哈希值，并将结果与 hash 变量进行拼接
	hash = ZBX_DEFAULT_HASH_ALGO(&func->timespec.sec, sizeof(func->timespec.sec), hash);

	// 使用 ZBX_DEFAULT_HASH_ALGO 计算 func->timespec.ns 的哈希值，并将结果与 hash 变量进行拼接
	hash = ZBX_DEFAULT_HASH_ALGO(&func->timespec.ns, sizeof(func->timespec.ns), hash);

	// 返回计算得到的哈希值 hash
	return hash;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个zbx_func_t结构体变量（func1和func2）的各个字段是否相等。如果相等，则返回0；否则，返回非零值。其中，zbx_func_t结构体可能包含如下字段：itemid、function、parameter、timespec（包括sec和ns）。在比较过程中，如果发现任何不相等的字段，都将返回非零值。
 ******************************************************************************/
// 定义一个名为 func_compare_func 的静态函数，该函数接收两个参数，均为 void 类型的指针
static int	func_compare_func(const void *d1, const void *d2)
{
	// 将指针 d1 和 d2 分别转换为指向 zbx_func_t 类型的指针，方便后续操作
	const zbx_func_t	*func1 = (const zbx_func_t *)d1;
	const zbx_func_t	*func2 = (const zbx_func_t *)d2;
	// 定义一个整型变量 ret，用于存储比较结果
	int			ret;

	// 判断 func1 和 func2 的 itemid 是否相等，若不相等，则返回一个非零值
	ZBX_RETURN_IF_NOT_EQUAL(func1->itemid, func2->itemid);

	// 比较 func1 和 func2 的 function 字段，若不相等，则返回一个非零值
	if (0 != (ret = strcmp(func1->function, func2->function)))
		return ret;

	// 比较 func1 和 func2 的 parameter 字段，若不相等，则返回一个非零值
	if (0 != (ret = strcmp(func1->parameter, func2->parameter)))
		return ret;

	// 判断 func1 和 func2 的 timespec.sec 是否相等，若不相等，则返回一个非零值
	ZBX_RETURN_IF_NOT_EQUAL(func1->timespec.sec, func2->timespec.sec);
/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的函数ID数组、触发器数组，以及函数和触发器的哈希集合，遍历函数ID列表，获取对应的函数信息，并将这些函数添加到函数哈希集合中。同时，将这些函数添加到内部函数哈希集合中。最后，释放内存。
 ******************************************************************************/
// 定义静态函数zbx_populate_function_items，传入参数：
// const zbx_vector_uint64_t *functionids：函数ID数组
// zbx_hashset_t *funcs：    int *errcodes = NULL;
    zbx_ifunc_t ifunc_local;
    zbx_func_t *func, func_local;

    // 打印调试信息，输入函数名和函数ID个数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() functionids_num:%d", __function_name, functionids->values_num);

    // 初始化函数局部变量
    func_local.value = NULL;
    func_local.error = NULL;

    // 分配内存，存储函数结构体数组
    functions = (DC_FUNCTION *)zbx_malloc(functions, sizeof(DC_FUNCTION) * functionids->values_num);
    errcodes = (i    int *errcodes = NULL;
    zbx_ifunc_t ifunc_local;
    zbx_func_t *func, func_local;

    // 打印调试信息，输入函数名和函数ID个数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() functionids_num:%d", __function_name, functionids->values_num);

    // 初始化函数局部变量
    func_local.value = NULL;
    func_local.error = NULL;

    // 分配内存，存储函数结构体数组
    functions = (DC_FUNCTION *)zbx_malloc(functions, sizeof(DC_FUNCTION) * functionids->values_num);
    errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * functionids->values_num);

    // 根据函数ID获取函数列表
    DCconfig_get_functions_by_functionids(functions, functionids->values, errcodes, functionids->values_num);

    // 遍历函数列表，处理每个函数
    for (i = 0; i < functionids->values_num; i++)
    {
        // 如果获取函数失败，跳过该循环
        if (SUCCEED != errcodes[i])
            continue;

        // 保存函数局部变量
        func_local.itemid = functions[i].itemid;

        // 查找触发器列表中对应的触发器
        if (FAIL != (j = zbx_vector_ptr_bsearch(triggers, &functions[i].triggerid,
                                                 ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
        {
/******************************************************************************
 * 
 ******************************************************************************/
static void zbx_evaluate_item_functions(zbx_hashset_t *funcs, zbx_vector_ptr_t *unknown_msgs)
{
    // 定义变量
    const char *__function_name = "zbx_evaluate_item_functions";
    DC_ITEM *items = NULL;
    char value[MAX_BUFFER_LEN];
    int i;
    zbx_func_t *func;
    zbx_vector_uint64_t itemids;
    int *errcodes = NULL;
    zbx_hashset_iter_t iter;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() funcs_num:%d", __function_name, funcs->num_data);

    // 创建一个uint64类型的vector，用于存储itemid
    zbx_vector_uint64_create(&itemids);
    // 为vector分配内存，预留funcs->num_data个元素
    zbx_vector_uint64_reserve(&itemids, funcs->num_data);

    // 遍历函数集合
    zbx_hashset_iter_reset(funcs, &iter);
    while (NULL != (func = (zbx_func_t *)zbx_hashset_iter_next(&iter)))
    {
        // 将func->itemid添加到itemidsvector中
        zbx_vector_uint64_append(&itemids, func->itemid);
    }

    // 对itemidsvector进行排序
    zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    // 去重
    zbx_vector_uint64_uniq(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 分配内存，用于存储DC_ITEM结构体数组和错误码数组
    items = (DC_ITEM *)zbx_malloc(items, sizeof(DC_ITEM) * (size_t)itemids.values_num);
    errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * (size_t)itemids.values_num);

    // 调用DCconfig_get_items_by_itemids函数，根据itemids获取物品信息，并将结果存储在items数组中
    // 同时将对应的错误码存储在errcodes数组中
    DCconfig_get_items_by_itemids(items, itemids.values, errcodes, itemids.values_num);

    // 遍历函数集合
    zbx_hashset_iter_reset(funcs, &iter);
    while (NULL != (func = (zbx_func_t *)zbx_hashset_iter_next(&iter)))
    {
        // 判断item是否存在
        i = zbx_vector_uint64_bsearch(&itemids, func->itemid, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

        // 如果item不存在，记录错误信息
        if (SUCCEED != errcodes[i])
        {
            // 构造错误信息
            func->error = zbx_dsprintf(func->error, "Cannot evaluate function \"%s(%s)\":"
                                " item does not exist.",
                                func->function, func->parameter);
            continue;
        }

        // 判断item是否禁用或所属主机禁用
        if (ITEM_STATUS_ACTIVE != items[i].status)
        {
            // 构造错误信息
            func->error = zbx_dsprintf(func->error, "Cannot evaluate function \"%s:%s.%s(%s)\":"
                                " item is disabled.",
                                items[i].host.host, items[i].key_orig, func->function, func->parameter);
            continue;
        }

        // 判断item所属主机是否禁用
        if (HOST_STATUS_MONITORED != items[i].host.status)
        {
            // 构造错误信息
            func->error = zbx_dsprintf(func->error, "Cannot evaluate function \"%s:%s.%s(%s)\":"
                                " item belongs to a disabled host.",
                                items[i].host.host, items[i].key_orig, func->function, func->parameter);
            continue;
        }

        // 判断item是否支持函数评估
        if (ITEM_STATE_NOTSUPPORTED == items[i].state && FAIL == evaluatable_for_notsupported(func->function))
        {
            // 构造未知信息
            unknown_msg = zbx_dsprintf(NULL,
                                "Cannot evaluate function \"%s:%s.%s(%s)\": item is not supported.",
                                items[i].host.host, items[i].key_orig, func->function, func->parameter);

            // 将未知信息添加到unknown_msgsvector中
            zbx_vector_ptr_append(unknown_msgs, unknown_msg);
            ret_unknown = 1;
        }

        // 调用evaluate_function函数评估函数
        if (0 == ret_unknown && SUCCEED != evaluate_function(value, &items[i], func->function,
                                                            func->parameter, &func->timespec, &error))
        {
            // 构造错误信息
            if (NULL != error)
            {
                unknown_msg = zbx_dsprintf(NULL,
                                "Cannot evaluate function \"%s:%s.%s(%s)\": %s.",
                                items[i].host.host, items[i].key_orig,
                                func->function, func->parameter, error);

                // 释放func->error内存
                zbx_free(func->error);
                // 释放error内存
                zbx_free(error);
            }
            else
            {
                unknown_msg = zbx_dsprintf(NULL,
                                "Cannot evaluate function \"%s:%s.%s(%s)\".",
                                items[i].host.host, items[i].key_orig,
                                func->function, func->parameter);

                // 释放func->error内存
                zbx_free(func->error);
            }

            // 将未知信息添加到unknown_msgsvector中
            zbx_vector_ptr_append(unknown_msgs, unknown_msg);
            ret_unknown = 1;
        }

        // 如果未知标志为0，则将函数值存储在func中
        if (0 == ret_unknown)
        {
            // 构造函数值
            func->value = zbx_strdup(func->value, value);
        }
        else
        {
            // 构造特殊未知值，例如ZBX_UNKNOWN0，ZBX_UNKNOWN1等
            func


		/* do not evaluate if the item is disabled or belongs to a disabled host */

		if (ITEM_STATUS_ACTIVE != items[i].status)
		{
			func->error = zbx_dsprintf(func->error, "Cannot evaluate function \"%s:%s.%s(%s)\":"
					" item is disabled.",
					items[i].host.host, items[i].key_orig, func->function, func->parameter);
			continue;
		}

		if (HOST_STATUS_MONITORED != items[i].host.status)
		{
			func->error = zbx_dsprintf(func->error, "Cannot evaluate function \"%s:%s.%s(%s)\":"
					" i			/* compose and store 'unknown' message for future use */
			unknown_msg = zbx_dsprintf(NULL,
					"Cannot evaluate function \"%s:%s.%s(%s)\": item is not supported.",
					items[i].host.host, items[i].key_orig, func->function, func->parameter);

			zbx_free(func->error);
			zbx_vector_ptr_append(unknown_msgs, unknown_msg);
			ret_unknown = 1;
		}

		if (0 == ret_unknown && SUCCEED != evaluate_function(value, &items[i], func->function,
				func->parameter, &func->timespec, &error))
		{
			/* compose and store er			/* compose and store 'unknown' message for future use */
			unknown_msg = zbx_dsprintf(NULL,
					"Cannot evaluate function \"%s:%s.%s(%s)\": item is not supported.",
					items[i].host.host, items[i].key_orig, func->function, func->parameter);

			zbx_free(func->error);
			zbx_vector_ptr_append(unknown_msgs, unknown_msg);
			ret_unknown = 1;
		}

		if (0 == ret_unknown && SUCCEED != evaluate_function(value, &items[i], func->function,
				func->parameter, &func->timespec, &error))
		{
			/* compose and store error message for future use */
			if (NULL != error)
			{
				unknown_msg = zbx_dsprintf(NULL,
						"Cannot evaluate function \"%s:%s.%s(%s)\": %s.",
						items[i].host.host, items[i].key_orig, func->function,
						func->parameter, error);

				zbx_free(func->error);
				zbx_free(error);
			}
			else
			{
				unknown_msg = zbx_dsprintf(NULL,
						"Cannot evaluate function \"%s:%s.%s(%s)\".",
						items[i].host.host, items[i].key_orig,
						func->function, func->parameter);

				zbx_free(func->error);
			}

			zbx_vector_ptr_append(unknown_msgs, unknown_msg);
			ret_unknown = 1;
		}

		if (0 == ret_unknown)
		{
			func->value = zbx_strdup(func->value, value);
		}
		else
		{
			/* write a special token of unknown value with 'unknown' message number, like */
			/* ZBX_UNKNOWN0, ZBX_UNKNOWN1 etc. not wrapped in () */
			func->value = zbx_dsprintf(func->value, ZBX_UNKNOWN_STR "%d",
					unknown_msgs->values_num - 1);
		}
/******************************************************************************
 * *
 *该代码块的主要目的是替换表达式中的函数结果。输入为一个表达式字符串、一个指向哈希集的指针（用于查找函数）、一个输出缓冲区和一个错误指针。输出是一个替换了函数结果的新字符串。如果表达式无效或处理过程中出现错误，函数将输出错误信息并返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于替换表达式中的函数结果
static int substitute_expression_functions_results(zbx_hashset_t *ifuncs, char *expression, char **out,
                                               size_t *out_alloc, char **error)
{
    // 定义两个指针，分别指向表达式中的起始和结束位置
    char *br, *bl;
    size_t out_offset = 0;
    zbx_uint64_t functionid;
    zbx_func_t *func;
    zbx_ifunc_t *ifunc;

    // 遍历表达式，查找函数调用
    for (br = expression, bl = strchr(expression, '{'); NULL != bl; bl = strchr(bl, '{'))
    {
        // 将起始位置的字符串复制到输出缓冲区，并更新输出缓冲区分配大小
        zbx_strcpy_alloc(out, out_alloc, &out_offset, br);
        *bl = '\0';

        // 检查结束位置是否为合法的函数调用
        if (NULL == (br = strchr(bl, '}')))
        {
            // 输出错误信息，并返回失败
            *error = zbx_strdup(*error, "Invalid trigger expression");
            return FAIL;
        }

        // 替换结束位置的字符为空字符，以便后续处理
        *br = '\0';

        // 将起始位置指向函数调用后的位置
        br = bl + 1;

        // 将函数ID转换为整数类型
        ZBX_STR2UINT64(functionid, br);

        // 跳过结束位置后的字符
        br++;
        bl = br;

        // 在哈希集中查找对应的函数
        if (NULL == (ifunc = (zbx_ifunc_t *)zbx_hashset_search(ifuncs, &functionid)))
        {
            // 输出错误信息，并返回失败
            *error = zbx_dsprintf(*error, "Cannot obtain function"
                                " and item for functionid: " ZBX_FS_UI64, functionid);
            return FAIL;
        }

        // 获取函数指针
        func = ifunc->func;

        // 检查函数是否有错误信息
        if (NULL != func->error)
        {
            // 输出错误信息，并返回失败
            *error = zbx_strdup(*error, func->error);
            return FAIL;
        }

        // 检查函数返回值是否为空
        if (NULL == func->value)
        {
            // 输出错误信息，并返回失败
            *error = zbx_strdup(*error, "Unexpected error while processing a trigger expression");
            return FAIL;
        }

        // 检查函数返回值是否符合预期格式
        if (SUCCEED != is_double_suffix(func->value, ZBX_FLAG_DOUBLE_SUFFIX) || '-' == *func->value)
        {
            // 在输出缓冲区添加左括号
            zbx_chrcpy_alloc(out, out_alloc, &out_offset, '(');
            // 添加函数返回值
            zbx_strcpy_alloc(out, out_alloc, &out_offset, func->value);
            // 在输出缓冲区添加右括号
            zbx_chrcpy_alloc(out, out_alloc, &out_offset, ')');
        }
        else
        {
            // 直接复制函数返回值到输出缓冲区
            zbx_strcpy_alloc(out, out_alloc, &out_offset, func->value);
        }
    }

    // 将剩余的字符复制到输出缓冲区
    zbx_strcpy_alloc(out, out_alloc, &out_offset, br);

    // 返回成功
    return SUCCEED;
}

			zbx_strcpy_alloc(out, out_alloc, &out_offset, func->value);
			zbx_chrcpy_alloc(out, out_alloc, &out_offset, ')');
		}
		else
			zbx_strcpy_alloc(out, out_alloc, &out_offset, func->value);
	}

	zbx_strcpy_alloc(out, out_alloc, &out_offset, br);

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是替换触发器表达式中的函数结果。具体步骤如下：
 *
 *1. 定义常量和变量，用于存储日志标识、触发器指针、输出字符串指针、输出字符串分配大小、循环变量等。
 *2. 调用`zabbix_log`函数记录日志，表示进入函数。
 *3. 分配内存用于存储替换后的表达式输出。
 *4. 遍历触发器列表，依次处理每个触发器：
 *   a. 如果新的错误信息不为空，跳过本次循环。
 *   b. 调用`substitute_expression_functions_results`函数替换表达式中的函数结果，并将结果存储在`out`字符串中。
 *   c. 记录日志，表示表达式替换结果。
 *   d. 保存替换后的表达式，存储在`tr->expression`中。
 *   e. 如果恢复模式为`TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION`，重复步骤4b-d，替换恢复表达式。
 *5. 释放分配的内存。
 *6. 调用`zabbix_log`函数记录日志，表示函数执行结束。
 ******************************************************************************/
static void zbx_substitute_functions_results(zbx_hashset_t *ifuncs, zbx_vector_ptr_t *triggers)
{
    // 定义常量
    const char *__function_name = "zbx_substitute_functions_results";

    // 定义变量
    DC_TRIGGER *tr;
    char *out = NULL;
    size_t out_alloc = TRIGGER_EXPRESSION_LEN_MAX;
    int i;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() ifuncs_num:%d tr_num:%d",
              __function_name, ifuncs->num_data, triggers->values_num);

    // 分配内存
    out = (char *)zbx_malloc(out, out_alloc);

    // 遍历触发器列表
    for (i = 0; i < triggers->values_num; i++)
    {
        tr = (DC_TRIGGER *)triggers->values[i];

        // 如果新的错误信息不为空，跳过本次循环
        if (NULL != tr->new_error)
            continue;

        // 调用函数替换表达式中的函数结果
        if (SUCC        // 如果恢复模式为TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION
        if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
        {
            // 替换恢复表达式
            if (SUCCEED != substitute_expression_functions_results(ifuncs,
                                                                  tr->recovery_expression, &out, &out_alloc, &tr->new_error))
            {
                tr->new_value = TRIGGER_VALUE_UNKNOWN;
                continue;
            }

            /        // 如果恢复模式为TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION
        if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
        {
            // 替换恢复表达式
            if (SUCCEED != substitute_expression_functions_results(ifuncs,
                                                                  tr->recovery_expression, &out, &out_alloc, &tr->new_error))
            {
                tr->new_value = TRIGGER_VALUE_UNKNOWN;
                continue;
            }

            // 记录日志
            zabbix_log(LOG_LEVEL_DEBUG, "%s() recovery_expression[%d]:'%s' => '%s'", __function_name, i,
                      tr->recovery_expression, out);

            // 保存替换后的恢复表达式
            tr->recovery_expression = zbx_strdup(tr->recovery_expression, out);
        }
    }

    // 释放内存
    zbx_free(out);

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: substitute_functions                                             *
 *                                                                            *
 * Purpose: substitute expression functions with their values                 *
 *                                                                            *
 * Parameters: triggers - [IN] vector of DC_TRIGGGER pointers, sorted by      *
 *                             triggerids                                     *
 *             unknown_msgs - vector for storing messages for NOTSUPPORTED    *
 *                            items and failed functions                      *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev, Aleksandrs Saveljevs        *
 *                                                                            *
 * Comments: example: "({15}>10) or ({123}=1)" => "(26.416>10) or (0=1)"      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是替换 C 语言代码中的函数，并将替换后的函数结果存储在 triggers 指向的 vector 中。具体步骤如下：
 *
 *1. 定义一个名为 substitute_functions 的静态函数，不接受任何参数。
 *2. 创建一个 vector 用于存储函数 ID，并从 triggers 指向的 vector 中提取函数 ID 并存储到 functionids vector 中。
 *3. 如果 functionids vector 为空，直接退出函数。
 *4. 创建一个 hashset 用于存储条件函数（ifuncs），另一个 hashset 用于存储函数信息（funcs）。
 *5. 填充函数信息到 funcs 和 ifuncs hashset 中。
 *6. 如果 ifuncs hashset 中的元素数量不为零，则评估未知消息中的函数，并替换函数结果存储到 triggers 中。
 *7. 销毁 ifuncs 和 funcs hashset。
 *8. 销毁 functionids vector。
 *9. 记录函数进入和退出日志。
 ******************************************************************************/
// 定义一个名为 substitute_functions 的静态函数，该函数不接受任何参数
static void substitute_functions(zbx_vector_ptr_t *triggers, zbx_vector_ptr_t *unknown_msgs)
{
/******************************************************************************
 * *
 *主要目的：这个代码块用于评估触发器的表达式，并根据评估结果更新触发器的值。在处理过程中，还会处理恢复模式和表达式。如果遇到错误，会记录错误信息并继续处理其他触发器。评估完成后，清除错误消息 vector 并打印调试信息。
 ******************************************************************************/
void evaluate_expressions(zbx_vector_ptr_t *triggers)
{
	const char *__function_name = "evaluate_expressions";

	// 定义变量
	DB_EVENT event;
	DC_TRIGGER *tr;
	int i;
	double expr_result;
	zbx_vector_ptr_t unknown_msgs; // 存储未知值来源的消息
	char err[MAX_STRING_LEN];

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() tr_num:%d", __function_name, triggers->values_num);

	// 初始化事件结构体
	event.object = EVENT_OBJECT_TRIGGER;

	// 遍历触发器数组
	for (i = 0; i < triggers->values_num; i++)
	{
		tr = (DC_TRIGGER *)triggers->values[i];

		// 设置触发器值
		event.value = tr->value;

		// 扩展触发器宏
		if (SUCCEED != expand_trigger_macros(&event, tr, err, sizeof(err)))
		{
			// 记录错误信息
			tr->new_error = zbx_dsprintf(tr->new_error, "Cannot evaluate expression: %s", err);
			tr->new_value = TRIGGER_VALUE_UNKNOWN;
		}
	}

	// 假设大多数情况下不会有不支持的项和函数错误
	// 初始化错误消息 vector，但不分配空间
	zbx_vector_ptr_create(&unknown_msgs);

	// 替换函数
	substitute_functions(triggers, &unknown_msgs);

	// 计算新的触发器值 based on 恢复模式和表达式评估
	for (i = 0; i < triggers->values_num; i++)
	{
		tr = (DC_TRIGGER *)triggers->values[i];

		// 如果有错误信息，跳过
		if (NULL != tr->new_error)
			continue;

		// 评估表达式
		if (SUCCEED != evaluate(&expr_result, tr->expression, err, sizeof(err), &unknown_msgs))
		{
			// 记录错误信息
			tr->new_error = zbx_strdup(tr->new_error, err);
			tr->new_value = TRIGGER_VALUE_UNKNOWN;
			continue;
		}

		// 触发器表达式评估为真，设置 PROBLEM 值
		if (SUCCEED != zbx_double_compare(expr_result, 0.0))
		{
			// 如果恢复模式为 none，保持原值
			if (0 == (tr->flags & ZBX_DC_TRIGGER_PROBLEM_EXPRESSION))
			{
				tr->new_value = TRIGGER_VALUE_NONE;
			}
			else
				tr->new_value = TRIGGER_VALUE_PROBLEM;

			// 继续处理恢复表达式
			if (TRIGGER_VALUE_PROBLEM == tr->value && TRIGGER_RECOVERY_MODE_NONE != tr->recovery_mode)
			{
				// 如果是恢复模式为表达式
				if (TRIGGER_RECOVERY_MODE_EXPRESSION == tr->recovery_mode)
				{
					tr->new_value = TRIGGER_VALUE_OK;
					continue;
				}

				// 处理恢复表达式
				if (SUCCEED != evaluate(&expr_result, tr->recovery_expression, err, sizeof(err), &unknown_msgs))
				{
					// 记录错误信息
					tr->new_error = zbx_strdup(tr->new_error, err);
					tr->new_value = TRIGGER_VALUE_UNKNOWN;
					continue;
				}

				// 恢复表达式评估为真，设置 OK 值
				if (SUCCEED != zbx_double_compare(expr_result, 0.0))
				{
					tr->new_value = TRIGGER_VALUE_OK;
					continue;
				}
			}
		}

		// 否则保持原值
		tr->new_value = TRIGGER_VALUE_NONE;
	}

	// 清除未知消息 vector
	zbx_vector_ptr_clear_ext(&unknown_msgs, zbx_ptr_free);
	// 销毁未知消息 vector
	zbx_vector_ptr_destroy(&unknown_msgs);

	// 如果调试级别足够，打印调试信息
	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		for (i = 0; i < triggers->values_num; i++)
		{
			tr = (DC_TRIGGER *)triggers->values[i];

			// 如果新的错误信息不为空，打印错误信息
			if (NULL != tr->new_error)
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s():expression [%s] cannot be evaluated: %s",
						__function_name, tr->expression, tr->new_error);
			}
		}

		// 打印函数结束信息
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name)				tr->new_value = TRIGGER_VALUE_PROBLEM;

			continue;
		}

		/* otherwise try to recover trigger by setting OK value */
		if (TRIGGER_VALUE_PROBLEM == tr->value && TRIGGER_RECOVERY_MODE_NONE != tr->recovery_mode)
		{
			if (TRIGGER_RECOVERY_MODE_EXPRESSION == tr->recovery_mode)
			{
				tr->new_value = TRIGGER_VALUE_OK;
				continue;
			}

			/* processing recovery expression mode */
			if (SUCCEED != evaluate(&expr_result, tr->recovery_expression, err, sizeof(err), &unknown_msgs))
			{
				tr->new_error = 				tr->new_value = TRIGGER_VALUE_PROBLEM;

			continue;
		}

		/* otherwise try to recover trigger by setting OK value */
		if (TRIGGER_VALUE_PROBLEM == tr->value && TRIGGER_RECOVERY_MODE_NONE != tr->recovery_mode)
		{
			if (TRIGGER_RECOVERY_MODE_EXPRESSION == tr->recovery_mode)
			{
				tr->new_value = TRIGGER_VALUE_OK;
				continue;
			}

			/* processing recovery expression mode */
			if (SUCCEED != evaluate(&expr_result, tr->recovery_expression, err, sizeof(err), &unknown_msgs))
			{
				tr->new_error = zbx_strdup(tr->new_error, err);
				tr->new_value = TRIGGER_VALUE_UNKNOWN;
				continue;
			}

			if (SUCCEED != zbx_double_compare(expr_result, 0.0))
			{
				tr->new_value = TRIGGER_VALUE_OK;
				continue;
			}
		}

		/* no changes, keep the old value */
		tr->new_value = TRIGGER_VALUE_NONE;
	}

	zbx_vector_ptr_clear_ext(&unknown_msgs, zbx_ptr_free);
	zbx_vector_ptr_destroy(&unknown_msgs);

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		for (i = 0; i < triggers->values_num; i++)
		{
			tr = (DC_TRIGGER *)triggers->values[i];

			if (NULL != tr->new_error)
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s():expression [%s] cannot be evaluated: %s",
						__function_name, tr->expression, tr->new_error);
			}
		}

		zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: process_simple_macro_token                                       *
 *                                                                            *
 * Purpose: trying to resolve the discovery macros in item key parameters     *
 *          in simple macros like {host:key[].func()}                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是处理简单的宏token。它首先检查输入的字符串是否包含合法的简单宏，然后提取键和函数参数，并对键和函数参数中的宏进行替换。最后，将替换后的字符串替换原始字符串中的相应部分，并调整token的边界。如果整个过程成功，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
/* 定义一个函数，用于处理简单的宏token
 * 输入参数：
 *   data：字符指针，指向输入的字符串
 *   token：zbx_token_t类型指针，指向解析后的token结构体
 *   jp_row：指向zbx_json_parse结构体的指针，用于存储解析过程中的信息
 *   error：字符指针，用于存储错误信息
 *   max_error_len：error字符串的最大长度
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	process_simple_macro_token(char **data, zbx_token_t *token, const struct zbx_json_parse *jp_row,
		char *error, size_t max_error_len)
{
	/* 定义一些变量，用于存储中间结果 */
	char	*key = NULL, *replace_to = NULL, *dot, *params;
	size_t	replace_to_offset = 0, replace_to_alloc = 128, lld_start, lld_end;
	int	ret = FAIL;

	/* 检查是否是一个合法的简单宏 */
	if ('{' == (*data)[token->data.simple_macro.host.l] &&
			NULL == macro_in_list(*data, token->data.simple_macro.host, simple_host_macros, NULL))
	{
		goto out;
	}

	/* 分配内存，用于存储替换后的字符串 */
	replace_to = (char *)zbx_malloc(NULL, replace_to_alloc);

	/* 获取键和函数参数的起始位置 */
	lld_start = token->data.simple_macro.key.l;
	lld_end = token->data.simple_macro.func_param.r - 1;
/******************************************************************************
 * *
 *这个代码块的主要目的是处理ZBX_TOKEN_LLD_MACRO类型的token。首先，根据token的类型和位置，替换掉macro中的字符。然后，根据传入的标志位，对替换后的字符进行相应的处理，如转义、数值包装等。最后，将处理后的结果替换回原token所在的位置，并返回成功。
 ******************************************************************************/
// 定义一个C语言函数，名为process_lld_macro_token
static int process_lld_macro_token(char **data, zbx_token_t *token, int flags, const struct zbx_json_parse *jp_row,
                                 char *error, size_t error_len)
{
    // 定义一些变量
    char c, *replace_to = NULL;
    int ret = SUCCEED, l ,r;
    size_t replace_to_alloc = 0;

    // 判断token的类型
    if (ZBX_TOKEN_LLD_FUNC_MACRO == token->type)
    {
        l = token->data.lld_func_macro.macro.l;
        r = token->data.lld_func_macro.macro.r;
    }
    else
    {
        l = token->loc.l;
        r = token->loc.r;
    }

    // 替换macro
    c = (*data)[r + 1];
    (*data)[r + 1] = '\0';

    // 查找并替换macro
    if (SUCCEED != zbx_json_value_by_name_dyn(jp_row, *data + l, &replace_to, &replace_to_alloc, NULL))
    {
        // 调试日志
        zabbix_log(LOG_LEVEL_DEBUG, "cannot substitute macro \"%s\": not found in value set", *data + l);

        // 如果有数值标志，则输出错误信息
        if (0 != (flags & ZBX_TOKEN_NUMERIC))
        {
            zbx_snprintf(error, error_len, "no value for macro \"%s\"", *data + l);
            ret = FAIL;
        }

        // 恢复原样
        (*data)[r + 1] = c;
        zbx_free(replace_to);

        // 返回结果
        return ret;
    }

    // 替换macro
    (*data)[r + 1] = c;

    // 判断token类型，如果是函数macro，则执行函数
    if (ZBX_TOKEN_LLD_FUNC_MACRO == token->type)
    {
        replace_to_alloc = 0;
        // 计算macro函数结果
        if (SUCCEED != zbx_calculate_macro_function(*data, &token->data.lld_func_macro, &replace_to))
        {
            // 调试日志
            int	len = token->data.lld_func_macro.func.r - token->data.lld_func_macro.func.l + 1;

            zabbix_log(LOG_LEVEL_DEBUG, "cannot execute function \"%.*s\"", len,
                        *data + token->data.lld_func_macro.func.l);

            // 如果有数值标志，则输出错误信息
            if (0 != (flags & ZBX_TOKEN_NUMERIC))
            {
                zbx_snprintf(error, error_len, "unable to execute function \"%.*s\"", len,
                              *data + token->data.lld_func_macro.func.l);
                return FAIL;
            }

            // 释放replace_to
            zbx_free(replace_to);
        }
    }

    // 根据标志处理结果
    if (0 != (flags & ZBX_TOKEN_NUMERIC))
    {
        // 如果结果是双精度浮点数
        if (SUCCEED == is_double_suffix(replace_to, ZBX_FLAG_DOUBLE_SUFFIX))
        {
            // 包装负数后缀
            wrap_negative_double_suffix(&replace_to, &replace_to_alloc);
        }
        // 否则释放replace_to
        else
        {
            zbx_free(replace_to);
            zbx_snprintf(error, error_len, "not numeric value in macro \"%.*s\"",
                         (int)(token->loc.r - token->loc.l + 1), *data + token->loc.l);
            return FAIL;
        }
    }
    else if (0 != (flags & ZBX_TOKEN_JSON))
    {
        // 转义JSON字符
        zbx_json_escape(&replace_to);
    }
    else if (0 != (flags & ZBX_TOKEN_XML))
    {
                zbx_free(replace_to);
        replace_to = replace_to_esc;
    }
    else if (0 != (flags & ZBX_TOKEN_XPATH))
    {
        // 转义XPath字符
        xml_escape_xpath(&replace_to);
    }

    // 如果有replace_to，处理内存分配和替换
    if (NULL != replace_to)
    {
        size_t	data_alloc, data_len;

        data_alloc = data_len = strlen(*data) + 1;
        // 替换内存
        token->loc.r += zbx_replace_mem_dyn(data, &data_alloc, &data_len, token->loc.l,
                                zbx_free(replace_to);
        replace_to = replace_to_esc;
    }
    else if (0 != (flags & ZBX_TOKEN_XPATH))
    {
        // 转义XPath字符
        xml_escape_xpath(&replace_to);
    }

    // 如果有replace_to，处理内存分配和替换
    if (NULL != replace_to)
    {
        size_t	data_alloc, data_len;

        data_alloc = data_len = strlen(*data) + 1;
        // 替换内存
        token->loc.r += zbx_replace_mem_dyn(data, &data_alloc, &data_len, token->loc.l,
                                token->loc.r - token->loc.l + 1, replace_to, strlen(replace_to));
        // 释放replace_to
        zbx_free(replace_to);
    }

    // 返回成功
    return SUCCEED;
}


	if (0 != (flags & ZBX_TOKEN_NUMERIC))
	{
		if (SUCCEED == is_double_suffix(replace_to, ZBX_FLAG_DOUBLE_SUFFIX))
		{
			wrap_negative_double_suffix(&replace_to, &replace_to_alloc);
		}
		else
		{
			zbx_free(replace_to);
			zbx_snprintf(error, error_len, "not numeric value in macro \"%.*s\"",
					(int)(token->loc.r - token->loc.l + 1), *data + token->loc.l);
			return FAIL;
		}
	}
	else if (0 != (flags & ZBX_TOKEN_JSON))
	{
		zbx_json_escape(&replace_to);
	}
	else if (0 != (flags & ZBX_TOKEN_XML))
	{
		char	*replace_to_esc;

		replace_to_esc = xml_escape_dyn(replace_to);
		zbx_free(replace_to);
		replace_to = replace_to_esc;
	}
	else if (0 != (flags & ZBX_TOKEN_REGEXP))
	{
		zbx_regexp_escape(&replace_to);
	}
	else if (0 != (flags & ZBX_TOKEN_REGEXP_OUTPUT))
	{
		char	*replace_to_esc;

		replace_to_esc = zbx_dyn_escape_string(replace_to, "\\");
		zbx_free(replace_to);
		replace_to = replace_to_esc;
	}
	else if (0 != (flags & ZBX_TOKEN_XPATH))
	{
		xml_escape_xpath(&replace_to);
	}

	if (NULL != replace_to)
	{
		size_t	data_alloc, data_len;

		data_alloc = data_len = strlen(*data) + 1;
		token->loc.r += zbx_replace_mem_dyn(data, &data_alloc, &data_len, token->loc.l,
				token->loc.r - token->loc.l + 1, replace_to, strlen(replace_to));
		zbx_free(replace_to);
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: process_user_macro_token                                         *
 *                                                                            *
 * Purpose: expand discovery macro in user macro context                      *
 *                                                                            *
 * Parameters: data      - [IN/OUT] the expression containing lld macro       *
 *             token     - [IN/OUT] the token with user macro location data   *
 *             jp_row    - [IN] discovery data                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理C语言中的用户自定义宏。具体来说，该函数接收一个指向存储用户自定义宏的字符指针、一个指向存储宏信息的zbx_token_t结构体指针和一个指向存储解析结果的zbx_json_parse结构体指针。函数首先判断用户自定义宏是否有上下文，如果没有，则直接返回。如果有上下文，则进行以下操作：
 *
 *1. 判断数据字符串中第一个字符是否为双引号，如果是，则设置force_quote标志位。
 *2. 提取用户自定义宏的上下文字符串。
 *3. 调用 substitute_lld_macros 函数处理宏替换。
 *4. 为上下文字符串添加引号，并存储到context_esc中。
 *5. 更新zbx_token_user_macro结构体中的context_r。
 *6. 使用zbx_replace_string()函数替换原始字符串中的宏。
 *7. 更新token->loc.r，以便在后续处理其他token时使用。
 *8. 释放context_esc和context内存。
 ******************************************************************************/
/* 定义一个静态函数，用于处理用户自定义宏token
 * 参数：
 *   data：指向存储用户自定义宏的字符指针
 *   token：指向存储宏信息的zbx_token_t结构体指针
 *   jp_row：指向存储解析结果的zbx_json_parse结构体指针
 */
static void process_user_macro_token(char **data, zbx_token_t *token, const struct zbx_json_parse *jp_row)
{
	/* 定义一些变量，用于存储解析过程中的数据 */
	int force_quote;               // 用于存储是否需要引号替换的标志位
	size_t context_r;              // 用于存储context的长度
	char *context, *context_esc;   // 用于存储context字符串及其转义后的字符串
	zbx_token_user_macro_t *macro; // 用于存储用户自定义宏的信息

	/* 如果用户自定义宏没有上下文，则直接返回，无需处理 */
	if (0 == token->data.user_macro.context.l)
		return;

	/* 判断data字符串中第一个字符是否为双引号，如果是，则设置force_quote标志位 */
	force_quote = ('"' == (*data)[macro->context.l]);
	context = zbx_user_macro_unquote_context_dyn(*data + macro->context.l, macro->context.r - macro->context.l + 1);

	/* 调用 substitute_lld_macros 函数处理宏替换 */
	/* substitute_lld_macros() 函数不会失败，因为它会设置 ZBX_TOKEN_LLD_MACRO 或 ZBX_TOKEN_LLD_FUNC_MACRO 标志位 */
	substitute_lld_macros(&context, jp_row, ZBX_TOKEN_LLD_MACRO | ZBX_TOKEN_LLD_FUNC_MACRO, NULL, 0);

	/* 为context字符串添加引号，并存储到context_esc中 */
	context_esc = zbx_user_macro_quote_context_dyn(context, force_quote);

	/* 更新macro->context.r，以便在后续替换字符串时使用 */
	context_r = macro->context.r;

	/* 使用zbx_replace_string()函数替换原始字符串中的宏 */
	zbx_replace_string(data, macro->context.l, &context_r, context_esc);

	/* 更新token->loc.r，以便在后续处理其他token时使用 */
	token->loc.r += context_r - macro->context.r;

	/* 释放context_esc和context内存 */
	zbx_free(context_esc);
	zbx_free(context);
}


/******************************************************************************
 *                                                                            *
 * Function: substitute_func_macro                                            *
 *                                                                            *
 * Purpose: substitute lld macros in function macro parameters                *
 *                                                                            *
 * Parameters: data   - [IN/OUT] pointer to a buffer                          *
 *             token  - [IN/OUT] the token with function macro location data  *
 *             jp_row - [IN] discovery data                                   *
 *             error  - [OUT] error message                                   *
 *             max_error_len - [IN] the size of error buffer                  *
 *                                                                            *
 * Return value: SUCCEED - the lld macros were resolved successfully          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
/* 定义一个替换函数，用于在 C 语言代码中替换函数宏调用 */
static int	substitute_func_macro(char **data, zbx_token_t *token, const struct zbx_json_parse *jp_row,
		char *error, size_t max_error_len)
{
	/* 定义一些变量，用于保存替换过程中的信息 */
	int	ret;
	char	*exp = NULL;
	size_t	exp_alloc = 0, exp_offset = 0;
	size_t	par_l = token->data.func_macro.func_param.l, par_r = token->data.func_macro.func_param.r;

	/* 调用替换函数，将函数宏替换为具体的函数参�/* 定义一个替换函数，用于在 C 语言代码中替换函数宏调用 */
static int	substitute_func_macro(char **data, zbx_token_t *token, const struct zbx_json_parse *jp_row,
		char *error, size_t max_error_len)
{
	/* 定义一些变量，用于保存替换过程中的信息 */
	int	ret;
	char	*exp = NULL;
	size_t	exp_alloc = 0, exp_offset = 0;
	size_t	par_l = token->data.func_macro.func_param.l, par_r = token->data.func_macro.func_param.r;

	/* 调用替换函数，将函数宏替换为具体的函数参数 */
	ret = substitute_function_lld_param(*data + par_l + 1, par_r - (par_l + 1), 0, &exp, &exp_alloc, &exp_offset,
			jp_row, error, max_error_len);

	/* 如果替换成功，进行以下操作 */
	if (SUCCEED == ret)
	{
		/* 复制剩余部分，包括闭括号，并替换函数参数 */
		zbx_strncpy_alloc(&exp, &exp_alloc, &exp_offset, *data + par_r, token->loc.r - (par_r - 1));
		zbx_replace_string(data, par_l + 1, &token->loc.r, exp);
	}

	/* 释放分配的内存 */
	zbx_free(exp);

	/* 返回替换结果 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: substitute_lld_macros                                            *
 *                                                                            *
 * Parameters: data   - [IN/OUT] pointer to a buffer                          *
 *             jp_row - [IN] discovery data                                   *
 *             flags  - [IN] ZBX_MACRO_ANY - all LLD macros will be resolved  *
 *                            without validation of the value type            *
 *                           ZBX_MACRO_NUMERIC - values for LLD macros should *
 *                            be numeric                                      *
 *                           ZBX_MACRO_SIMPLE - LLD macros, located in the    *
 *                            item key parameters in simple macros will be    *
 *                            resolved considering quotes.                    *
 *                            Flag ZBX_MACRO_NUMERIC doesn't affect these     *
 *                            macros.                                         *
 *                           ZBX_MACRO_FUNC - function macros will be         *
 *                            skipped (lld macros inside function macros will *
 *                            be ignored) for macros specified in func_macros *
 *                            array                                           *
/******************************************************************************
 * *
 *整个代码块的主要目的是替换C语言代码中的LLD宏。该函数接收多个参数，包括一个指向数据的指针、一个指向JSON解析结构的指针、一个表示宏类型的整数、一个错误信息的指针以及一个错误信息长度的整数。函数内部首先记录日志，表示函数开始执行。然后遍历输入的数据，查找所有的token。根据token的类型进行切换，处理不同的宏类型。如果找到函数宏，且该函数宏在预定义的列表中，则替换函数宏。最后记录日志，表示函数执行结束，并返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，用于替换LLD宏
int substitute_lld_macros(char **data, const struct zbx_json_parse *jp_row, int flags, char *error,
                         size_t max_error_len)
{
    // 定义一个常量，表示函数名
    const char *__function_name = "substitute_lld_macros";

    // 定义一些变量，用于保存函数执行过程中的信息
    int		ret = SUCCEED, pos = 0;
    zbx_token_t	token;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() data:'%s'", __function_name, *data);

    // 遍历输入的数据，查找所有的token
    while (SUCCEED == ret && SUCCEED == zbx_token_find(*data, pos, &token, ZBX_TOKEN_SEARCH_BASIC))
    {
        // 如果token的类型包含了flags中的宏
        if (0 != (token.type & flags))
        {
            // 根据token的类型进行切换
            switch (token.type)
            {
                // 处理LLD宏和LLD函数宏
                case ZBX_TOKEN_LLD_MACRO:
                case ZBX_TOKEN_LLD_FUNC_MACRO:
                    ret = process_lld_macro_token(data, &token, flags, jp_row, error,
                                                max_error_len);
                    // 更新position
                    pos = token.loc.r;
                    break;
                // 处理用户自定义宏
                case ZBX_TOKEN_USER_MACRO:
                    process_user_macro_token(data, &token, jp_row);
                    // 更新position
                    pos = token.loc.r;
                    break;
                // 处理简单宏
                case ZBX_TOKEN_SIMPLE_MACRO:
                    process_simple_macro_token(data, &token, jp_row, error, max_error_len);
                    // 更新position
                    pos = token.loc.r;
                    break;
                // 处理函数宏
                case ZBX_TOKEN_FUNC_MACRO:
                    // 如果宏在预定义的列表中，则替换函数宏
                    if (NULL != macro_in_list(*data, token.data.func_macro.macro, mod_macros, NULL))
                    {
                        ret = substitute_func_macro(data, &token, jp_row, error, max_error_len);
                        // 更新position
                        pos = token.loc.r;
                    }
                    break;
            }
        }

        // 更新position
        pos++;
    }

/******************************************************************************
 * *
 *整个代码块的主要目的是替换 key_param 中的宏，并根据不同的关键字类型和级别进行相应的处理。具体来说，该代码块执行以下操作：
 *
 *1. 初始化 replace_key_param_data 结构体指针。
 *2. 判断关键字类型和级别，如果关键字类型为 ZBX_KEY_TYPE_ITEM 且级别为 0，直接返回成功。
 *3. 判断数据中是否包含大括号 '{'，如果不包含，直接返回成功。
 *4. 复制数据到 param 指针数组中，并去掉引号（如果级别为 0）。
 *5. 如果级别不为 0，则去除 key_param 中的引号。
 *6. 如果 jp_row 为 NULL，则使用简单替换宏替换参数；否则，使用 LLB 替换宏替换参数。
 *7. 如果级别不为 0，且引号标志为 1，则对 key_param 进行引号替换。
 *8. 返回成功。
 ******************************************************************************/
// 定义一个名为 replace_key_param_cb 的静态函数，该函数接受 7 个参数：
// 1. 一个指向数据的指针 data
// 2. 一个整数类型的关键字类型 key_type
// 3. 一个整数类型的级别 level
// 4. 一个整数类型的数量 num
// 5. 一个布尔类型的引号标志 quoted
// 6. 一个回调数据的指针 cb_data
// 7. 一个字符串指针数组 param

static int	replace_key_param_cb(const char *data, int key_type, int level, int num, int quoted, void *cb_data,
                            char **param)
{
    // 转换回调数据的指针类型，使其指向 replace_key_param_data 结构体
    replace_key_param_data_t	*replace_key_param_data = (replace_key_param_data_t *)cb_data;
    // 获取 replac    // 忽略 num 参数
    ZBX_UNUSED(num);

    // 判断关键字类型和级别，如果关键字类型为 ZBX_KEY_TYPE_ITEM 且级别为 0，直接返回成功
    if (ZBX_KEY_TYPE_ITEM == key_type && 0 == level)
        return ret;

    // 判断数据中是否包含大括号 '{'，如果不包含，直接返回成功
    if (NULL == strchr(data, '{'))
        return ret;

    // 复制数据到 param 指针数组中，并去掉引号（如果级别为 0）
    *param = zbx_strdup(NULL, data);

    // 如�    // 忽略 num 参数
    ZBX_UNUSED(num);

    // 判断关键字类型和级别，如果关键字类型为 ZBX_KEY_TYPE_ITEM 且级别为 0，直接返回成功
    if (ZBX_KEY_TYPE_ITEM == key_type && 0 == level)
        return ret;

    // 判断数据中是否包含大括号 '{'，如果不包含，直接返回成功
    if (NULL == strchr(data, '{'))
        return ret;

    // 复制数据到 param 指针数组中，并去掉引号（如果级别为 0）
    *param = zbx_strdup(NULL, data);

    // 如果级别不为 0，则去除 key_param 中的引号
    if (0 != level)
        unquote_key_param(*param);

    // 如果 jp_row 为 NULL，则使用简单替换宏替换参数
    if (NULL == jp_row)
        substitute_simple_macros(NULL, NULL, NULL, NULL, hostid, NULL, dc_item, NULL, NULL,
                                param, macro_type, NULL, 0);
    // 如果 jp_row 不为 NULL，则使用 LLD 替换宏替换参数
    else
        substitute_lld_macros(param, jp_row, ZBX_MACRO_ANY, NULL, 0);

    // 如果级别不为 0，且引号标志为 1，则对 key_param 进行引号替换
    if (0 != level)
    {
        if (FAIL == (ret = quote_key_param(param, quoted)))
            zbx_free(*param);
    }

    // 返回成功
    return ret;
}

			char **param)
{
	replace_key_param_data_t	*replace_key_param_data = (replace_key_param_data_t *)cb_data;
	zbx_uint64_t			*hostid = replace_key_param_data->hostid;
	DC_ITEM				*dc_item = replace_key_param_data->dc_item;
	const struct zbx_json_parse	*jp_row = replace_key_param_data->jp_row;
	int				macro_type = replace_key_param_data->macro_type, ret = SUCCEED;

	ZBX_UNUSED(num);

	if (ZBX_KEY_TYPE_ITEM == key_type && 0 == level)
		return ret;

	if (NULL == strchr(data, '{'))
		return ret;

	*param = zbx_strdup(NULL, data);

	if (0 != level)
		unquote_key_param(*param);
/******************************************************************************
 * *
 *整个代码块的主要目的是从给定的触发器（triggers）中提取有效的函数ID（functionids），并对提取到的函数ID进行排序和去重。在提取函数ID的过程中，遇到错误表达式时，记录错误信息并设置触发器的错误状态。
 ******************************************************************************/
// 定义静态函数zbx_extract_functionids，输入参数为一个zbx_vector_uint64_t类型的指针（functionids），一个zbx_vector_ptr_t类型的指针（triggers）
static void zbx_extract_functionids(zbx_vector_uint64_t *functionids, zbx_vector_ptr_t *triggers)
{
	// 定义常量字符串，表示函数名
	const char *__function_name = "zbx_extract_functionids";

	// 定义DC_TRIGGER结构体指针，用于遍历触发器
	DC_TRIGGER *tr;
	int i, values_num_save;

	// 记录日志，输入函数名和触发器数量
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() tr_num:%d", __function_name, triggers->values_num);

	// 遍历触发器数组
	for (i = 0; i < triggers->values_num; i++)
	{
		// 定义错误表达式指针，初始值为空
		const char *error_expression = NULL;

		// 获取当前触发器
		tr = (DC_TRIGGER *)triggers->values[i];

		// 如果触发器有错误表达式，跳过本次循环
		if (NULL != tr->new_error)
			continue;

		// 保存函数ID列表的长度
		values_num_save = functionids->values_num;

		// 提取表达式中的函数ID，若失败，记录错误表达式
		if (SUCCEED != extract_expression_functionids(functionids, tr->expression))
		{
			error_expression = tr->expression;
		}
		else if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode &&
				SUCCEED != extract_expression_functionids(functionids, tr->recovery_expression))
		{
			error_expression = tr->recovery_expression;
		}

		// 如果错误表达式不为空，设置触发器的错误信息和值
		if (NULL != error_expression)
		{
			tr->new_error = zbx_dsprintf(tr->new_error, "Invalid expression [%s]", error_expression);
			tr->new_value = TRIGGER_VALUE_UNKNOWN;
			functionids->values_num = values_num_save;
		}
	}

	// 对函数ID列表进行排序
	zbx_vector_uint64_sort(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 对函数ID列表去重
	zbx_vector_uint64_uniq(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 记录日志，输出函数名和函数ID数量
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() functionids_num:%d", __function_name, functionids->values_num);
}

 *           echo.sh["{$MACRO}"]     | a]\    | undefined         | FAIL      *
 *           echo.sh[{$MACRO}]       | [a     | echo.sh["a]"]     | SUCCEED   *
 *           echo.sh[{$MACRO}]       | [a\    | undefined         | FAIL      *
 *           echo.sh["{$MACRO}"]     | [a     | echo.sh["[a"]     | SUCCEED   *
 *           echo.sh["{$MACRO}"]     | [a\    | undefined         | FAIL      *
 *           ifInOctets.{#SNMPINDEX} | 1      | ifInOctets.1      | SUCCEED   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是替换键参数。该函数接收多个参数，包括数据指针、主机ID、DC_ITEM指针、JSON解析结构指针、键类型、错误字符串指针和错误字符串长度。根据传入的键类型，切换到相应的处理逻辑，并调用动态函数`replace_key_params_dyn`进行替换操作。函数最后返回替换后的结果。在执行过程中，还对函数 entry 和 exit 进行了调试日志记录。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "substitute_key_macros";

// 定义替换键参数数据结构
replace_key_param_data_t replace_key_param_data;

// 定义键类型
int key_type;

// 定义函数入口，传入参数
int substitute_key_macros(char **data, zbx_uint64_t *hostid, DC_ITEM *dc_item, const struct zbx_json_parse *jp_row,
                         int macro_type, char *error, size_t maxerrlen)
{
    // 调试日志：进入函数，输出传入数据
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() data:'%s'", __function_name, *data);

    // 初始化替换键参数数据结构
    replace_key_param_data.hostid = hostid;
    replace_key_param_data.dc_item = dc_item;
    replace_key_param_data.jp_row = jp_row;
    replace_key_param_data.macro_type = macro_type;

    // 根据键类型进行切换
    switch (macro_type)
    {
        case MACRO_TYPE_ITEM_KEY:
            key_type = ZBX_KEY_TYPE_ITEM;
            break;
        case MACRO_TYPE_SNMP_OID:
            key_type = ZBX_KEY_TYPE_OID;
            break;
        default:
            THIS_SHOULD_NEVER_HAPPEN;
            exit(EXIT_FAILURE);
    }

    // 调用替换键参数的动态函数
    ret = replace_key_params_dyn(data, key_type, replace_key_param_cb, &replace_key_param_data, error, maxerrlen);

    // 调试日志：函数结束，输出结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s data:'%s'", __function_name, zbx_result_string(ret), *data);

    // 返回替换键参数的返回值
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: substitute_function_lld_param                             * size_t len：字符串长度
 * unsigned char key_in_param：关键字参数，用于识别是否需要解析主机名和键名
 * char **exp：输出参数，用于存储替换后的字符串
 * size_t *exp_alloc：输出参数，用于存储分配给 exp 的内存大小
 * size_t *exp_offset：输出参数，用于记录当前处理到的字符位置
 * const struct zbx_json_parse *jp_row：指向 JSON 解析结构的指针，用于获取宏替换所需的信息
 * char *error：错误信息指针
 * size_t max_err * size_t len：字符串长度
 * unsigned char key_in_param：关键字参数，用于识别是否需要解析主机名和键名
 * char **exp：输出参数，用于存储替换后的字符串
 * size_t *exp_alloc：输出参数，用于存储分配给 exp 的内存大小
 * size_t *exp_offset：输出参数，用于记录当前处理到的字符位置
 * const struct zbx_json_parse *jp_row：指向 JSON 解析结构的指针，用于获取宏替换所需的信息
 * char *error：错误信息指针
 * size_t max_error_len：错误信息的最大长度
 *
 * 函数主要目的是对输入的字符串进行替换操作，特别地，当遇到关键字参数时，会将主机名和键名进行替换
 * 替换后的字符串存储在 exp 指针所指向的内存区域，exp_alloc 和 exp_offset 用于记录分配的内存和当前处理到的字符位置
 * 函数返回成功或失败，失败时输出错误信息
 */
int	substitute_function_lld_param(const char *e, size_t len, unsigned char key_in_param,
		char **exp, size_t *exp_alloc, size_t *exp_offset, const struct zbx_json_parse *jp_row,
		char *error, size_t max_error_len)
{
	/* 定义日志级别 */
	const char	*__function_name = "substitute_function_lld_param";
	int		ret = SUCCEED;
	size_t		sep_pos;
	char		*param = NULL;
	const char	*p;

	/* 记录函数调用日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 如果输入字符串长度为0，直接返回空字符串 */
	if (0 == len)
	{
		zbx_strcpy_alloc(exp, exp_alloc, exp_offset, "");
		goto out;
	}

	/* 遍历输入字符串，查找参数分隔符 */
	for (p = e; p < len + e ; p += sep_pos + 1)
	{
		size_t	param_pos, param_len, rel_len = len - (p - e);
/******************************************************************************
 * *
 *整个代码块的主要目的是将触发器与函数关联起来，以便在触发器触发时执行相应的函数。具体来说，这段代码实现了以下功能：
 *
 *1. 定义必要的变量和结构体，用于存储触发器、函数ID等信息。
 *2. 遍历触发器顺序 vector，依次处理每个触发器。
 *3. 检查触发器是否有错误信息，如有则跳过此次循环。
 *4. 扩展触发器宏，准备处理触发器。
 *5. 提取表达式中的函数ID，并将触发器与函数的关系存储在 memory 中。
 *6. 将存储触发器与函数关系的 memory 添加到 functionids vector 中，以便后续使用。
 *7. 销毁已创建的 memory，并记录日志，表示函数执行结束。
 ******************************************************************************/
// 定义一个静态函数，用于将触发器与函数关联起来
static void zbx_link_triggers_with_functions(zbx_vector_ptr_t *triggers_func_pos, zbx_vector_uint64_t *functionids, zbx_vector_ptr_t *trigger_order)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "zbx_link_triggers_with_functions";

	// 创建一个uint64类型的 vector，用于存储函数ID
	zbx_vector_uint64_t funcids;
	// 定义一个DC_TRIGGER类型的指针，用于遍历触发器
	DC_TRIGGER *tr;
	// 定义一个DB_EVENT类型的结构体，用于存储触发器信息
	DB_EVENT ev;
	// 定义一个整型变量，用于循环计数
	int i;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() trigger_order_num:%d", __function_name, trigger_order->values_num);

	// 初始化funcids vector
	zbx_vector_uint64_create(&funcids);
	// 为funcids vector预留空间
	zbx_vector_uint64_reserve(&funcids, functionids->values_num);

	// 初始化ev结构体
	ev.object = EVENT_OBJECT_TRIGGER;

	// 遍历触发器顺序 vector
	for (i = 0; i < trigger_order->values_num; i++)
	{
		// 定义一个zbx_trigger_func_position_t类型的指针，用于存储触发器与函数的关系
		zbx_trigger_func_position_t *tr_func_pos;

		// 获取触发器指针
		tr = (DC_TRIGGER *)trigger_order->values[i];

		// 如果触发器有错误信息，跳过此次循环
		if (NULL != tr->new_error)
			continue;
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据给定的触发器顺序（trigger_order）、项ID列表（itemids）和项数量（item_num），关联触发器与函数，并判断表达式中的项是否存在于排序后的项ID列表中。如果找到该项，设置触发器的标志位。最后清理相关资源。
 ******************************************************************************/
void	zbx_determine_items_in_expressions(zbx_vector_ptr_t *trigger_order, const zbx_uint64_t *itemids, int item_num)
{
	/* 定义并创建一些变量 */
	zbx_vector_ptr_t	triggers_func_pos;
	zbx_vector_uint64_t	functionids, itemids_sorted;
	DC_FUNCTION		*functions = NULL;
	int			*errcodes = NULL, t, f;

	/* 将itemids排序并存储到itemids_sorted中 */
	zbx_vector_uint64_create(&itemids_sorted);
	zbx_vector_uint64_append_array(&itemids_sorted, itemids, item_num);
	zbx_vector_uint64_sort(&itemids_sorted, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 创建triggers_func_pos向量 */
	zbx_vector_ptr_create(&triggers_func_pos);
	zbx_vector_ptr_reserve(&triggers_func_pos, trigger_order->values_num);

	/* 创建functionids向量 */
	zbx_vector_uint64_create(&functionids);
	zbx_vector_uint64_reserve(&functionids, item_num);

	/* 关联触发器与函数 */
	zbx_link_triggers_with_functions(&triggers_func_pos, &functionids, trigger_order);

	/* 分配内存并获取函数列表和错误码 */
	functions = (DC_FUNCTION *)zbx_malloc(functions, sizeof(DC_FUNCTION) * functionids.values_num);
	errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * functionids.values_num);

	/* 获取函数列表 */
	DCconfig_get_functions_by_functionids(functions, functionids.values, errcodes, functionids.values_num);

	/* 遍历触发器与函数的关联位置，判断表达式中的项是否存在于排序后的itemids中 */
	for (t = 0; t < triggers_func_pos.values_num; t++)
	{
		zbx_trigger_func_position_t	*func_pos = (zbx_trigger_func_position_t *)triggers_func_pos.values[t];

		for (f = func_pos->start_index; f < func_pos->start_index + func_pos->count; f++)
		{
			/* 在排序后的itemids中查找函数对应的项 */
			if (FAIL != zbx_vector_uint64_bsearch(&itemids_sorted, functions[f].itemid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				/* 如果找到该项，设置触发器的标志位 */
				func_pos->trigger->flags |= ZBX_DC_TRIGGER_PROBLEM_EXPRESSION;
				break;
			}
		}
	}

	/* 清理获取的函数列表和错误码 */
	DCconfig_clean_functions(functions, errcodes, functionids.values_num);
	zbx_free(errcodes);
	zbx_free(functions);

	/* 清理触发器与函数的关联位置 */
	zbx_vector_ptr_clear_ext(&triggers_func_pos, zbx_ptr_free);
	zbx_vector_ptr_destroy(&triggers_func_pos);

	/* 清理相关向量 */
	zbx_vector_uint64_clear(&functionids);
	zbx_vector_uint64_destroy(&functionids);

	zbx_vector_uint64_clear(&itemids_sorted);
	zbx_vector_uint64_destroy(&itemids_sorted);
}

	/* 返回成功或失败 */
	return ret;
}

		}

		/* copy the parameter */
		zbx_strcpy_alloc(exp, exp_alloc, exp_offset, param);

		/* copy what was after the parameter (including separator) */
		if (sep_pos < rel_len)
			zbx_strncpy_alloc(exp, exp_alloc, exp_offset, p + param_pos + param_len,
					sep_pos - param_pos - param_len + 1);
	}
out:
	zbx_free(pa *4. 检查输入数据是否为空，如果为空则直接退出函数。
 *5. 解析输入的 JSON 数据，并打开 JSON 数组。
 *6. 遍历 JSON 数组中的每个元素，解析为 JSON 对象。
 *7. 处理 JSON 对象中的键值对，进行宏替换。
 *8. 将处理后的键值对添加到新的 JSON 对象中。
 *9. 关闭 JSON 对象，并释放内存。
 *10. 释放原始输入数据。
 *11. 释放 JSON 对象占用的内存。
 *12. 结束函数，并输出日志。
 *13. 返回函数执行结果。 *4. 检查输入数据是否为空，如果为空则直接退出函数。
 *5. 解析输入的 JSON 数据，并打开 JSON 数组。
 *6. 遍历 JSON 数组中的每个元素，解析为 JSON 对象。
 *7. 处理 JSON 对象中的键值对，进行宏替换。
 *8. 将处理后的键值对添加到新的 JSON 对象中。
 *9. 关闭 JSON 对象，并释放内存。
 *10. 释放原始输入数据。
 *11. 释放 JSON 对象占用的内存。
 *12. 结束函数，并输出日志。
 *13. 返回函数执行结果。
 ******************************************************************************/
/* 定义函数名和日志级别 */
const char *__function_name = "substitute_macros_in_json_pairs";
intLOG_LEVEL_DEBUG = 10;

/* 声明结构体和变量 */
struct zbx_json_parse	jp_array, jp_object;
struct zbx_json		json;
const char		*member, *element = NULL;
char			name[MAX_STRING_LEN], value[MAX_STRING_LEN], *p_name = NULL, *p_value = NULL;
int			ret = SUCCEED;

/* 开启日志记录 */
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

/* 检查输入数据是否为空 */
if ('\0' == **data)
	goto exit;

/* 解析 JSON 数据 */
if (SUCCEED != zbx_json_open(*data, &jp_array))
{
	zbx_snprintf(error, maxerrlen, "cannot parse query fields: %s", zbx_json_strerror());
	ret = FAIL;
	goto exit;
}

/* 获取 JSON 数组的第一个元素 */
if (NULL == (element = zbx_json_next(&jp_array, element)))
{
	zbx_strlcpy(error, "cannot parse query fields: array is empty", maxerrlen);
	ret = FAIL;
	goto exit;
}

/* 初始化 JSON 对象 */
zbx_json_initarray(&json, ZBX_JSON_STAT_BUF_LEN);

/* 遍历 JSON 数组中的每个元素，解析并处理 */
do
{
	if (SUCCEED != zbx_json_brackets_open(element, &jp_object) ||
			NULL == (member = zbx_json_pair_next(&jp_object, NULL, name, sizeof(name))) ||
			NULL == zbx_json_decodevalue(member, value, sizeof(value), NULL))
	{
		zbx_snprintf(error, maxerrlen, "cannot parse query fields: %s", zbx_json_strerror());
		ret = FAIL;
		goto clean;
	}

	/* 分配内存并处理宏替换 */
	p_name = zbx_strdup(NULL, name);
	p_value = zbx_strdup(NULL, value);

	substitute_lld_macros(&p_name, jp_row, ZBX_MACRO_ANY, NULL, 0);
	substitute_lld_macros(&p_value, jp_row, ZBX_MACRO_ANY, NULL, 0);

/******************************************************************************
 * *
 *这段代码的主要目的是在XML元素中替换宏。它遍历XML节点，根据节点类型进行相应的处理。对于XML文本节点和CDATA段节点，替换简单宏和LLD宏；对于XML元素节点，替换属性值中的简单宏和LLD宏。整个过程是通过递归调用`substitute_macros_in_xml_elements`函数来完成的。
 ******************************************************************************/
/* 定义一个静态函数，用于在XML元素中替换宏 */
static void substitute_macros_in_xml_elements(const DC_ITEM *item, const struct zbx_json_parse *jp_row,
                                            xmlNode *node)
{
    /* 定义变量，用于存储XML节点的属性值 */
    xmlChar *value;
    xmlAttr *attr;
    char *value_tmp;

    /* 遍历节点 */
    for (; NULL != node; node = node->next)
    {
        /* 根据节点类型进行切换 */
        switch (node->type)
        {
            /* 处理XML文本节点 */
            case XML_TEXT_NODE:
                if (NULL == (value = xmlNodeGetContent(node)))
                    break;

                /* 分配内存存储节点内容，并复制到value_tmp */
                value_tmp = zbx_strdup(NULL, (const char *)value);

                /* 如果有item，则替换简单宏 */
                if (NULL != item)
                {
                    substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &item->host, item, NULL,
                                           NULL, &value_tmp, MACRO_TYPE_HTTP_XML, NULL, 0);
                }
                else
                    /* 否则，替换LLD宏 */
                    substitute_lld_macros(&value_tmp, jp_row, ZBX_MACRO_XML, NULL, 0);

                /* 更新节点内容为替换后的值 */
                xmlNodeSetContent(node, (xmlChar *)value_tmp);

                /* 释放分配的内存 */
                zbx_free(value_tmp);
                xmlFree(value);
                break;

            /* 处理CDATA段节点 */
            case XML_CDATA_SECTION_NODE:
                if (NULL == (value = xmlNodeGetContent(node)))
                    break;

                /* 分配内存存储节点内容，并复制到value_tmp */
                value_tmp = zbx_strdup(NULL, (const char *)value);

                /* 如果有item，则替换简单宏 */
                if (NULL != item)
                {
                    substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &item->host, item, NULL,
                                           NULL, &value_tmp, MACRO_TYPE_HTTP_RAW, NULL, 0);
                }
                else
                    /* 否则，替换LLD宏 */
                    substitute_lld_macros(&value_tmp, jp_row, ZBX_MACRO_ANY, NULL, 0);

                /* 更新节点内容为替换后的值 */
                xmlNodeSetContent(node, (xmlChar *)value_tmp);

                /* 释放分配的内存 */
                zbx_free(value_tmp);
                xmlFree(value);
                break;

            /* 处理XML元素节点 */
            case XML_ELEMENT_NODE:
                for (attr = node->properties; NULL != attr; attr = attr->next)
                {
                    /* 获取属性名和属性值 */
                    if (NULL == attr->name || NULL == (value = xmlGetProp(node, attr->name)))
                        continue;

                    /* 分配内存存储属性值，并复制到value_tmp */
                    value_tmp = zbx_strdup(NULL, (const char *)value);

                    /* 如果有item，则替换简单宏 */
                    if (NULL != item)
                    {
                        substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &item->host,
                                               item, NULL, NULL, &value_tmp, MACRO_TYPE_HTTP_XML,
                                               NULL, 0);
                    }
                    else
                        /* 否则，替换LLD宏 */
                        substitute_lld_macros(&value_tmp, jp_row, ZBX_MACRO_XML, NULL, 0);

                    /* 更新属性值为替换后的值 */
                    xmlSetProp(node, attr->name, (xmlChar *)value_tmp);

                    /* 释放分配的内存 */
                    zbx_free(value_tmp);
                    xmlFree(value);
                }
                break;

            /* 处理其他类型的节点，直接跳过 */
            default:
                break;
        }

        /* 递归处理子节点 */
        substitute_macros_in_xml_elements(item, jp_row, node->children);
    }
}

				zbx_free(value_tmp);
				xmlFree(value);
				break;
			case XML_CDATA_SECTION_NODE:
				if (NULL == (value = xmlNodeGetContent(node)))
					break;

				value_tmp = zbx_strdup(NULL, (const char *)value);

				if (NULL != item)
				{
					substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &item->host, item, NULL,
							NULL, &value_tmp, MACRO_TYPE_HTTP_RAW, NULL, 0);
				}
				else
					substitute_lld_macros(&value_tmp, jp_row, ZBX_MACRO_ANY, NULL, 0);

				xmlNodeSetContent(node, (xmlCha					else
						substitute_lld_macros(&value_tmp, jp_row, ZBX_MACRO_XML, NULL, 0);

					xmlSetProp(node, attr->name, (xmlChar *)value_tmp);

					zbx_free(value_tmp);
					xmlFree(value);
				}
				break;
			default:
				break;
		}

		substitute_macros_in_xml_elements(item, jp_row, node->children);
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: substitute_macros_xml    					else
						substitute_lld_macros(&value_tmp, jp_row, ZBX_MACRO_XML, NULL, 0);

					xmlSetProp(node, attr->name, (xmlChar *)value_tmp);

					zbx_free(value_tmp);
					xmlFree(value);
				}
				break;
			default:
				break;
		}

		substitute_macros_in_xml_elements(item, jp_row, node->children);
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: substitute_macros_xml                                            *
 *                                                                            *
 * Purpose: substitute simple or LLD macros in XML text nodes, attributes of  *
 *          a node or in CDATA section, validate XML                          *
 *                                                                            *
 * Parameters: data   - [IN/OUT] pointer to a buffer that contains XML        *
 *             item   - [IN] item for simple macro substitution               *
 *             jp_row - [IN] discovery data for LLD macro substitution        *
 *             error  - [OUT] reason for XML parsing failure                  *
 *             maxerrlen - [IN] the size of error buffer                      *
 *                                                                            *
 * Return value: SUCCEED or FAIL if XML validation has failed                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是替换XML数据中的宏。该函数接收一个指向XML数据的指针、一个DC_ITEM结构体指针、一个zbx_json_parse结构体指针、一个错误信息字符串指针和一个错误信息最大长度。在函数中，首先检查是否支持XML，如果不支持，则直接返回失败。如果支持XML，则解析输入的XML数据，替换其中的宏，并将替换后的XML数据保存到新的内存块中。最后，释放原始数据内存，返回替换后的XML数据。
 ******************************************************************************/
int substitute_macros_xml(char **data, const DC_ITEM *item, const struct zbx_json_parse *jp_row, char *error, int maxerrlen)
{
    // 定义一个宏替换函数
#ifndef HAVE_LIBXML2
    // 如果未编译支持XML，直接返回失败
    ZBX_UNUSED(data);
    ZBX_UNUSED(item);
    ZBX_UNUSED(jp_row);
    zbx_snprintf(error, maxerrlen, "Support for XML was not compiled in");
    return FAIL;
#else
    // 定义一个函数名
    const char *__function_name = "substitute_macros_xml";
    xmlDoc *doc = NULL;
    xmlErrorPtr pErr = NULL;
    xmlNode *root_element = NULL;
    xmlChar *mem = NULL;
    int size, ret = FAIL;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 解析XML数据
    if (NULL == (doc = xmlReadMemory(*data, strlen(*data), "noname.xml", NULL, 0)))
    {
        // 如果解析出错，记录错误信息
        if (NULL != (pErr = xmlGetLastError()))
            zbx_snprintf(error, maxerrlen, "Cannot parse XML value: %s", pErr->message);
        else
            zbx_snprintf(error, maxerrlen, "Cannot parse XML value");

        goto exit;
    }

    // 获取根元素
    if (NULL == (root_element = xmlDocGetRootElement(doc)))
    {
        // 如果找不到根元素，记录错误信息
        zbx_snprintf(error, maxerrlen, "Cannot parse XML root");
        goto clean;
    }

    // 替换XML元素中的宏
    substitute_macros_in_xml_elements(item, jp_row, root_element);
    xmlDocDumpMemory(doc, &mem, &size);

    // 保存替换后的XML数据
    if (NULL == mem)
    {
        // 如果保存出错，记录错误信息
        if (NULL != (pErr = xmlGetLastError()))
            zbx_snprintf(error, maxerrlen, "Cannot save XML: %s", pErr->message);
        else
            zbx_snprintf(error, maxerrlen, "Cannot save XML");

        goto clean;
    }

    // 释放原始数据内存
    zbx_free(*data);
    *data = zbx_malloc(NULL, size + 1);
    memcpy(*data, (const char *)mem, size + 1);
    xmlFree(mem);
    ret = SUCCEED;
clean:
    // 释放文档内存
    xmlFreeDoc(doc);
exit:
    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回结果
    return ret;
#endif
}


#ifdef HAVE_LIBXML2
/******************************************************************************
 *                                                                            *
 * Function: libxml_handle_error                                              *
 *                                                                            *
 * Purpose: libxml2 callback function for error handle                        *
 *                                                                            *
 * Parameters: user_data - [IN/OUT] the user context                          *
 *             err       - [IN] the libxml2 error message                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理XML库中的错误信息。当XML库发生错误时，会将错误信息存储在`err_ctx->buf`字符串中，并将错误信息的各个部分（如err->str1，err->str2，err->str3）拼接到该字符串中。这个过程是通过调用`zbx_strlcat`函数来实现的。最后，将处理后的错误信息存储在`err_ctx`指向的结构体中，以便后续使用或输出。
 ******************************************************************************/
// 定义一个静态函数，用于处理XML库的错误信息
static void	libxml_handle_error(void *user_data, xmlErrorPtr err)
{
	// 定义一个指向错误上下文的指针
	zbx_libxml_error_t	*err_ctx;

	// 检查user_data是否为空，如果为空则直接返回，不需要处理错误信息
	if (NULL == user_data)
		return;

	// 将user_data转换为zbx_libxml_error_t类型的指针
	err_ctx = (zbx_libxml_error_t *)user_data;

	// 拼接错误信息到err_ctx->buf字符串中
	zbx_strlcat(err_ctx->buf, err->message, err_ctx->len);

	// 如果err中包含字符串err->str1，则将其拼接到err_ctx->buf中
	if (NULL != err->str1)
		zbx_strlcat(err_ctx->buf, err->str1, err_ctx->len);

	// 如果err中包含字符串err->str2，则将其拼接到err_ctx->buf中
	if (NULL != err->str2)
		zbx_strlcat(err_ctx->buf, err->str2, err_ctx->len);

	// 如果err中包含字符串err->str3，则将其拼接到err_ctx->buf中
	if (NULL != err->str3)
		zbx_strlcat(err_ctx->buf, err->str3, err_ctx->len);
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: xml_xpath_check                                                  *
 *                                                                            *
 * Purpose: validate xpath string                                             *
 *                                                                            *
 * Parameters: xpath  - [IN] the xp ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的XPath表达式是否有效。首先，根据库是否安装来判断是否支持XPath表达式的检查，如果不支持，则直接返回失败。如果支持，则创建一个新的xmlXPathContext对象，用于编译XPath表达式。接着，编译给定的XPath表达式，如果编译失败，则释放资� ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的XPath表达式是否有效。首先，根据库是否安装来判断是否支持XPath表达式的检查，如果不支持，则直接返回失败。如果支持，则创建一个新的xmlXPathContext对象，用于编译XPath表达式。接着，编译给定的XPath表达式，如果编译失败，则释放资源并返回失败。如果编译成功，则释放资源并返回成功。
 ******************************************************************************/
// 定义一个C语言函数，用于检查给定的XPath表达式是否有效
int xml_xpath_check(const char *xpath, char *error, size_t errlen)
{
    // 如果没有安装libxml2库，直接返回失败
#ifndef HAVE_LIBXML2
    ZBX_UNUSED(xpath);
    ZBX_UNUSED(error);
    ZBX_UNUSED(errlen);
    return FAIL;
#else
    // 定义一个结构体，用于存储错误信息
    zbx_libxml_error_t err;
    // 创建一个新的xmlXPathContext对象
    xmlXPathContextPtr ctx;
    // 创建一个新的xmlXPathCompExpr对象
    xmlXPathCompExprPtr p;

    // 初始化错误结构体，将错误信息存储在error字符串中
    err.buf = error;
    err.len = errlen;

    // 创建一个新的xmlXPathContext对象，用于编译XPath表达式
    ctx = xmlXPathNewContext(NULL);
    // 设置结构化错误处理函数，用于处理xml库中的错误
    xmlSetStructuredErrorFunc(&err, &libxml_handle_error);

    // 编译给定的XPath表达式
    p = xmlXPathCtxtCompile(ctx, (xmlChar *)xpath);
    // 恢复默认的错误处理函数
    xmlSetStructuredErrorFunc(NULL, NULL);

    // 如果编译失败，即p为NULL，释放资源并返回失败
    if (NULL == p)
    {
        xmlXPathFreeContext(ctx);
        return FAIL;
    }

    // 释放编译后的XPath表达式
    xmlXPathFreeCompExpr(p);
    // 释放xmlXPathContext对象
    xmlXPathFreeContext(ctx);

    // 编译成功，返回成功
    return SUCCEED;
#endif
}

