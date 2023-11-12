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

#include <unistd.h>
#include <stropts.h>
#include <sys/dlpi.h>
#include <sys/dlpi_ext.h>
#include <sys/mib.h>

#include "common.h"
#include "sysinfo.h"
#include "zbxjson.h"

#define PPA(n) (*(dl_hp_ppa_info_t *)(ppa_data_buf + n * sizeof(dl_hp_ppa_info_t)))

static char	buf_ctl[1024];

/* Low Level Discovery needs a way to get the list of network interfaces available */
/* on the monitored system. HP-UX versions starting from 11.31 have if_nameindex() */
/* available in libc, older versions have it in libipv6 which we do not want to    */
/* depend on. So for older versions we use different code to get that list.        */
/* More information:                                                               */
/* h20000.www2.hp.com/bc/docs/support/SupportManual/c02258083/c02258083.pdf        */

static struct strbuf	ctlbuf =
{
	sizeof(buf_ctl),
	0,
	buf_ctl
};

#if HPUX_VERSION < 1131

// 定义一个字符串分隔符，用于分离接口名称
#define ZBX_IF_SEP	','

static void	add_if_name(char **if_list, size_t *if_list_alloc, size_t *if_list_offset, const char *name)
{
	if (FAIL == str_in_list(*if_list, name, ZBX_IF_SEP))
	{
		if ('\0' != **if_list)
			zbx_chrcpy_alloc(if_list, if_list_alloc, if_list_offset, ZBX_IF_SEP);

		zbx_strcpy_alloc(if_list, if_list_alloc, if_list_offset, name);
	}
}

static int	get_if_names(char **if_list, size_t *if_list_alloc, size_t *if_list_offset)
{
	int			s, ifreq_size, numifs, i, family = AF_INET;
	struct sockaddr		*from;
	size_t			fromlen;
	u_char			*buffer = NULL;
	struct ifconf		ifc;
	struct ifreq		*ifr;
	struct if_laddrconf	lifc;
	struct if_laddrreq	*lifr;

	// 创建一个AF_INET家族的套接字
	if (-1 == (s = socket(family, SOCK_DGRAM, 0)))
		return FAIL;

	// 初始化ifc结构体，用于获取网络接口信息
	ifc.ifc_buf = 0;
	ifc.ifc_len = 0;

	// 尝试获取网络接口信息，并将其存储在ifc结构体中
	if (0 == ioctl(s, SIOCGIFCONF, (caddr_t)&ifc) && 0 != ifc.ifc_len)
		ifreq_size = 2 * ifc.ifc_len;
	else
		ifreq_size = 2 * 512;

	// 分配内存存储网络接口信息
	buffer = zbx_malloc(buffer, ifreq_size);
	memset(buffer, 0, ifreq_size);

	// 将ifc结构体的缓冲区指针指向分配的内存
	ifc.ifc_buf = (caddr_t)buffer;
	ifc.ifc_len = ifreq_size;

	// 再次尝试获取网络接口信息
	if (-1 == ioctl(s, SIOCGIFCONF, &ifc))
		goto next;

	/* 遍历所有IPv4接口，并将接口名称添加到if_list中 */
	ifr = (struct ifreq *)ifc.ifc_req;
	while ((u_char *)ifr < (u_char *)(buffer + ifc.ifc_len))
	{
		from = &ifr->ifr_addr;

		// 仅处理IPv4和IPv6接口
		if (AF_INET6 != from->sa_family && AF_INET != from->sa_family)
			continue;

		// 将接口名称添加到if_list中
		add_if_name(if_list, if_list_alloc, if_list_offset, ifr->ifr_name);

#ifdef _SOCKADDR_LEN
		ifr = (struct ifreq *)((char *)ifr + sizeof(*ifr) + (from->sa_len > sizeof(*from) ? from->sa_len - sizeof(*from) : 0));
#else
		ifr++;
#endif
	}
next:
	// 释放分配的内存
	zbx_free(buffer);
	// 关闭套接字
	close(s);

#if defined (SIOCGLIFCONF)
	// 切换到AF_INET6家族的套接字
	family = AF_INET6;

	// 创建一个AF_INET6家族的套接字
	if (-1 == (s = socket(family, SOCK_DGRAM, 0)))
		return FAIL;

	// 获取IPv6接口数量
	i = ioctl(s, SIOCGLIFNUM, (char *)&numifs);
	// 如果接口数量为0，则直接返回SUCCEED
	if (0 == numifs)
	{
		close(s);
		return SUCCEED;
	}

	// 分配内存存储IPv6接口信息
	lifc.iflc_len = numifs * sizeof(struct if_laddrreq);
	lifc.iflc_buf = zbx_malloc(NULL, lifc.iflc_len);
	buffer = (u_char *)lifc.iflc_buf;

	// 获取IPv6接口信息
	if (-1 == ioctl(s, SIOCGLIFCONF, &lifc))
		goto end;

	/* 遍历所有IPv6接口，并将接口名称添加到if_list中 */
	for (lifr = lifc.iflc_req; '\0' != *lifr->iflr_name; lifr++)
	{
		from = (struct sockaddr *)&lifr->iflr_addr;

		// 仅处理IPv4和IPv6接口
		if (AF_INET6 != from->sa_family && AF_INET != from->sa_family)
			continue;

		// 将接口名称添加到if_list中
		add_if_name(if_list, if_list_alloc, if_list_offset, lifr->iflr_name);
	}
end:
	// 释放分配的内存
	zbx_free(buffer);
	// 关闭套接字
	close(s);
#endif
	return SUCCEED;
}

