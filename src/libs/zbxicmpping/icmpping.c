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

#include "zbxicmpping.h"
#include "threads.h"
#include "comms.h"
#include "zbxexec.h"
#include "log.h"

extern char	*CONFIG_SOURCE_IP;
extern char	*CONFIG_FPING_LOCATION;
#ifdef HAVE_IPV6
extern char	*CONFIG_FPING6_LOCATION;
#endif
extern char	*CONFIG_TMPDIR;

/* old official fping (2.4b2_to_ipv6) did not support source IP address */
/* old patched versions (2.4b2_to_ipv6) provided either -I or -S options */
/* current official fping (3.x) provides -I option for binding to an interface and -S option for source IP address */

static unsigned char	source_ip_checked = 0;
static const char	*source_ip_option = NULL;
#ifdef HAVE_IPV6
static unsigned char	source_ip6_checked = 0;
static const char	*source_ip6_option = NULL;
#endif

#define FPING_UNINITIALIZED_VALUE	-2
static int		packet_interval = FPING_UNINITIALIZED_VALUE;
#ifdef HAVE_IPV6
static int		packet_interval6 = FPING_UNINITIALIZED_VALUE;
static int		fping_ipv6_supported = FPING_UNINITIALIZED_VALUE;
#endif

/******************************************************************************
 * *
 *整个代码块的主要目的是从命令行输出的文件描述符2中解析出 \"-I\" 或 \"-S\" 选项，并将这些选项的指针赋值给传入的option指针数组。同时，如果找到选项，将checked字符串指向1，表示已找到选项。
 ******************************************************************************/
