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

#include "sysinfo.h"
#include "zbxalgo.h"
#include "zbxexec.h"
#include "cfg.h"
#include "software.h"
#include "zbxregexp.h"
#include "log.h"

#ifdef HAVE_SYS_UTSNAME_H
#       include <sys/utsname.h>
#endif
/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的架构信息（机器名），并将结果存储在 `AGENT_RESULT` 结构体的字符串字段中。如果获取系统架构信息失败，则设置错误信息并返回失败状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统架构信息
int SYSTEM_SW_ARCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个结构体 utsname，用于存储系统架构信息
    struct utsname	name;

    // 忽略传入的 request 参数，不需要使用
    ZBX_UNUSED(request);

    // 调用 uname 函数获取系统架构信息，并将结果存储在 name 结构体中
    if (-1 == uname(&name))
    {
        // 如果获取系统架构信息失败，设置错误信息并返回失败状态
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 将获取到的系统架构信息（机器名）复制到 result 指向的内存空间
    SET_STR_RESULT(result, zbx_strdup(NULL, name.machine));

    // 返回成功状态
    return SYSINFO_RET_OK;
}


int     SYSTEM_SW_OS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*type, line[MAX_STRING_LEN], tmp_line[MAX_STRING_LEN];
	int	ret = SYSINFO_RET_FAIL, line_read = FAIL;
	FILE	*f = NULL;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return ret;
	}

	type = get_rparam(request, 0);

	if (NULL == type || '\0' == *type || 0 == strcmp(type, "full"))
	{
		if (NULL == (f = fopen(SW_OS_FULL, "r")))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open " SW_OS_FULL ": %s",
					zbx_strerror(errno)));
			return ret;
		}
	}
	else if (0 == strcmp(type, "short"))
	{
		if (NULL == (f = fopen(SW_OS_SHORT, "r")))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open " SW_OS_SHORT ": %s",
					zbx_strerror(errno)));
			return ret;
		}
	}
	else if (0 == strcmp(type, "name"))
	{
		/* firstly need to check option PRETTY_NAME in /etc/os-release */
		/* if cannot find it, get value from /etc/issue.net            */
		if (NULL != (f = fopen(SW_OS_NAME_RELEASE, "r")))
		{
			while (NULL != fgets(tmp_line, sizeof(tmp_line), f))
			{
				if (0 != strncmp(tmp_line, SW_OS_OPTION_PRETTY_NAME,
						ZBX_CONST_STRLEN(SW_OS_OPTION_PRETTY_NAME)))
					continue;

				if (1 == sscanf(tmp_line, SW_OS_OPTION_PRETTY_NAME "=\"%[^\"]", line))
				{
					line_read = SUCCEED;
					break;
				}
			}
			zbx_fclose(f);
		}

		if (FAIL == line_read && NULL == (f = fopen(SW_OS_NAME, "r")))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open " SW_OS_NAME ": %s",
					zbx_strerror(errno)));
			return ret;
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return ret;
	}

	if (SUCCEED == line_read || NULL != fgets(line, sizeof(line), f))
	{
		ret = SYSINFO_RET_OK;
		zbx_rtrim(line, ZBX_WHITESPACE);
		SET_STR_RESULT(result, zbx_strdup(NULL, line));
	}
	else
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read from file."));

	zbx_fclose(f);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是解析一条日志记录（line），从中提取软件包名（存储在 package 变量中）和操作符（存储在 tmp 变量中）。如果解析成功，返回 SUCCEED；如果解析失败或解析到的操作符不是 \"install\"，返回 FAIL。
 ******************************************************************************/
// 定义一个名为 dpkg_parser 的静态函数，该函数接收三个参数：
// 参数1：一个指向字符串的指针 line，表示输入的一条日志记录；
// 参数2：一个字符指针 package，用于存储解析后的软件包名；
// 参数3：一个整型变量 max_package_len，表示 package 字符串的最大长度。
static int	dpkg_parser(const char *line, char *package, size_t max_package_len)
{
	// 定义两个字符数组 fmt 和 tmp，分别用于存储格式化字符串和临时存储解析结果。
	char	fmt[32], tmp[32];

	// 使用 zbx_snprintf 函数根据 max_package_len 和 sizeof(tmp) - 1 生成一个格式化字符串 fmt，
	// 用于后续解析字符串 line 中的软件包名和操作符。
	zbx_snprintf(fmt, sizeof(fmt), "%%" ZBX_FS_SIZE_T "s %%" ZBX_FS_SIZE_T "s",
			(zbx_fs_size_t)(max_package_len - 1), (zbx_fs_size_t)(sizeof(tmp) - 1));

	// 使用 sscanf 函数按照 fmt 格式解析 line 字符串，将解析得到的软件包名存储在 package 变量中，
	// 将解析得到的操作符存储在 tmp 变量中。注意，这里只匹配成功一次，即找到一个完整的软件包名。
	if (2 != sscanf(line, fmt, package, tmp) || 0 != strcmp(tmp, "install"))
		// 如果解析失败或解析到的操作符不是 "install"，返回 FAIL 表示解析失败。
		return FAIL;

	// 如果解析成功，返回 SUCCEED 表示解析成功。
	return SUCCEED;
}

