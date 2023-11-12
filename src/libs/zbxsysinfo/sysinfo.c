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
#include "module.h"
#include "sysinfo.h"
#include "log.h"
#include "cfg.h"
#include "alias.h"
#include "threads.h"
#include "sighandler.h"

#ifdef WITH_AGENT_METRICS
#	include "agent/agent.h"
#endif

#ifdef WITH_COMMON_METRICS
#	include "common/common.h"
#endif

#ifdef WITH_SIMPLE_METRICS
#	include "simple/simple.h"
#endif

#ifdef WITH_SPECIFIC_METRICS
#	include "specsysinfo.h"
#endif

#ifdef WITH_HOSTNAME_METRIC
extern ZBX_METRIC      parameter_hostname;
#endif

static ZBX_METRIC	*commands = NULL;

#define ZBX_COMMAND_ERROR		0
#define ZBX_COMMAND_WITHOUT_PARAMS	1
#define ZBX_COMMAND_WITH_PARAMS		2

/******************************************************************************
 *                                                                            *
 * Function: parse_command_dyn                                                *
 *                                                                            *
 * Purpose: parses item key and splits it into command and parameters         *
 *                                                                            *
 * Return value: ZBX_COMMAND_ERROR - error                                    *
 *               ZBX_COMMAND_WITHOUT_PARAMS - command without parameters      *
 *               ZBX_COMMAND_WITH_PARAMS - command with parameters            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析命令行参数。它接收一个命令字符串（const char *command），并尝试从中提取关键字和参数。如果成功提取到关键字和参数，函数返回带有参数的命令状态码；如果遇到错误，返回相应的错误码。整个代码块分为以下几个部分：
 *
 *1. 定义指针pl和pr，分别指向命令字符串和可能的参数字符串。
 *2. 遍历命令字符串，查找关键字字符。
 *3. 检查命令字符串末尾是否为'\\0'，如果是，说明没有指定参数，返回相应状态码。
 *4. 检查命令字符串中是否包含未支持的字符，如['']，如果是，返回错误。
 *5. 遍历命令字符串，查找参数结束符']'。
 *6. 拷贝参数字符串到param数组，并分配内存。
 *7. 函数执行成功，返回带有参数的命令状态码。
 ******************************************************************************/
// 定义一个静态函数parse_command_dyn，用于解析命令行参数
static int	parse_command_dyn(const char *command, char **cmd, char **param)
{
	// 定义两个指针pl和pr，分别指向命令字符串和可能的参数字符串
	const char	*pl, *pr;
	// 定义四个变量，用于记录字符串的长度和偏移量
	size_t		cmd_alloc = 0, param_alloc = 0,
			cmd_offset = 0, param_offset = 0;

	// 遍历命令字符串，查找关键字字符
	for (pl = command; SUCCEED == is_key_char(*pl); pl++)
		;

	// 如果pl等于命令字符串，说明没有找到关键字，返回错误
	if (pl == command)
		return ZBX_COMMAND_ERROR;

	// 拷贝命令字符串到cmd数组，并分配内存
	zbx_strncpy_alloc(cmd, &cmd_alloc, &cmd_offset, command, pl - command);

	// 如果命令字符串末尾是'\0'，说明没有指定参数
	if ('\0' == *pl)
		return ZBX_COMMAND_WITHOUT_PARAMS;

	// 检查命令字符串中是否包含未支持的字符
	if ('[' != *pl)
		return ZBX_COMMAND_ERROR;

	// 遍历命令字符串，查找参数结束符']'
	for (pr = ++pl; '\0' != *pr; pr++)
		;

	// 如果']'不等于*--pr，说明解析错误，返回错误
	if (']' != *--pr)
		return ZBX_COMMAND_ERROR;

	// 拷贝参数字符串到param数组，并分配内存
	zbx_strncpy_alloc(param, &param_alloc, &param_offset, pl, pr - pl);

	// 函数执行成功，返回带有参数的命令状态码
	return ZBX_COMMAND_WITH_PARAMS;
}


/******************************************************************************
 *                                                                            *
 * Function: add_metric                                                       *
 *                                                                            *
 * Purpose: registers a new item key into the system                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：实现一个 C 语言函数 `add_metric`，用于向命令数组 `commands` 中添加一个新的元素，该元素表示一个指标（metric）。在添加过程中，首先检查数组中是否已存在相同的 key，如果存在，则输出错误信息并返回失败；如果不存在，则将 metric 的信息存储到数组中，并重新分配内存以扩大数组空间。最后，返回成功状态码。
 ******************************************************************************/
int	add_metric(ZBX_METRIC *metric, char *error, size_t max_error_len)
{
	// 定义一个整型变量 i，用于遍历数组 commands
	int	i = 0;

	// 使用 while 循环遍历数组 commands，直到数组末尾
	while (NULL != commands[i].key)
	{
		// 判断当前元素 key 是否与传入的 metric->key 相同
		if (0 == strcmp(commands[i].key, metric->key))
		{
			// 如果相同，则输出错误信息，并返回失败状态码（FAIL）
			zbx_snprintf(error, max_error_len, "key \"%s\" already exists", metric->key);
			return FAIL;	/* metric already exists */
		}
		// 否则，继续遍历下一个元素
		i++;
	}

	// 将 metric 的信息存储到 commands 数组中
	commands[i].key = zbx_strdup(NULL, metric->key);
	commands[i].flags = metric->flags;
	commands[i].function = metric->function;
	commands[i].test_param = (NULL == metric->test_param ? NULL : zbx_strdup(NULL, metric->test_param));

	// 重新分配内存，扩大 commands 数组空间
	commands = (ZBX_METRIC *)zbx_realloc(commands, (i + 2) * sizeof(ZBX_METRIC));
	memset(&commands[i + 1], 0, sizeof(ZBX_METRIC));

	// 函数执行成功，返回成功状态码（SUCCEED）
	return SUCCEED;
}

/******************************************************************************
 * *
 *这块代码的主要目的是解析用户输入的itemkey，并根据其是否存在参数来设置相应的标志位。接着，将解析后的信息封装到metric结构体中，并添加到系统中。如果解析失败，则返回语法错误信息。
 *
 *整个代码块的作用可以概括为：解析用户输入的itemkey，判断其是否存在语法错误，如果不存在，则将其添加为用户自定义参数。其中，涉及到一些字符串操作和结构体的初始化与释放。
 ******************************************************************************/