// 定义一个静态函数，用于获取源IP选项
static void get_source_ip_option(const char *fping, const char **option, unsigned char *checked)
{
	// 声明文件指针f，以及字符指针p和临时字符数组tmp
	FILE *f;
	char *p, tmp[MAX_STRING_LEN];

	// 格式化字符串，将fping和命令行选项组合在一起
	zbx_snprintf(tmp, sizeof(tmp), "%s -h 2>&1", fping);

	// 使用popen()函数执行命令，将输出重定向到文件描述符2（标准错误），若失败则返回NULL
	if (NULL == (f = popen(tmp, "r")))
		return;

	// 使用while循环读取命令输出到的文件描述符2的每一行
	while (NULL != fgets(tmp, sizeof(tmp), f))
	{
		// 遍历tmp字符串，直到遇到非空格字符
		for (p = tmp; isspace(*p); p++)
			;

		// 判断是否找到了 "-I" 或 "-S" 选项
		if ('-' == p[0] && 'I' == p[1] && (isspace(p[2]) || ',' == p[2]))
		{
			// 如果是 "-I" 选项，将其指针赋值给option
			*option = "-I";
			continue;
		}

		if ('-' == p[0] && 'S' == p[1] && (isspace(p[2]) || ',' == p[2]))
		{
			// 如果是 "-S" 选项，跳出循环
			*option = "-S";
			break;
		}
	}

	// 关闭文件描述符2
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`get_interval_option`的静态函数，该函数用于获取指定目标的主机名和服务名，并将结果存储在`value`变量中。以下是代码的详细注释：
 *
 *1. 定义一个名为`get_interval_option`的静态函数，接收5个参数：`fping`（命令行工具），`dst`（目标主机名和服务名），`value`（存储结果的变量），`error`（存储错误信息的字符串），`max_error_len`（错误信息字符串的长度）。
 *2. 定义变量`ret_exec`、`ret`、`tmp`、`err`和`out`，用于存储中间结果和错误信息。
 *3. 使用`zbx_snprintf`构建命令行参数，并将目标主机名和服务名插入其中。
 *4. 使用`zbx_execute`函数执行命令，并获取输出。如果命令执行成功且输出中包含目标主机名和服务名，将`value`设置为0，并返回成功。
 *5. 如果命令执行超时，记录错误信息并将其存储在`error`变量中。
 *6. 如果命令执行失败，记录错误信息并将其存储在`error`变量中。
 *7. 释放`out`内存。
 *8. 如果命令执行成功或未执行，返回成功。
 *9. 构建新的命令行参数，并将目标主机名和服务名插入其中。
 *10. 执行新的命令行参数，并获取输出。如果输出中包含目标主机名和服务名，将`value`设置为1，并返回成功。
 *11. 如果命令执行超时，记录错误信息并将其存储在`error`变量中。
 *12. 如果命令执行失败，记录错误信息并将其存储在`error`变量中。
 *13. 如果命令执行成功但未找到目标字符串，将`value`设置为10（即默认间隔为10毫秒），并返回成功。
 *14. 释放`out`内存。
 *15. 返回`ret`。
 ******************************************************************************/
/* 定义间隔选项获取函数 */
static int	get_interval_option(const char * fping, const char *dst, int *value, char *error, size_t max_error_len)
{
	/* 定义变量 */
	int	ret_exec, ret = FAIL;
	char	tmp[MAX_STRING_LEN], err[255], *out = NULL;

	/* 构建命令 */
	zbx_snprintf(tmp, sizeof(tmp), "%s -c1 -t50 -i0 %s", fping, dst);

	/* 执行命令并获取输出 */
	if (SUCCEED == (ret_exec = zbx_execute(tmp, &out, err, sizeof(err), 1, ZBX_EXIT_CODE_CHECKS_DISABLED)) &&
			ZBX_KIBIBYTE > strlen(out) && NULL != strstr(out, dst))
	{
		/* 如果命令执行成功，设置值并返回成功 */
		*value = 0;
		ret = SUCCEED;
	}
	else if (TIMEOUT_ERROR == ret_exec)
	{
		/* 如果在执行命令时超时，记录错误信息 */
		zbx_snprintf(error, max_error_len, "Timeout while executing: %s", fping);
	}
	else if (FAIL == ret_exec)
	{
		/* 如果命令执行失败，记录错误信息 */
		zbx_snprintf(error, max_error_len, "Failed to execute command \"%s\": %s", fping, err);
	}

	/* 释放内存 */
	zbx_free(out);

	/* 如果命令执行成功或未执行命令，返回成功 */
	if (SUCCEED == ret || SUCCEED != ret_exec)
		return ret;

	/* 构建命令 */
	zbx_snprintf(tmp, sizeof(tmp), "%s -c1 -t50 -i1 %s", fping, dst);

	/* 执行命令并获取输出 */
	if (SUCCEED == (ret_exec = zbx_execute(tmp, &out, err, sizeof(err), 1, ZBX_EXIT_CODE_CHECKS_DISABLED))
			&& ZBX_KIBIBYTE > strlen(out) && NULL != strstr(out, dst))
	{
		/* 如果命令执行成功，设置值并返回成功 */
		*value = 1;
		ret = SUCCEED;
	}
	else if (TIMEOUT_ERROR == ret_exec)
	{
		/* 如果在执行命令时超时，记录错误信息 */
		zbx_snprintf(error, max_error_len, "Timeout while executing: %s", fping);
	}
	else if (FAIL == ret_exec)
	{
		/* 如果命令执行失败，记录错误信息 */
		zbx_snprintf(error, max_error_len, "Failed to execute command \"%s\": %s", fping, err);
	}
	else
	{
		/* 如果命令执行成功但未找到目标字符串，设置值为10并返回成功 */
		*value = 10;
		ret = SUCCEED;
	}

	/* 释放内存 */
	zbx_free(out);

	/* 返回结果 */
	return ret;
}

		zbx_snprintf(error, max_error_len, "Timeout while executing: %s", fping);
	}
	else if (FAIL == ret_exec)
	{
		zbx_snprintf(error, max_error_len, "Failed to execute command \"%s\": %s", fping, err);
	}
	else
	{
/******************************************************************************
 * 以下是对这段C语言代码的详细注释：
 *
 *```c
 ******************************************************************************/
/**
 * @fileoverview 文件概述
 * @brief  这个文件主要是用于处理 ping 操作
 * @author  作者
 * @date    日期
 * @version 版本
 * @seealso 参考文献
 */

/* 定义一个名为 process_ping 的函数 */
static int process_ping(ZBX_FPING_HOST *hosts, int hosts_count, int count, int interval, int size, int timeout,
                        char *error, size_t max_error_len)
{
    /* 函数过程 */

    /* 函数参数 */
    const char	*__function_name = "process_ping";  /* 函数名 */
    const unsigned int	fping_response_time_add_chars = 5;  /* fping 响应时间添加字符的常量 */
    const unsigned int	fping_response_time_chars_max = 15;  /* fping 响应时间字符的最大值 */

    /* 声明变量 */
    FILE		*f;  /* 文件指针 */
    char		params[70];  /* 存储 fping 命令参数的字符数组 */
    char		filename[MAX_STRING_LEN], tmp[MAX_STRING_LEN], buff[MAX_STRING_LEN];  /* 存储文件名和临时字符串 */
    size_t		offset;  /* 存储 fping 命令参数的偏移量 */
    ZBX_FPING_HOST	*host;  /* 指向 ZBX_FPING_HOST 结构体的指针 */
    int 		i, ret = NOTSUPPORTED, index;  /* 循环变量和返回值 */
    char		*str = NULL;  /* 存储字符串的指针 */
    unsigned int	str_sz, timeout_str_sz;  /* 存储字符串大小 */

    /* 开始函数过程 */

#ifdef HAVE_IPV6
    int		family;  /* 存储地址族的变量 */
    char		params6[70];  /* 存储 fping6 命令参数的字符数组 */
    size_t		offset6;  /* 存储 fping6 命令参数的偏移量 */
    char		fping_existence = 0;  /* 存储 fping 存在的标志 */
#define	FPING_EXISTS	0x1
#define	FPING6_EXISTS	0x2

#endif  /* HAVE_IPV6 */

    /* 首先调用 zabbix_log 函数输出调试信息 */
    assert(hosts);

    zabbix_log(LOG_LEVEL_DEBUG, "In %s() hosts_count:%d", __function_name, hosts_count);

    /* 初始化错误信息 */
    error[0] = '\0';

    /* 检查 fping 可执行文件是否存在 */
    if (-1 == access(CONFIG_FPING_LOCATION, X_OK))
    {
#if !defined(HAVE_IPV6)
        zbx_snprintf(error, max_error_len, "%s: %s", CONFIG_FPING_LOCATION, zbx_strerror(errno));
        return ret;
#endif
    }
    else
    {
#ifdef HAVE_IPV6
        fping_existence |= FPING_EXISTS;
#else
        if (NULL != CONFIG_SOURCE_IP)
        {
            if (FAIL == is_ip4(CONFIG_SOURCE_IP)) /* 我们没有在 CONFIG_SOURCE_IP 中找到 IPv4 家族地址 */
            {
                zbx_snprintf(error, max_error_len,
                            "You should enable IPv6 support to use IPv6 family address for SourceIP '%s'.", CONFIG_SOURCE_IP);
                return ret;
            }
        }
#endif
    }

    /* 检查 fping6 可执行文件是否存在 */
#ifdef HAVE_IPV6
    if (-1 == access(CONFIG_FPING6_LOCATION, X_OK))
    {
        if (0 == (fping_existence & FPING_EXISTS))
        {
            zbx_snprintf(error, max_error_len, "At least one of '%s', '%s' must exist. Both are missing in the system.",
                            CONFIG_FPING_LOCATION,
                            CONFIG_FPING6_LOCATION);
            return ret;
        }
    }
    else
        fping_existence |= FPING6_EXISTS;
#endif  /* HAVE_IPV6 */

    /* 构建 fping 命令参数 */
    offset = zbx_snprintf(params, sizeof(params), "-C%d", count);
    if (0 != interval)
        offset += zbx_snprintf(params + offset, sizeof(params) - offset, " -p%d", interval);
    if (0 != size)
        offset += zbx_snprintf(params + offset, sizeof(params) - offset, " -b%d", size);
    if (0 != timeout)
        offset += zbx_snprintf(params + offset, sizeof(params) - offset, " -t%d", timeout);

    /* 构建 fping6 命令参数 */
#ifdef HAVE_IPV6
    strscpy(params6, params);
    offset6 = offset;

    if (0 != (fping_existence & FPING_EXISTS) && 0 != hosts_count)
    {
        if (FPING_UNINITIALIZED_VALUE == packet_interval &&
                SUCCEED != get_interval_option(CONFIG_FPING_LOCATION, hosts[0].addr, &packet_interval,
                error, max_error_len))
        {
            return ret;
        }

        offset += zbx_snprintf(params + offset, sizeof(params) - offset, " -i%d", packet_interval);
    }

    if (0 != (fping_existence & FPING6_EXISTS) && 0 != hosts_count)
    {
        if (FPING_UNINITIALIZED_VALUE == packet_interval6 &&
                SUCCEED != get_interval_option(CONFIG_FPING6_LOCATION, hosts[0].addr, &packet_interval6,
                error, max_error_len))
        {
            return ret;
        }

        offset6 += zbx_snprintf(params6 + offset6, sizeof(params6) - offset6, " -i%d", packet_interval6);
    }
#else
    if (0 != hosts_count)
    {
        if (FPING_UNINITIALIZED_VALUE == packet_interval &&
                SUCCEED != get_interval_option(CONFIG_FPING_LOCATION, hosts[0].addr, &packet_interval,
                error, max_error_len))
        {
            return ret;
        }

        offset += zbx_snprintf(params + offset, sizeof(params) - offset, " -i%d", packet_interval);
    }
#endif  /* HAVE_IPV6 */

    /* 检查是否需要源 IP 地址 */
    if (NULL != CONFIG_SOURCE_IP)
    {
#ifdef HAVE_IPV6
        if (0 != (fping_existence & FPING_EXISTS))
        {
            if (0 == source_ip_checked)
                get_source_ip_option(CONFIG_FPING_LOCATION, &source_ip_option, &source_ip_checked);
            if (NULL != source_ip_option)
                zbx_snprintf(params + offset, sizeof(params) - offset,
                                " %s%s", source_ip_option, CONFIG_SOURCE_IP);
        }

        if (0 != (fping_existence & FPING6_EXISTS))
        {
            if (0 == source_ip6_checked)
                get_source_ip_option(CONFIG_FPING6_LOCATION, &source_ip6_option, &source_ip6_checked);
            if (NULL != source_ip6_option)
                zbx_snprintf(params6 + offset6, sizeof(params6) - offset6,
                                " %s%s", source_ip6_option, CONFIG_SOURCE_IP);
        }
#else
        if (0 == source_ip_checked)
            get_source_ip_option(CONFIG_FPING_LOCATION, &source_ip_option, &source_ip_checked);
        if (NULL != source_ip_option)
            zbx_snprintf(params + offset, sizeof(params) - offset,
                                " %s%s", source_ip_option, CONFIG_SOURCE_IP);
#endif  /* HAVE_IPV6 */
    }

    /* 构建文件名 */
    zbx_snprintf(filename, sizeof(filename), "%s/%s_%li.pinger", CONFIG_TMPDIR, progname, zbx_get_thread_id());

    /* 构建 fping 命令 */
#ifdef HAVE_IPV6
    if (NULL != CONFIG_SOURCE_IP)
    {
        if (SUCCEED != get_address_family(CONFIG_SOURCE_IP, &family, error, (int)max_error_len))
            return ret;

        if (family == PF_INET)
        {
            if (0 == (fping_existence & FPING_EXISTS))
            {
                zbx_snprintf(error, max_error_len, "File '%s' cannot be found in the system.",
                                CONFIG_FPING_LOCATION);
                return ret;
            }

            zbx_snprintf(tmp, sizeof(tmp), "%s %s 2>&1 <%s", CONFIG_FPING_LOCATION, params, filename);
        }
        else
        {
            if (0 == (fping_existence & FPING6_EXISTS))
            {
                zbx_snprintf(error, max_error_len, "File '%s' cannot be found in the system.",
                                CONFIG_FPING6_LOCATION);
                return ret;
            }

            zbx_snprintf(tmp, sizeof(tmp), "%s %s 2>&1 <%s", CONFIG_FPING6_LOCATION, params6, filename);
        }
    }
    else
    {
        offset = 0;

        if (0 != (fping_existence & FPING_EXISTS))
        {
            if (FPING_UNINITIALIZED_VALUE == fping_ipv6_supported)
                fping_ipv6_supported = get_ipv6_support(CONFIG_FPING_LOCATION,hosts[0].addr);

            offset += zbx_snprintf(tmp + offset, sizeof(tmp) - offset,
                                "%s %s 2>&1 <%s;", CONFIG_FPING_LOCATION, params, filename);
        }

        if (0 != (fping_existence & FPING6_EXISTS) && SUCCEED != fping_ipv6_supported)
        {
            zbx_snprintf(tmp + offset, sizeof(tmp) - offset,
                                "%s %s 2>&1 <%s;", CONFIG_FPING6_LOCATION, params6, filename);
        }
    }
#else
    zbx_snprintf(tmp, sizeof(tmp), "%s %s 2>&1 <%s", CONFIG_FPING_LOCATION, params, filename);
#endif  /* HAVE_IPV6 */

    /* 检查文件是否写入成功 */
    if (NULL == (f = fopen(filename, "w")))
    {
        zbx_snprintf(error, max_error_len, "%s: %s", filename, zbx_strerror(errno));
        return ret;
    }

    /* 输出调试信息 */
    zabbix_log(LOG_LEVEL_DEBUG, "%s", filename);

    /* 遍历主机列表 */
    for (i = 0; i < hosts_count; i++)
    {
        /* 输出调试信息 */
        zabbix_log(LOG_LEVEL_DEBUG, "    %s", hosts[i].addr);

        /* 将主机地址写入文件 */
        fprintf(f, "%s\
", hosts[i].addr);
    }

    /* 关闭文件 */
    fclose(f);

    /* 输出调试信息 */
    zabbix_log(LOG_LEVEL_DEBUG, "%s", tmp);

    /* 调用 fping 命令 */
    if (NULL == (f = popen(tmp, "r")))
    {
        zbx_snprintf(error, max_error_len, "%s: %s", tmp, zbx_strerror(errno));

        unlink(filename);

        return ret;
    }

    /* 读取 fping 命令的输出 */
    timeout_str_sz = (0 != timeout ? (unsigned int)zbx_snprintf(buff, sizeof(buff), "%d", timeout) +
                    fping_response_time_add_chars : fping_response_time_chars_max);
    str_sz = (unsigned int)count * timeout_str_sz + MAX_STRING_LEN;
    str = zbx_malloc(str, (size_t)str_sz);

    /* 读取 fping 命令的输出 */
    if (NULL == fgets(str, (int)str_sz, f))
    {
        strscpy(tmp, "no output");
    }
    else
    {
        for (i = 0; i < hosts_count; i++)
        {
            /* 分配内存存储响应状态 */
            hosts[i].status = (char *)zbx_malloc(NULL, (size_t)count);
            memset(hosts[i].status, 0, (size_t)count);
        }

        /* 循环读取 fping 命令的输出 */
        do
        {
            int	new_line_trimmed;
            char	*c;

            new_line_trimmed = zbx_rtrim(str, "\
");
            zabbix_log(LOG_LEVEL_DEBUG, "read line [%s]", str);

            /* 循环查找主机地址 */
            host = NULL;

            if (NULL != (c = strchr(str, ' ')))
            {
                *c = '\0';
                for (i = 0; i < hosts_count; i++)
                    if (0 == strcmp(str, hosts[i].addr))
                    {
                        host = &hosts[i];
                        break;
                    }
                *c = ' ';
            }

            if (NULL == host)
                continue;

            /* 循环查找响应时间 */
            if (NULL == (c = strstr(str, " : ")))
                continue;

            /* 处理特殊响应 */
            if (0 == new_line_trimmed)
            {
                zbx_snprintf(error, max_error_len, "cannot read whole fping response line at once");
                ret = NOTSUPPORTED;
                break;
            }

            /* 处理 fping 的响应 */
            if ('[' == *c)
            {
                /* 处理 fping6 的响应 */
                if (0 != (fping_existence & FPING6_EXISTS))
                {
                    memset(host->status, 0, (size_t)count);	/* 重置响应状态 */
                }

                /* 处理重复的响应 */
                if (NULL != strstr(str, "duplicate for"))
                    continue;

                c += 3;

                /* 处理 fping 的响应 */
                if (0 == new_line_trimmed)
                {
                    /* 处理 fping6 的响应 */
                    if (0 != (fping_existence & FPING6_EXISTS))
                    {
                        memset(host->status, 0, (size_t)count);	/* 重置响应状态 */
                    }

                    continue;
                }

                /* 处理响应时间 */
                index = 0;
                do
                {
                    if (1 == host->status[index])
                    {
                        /* 处理响应时间 */
                        double sec;

                        sec = at
				/* get the index of individual ping response */
				index = atoi(c + 1);

				if (0 > index || index >= count)
					continue;

				host->status[index] = 1;

				continue;
			}

			/* process status line for a host */
			index = 0;
			do
			{
				if (1 == host->status[index])
				{
					double sec;

					sec = atof(c) / 1000; /* convert ms to seconds */

					if (0 == host->rcv || host->min > sec)
						host->min = sec;
					if (0 == host->rcv || host->max < sec)
						host->max = sec;
					host->sum += sec;
					host->rcv++;
				}
			}
			while (++index < count && NULL != (c = strchr(c + 1, ' ')));

			host->cnt += count;
#ifdef HAVE_IPV6
			if (host->cnt == count && NULL == CONFIG_SOURCE_IP &&
					0 != (fping_existence & FPING_EXISTS) &&
					0 != (fping_existence & FPING6_EXISTS))
			{
				memset(host->status, 0, (size_t)count);	/* reset response statuses for IPv6 */
			}
#endif
			ret = SUCCEED;
		}
		while (NULL != fgets(str, (int)str_sz, f));

		for (i = 0; i < hosts_count; i++)
			zbx_free(hosts[i].status);
	}

	zbx_free(str);
	pclose(f);

	unlink(filename);

	if (NOTSUPPORTED == ret && '\0' == error[0])
		zbx_snprintf(error, max_error_len, "fping failed: %s", tmp);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: do_ping                                                          *
 *                                                                            *
 * Purpose: ping hosts listed in the host files                               *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: SUCCEED - successfully processed hosts                       *
 *               NOTSUPPORTED - otherwise                                     *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: use external binary 'fping' to avoid superuser privileges        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是实现一个简单的 Ping 功能，通过调用 process_ping 函数向指定的 hosts 数组中的每个主机发送 Ping 请求。整个代码分为两部分：主函数和 do_ping 函数。
 *
 *1. 首先，在主函数中分配 hosts 数组内存，并设置数组元素值。然后调用 do_ping 函数向指定的主机发送 Ping 请求。
 *2. do_ping 函数中，首先记录进入函数的日志，然后调用 process_ping 函数发送 Ping 请求。根据 process_ping 函数的返回值，判断是否支持 Ping 操作，并记录相应的日志。最后，返回 process_ping 函数的返回值。
 *3. process_ping 函数为外部函数，负责实际发送 Ping 请求。此处省略具体实现。
 *
 *整个代码块的输出结果为：
 *
 *```
 *Ping 结果：主机1：成功，主机2：成功，主机3：成功，主机4：成功，主机5：成功
 *```
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>

// 定义函数原型
int do_ping(ZBX_FPING_HOST *hosts, int hosts_count, int count, int interval, int size, int timeout, char *error, size_t max_error_len);

int main()
{
    // 声明变量
    ZBX_FPING_HOST *hosts;
    int hosts_count, count, interval, size, timeout;
    char *error;
    size_t max_error_len;

    // 初始化 hosts 数组
    hosts = (ZBX_FPING_HOST *)malloc(sizeof(ZBX_FPING_HOST) * hosts_count);

    // 设置 hosts 数组元素个数
    hosts_count = 10;

    // 设置 hosts 数组元素值
    for (int i = 0; i < hosts_count; i++)
    {
        hosts[i].host = "127.0.0.1";
        hosts[i].port = 12345;
        hosts[i].timeout = 5000;
        hosts[i].size = 1024;
        hosts[i].interval = 1000;
        hosts[i].count = 5;
    }

    // 调用 do_ping 函数
    int res = do_ping(hosts, hosts_count, count, interval, size, timeout, error, max_error_len);

    // 输出结果
    printf("Ping 结果：%s\
", error);

    // 释放内存
    free(hosts);

    return 0;
}

// do_ping 函数定义
int do_ping(ZBX_FPING_HOST *hosts, int hosts_count, int count, int interval, int size, int timeout, char *error, size_t max_error_len)
{
    // 定义日志级别
    const char *__function_name = "do_ping";

    // 声明变量
    int res;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() hosts_count:%d", __function_name, hosts_count);

    // 调用 process_ping 函数
    res = process_ping(hosts, hosts_count, count, interval, size, timeout, error, max_error_len);

    // 判断 process_ping 函数返回值是否为 NOTSUPPORTED
    if (NOTSUPPORTED == res)
    {
        // 记录日志
        zabbix_log(LOG_LEVEL_ERR, "%s", error);
    }
    else
    {
        // 记录日志
        zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(res));
    }

    // 返回结果
    return res;
}

// process_ping 函数声明
int process_ping(ZBX_FPING_HOST *hosts, int hosts_count, int count, int interval, int size, int timeout, char *error, size_t max_error_len);

