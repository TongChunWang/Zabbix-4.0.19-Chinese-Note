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
#include "dbupgrade.h"
#include "log.h"

/*
 * 4.0 maintenance database patches
 */

#ifndef HAVE_SQLITE3

extern unsigned char program_type;

static int	DBpatch_4000000(void)
{
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: str_rename_macro                                                 *
 *                                                                            *
 * Purpose: rename macros in the string                                       *
 *                                                                            *
 * Parameters: in        - [IN] the input string                              *
 *             oldmacro  - [IN] the macro to rename                           *
 *             newmacro  - [IN] the new macro name                            *
 *             out       - [IN/OUT] the string with renamed macros            *
 *             out_alloc - [IN/OUT] the output buffer size                    *
 *                                                                            *
 * Return value: SUCCEED - macros were found and renamed                      *
 *               FAIL    - no target macros were found                        *
 *                                                                            *
 * Comments: If the oldmacro is found in input string then all occurrences of *
 *           it are replaced with the new macro in the output string.         *
 *           Otherwise the output string is not changed.                      *
 *                                                                            *
 ******************************************************************************/
static int	str_rename_macro(const char *in, const char *oldmacro, const char *newmacro, char **out,
		size_t *out_alloc)
{
	zbx_token_t	token;
	int		pos = 0, ret = FAIL;
	size_t		out_offset = 0, newmacro_len;

	newmacro_len = strlen(newmacro);
	zbx_strcpy_alloc(out, out_alloc, &out_offset, in);
	out_offset++;

	for (; SUCCEED == zbx_token_find(*out, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		switch (token.type)
		{
			case ZBX_TOKEN_MACRO:
				pos = token.loc.r;
				if (0 == strncmp(*out + token.loc.l, oldmacro, token.loc.r - token.loc.l + 1))
				{
					pos += zbx_replace_mem_dyn(out, out_alloc, &out_offset, token.loc.l,
							token.loc.r - token.loc.l + 1, newmacro, newmacro_len);
					ret = SUCCEED;
				}
				break;

			case ZBX_TOKEN_USER_MACRO:
			case ZBX_TOKEN_SIMPLE_MACRO:
				pos = token.loc.r;
				break;
		}
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: db_rename_macro                                                  *
 *                                                                            *
 * Purpose: rename macro in the specified database fields                     *
 *                                                                            *
 * Parameters: result     - [IN] database query with fields to replace. First *
 *                               field is table id field, following with      *
 *                               the target fields listed in fields parameter *
 *             table      - [IN] the target table name                        *
 *             pkey       - [IN] the primary key field name                   *
 *             fields     - [IN] the table fields to check for macros and     *
 *                               rename if found                              *
 *             fields_num - [IN] the number of fields to check                *
 *             oldmacro   - [IN] the macro to rename                          *
 *             newmacro   - [IN] the new macro name                           *
 *                                                                            *
 * Return value: SUCCEED  - macros were renamed successfully                  *
 *               FAIL     - database error occurred                           *
 *                                                                            *
 ******************************************************************************/
static int	db_rename_macro(DB_RESULT result, const char *table, const char *pkey, const char **fields,
		int fields_num, const char *oldmacro, const char *newmacro)
{
	DB_ROW		row;
	char		*sql = 0, *field = NULL, *field_esc;
	size_t		sql_alloc = 4096, sql_offset = 0, field_alloc = 0, old_offset;
	int		i, ret = SUCCEED;

	sql = zbx_malloc(NULL, sql_alloc);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	while (NULL != (row = DBfetch(result)))
	{
		old_offset = sql_offset;

		for (i = 0; i < fields_num; i++)
		{
			if (SUCCEED == str_rename_macro(row[i + 1], oldmacro, newmacro, &field, &field_alloc))
			{
				if (old_offset == sql_offset)
					zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set ", table);
				else
					zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ',');

				field_esc = DBdyn_escape_string(field);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s='%s'", fields[i], field_esc);
				zbx_free(field_esc);
			}
		}

		if (old_offset != sql_offset)
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where %s=%s;\n", pkey, row[0]);
			if (SUCCEED != (ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset)))
				goto out;
		}
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset && ZBX_DB_OK > DBexecute("%s", sql))
		ret = FAIL;
out:
	zbx_free(field);
	zbx_free(sql);

	return ret;
}

static int	DBpatch_4000001(void)
{
	DB_RESULT	result;
	int		ret;
	const char	*fields[] = {"def_shortdata", "def_longdata", "r_shortdata", "r_longdata", "ack_shortdata",
				"ack_longdata"};

	/* 0 - EVENT_SOURCE_TRIGGERS */
	result = DBselect("select actionid,def_shortdata,def_longdata,r_shortdata,r_longdata,ack_shortdata,"
			"ack_longdata from actions where eventsource=0");

	ret = db_rename_macro(result, "actions", "actionid", fields, ARRSIZE(fields), "{TRIGGER.NAME}",
			"{EVENT.NAME}");

	DBfree_result(result);

	return ret;
}

static int	DBpatch_4000002(void)
{
	DB_RESULT	result;
	int		ret;
	const char	*fields[] = {"subject", "message"};

	/* 0 - EVENT_SOURCE_TRIGGERS */
	result = DBselect("select om.operationid,om.subject,om.message"
			" from opmessage om,operations o,actions a"
			" where om.operationid=o.operationid"
				" and o.actionid=a.actionid"
				" and a.eventsource=0");

	ret = db_rename_macro(result, "opmessage", "operationid", fields, ARRSIZE(fields), "{TRIGGER.NAME}",
			"{EVENT.NAME}");

	DBfree_result(result);

	return ret;
}

static int	DBpatch_4000003(void)
{
	DB_RESULT	result;
	int		ret;
	const char	*fields[] = {"command"};

	/* 0 - EVENT_SOURCE_TRIGGERS */
	result = DBselect("select oc.operationid,oc.command"
			" from opcommand oc,operations o,actions a"
			" where oc.operationid=o.operationid"
				" and o.actionid=a.actionid"
				" and a.eventsource=0");

	ret = db_rename_macro(result, "opcommand", "operationid", fields, ARRSIZE(fields), "{TRIGGER.NAME}",
			"{EVENT.NAME}");

	DBfree_result(result);

	return ret;
}

static int	DBpatch_4000004(void)
{
	int		i;
	const char	*values[] = {
			"alarm_ok",
			"no_sound",
			"alarm_information",
			"alarm_warning",
			"alarm_average",
			"alarm_high",
			"alarm_disaster"
		};

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	for (i = 0; i < (int)ARRSIZE(values); i++)
	{
		if (ZBX_DB_OK > DBexecute(
				"update profiles"
				" set value_str='%s.mp3'"
				" where value_str='%s.wav'"
					" and idx='web.messages'", values[i], values[i]))
		{
			return FAIL;
		}
	}

	return SUCCEED;
}

static int	DBpatch_4000005(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		time_period_id, every;
	int			invalidate = 0;
	const ZBX_TABLE		*timeperiods;
	const ZBX_FIELD		*field;

	if (NULL != (timeperiods = DBget_table("timeperiods")) &&
			NULL != (field = DBget_field(timeperiods, "every")))
	{
		ZBX_STR2UINT64(every, field->default_value);
	}
	else
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	result = DBselect("select timeperiodid from timeperiods where every=0");

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(time_period_id, row[0]);

		zabbix_log(LOG_LEVEL_WARNING, "Invalid maintenance time period found: "ZBX_FS_UI64
				", changing \"every\" to "ZBX_FS_UI64, time_period_id, every);
		invalidate = 1;
	}

	DBfree_result(result);

	if (0 != invalidate &&
			ZBX_DB_OK > DBexecute("update timeperiods set every=1 where timeperiodid!=0 and every=0"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_4000006(void)
{
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from profiles where idx='web.screens.graphid'"))
		return FAIL;

	return SUCCEED;
}

#endif

DBPATCH_START(4000)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(4000000, 0, 1)
DBPATCH_ADD(4000001, 0, 0)
DBPATCH_ADD(4000002, 0, 0)
DBPATCH_ADD(4000003, 0, 0)
DBPATCH_ADD(4000004, 0, 0)
DBPATCH_ADD(4000005, 0, 0)
DBPATCH_ADD(4000006, 0, 0)

DBPATCH_END()