#endif	/* HPUX_VERSION < 1131 */

int	NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 初始化一个json结构体
	struct zbx_json j;
	
	// 定义一个字符指针，用于存储网络接口名称
	char *if_name;

	// 针对HPUX系统版本小于1131的情况，使用get_if_names函数获取网络接口名称列表
#if HPUX_VERSION < 1131
	char *if_list = NULL, *if_name_end;
	size_t if_list_alloc = 64, if_list_offset = 0;

	// 分配内存用于存储网络接口名称列表
	if_list = zbx_malloc(if_list, if_list_alloc);
	*if_list = '\0';

	// 获取网络接口名称列表，如果失败则返回错误信息
	if (FAIL == get_if_names(&if_list, &if_list_alloc, &if_list_offset))
	{
		// 设置错误结果
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain network interface information."));
		// 释放内存
		zbx_free(if_list);
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 初始化json结构体
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	// 添加一个数组到json结构体中
	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	// 遍历网络接口名称列表，将其添加到json结构体中
	if_name = if_list;

	while (NULL != if_name)
	{
		// 查找下一个接口名称的分隔符位置
		if (NULL != (if_name_end = strchr(if_name, ZBX_IF_SEP)))
			*if_name_end = '\0';

		// 添加一个对象到json结构体中
		zbx_json_addobject(&j, NULL);
		// 添加接口名称到json结构体中
		zbx_json_addstring(&j, "{#IFNAME}", if_name, ZBX_JSON_TYPE_STRING);
		// 关闭当前对象
		zbx_json_close(&j);

		// 找到下一个接口名称
		if (NULL != if_name_end)
		{
			*if_name_end = ZBX_IF_SEP;
			if_name = if_name_end + 1;
		}
		else
			if_name = NULL;
	}

	// 释放内存
	zbx_free(if_list);
#else
	// 针对其他系统，使用if_nameindex函数获取网络接口名称列表
	struct if_nameindex *ni;
	int i;

	// 获取系统信息，如果失败则返回错误信息
	if (NULL == (ni = if_nameindex()))
	{
		// 设置错误结果
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 初始化json结构体
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	// 添加一个数组到json结构体中
	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	// 遍历网络接口名称列表，将其添加到json结构体中
	for (i = 0; 0 != ni[i].if_index; i++)
	{
		// 添加一个对象到json结构体中
		zbx_json_addobject(&j, NULL);
		// 添加接口名称到json结构体中
		zbx_json_addstring(&j, "{#IFNAME}", ni[i].if_name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);
	}

	if_freenameindex(ni);
#endif
	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	zbx_json_free(&j);

	return SYSINFO_RET_OK;
}

/* attaches to a PPA via an already open stream to DLPI provider */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现与指定PPA的连接。首先，创建一个连接请求结构体attach_req，并设置其dl_primitive字段为DL_ATTACH_REQ，表示连接请求。然后，向文件描述符fd发送连接请求。接着，重新设置ctlbuf结构体，用于接收响应数据。从文件描述符fd接收响应数据，并检查响应数据，判断连接是否成功。如果连接成功，返回SUCCEED；否则，返回FAIL。
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为dlpi_attach。该函数的主要目的是实现与PPA（物理端点附件）的连接。
 * 输入参数：
 *   fd：一个整数，表示客户端文件描述符
 *   ppa：一个整数，表示要连接的PPA编号
 * 返回值：
 *   如果连接成功，返回SUCCEED；否则，返回FAIL
 */
static int	dlpi_attach(int fd, int ppa)
{
	/* 定义一个结构体dl_attach_req_t，用于存储连接请求信息 */
	dl_attach_req_t		attach_req;
	int			flags = RS_HIPRI;

	/* 设置attach_req结构体的dl_primitive字段为DL_ATTACH_REQ，表示连接请求 */
	attach_req.dl_primitive = DL_ATTACH_REQ;
	/* 设置attach_req结构体的dl_ppa字段为传入的ppa参数 */
	attach_req.dl_ppa = ppa;

	/* 初始化ctlbuf结构体，用于存储发送和接收的数据 */
	ctlbuf.len = sizeof(attach_req);
	ctlbuf.buf = (char *)&attach_req;

	/* 向文件描述符fd发送连接请求，使用putmsg函数 */
	if (0 != putmsg(fd, &ctlbuf, NULL, flags))
		return FAIL;

	/* 重新设置ctlbuf的结构，准备接收响应数据 */
	ctlbuf.buf = buf_ctl;
	ctlbuf.maxlen = sizeof(buf_ctl);

	/* 从文件描述符fd接收响应数据，使用getmsg函数 */
	if (0 > getmsg(fd, &ctlbuf, NULL, &flags))
		return FAIL;

	/* 检查接收到的响应数据，判断连接是否成功 */
	if (DL_OK_ACK != *(int *)buf_ctl)
		return FAIL;

	/* 如果连接成功，返回SUCCEED */
	return SUCCEED;
}


/* Detaches from a PPA via an already open stream to DLPI provider. */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 dlpi_detach 的函数，该函数用于断开与指定文件描述符（fd）相关的连接。函数接收一个整数参数 fd，并返回一个整数表示操作结果。如果成功断开连接，返回 SUCCEED；否则，返回 FAIL。
 *
 *为实现此目的，代码块首先定义了一个名为 detach_req 的 dl_detach_req_t 类型结构体变量，并设置了其 dl_primitive 值为 DL_DETACH_REQ。接着，设置了一个名为 ctlbuf 的结构体变量，用于存储要发送和接收的消息。
 *
 *然后，通过 putmsg 函数向 fd 发送一个包含 detach_req 的消息，发送标志为 flags。接下来，设置 ctlbuf 的 buf 为 buf_ctl，并从 fd 接收一个消息，存储在 ctlbuf 中。判断接收到的消息是否为 DL_OK_ACK，如果不是，返回 FAIL。最后，如果成功断开连接，返回 SUCCEED。
 ******************************************************************************/
/* 定义一个名为 dlpi_detach 的静态函数，接收一个整数参数 fd，返回值为 int 类型 */
static int	dlpi_detach(int fd)
{
	/* 定义一个名为 detach_req 的 dl_detach_req_t 类型结构体变量 */
	dl_detach_req_t		detach_req;
	/* 定义一个名为 flags 的整数变量，初始值为 RS_HIPRI */
	int			flags = RS_HIPRI;

	/* 设置 detach_req 结构体中的 dl_primitive 值为 DL_DETACH_REQ */
	detach_req.dl_primitive = DL_DETACH_REQ;

	/* 设置 ctlbuf 结构体的 len 为 detach_req 的大小，buf 为 detach_req 的地址 */
	ctlbuf.len = sizeof(detach_req);
	ctlbuf.buf = (char *)&detach_req;

	/* 向 fd 发送一个消息，消息内容为 ctlbuf，发送标志为 flags，若发送失败，返回 FAIL */
	if (0 != putmsg(fd, &ctlbuf, NULL, flags))
		return FAIL;

	/* 设置 ctlbuf 的 buf 为 buf_ctl，maxlen 为 buf_ctl 的大小 */
	ctlbuf.buf = buf_ctl;
	ctlbuf.maxlen = sizeof(buf_ctl);

	/* 从 fd 接收一个消息，消息内容为 ctlbuf，若接收失败，返回 FAIL */
	if (0 > getmsg(fd, &ctlbuf, NULL, &flags))
		return FAIL;

	/* 判断接收到的消息是否为 DL_OK_ACK，如果不是，返回 FAIL */
	if (DL_OK_ACK != *(int *)buf_ctl)
		return FAIL;

	/* 如果成功断开连接，返回 SUCCEED */
	/* 这里可以理解为成功断开连接的标志位 */
	return SUCCEED;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是从一个设备（通过fd参数指定）获取统计信息，并将获取到的统计信息存储在Ext_mib_t类型的结构体变量mib中。为了实现这个目的，代码首先发送一个DL_GET_STATISTICS_REQ类型的请求，然后接收响应数据并检查响应数据的类型是否为DL_GET_STATISTICS_ACK。如果检查通过，将响应数据解析为dl_get_statistics_ack_t类型的结构体变量stat_msg，并将stat_msg中的数据复制到mib指向的Ext_mib_t结构体中。最后，如果没有发生错误，返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数dlpi_get_stats，接收两个参数，一个是整型变量fd，另一个是指向Ext_mib_t结构体的指针mib。
static int dlpi_get_stats(int fd, Ext_mib_t *mib)
{
	// 定义一个dl_get_statistics_req_t类型的结构体变量stat_req，用于存储请求数据。
	dl_get_statistics_req_t		stat_req;
	// 定义一个dl_get_statistics_ack_t类型的结构体变量stat_msg，用于存储响应数据。
	dl_get_statistics_ack_t		stat_msg;
	int				flags = RS_HIPRI;

	stat_req.dl_primitive = DL_GET_STATISTICS_REQ;

	ctlbuf.len = sizeof(stat_req);
	ctlbuf.buf = (char *)&stat_req;

	if (0 != putmsg(fd, &ctlbuf, NULL, flags))
		return FAIL;

	ctlbuf.buf = buf_ctl;
	ctlbuf.maxlen = sizeof(buf_ctl);

	if (0 > getmsg(fd, &ctlbuf, NULL, &flags))
		return FAIL;

	if (DL_GET_STATISTICS_ACK != *(int *)buf_ctl)
		return FAIL;

	stat_msg = *(dl_get_statistics_ack_t *)buf_ctl;

	memcpy(mib, (Ext_mib_t *)(buf_ctl + stat_msg.dl_stat_offset), sizeof(Ext_mib_t));

	return SUCCEED;
}

static int get_ppa(int fd, const char *if_name, int *ppa)
{
	/* 定义一个结构体，用于发送给内核的请求 */
	dl_hp_ppa_req_t		ppa_req;
	/* 定义一个结构体指针，用于接收内核返回的响应 */
	dl_hp_ppa_ack_t		*dlp;
	/* 定义循环变量 */
	int			i, ret = FAIL, flags = RS_HIPRI, res;
	/* 定义缓冲区 */
	char			*buf = NULL, *ppa_data_buf = NULL;

	/* 设置请求结构体的初始值 */
	ppa_req.dl_primitive = DL_HP_PPA_REQ;

	/* 设置控制缓冲区，包括缓冲区长度和指向请求结构体的指针 */
	ctlbuf.len = sizeof(ppa_req);
	ctlbuf.buf = (char *)&ppa_req;

	/* 向内核发送请求 */
	if (0 != putmsg(fd, &ctlbuf, NULL, flags))
		return ret;

	/* 重新设置控制缓冲区，包括缓冲区长度和指向缓冲区的指针 */
	ctlbuf.buf = buf_ctl;
	ctlbuf.maxlen = DL_HP_PPA_ACK_SIZE;

	/* 接收内核响应 */
	res = getmsg(fd, &ctlbuf, NULL, &flags);

	/* 首先获取响应的头部 */
	if (0 > res)
		return ret;

	/* 解析响应头，获取PPA响应 */
	dlp = (dl_hp_ppa_ack_t *)ctlbuf.buf;

	/* 检查响应类型是否为PPA响应 */
	if (DL_HP_PPA_ACK != dlp->dl_primitive)
		return ret;

	/* 检查响应长度是否符合预期 */
	if (DL_HP_PPA_ACK_SIZE > ctlbuf.len)
		return ret;

	/* 判断响应是否分段 */
	if (MORECTL == res)
	{
		size_t	if_name_sz = strlen(if_name) + 1;

		ctlbuf.maxlen = dlp->dl_count * sizeof(dl_hp_ppa_info_t);
		ctlbuf.len = 0;

		ppa_data_buf = zbx_malloc(ppa_data_buf, (size_t)ctlbuf.maxlen);

		ctlbuf.buf = ppa_data_buf;

		/* get the data */
		if (0 > getmsg(fd, &ctlbuf, NULL, &flags) || ctlbuf.len < dlp->dl_length)
		{
			zbx_free(ppa_data_buf);
			return ret;
		}

		buf = zbx_malloc(buf, if_name_sz);

		for (i = 0; i < dlp->dl_count; i++)
		{
			zbx_snprintf(buf, if_name_sz, "%s%d", PPA(i).dl_module_id_1, PPA(i).dl_ppa);

			if (0 == strcmp(if_name, buf))
			{
				*ppa = PPA(i).dl_ppa;
				ret = SUCCEED;
				break;
			}
		}

		zbx_free(buf);
		zbx_free(ppa_data_buf);
	}

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定接口的网络统计信息。函数 `get_net_stat` 接收一个 Ext_mib_t 结构体的指针和一个接口名称，通过一系列操作（打开设备文件、获取 PPA、挂载 PPA、获取网络统计信息、卸载 PPA、关闭文件描述符）来完成这个任务。如果过程中出现任何错误，函数将返回 FAIL，否则返回 SUCCEED。
 ******************************************************************************/
/*
 * 函数名：get_net_stat
 * 参数：
 *   - mib：一个指向Ext_mib_t结构体的指针，用于存储网络统计信息
 *   - if_name：一个指向字符串的指针，表示要获取网络统计信息的接口名称
 * 返回值：
 *   - 成功：SUCCEED
 *   - 失败：FAIL
 */
static int	get_net_stat(Ext_mib_t *mib, const char *if_name)
{
	// 声明变量
	int	fd, ppa;

	// 打开设备文件 "/dev/dlpi"，参数 O_RDWR 表示读写模式
	if (-1 == (fd = open("/dev/dlpi", O_RDWR)))
		// 打开失败，返回 FAIL
		return FAIL;

	// 调用 get_ppa 函数获取接口的物理端口地址（PPA），并将结果存储在 ppa 变量中
	if (FAIL == get_ppa(fd, if_name, &ppa))
	{
		close(fd);
		return FAIL;
	}

	if (FAIL == dlpi_attach(fd, ppa))
		return FAIL;

	if (FAIL == dlpi_get_stats(fd, mib))
		return FAIL;

	dlpi_detach(fd);

	close(fd);

	return SUCCEED;
}

int	NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针变量，分别为 if_name 和 mode，用于存储从请求中获取的网络接口名称和模式。
	char *if_name, *mode;
	// 定义一个 Ext_mib_t 类型的变量 mib，用于存储网络接口信息。
	Ext_mib_t mib;

	// 检查请求参数的数量是否大于2，如果是，则返回错误信息。
	if (2 < request->nparam)
	{
		// 设置结果对象的错误信息为 "Too many parameters."，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (FAIL == get_net_stat(&mib, if_name))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain network interface information."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, mib.mib_if.ifInOctets);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, mib.mib_if.ifInUcastPkts + mib.mib_if.ifInNUcastPkts);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, mib.mib_if.ifInErrors);
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, mib.mib_if.ifInDiscards);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *mode;
	Ext_mib_t	mib;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取接口名和模式 */
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	/* 调用函数获取网络接口统计信息，并将结果存储在 mib 结构体中 */
	if (FAIL == get_net_stat(&mib, if_name))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain network interface information."));
		return SYSINFO_RET_FAIL;
	}

	/* 判断模式是否合法，如果是合法模式，则设置结果 */
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, mib.mib_if.ifOutOctets);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, mib.mib_if.ifOutUcastPkts + mib.mib_if.ifOutNUcastPkts);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, mib.mib_if.ifOutErrors);
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, mib.mib_if.ifOutDiscards);
	// 如果 mode 参数不符合上述条件，返回错误信息并退出。
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果一切正常，返回 SYSINFO_RET_OK，表示成功。
	return SYSINFO_RET_OK;
}


int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *mode;
	Ext_mib_t	mib;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (FAIL == get_net_stat(&mib, if_name))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain network interface information."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
	{
		SET_UI64_RESULT(result, mib.mib_if.ifInOctets + mib.mib_if.ifOutOctets);
	}
	else if (0 == strcmp(mode, "packets"))
	{
		SET_UI64_RESULT(result, mib.mib_if.ifInUcastPkts + mib.mib_if.ifInNUcastPkts
				+ mib.mib_if.ifOutUcastPkts + mib.mib_if.ifOutNUcastPkts);
	}
	else if (0 == strcmp(mode, "errors"))
	{
		SET_UI64_RESULT(result, mib.mib_if.ifInErrors + mib.mib_if.ifOutErrors);
	}
	else if (0 == strcmp(mode, "dropped"))
	{
		SET_UI64_RESULT(result, mib.mib_if.ifInDiscards + mib.mib_if.ifOutDiscards);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
