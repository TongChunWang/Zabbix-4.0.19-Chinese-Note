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
#include "sysinfo.h"
#include "zbxjson.h"
#include "log.h"

typedef struct
{
	zbx_uint64_t ibytes;
	zbx_uint64_t ipackets;
	zbx_uint64_t ierr;
	zbx_uint64_t obytes;
	zbx_uint64_t opackets;
	zbx_uint64_t oerr;
	zbx_uint64_t colls;
}
net_stat_t;

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定网络接口的统计信息，并将结果存储在 `net_stat_t` 结构体中。注释中详细说明了每个步骤，包括检查接口名称、初始化结构体、调用 `perfstat_netinterface` 函数获取统计信息、提取数据并存储在 `net_stat_t` 结构体中。最后根据是否成功获取到统计信息返回相应的状态码。如果没有编译时支持 Perfstat API，则返回错误信息。
 ******************************************************************************/
// 定义一个静态函数，用于获取网络接口的统计信息
static int get_net_stat(const char *if_name, net_stat_t *ns, char **error)
{
#if defined(HAVE_LIBPERFSTAT)
	perfstat_id_t		ps_id;
	perfstat_netinterface_t	ps_netif;
    // 检查 if_name 是否为空，如果是，则返回错误信息
    if (NULL == if_name || '\0' == *if_name)
    {
        *error = zbx_strdup(NULL, "Network interface name cannot be empty.");
        return SYSINFO_RET_FAIL;
    }


    // 将 if_name 复制到 ps_id.name 中
    strscpy(ps_id.name, if_name);

    // 调用 perfstat_netinterface 函数获取网络接口的统计信息
    if (-1 == perfstat_netinterface(&ps_id, &ps_netif, sizeof(ps_netif), 1))
    {
        *error = zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno));
        return SYSINFO_RET_FAIL;
    }

    // 提取 ps_netif 结构体中的数据，并赋值给 ns 结构体
    ns->ibytes = (zbx_uint64_t)ps_netif.ibytes;
    ns->ipackets = (zbx_uint64_t)ps_netif.ipackets;
    ns->ierr = (zbx_uint64_t)ps_netif.ierrors;

    ns->obytes = (zbx_uint64_t)ps_netif.obytes;
    ns->opackets = (zbx_uint64_t)ps_netif.opackets;
    ns->oerr = (zbx_uint64_t)ps_netif.oerrors;

	ns->colls = (zbx_uint64_t)ps_netif.collisions;

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}

int	NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个字符指针变量，分别为 if_name、mode 和 error，用于存储从请求参数中获取的值。
	char		*if_name, *mode, *error;
	// 定义一个 net_stat_t 类型的变量 ns，用于存储网络接口的统计信息。
	net_stat_t	ns;

	// 检查 request 中的参数数量，如果超过2个，则返回错误信息，并设置结果为失败。
	if (2 < request->nparam)
	{
		// 设置结果为失败，并附带错误信息 "Too many parameters."
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取 if_name 和 mode 的值。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_net_stat 函数获取网络接口的统计信息，如果失败，则设置错误信息，并返回失败。
	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		// 设置结果为错误信息，并返回失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 检查 mode 是否为空或者是一个空字符，或者等于 "bytes"、"packets"、"errors" 中的任意一个字符串。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		// 设置结果为网络接口的 obytes 值。
		SET_UI64_RESULT(result, ns.ibytes);
	else if (0 == strcmp(mode, "packets"))
		// 设置结果为网络接口的 opackets 值。
		SET_UI64_RESULT(result, ns.ibytes);
	else if (0 == strcmp(mode, "errors"))
		// 设置结果为网络接口的 oerr 值。
		SET_UI64_RESULT(result, ns.ierr);
	else
	{
		// 设置结果为错误信息 "Invalid second parameter."，并返回失败。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 返回成功
	return SYSINFO_RET_OK;
}

int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *mode, *error;
	net_stat_t	ns;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, ns.obytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.opackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.oerr);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 NET_IF_TOTAL 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。函数的主要功能是根据请求参数中的接口名称和模式，获取网络接口的统计信息，并将结果返回给调用者。如果请求参数不合法或网络接口统计信息获取失败，函数将返回错误结果。
 ******************************************************************************/