// 定义一个函数，用于添加用户自定义参数
int add_user_parameter(const char *itemkey, char *command, char *error, size_t max_error_len)
{
	// 定义一些变量
	int		ret;
	unsigned	flags = CF_USERPARAMETER;
	ZBX_METRIC	metric;
	AGENT_REQUEST	request;

	// 初始化请求结构体
	init_request(&request);

	// 解析itemkey，如果成功，则继续执行后续操作
	if (SUCCEED == (ret = parse_item_key(itemkey, &request)))
	{
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化各种 metrics，并将它们添加到系统中。具体来说，代码逐个遍历不同模块的 metrics 参数，并为每个 metrics 分配 key。在添加 metrics 过程中，如果遇到错误，会打印错误日志并退出程序。
 ******************************************************************************/
// 定义一个函数，用于初始化 metrics
void init_metrics(void)
{
	// 定义一个整型变量 i，用于循环计数
	int i;
	// 定义一个字符型数组 error，用于存储错误信息
	char error[MAX_STRING_LEN];

	// 为 commands 数组分配内存，存储 ZBX_METRIC 结构体
	commands = (ZBX_METRIC *)zbx_malloc(commands, sizeof(ZBX_METRIC));
	// 初始化 commands 数组的第一个元素，使其 key 为空
	commands[0].key = NULL;

	// 针对不同的模块，分别加载对应的 metrics
#ifdef WITH_AGENT_METRICS
	for (i = 0; NULL != parameters_agent[i].key; i++)
	{
		// 尝试添加 metrics
		if (SUCCEED != add_metric(&parameters_agent[i], error, sizeof(error)))
		{
			// 打印错误日志
			zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
			// 程序退出，返回失败
			exit(EXIT_FAILURE);
		}
	}
#endif

#ifdef WITH_COMMON_METRICS
	for (i = 0; NULL != parameters_common[i].key; i++)
	{
		// 尝试添加 metrics
		if (SUCCEED != add_metric(&parameters_common[i], error, sizeof(error)))
		{
			// 打印错误日志
			zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
			// 程序退出，返回失败
			exit(EXIT_FAILURE);
		}
	}
#endif

#ifdef WITH_SPECIFIC_METRICS
	for (i = 0; NULL != parameters_specific[i].key; i++)
	{
		// 尝试添加 metrics
		if (SUCCEED != add_metric(&parameters_specific[i], error, sizeof(error)))
		{
			// 打印错误日志
			zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
			// 程序退出，返回失败
			exit(EXIT_FAILURE);
		}
	}
#endif

#ifdef WITH_SIMPLE_METRICS
	for (i = 0; NULL != parameters_simple[i].key; i++)
	{
		// 尝试添加 metrics
		if (SUCCEED != add_metric(&parameters_simple[i], error, sizeof(error)))
		{
			// 打印错误日志
			zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
			// 程序退出，返回失败
			exit(EXIT_FAILURE);
		}
	}
#endif

#ifdef WITH_HOSTNAME_METRIC
	// 尝试添加 hostname 相关的 metrics
	if (SUCCEED != add_metric(&parameter_hostname, error, sizeof(error)))
	{
		// 打印错误日志
		zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
		// 程序退出，返回失败
		exit(EXIT_FAILURE);
	}
#endif
}

#endif

#ifdef WITH_SPECIFIC_METRICS
	for (i = 0; NULL != parameters_specific[i].key; i++)
	{
		if (SUCCEED != add_metric(&parameters_specific[i], error, sizeof(error)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
			exit(EXIT_FAILURE);
		}
	}
#endif

#ifdef WITH_SIMPLE_METRICS
	for (i = 0; NULL != parameters_simple[i].key; i++)
	{
		if (SUCCEED != add_metric(&parameters_simple[i], error, sizeof(error)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
			exit(EXIT_FAILURE);
		}
	}
#endif

#ifdef WITH_HOSTNAME_METRIC
	if (SUCCEED != add_metric(&parameter_hostname, error, sizeof(error)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot add item key: %s", error);
		exit(EXIT_FAILURE);
	}
#endif
}
/******************************************************************************
 * *
 *整个代码块的主要目的是释放一个名为commands的数组及其数组元素中key和test_param指针所指向的内存。这个函数在程序运行过程中，用于确保内存的有效管理。
 ******************************************************************************/
void	free_metrics(void)			// 定义一个名为free_metrics的函数，用于释放内存
{
	if (NULL != commands)			// 如果commands指针不为NULL，说明数组存在
	{
		int	i;				// 定义一个整型变量i，用于循环计数

		for (i = 0; NULL != commands[i].key; i++)	// 遍历数组中的每个元素
		{
			zbx_free(commands[i].key);	// 释放commands[i].key所指向的内存
			zbx_free(commands[i].test_param);	// 释放commands[i].test_param所指向的内存
		}

		zbx_free(commands);			// 释放commands数组所指向的内存
	}
}


/******************************************************************************
 * *
 *这块代码的主要目的是初始化一个 zbx_log_t 类型的结构体变量 log。在这个函数中，逐个设置 log 结构体中的各个成员变量为初始值，分别为：value 成员设置为 NULL，source 成员设置为 NULL，timestamp 成员设置为 0，severity 成员设置为 0，logeventid 成员设置为 0。这样就完成了 log 结构体的初始化工作。
 ******************************************************************************/
// 定义一个名为 zbx_log_init 的静态函数，参数为一个 zbx_log_t 类型的指针 log
static void zbx_log_init(zbx_log_t *log)
{
    // 将 log 结构体中的 value 成员设置为 NULL
    log->value = NULL;
    // 将 log 结构体中的 source 成员设置为 NULL
    log->source = NULL;
    // 将 log 结构体中的 timestamp 成员设置为 0
    log->timestamp = 0;
    // 将 log 结构体中的 severity 成员设置为 0
    log->severity = 0;
    // 将 log 结构体中的 logeventid 成员设置为 0
    log->logeventid = 0;
}

/******************************************************************************
 * *
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */*
 * * 定义一个函数 free_result，接收一个 AGENT_RESULT 类型的指针作为参数。
 * * 这个函数的主要目的是释放result指向的结构体中的内存空间。
 * * 
 * * 函数详细注释：
 * * 1. 使用UNSET_UI64_RESULT函数清零result指向的结构体中的UI64_RESULT成员。
 * * 2. 使用UNSET_DBL_RESULT函数清零result指向的结构体中的DBL_RESULT成员。
 * * 3. 使用UNSET_STR_RESULT函数清零result指向的结构体中的STR_RESULT成员。
 * * 4. 使用UNSET_TEXT_RESULT函数清零result指向的结构体中的TEXT_RESULT成员。
 * * 5. 使用UNSET_LOG_RESULT函数清零result指向的结构体中的LOG_RESULT成员。
 * * 6. 使用UNSET_MSG_RESULT函数清零result指向的结构体中的MSG_RESULT成员。
 * */
 *void\tfree_result(AGENT_RESULT *result)
 *{
 *\tUNSET_UI64_RESULT(result);
 *\tUNSET_DBL_RESULT(result);
 *\tUNSET_STR_RESULT(result);
 *\tUNSET_TEXT_RESULT(result);
 *\tUNSET_LOG_RESULT(result);
 *\tUNSET_MSG_RESULT(result);
 *}
 *```
 ******************************************************************************/
/*
 * 定义一个函数 free_result，接收一个 AGENT_RESULT 类型的指针作为参数。
 * 这个函数的主要目的是释放result指向的结构体中的内存空间。
 * 
 * 下面是函数的详细注释：
 */
void	free_result(AGENT_RESULT *result)
{
	// 1. 使用UNSET_UI64_RESULT函数清零result指向的结构体中的UI64_RESULT成员。
	UNSET_UI64_RESULT(result);

	// 2. 使用UNSET_DBL_RESULT函数清零result指向的结构体中的DBL_RESULT成员。
	UNSET_DBL_RESULT(result);

	// 3. 使用UNSET_STR_RESULT函数清零result指向的结构体中的STR_RESULT成员。
	UNSET_STR_RESULT(result);

	// 4. 使用UNSET_TEXT_RESULT函数清零result指向的结构体中的TEXT_RESULT成员。
	UNSET_TEXT_RESULT(result);

	// 5. 使用UNSET_LOG_RESULT函数清零result指向的结构体中的LOG_RESULT成员。
	UNSET_LOG_RESULT(result);

	// 6. 使用UNSET_MSG_RESULT函数清零result指向的结构体中的MSG_RESULT成员。
	UNSET_MSG_RESULT(result);
}

}


/******************************************************************************
 * ```c
 ******************************************************************************/
// 定义一个静态函数zbx_log_clean，参数为一个zbx_log_t类型的指针log
static void zbx_log_clean(zbx_log_t *log)
{
    // 释放log结构体中的source成员所指向的内存空间
    zbx_free(log->source);
    // 释放log结构体中的value成员所指向的内存空间
    zbx_free(log->value);
}

整个代码块的主要目的是清理zbx_log_t类型的结构体对象log中的source和value成员所指向的内存空间。这段代码通过两个连续的free()函数来实现内存的释放。需要注意的是，这里的log结构体是指向一个已分配内存的结构体对象。在程序运行过程中，为了避免内存泄漏，需要在适当的时候清理不再使用的内存空间。而这个函数就是用来清理日志相关数据的内存空间。
/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_log_t类型的指针log所指向的内存空间。具体来说，首先调用zbx_log_clean函数清理日志记录，然后使用zbx_free函数释放log内存。
 ******************************************************************************/
// 定义一个函数：zbx_log_free，传入一个zbx_log_t类型的指针
void	zbx_log_free(zbx_log_t *log)
{
    // 调用zbx_log_clean函数，清理日志记录
    zbx_log_clean(log);
    
    // 使用zbx_free函数释放log内存
    zbx_free(log);
}


void	free_result(AGENT_RESULT *result)
{
	UNSET_UI64_RESULT(result);
	UNSET_DBL_RESULT(result);
	UNSET_STR_RESULT(result);
	UNSET_TEXT_RESULT(result);
	UNSET_LOG_RESULT(result);
	UNSET_MSG_RESULT(result);
}

/******************************************************************************
 *                                                                            *
 * Function: init_request                                                     *
 *                                                                            *
 * Purpose: initialize the request structure                                  *
 *                                                                            *
 * Parameters: request - pointer to the structure                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：初始化一个AGENT_REQUEST结构体的指针request，将其各个成员变量设置为初始值，为后续处理请求做好准备。
 ******************************************************************************/
void	init_request(AGENT_REQUEST *request) // 定义一个函数，用于初始化AGENT_REQUEST结构体的指针request
{
	request->key = NULL; // 初始化request->key为NULL，key用于存储请求的唯一标识
	request->nparam = 0; // 初始化request->nparam为0，表示尚未接收任何参数
	request->params = NULL; // 初始化request->params为NULL，params用于存储请求的参数列表
	request->lastlogsize = 0; // 初始化request->lastlogsize为0，记录最后一次日志的大小
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放一个AGENT_REQUEST结构体中的所有请求参数所占用的内存。在此过程中，首先遍历请求参数的数量，然后依次释放每个请求参数所占用的内存，接着释放请求参数数组所占用的内存，最后将请求参数的数量设置为0，表示已经全部释放。
 ******************************************************************************/
static void	free_request_params(AGENT_REQUEST *request) // 定义一个静态函数，用于释放请求参数
{
	int	i; // 定义一个整型变量i，用于循环计数

	for (i = 0; i < request->nparam; i++) // 遍历请求参数的数量
		zbx_free(request->params[i]); // 释放每个请求参数所占用的内存
	zbx_free(request->params); // 释放请求参数数组所占用的内存

	request->nparam = 0; // 将请求参数的数量设置为0，表示已经全部释放
}

 * Parameters: request - pointer to the request structure                     *
 *                                                                            *
 ******************************************************************************/
static void	free_request_params(AGENT_REQUEST *request)
{
	int	i;

	for (i = 0; i < request->nparam; i++)
		zbx_free(request->params[i]);
	zbx_free(request->params);

	request->nparam = 0;
}

/******************************************************************************
 *                                                                            *
 * Function: free_request                                                     *
 *                                                                            *
 * Purpose: free memory used by the request                                   *
 *                                                                            *
 * Parameters: request - pointer to the request structure                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放AGENT_REQUEST结构体中key和request_params所指向的内存空间。这是一段用于处理内存释放的函数代码。
 ******************************************************************************/
// 定义一个函数free_request，参数为一个AGENT_REQUEST结构体的指针request
void free_request(AGENT_REQUEST *request)
{
    // 释放request结构体中key所指向的内存空间
    zbx_free(request->key);
    
    // 调用free_request_params函数，释放request结构体中request_params所指向的内存空间
    free_request_params(request);
}


/******************************************************************************
 *                                                                            *
 * Function: add_request_param                                                *
 *                                                                            *
 * Purpose: add a new parameter                                               *
 *                                                                            *
 * Parameters: request - pointer to the request structure                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：为一个AGENT_REQUEST结构体的请求参数数组添加一个新的元素。
 *
 *整个代码块的功能总结：
 *1. 增加请求参数的数量。
 *2. 重新分配内存，使请求参数数组能够容纳新元素。
 *3. 将新参数值赋给请求参数数组的最后一个元素。
 ******************************************************************************/
// 定义一个静态函数，用于向请求参数数组中添加一个新元素
static void add_request_param(AGENT_REQUEST *request, char *pvalue)
{
    // 增加请求参数的数量
    request->nparam++;
    
    // 重新分配内存，使请求参数数组能够容纳新元素
    request->params = (char **)zbx_realloc(request->params, request->nparam * sizeof(char *));
    
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个名为itemkey的字符串，并根据解析结果填充agent请求结构体。具体来说，这段代码首先根据itemkey的类型分配相应的内存，然后处理带参数的命令，将参数值存储到请求结构体中。最后，释放内存并返回解析结果。
 ******************************************************************************/
// 定义一个函数，用于解析itemkey和agent请求结构体
int parse_item_key(const char *itemkey, AGENT_REQUEST *request)
{
	// 定义变量，用于存储解析结果和错误码
	int	i, ret = FAIL;
	char	*key = NULL, *params = NULL;

	// 使用switch语句根据itemkey解析命令类型
	switch (parse_command_dyn(itemkey, &key, &params))
	{
		// 解析出的是带参数的命令
		case ZBX_COMMAND_WITH_PARAMS:
			// 检查参数个数是否合法，如果不合法，跳出函数
			if (0 == (request->nparam = num_param(params)))
				goto out;	/* key is badly formatted */
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 test_parameter 的函数，接收一个字符串类型的参数 key，对字符串进行处理，并输出处理结果。处理过程中，按照 ZBX_KEY_COLUMN_WIDTH 的宽度输出 key，同时根据 result 结构体中的不同字段输出相应的值。如果处理失败，则输出相应的错误信息。最后释放 result 结构体占用的内存，并刷新标准输出缓冲区。
 ******************************************************************************/
void	test_parameter(const char *key) // 定义一个名为 test_parameter 的函数，接收一个 const char * 类型的参数 key
{
#define ZBX_KEY_COLUMN_WIDTH	45 // 定义一个常量 ZBX_KEY_COLUMN_WIDTH，值为 45

	AGENT_RESULT	result; // 定义一个 AGENT_RESULT 类型的变量 result

	printf("%-*s", ZBX_KEY_COLUMN_WIDTH, key); // 按照 ZBX_KEY_COLUMN_WIDTH 的宽度输出 key

	init_result(&result); // 初始化一个 AGENT_RESULT 类型的变量 result

	if (SUCCEED == process(key, PROCESS_WITH_ALIAS, &result)) // 如果 process 函数执行成功
	{
		if (0 != ISSET_UI64(&result)) // 如果 result 结构体中的 ui64 字段不为空
			printf(" [u|" ZBX_FS_UI64 "]", result.ui64); // 输出结果的 ui64 字段

		if (0 != ISSET_DBL(&result)) // 如果 result 结构体中的 dbl 字段不为空
			printf(" [d|" ZBX_FS_DBL "]", result.dbl); // 输出结果的 dbl 字段

		if (0 != ISSET_STR(&result)) // 如果 result 结构体中的 str 字段不为空
			printf(" [s|%s]", result.str); // 输出结果的 str 字段

		if (0 != ISSET_TEXT(&result)) // 如果 result 结构体中的 text 字段不为空
			printf(" [t|%s]", result.text); // 输出结果的 text 字段

		if (0 != ISSET_MSG(&result)) // 如果 result 结构体中的 msg 字段不为空
			printf(" [m|%s]", result.msg); // 输出结果的 msg 字段
	}
	else
	{
		if (0 != ISSET_MSG(&result)) // 如果 result 结构体中的 msg 字段不为空
			printf(" [m|" ZBX_NOTSUPPORTED "] [%s]", result.msg); // 输出结果的 msg 字段以及 ZBX_NOTSUPPORTED
		else
			printf(" [m|" ZBX_NOTSUPPORTED "]"); // 输出 ZBX_NOTSUPPORTED
	}

	free_result(&result); // 释放 result 结构体占用的内存

	printf("\
"); // 换行

	fflush(stdout); // 刷新标准输出缓冲区

#undef ZBX_KEY_COLUMN_WIDTH // 取消定义 ZBX_KEY_COLUMN_WIDTH 常量
}


	return ret;
}

void	test_parameter(const char *key)
{
#define ZBX_KEY_COLUMN_WIDTH	45

	AGENT_RESULT	result;

	printf("%-*s", ZBX_KEY_COLUMN_WIDTH, key);

	init_result(&result);

	if (SUCCEED == process(key, PROCESS_WITH_ALIAS, &result))
	{
		if (0 != ISSET_UI64(&result))
			printf(" [u|" ZBX_FS_UI64 "]", result.ui64);

		if (0 != ISSET_DBL(&result))
			printf(" [d|" ZBX_FS_DBL "]", result.dbl);

		if (0 != ISSET_STR(&result))
			printf(" [s|%s]", result.str);

		if (0 != ISSET_TEXT(&result))
			printf(" [t|%s]", result.text);

		if (0 != ISSET_MSG(&result))
			printf(" [m|%s]", result.msg);
	}
	else
	{
		if (0 != ISSET_MSG(&result))
			printf(" [m|" ZBX_NOTSUPPORTED "] [%s]", result.msg);
		else
			printf(" [m|" ZBX_NOTSUPPORTED "]");
	}

	free_result(&result);

	printf("\n");

	fflush(stdout);

#undef ZBX_KEY_COLUMN_WIDTH
}
/******************************************************************************
 * *
 *整个代码块的主要目的是遍历一个名为 commands 的数组，查找其中的 \"__UserPerfCounter\" 关键字，并对符合条件的元素进行处理。处理过程包括：
 *
 *1. 将符合条件的元素的 key 拷贝到 key 指针中，并分配内存。
 *2. 判断 key 指针所指字符串是否包含 \"__UserPerfCounter\" 关键字。
 *3. 如果 key 指针所指字符串不包含 \"__UserPerfCounter\" 关键字，且 test_param 不为 NULL，则执行以下操作：
 *   a. 在 key 指针所指字符串的开头添加 '[' 字符。
 *   b. 拷贝 commands[i].test_param 到 key 指针所指字符串中。
 *   c. 在 key 指针所指字符串的结尾添加 ']' 字符。
 *4. 调用 test_parameter 函数，传入 key 指针所指的字符串作为参数。
 *5. 释放 key 指针所指的内存。
 *6. 调用 test_aliases 函数。
 ******************************************************************************/
void	test_parameters(void)		// 定义一个名为 test_parameters 的函数，用于测试参数
{
	int	i;			// 定义一个整型变量 i，用于循环计数
	char	*key = NULL;		// 定义一个字符指针 key，初始值为 NULL
	size_t	key_alloc = 0;		// 定义一个大小为 0 的 size_t 类型变量 key_alloc

	for (i = 0; NULL != commands[i].key; i++)	// 遍历数组 commands 中的每个元素
	{
		if (0 != strcmp(commands[i].key, "__UserPerfCounter"))	// 如果当前元素的 key 等于 "__UserPerfCounter"
		{
			size_t	key_offset = 0;	// 定义一个 size_t 类型变量 key_offset，初始值为 0

			zbx_strcpy_alloc(&key, &key_alloc, &key_offset, commands[i].key);	// 拷贝当前元素的 key 到 key 指针中，并分配内存

			if (0 == (commands[i].flags & CF_USERPARAMETER) && NULL != commands[i].test_param)	// 如果当前元素的 flags 字段与 CF_USERPARAMETER 比较，且 test_param 不为 NULL
			{
				zbx_chrcpy_alloc(&key, &key_alloc, &key_offset, '[');	// 在 key 指针所指字符串的开头添加 '[' 字符
				zbx_strcpy_alloc(&key, &key_alloc, &key_offset, commands[i].test_param);	// 拷贝当前元素的 test_param 到 key 指针中
				zbx_chrcpy_alloc(&key, &key_alloc, &key_offset, ']');	// 在 key 指针所指字符串的结尾添加 ']' 字符
			}

			test_parameter(key);	// 调用 test_parameter 函数，传入 key 作为参数
		}
	}

	zbx_free(key);	// 释放 key 指针所指的内存

	test_aliases();	// 调用 test_aliases 函数
}


/******************************************************************************
 * *
 *整个代码块的主要目的是检查用户输入的参数是否包含禁止使用的特殊字符。如果发现禁止使用的特殊字符，则生成一条错误信息并返回失败；否则返回成功。
 ******************************************************************************/
// 定义一个静态函数zbx_check_user_parameter，用于检查用户参数是否合法
static int	zbx_check_user_parameter(const char *param, char *error, int max_error_len)
{
	// 定义一个字符串数组 suppression_chars，用于存放需要被抑制的特殊字符
	const char	suppressed_chars[] = "\\'\"`*?[]{}~$!&;()<>|#@\
", *c;
	// 定义一个字符指针buf，用于存放检查结果
	char		*buf = NULL;
	// 定义一个size_t类型的变量buf_alloc，表示buf分配的最大长度
	size_t		buf_alloc = 128, buf_offset = 0;

	// 判断配置是否启用了不安全用户参数，如果启用，直接返回成功
	if (0 != CONFIG_UNSAFE_USER_PARAMETERS)
		return SUCCEED;

	// 遍历suppression_chars数组中的每个字符
	for (c = suppression_chars; '\0' != *c; c++)
	{
		// 判断param字符串中是否包含suppression_chars中的字符，如果不包含，继续循环
		if (NULL == strchr(param, *c))
			continue;

		// 为buf分配内存空间
		buf = (char *)zbx_malloc(buf, buf_alloc);

		// 遍历suppression_chars数组中的每个字符（不包括第一个字符）
		for (c = suppression_chars + 1; '\0' != *c; c++)
		{
			// 如果当前字符不是suppression_chars中的字符，则在buf中添加一个逗号和一个空格
			if (c != suppression_chars + 1)
				zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, ", ");

			// 如果当前字符是可打印的，则将其复制到buf中
			if (0 != isprint(*c))
				zbx_chrcpy_alloc(&buf, &buf_alloc, &buf_offset, *c);
			// 否则，将字符的十六进制表示添加到buf中
			else
				zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, "0x%02x", (unsigned int)(*c));
		}

		// 格式化错误信息，并将其存储在error字符串中
		zbx_snprintf(error, max_error_len, "Special characters \"%s\" are not allowed in the parameters.", buf);

		// 释放buf内存
		zbx_free(buf);

		// 如果发现非法字符，返回失败
		return FAIL;
	}

	// 如果没有发现非法字符，返回成功
	return SUCCEED;
}


/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 这个函数的主要目的是替换命令行中的参数。
 * 输入参数：
 *   cmd：命令行字符串
 *   request：代理请求结构体指针
 *   out：输出字符串指针
 *   error：错误字符串指针
 *   max_error_len：错误字符串的最大长度
 * 返回值：
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为 `process` 的函数，该函数接收一个命令行参数 `in_command`，以及一些标志 `flags`，然后根据这些参数和标志执行相应的命令。函数的返回值表示命令执行的成功与否。
 *
 *代码首先定义了一些变量，然后解析命令行参数并检查其格式是否正确。接下来，代码检查远程命令是否已启用，以及请求的 item key 是否符合要求。然后，代码会查找并解析匹配的命令，检查命令是否支持参数，并根据需要设置请求的参数。
 *
 *接着，代码调用命令函数并传入请求和结果。最后，根据命令执行的结果，设置返回值并释放请求结构体。整个代码块主要用于处理和执行客户端发送的命令请求。
 ******************************************************************************/
int	process(const char *in_command, unsigned flags, AGENT_RESULT *result)
{
	/* 定义变量，ret 初始化为 NOTSUPPORTED，command 为空指针，request 为 AGENT_REQUEST 结构体 */
	int		ret = NOTSUPPORTED;
	ZBX_METRIC	*command = NULL;
	AGENT_REQUEST	request;

	/* 初始化 request 结构体 */
	init_request(&request);

	/* 解析 item key，如果失败则设置错误信息并退出 */
	if (SUCCEED != parse_item_key((0 == (flags & PROCESS_WITH_ALIAS) ? in_command : zbx_alias_get(in_command)),
			&request))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
		goto notsupported;
	}

	/* 检查是否允许远程命令，如果不允许且不是本地命令，则设置错误信息并退出 */
	if (1 != CONFIG_ENABLE_REMOTE_COMMANDS && 0 == (flags & PROCESS_LOCAL_COMMAND) &&
			0 == strcmp(request.key, "system.run"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Remote commands are not enabled."));
		goto notsupported;
	}

	/* 遍历命令列表，查找匹配的命令 */
	for (command = commands; NULL != command->key; command++)
	{
		if (0 == strcmp(command->key, request.key))
			break;
	}

	/* 如果没有找到匹配的 item key，则设置错误信息并退出 */
	if (NULL == command->key)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported item key."));
		goto notsupported;
	}

	/* 检查是否支持模块化命令，如果不支持，则设置错误信息并退出 */
	if (0 != (flags & PROCESS_MODULE_COMMAND) && 0 == (command->flags & CF_MODULE))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported item key."));
		goto notsupported;
	}

	/* 检查命令是否支持参数，如果不支持，则设置错误信息并退出 */
	if (0 == (command->flags & CF_HAVEPARAMS) && 0 != request.nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Item does not allow parameters."));
		goto notsupported;
	}

	/* 如果命令支持用户参数，则处理参数 */
	if (0 != (command->flags & CF_USERPARAMETER))
	{
		if (0 != (command->flags & CF_HAVEPARAMS))
		{
			char	*parameters = NULL, error[MAX_STRING_LEN];

			if (FAIL == replace_param(command->test_param, &request, &parameters, error, sizeof(error)))
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, error));
				goto notsupported;
			}

			free_request_params(&request);
			add_request_param(&request, parameters);
		}
		else
		{
			free_request_params(&request);
			add_request_param(&request, zbx_strdup(NULL, command->test_param));
		}
	}

	/* 调用命令函数，并传入 request 和 result */
	if (SYSINFO_RET_OK != command->function(&request, result))
	{
		/* "return NOTSUPPORTED;" 会更合适，以便保留原始错误信息 */
		/* 但会破坏依赖于 ZBX_NOTSUPPORTED 消息的事物。 */
		if (0 != (command->flags & CF_MODULE) && 0 == ISSET_MSG(result))
			SET_MSG_RESULT(result, zbx_strdup(NULL, ZBX_NOTSUPPORTED_MSG));

		goto notsupported;
	}

	/* 设置返回值 */
	ret = SUCCEED;

notsupported:
	/* 释放 request 结构体 */
	free_request(&request);

	return ret;
}


	if (SUCCEED != parse_item_key((0 == (flags & PROCESS_WITH_ALIAS) ? in_command : zbx_alias_get(in_command)),
			&request))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
		goto notsupported;
	}

	/* system.run is not allowed by default except for getting hostname for daemons */
	if (1 != CONFIG_ENABLE_REMOTE_COMMANDS && 0 == (flags & PROCESS_LOCAL_COMMAND) &&
			0 == strcmp(request.key, "system.run"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Remote commands are not enabled."));
		goto notsupported;
	}

	for (command = commands; NULL != command->key; command++)
	{
		if (0 == strcmp(command->key, request.key))
			break;
	}

	/* item key not found */
	if (NULL == command->key)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported item key."));
		goto notsupported;
	}

	/* expected item from a module */
	if (0 != (flags & PROCESS_MODULE_COMMAND) && 0 == (command->flags & CF_MODULE))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported item key."));
		goto notsupported;
	}

	/* command does not accept parameters but was called with parameters */
	if (0 == (command->flags & CF_HAVEPARAMS) && 0 != request.nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Item does not allow parameters."));
		goto notsupported;
	}

	if (0 != (command->flags & CF_USERPARAMETER))
	{
		if (0 != (command->flags & CF_HAVEPARAMS))
		{
			char	*parameters = NULL, error[MAX_STRING_LEN];

			if (FAIL == replace_param(command->test_param, &request, &parameters, error, sizeof(error)))
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, error));
				goto notsupported;
			}

			free_request_params(&request);
			add_request_param(&request, parameters);
		}
		else
		{
			free_request_params(&request);
			add_request_param(&request, zbx_strdup(NULL, command->test_param));
		}
	}

	if (SYSINFO_RET_OK != command->function(&request, result))
	{
		/* "return NOTSUPPORTED;" would be more appropriate here for preserving original error */
		/* message in "result" but would break things relying on ZBX_NOTSUPPORTED message. */
		if (0 != (command->flags & CF_MODULE) && 0 == ISSET_MSG(result))
			SET_MSG_RESULT(result, zbx_strdup(NULL, ZBX_NOTSUPPORTED_MSG));

		goto notsupported;
	}

	ret = SUCCEED;

