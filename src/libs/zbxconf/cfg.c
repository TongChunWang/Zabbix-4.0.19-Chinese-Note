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
#include "cfg.h"
#include "log.h"

extern unsigned char	program_type;

char	*CONFIG_FILE		= NULL;

char	*CONFIG_LOG_TYPE_STR	= NULL;
int	CONFIG_LOG_TYPE		= LOG_TYPE_UNDEFINED;
char	*CONFIG_LOG_FILE	= NULL;
int	CONFIG_LOG_FILE_SIZE	= 1;
int	CONFIG_ALLOW_ROOT	= 0;
int	CONFIG_TIMEOUT		= 3;

static int	__parse_cfg_file(const char *cfg_file, struct cfg_line *cfg, int level, int optional, int strict);

/******************************************************************************
 *                                                                            *
 * Function: match_glob                                                       *
 *                                                                            *
 * Purpose: see whether a file (e.g., "parameter.conf")                       *
 *          matches a pattern (e.g., "p*.conf")                               *
 *                                                                            *
 * Return value: SUCCEED - file matches a pattern                             *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	match_glob(const char *file, const char *pattern)
{
	const char	*f, *g, *p, *q;

	f = file;
	p = pattern;

	while (1)
	{
		/* corner case */

		if ('\0' == *p)
			return '\0' == *f ? SUCCEED : FAIL;

		/* find a set of literal characters */

		while ('*' == *p)
			p++;

		for (q = p; '\0' != *q && '*' != *q; q++)
			;

		/* if literal characters are at the beginning... */

		if (pattern == p)
		{
#ifdef _WINDOWS
			if (0 != zbx_strncasecmp(f, p, q - p))
#else
			if (0 != strncmp(f, p, q - p))
#endif
				return FAIL;

			f += q - p;
			p = q;

			continue;
		}

		/* if literal characters are at the end... */

		if ('\0' == *q)
		{
			for (g = f; '\0' != *g; g++)
				;

			if (g - f < q - p)
				return FAIL;
#ifdef _WINDOWS
			return 0 == strcasecmp(g - (q - p), p) ? SUCCEED : FAIL;
#else
			return 0 == strcmp(g - (q - p), p) ? SUCCEED : FAIL;
#endif
		}

		/* if literal characters are in the middle... */

		while (1)
		{
			if ('\0' == *f)
				return FAIL;
#ifdef _WINDOWS
			if (0 == zbx_strncasecmp(f, p, q - p))
#else
			if (0 == strncmp(f, p, q - p))
#endif
			{
				f += q - p;
				p = q;

				break;
			}

			f++;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: parse_glob                                                       *
 *                                                                            *
 * Purpose: parse a glob like "/usr/local/etc/zabbix_agentd.conf.d/p*.conf"   *
 *          into "/usr/local/etc/zabbix_agentd.conf.d" and "p*.conf" parts    *
 *                                                                            *
 * Parameters: glob    - [IN] glob as specified in Include directive          *
 *             path    - [OUT] parsed path, either directory or file          *
 *             pattern - [OUT] parsed pattern, if path is directory           *
 *                                                                            *
 * Return value: SUCCEED - glob is valid and was parsed successfully          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	parse_glob(const char *glob, char **path, char **pattern)
{
	const char	*p;

	if (NULL == (p = strchr(glob, '*')))
	{
		*path = zbx_strdup(NULL, glob);
		*pattern = NULL;

		goto trim;
	}

	if (NULL != strchr(p + 1, PATH_SEPARATOR))
	{
		zbx_error("%s: glob pattern should be the last component of the path", glob);
		return FAIL;
	}

	do
	{
		if (glob == p)
		{
			zbx_error("%s: path should be absolute", glob);
			return FAIL;
		}

		p--;
	}
	while (PATH_SEPARATOR != *p);

	// 复制路径字符串，并去掉尾部的分隔符；
	*path = zbx_strdup(NULL, glob);
	(*path)[p - glob] = '\0';

	// 复制匹配模式字符串，去掉开头的分隔符；
	*pattern = zbx_strdup(NULL, p + 1);
trim:
	// 根据系统类型（Windows 或 Unix）调整路径和模式字符串；
#ifdef _WINDOWS
	if (0 != zbx_rtrim(*path, "\\") && NULL == *pattern)
		*pattern = zbx_strdup(NULL, "*");			/* make sure path is a directory */

	if (':' == (*path)[1] && '\0' == (*path)[2] && '\\' == glob[2])	/* retain backslash for "C:\" */
	{
		(*path)[2] = '\\';
		(*path)[3] = '\0';
	}
#else
	if (0 != zbx_rtrim(*path, "/") && NULL == *pattern)
		*pattern = zbx_strdup(NULL, "*");			/* make sure path is a directory */

	if ('\0' == (*path)[0] && '/' == glob[0])			/* retain forward slash for "/" */
	{
		(*path)[0] = '/';
		(*path)[1] = '\0';
	}
#endif

	// 函数执行成功，返回 SUCCEED；
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_cfg_dir                                                    *
 *                                                                            *
 * Purpose: parse directory with configuration files                          *
 *                                                                            *
 * Parameters: path    - full path to directory                               *
 *             pattern - pattern that files in the directory should match     *
 *             cfg     - pointer to configuration parameter structure         *
 *             level   - a level of included file                             *
 *             strict  - treat unknown parameters as error                    *
 *                                                                            *
 * Return value: SUCCEED - parsed successfully                                *
 *               FAIL - error processing directory                            *
 *                                                                            *
 ******************************************************************************/
#ifdef _WINDOWS
static int	parse_cfg_dir(const char *path, const char *pattern, struct cfg_line *cfg, int level, int strict)
{
	// 定义一些变量，用于存放查找文件的信息
	WIN32_FIND_DATAW	find_file_data;
	HANDLE			h_find;
	char 			*find_path = NULL, *file = NULL, *file_name;
	wchar_t			*wfind_path = NULL;
	int			ret = FAIL;

	// 构造查找文件的路径，使用通配符匹配所有文件
	find_path = zbx_dsprintf(find_path, "%s\\*", path);
	// 将查找路径从UTF-8编码转换为Unicode编码
	wfind_path = zbx_utf8_to_unicode(find_path);

	// 调用FindFirstFileW函数查找第一个文件，如果查找失败，则退出函数
	if (INVALID_HANDLE_VALUE == (h_find = FindFirstFileW(wfind_path, &find_file_data)))
		goto clean;

	// 使用循环遍历查找到的文件
	while (0 != FindNextFileW(h_find, &find_file_data))
	{
		// 如果文件是目录，则跳过
		if (0 != (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		// 将文件名从Unicode编码转换为UTF-8编码
		file_name = zbx_unicode_to_utf8(find_file_data.cFileName);

		// 如果提供了匹配模式，并且当前文件名与模式不匹配，则跳过该文件
		if (NULL != pattern && SUCCEED != match_glob(file_name, pattern))
		{
			zbx_free(file_name);
			continue;
		}

		// 构造当前文件的路径
		file = zbx_dsprintf(file, "%s\\%s", path, file_name);

		zbx_free(file_name);
		// 解析当前文件，并将结果添加到cfg结构体中
		if (SUCCEED != __parse_cfg_file(file, cfg, level, ZBX_CFG_FILE_REQUIRED, strict))
			goto close;
	}

	// 标记解析成功
	ret = SUCCEED;

close:
	// 释放文件名和查找路径的空间
	zbx_free(file);
	FindClose(h_find);

clean:
	// 释放Unicode编码的查找路径的空间
	zbx_free(wfind_path);
	// 释放UTF-8编码的查找路径的空间
	zbx_free(find_path);

	// 返回解析结果
	return ret;
}

#else
static int	parse_cfg_dir(const char *path, const char *pattern, struct cfg_line *cfg, int level, int strict)
{
	DIR		*dir;
	struct dirent	*d;
	zbx_stat_t	sb;
	char		*file = NULL;
	int		ret = FAIL;

	if (NULL == (dir = opendir(path)))
	{
		zbx_error("%s: %s", path, zbx_strerror(errno));
		goto out;
	}

	while (NULL != (d = readdir(dir)))
	{
		file = zbx_dsprintf(file, "%s/%s", path, d->d_name);

		if (0 != zbx_stat(file, &sb) || 0 == S_ISREG(sb.st_mode))
			continue;

		if (NULL != pattern && SUCCEED != match_glob(d->d_name, pattern))
			continue;

		if (SUCCEED != __parse_cfg_file(file, cfg, level, ZBX_CFG_FILE_REQUIRED, strict))
			goto close;
	}

	ret = SUCCEED;
close:
	if (0 != closedir(dir))
	{
		zbx_error("%s: %s", path, zbx_strerror(errno));
		ret = FAIL;
	}

	zbx_free(file);
out:
	return ret;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: parse_cfg_object                                                 *
 *                                                                            *
 * Purpose: parse "Include=..." line in configuration file                    *
 *                                                                            *
 * Parameters: cfg_file - full name of config file                            *
 *             cfg      - pointer to configuration parameter structure        *
 *             level    - a level of included file                            *
 *             strict   - treat unknown parameters as error                   *
 *                                                                            *
 * Return value: SUCCEED - parsed successfully                                *
 *               FAIL - error processing object                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个或多个Cfg文件，并根据给定的级别和严格模式进行处理。在这个过程中，首先解析glob模式，获取路径和模式。然后检查路径是否为一个目录，根据不同的情况对cfg文件进行解析或处理错误。最后，释放路径和模式占用的内存，并返回解析结果。
 ******************************************************************************/
// 定义一个静态函数，用于解析cfg文件
static int	parse_cfg_object(const char *cfg_file, struct cfg_line *cfg, int level, int strict)
{
	// 定义一些变量
	int		ret = FAIL;
	char		*path = NULL, *pattern = NULL;
	zbx_stat_t	sb;

	// 解析glob模式，获取路径和模式
	if (SUCCEED != parse_glob(cfg_file, &path, &pattern))
		goto clean;

	if (0 != zbx_stat(path, &sb))
	{
		zbx_error("%s: %s", path, zbx_strerror(errno));
		goto clean;
	}

	if (0 == S_ISDIR(sb.st_mode))
	{
		if (NULL == pattern)
		{
			ret = __parse_cfg_file(path, cfg, level, ZBX_CFG_FILE_REQUIRED, strict);
			goto clean;
		}

		zbx_error("%s: base path is not a directory", cfg_file);
		goto clean;
	}

	ret = parse_cfg_dir(path, pattern, cfg, level, strict);
clean:
	zbx_free(pattern);
	zbx_free(path);

	return ret;
}
/******************************************************************************
 *                                                                            *
 * Function: parse_cfg_file                                                   *
 *                                                                            *
 * Purpose: parse configuration file                                          *
 *                                                                            *
 * Parameters: cfg_file - full name of config file                            *
 *             cfg      - pointer to configuration parameter structure        *
 *             level    - a level of included file                            *
 *             optional - do not treat missing configuration file as error    *
 *             strict   - treat unknown parameters as error                   *
 *                                                                            *
 * Return value: SUCCEED - parsed successfully                                *
 *               FAIL - error processing config file                          *
 *                                                                            *
 * Author: Alexei Vladishev, Eugene Grigorjev                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个函数的主要目的是解析一个名为`cfg_file`的配置文件，并将解析后的结果存储在`cfg`结构体数组中。函数接受五个参数：
 *
 *1. `cfg_file`：要解析的配置文件路径。
 *2. `cfg`：存储解析结果的结构体数组。
 *3. `level`：当前包含的级别，用于防止递归调用过多。
 *4. `optional`：表示是否允许省略配置文件，如果为`0`，则必须提供配置文件。
 *5. `strict`：表示是否严格检查配置文件中的参数，如果为`1`，则严格检查。
 *
 *在函数内部，首先定义了一些常量，如最大包含级别、配置文件字符串处理等。然后打开配置文件并逐行读取，提取行首尾空白字符和分号，只支持UTF-8字符。接下来，根据配置项的类型解析值，并检查配置项的有效性。最后，检查必需的配置项是否缺失，如果缺失则输出错误信息。
 *
 *整个函数的输出结果是成功（SUCCEED）或失败（FAIL），失败情况下会输出错误信息并退出程序。
 ******************************************************************************/
static int __parse_cfg_file(const char *cfg_file, struct cfg_line *cfg, int level, int optional, int strict)
{
#define ZBX_MAX_INCLUDE_LEVEL	10

#define ZBX_CFG_LTRIM_CHARS	"\t "
#define ZBX_CFG_RTRIM_CHARS	ZBX_CFG_LTRIM_CHARS "\r\n"
    // 定义一些常量，如最大包含级别、配置文件字符串处理等

    // 打开配置文件
    FILE *file;
    int i, lineno, param_valid;
    char line[MAX_STRING_LEN + 3], *parameter, *value;
    zbx_uint64_t var;
    size_t len;
    // 处理Windows平台特殊字符
#ifdef _WINDOWS
    wchar_t *wcfg_file;
#endif

    // 防止递归调用过多，限制最大包含级别
    if (++level > ZBX_MAX_INCLUDE_LEVEL)
    {
        zbx_error("Recursion detected! Skipped processing of '%s'.", cfg_file);
        return FAIL;
    }

    // 打开配置文件
    if (NULL != cfg_file)
    {
#ifdef _WINDOWS
        wcfg_file = zbx_utf8_to_unicode(cfg_file);
        file = _wfopen(wcfg_file, L"r");
        zbx_free(wcfg_file);

        if (NULL == file)
            goto cannot_open;
#else
        if (NULL == (file = fopen(cfg_file, "r")))
            goto cannot_open;
#endif

        // 逐行读取配置文件
        for (lineno = 1; NULL != fgets(line, sizeof(line), file); lineno++)
        {
            // 检查行长度是否超过限制
            len = strlen(line);
			if (MAX_STRING_LEN < len && NULL == strchr("\r\n", line[MAX_STRING_LEN]))
				goto line_too_long;

            // 处理行首尾空白字符
            zbx_ltrim(line, ZBX_CFG_LTRIM_CHARS);
            zbx_rtrim(line, ZBX_CFG_RTRIM_CHARS);

            // 跳过空行和以#开头的行
            if ('#' == *line || '\0' == *line)
                continue;

            // 只支持UTF-8字符
            if (SUCCEED != zbx_is_utf8(line))
                goto non_utf8;

            // 提取参数和值
            parameter = line;
            if (NULL == (value = strchr(line, '=')))
                goto non_key_value;

            *value++ = '\0';

            // 处理配置参数
            zbx_rtrim(parameter, ZBX_CFG_RTRIM_CHARS);
            zbx_ltrim(value, ZBX_CFG_LTRIM_CHARS);

            zabbix_log(LOG_LEVEL_DEBUG, "cfg: para: [%s] val [%s]", parameter, value);

            // 检查是否为"Include"
            if (0 == strcmp(parameter, "Include"))
            {
                // 解析包含的配置对象
                if (FAIL == parse_cfg_object(value, cfg, level, strict))
                {
                    fclose(file);
                    goto error;
                }

                continue;
            }

            // 检查参数有效性
            param_valid = 0;

            // 遍历配置项
            for (i = 0; NULL != cfg[i].parameter; i++)
            {
                // 跳过不符合的配置项
                if (0 != strcmp(cfg[i].parameter, parameter))
                    continue;

                param_valid = 1;

				zabbix_log(LOG_LEVEL_DEBUG, "accepted configuration parameter: '%s' = '%s'",
						parameter, value);

				switch (cfg[i].type)
				{
					case TYPE_INT:
						if (FAIL == str2uint64(value, "KMGT", &var))
							goto incorrect_config;

                        if (cfg[i].min > var || (0 != cfg[i].max && var > cfg[i].max))
                            goto incorrect_config;

                        *((int *)cfg[i].variable) = (int)var;
                        break;
					case TYPE_STRING_LIST:
                        zbx_trim_str_list(value, ',');
                        ZBX_FALLTHROUGH;
					case TYPE_STRING:
						*((char **)cfg[i].variable) =
								zbx_strdup(*((char **)cfg[i].variable), value);
						break;
                    case TYPE_MULTISTRING:
                        zbx_strarr_add((char ***)cfg[i].variable, value);
                        break;
                    case TYPE_UINT64:
                        if (FAIL == str2uint64(value, "KMGT", &var))
                            goto incorrect_config;

                        if (cfg[i].min > var || (0 != cfg[i].max && var > cfg[i].max))
                            goto incorrect_config;

                        *((zbx_uint64_t *)cfg[i].variable) = var;
                        break;
                    default:
                        assert(0);
                }
            }

            // 未找到有效配置项
            if (0 == param_valid && ZBX_CFG_STRICT == strict)
                goto unknown_parameter;
        }
        fclose(file);
    }

    // 非强制性配置文件检查
    if (1 != level)
        return SUCCEED;

    // 检查必需的配置项
    for (i = 0; NULL != cfg[i].parameter; i++)
    {
        // 跳过非必需配置项
        if (PARM_MAND != cfg[i].mandatory)
            continue;

		switch (cfg[i].type)
		{
			case TYPE_INT:
				if (0 == *((int *)cfg[i].variable))
					goto missing_mandatory;
				break;
			case TYPE_STRING:
			case TYPE_STRING_LIST:
				if (NULL == (*(char **)cfg[i].variable))
					goto missing_mandatory;
				break;
			default:
				assert(0);
		}
	}

	return SUCCEED;
cannot_open:
	if (ZBX_CFG_FILE_REQUIRED != optional)
		return SUCCEED;
	zbx_error("cannot open config file \"%s\": %s", cfg_file, zbx_strerror(errno));
	goto error;
line_too_long:
	fclose(file);
	zbx_error("line %d exceeds %d byte length limit in config file \"%s\"", lineno, MAX_STRING_LEN, cfg_file);
	goto error;
non_utf8:
	fclose(file);
	zbx_error("non-UTF-8 character at line %d \"%s\" in config file \"%s\"", lineno, line, cfg_file);
	goto error;
non_key_value:
	fclose(file);
	zbx_error("invalid entry \"%s\" (not following \"parameter=value\" notation) in config file \"%s\", line %d",
			line, cfg_file, lineno);
	goto error;
incorrect_config:
	fclose(file);
	zbx_error("wrong value of \"%s\" in config file \"%s\", line %d", cfg[i].parameter, cfg_file, lineno);
	goto error;
unknown_parameter:
	fclose(file);
	zbx_error("unknown parameter \"%s\" in config file \"%s\", line %d", parameter, cfg_file, lineno);
	goto error;
missing_mandatory:
	zbx_error("missing mandatory parameter \"%s\" in config file \"%s\"", cfg[i].parameter, cfg_file);
error:
	exit(EXIT_FAILURE);
}

int	parse_cfg_file(const char *cfg_file, struct cfg_line *cfg, int optional, int strict)
{
	return __parse_cfg_file(cfg_file, cfg, 0, optional, strict);
}
/******************************************************************************
 * *
 *这块代码的主要目的是检查配置文件中的参数值是否符合要求。具体来说，函数`check_cfg_feature_int`接收三个参数：`parameter`表示配置文件中的参数名，`value`表示该参数的值，`feature`表示一个特征。函数首先判断`value`是否不为0，如果不为0，则输出错误信息，指出该配置参数不能使用，并返回失败状态码。如果条件成立，即`value`为0，返回成功状态码。
 ******************************************************************************/
// 定义一个函数，用于检查配置文件中的参数值是否符合要求
int check_cfg_feature_int(const char *parameter, int value, const char *feature)
{
    // 如果参数值不为0
    if (0 != value)
    {
        // 输出错误信息，指出该配置参数不能使用
        zbx_error("\"%s\" configuration parameter cannot be used: Zabbix %s was compiled without %s",
                    parameter, get_program_type_string(program_type), feature);
        // 返回失败状态码
        return FAIL;
    }

    // 如果条件成立，返回成功状态码
    return SUCCEED;
}

/******************************************************************************
 * *
 *这块代码的主要目的是检查传入的配置文件参数（parameter 和 value）以及特征（feature），如果值不为空，则输出错误信息并返回失败状态码；如果值为空，则返回成功状态码。具体来说，该函数用于确保配置文件中的参数合法，并在发现问题时给出提示。
 ******************************************************************************/
// 定义一个函数，用于检查配置文件中的参数是否合法
int check_cfg_feature_str(const char *parameter, const char *value, const char *feature)
{
    // 判断参数 value 是否为空，如果不为空，则继续执行后续代码
    if (NULL != value)
    {
        // 输出错误信息，说明配置参数不能使用，原因是 Zabbix 编译时未包含 feature 功能
        zbx_error("\"%s\" configuration parameter cannot be used: Zabbix %s was compiled without %s",
                 parameter, get_program_type_string(program_type), feature);
        // 返回失败状态码
        return FAIL;
    }

    // 如果 value 为空，则返回成功状态码
    return SUCCEED;
}

