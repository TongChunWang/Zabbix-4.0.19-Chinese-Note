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
#include "zbxmodules.h"

#include "log.h"
#include "sysinfo.h"
#include "zbxalgo.h"

#define ZBX_MODULE_FUNC_INIT			"zbx_module_init"
#define ZBX_MODULE_FUNC_API_VERSION		"zbx_module_api_version"
#define ZBX_MODULE_FUNC_ITEM_LIST		"zbx_module_item_list"
#define ZBX_MODULE_FUNC_ITEM_PROCESS		"zbx_module_item_process"
#define ZBX_MODULE_FUNC_ITEM_TIMEOUT		"zbx_module_item_timeout"
#define ZBX_MODULE_FUNC_UNINIT			"zbx_module_uninit"
#define ZBX_MODULE_FUNC_HISTORY_WRITE_CBS	"zbx_module_history_write_cbs"

static zbx_vector_ptr_t	modules;

zbx_history_float_cb_t		*history_float_cbs = NULL;
zbx_history_integer_cb_t	*history_integer_cbs = NULL;
zbx_history_string_cb_t		*history_string_cbs = NULL;
zbx_history_text_cb_t		*history_text_cbs = NULL;
zbx_history_log_cb_t		*history_log_cbs = NULL;

/******************************************************************************
 *                                                                            *
 * Function: zbx_register_module_items                                        *
 *                                                                            *
 * Purpose: add items supported by module                                     *
 *                                                                            *
 * Parameters: metrics       - list of items supported by module              *
 *             error         - error buffer                                   *
 *             max_error_len - error buffer size                              *
 *                                                                            *
 * Return value: SUCCEED - all module items were added or there were none     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是注册zbx模块的指标项。循环遍历metrics数组中的每个元素，对每个元素进行处理，将它们的标志位设置为来自可加载模块的标志，并调用add_metric函数将它们添加到系统中。如果添加过程中出现错误，返回FAIL，否则返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数，用于注册zbx模块的指标项
static int	zbx_register_module_items(ZBX_METRIC *metrics, char *error, size_t max_error_len)
{
	// 定义一个整型变量i，用于循环计数
	int	i;

	// 使用一个for循环，遍历metrics数组中的每个元素
	for (i = 0; NULL != metrics[i].key; i++)
	{
		/* 只接受来自模块项的CF_HAVEPARAMS标志 */
		metrics[i].flags &= CF_HAVEPARAMS;
		/* 将标志位设置为来自可加载模块的标志 */
		metrics[i].flags |= CF_MODULE;

		// 如果添加指标项失败，返回FAIL
		if (SUCCEED != add_metric(&metrics[i], error, max_error_len))
			return FAIL;
	}

	// 如果没有出现错误，返回SUCCEED
	return SUCCEED;
/******************************************************************************
 * *
 *这段代码的主要目的是用于注册一个模块到zbx系统中。它接受两个参数，一个是模块所依赖的库的指针，另一个是模块的名称。在函数内部，首先为新创建的模块分配内存空间，然后设置模块的依赖库指针和名称。最后，将新创建的模块添加到模块列表中，并返回模块的指针。
 ******************************************************************************/
/* 定义一个C语言函数，用于注册一个模块到zbx系统中。
 * 参数1：lib，模块所依赖的库的指针。
 * 参数2：name，模块的名称。
 * 返回值：返回一个指向注册成功后的模块结构的指针。
 */