notsupported:
	free_request(&request);

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是：为一个 AGENT_RESULT 类型的结构体添加日志信息。具体来说，它分配内存空间用于存储日志信息，初始化日志结构体，将给定的值复制到日志结构体的 value 字段中，并设置结果类型标志位，表示该结果包含日志信息。
 ******************************************************************************/
// 定义一个静态函数，用于添加日志结果
static void add_log_result(AGENT_RESULT *result, const char *value)
{
    // 为结果结构体分配日志部分的内存空间
    result->log = (zbx_log_t *)zbx_malloc(result->log, sizeof(zbx_log_t));
/******************************************************************************
 * *
 *整个代码块的主要目的是设置一个AGENT_RESULT结构体的结果类型。根据传入的value_type参数，分别处理不同类型的数据，如uint64、float、字符串、文本和日志。最后返回操作是否成功。
 ******************************************************************************/
int	set_result_type(AGENT_RESULT *result, int value_type, char *c)
{
	// 定义一个zbx_uint64_t类型的变量value_uint64，用于存储uint64类型的值
	zbx_uint64_t	value_uint64;
	// 定义一个整型变量ret，初始值为FAIL（失败）
	int		ret = FAIL;

	// 断言，确保传入的result指针不为空
	assert(result);

	// 使用switch语句根据value_type的不同进行分支处理
	switch (value_type)
	{
		// 定义一个double类型的变量dbl_tmp，用于存储浮点数
		double	dbl_tmp;

		case ITEM_VALUE_TYPE_UINT64:
			// 使用zbx_trim_integer函数去除字符串c中的空格
			zbx_trim_integer(c);
			// 使用del_zeros函数删除字符串c中的零
			del_zeros(c);

			// 判断字符串c是否为uint64类型，如果成功，则设置result的值
			if (SUCCEED == is_uint64(c, &value_uint64))
			{
				// 设置result的值为value_uint64
				SET_UI64_RESULT(result, value_uint64);
				// 将ret的值设置为SUCCEED（成功）
				ret = SUCCEED;
			}
			break;
		case ITEM_VALUE_TYPE_FLOAT:
			// 使用zbx_trim_float函数去除字符串c中的空格
			zbx_trim_float(c);

			// 判断字符串c是否为double类型，如果成功，则设置result的值
			if (SUCCEED == is_double(c, &dbl_tmp))
			{
				// 设置result的值为dbl_tmp
				SET_DBL_RESULT(result, dbl_tmp);
				// 将ret的值设置为SUCCEED（成功）
				ret = SUCCEED;
			}
			break;
		case ITEM_VALUE_TYPE_STR:
			// 使用zbx_replace_invalid_utf8函数替换字符串c中的无效utf8字符
			zbx_replace_invalid_utf8(c);
			// 设置result的值为zbx_strdup(NULL, c)（即字符串c的拷贝）
			SET_STR_RESULT(result, zbx_strdup(NULL, c));
			// 将ret的值设置为SUCCEED（成功）
			ret = SUCCEED;
			break;
		case ITEM_VALUE_TYPE_TEXT:
			// 使用zbx_replace_invalid_utf8函数替换字符串c中的无效utf8字符
			zbx_replace_invalid_utf8(c);
			// 设置result的值为zbx_strdup(NULL, c)（即字符串c的拷贝）
			SET_TEXT_RESULT(result, zbx_strdup(NULL, c));
			// 将ret的值设置为SUCCEED（成功）
			ret = SUCCEED;
			break;
		case ITEM_VALUE_TYPE_LOG:
			// 使用zbx_replace_invalid_utf8函数替换字符串c中的无效utf8字符
			zbx_replace_invalid_utf8(c);
			// 使用add_log_result函数将字符串c添加到日志结果中
			add_log_result(result, c);
			// 将ret的值设置为SUCCEED（成功）
			ret = SUCCEED;
			break;
	}

	// 返回ret的值，即操作是否成功
	return ret;
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */*
 * * 头文件声明
 * */
 *#include \"agent_result.h\"
 *
 */*
 * * 函数声明
 * * void set_result_meta(AGENT_RESULT *result, zbx_uint64_t lastlogsize, int mtime);
 * */
 *
 */*
 * * set_result_meta函数实现
 * * 设置AgentResult结构体中的元数据，包括lastlogsize和mtime成员
 * */
 *void\tset_result_meta(AGENT_RESULT *result, zbx_uint64_t lastlogsize, int mtime)
 *{
 *\t/* 将lastlogsize的值赋给result结构体中的lastlogsize成员 */
 *\tresult->lastlogsize = lastlogsize;
 *
 *\t/* 将mtime的值赋给result结构体中的mtime成员 */
 *\tresult->mtime = mtime;
 *
 *\t/* 将result结构体的type成员中的AR_META位设置为1，表示设置了元数据 */
 *\tresult->type |= AR_META;
 *}
 *```
 ******************************************************************************/
/*
 * 函数名：set_result_meta
 * 参数：
 *   AGENT_RESULT *result：指向AgentResult结构体的指针，用于存储结果数据
 *   zbx_uint64_t lastlogsize：最后一个日志文件大小，用于设置结果的lastlogsize成员
 *   int mtime：最后一次修改时间，用于设置结果的mtime成员
 * 返回值：无
 * 主要目的：设置AgentResult结构体中的元数据，包括lastlogsize和mtime成员
 * 输出：无
 */
void	set_result_meta(AGENT_RESULT *result, zbx_uint64_t lastlogsize, int mtime)
{
	/* 将lastlogsize的值赋给result结构体中的lastlogsize成员 */
	result->lastlogsize = lastlogsize;

	/* 将mtime的值赋给result结构体中的mtime成员 */
	result->mtime = mtime;

	/* 将result结构体的type成员中的AR_META位设置为1，表示设置了元数据 */
	result->type |= AR_META;
}

	}

	return ret;
}

void	set_result_meta(AGENT_RESULT *result, zbx_uint64_t lastlogsize, int mtime)
{
	result->lastlogsize = lastlogsize;
	result->mtime = mtime;
	result->type |= AR_META;
}

static zbx_uint64_t	*get_result_ui64_value(AGENT_RESULT *result)
{
	zbx_uint64_t	value;
/******************************************************************************
 * *
 *整个代码块的主要目的是将不同类型的数据（UI64、DBL、STR、TEXT）转换为UI64类型，并返回对应的UI64值。在转换过程中，对数据进行了一些预处理，如去除前后空格和删除末尾的零。如果判断失败，返回NULL。
 ******************************************************************************/
// 断言，确保传入的result不为NULL
assert(result);

// 判断result的数据类型，分别为UI64、DBL、STR、TEXT和MESSAGE
if (0 != ISSET_UI64(result)) // 判断result为UI64类型
{
    /* nothing to do */
    // 什么都不做，因为UI64类型不需要处理
}
else if (0 != ISSET_DBL(result)) // 判断result为DBL类型
{
    SET_UI64_RESULT(result, result->dbl); // 将DBL类型的结果转换为UI64类型
}
else if (0 != ISSET_STR(result)) // 判断result为STR类型
{
    zbx_trim_integer(result->str); // 去除字符串前后的空格
    del_zeros(result->str); // 删除字符串中的零

    if (SUCCEED != is_uint64(result->str, &value)) // 判断字符串是否为UI64类型
        return NULL; // 如果不是UI64类型，返回NULL

    SET_UI64_RESULT(result, value); // 将字符串转换为UI64类型
}
else if (0 != ISSET_TEXT(result)) // 判断result为TEXT类型
{
    zbx_trim_integer(result->text); // 去除TEXT类型的前后的空格
    del_zeros(result->text); // 删除TEXT类型中的零

    if (SUCCEED != is_uint64(result->text, &value)) // 判断TEXT类型是否为UI64类型
        return NULL; // 如果不是UI64类型，返回NULL

    SET_UI64_RESULT(result, value); // 将TEXT类型转换为UI64类型
}
/* skip AR_MESSAGE - it is information field */
// 跳过MESSAGE类型，因为它是一个信息字段，不需要处理
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个 AGENT_RESULT 类型的指针中获取双精度浮点数的值，并将其存储到该指针对应的双精度浮点数位置。为实现这个目的，代码首先判断传入的 AGENT_RESULT 指针是否有效，然后根据不同的数据类型标志位，对字符串或文本进行处理，将其转换为双精度浮点数，并存储到结果结构体中的双精度浮点数位置。如果找到了合适的数据类型，则返回该数据类型的指针；如果没有找到，则返回 NULL。
 ******************************************************************************/
/* 定义一个函数，用于从 AGENT_RESULT 类型的指针中获取双精度浮点数的值
 * 参数：AGENT_RESULT *result，指向 AGENT_RESULT 结构体的指针
 * 返回值：静态双精度浮点数指针
 */
static double *get_result_dbl_value(AGENT_RESULT *result)
{
	/* 声明一个双精度浮点数变量 value，用于存储结果值 */
	double	value;

	/* 断言，确保传入的 result 指针不为空 */
	assert(result);

	/* 判断 result 中的 ISSET_DBL 标志位是否设置，即是否已经设置了双精度浮点数值
	 * 如果已经设置了，则直接返回 NULL，表示无需再次处理
	 */
	if (0 != ISSET_DBL(result))
	{
		/* 空操作，什么都不做 */
	}
	else if (0 != ISSET_UI64(result)) // 判断 result 中的 ISSET_UI64 标志位是否设置，即是否已经设置了无符号64位整数值
	{
		/* 将 result 中的无符号64位整数转换为双精度浮点数 */
		SET_DBL_RESULT(result, result->ui64);
	}
	else if (0 != ISSET_STR(result)) // 判断 result 中的 ISSET_STR 标志位是否设置，即是否已经设置了字符串值
	{
		/* 对字符串进行去空格处理 */
		zbx_trim_float(result->str);

		/* 判断字符串是否为合法的双精度浮点数格式，并将值存储到 value 变量中 */
		if (SUCCEED != is_double(result->str, &value))
			return NULL;

		/* 将 value 存储到 result 中的双精度浮点数位置 */
		SET_DBL_RESULT(result, value);
	}
	else if (0 != ISSET_TEXT(result)) // 判断 result 中的 ISSET_TEXT 标志位是否设置，即是否已经设置了文本值
	{
		/* 对文本进行去空格处理 */
		zbx_trim_float(result->text);

		/* 判断文本是否为合法的双精度浮点数格式，并将值存储到 value 变量中 */
		if (SUCCEED != is_double(result->text, &value))
			return NULL;

		/* 将 value 存储到 result 中的双精度浮点数位置 */
		SET_DBL_RESULT(result, value);
	}
	/* 跳过 AR_MESSAGE 标志位，因为它是一个信息字段 */

	/* 判断 result 中的 ISSET_DBL 标志位是否设置，如果设置，则返回 result 中的双精度浮点数指针 */
	if (0 != ISSET_DBL(result))
		return &result->dbl;

	/* 如果没有找到合适的数据类型，则返回 NULL */
	return NULL;
}

	}
	/* skip AR_MESSAGE - it is information field */

	if (0 != ISSET_DBL(result))
		return &result->dbl;

	return NULL;
}

static char	**get_result_str_value(AGENT_RESULT *result)
{
	char	*p, tmp;
/******************************************************************************
 * *
 *整个代码块的主要目的是根据result的类型进行不同的处理，将结果转换为字符串并返回。具体来说：
 *
 *1. 判断result的类型，如果是字符串类型，则不做任何操作。
 *2. 如果是文本类型，则只复制字符串的第一行，并将原始字符串中的换行符替换为NUL字符。
 *3. 如果是整数类型，则使用zbx_dsprintf()函数将整数转换为字符串。
 *4. 如果是浮点数类型，则使用zbx_dsprintf()函数将浮点数转换为字符串。
 *5. 跳过 AR_MESSAGE 字段，因为它是一个信息字段。
 *6. 如果有字符串类型的result，返回result的字符指针。
 *7. 如果没有找到符合条件的类型，返回NULL。
 ******************************************************************************/
// 断言，确保result不为NULL
assert(result);

// 判断result的类型
if (0 != ISSET_STR(result))
{
    /* 啥都不做，因为result是字符串类型 */
}
else if (0 != ISSET_TEXT(result))
{
    /* 注意：只复制字符串的第一行 */
    for (p = result->text; '\0' != *p && '\r' != *p && '\
' != *p; p++);
    tmp = *p; /* 记住result->text的字符 */
    *p = '\0'; /* 替换为NUL字符 */
    SET_STR_RESULT(result, zbx_strdup(NULL, result->text)); /* 复制字符串 */
    *p = tmp; /* 恢复result->text的字符 */
}
else if (0 != ISSET_UI64(result))
{
    SET_STR_RESULT(result, zbx_dsprintf(NULL, ZBX_FS_UI64, result->ui64));
}
else if (0 != ISSET_DBL(result))
{
    SET_STR_RESULT(result, zbx_dsprintf(NULL, ZBX_FS_DBL, result->dbl));
}
/* 跳过 AR_MESSAGE，因为它是一个信息字段 */

// 如果有字符串类型的result，返回result的字符指针
if (0 != ISSET_STR(result))
    return &result->str;

// 如果没有找到符合条件的类型，返回NULL
return NULL;


static char	**get_result_text_value(AGENT_RESULT *result)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是根据 result 的类型进行不同方式的处理，将其转换为文本类型并设置到 SET_TEXT_RESULT 函数中。输出结果为一个指向文本类型的指针。如果 result 不是文本类型，则返回 NULL。
 ******************************************************************************/
// 断言，确保 result 非空
assert(result);

// 判断 result 的类型
if (0 != ISSET_TEXT(result))
{
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于获取 AGENT_RESULT 结构体中的日志值
 * 参数：
 *   AGENT_RESULT *result：指向 AGENT_RESULT 结构体的指针
 * 返回值：
 *   zbx_log_t 类型的指针，指向创建的日志结构体
 * 函数主要目的：
 *   从 AGENT_RESULT 结构体中提取日志值，并创建一个新的日志结构体返回
 */
static zbx_log_t *get_result_log_value(AGENT_RESULT *result)
{
	// 判断 result 是否设置了日志值 ISSET_LOG
	if (0 != ISSET_LOG(result))
	{
		// 如果设置了日志值，直接返回 result 中的日志指针
		return result->log;
	}

	// 判断 result 是否设置了值 ISSET_VALUE
	if (0 != ISSET_VALUE(result))
	{
		// 为 result 分配一个新的日志结构体空间
		result->log = (zbx_log_t *)zbx_malloc(result->log, sizeof(zbx_log_t));

		// 初始化新的日志结构体
		zbx_log_init(result->log);

		// 判断 result 中是否设置了字符串、文本、无符号整数或双精度浮点数
		if (0 != ISSET_STR(result))
		{
			// 如果是字符串，复制到日志结构体的 value 字段
			result->log->value = zbx_strdup(result->log->value, result->str);
		}
		else if (0 != ISSET_TEXT(result))
		{
			// 如果是文本，复制到日志结构体的 value 字段
			result->log->value = zbx_strdup(result->log->value, result->text);
		}
		else if (0 != ISSET_UI64(result))
		{
			// 如果是无符号整数，格式化后复制到日志结构体的 value 字段
			result->log->value = zbx_dsprintf(result->log->value, ZBX_FS_UI64, result->ui64);
		}
		else if (0 != ISSET_DBL(result))
		{
			// 如果是双精度浮点数，格式化后复制到日志结构体的 value 字段
			result->log->value = zbx_dsprintf(result->log->value, ZBX_FS_DBL, result->dbl);
		}

		// 设置 result 的类型标志位 AR_LOG
		result->type |= AR_LOG;

		// 返回新创建的日志结构体指针
		return result->log;
	}

	// 如果没有设置值，返回 NULL
	return NULL;
}

static zbx_log_t	*get_result_log_value(AGENT_RESULT *result)
{
	if (0 != ISSET_LOG(result))
		return result->log;

	if (0 != ISSET_VALUE(result))
	{
		result->log = (zbx_log_t *)zbx_malloc(result->log, sizeof(zbx_log_t));

		zbx_log_init(result->log);

		if (0 != ISSET_STR(result))
			result->log->value = zbx_strdup(result->log->value, result->str);
		else if (0 != ISSET_TEXT(result))
			result->log->value = zbx_strdup(result->log->value, result->text);
		else if (0 != ISSET_UI64(result))
			result->log->value = zbx_dsprintf(result->log->value, ZBX_FS_UI64, result->ui64);
		else if (0 != ISSET_DBL(result))
			result->log->value = zbx_dsprintf(result->log->value, ZBX_FS_DBL, result->dbl);

		result->type |= AR_LOG;

		return result->log;
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: get_result_value_by_type                                         *
 *                                                                            *
 * Purpose: return value of result in special type                            *
 *          if value missing, convert existing value to requested type        *
 *                                                                            *
 * Return value:                                                              *
 *         NULL - if value is missing or can't be converted                   *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:  better use definitions                                          *
 *                GET_UI64_RESULT                                             *
 *                GET_DBL_RESULT                                              *
 *                GET_STR_RESULT                                              *
 *                GET_TEXT_RESULT                                             *
 *                GET_LOG_RESULT                                              *
 *                GET_MSG_RESULT                                              *
 *                                                                            *
 *    AR_MESSAGE - skipped in conversion                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的结果指针（AGENT_RESULT 类型）和所需类型（int 类型），获取对应类型的值。代码中使用了 switch 语句进行分支，根据不同类型调用相应的获取值函数，并将返回值转换为 void 指针类型。如果在分支中找不到对应类型，则返回 NULL。
 ******************************************************************************/
// 定义一个函数，用于根据结果类型的不同，获取对应类型的值
void *get_result_value_by_type(AGENT_RESULT *result, int require_type)
{
    // 检查传入的 result 指针是否有效
    assert(result);

    // 使用 switch 语句根据 require_type 判断结果类型
    switch (require_type)
    {
        // 如果是 AR_UINT64 类型，则返回 get_result_ui64_value(result) 的地址
        case AR_UINT64:
            return (void *)get_result_ui64_value(result);
        // 如果是 AR_DOUBLE 类型，则返回 get_result_dbl_value(result) 的地址
        case AR_DOUBLE:
            return (void *)get_result_dbl_value(result);
        // 如果是 AR_STRING 类型，则返回 get_result_str_value(result) 的地址
        case AR_STRING:
            return (void *)get_result_str_value(result);
        // 如果是 AR_TEXT 类型，则返回 get_result_text_value(result) 的地址
        case AR_TEXT:
            return (void *)get_result_text_value(result);
        // 如果是 AR_LOG 类型，则返回 get_result_log_value(result) 的地址
        case AR_LOG:
            return (void *)get_result_log_value(result);
        // 如果是 AR_MESSAGE 类型，且 ISSET_MSG(result) 不为零，则返回 result->msg 的地址
        case AR_MESSAGE:
            if (0 != ISSET_MSG(result))
                return (void *)(&result->msg);
            break;
        // 如果不是以上类型，则返回 NULL
        default:
            break;
    }

    // 如果找不到对应类型，返回 NULL
    return NULL;
}


/******************************************************************************
 *                                                                            *
 * Function: unquote_key_param                                                *
 *                                                                            *
 * Purpose: unquotes special symbols in item key parameter                    *
 *                                                                            *
 * Parameters: param - [IN/OUT] item key parameter                            *
 *                                                                            *
 * Comments:                                                                  *
 *   "param"     => param                                                     *
 *   "\"param\"" => "param"                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是去除字符串中的双引号。函数接受一个字符指针作为参数，遍历该字符串，遇到双引号则跳过，遇到反斜杠且紧跟双引号也跳过。最后将处理后的字符串返回。
 ******************************************************************************/
void unquote_key_param(char *param) // 定义一个函数，参数为一个字符指针，该函数的主要目的是去除字符串中的双引号
{
	char *dst; // 定义一个字符指针，用于存储处理后的字符串

	if ('"' != *param) // 如果参数字符串的开始不是双引号，直接返回，不进行处理
		return;

	for (dst = param++; '\0' != *param; param++) // 遍历字符串，直到遇到字符串结束符'\0'
	{
		if ('\\' == *param && '"' == param[1]) // 如果当前字符是反斜杠（转义字符），且下一个字符是双引号，则跳过这个双引号，继续遍历
			continue;

		*dst++ = *param; // 否则，将当前字符添加到处理后的字符串中
	}
	*--dst = '\0'; // 遍历结束后，将字符串结束符'\0'添加到处理后的字符串中，并返回指向该字符串的指针
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的字符串进行处理，使其符合C语言字符串的要求。具体来说，就是这个函数会对输入的字符串进行检查，如果满足以下条件：
 *
 *1. 字符串不以双引号、空格、方括号、逗号或大括号结尾；
 *2. 字符串中没有双引号；
 *3. 字符串中没有逗号或大括号。
 *
 *如果满足这些条件，函数会将字符串的双引号去掉，并在字符串的末尾添加一个双引号，以使其成为一个有效的C语言字符串。如果字符串中包含需要处理的双引号，函数会将其替换为反斜杠。最后，函数返回成功（0）。
 ******************************************************************************/
int	quote_key_param(char **param, int forced)	// 定义一个函数，接收两个参数，一个是指向字符指针的指针（char **param），另一个是强迫参数（int forced）。这个函数的主要目的是对字符串进行双引号处理。

{
	size_t	sz_src, sz_dst;		// 定义两个大小为size_t类型的变量，分别表示源字符串的长度（sz_src）和目标字符串的长度（sz_dst）。

	if (0 == forced)			// 如果强迫参数为0，即不需要强制处理双引号。
	{
		if ('"' != **param && ' ' != **param && '[' != **param && NULL == strchr(*param, ',') &&
				NULL == strchr(*param, ']'))
		{
			return SUCCEED;
		}
	}

	if (0 != (sz_src = strlen(*param)) && '\\' == (*param)[sz_src - 1])	// 如果源字符串长度不为0，且最后一个字符是反斜杠（表示需要处理双引号），
		return FAIL;			// 则返回失败。

	sz_dst = zbx_get_escape_string_len(*param, "\"") + 3;	// 计算目标字符串的长度，需要加上双引号的长度1，以及前后空白字符，共计3。

	*param = (char *)zbx_realloc(*param, sz_dst);	// 重新分配内存，将源字符串的长度扩展到目标长度。

	(*param)[--sz_dst] = '\0';		// 在目标字符串的末尾添加一个空字符，使其成为一个有效的C语言字符串。
	(*param)[--sz_dst] = '"';		// 在目标字符串的末尾添加双引号。

	while (0 < sz_src)			// 遍历源字符串，将字符复制到目标字符串。
	{
		(*param)[--sz_dst] = (*param)[--sz_src];	// 从源字符串中复制字符。
		if ('"' == (*param)[sz_src])		// 如果当前字符是双引号，
			(*param)[--sz_dst] = '\\';		// 在目标字符串中添加一个反斜杠。
	}
	(*param)[--sz_dst] = '"';		// 在目标字符串的末尾添加双引号。

	return SUCCEED;			// 函数执行成功，返回0。
}


	if (0 != (sz_src = strlen(*param)) && '\\' == (*param)[sz_src - 1])
		return FAIL;

	sz_dst = zbx_get_escape_string_len(*param, "\"") + 3;

	*param = (char *)zbx_realloc(*param, sz_dst);

	(*param)[--sz_dst] = '\0';
	(*param)[--sz_dst] = '"';

	while (0 < sz_src)
	{
		(*param)[--sz_dst] = (*param)[--sz_src];
		if ('"' == (*param)[sz_src])
			(*param)[--sz_dst] = '\\';
	}
	(*param)[--sz_dst] = '"';

	return SUCCEED;
}

#ifdef HAVE_KSTAT_H
zbx_uint64_t	get_kstat_numeric_value(const kstat_named_t *kn)
{
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * 根据kn指针中的数据类型，返回对应的数据值。
 * * @param kn：包含统计数据类型的结构体指针。
 * * @return：根据数据类型返回相应的数据值，否则返回0。
 * */
 *switch (kn->data_type)
 *{
 *    case KSTAT_DATA_INT32:
 *        return kn->value.i32;
 *    case KSTAT_DATA_UINT32:
 *        return kn->value.ui32;
 *    case KSTAT_DATA_INT64:
 *        return kn->value.i64;
 *    case KSTAT_DATA_UINT64:
 *        return kn->value.ui64;
 *    default:
 *        THIS_SHOULD_NEVER_HAPPEN;
 *        return 0;
 *}
 *```
 ******************************************************************************/
// 这段代码的主要目的是根据kn指针中的数据类型，返回对应的数据值。
switch (kn->data_type) // 使用switch语句根据kn指针的数据类型进行分支处理
{
    case KSTAT_DATA_INT32: // 当数据类型为KSTAT_DATA_INT32时，即整型32位数据
        return kn->value.i32; // 返回该整型数据的值
    case KSTAT_DATA_UINT32: // 当数据类型为KSTAT_DATA_UINT32时，即无符号整型32位数据
        return kn->value.ui32; // 返回该无符号整型数据的值
    case KSTAT_DATA_INT64: // 当数据类型为KSTAT_DATA_INT64时，即整型64位数据
        return kn->value.i64; // 返回该整型数据的值
    case KSTAT_DATA_UINT64: // 当数据类型为KSTAT_DATA_UINT64时，即无符号整型64位数据
/******************************************************************************
 * *
 *整个代码块的主要目的是序列化代理返回的结果。该函数接收代理返回的ret值和result结构体，根据result的不同类型，将相应的内容序列化为字符串，并存储在提前分配好的数据空间中。最后将序列化后的数据返回。
 ******************************************************************************/
// 定义一个函数，用于序列化代理结果
static void serialize_agent_result(char **data, size_t *data_alloc, size_t *data_offset, int agent_ret, AGENT_RESULT *result)
{
    // 定义一些变量
    char **pvalue, result_type;
    size_t value_len;

    // 判断代理返回值是否正常
    if (SYSINFO_RET_OK == agent_ret)
    {
        // 判断结果类型，并赋值给result_type
        if (ISSET_TEXT(result))
            result_type = 't';
        else if (ISSET_STR(result))
            result_type = 's';
        else if (ISSET_UI64(result))
            result_type = 'u';
        else if (ISSET_DBL(result))
            result_type = 'd';
        else if (ISSET_MSG(result))
            result_type = 'm';
        else
            result_type = '-';
    }
    else
        result_type = 'm';

    // 根据result_type切换处理方式
    switch (result_type)
    {
        case 't':
        case 's':
        case 'u':
        case 'd':
            pvalue = GET_TEXT_RESULT(result);
            break;
        case 'm':
            pvalue = GET_MSG_RESULT(result);
            break;
        default:
            pvalue = NULL;
    }

    // 获取值的长度
    if (NULL != pvalue)
    {
        value_len = strlen(*pvalue) + 1;
    }
    else
    {
        value_len = 0;
        result_type = '-';
    }

    // 检查分配的数据空间是否足够
    if (*data_alloc - *data_offset < value_len + 1 + sizeof(int))
    {
        // 扩充数据空间
        while (*data_alloc - *data_offset < value_len + 1 + sizeof(int))
            *data_alloc *= 1.5;

        // 重新分配数据内存
        *data = (char *)zbx_realloc(*data, *data_alloc);
    }

    // 将代理返回值写入数据
    memcpy(*data + *data_offset, &agent_ret, sizeof(int));
    *data_offset += sizeof(int);

    // 将结果类型写入数据
    (*data)[(*data_offset)++] = result_type;

    // 将值写入数据（除非结果类型为'-'）
    if ('-' != result_type)
    {
        memcpy(*data + *data_offset, *pvalue, value_len);
        *data_offset += value_len;
    }
}

		value_len = strlen(*pvalue) + 1;
	}
	else
	{
		value_len = 0;
		result_type = '-';
	}

	if (*data_alloc - *data_offset < value_len + 1 + sizeof(int))
	{
		while (*data_alloc - *data_offset < value_len + 1 + sizeof(int))
			*data_alloc *= 1.5;

/******************************************************************************
 * *
 *整个代码块的主要目的是用于反序列化 agent 返回的结果，根据结果的数据类型设置相应的结果字段。具体来说，代码逐行注释如下：
 *
 *1. 定义变量 ret、agent_ret 和 type，用于存储反序列化的结果。
 *2. 将 agent_ret 大小写入到 data 指向的内存空间。
 *3. 移动数据指针，以避免覆盖其他数据。
 *4. 读取下一个字符，即数据类型。
 *5. 判断数据类型是否为 'm' 或 ZBX_NOTSUPPORTED，如果是，则复制数据到 result 的消息结果字段，并返回 agent_ret。
 *6. 根据数据类型进行切换处理，设置结果类型并复制数据到 result。
 *7. 返回反序列化后的返回码或 SYSINFO_RET_FAIL，表示设置结果数据失败。
 ******************************************************************************/
// 定义一个静态函数，用于反序列化 agent 返回的结果
static int	deserialize_agent_result(char *data, AGENT_RESULT *result)
{
	// 定义一些变量，用于存储反序列化的结果
	int	ret, agent_ret;
	char	type;

	// 首先，将 agent_ret 的大小写入到 data 指向的内存空间
	memcpy(&agent_ret, data, sizeof(int));
	// 然后将数据指针向前移动 sizeof(int) 的大小，以避免覆盖其他数据
	data += sizeof(int);

	// 读取下一个字符，即数据类型
	type = *data++;

	// 判断数据类型是否为 'm' 或 ZBX_NOTSUPPORTED
	if ('m' == type || 0 == strcmp(data, ZBX_NOTSUPPORTED))
	{
		// 如果是 'm' 或 ZBX_NOTSUPPORTED，则复制数据到 result 的消息结果字段
		SET_MSG_RESULT(result, zbx_strdup(NULL, data));
		// 返回 agent_ret
		return agent_ret;
	}

	// 根据数据类型进行切换处理
	switch (type)
	{
		case 't':
			// 如果是文本类型，设置结果类型为 ITEM_VALUE_TYPE_TEXT，并将数据复制到 result
			ret = set_result_type(result, ITEM_VALUE_TYPE_TEXT, data);
			break;
		case 's':
			// 如果是字符串类型，设置结果类型为 ITEM_VALUE_TYPE_STR，并将数据复制到 result
			ret = set_result_type(result, ITEM_VALUE_TYPE_STR, data);
			break;
		case 'u':
			// 如果是无符号整数类型，设置结果类型为 ITEM_VALUE_TYPE_UINT64，并将数据复制到 result
			ret = set_result_type(result, ITEM_VALUE_TYPE_UINT64, data);
			break;
		case 'd':
			// 如果是浮点数类型，设置结果类型为 ITEM_VALUE_TYPE_FLOAT，并将数据复制到 result
			ret = set_result_type(result, ITEM_VALUE_TYPE_FLOAT, data);
			break;
		default:
			// 如果是其他未知类型，直接返回成功
			ret = SUCCEED;
	}

	// 返回反序列化后的返回码或 SYSINFO_RET_FAIL，表示设置结果数据失败
	return (FAIL == ret ? SYSINFO_RET_FAIL : agent_ret);
}

	if ('m' == type || 0 == strcmp(data, ZBX_NOTSUPPORTED))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, data));
		return agent_ret;
	}
