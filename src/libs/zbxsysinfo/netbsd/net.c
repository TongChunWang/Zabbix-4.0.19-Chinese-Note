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

static struct nlist kernel_symbols[] =
{
	{"_ifnet", N_UNDF, 0, 0, 0},
	{"_tcbtable", N_UNDF, 0, 0, 0},
	{NULL, 0, 0, 0, 0}
};

#define IFNET_ID 0

/******************************************************************************
 * *
 *该代码主要目的是获取指定网络接口的统计信息，如收发数据包数量、错误次数等。代码首先检查接口名称是否为空，然后打开内核内存访问权限。接着遍历ifnet链表，查找匹配的接口并累加相应的统计信息。最后关闭内核内存访问权限，根据查询结果返回成功或失败。
 ******************************************************************************/
// 定义一个静态函数get_ifdata，用于获取指定网络接口的统计信息
static int get_ifdata(const char *if_name,
                     zbx_uint64_t *ibytes, zbx_uint64_t *ipackets, zbx_uint64_t *ierrors, zbx_uint64_t *idropped,
                     zbx_uint64_t *obytes, zbx_uint64_t *opackets, zbx_uint64_t *oerrors,
                     zbx_uint64_t *tbytes, zbx_uint64_t *tpackets, zbx_uint64_t *terrors,
                     zbx_uint64_t *icollisions, char **error)
{
    // 定义一个ifnet链表头结构体
    struct ifnet_head head;
	struct ifnet		*ifp;
    // 定义一个ifnet结构体变量
    struct ifnet v;

    // 打开内核内存访问权限，需要root权限
    kvm_t *kp;
    // 初始化错误码
    int len = 0;
    int ret = SYSINFO_RET_FAIL;

    // 如果if_name为空或为'\0'，报错并返回FAIL
    if (NULL == if_name || '\0' == *if_name)
    {
        *error = zbx_strdup(NULL, "Network interface name cannot be empty.");
        return FAIL;
    }

    // 打开内核内存访问权限
    if (NULL == (kp = kvm_open(NULL, NULL, NULL, O_RDONLY, NULL)))
    {
        *error = zbx_strdup(NULL, "Cannot obtain a descriptor to access kernel virtual memory.");
        return FAIL;
    }

    // 查询内核符号表中IFNET_ID对应的结构体类型
    if (N_UNDF == kernel_symbols[IFNET_ID].n_type)
        if (0 != kvm_nlist(kp, &kernel_symbols[0]))
            kernel_symbols[IFNET_ID].n_type = N_UNDF;

    // 如果IFNET_ID对应的结构体类型不为N_UNDF，则继续执行后续操作
    if (N_UNDF != kernel_symbols[IFNET_ID].n_type)
    {
        // 读取内核内存中ifnet链表的头长度
        len = sizeof(struct ifnet_head);

        // 读取ifnet链表头
        if (kvm_read(kp, kernel_symbols[IFNET_ID].n_value, &head, len) >= len)
        {
            // 读取ifnet结构体变量
            len = sizeof(struct ifnet);

			/* if_ibytes;		total number of octets received */
			/* if_ipackets;		packets received on interface */
			/* if_ierrors;		input errors on interface */
			/* if_iqdrops;		dropped on input, this interface */
			/* if_obytes;		total number of octets sent */
			/* if_opackets;		packets sent on interface */
			/* if_oerrors;		output errors on interface */
			/* if_collisions;	collisions on csma interfaces */

			if (ibytes)
				*ibytes = 0;
			if (ipackets)
				*ipackets = 0;
			if (ierrors)
				*ierrors = 0;
			if (idropped)
				*idropped = 0;
			if (obytes)
				*obytes = 0;
			if (opackets)
				*opackets = 0;
			if (oerrors)
				*oerrors = 0;
			if (tbytes)
				*tbytes = 0;
			if (tpackets)
				*tpackets = 0;
			if (terrors)
				*terrors = 0;
			if (icollisions)
				*icollisions = 0;

			for (ifp = head.tqh_first; ifp; ifp = v.if_list.tqe_next)
			{
				if (kvm_read(kp, (u_long)ifp, &v, len) < len)
					break;

                // 判断接口名称是否匹配
                if (0 == strcmp(if_name, v.if_xname))
                {
                    // 累加接口收发数据包的统计信息
                    if (ibytes)
                        *ibytes += v.if_ibytes;
                    if (ipackets)
                        *ipackets += v.if_ipackets;
                    if (ierrors)
                        *ierrors += v.if_ierrors;
                    if (idropped)
                        *idropped += v.if_iqdrops;
                    if (obytes)
                        *obytes += v.if_obytes;
                    if (opackets)
                        *opackets += v.if_opackets;
                    if (oerrors)
                        *oerrors += v.if_oerrors;
                    if (tbytes)
                        *tbytes += v.if_ibytes + v.if_obytes;
                    if (tpackets)
                        *tpackets += v.if_ipackets + v.if_opackets;
                    if (terrors)
                        *terrors += v.if_ierrors + v.if_oerrors;
                    if (icollisions)
                        *icollisions += v.if_collisions;
                    // 设置返回码为SYSINFO_RET_OK
                    ret = SYSINFO_RET_OK;
                }
            }
        }
    }

    // 关闭内核内存访问权限
    kvm_close(kp);

    // 如果返回码为SYSINFO_RET_FAIL，报错并返回
    if (SYSINFO_RET_FAIL == ret)
    {
        *error = zbx_strdup(NULL, "Cannot find information for this network interface.");
        return SYSINFO_RET_FAIL;
    }

    // 返回成功
    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取接口的网络数据（如字节数、数据包数、错误数和丢包数），并根据传入的参数模式返回相应的数据。在此过程中，还对传入的参数进行了有效性检查，以确保参数合法。如果一切顺利，将返回 SYSINFO_RET_OK 表示成功。
 ******************************************************************************/
// 定义一个名为 NET_IF_IN 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针和整数类型的变量，用于存储接口名称、模式、数据流量、数据包计数、错误计数和丢包计数等参数。
	char *if_name, *mode, *error;
	zbx_uint64_t ibytes, ipackets, ierrors, idropped;

	// 检查 request 参数的数量，如果大于2，则表示参数过多。
	if (2 < request->nparam)
	{
		// 设置结果消息，提示参数过多，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取请求参数中的接口名称和模式。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_ifdata 函数获取接口的网络数据，如果失败，则设置结果消息为错误信息，并返回 SYSINFO_RET_FAIL 表示失败。
	if (SYSINFO_RET_OK != get_ifdata(if_name, &ibytes, &ipackets, &ierrors, &idropped, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, &error))
	{
		// 设置结果消息为错误信息，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 判断模式是否合法，如果合法，则设置结果消息为相应的网络数据，并返回 SYSINFO_RET_OK 表示成功。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, ibytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ipackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ierrors);
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, idropped);
	else
	{
		// 设置结果消息为无效参数提示，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 返回 SYSINFO_RET_OK，表示成功。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取接口的网络数据（如输出字节数、输出数据包数和错误次数），并根据传入的参数模式输出相应的数据。在这个过程中，还对传入的参数进行了检查，确保参数合法。如果遇到错误，会设置错误消息并返回失败状态码。
 ******************************************************************************/
// 定义一个名为 NET_IF_OUT 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符指针变量，用于存储接口名称、模式和错误信息
	char		*if_name, *mode, *error;
	// 定义一些整型变量，用于存储输出字节数、输出数据包数和错误次数
	zbx_uint64_t	obytes, opackets, oerrors;

	// 检查 request 中的参数数量，如果大于2，则表示参数过多
	if (2 < request->nparam)
	{
		// 设置结果消息，提示参数过多
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态码
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取接口名称和模式
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_ifdata 函数获取接口的网络数据，如果不成功，则设置错误信息并返回失败状态码
	if (SYSINFO_RET_OK != get_ifdata(if_name, NULL, NULL, NULL, NULL, &obytes, &opackets, &oerrors, NULL, NULL,
			NULL, NULL, &error))
	{
		// 设置结果消息，传递错误信息
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, obytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, opackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, oerrors);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
// 检查模式是否合法，如果模式为空或者等于 "bytes"，则默认输出字节数
/******************************************************************************
 *整个代码块的主要目的是获取接口的网络数据（如字节传输量、数据包传输量和错误次数），并根据传入的参数模式返回相应的数据。在此过程中，还对传入的参数进行了检查，以确保参数合法。如果遇到错误，将返回失败状态码并附带错误信息。
 ******************************************************************************/
// 定义一个名为 NET_IF_TOTAL 的函数，该函数接受两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针和zbx_uint64_t类型的变量，用于存储接口名称、模式、错误信息、字节传输量、数据包传输量和错误次数。
	char		*if_name, *mode, *error;
	zbx_uint64_t	tbytes, tpackets, terrors;

	// 检查 request 参数的数量，如果大于2，则表示参数过多。
	if (2 < request->nparam)
	{
		// 设置结果消息，提示参数过多。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 从 request 参数中获取接口名称和模式。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_OK != get_ifdata(if_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &tbytes, &tpackets,
			&terrors, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, tbytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, tpackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, terrors);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定接口的碰撞次数，并将结果存储在 result 对象中。如果请求参数过多或查询失败，则会返回错误信息。
 ******************************************************************************/
// 定义一个函数 NET_IF_COLLISIONS，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int	NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针变量，分别用于存储接口名称和错误信息
	char		*if_name, *error;
	// 定义一个 zbx_uint64_t 类型的变量 icollisions，用于存储碰撞次数
	zbx_uint64_t	icollisions;

	// 检查 request 中的参数数量，如果大于1，则报错并返回 SYSINFO_RET_FAIL
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取第一个参数（接口名称）
	if_name = get_rparam(request, 0);

	// 调用 get_ifdata 函数获取接口的碰撞次数，若成功则存储在 icollisions 中，失败则存储在 error 中
	if (SYSINFO_RET_OK != get_ifdata(if_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, &icollisions, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, icollisions);

	return SYSINFO_RET_OK;
}

int	NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个整型变量 i，用于循环计数。
	int			i;
	// 定义一个 zbx_json 类型的变量 j，用于存储 JSON 数据。
	struct zbx_json		j;
	// 定义一个 struct if_nameindex 类型的变量 interfaces，用于存储系统接口信息。
	struct if_nameindex	*interfaces;

	// 检查 interfaces 是否为空，如果不为空，则执行以下操作：
	if (NULL == (interfaces = if_nameindex()))
	{
		// 设置错误信息，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 初始化 zbx_json 变量 j，并设置缓冲区大小。
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	// 添加一个数组标签，表示要存储的数据。
	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	// 遍历 interfaces 数组，获取系统接口信息。
	for (i = 0; 0 != interfaces[i].if_index; i++)
	{
		// 添加一个对象标签，表示接口信息。
		zbx_json_addobject(&j, NULL);
		// 添加一个字符串标签，表示接口名称。
		zbx_json_addstring(&j, "{#IFNAME}", interfaces[i].if_name, ZBX_JSON_TYPE_STRING);
		// 关闭当前对象，准备添加下一个对象。

		zbx_json_close(&j);
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	if_freenameindex(interfaces);

	return SYSINFO_RET_OK;
}
