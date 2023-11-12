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
#include "../common/common.h"
#include "log.h"

/******************************************************************************
 * *
 *整个代码块的主要目的是获取内核统计数据中指定接口的网络统计字段值。该函数名为 `get_kstat_named_field`，接收四个参数：接口名称 `name`、字段名 `field`、存储字段值的指针 `field_value` 和错误信息指针 `error`。函数首先打开内核统计数据设施，然后遍历所有内核统计数据链表，寻找指定接口并找到最小实例。接着读取内核统计数据，查找指定字段的数据，并获取数值字段的值。最后关闭内核统计数据设施，返回成功或错误信息。
 ******************************************************************************/
/* 定义一个函数，用于获取内核统计数据中指定接口的网络统计字段值 */
static int	get_kstat_named_field(const char *name, const char *field, zbx_uint64_t *field_value, char **error)
{
	/* 声明变量 */
	int		ret = FAIL, min_instance = -1;
	kstat_ctl_t	*kc;
	kstat_t		*kp, *min_kp;
	kstat_named_t	*kn;

	/* 打开内核统计数据设施 */
	if (NULL == (kc = kstat_open()))
	{
		*error = zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s", zbx_strerror(errno));
		return FAIL;
	}

	/* 遍历所有内核统计数据链表 */
	for (kp = kc->kc_chain; NULL != kp; kp = kp->ks_next)
	{
		/* 检查接口名称是否匹配 */
		if (0 != strcmp(name, kp->ks_name))
			continue;

		/* 检查类别是否为 "net" */
		if (0 != strcmp("net", kp->ks_class))
			continue;

		/* 寻找最小实例 */
		if (-1 == min_instance || kp->ks_instance < min_instance)
		{
			min_instance = kp->ks_instance;
			min_kp = kp;
		}

		if (0 == min_instance)
			break;
	}

	/* 如果有找到最小实例，则使用该实例 */
	if (-1 != min_instance)
		kp = min_kp;

	/* 如果没有找到指定接口，返回错误信息 */
	if (NULL == kp)
	{
		*error = zbx_dsprintf(NULL, "Cannot look up interface \"%s\" in kernel statistics facility", name);
		goto clean;
	}

	/* 读取内核统计数据 */
	if (-1 == kstat_read(kc, kp, 0))
	{
		*error = zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s", zbx_strerror(errno));
		goto clean;
	}

	/* 查找指定字段的数据 */
	if (NULL == (kn = (kstat_named_t *)kstat_data_lookup(kp, (char *)field)))
	{
		*error = zbx_dsprintf(NULL, "Cannot look up data in kernel statistics facility: %s",
				zbx_strerror(errno));
		goto clean;
	}

	/* 获取数值字段的值 */
	*field_value = get_kstat_numeric_value(kn);

	/* 标记成功 */
	ret = SUCCEED;
clean:
	/* 关闭内核统计数据设施 */
	kstat_close(kc);

	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定网络接口的接收字节数，并将查询结果存储在传入的 AGENT_RESULT 结构体中。如果查询失败，返回 SYSINFO_RET_FAIL 表示失败，并设置错误信息。如果查询成功，返回 SYSINFO_RET_OK 表示成功，并将查询到的字节数存储在结果中。
 ******************************************************************************/
// 定义一个名为 NET_IF_IN_BYTES 的静态函数，该函数接收两个参数：一个 const char 类型的指针 if_name，用于表示网络接口的名称；另一个是 AGENT_RESULT 类型的指针 result，用于存储查询结果。
static int NET_IF_IN_BYTES(const char *if_name, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储查询到的数据。
	zbx_uint64_t value;
	// 定义一个 char 类型的指针 error，用于存储错误信息。
	char *error;

	// 使用 get_kstat_named_field 函数查询网络接口的接收字节数，将查询结果存储在 value 变量中，并将错误信息存储在 error 指针中。
	if (SUCCEED != get_kstat_named_field(if_name, "rbytes64", &value, &error))
	{
		// 如果查询失败，设置结果错误信息为 error，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}
	else if (0 == value && SUCCEED != get_kstat_named_field(if_name, "rbytes", &value, &error))
	{
		// 如果查询字节数失败，设置结果错误信息为 error，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 如果查询成功，将 value 存储在结果中，并返回 SYSINFO_RET_OK 表示成功。
	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定网络接口的 ipackets64 和 ipackets 字段值，并将查询结果存储在 result 结构体中。如果查询过程中出现错误，则设置 result 的错误信息并返回 SYSINFO_RET_FAIL。
 ******************************************************************************/
/* 定义一个名为 NET_IF_IN_PACKETS 的静态函数，接收两个参数：一个字符串指针 if_name，用于表示网络接口的名称；一个 AGENT_RESULT 类型的指针 result，用于存储查询结果。

*/
static int	NET_IF_IN_PACKETS(const char *if_name, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储查询到的数据。
	 * 定义一个字符串指针 error，用于存储错误信息。
	 */
	zbx_uint64_t	value;
	char		*error;

	/* 调用 get_kstat_named_field 函数，查询网络接口名为 if_name 的 "ipackets64" 字段值。
	 * 如果查询失败，设置 result 的错误信息为 error，并返回 SYSINFO_RET_FAIL。
	 */
	if (SUCCEED != get_kstat_named_field(if_name, "ipackets64", &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}
	/* 如果查询到的 ipackets64 为 0，则继续查询 "ipackets" 字段值。
	 * 如果查询失败，设置 result 的错误信息为 error，并返回 SYSINFO_RET_FAIL。
	 */
	else if (0 == value && SUCCEED != get_kstat_named_field(if_name, "ipackets", &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}
	/* 查询成功，将 value 存储到 result 中，并返回 SYSINFO_RET_OK。
	 */
	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定网络接口的错误计数（ierrors 字段），并将错误计数存储在 result 指针所指向的结果对象中。如果获取字段值失败，将错误信息设置为 result 对象的错误信息，并返回失败状态。否则，将错误计数设置为 result 对象的值，并返回成功状态。
 ******************************************************************************/
/* 定义一个名为 NET_IF_IN_ERRORS 的静态函数，接收两个参数：一个 const char 类型的 if_name 字符串，以及一个 AGENT_RESULT 类型的指针 result。
*/
static int	NET_IF_IN_ERRORS(const char *if_name, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储错误计数。
	*/
	zbx_uint64_t	value;
	/* 定义一个 char 类型的变量 error，用于存储错误信息。
	*/
	char		*error;

	/* 调用 get_kstat_named_field 函数，获取指定接口（if_name）的 "ierrors" 字段值。
	 * 参数1：if_name 接口名称
	 * 参数2：要获取的字段名："ierrors"
	 * 参数3：存储字段值的指针 value
	 * 参数4：存储错误信息的指针 error
	 * 返回值：若成功，返回 SUCCEED；若失败，返回失败代码
	*/
	if (SUCCEED != get_kstat_named_field(if_name, "ierrors", &value, &error))
	{
		/* 如果获取字段值失败，设置结果对象的错误信息为 error，并返回失败状态。
		 * 这里的目的是为了将错误信息传递给调用者，以便于进行错误处理。
		*/
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	/* 设置结果对象的值为目标字段值（即错误计数 value）。
	 * 这里的目的是为了将错误计数返回给调用者，以便于进行后续处理。
	*/
	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	NET_IF_OUT_BYTES(const char *if_name, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储取得的 kstat 数据。
	 * 定义一个 char 类型的变量 error，用于存储错误信息。
	 */
	zbx_uint64_t	value;
	char		*error;

	if (SUCCEED != get_kstat_named_field(if_name, "obytes64", &value, &error))
	{
		/* 如果获取失败，设置 result 的错误信息为 error，并返回 SYSINFO_RET_FAIL，表示操作失败。
		 */
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}
	else if (0 == value && SUCCEED != get_kstat_named_field(if_name, "obytes", &value, &error))
	{
		// 如果查询 "obytes" 字段失败，设置结果消息为错误信息，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 如果成功查询到数据，将 value 变量设置为结果，并返回 SYSINFO_RET_OK 表示成功。
	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}


static int	NET_IF_OUT_PACKETS(const char *if_name, AGENT_RESULT *result)
{
	zbx_uint64_t	value;
	char		*error;

	if (SUCCEED != get_kstat_named_field(if_name, "opackets64", &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}
	else if (0 == value && SUCCEED != get_kstat_named_field(if_name, "opackets", &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定网络接口的 \"oerrors\" 字段值，并将查询结果存储在 result 指针指向的结构体中。如果查询失败，则设置 result 中的错误信息并为返回值赋值为 SYSINFO_RET_FAIL；如果查询成功，则将查询到的值存储在 result 中的相应字段，并返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个名为 NET_IF_OUT_ERRORS 的静态函数，该函数接收两个参数：一个 const char 类型的指针 if_name，用于表示网络接口的名称；另一个是 AGENT_RESULT 类型的指针 result，用于存储查询到的数据。
static int NET_IF_OUT_ERRORS(const char *if_name, AGENT_RESULT *result)
{
	zbx_uint64_t	value;
	char		*error;

	if (SUCCEED != get_kstat_named_field(if_name, "oerrors", &value, &error))
	{
		// 如果查询失败，设置 result 中的错误信息为 error，并返回 SYSINFO_RET_FAIL，表示查询失败。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	NET_IF_TOTAL_BYTES(const char *if_name, AGENT_RESULT *result)
{
	zbx_uint64_t	value_in, value_out;
	char		*error;

	// 检查从 kstat 获取接口输入字节数是否成功
	if (SUCCEED != get_kstat_named_field(if_name, "rbytes64", &value_in, &error)
			|| SUCCEED != get_kstat_named_field(if_name, "obytes64", &value_out, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}
	else if ((0 == value_in && SUCCEED != get_kstat_named_field(if_name, "rbytes", &value_in, &error)) ||
			(0 == value_out && SUCCEED != get_kstat_named_field(if_name, "obytes", &value_out, &error)))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 将输入和输出字节数相加，并将结果存储在 result 指向的 AGENT_RESULT 结构体中
	SET_UI64_RESULT(result, value_in + value_out);

	return SYSINFO_RET_OK;
}

static int	NET_IF_TOTAL_PACKETS(const char *if_name, AGENT_RESULT *result)
{
	zbx_uint64_t	value_in, value_out;
	char		*error;

	if (SUCCEED != get_kstat_named_field(if_name, "ipackets64", &value_in, &error) // 尝试使用 get_kstat_named_field 函数获取网络接口的 ipackets64 字段值，并将结果存储在 value_in 中，同时存储错误信息到 error 变量中。
			|| SUCCEED != get_kstat_named_field(if_name, "opackets64", &value_out, &error))
	{
		SET_MSG_RESULT(result, error); // 如果获取数据包数量失败，设置结果消息为 error，并返回 SYSINFO_RET_FAIL 表示失败。
		return SYSINFO_RET_FAIL;
	}
	else if ((0 == value_in && SUCCEED != get_kstat_named_field(if_name, "ipackets", &value_in, &error)) || // 如果 ipackets64 值为 0，尝试使用 get_kstat_named_field 函数获取 ipackets 字段值，并将结果存储在 value_in 中，同时存储错误信息到 error 变量中。
			(0 == value_out && SUCCEED != get_kstat_named_field(if_name, "opackets", &value_out, &error)))
	{
		SET_MSG_RESULT(result, error); // 如果获取数据包数量失败，设置结果消息为 error，并返回 SYSINFO_RET_FAIL 表示失败。
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value_in + value_out);

	return SYSINFO_RET_OK;
}

static int	NET_IF_TOTAL_ERRORS(const char *if_name, AGENT_RESULT *result)
{
	// 定义两个 zbx_uint64_t 类型的变量 value_in 和 value_out，用于存储接口统计数据。
	zbx_uint64_t value_in, value_out;
	// 定义一个字符指针 error，用于存储获取 kstat 数据时可能产生的错误信息。
	char *error;

	// 调用 get_kstat_named_field 函数获取接口的 "ierrors" 和 "oerrors" 字段值，并将结果存储在 value_in 和 value_out 变量中。
	if (SUCCEED != get_kstat_named_field(if_name, "ierrors", &value_in, &error) ||
			SUCCEED != get_kstat_named_field(if_name, "oerrors", &value_out, &error))
	{
		// 如果获取数据失败，设置 result 指向的 AGENT_RESULT 结构体的 msg 字段值为错误信息 error。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示获取接口统计数据失败。
		return SYSINFO_RET_FAIL;
	}

	// 将 value_in 和 value_out 变量相加，并将结果存储在 result 指向的 AGENT_RESULT 结构体的 u64_value 字段中。
	SET_UI64_RESULT(result, value_in + value_out);

	// 返回 SYSINFO_RET_OK，表示成功获取接口统计数据。
	return SYSINFO_RET_OK;
}

int	NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储接口碰撞计数器的值。
	zbx_uint64_t	value;
	// 定义两个字符串指针，分别为 if_name 和 error，用于存储接口名称和错误信息。
	char		*if_name, *error;

	// 检查 request 中的参数数量是否大于1，如果大于1，则设置结果消息为 "Too many parameters."，并返回 SYSINFO_RET_FAIL。
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取第一个参数（即接口名称），并存储在 if_name 指针中。
	if_name = get_rparam(request, 0);

	// 检查 if_name 是否为空，或者 if_name 第一个字符为 '\0'，如果是，则设置结果消息为 "Invalid first parameter."，并返回 SYSINFO_RET_FAIL。
	if (NULL == if_name || '\0' == *if_name)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 使用 get_kstat_named_field 函数获取接口名称对应的 kstat 结构中的 "collisions" 字段值，并将结果存储在 value 变量中，同时存储错误信息到 error 指针中。
	if (SUCCEED != get_kstat_named_field(if_name, "collisions", &value, &error))
	{
		// 如果获取 "collisions" 字段失败，则使用 error 指针存储错误信息，并返回 SYSINFO_RET_FAIL。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 将 value 变量转换为 UI64 类型，并存储在 result 中的相应位置。
	SET_UI64_RESULT(result, value);

	// 如果没有发生错误，返回 SYSINFO_RET_OK，表示成功获取接口碰撞计数器值。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定端口（通过端口字符串传入）上开放的TCP连接数量，并将结果返回给调用者。以下是详细注释：
 *
 *1. 定义两个字符指针变量port_str和command，一个无符号短整型变量port，以及一个整型变量res，用于存储函数返回值。
 *2. 判断请求参数的数量是否大于1，如果是，则返回错误信息。
 *3. 获取第一个参数（端口字符串）并存储在port_str变量中。
 *4. 判断端口字符串是否为空，或者无法将字符串转换为无符号短整型，如果是，则返回错误信息。
 *5. 格式化命令字符串，用于查询指定端口开放的TCP连接数量。
 *6. 执行命令，并将结果存储在result变量中。
 *7. 如果查询到的开放连接数量大于1，则将结果清零，仅返回1。
 *8. 返回函数执行结果。
 ******************************************************************************/
int NET_TCP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义字符指针变量，用于存储端口字符串和命令字符串
	char *port_str, command[64];
	// 定义无符号短整型变量，用于存储端口
	unsigned short port;
	// 定义整型变量，用于存储函数返回值
	int res;

	// 判断请求参数的数量是否大于1，如果是，则返回错误信息
	if (1 < request->nparam)
	{
		// 设置返回结果为“参数过多”错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回系统信息错误码：SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（端口字符串）
	port_str = get_rparam(request, 0);

	// 判断端口字符串是否为空，或者无法将字符串转换为无符号短整型
	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		// 设置返回结果为“第一个参数无效”错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回系统信息错误码：SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 格式化命令字符串，用于查询指定端口开放的TCP连接数量
	zbx_snprintf(command, sizeof(command), "netstat -an -P tcp | grep '\\.%hu[^.].*LISTEN' | wc -l", port);

	// 执行命令，并将结果存储在result变量中
	if (SYSINFO_RET_FAIL == (res = EXECUTE_INT(command, result)))
		// 如果执行命令失败，直接返回错误码
		return res;

	// 如果查询到的开放连接数量大于1，则将结果清零，仅返回1
	if (1 < result->ui64)
		result->ui64 = 1;

	// 返回函数执行结果
	return res;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是监听指定的UDP端口，并返回该端口上的空闲连接数。为实现这个目的，代码首先检查参数数量，如果超过1个，则返回错误信息。接着获取第一个参数（端口字符串），检查其是否合法，将其转换为短整型。然后构造一个命令字符串，用于查询端口上的UDP连接数。执行该命令并将结果存储在result变量中，如果结果不为1，则将其设置为1。最后返回执行结果。
 ******************************************************************************/
// 定义一个函数，用于监听UDP端口并获取空闲连接数
int NET_UDP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*port_str, command[64];
	unsigned short	port;
	int		res;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（端口字符串）
	port_str = get_rparam(request, 0);

	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 构造命令字符串，用于查询端口上的UDP连接数
	zbx_snprintf(command, sizeof(command), "netstat -an -P udp | grep '\\.%hu[^.].*Idle' | wc -l", port);

	if (SYSINFO_RET_FAIL == (res = EXECUTE_INT(command, result)))
		return res;

	if (1 < result->ui64)
		result->ui64 = 1;

	// 返回执行结果
	return res;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收 AGENT_REQUEST 类型的请求参数，根据请求参数中的接口名和模式，调用相应的 NET_IF_IN_* 函数计算接口的统计数据，并将结果存储在 AGENT_RESULT 类型的结果对象中。最后返回计算得到的接口统计数据。
 ******************************************************************************/
int	NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{ // 定义一个名为 NET_IF_IN 的函数，接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result

	char	*if_name, *mode; // 声明两个字符指针变量 if_name 和 mode，用于存储接口名和模式
	int	ret; // 声明一个整型变量 ret，用于存储返回值

	if (2 < request->nparam) // 如果请求参数的数量大于2
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters.")); // 设置返回结果为 "参数过多"，并返回失败码
		return SYSINFO_RET_FAIL; // 结束函数调用
	}

	if_name = get_rparam(request, 0); // 从请求参数中获取接口名，存入 if_name
	mode = get_rparam(request, 1); // 从请求参数中获取模式，存入 mode

	if (NULL == if_name || '\0' == *if_name) // 如果 if_name 为空或为空字符串
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter.")); // 设置返回结果为 "无效的第一个参数"，并返回失败码
		return SYSINFO_RET_FAIL; // 结束函数调用
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes")) // 如果 mode 为空或为空字符串，或者 mode 的值为 "bytes"
		ret = NET_IF_IN_BYTES(if_name, result); // 调用 NET_IF_IN_BYTES 函数，计算接口接收的字节数，并将结果存储在 result 中
	else if (0 == strcmp(mode, "packets")) // 如果 mode 的值为 "packets"
		ret = NET_IF_IN_PACKETS(if_name, result); // 调用 NET_IF_IN_PACKETS 函数，计算接口接收的数据包数，并将结果存储在 result 中
	else if (0 == strcmp(mode, "errors")) // 如果 mode 的值为 "errors"
		ret = NET_IF_IN_ERRORS(if_name, result); // 调用 NET_IF_IN_ERRORS 函数，计算接口接收错误的次数，并将结果存储在 result 中
	else // 如果 mode 的值既不是 "bytes"，也不是 "packets" 或 "errors"
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}

int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针变量，分别为 if_name 和 mode，用于存储从请求中获取的网络接口名称和模式参数。
	char	*if_name, *mode;
	// 定义一个整型变量 ret，用于存储函数执行结果。
	int	ret;

	// 检查 request 中的参数数量，如果大于2，则表示参数过多。
	if (2 < request->nparam)
	{
		// 设置结果消息，提示参数过多。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取网络接口名称，存储到 if_name 指针变量中。
	if_name = get_rparam(request, 0);
	// 从请求中获取模式参数，存储到 mode 指针变量中。
	mode = get_rparam(request, 1);

	// 检查 if_name 是否为空，如果为空，则表示第一个参数无效。
	if (NULL == if_name || '\0' == *if_name)
	{
		// 设置结果消息，提示第一个参数无效。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 检查 mode 是否为空，或者 mode 字符串的值为 "bytes"，如果满足条件，则调用 NET_IF_TOTAL_BYTES 函数计算网络接口的字节流量。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		ret = NET_IF_OUT_BYTES(if_name, result);
	else if (0 == strcmp(mode, "packets"))
		ret = NET_IF_OUT_PACKETS(if_name, result);
	else if (0 == strcmp(mode, "errors"))
		ret = NET_IF_OUT_ERRORS(if_name, result);
	else
	{
		// 设置结果消息，提示第二个参数无效。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 返回计算结果。
	return ret;
}

int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*if_name, *mode;
	int	ret;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (NULL == if_name || '\0' == *if_name)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		ret = NET_IF_TOTAL_BYTES(if_name, result);
	else if (0 == strcmp(mode, "packets"))
		ret = NET_IF_TOTAL_PACKETS(if_name, result);
	else if (0 == strcmp(mode, "errors"))
		ret = NET_IF_TOTAL_ERRORS(if_name, result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}

int	NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct if_nameindex	*ni;
	struct zbx_json		j;
	int			i;

	if (NULL == (ni = if_nameindex()))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	for (i = 0; 0 != ni[i].if_index; i++)
	{
		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, "{#IFNAME}", ni[i].if_name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);
	}

	if_freenameindex(ni);

	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	return SYSINFO_RET_OK;
}