/******************************************************************************
 * 
 ******************************************************************************/
// int zbx_execute_threaded_metric(zbx_metric_func_t metric_func, AGENT_REQUEST *request, AGENT_RESULT *result)
// 函数作用：执行一个带线程的度量指标，将度量指标的结果保存在result中
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "zbx_execute_threaded_metric";

	// 定义一些变量
	int		ret = SYSINFO_RET_OK; // 返回值
	pid_t		pid; // 进程ID
	int		fds[2], n, status; // 文件描述符数组，n为读取的字节数，status为子进程退出状态
	char		buffer[MAX_STRING_LEN], *data; // 缓冲区，data为存储数据的指针
	size_t		data_alloc = MAX_STRING_LEN, data_offset = 0; // 分配给data的最大空间，data_offset为当前数据偏移量

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __function_name, request->key);

	// 创建一个管道，用于在父进程和子进程之间通信
	if (-1 == pipe(fds))
	{
		// 管道创建失败，设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create data pipe: %s", strerror_from_system(errno)));
		ret = SYSINFO_RET_FAIL;
		goto out;
	}

	// 创建子进程，执行度量指标
	if (-1 == (pid = zbx_fork()))
	{
		// 子进程创建失败，关闭管道并设置错误信息返回失败
		close(fds[0]);
		close(fds[1]);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot fork data process: %s", strerror_from_system(errno)));
		ret = SYSINFO_RET_FAIL;
		goto out;
	}

	// 如果是子进程，执行以下操作：
	if (0 == pid)
	{
		// 设置度量指标线程信号处理程序
		zabbix_log(LOG_LEVEL_DEBUG, "executing in data process for key:'%s'", request->key);

		// 关闭管道中的读端口
		close(fds[0]);

		// 执行度量指标函数，并将结果序列化
		ret = metric_func(request, result);
		// 将序列化后的结果写入管道中的写端口
		serialize_agent_result(&data, &data_alloc, &data_offset, ret, result);

		// 写入完毕后，关闭管道并退出子进程
		ret = write_all(fds[1], data, data_offset);
		zbx_free(data);
		free_result(result);
		close(fds[1]);
		exit(SUCCEED == ret ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	// 关闭管道中的写端口
	close(fds[1]);

	// 开启报警机制，超时报警
	zbx_alarm_on(CONFIG_TIMEOUT);

	// 循环读取管道中的数据，直到读取完毕或超时
	while (0 != (n = read(fds[0], buffer, sizeof(buffer))))
	{
		// 如果超时，设置错误信息并杀死子进程
		if (SUCCEED == zbx_alarm_timed_out())
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while waiting for data."));
			kill(pid, SIGKILL);
			ret = SYSINFO_RET_FAIL;
			break;
		}

		// 读取管道失败，设置错误信息并杀死子进程
		if (-1 == n)
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为 `zbx_execute_threaded_metric` 的函数，该函数接收三个参数：`metric_func`、`request` 和 `result`。这个函数的作用是将计量函数（`metric_func`）在一个新的线程中执行，以便在主线程中继续处理其他任务。在此过程中，线程会等待计量函数的执行结果，并在超时的情况下终止线程。
 *
 *以下是代码的详细注释：
 *
 *1. 声明变量：定义线程句柄、线程参数、计量线程参数、返回码、终止线程标志等。
 *2. 打印日志：记录函数调用和请求键。
 *3. 创建超时事件：为数据线程创建一个超时事件。
 *4. 设置线程参数：将计量函数、请求和结果等数据传递给线程。
 *5. 启动线程：调用 `zbx_thread_start` 函数启动线程。
 *6. 检查线程启动状态：检查线程是否启动成功。
 *7. 等待线程结果：等待线程执行完成或超时。
 *8. 设置错误信息：如果等待超时或线程启动失败，设置错误信息。
 *9. 终止线程：在超时或收到错误信息时，终止线程。
 *10. 关闭线程和超时事件：释放线程和超时事件的资源。
 *11. 打印日志：记录函数结束和返回值。
 *12. 返回结果：根据线程执行情况返回成功或失败。
 ******************************************************************************/
int zbx_execute_threaded_metric(zbx_metric_func_t metric_func, AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char			*__function_name = "zbx_execute_threaded_metric";

	// 声明变量
	ZBX_THREAD_HANDLE		thread;
	zbx_thread_args_t		thread_args;
	zbx_metric_thread_args_t	metric_args = {metric_func, request, result, ZBX_MUTEX_THREAD_DENIED |
							ZBX_MUTEX_LOGGING_DENIED};
	DWORD				rc;
	BOOL				terminate_thread = FALSE;

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __function_name, request->key);

	// 创建超时事件
	if (NULL == (metric_args.timeout_event = CreateEvent(NULL, TRUE, FALSE, NULL)))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create timeout event for data thread: %s",
				strerror_from_system(GetLastError())));
		return SYSINFO_RET_FAIL;
	}

	// 设置线程参数
	thread_args.args = (void *)&metric_args;

	// 启动线程
	zbx_thread_start(agent_metric_thread, &thread_args, &thread);

	// 检查线程启动状态
	if (ZBX_THREAD_ERROR == thread)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot start data thread: %s",
				strerror_from_system(GetLastError())));
		// 关闭超时事件
		CloseHandle(metric_args.timeout_event);
		return SYSINFO_RET_FAIL;
	}

	/* 1000 是用于将秒转换为毫秒的乘数 */
	if (WAIT_FAILED == (rc = WaitForSingleObject(thread, CONFIG_TIMEOUT * 1000)))
	{
		// 意外错误

		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot wait for data: %s",
				strerror_from_system(GetLastError())));
		// 终止线程
		terminate_thread = TRUE;
	}
	else if (WAIT_TIMEOUT == rc)
	{
		// 设置超时信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while waiting for data."));

		/* 超时；通知线程清理并退出，如果卡住则终止它 */

		if (FALSE == SetEvent(metric_args.timeout_event))
		{
			zabbix_log(LOG_LEVEL_ERR, "SetEvent() failed: %s", strerror_from_system(GetLastError()));
			// 终止线程
			terminate_thread = TRUE;
		}
		else
		{
			DWORD	timeout_rc = WaitForSingleObject(thread, 3000);	/* 等待最多3秒 */

			if (WAIT_FAILED == timeout_rc)
			{
				zabbix_log(LOG_LEVEL_ERR, "Waiting for data failed: %s",
						strerror_from_system(GetLastError()));
				// 终止线程
				terminate_thread = TRUE;
			}
			else if (WAIT_TIMEOUT == timeout_rc)
			{
				zabbix_log(LOG_LEVEL_ERR, "Stuck data thread");
				// 终止线程
				terminate_thread = TRUE;
			}
			/* timeout_rc 必须是 WAIT_OBJECT_0（已信号）*/
		}
	}

	// 终止线程
	if (TRUE == terminate_thread)
	{
		if (FALSE != TerminateThread(thread, 0))
		{
			zabbix_log(LOG_LEVEL_ERR, "%s(): TerminateThread() for %s[%s%s] succeeded", __function_name,
					request->key, (0 < request->nparam) ? request->params[0] : "",
					(1 < request->nparam) ? ",..." : "");
		}
		else
		{
			zabbix_log(LOG_LEVEL_ERR, "%s(): TerminateThread() for %s[%s%s] failed: %s", __function_name,
					request->key, (0 < request->nparam) ? request->params[0] : "",
					(1 < request->nparam) ? ",..." : "",
					strerror_from_system(GetLastError()));
		}
	}

	// 关闭线程和超时事件
	CloseHandle(thread);
	CloseHandle(metric_args.timeout_event);

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s '%s'", __function_name,
			zbx_sysinfo_ret_string(metric_args.agent_ret), ISSET_MSG(result) ? result->msg : "");

	// 返回结果
	return WAIT_OBJECT_0 == rc ? metric_args.agent_ret : SYSINFO_RET_FAIL;
}

		}

		if (-1 == n)
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Error while reading data: %s", zbx_strerror(errno)));
			kill(pid, SIGKILL);
			ret = SYSINFO_RET_FAIL;
			break;
		}

		if ((int)(data_alloc - data_offset) < n + 1)
		{
			while ((int)(data_alloc - data_offset) < n + 1)
				data_alloc *= 1.5;

			data = (char *)zbx_realloc(data, data_alloc);
		}

		memcpy(data + data_offset, buffer, n);
		data_offset += n;
		data[data_offset] = '\0';
	}

	zbx_alarm_off();

	close(fds[0]);

	while (-1 == waitpid(pid, &status, 0))
	{
		if (EINTR != errno)
		{
			zabbix_log(LOG_LEVEL_ERR, "failed to wait on child processes: %s", zbx_strerror(errno));
			ret = SYSINFO_RET_FAIL;
			break;
		}
	}

	if (SYSINFO_RET_OK == ret)
	{
		if (0 == WIFEXITED(status))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Data gathering process terminated unexpectedly with"
					" error %d.", status));
			kill(pid, SIGKILL);
			ret = SYSINFO_RET_FAIL;
		}
		else if (EXIT_SUCCESS != WEXITSTATUS(status))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Data gathering process terminated with error %d.",
					status));
			ret = SYSINFO_RET_FAIL;
		}
		else
			ret = deserialize_agent_result(data, result);
	}

	zbx_free(data);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s '%s'", __function_name, zbx_sysinfo_ret_string(ret),
			ISSET_MSG(result) ? result->msg : "");
	return ret;
}
#else