// 定义一个名为 NET_IF_TOTAL 的函数，接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个字符指针变量，分别为 if_name、mode 和 error，用于存储从请求参数中获取的接口名称、模式和错误信息。
	char		*if_name, *mode, *error;
	// 定义一个 net_stat_t 类型的变量 ns，用于存储网络接口的统计信息。
	net_stat_t	ns;

	// 检查 request 中的参数数量，如果超过2个，则返回错误结果。
	if (2 < request->nparam)
	{
		// 设置错误结果，提示参数过多。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回 SYSINFO_RET_FAIL 表示调用失败。
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取接口名称和模式。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_net_stat 函数获取网络接口的统计信息，并将结果存储在 ns 变量中。如果获取失败，设置错误信息并返回 SYSINFO_RET_FAIL。
	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		// 设置错误结果，传入错误信息。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL 表示调用失败。
		return SYSINFO_RET_FAIL;
	}

	// 检查模式是否合法，如果为空或者等于 "bytes"，则将 ns.ibytes 和 ns.obytes 之和作为结果返回。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, ns.ibytes + ns.obytes);
	// 如果模式为 "packets"，则将 ns.ipackets 和 ns.opackets 之和作为结果返回。
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.ipackets + ns.opackets);
	// 如果模式为 "errors"，则将 ns.ierr 和 ns.oerr 之和作为结果返回。
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.ierr + ns.oerr);
	// 如果模式不合法，设置错误结果并返回 SYSINFO_RET_FAIL。
	else
	{
		// 设置错误结果，提示无效的第二个参数。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回 SYSINFO_RET_FAIL 表示调用失败。
		return SYSINFO_RET_FAIL;
	}

	// 如果一切都合法，返回 SYSINFO_RET_OK 表示调用成功。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定网络接口的碰撞次数。首先检查请求参数的数量，如果过多则返回失败。从请求参数中获取接口名称，然后调用 get_net_stat 函数获取网络接口的统计信息，并将碰撞次数存储在结果消息中。最后返回成功状态。
 ******************************************************************************/
// 定义一个函数 NET_IF_COLLISIONS，接收两个参数，一个是 AGENT_REQUEST 类型的请求，另一个是 AGENT_RESULT 类型的结果。
int NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符串指针，分别为 if_name 和 error，用于存储接口名称和错误信息。
	char *if_name, *error;
	// 定义一个 net_stat_t 类型的变量 ns，用于存储网络接口的统计信息。
	net_stat_t ns;

	// 检查请求参数的数量，如果大于1，则表示参数过多。
	if (1 < request->nparam)
	{
		// 设置结果消息，提示参数过多。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态。
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取接口名称。
	if_name = get_rparam(request, 0);

	if (SYSINFO_RET_FAIL == get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ns.colls);

	return SYSINFO_RET_OK;
}

int	NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义编译时符号，如果定义了HAVE_LIBPERFSTAT，则说明具备使用Perfstat API的条件
#if defined(HAVE_LIBPERFSTAT)
    int rc, i, ret = SYSINFO_RET_FAIL;
    perfstat_id_t ps_id;
    perfstat_netinterface_t *ps_netif = NULL;
    struct zbx_json j;

    // 查询系统中可用的perfstat_netinterface_t结构的数量
    if (-1 == (rc = perfstat_netinterface(NULL, NULL, sizeof(perfstat_netinterface_t), 0)))
    {
        // 获取系统信息失败，设置错误信息并返回SYSINFO_RET_FAIL
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 初始化zbx_json结构体
    zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

    // 添加一个数组，用于存储网络接口信息
    zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

    // 如果没有找到网络接口，直接返回SYSINFO_RET_OK
    if (0 == rc)
    {
        ret = SYSINFO_RET_OK;
        goto end;
    }

    // 为存储网络接口信息的结构分配内存
    ps_netif = zbx_malloc(ps_netif, rc * sizeof(perfstat_netinterface_t));

    // 为第一个网络接口设置名称
    strscpy(ps_id.name, FIRST_NETINTERFACE); /* 伪名称，表示第一个网络接口 */

    // 请求获取所有可用网络接口信息
    // 返回值表示获取到的结构数量
    if (-1 != (rc = perfstat_netinterface(&ps_id, ps_netif, sizeof(perfstat_netinterface_t), rc)))
        ret = SYSINFO_RET_OK;

    // 收集每个网络接口的信息
    for (i = 0; i < rc; i++)
    {
        // 添加一个对象，用于存储当前网络接口的信息
        zbx_json_addobject(&j, NULL);
        // 添加接口名称字段
        zbx_json_addstring(&j, "{#IFNAME}", ps_netif[i].name, ZBX_JSON_TYPE_STRING);
        // 关闭当前对象
        zbx_json_close(&j);

	}

	zbx_free(ps_netif);
end:
	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	return ret;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}