static size_t	print_packages(char *buffer, size_t size, zbx_vector_str_t *packages, const char *manager)
{
	size_t	offset = 0;
	int	i;

	if (NULL != manager)
		offset += zbx_snprintf(buffer + offset, size - offset, "[%s]", manager);

	if (0 < packages->values_num)
	{
		if (NULL != manager)
			offset += zbx_snprintf(buffer + offset, size - offset, " ");

		zbx_vector_str_sort(packages, ZBX_DEFAULT_STR_COMPARE_FUNC);

		for (i = 0; i < packages->values_num; i++)
			offset += zbx_snprintf(buffer + offset, size - offset, "%s, ", packages->values[i]);

		offset -= 2;
	}

	buffer[offset] = '\0';

	return offset;
}

static ZBX_PACKAGE_MANAGER	package_managers[] =
/*	NAME		TEST_CMD					LIST_CMD			PARSER */
{
	{"dpkg",	"dpkg --version 2> /dev/null",			"dpkg --get-selections",	dpkg_parser},
	{"pkgtools",	"[ -d /var/log/packages ] && echo true",	"ls /var/log/packages",		NULL},
	{"rpm",		"rpm --version 2> /dev/null",			"rpm -qa",			NULL},
	{"pacman",	"pacman --version 2> /dev/null",		"pacman -Q",			NULL},
	{NULL}
};

int	SYSTEM_SW_PACKAGES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义变量，用于存储偏移量、返回值、字符串缓冲区等
	size_t			offset = 0;
	int			ret = SYSINFO_RET_FAIL, show_pm, i, check_regex, check_manager;
	char			buffer[MAX_BUFFER_LEN], *regex, *manager, *mode, tmp[MAX_STRING_LEN], *buf = NULL,
				*package;
	zbx_vector_str_t	packages;
	ZBX_PACKAGE_MANAGER	*mng;

	// 检查参数数量，如果过多则返回错误
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return ret;
	}

	// 获取参数值
	regex = get_rparam(request, 0);
	manager = get_rparam(request, 1);
	mode = get_rparam(request, 2);

	// 判断参数是否合法
	check_regex = (NULL != regex && '\0' != *regex && 0 != strcmp(regex, "all"));
	check_manager = (NULL != manager && '\0' != *manager && 0 != strcmp(manager, "all"));

	// 判断mode参数值，根据不同值设置show_pm的值
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "full"))
		show_pm = 1;	/* show package managers' names */
	else if (0 == strcmp(mode, "short"))
		show_pm = 0;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return ret;
	}

	// 初始化字符串缓冲区
	*buffer = '\0';
	zbx_vector_str_create(&packages);

	// 遍历所有已知的package_managers
	for (i = 0; NULL != package_managers[i].name; i++)
	{
		mng = &package_managers[i];

		// 如果选择了特定的package manager，且当前manager不符合要求，则跳过
		if (1 == check_manager && 0 != strcmp(manager, mng->name))
			continue;

		// 执行test_cmd，判断该manager是否可用
		if (SUCCEED == zbx_execute(mng->test_cmd, &buf, tmp, sizeof(tmp), CONFIG_TIMEOUT,
				ZBX_EXIT_CODE_CHECKS_DISABLED) &&
				'\0' != *buf)	/* consider PMS present, if test_cmd outputs anything to stdout */
		{
			// 执行list_cmd，获取package列表
			if (SUCCEED != zbx_execute(mng->list_cmd, &buf, tmp, sizeof(tmp), CONFIG_TIMEOUT,
					ZBX_EXIT_CODE_CHECKS_DISABLED))
			{
				continue;
			}

			ret = SYSINFO_RET_OK;

			package = strtok(buf, "\n");

			while (NULL != package)
			{
				if (NULL != mng->parser)	/* check if the package name needs to be parsed */
				{
					if (SUCCEED == mng->parser(package, tmp, sizeof(tmp)))
						package = tmp;
					else
						goto next;
				}

				if (1 == check_regex && NULL == zbx_regexp_match(package, regex, NULL))
					goto next;

				zbx_vector_str_append(&packages, zbx_strdup(NULL, package));
next:
				package = strtok(NULL, "\n");
			}

			if (1 == show_pm)
			{
				offset += print_packages(buffer + offset, sizeof(buffer) - offset, &packages, mng->name);
				offset += zbx_snprintf(buffer + offset, sizeof(buffer) - offset, "\n");

				zbx_vector_str_clear_ext(&packages, zbx_str_free);
			}
		}
	}

	zbx_free(buf);

	if (0 == show_pm)
	{
		print_packages(buffer + offset, sizeof(buffer) - offset, &packages, NULL);

		zbx_vector_str_clear_ext(&packages, zbx_str_free);
	}
	else if (0 != offset)
		buffer[--offset] = '\0';

	zbx_vector_str_destroy(&packages);

	if (SYSINFO_RET_OK == ret)
		SET_TEXT_RESULT(result, zbx_strdup(NULL, buffer));
	else
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain package information."));

	return ret;
}
