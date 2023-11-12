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

#include <sys/ioctl.h>
#include <sys/sockio.h>

#if OpenBSD >= 201405			/* if OpenBSD 5.5 or newer */
#	if OpenBSD >= 201510		/* if Openbsd 5.8 or newer */
#		include <sys/malloc.h>	/* Workaround: include malloc.h here without _KERNEL to prevent its */
					/* inclusion later from if_var.h to avoid malloc() and free() redefinition. */
#		define _KERNEL	/* define _KERNEL to enable 'ifnet' and 'ifnet_head' definitions in if_var.h */
#	endif
#	include <net/if_var.h>  /* structs ifnet and ifnet_head are defined in this header since OpenBSD 5.5 */
#endif

static struct nlist kernel_symbols[] =
{
	{"_ifnet", N_UNDF, 0, 0, 0},
	{"_tcbtable", N_UNDF, 0, 0, 0},
	{NULL, 0, 0, 0, 0}
};

#define IFNET_ID 0

/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *这个代码块的主要目的是获取指定网络接口的统计数据，并将其存储在传入的指针变量中。代码首先尝试从内核符号表中获取接口数据，如果找不到，则使用`SIOCGIFDATA`接口函数从用户空间获取数据。
 ******************************************************************************/
static int get_ifdata(const char *if_name,
                     zbx_uint64_t *ibytes, zbx_uint64_t *ipackets, zbx_uint64_t *ierrors, zbx_uint64_t *idropped,
                     zbx_uint64_t *obytes, zbx_uint64_t *opackets, zbx_uint64_t *oerrors,
                     zbx_uint64_t *tbytes, zbx_uint64_t *tpackets, zbx_uint64_t *terrors,
                     zbx_uint64_t *icollisions, char **error)
{
	/* 定义一个结构体链表头 */
	struct ifnet_head head;
	/* 定义一个指向ifnet结构的指针 */
	struct ifnet *ifp;

	/* 打开内核符号表 */
	kvm_t *kp;
	/* 初始化变量len和ret */
	int len = 0;
	int ret = SYSINFO_RET_FAIL;

	/* 检查if_name是否为空，如果不为空，继续执行后续操作 */
	if (NULL == if_name || '\0' == *if_name)
	{
		/* 分配内存存储错误信息，并返回失败 */
		*error = zbx_strdup(NULL, "Network interface name cannot be empty.");
		return SYSINFO_RET_FAIL;
	}

	/* if(i)_ibytes;	total number of octets received */
	/* if(i)_ipackets;	packets received on interface */
	/* if(i)_ierrors;	input errors on interface */
	/* if(i)_iqdrops;	dropped on input, this interface */
	/* if(i)_obytes;	total number of octets sent */
	/* if(i)_opackets;	packets sent on interface */
	/* if(i)_oerrors;	output errors on interface */
	/* if(i)_collisions;	collisions on csma interfaces */

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

	if (NULL != (kp = kvm_open(NULL, NULL, NULL, O_RDONLY, NULL))) /* requires root privileges */
	{
		struct ifnet	v;

		if (N_UNDF == kernel_symbols[IFNET_ID].n_type)
			if (0 != kvm_nlist(kp, &kernel_symbols[0]))
				kernel_symbols[IFNET_ID].n_type = N_UNDF;

	/* 如果内核符号表中的IFNET_ID符号不为空，则继续执行后续操作 */
	if (N_UNDF != kernel_symbols[IFNET_ID].n_type)
	{
		/* 初始化变量len，用于读取内核符号表中的数据 */
		len = sizeof(struct ifnet_head);

			if (kvm_read(kp, kernel_symbols[IFNET_ID].n_value, &head, len) >= len)
			{
				len = sizeof(struct ifnet);

				for (ifp = head.tqh_first; ifp; ifp = v.if_list.tqe_next)
				{
					if (kvm_read(kp, (u_long)ifp, &v, len) < len)
						break;

					if (0 == strcmp(if_name, v.if_xname))
					{
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

						ret = SYSINFO_RET_OK;
					}
				}
			}
		}
		kvm_close(kp);

		if (SYSINFO_RET_FAIL == ret)
			*error = zbx_strdup(NULL, "Cannot find information for this network interface.");
	}
	else
	{
		/* fallback to using SIOCGIFDATA */

		int		if_s;
		struct ifreq	ifr;
		struct if_data	v;

		/* 判断socket创建是否成功 */
		if ((if_s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		{
			/* 分配内存存储错误信息，并返回失败 */
			*error = zbx_dsprintf(NULL, "Cannot create socket: %s", zbx_strerror(errno));
			goto clean;
		}

		/* 设置socket参数 */
		zbx_strlcpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);
		ifr.ifr_data = (caddr_t)&v;

		/* 执行ioctl操作，获取接口数据 */
		if (ioctl(if_s, SIOCGIFDATA, &ifr) < 0)
		{
			/* 分配内存存储错误信息，并返回失败 */
			*error = zbx_dsprintf(NULL, "Cannot set socket parameters: %s", zbx_strerror(errno));
			goto clean;
		}

		/* 更新统计数据 */
		if (ibytes)
			*ibytes += v.ifi_ibytes;
		if (ipackets)
			*ipackets += v.ifi_ipackets;
		if (ierrors)
			*ierrors += v.ifi_ierrors;
		if (idropped)
			*idropped += v.ifi_iqdrops;
		if (obytes)
			*obytes += v.ifi_obytes;
		if (opackets)
			*opackets += v.ifi_opackets;
		if (oerrors)
			*oerrors += v.ifi_oerrors;
		if (tbytes)
			*tbytes += v.ifi_ibytes + v.ifi_obytes;
		if (tpackets)
			*tpackets += v.ifi_ipackets + v.ifi_opackets;
		if (terrors)
			*terrors += v.ifi_ierrors + v.ifi_oerrors;
		if (icollisions)
			*icollisions += v.ifi_collisions;

		/* 设置返回值，表示找到了接口数据并更新成功 */
		ret = SYSINFO_RET_OK;
clean:
		/* 关闭socket */
		if (if_s >= 0)
			close(if_s);
	}

	/* 返回更新后的统计数据 */
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取接口的网络数据（如字节数、数据包数、错误数和丢包数），并根据传入的参数模式设置相应的结果。如果传入的参数不合法，则返回错误信息。否则，返回操作成功。
 ******************************************************************************/
// 定义一个名为 NET_IF_IN 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针和整数类型的变量，用于存储接口名称、模式、数据传输字节数、数据包数、错误数和丢包数等参数。
	char *if_name, *mode, *error;
	zbx_uint64_t ibytes, ipackets, ierrors, idropped;

	// 检查 request 参数的数量，如果大于2，则表示参数过多。
	if (2 < request->nparam)
	{
		// 设置结果消息，提示参数过多，并返回 SYSINFO_RET_FAIL 表示操作失败。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取请求参数中的接口名称和模式。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_ifdata 函数获取接口的网络数据，如果操作成功，将返回 SYSINFO_RET_OK，否则返回失败。
	if (SYSINFO_RET_OK != get_ifdata(if_name, &ibytes, &ipackets, &ierrors, &idropped, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, &error))
	{
		// 设置结果消息，传递错误信息，并返回 SYSINFO_RET_FAIL 表示操作失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 检查模式是否合法，如果模式为空或者为空字符串，或者等于 "bytes"（默认参数），则直接设置结果为字节数。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, ibytes);
	else if (0 == strcmp(mode, "packets"))
		// 如果模式为 "packets"，则设置结果为数据包数。
		SET_UI64_RESULT(result, ipackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ierrors);
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, idropped);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针和整数类型的变量，用于后续操作
	char		*if_name, *mode, *error;
	zbx_uint64_t	obytes, opackets, oerrors;

	// 检查传入的请求参数数量是否大于2，如果大于2则表示参数过多
	if (2 < request->nparam)
	{
		// 设置返回结果为 "Too many parameters."，并返回错误码 SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取接口名和模式字符串
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_ifdata 函数获取接口的网络数据信息，并将结果存储在 obytes、opackets 和 oerrors 变量中
	if (SYSINFO_RET_OK != get_ifdata(if_name, NULL, NULL, NULL, NULL, &obytes, &opackets, &oerrors, NULL, NULL,
			NULL, NULL, &error))
	{
		// 如果获取网络数据信息失败，设置返回结果为错误信息，并返回错误码 SYSINFO_RET_FAIL
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
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 NET_IF_TOTAL 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。函数的主要作用是根据请求的模式（字节、数据包或错误）获取接口的统计数据，并将结果输出到 result 指针指向的结构体中。如果请求参数不合法或接口统计数据获取失败，则返回错误信息。
 ******************************************************************************/
// 定义一个名为 NET_IF_TOTAL 的函数，该函数接受两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针和zbx_uint64_t类型的变量，用于存储接口名称、模式、错误信息、流量字节、数据包计数和错误计数。
	char		*if_name, *mode, *error;
	zbx_uint64_t	tbytes, tpackets, terrors;

	// 检查 request 中的参数数量，如果大于2，则返回错误信息，并设置结果为失败。
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取请求的第一个参数（接口名称）
	if_name = get_rparam(request, 0);
	// 获取请求的第二个参数（模式）
	mode = get_rparam(request, 1);

	// 调用 get_ifdata 函数获取接口的统计数据，包括流量字节、数据包计数和错误计数等。
	if (SYSINFO_RET_OK != get_ifdata(if_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &tbytes, &tpackets,
			&terrors, NULL, &error))
	{
		// 如果获取接口统计数据失败，则设置错误信息，并返回失败结果。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 检查模式是否合法，如果模式为空或者等于 "bytes"，则默认输出流量字节。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, tbytes);
	// 如果模式为 "packets"，则输出数据包计数。
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, tpackets);
	// 如果模式为 "errors"，则输出错误计数。
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, terrors);
	// 否则，返回错误信息，并设置结果为失败。
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果没有错误，返回成功结果。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定接口的碰撞次数，并将结果存储在 result 指针中。程序首先检查请求参数的数量，如果过多则返回失败。接着从请求中获取接口名称，然后调用 get_ifdata 函数获取接口碰撞次数和相关数据。如果获取数据成功，将碰撞次数存储在 result 指针中，并返回成功状态。如果失败，则返回失败状态并携带错误信息。
 ******************************************************************************/
// 定义一个名为 NET_IF_COLLISIONS 的函数，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符串指针，分别指向 if_name 和 error，用于存储接口名称和错误信息。
	char *if_name, *error;
	// 定义一个 zbx_uint64_t 类型的变量 icollisions，用于存储接口碰撞次数。
	zbx_uint64_t icollisions;

	// 检查 request 中的参数数量，如果大于1，则表示参数过多。
	if (1 < request->nparam)
	{
		// 设置结果消息，提示参数过多。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态。
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取第一个参数（接口名称），并存储在 if_name 指针中。
	if_name = get_rparam(request, 0);

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
	int i;

	// 定义一个 zbx_json 类型的变量 j，用于存储 JSON 数据。
	struct zbx_json j;

	// 定义一个 if_nameindex 类型的指针 interfaces，用于存储系统接口信息。
	struct if_nameindex *interfaces;

	// 检查 interfaces 是否为空，如果不为空，则执行以下操作：
	if (NULL == (interfaces = if_nameindex()))
	{
		// 设置错误信息，并返回失败状态。
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 初始化 zbx_json 结构体变量 j。
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	// 向 JSON 数据中添加一个数组。
	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	// 遍历 interfaces 数组，获取系统接口信息。
	for (i = 0; 0 != interfaces[i].if_index; i++)
	{
		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, "{#IFNAME}", interfaces[i].if_name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	if_freenameindex(interfaces);

	return SYSINFO_RET_OK;
}