ZBX_THREAD_LOCAL static zbx_uint32_t	mutex_flag = ZBX_MUTEX_ALL_ALLOW;

zbx_uint32_t get_thread_global_mutex_flag()
{
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 agent_metric_thread 的线程入口函数，该函数用于处理接收到的数据请求。线程参数存储在 zbx_metric_thread_args_t 类型的指针 args 中。函数主要完成以下操作：
 *
 *1. 保存 args 中的 mutex_flag 变量。
 *2. 打印调试日志，表示正在执行数据线程，日志中包含请求的 key 参数。
 *3. 调用 args 中的 func 函数，传入相应的参数，处理请求。
 *4. 如果 func 函数执行失败，设置 args->result 的默认值为 ZBX_NOTSUPPORTED。
 *5. 正常退出线程，返回 0。
 ******************************************************************************/
// 定义一个名为 agent_metric_thread 的线程入口函数
ZBX_THREAD_ENTRY(agent_metric_thread, data)
{
    // 定义一个指向 zbx_metric_thread_args_t 类型的指针，用于存储线程参数
    zbx_metric_thread_args_t *args = (zbx_metric_thread_args_t *)((zbx_thread_args_t *)data)->args;

    // 保存 args 中的 mutex_flag 变量
    mutex_flag = args->mutex_flag;

    // 打印调试日志，表示正在执行数据线程，参数为 args->request->key
    zabbix_log(LOG_LEVEL_DEBUG, "executing in data thread for key:'%s'", args->request->key);

    // 调用 args 中的 func 函数，传入 args->request、args->result 和 args->timeout_event 参数
    if (SYSINFO_RET_FAIL == (args->agent_ret = args->func(args->request, args->result, args->timeout_event)))
    {
        // 如果 args->result 为空，则设置默认值 ZBX_NOTSUPPORTED
        if (NULL == GET_MSG_RESULT(args->result))
            SET_MSG_RESULT(args->result, zbx_strdup(NULL, ZBX_NOTSUPPORTED));
    }

    // 正常退出线程，返回 0
    zbx_thread_exit(0);
}

	zbx_metric_thread_args_t	*args = (zbx_metric_thread_args_t *)((zbx_thread_args_t *)data)->args;
	mutex_flag = args->mutex_flag;

	zabbix_log(LOG_LEVEL_DEBUG, "executing in data thread for key:'%s'", args->request->key);

	if (SYSINFO_RET_FAIL == (args->agent_ret = args->func(args->request, args->result, args->timeout_event)))
	{
		if (NULL == GET_MSG_RESULT(args->result))
			SET_MSG_RESULT(args->result, zbx_strdup(NULL, ZBX_NOTSUPPORTED));
	}

	zbx_thread_exit(0);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_execute_threaded_metric                                      *
 *                                                                            *
 * Purpose: execute metric in a separate process/thread so it can be          *
 *          killed/terminated when timeout is detected                        *
 *                                                                            *
 * Parameters: metric_func - [IN] the metric function to execute              *
 *             ...                the metric function parameters              *
 *                                                                            *
 * Return value:                                                              *
 *         SYSINFO_RET_OK - the metric was executed successfully              *
 *         SYSINFO_RET_FAIL - otherwise                                       *
 *                                                                            *
 ******************************************************************************/
int	zbx_execute_threaded_metric(zbx_metric_func_t metric_func, AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char			*__function_name = "zbx_execute_threaded_metric";

	ZBX_THREAD_HANDLE		thread;
	zbx_thread_args_t		thread_args;
	zbx_metric_thread_args_t	metric_args = {metric_func, request, result, ZBX_MUTEX_THREAD_DENIED |
							ZBX_MUTEX_LOGGING_DENIED};
	DWORD				rc;
	BOOL				terminate_thread = FALSE;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __function_name, request->key);

	if (NULL == (metric_args.timeout_event = CreateEvent(NULL, TRUE, FALSE, NULL)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create timeout event for data thread: %s",
				strerror_from_system(GetLastError())));
		return SYSINFO_RET_FAIL;
	}

	thread_args.args = (void *)&metric_args;

	zbx_thread_start(agent_metric_thread, &thread_args, &thread);

	if (ZBX_THREAD_ERROR == thread)
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot start data thread: %s",
				strerror_from_system(GetLastError())));
		CloseHandle(metric_args.timeout_event);
		return SYSINFO_RET_FAIL;
	}

	/* 1000 is multiplier for converting seconds into milliseconds */
	if (WAIT_FAILED == (rc = WaitForSingleObject(thread, CONFIG_TIMEOUT * 1000)))
	{
		/* unexpected error */

		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot wait for data: %s",
				strerror_from_system(GetLastError())));
		terminate_thread = TRUE;
	}
	else if (WAIT_TIMEOUT == rc)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while waiting for data."));

		/* timeout; notify thread to clean up and exit, if stuck then terminate it */

		if (FALSE == SetEvent(metric_args.timeout_event))
		{
			zabbix_log(LOG_LEVEL_ERR, "SetEvent() failed: %s", strerror_from_system(GetLastError()));
			terminate_thread = TRUE;
		}
		else
		{
			DWORD	timeout_rc = WaitForSingleObject(thread, 3000);	/* wait up to 3 seconds */

			if (WAIT_FAILED == timeout_rc)
			{
				zabbix_log(LOG_LEVEL_ERR, "Waiting for data failed: %s",
						strerror_from_system(GetLastError()));
				terminate_thread = TRUE;
			}
			else if (WAIT_TIMEOUT == timeout_rc)
			{
				zabbix_log(LOG_LEVEL_ERR, "Stuck data thread");
				terminate_thread = TRUE;
			}
			/* timeout_rc must be WAIT_OBJECT_0 (signaled) */
		}
	}

	if (TRUE == terminate_thread)
	{
		if (FALSE != TerminateThread(thread, 0))
		{
			zabbix_log(LOG_LEVEL_ERR, "%s(): TerminateThread() for %s[%s%s] succeeded", __function_name,
					request->key, (0 < request->nparam) ? request->params[0] : "",
					(1 < request->nparam) ? ",..." : "");
		}
		else
		{
			zabbix_log(LOG_LEVEL_ERR, "%s(): TerminateThread() for %s[%s%s] failed: %s", __function_name,
					request->key, (0 < request->nparam) ? request->params[0] : "",
					(1 < request->nparam) ? ",..." : "",
					strerror_from_system(GetLastError()));
		}
	}

	CloseHandle(thread);
	CloseHandle(metric_args.timeout_event);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s '%s'", __function_name,
			zbx_sysinfo_ret_string(metric_args.agent_ret), ISSET_MSG(result) ? result->msg : "");

	return WAIT_OBJECT_0 == rc ? metric_args.agent_ret : SYSINFO_RET_FAIL;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_mpoints_free                                                 *
 *                                                                            *
 * Purpose: frees previously allocated mount-point structure                  *
 *                                                                            *
 * Parameters: mpoint - [IN] pointer to structure from vector                 *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * c
 *// 定义一个函数zbx_mpoints_free，接收一个zbx_mpoint_t类型的指针作为参数
 *void zbx_mpoints_free(zbx_mpoint_t *mpoint)
 *{
 *    // 使用zbx_free函数释放mpoint指向的内存空间
 *    zbx_free(mpoint);
 *}
 *```
 ******************************************************************************/
// 定义一个函数zbx_mpoints_free，接收一个zbx_mpoint_t类型的指针作为参数
void zbx_mpoints_free(zbx_mpoint_t *mpoint)
{
    // 使用zbx_free函数释放mpoint指向的内存空间
    zbx_free(mpoint);
}

整个代码块的主要目的是：释放mpoint指向的内存空间。这块代码定义了一个名为zbx_mpoints_free的函数，该函数接收一个zbx_mpoint_t类型的指针作为参数，然后使用zbx_free函数释放该指针所指向的内存空间。

注释好的代码块如下：


