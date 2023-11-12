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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "module.h"

/* the variable keeps timeout setting for item processing */
static int	item_timeout = 0;

/* module SHOULD define internal functions as static and use a naming pattern different from Zabbix internal */
/* symbols (zbx_*) and loadable module API functions (zbx_module_*) to avoid conflicts                       */
/******************************************************************************
 * c
 *static int\tdummy_ping(AGENT_REQUEST *request, AGENT_RESULT *result);
 *static int\tdummy_echo(AGENT_REQUEST *request, AGENT_RESULT *result);
 *static int\tdummy_random(AGENT_REQUEST *request, AGENT_RESULT *result);
 *
 *static ZBX_METRIC keys[] =
 */*\tKEY\t\t\tFLAG\t\tFUNCTION\tTEST PARAMETERS */
 *{
 *\t{\"dummy.ping\",\t\t0,\t\tdummy_ping,\tNULL},
 *\t{\"dummy.echo\",\t\tCF_HAVEPARAMS,\tdummy_echo,\t\"a message\"},
 *\t{\"dummy.random\",\tCF_HAVEPARAMS,\tdummy_random,\t\"1,1000\"},
 *\t{NULL}
 *};
 *
 */******************************************************************************
 * *                                                                            *
 * * Function: zbx_module_api_version                                           *
 * *                                                                            *
 * * Purpose: returns version number of the module interface                    *
 * *                                                                            *
 * * Return value: ZBX_MODULE_API_VERSION - version of module.h module is       *
 * *               compiled with, in order to load module successfully Zabbix   *
 * *               MUST be compiled with the same version of this header file   *
 * *                                                                            *
 * ******************************************************************************/
 *int\tzbx_module_api_version(void)
 *{
 *\treturn ZBX_MODULE_API_VERSION;
 *}
 *```
 ******************************************************************************/
/* 定义三个静态函数，分别为 dummy_ping、dummy_echo 和 dummy_random，它们都是 AGENT_REQUEST 和 AGENT_RESULT 类型的指针作为参数。

下面是一个结构体数组，其中包含了一系列的键值对，这些键值对用于定义模块的功能和参数。例如，第一个键值对 "dummy.ping" 没有任何参数，第二个键值对 "dummy.echo" 有一个参数 "a message"，第三个键值对 "dummy.random" 有两个参数 "1" 和 "1000"。

接下来是一个函数 zbx_module_api_version，它的作用是返回模块接口的版本号。这个版本号用于确保 Zabbix 编译时使用了与模块头文件相同版本的接口。

整个代码块的主要目的是定义一组模拟功能（dummy_ping、dummy_echo 和 dummy_random）以及它们的参数，并提供一个模块接口版本号。

下面是整个注释好的代码块：



/******************************************************************************
 *                                                                            *
 * Function: zbx_module_item_timeout                                          *
 *                                                                            *
 * Purpose: set timeout value for processing of items                         *
 *                                                                            *
 * Parameters: timeout - timeout in seconds, 0 - no timeout set               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是定义一个名为`zbx_module_item_timeout`的函数，该函数接收一个整数类型的参数`timeout`，然后将该参数的值赋给名为`item_timeout`的变量。这个函数主要用于设置一个超时值，以便在特定情况下触发超时处理。
 ******************************************************************************/
// 定义一个函数：zbx_module_item_timeout，接收一个整数参数timeout
void zbx_module_item_timeout(int timeout)
{
    // 将传入的timeout值赋给变量item_timeout
    item_timeout = timeout;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_module_item_list                                             *
 *                                                                            *
 * Purpose: returns list of item keys supported by the module                 *
 *                                                                            *
 * Return value: list of item keys                                            *
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 dummy_ping 的静态函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。在函数内部，首先设置结果对象的值为 1，然后返回 SYSINFO_RET_OK，表示操作成功。这个函数可能是用于模拟一个简单的 ping 操作，通过设置结果值和返回码来表示操作的结果。
 ******************************************************************************/
// 定义一个名为 dummy_ping 的静态函数，参数分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result
static int	dummy_ping(AGENT_REQUEST *request, AGENT_RESULT *result)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 dummy_echo 的静态函数，用于处理代理请求（AGENT_REQUEST *request）并返回一个代理结果（AGENT_RESULT *result）。该函数检查请求参数的数量，如果数量不合法，则返回错误信息；如果数量合法，则获取第一个参数并将其复制到结果中，最后返回成功码。
 ******************************************************************************/
/* 定义一个静态函数 dummy_echo，接收两个参数：AGENT_REQUEST *request 和 AGENT_RESULT *result */
static int	dummy_echo(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义一个字符指针变量 param，用于存储请求参数 */
	char	*param;

	/* 检查请求参数的数量，如果不为1，则表示参数不合法 */
	if (1 != request->nparam)
	{
		/* 设置可选的错误信息 */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
		/* 返回错误码，表示调用失败 */
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第一个参数，并将其存储在 param 变量中 */
	param = get_rparam(request, 0);

	/* 将 param 中的字符串复制到 result 中，并返回成功码 */
	SET_STR_RESULT(result, strdup(param));

	/* 函数执行成功，返回成功码 */
	return SYSINFO_RET_OK;
}

	char	*param;

	if (1 != request->nparam)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
		return SYSINFO_RET_FAIL;
	}

	param = get_rparam(request, 0);

	SET_STR_RESULT(result, strdup(param));

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为dummy_random的函数，该函数接收两个参数（请求和结果），并根据请求中的参数生成一个随机数。具体流程如下：
 *
 *1. 检查请求中的参数数量，如果数量不等于2，则设置错误信息并返回失败。
 *2. 从请求中获取参数1和参数2的值，并分别存储在指针变量param1和param2中。
 *3. 对参数进行简化验证，即将参数1和参数2的值转换为整型并存储在变量from和to中。
 *4. 如果from大于to，说明范围设置不合法，设置错误信息并返回失败。
 *5. 生成一个随机数，范围在from和to之间（包括from和to），并将结果存储在result中。
 *6. 返回成功。
 ******************************************************************************/
static int	dummy_random(AGENT_REQUEST *request, AGENT_RESULT *result)
{  // 定义一个名为dummy_random的静态函数，接收两个参数，一个是AGENT_REQUEST类型的请求，另一个是AGENT_RESULT类型的结果

	char	*param1, *param2;       // 声明两个字符指针，分别用于存储参数1和参数2的值
	int	from, to;               // 声明两个整型变量，分别用于存储范围的起始值和结束值

	if (2 != request->nparam)      // 检查请求参数的数量，如果数量不等于2，说明参数不合法
	{
		/* set optional error message */   // 设置可选的错误信息
		SET_MSG_RESULT(result, strdup("Invalid number of parameters."));  // 把错误信息存储到result中，并返回SYSINFO_RET_FAIL表示失败
		return SYSINFO_RET_FAIL;       // 返回失败
	}

	param1 = get_rparam(request, 0);  // 从请求中获取参数1的值
	param2 = get_rparam(request, 1);  // 从请求中获取参数2的值

	/* there is no strict validation of parameters for simplicity sake */  // 为了简化，不对参数进行严格验证
	from = atoi(param1);           // 将参数1的值转换为整型并存储到变量from中
	to = atoi(param2);           // 将参数2的值转换为整型并存储到变量to中

	if (from > to)           // 如果from大于to，说明范围设置不合法
	{
		SET_MSG_RESULT(result, strdup("Invalid range specified."));  // 设置错误信息
		return SYSINFO_RET_FAIL;       // 返回失败
	}

	SET_UI64_RESULT(result, from + rand() % (to - from + 1));  // 生成一个随机数，范围在from和to之间（包括from和to），并将结果存储到result中

	return SYSINFO_RET_OK;       // 返回成功
}

	char	*param1, *param2;
	int	from, to;

	if (2 != request->nparam)
	{
		/* set optional error message */
		SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
		return SYSINFO_RET_FAIL;
	}

	param1 = get_rparam(request, 0);
	param2 = get_rparam(request, 1);

	/* there is no strict validation of parameters for simplicity sake */
	from = atoi(param1);
	to = atoi(param2);

	if (from > to)
	{
		SET_MSG_RESULT(result, strdup("Invalid range specified."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, from + rand() % (to - from + 1));

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_init                                                  *
 *                                                                            *
 * Purpose: the function is called on agent startup                           *
 *          It should be used to call any initialization routines             *
 *                                                                            *
 * Return value: ZBX_MODULE_OK - success                                      *
 *               ZBX_MODULE_FAIL - module initialization failed               *
 *                                                                            *
 * Comment: the module won't be loaded in case of ZBX_MODULE_FAIL             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化 dummy.random 模块，通过设置随机数种子确保生成的随机数不同。初始化成功后，返回一个状态码 ZBX_MODULE_OK 表示初始化正常。
 ******************************************************************************/
/* 定义一个名为 zbx_module_init 的函数，该函数接受 void 类型的参数，即不需要任何参数
* 该函数的主要目的是进行一些初始化操作
*/
int zbx_module_init(void)
{
	/* 为了初始化 dummy.random 模块，我们需要设置随机数种子
	* 这里使用 time(NULL) 作为随机数种子，time(NULL) 返回当前时间戳，用于生成不同随机数
	*/
	srand(time(NULL));

	/* 返回 ZBX_MODULE_OK 表示初始化成功，此处可以理解为返回一个状态码，表示模块初始化是否成功
	* ZBX_MODULE_OK 是一个预定义的常量，表示正常状态
	*/
	return ZBX_MODULE_OK;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_module_uninit                                                *
 *                                                                            *
 * Purpose: the function is called on agent shutdown                          *
 *          It should be used to cleanup used resources if there are any      *
 *                                                                            *
 * Return value: ZBX_MODULE_OK - success                                      *
 *               ZBX_MODULE_FAIL - function failed                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *注释详细说明：
 *
 *1. 首先，我们定义了一个名为 zbx_module_uninit 的函数，它是一个空函数，没有接收任何参数。
 *2. 在函数内部，我们使用 return 语句返回一个整数值，该值表示模块初始化的状态。
 *3. 函数返回的整数值为 ZBX_MODULE_OK，表示模块初始化成功。
 *
 *整个代码块的主要目的是定义一个用于表示模块初始化状态的函数。
 ******************************************************************************/
// 定义一个函数：zbx_module_uninit，该函数为空，没有接收任何参数
int zbx_module_uninit(void)
{
    // 返回一个整数值，表示模块初始化的状态
    return ZBX_MODULE_OK;
}

// 整个代码块的主要目的是定义一个名为 zbx_module_uninit 的函数，该函数用于返回一个表示模块初始化状态的整数值。


/******************************************************************************
 *                                                                            *
 * Functions: dummy_history_float_cb                                          *
 *            dummy_history_integer_cb                                        *
 *            dummy_history_string_cb                                         *
 *            dummy_history_text_cb                                           *
 *            dummy_history_log_cb                                            *
 *                                                                            *
 * Purpose: callback functions for storing historical data of types float,    *
 *          integer, string, text and log respectively in external storage    *
/******************************************************************************
 * *
 *整个代码块的主要目的是处理一个名为 ZBX_HISTORY_INTEGER 的历史数据结构，该结构包含多个字段，如物品ID、时间戳、纳秒和数值等。代码通过 for 循环遍历历史数据结构数组，并对每个结构中的字段进行相应的处理。
 ******************************************************************************/
// 定义一个静态函数，用于处理历史数据
static void dummy_history_integer_cb(const ZBX_HISTORY_INTEGER *history, int history_num)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 使用 for 循环，从 0 开始，直到 history_num 减 1
	for (i = 0; i < history_num; i++)
	{
		/* 这里应该做些什么呢？
		 * 哦，我知道了，这是在处理历史数据结构 ZBX_HISTORY_INTEGER 中的内容。
		 * 接下来，我们一一处理 history[i] 中的各个字段：
		 * history[i].itemid：处理物品ID
		 * history[i].clock：处理时间戳
		 * history[i].ns：处理纳秒
		 * history[i].value：处理数值
		 * ...
		 */
	}
}

/* 定义一个静态函数，用于处理历史数据 */
static void dummy_history_float_cb(const ZBX_HISTORY_FLOAT *history, int history_num)
{
	/* 定义一个整型变量 i，用于循环计数 */
	int i;

	/* 遍历历史数据数组 */
	for (i = 0; i < history_num; i++)
	{
		/* 对历史数据数组中的每个元素进行某种操作，如：处理itemid、clock、ns、value等 */
		/* 此处省略具体操作代码 */
	}
}


static void	dummy_history_integer_cb(const ZBX_HISTORY_INTEGER *history, int history_num)
/******************************************************************************
 * *
 *这块代码的主要目的是遍历一个历史数据数组（ZBX_HISTORY_STRING 类型），并对数组中的每个元素进行处理。注释中已经提到了一些可能的操作，如处理 itemid、clock、ns、value 等字段。实际应用中，可以根据需要对这些字段进行相应的操作。
 ******************************************************************************/
/* 定义一个静态函数，用于处理历史数据 */
static void dummy_history_string_cb(const ZBX_HISTORY_STRING *history, int history_num)
{
	/* 定义一个整型变量 i，用于循环计数 */
	int i;

	/* 遍历历史数据数组 */
	for (i = 0; i < history_num; i++)
	{
		/* 对历史数据数组中的每个元素进行处理，这里只是简单地用逗号分隔，实际应用中可以根据需要对各个字段进行操作 */
		/* 例如：处理 history[i].itemid，history[i].clock，history[i].ns，history[i].value 等字段 */
	}
}

static void	dummy_history_string_cb(const ZBX_HISTORY_STRING *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
	{
		/* do something with history[i].itemid, history[i].clock, history[i].ns, history[i].value, ... */
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历一个 ZBX_HISTORY_TEXT 结构体数组，并对每个结构体的成员进行某种操作。注释中省略了具体操作的代码，实际使用时需要根据具体需求填写相应的操作。
 ******************************************************************************/
/* 定义一个名为 dummy_history_text_cb 的静态函数，它接收两个参数：一个指向 ZBX_HISTORY_TEXT 结构体的指针 history，以及一个整数 history_num。
*/
static void	dummy_history_text_cb(const ZBX_HISTORY_TEXT *history, int history_num)
{
	/* 定义一个整数变量 i，用于循环计数 */
	int	i;
/******************************************************************************
 * *
 *这段代码的主要目的是遍历一个 ZBX_HISTORY_LOG 结构体数组，并对每个结构体的成员进行某种操作。这里的循环变量 i 用于表示当前遍历到的结构体在数组中的位置，history_num 表示数组中结构体的总数。在循环内部，可以对每个结构体的成员（如 itemid、clock、ns、value 等）进行相应的处理。
 ******************************************************************************/
/* 定义一个名为 dummy_history_log_cb 的静态函数，它接收两个参数：一个指向 ZBX_HISTORY_LOG 结构体的指针 history，以及一个整数 history_num。
*/
static void dummy_history_log_cb(const ZBX_HISTORY_LOG *history, int history_num)
{
	/* 定义一个整数变量 i，用于循环计数 */
	int i;

	/* 使用 for 循环遍历 history_num 个 ZBX_HISTORY_LOG 结构体 */
	for (i = 0; i < history_num; i++)
	{
		/* 对 history[i].itemid、history[i].clock、history[i].ns、history[i].value 等成员进行某种操作 */
		/* 这里可以添加具体的操作代码 */
	}
}


static void	dummy_history_log_cb(const ZBX_HISTORY_LOG *history, int history_num)
{
	int	i;

	for (i = 0; i < history_num; i++)
	{
		/* do something with history[i].itemid, history[i].clock, history[i].ns, history[i].value, ... */
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_module_history_write_cbs                                     *
 *                                                                            *
 * Purpose: returns a set of module functions Zabbix will call to export      *
 *          different types of historical data                                *
 *                                                                            *
 * Return value: structure with callback function pointers (can be NULL if    *
 *               module is not interested in data of certain types)           *
 *                                                                            *
 ******************************************************************************/
ZBX_HISTORY_WRITE_CBS	zbx_module_history_write_cbs(void)
{
	static ZBX_HISTORY_WRITE_CBS	dummy_callbacks =
	{
		dummy_history_float_cb,
		dummy_history_integer_cb,
		dummy_history_string_cb,
		dummy_history_text_cb,
		dummy_history_log_cb,
	};

	return dummy_callbacks;
}