static zbx_module_t *zbx_register_module(void *lib, char *name)
{
	/* 定义一个指向zbx_module_t结构的指针，用于保存注册成功的模块。 */
	zbx_module_t	*module;

	/* 为新创建的模块分配内存空间。 */
	module = (zbx_module_t *)zbx_malloc(NULL, sizeof(zbx_module_t));
	/* 设置模块所依赖的库指针。 */
	module->lib = lib;
	/* 设置模块的名称。 */
	module->name = zbx_strdup(NULL, name);
	/* 将新创建的模块添加到模块列表中。 */
	zbx_vector_ptr_append(&modules, module);
/******************************************************************************
 * *
 *这段代码的主要目的是注册历史数据写入回调函数。函数接收两个参数，一个是模块（zbx_module_t 类型），另一个是历史数据写入回调函数结构体（ZBX_HISTORY_WRITE_CBS 类型）。在函数内部，首先判断历史数据写入回调函数结构体中的各个回调函数（history_float_cb、history_integer_cb、history_string_cb、history_text_cb 和 history_log_cb）是否不为空。
 *
 *如果某个回调函数不为空，就为其分配内存并初始化一个数组（history_float_cbs、history_integer_cbs、history_string_cbs、history_text_cbs 和 history_log_cbs）。然后遍历数组，找到最后一个元素，重新分配数组内存，并添加新元素。最后，将模块指针和相应的回调函数指针存储在数组中。
 *
 *整个代码块的主要目的是为模块注册相应的历史数据写入回调函数。
 ******************************************************************************/
static void zbx_register_history_write_cbs(zbx_module_t *module, ZBX_HISTORY_WRITE_CBS history_write_cbs)
{
    // 判断 history_write_cbs 结构体中的 history_float_cb 是否不为空
    if (NULL != history_write_cbs.history_float_cb)
    {
        int	j = 0;

        // 初始化 history_float_cbs 数组，并为其分配内存
        if (NULL == history_float_cbs)
        {
            history_float_cbs = (zbx_history_float_cb_t *)zbx_malloc(history_float_cbs, sizeof(zbx_history_float_cb_t));
            history_float_cbs[0].module = NULL;
        }

        // 遍历 history_float_cbs 数组，找到最后一个元素
        while (NULL != history_float_cbs[j].module)
            j++;

        // 重新分配 history_float_cbs 数组内存，并添加新元素
        history_float_cbs = (zbx_history_float_cb_t *)zbx_realloc(history_float_cbs, (j + 2) * sizeof(zbx_history_float_cb_t));
        history_float_cbs[j].module = module;
        history_float_cbs[j].history_float_cb = history_write_cbs.history_float_cb;
        history_float_cbs[j + 1].module = NULL;
    }

    // 判断 history_write_cbs 结构体中的 history_integer_cb 是否不为空
    if (NULL != history_write_cbs.history_integer_cb)
    {
        int	j = 0;

        // 初始化 history_integer_cbs 数组，并为其分配内存
        if (NULL == history_integer_cbs)
        {
            history_integer_cbs = (zbx_history_integer_cb_t *)zbx_malloc(history_integer_cbs, sizeof(zbx_history_integer_cb_t));
            history_integer_cbs[0].module = NULL;
        }

        // 遍历 history_integer_cbs 数组，找到最后一个元素
        while (NULL != history_integer_cbs[j].module)
            j++;

        // 重新分配 history_integer_cbs 数组内存，并添加新元素
        history_integer_cbs = (zbx_history_integer_cb_t *)zbx_realloc(history_integer_cbs, (j + 2) * sizeof(zbx_history_integer_cb_t));
        history_integer_cbs[j].module = module;
        history_integer_cbs[j].history_integer_cb = history_write_cbs.history_integer_cb;
        history_integer_cbs[j + 1].module = NULL;
    }

    // 判断 history_write_cbs 结构体中的 history_string_cb 是否不为空
    if (NULL != history_write_cbs.history_string_cb)
    {
        int	j = 0;

        // 初始化 history_string_cbs 数组，并为其分配内存
        if (NULL == history_string_cbs)
        {
            history_string_cbs = (zbx_history_string_cb_t *)zbx_malloc(history_string_cbs, sizeof(zbx_history_string_cb_t));
            history_string_cbs[0].module = NULL;
        }

        // 遍历 history_string_cbs 数组，找到最后一个元素
        while (NULL != history_string_cbs[j].module)
            j++;

        // 重新分配 history_string_cbs 数组内存，并添加新元素
        history_string_cbs = (zbx_history_string_cb_t *)zbx_realloc(history_string_cbs, (j + 2) * sizeof(zbx_history_string_cb_t));
        history_string_cbs[j].module = module;
        history_string_cbs[j].history_string_cb = history_write_cbs.history_string_cb;
        history_string_cbs[j + 1].module = NULL;
    }

    // 判断 history_write_cbs 结构体中的 history_text_cb 是否不为空
    if (NULL != history_write_cbs.history_text_cb)
    {
        int	j = 0;

        // 初始化 history_text_cbs 数组，并为其分配内存
        if (NULL == history_text_cbs)
        {
            history_text_cbs = (zbx_history_text_cb_t *)zbx_malloc(history_text_cbs, sizeof(zbx_history_text_cb_t));
            history_text_cbs[0].module = NULL;
        }

        // 遍历 history_text_cbs 数组，找到最后一个元素
        while (NULL != history_text_cbs[j].module)
            j++;

        // 重新分配 history_text_cbs 数组内存，并添加新元素
        history_text_cbs = (zbx_history_text_cb_t *)zbx_realloc(history_text_cbs, (j + 2) * sizeof(zbx_history_text_cb_t));
        history_text_cbs[j].module = module;
        history_text_cbs[j].history_text_cb = history_write_cbs.history_text_cb;
        history_text_cbs[j + 1].module = NULL;
    }

    // 判断 history_write_cbs 结构体中的 history_log_cb 是否不为空
    if (NULL != history_write_cbs.history_log_cb)
    {
        int	j = 0;

        // 初始化 history_log_cbs 数组，并为其分配内存
        if (NULL == history_log_cbs)
        {
            history_log_cbs = (zbx_history_log_cb_t *)zbx_malloc(history_log_cbs, sizeof(zbx_history_log_cb_t));
            history_log_cbs[0].module = NULL;
        }

        // 遍历 history_log_cbs 数组，找到最后一个元素
        while (NULL != history_log_cbs[j].module)
            j++;

        // 重新分配 history_log_cbs 数组内存，并添加新元素
        history_log_cbs = (zbx_history_log_cb_t *)zbx_realloc(history_log_cbs, (j + 2) * sizeof(zbx_history_log_cb_t));
        history_log_cbs[j].module = module;
        history_log_cbs[j].history_log_cb = history_write_cbs.history_log_cb;
        history_log_cbs[j + 1].module = NULL;
/******************************************************************************
 * *
 *这段代码的主要目的是加载并初始化一个Zabbix模块。函数`zbx_load_module`接受三个参数：模块路径、模块名称和超时时间。如果在加载模块过程中遇到错误，函数将返回FAIL。如果模块加载成功，函数将返回SUCCEED。
 *
 *代码块的分析如下：
 *
 *1. 定义局部变量，包括库指针、全路径名称、错误字符串等。
 *2. 判断模块名称是否包含斜杠，如果没有，则添加路径。
 *3. 使用`dlopen`函数加载模块库。
 *4. 检查模块是否已经加载，如果已加载，则返回SUCCEED。
 *5. 获取模块版本函数并验证版本。
 *6. 获取模块初始化函数并验证它。
 *7. 获取模块项列表函数并注册项。
 *8. 获取模块超时函数并调用它。
 *9. 获取模块历史写回调函数并注册。
 *10. 加载模块成功后，将其注册为zbx模块。
 *11. 如果加载失败，关闭模块库并返回FAIL。
 ******************************************************************************/
/**
 * @file             zbx_load_module.c
 * @brief           Load and initialize a Zabbix module.
 *
 * @author          Alexander Klimov
 * @copyright       Copyright (c) 2006-2017, Zabbix SIA
 * @license         GNU General Public License v2
 *
 * @description     Load a Zabbix module and initialize its functions.
 *                  The function takes a module path, module name and timeout as input parameters.
 *                  If the module is already loaded, the function returns SUCCEED.
 *                  If the module loading fails, the function returns FAIL.
 *
 * @param path       Module path.
 * @param name       Module name.
 * @param timeout    Timeout for module loading.
 * @return          SUCCEED if the module is loaded successfully, FAIL otherwise.
 */
static int zbx_load_module(const char *path, char *name, int timeout)
{
	/* Declare local variables */
	void *lib;
	char full_name[MAX_STRING_LEN], error[MAX_STRING_LEN];
	int (*func_init)(void), (*func_version)(void), version;
	ZBX_METRIC *(*func_list)(void);
	void (*func_timeout)(int);
	ZBX_HISTORY_WRITE_CBS (*func_history_write_cbs)(void);
	zbx_module_t *module, module_tmp;

	/* If the module name doesn't contain a slash, add the path */
	if ('/' != *name)
		zbx_snprintf(full_name, sizeof(full_name), "%s/%s", path, name);
	else
		zbx_snprintf(full_name, sizeof(full_name), "%s", name);

	/* Log the loading process */
	zabbix_log(LOG_LEVEL_DEBUG, "loading module \"%s\"", full_name);

	/* Load the module library */
	if (NULL == (lib = dlopen(full_name, RTLD_NOW)))
	{
		/* Log the error and return FAIL if the module can't be loaded */
		zabbix_log(LOG_LEVEL_CRIT, "cannot load module \"%s\": %s", name, dlerror());
		return FAIL;
	}

	/* Save the library handle for later use */
	module_tmp.lib = lib;

	/* Check if the module is already loaded */
	if (FAIL != zbx_vector_ptr_search(&modules, &module_tmp, zbx_module_compare_func))
	{
		/* Log the debug message and return SUCCEED */
		zabbix_log(LOG_LEVEL_DEBUG, "module \"%s\" has already beed loaded", name);
		return SUCCEED;
	}

	/* Get the module version and verify it */
	if (NULL == (func_version = (int (*)(void))dlsym(lib, ZBX_MODULE_FUNC_API_VERSION)))
	{
		/* Log the error and return FAIL if the module version is not found */
		zabbix_log(LOG_LEVEL_CRIT, "cannot find \"" ZBX_MODULE_FUNC_API_VERSION "()\""
				" function in module \"%s\": %s", name, dlerror());
		goto fail;
	}

	/* Get the module version and verify it */
	version = func_version();
	if (ZBX_MODULE_API_VERSION != version)
	{
		/* Log the error and return FAIL if the module version is unsupported */
		zabbix_log(LOG_LEVEL_CRIT, "unsupported module \"%s\" version: %d", name, version);
		goto fail;
	}

	/* Get the module initialization function and verify it */
	if (NULL == (func_init = (int (*)(void))dlsym(lib, ZBX_MODULE_FUNC_INIT)))
	{
		/* Log the error and return FAIL if the module init function is not found */
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_INIT "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
	{
		/* Call the module init function and log the result */
		if (ZBX_MODULE_OK != func_init())
		{
			/* Log the error and return FAIL if the module init fails */
			zabbix_log(LOG_LEVEL_CRIT, "cannot initialize module \"%s\"", name);
			goto fail;
		}
	}

	/* Get the module item list function and register the items */
	if (NULL == (func_list = (ZBX_METRIC *(*)(void))dlsym(lib, ZBX_MODULE_FUNC_ITEM_LIST)))
	{
		/* Log the error and return FAIL if the module item list function is not found */
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_ITEM_LIST "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
	{
		/* Register the module items and log the result */
		if (SUCCEED != zbx_register_module_items(func_list(), error, sizeof(error)))
		{
			/* Log the error and return FAIL if the module items cannot be registered */
			zabbix_log(LOG_LEVEL_CRIT, "cannot load module \"%s\": %s", name, error);
			goto fail;
		}
	}

	/* Get the module timeout function and call it with the given timeout */
	if (NULL == (func_timeout = (void (*)(int))dlsym(lib, ZBX_MODULE_FUNC_ITEM_TIMEOUT)))
	{
		/* Log the error and return FAIL if the module timeout function is not found */
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_ITEM_TIMEOUT "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
		func_timeout(timeout);

	/* Get the module history write callback function and register it */
	if (NULL == (func_history_write_cbs = (ZBX_HISTORY_WRITE_CBS (*)(void))dlsym(lib,
			ZBX_MODULE_FUNC_HISTORY_WRITE_CBS)))
	{
		/* Log the error and return FAIL if the module history write callback function is not found */
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_HISTORY_WRITE_CBS "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
		zbx_register_history_write_cbs(module, func_history_write_cbs());

	/* The module has been loaded and can now be registered */
	module = zbx_register_module(lib, name);

	/* Log the success message */
	zabbix_log(LOG_LEVEL_DEBUG, "module \"%s\" loaded and registered", name);

	return SUCCEED;
fail:
	/* Close the module library and return FAIL */
	dlclose(lib);

	return FAIL;
}

	}

	if (NULL == (func_version = (int (*)(void))dlsym(lib, ZBX_MODULE_FUNC_API_VERSION)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot find \"" ZBX_MODULE_FUNC_API_VERSION "()\""
				" function in module \"%s\": %s", name, dlerror());
		goto fail;
	}

	if (ZBX_MODULE_API_VERSION != (version = func_version()))
	{
		zabbix_log(LOG_LEVEL_CRIT, "unsupported module \"%s\" version: %d", name, version);
		goto fail;
	}

	if (NULL == (func_init = (int (*)(void))dlsym(lib, ZBX_MODULE_FUNC_INIT)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_INIT "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else if (ZBX_MODULE_OK != func_init())
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize module \"%s\"", name);
		goto fail;
	}

	if (NULL == (func_list = (ZBX_METRIC *(*)(void))dlsym(lib, ZBX_MODULE_FUNC_ITEM_LIST)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_ITEM_LIST "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
	{
		if (SUCCEED != zbx_register_module_items(func_list(), error, sizeof(error)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot load module \"%s\": %s", name, error);
			goto fail;
		}

		if (NULL == (func_timeout = (void (*)(int))dlsym(lib, ZBX_MODULE_FUNC_ITEM_TIMEOUT)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_ITEM_TIMEOUT "()\""
					" function in module \"%s\": %s", name, dlerror());
		}
		else
			func_timeout(timeout);
	}

	/* module passed validation and can now be registered */
	module = zbx_register_module(lib, name);

	if (NULL == (func_history_write_cbs = (ZBX_HISTORY_WRITE_CBS (*)(void))dlsym(lib,
			ZBX_MODULE_FUNC_HISTORY_WRITE_CBS)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find \"" ZBX_MODULE_FUNC_HISTORY_WRITE_CBS "()\""
				" function in module \"%s\": %s", name, dlerror());
	}
	else
		zbx_register_history_write_cbs(module, func_history_write_cbs());

	return SUCCEED;
fail:
	dlclose(lib);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_load_modules                                                 *
 *                                                                            *
 * Purpose: load loadable modules (dynamic libraries)                         *
 *                                                                            *
 * Parameters: path - directory where modules are located                     *
 *             file_names - list of module names                              *
 *             timeout - timeout in seconds for processing of items by module *
 *             verbose - output list of loaded modules                        *
 *                                                                            *
 * Return value: SUCCEED - all modules are successfully loaded                *
 *               FAIL - loading of modules failed                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是从给定的路径中加载多个模块。它首先创建一个模块容器，然后遍历file_names数组，逐个加载模块。如果加载成功，它会打印已加载的模块名称。最后，关闭日志记录并返回加载模块的结果。
 ******************************************************************************/
// 定义一个C函数：zbx_load_modules，该函数的主要目的是从给定的路径中加载模块
// 参数：
//   path：模块文件的路径
//   file_names：一个字符指针数组，其中包含要加载的模块文件名
//   timeout：加载模块的超时时间
//   verbose：是否打印加载成功的模块名称
int zbx_load_modules(const char *path, char **file_names, int timeout, int verbose)
{
	// 定义一些常量和变量
	const char	*__function_name = "zbx_load_modules";
	char		**file_name;
	int		ret = SUCCEED;

	// 开启日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个模块容器
	zbx_vector_ptr_create(&modules);

	// 检查file_names是否为空
	if (NULL == *file_names)
		goto out;

	// 遍历file_names数组，加载每个模块
	for (file_name = file_names; NULL != *file_name; file_name++)
	{
		// 调用zbx_load_module函数加载模块，若加载失败，跳出循环
		if (SUCCEED != (ret = zbx_load_module(path, *file_name, timeout)))
			goto out;
	}

	// 如果verbose参数为1，打印已加载的模块名称
	if (0 != verbose)
	{
		char	*buffer;
		int	i = 0;

		// 至少有一个模块已成功加载
		buffer = zbx_strdcat(NULL, ((zbx_module_t *)modules.values[i++])->name);

		// 拼接其他已加载模块的名称
		while (i < modules.values_num)
		{
			buffer = zbx_strdcat(buffer, ", ");
			buffer = zbx_strdcat(buffer, ((zbx_module_t *)modules.values[i++])->name);
		}

		// 打印加载的模块名称
		zabbix_log(LOG_LEVEL_WARNING, "loaded modules: %s", buffer);
		// 释放buffer内存
		zbx_free(buffer);
	}

out:
	// 关闭日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回加载模块的结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_unload_module                                                *
 *                                                                            *
 * Purpose: unload module and free allocated resources                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是卸载一个已加载的模块。首先，通过dlsym函数查找模块库中的卸载函数，并将结果存储在func_uninit指针中。然后判断是否找到了卸载函数，如果找不到或卸载函数执行失败，记录日志。最后关闭模块库，释放模块名称和模块指针，完成模块卸载。
 ******************************************************************************/
// 定义一个静态函数zbx_unload_module，接收一个void指针作为参数
static void zbx_unload_module(void *data)
{
	// 类型转换，将void指针转换为zbx_module_t类型的指针，方便后续操作
	zbx_module_t *module = (zbx_module_t *)data;

	// 定义一个指向模块卸载函数的指针
	int (*func_uninit)(void);

	// 使用dlsym函数查找模块库中的卸载函数，并将结果存储在func_uninit指针中
	if (NULL == (func_uninit = (int (*)(void))dlsym(module->lib, ZBX_MODULE_FUNC_UNINIT)))
	{
		// 如果没有找到卸载函数，记录日志
		zabbix_log(LOG_LEVEL_DEBUG, "cannot find " ZBX_MODULE_FUNC_UNINIT "()"
				" function in module \"%s\": %s", module->name, dlerror());
	}
	else if (ZBX_MODULE_OK != func_uninit())
		// 如果卸载函数执行失败，记录日志
		zabbix_log(LOG_LEVEL_WARNING, "uninitialization of module \"%s\" failed", module->name);

	// 关闭模块库
	dlclose(module->lib);

	// 释放模块名称内存
	zbx_free(module->name);

	// 释放模块指针，完成模块卸载
	zbx_free(module);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_unload_modules                                               *
 *                                                                            *
 * Purpose: Unload already loaded loadable modules (dynamic libraries).       *
 *          It is called on process shutdown.                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放历史数据回调函数指针所占用的内存空间，并清空 modules 向量。日志记录用于显示函数的执行过程。
 ******************************************************************************/
// 定义一个名为 zbx_unload_modules 的函数，该函数为 void 类型，即无返回值。
void zbx_unload_modules(void)
{
    // 定义一个常量字符串指针 __function_name，用于存储函数名
    const char *__function_name = "zbx_unload_modules";

    // 使用 zabbix_log 函数记录日志，表示进入 zbx_unload_modules 函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 释放 history_float_cbs 内存空间
    zbx_free(history_float_cbs);

    // 释放 history_integer_cbs 内存空间
    zbx_free(history_integer_cbs);

    // 释放 history_string_cbs 内存空间
    zbx_free(history_string_cbs);

    // 释放 history_text_cbs 内存空间
    zbx_free(history_text_cbs);

    // 释放 history_log_cbs 内存空间
    zbx_free(history_log_cbs);

    // 清空 modules 向量中的元素，并销毁该向量
    zbx_vector_ptr_clear_ext(&modules, zbx_unload_module);
    zbx_vector_ptr_destroy(&modules);

    // 使用 zabbix_log 函数记录日志，表示退出 zbx_unload_modules 函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

