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

#include "checks_simple_vmware.h"
#include "checks_simple.h"
#include "simple.h"
#include "log.h"

#include "zbxself.h"

typedef int	(*vmfunc_t)(AGENT_REQUEST *, const char *, const char *, AGENT_RESULT *);

#define ZBX_VMWARE_PREFIX	"vmware."

typedef struct
{
	const char	*key;
	vmfunc_t	func;
}
zbx_vmcheck_t;

#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
#	define VMCHECK_FUNC(func)	func
#else
#	define VMCHECK_FUNC(func)	NULL
#endif

static zbx_vmcheck_t	vmchecks[] =
{
	{"cluster.discovery", VMCHECK_FUNC(check_vcenter_cluster_discovery)},
	{"cluster.status", VMCHECK_FUNC(check_vcenter_cluster_status)},
	{"version", VMCHECK_FUNC(check_vcenter_version)},
	{"fullname", VMCHECK_FUNC(check_vcenter_fullname)},
	{"datastore.discovery", VMCHECK_FUNC(check_vcenter_datastore_discovery)},
	{"datastore.read", VMCHECK_FUNC(check_vcenter_datastore_read)},
	{"datastore.size", VMCHECK_FUNC(check_vcenter_datastore_size)},
	{"datastore.write", VMCHECK_FUNC(check_vcenter_datastore_write)},
	{"datastore.hv.list", VMCHECK_FUNC(check_vcenter_datastore_hv_list)},

	{"hv.cluster.name", VMCHECK_FUNC(check_vcenter_hv_cluster_name)},
	{"hv.cpu.usage", VMCHECK_FUNC(check_vcenter_hv_cpu_usage)},
	{"hv.datacenter.name", VMCHECK_FUNC(check_vcenter_hv_datacenter_name)},
	{"hv.datastore.discovery", VMCHECK_FUNC(check_vcenter_hv_datastore_discovery)},
	{"hv.datastore.read", VMCHECK_FUNC(check_vcenter_hv_datastore_read)},
	{"hv.datastore.size", VMCHECK_FUNC(check_vcenter_hv_datastore_size)},
	{"hv.datastore.write", VMCHECK_FUNC(check_vcenter_hv_datastore_write)},
	{"hv.datastore.list", VMCHECK_FUNC(check_vcenter_hv_datastore_list)},
	{"hv.discovery", VMCHECK_FUNC(check_vcenter_hv_discovery)},
	{"hv.fullname", VMCHECK_FUNC(check_vcenter_hv_fullname)},
	{"hv.hw.cpu.num", VMCHECK_FUNC(check_vcenter_hv_hw_cpu_num)},
	{"hv.hw.cpu.freq", VMCHECK_FUNC(check_vcenter_hv_hw_cpu_freq)},
	{"hv.hw.cpu.model", VMCHECK_FUNC(check_vcenter_hv_hw_cpu_model)},
	{"hv.hw.cpu.threads", VMCHECK_FUNC(check_vcenter_hv_hw_cpu_threads)},
	{"hv.hw.memory", VMCHECK_FUNC(check_vcenter_hv_hw_memory)},
	{"hv.hw.model", VMCHECK_FUNC(check_vcenter_hv_hw_model)},
	{"hv.hw.uuid", VMCHECK_FUNC(check_vcenter_hv_hw_uuid)},
	{"hv.hw.vendor", VMCHECK_FUNC(check_vcenter_hv_hw_vendor)},
	{"hv.memory.size.ballooned", VMCHECK_FUNC(check_vcenter_hv_memory_size_ballooned)},
	{"hv.memory.used", VMCHECK_FUNC(check_vcenter_hv_memory_used)},
	{"hv.network.in", VMCHECK_FUNC(check_vcenter_hv_network_in)},
	{"hv.network.out", VMCHECK_FUNC(check_vcenter_hv_network_out)},
	{"hv.perfcounter", VMCHECK_FUNC(check_vcenter_hv_perfcounter)},
	{"hv.sensor.health.state", VMCHECK_FUNC(check_vcenter_hv_sensor_health_state)},
	{"hv.status", VMCHECK_FUNC(check_vcenter_hv_status)},
	{"hv.uptime", VMCHECK_FUNC(check_vcenter_hv_uptime)},
	{"hv.version", VMCHECK_FUNC(check_vcenter_hv_version)},
	{"hv.vm.num", VMCHECK_FUNC(check_vcenter_hv_vm_num)},

	{"vm.cluster.name", VMCHECK_FUNC(check_vcenter_vm_cluster_name)},
	{"vm.cpu.num", VMCHECK_FUNC(check_vcenter_vm_cpu_num)},
	{"vm.cpu.ready", VMCHECK_FUNC(check_vcenter_vm_cpu_ready)},
	{"vm.cpu.usage", VMCHECK_FUNC(check_vcenter_vm_cpu_usage)},
	{"vm.datacenter.name", VMCHECK_FUNC(check_vcenter_vm_datacenter_name)},
	{"vm.discovery", VMCHECK_FUNC(check_vcenter_vm_discovery)},
	{"vm.hv.name", VMCHECK_FUNC(check_vcenter_vm_hv_name)},
	{"vm.memory.size", VMCHECK_FUNC(check_vcenter_vm_memory_size)},
	{"vm.memory.size.ballooned", VMCHECK_FUNC(check_vcenter_vm_memory_size_ballooned)},
	{"vm.memory.size.compressed", VMCHECK_FUNC(check_vcenter_vm_memory_size_compressed)},
	{"vm.memory.size.swapped", VMCHECK_FUNC(check_vcenter_vm_memory_size_swapped)},
	{"vm.memory.size.usage.guest", VMCHECK_FUNC(check_vcenter_vm_memory_size_usage_guest)},
	{"vm.memory.size.usage.host", VMCHECK_FUNC(check_vcenter_vm_memory_size_usage_host)},
	{"vm.memory.size.private", VMCHECK_FUNC(check_vcenter_vm_memory_size_private)},
	{"vm.memory.size.shared", VMCHECK_FUNC(check_vcenter_vm_memory_size_shared)},
	{"vm.net.if.discovery", VMCHECK_FUNC(check_vcenter_vm_net_if_discovery)},
	{"vm.net.if.in", VMCHECK_FUNC(check_vcenter_vm_net_if_in)},
	{"vm.net.if.out", VMCHECK_FUNC(check_vcenter_vm_net_if_out)},
	{"vm.perfcounter", VMCHECK_FUNC(check_vcenter_vm_perfcounter)},
	{"vm.powerstate", VMCHECK_FUNC(check_vcenter_vm_powerstate)},
	{"vm.storage.committed", VMCHECK_FUNC(check_vcenter_vm_storage_committed)},
	{"vm.storage.unshared", VMCHECK_FUNC(check_vcenter_vm_storage_unshared)},
	{"vm.storage.uncommitted", VMCHECK_FUNC(check_vcenter_vm_storage_uncommitted)},
	{"vm.uptime", VMCHECK_FUNC(check_vcenter_vm_uptime)},
	{"vm.vfs.dev.discovery", VMCHECK_FUNC(check_vcenter_vm_vfs_dev_discovery)},
	{"vm.vfs.dev.read", VMCHECK_FUNC(check_vcenter_vm_vfs_dev_read)},
	{"vm.vfs.dev.write", VMCHECK_FUNC(check_vcenter_vm_vfs_dev_write)},
	{"vm.vfs.fs.discovery", VMCHECK_FUNC(check_vcenter_vm_vfs_fs_discovery)},
	{"vm.vfs.fs.size", VMCHECK_FUNC(check_vcenter_vm_vfs_fs_size)},

	{NULL, NULL}
};

/******************************************************************************
 *                                                                            *
 * Function: get_vmware_function                                              *
 *                                                                            *
 * Purpose: Retrieves a handler of the item key                               *
 *                                                                            *
 * Parameters: key    - [IN] an item key (without parameters)                 *
 *             vmfunc - [OUT] a handler of the item key; can be NULL if       *
 *                            libxml2 or libcurl is not compiled in           *
 *                                                                            *
 * Return value: SUCCEED if key is a valid VMware key, FAIL - otherwise       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数`get_vmware_function`，接收两个参数，一个是字符串指针`key`，另一个是指向`vmfunc_t`类型的指针`vmfunc`。该函数用于根据`key`字符串查找对应的VMware函数，并将找到的函数值存储到`vmfunc`指针指向的内存地址。如果找不到匹配的`key`，则返回FAIL。如果找到匹配的`key`，则返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数，用于获取VMware函数
static int	get_vmware_function(const char *key, vmfunc_t *vmfunc)
{
	// 定义一个指向zbx_vmcheck_t类型的指针，用于存储vmchecks数组的首元素
	zbx_vmcheck_t	*check;

	// 判断key字符串是否以ZBX_VMWARE_PREFIX开头，如果不开头，返回FAIL
	if (0 != strncmp(key, ZBX_VMWARE_PREFIX, ZBX_CONST_STRLEN(ZBX_VMWARE_PREFIX)))
		return FAIL;

	// 遍历vmchecks数组，查找与key字符串相等的元素
	for (check = vmchecks; NULL != check->key; check++)
	{
		if (0 == strcmp(key + ZBX_CONST_STRLEN(ZBX_VMWARE_PREFIX), check->key))
		{
			*vmfunc = check->func;
			return SUCCEED;
		}
	}

	return FAIL;
}

int	get_value_simple(DC_ITEM *item, AGENT_RESULT *result, zbx_vector_ptr_t *add_results)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "get_value_simple";

	// 初始化一个AGENT_REQUEST结构体
	AGENT_REQUEST request;
	vmfunc_t vmfunc;
	int ret = NOTSUPPORTED;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key_orig:'%s' addr:'%s'",
			__function_name, item->key_orig, item->interface.addr);

	// 初始化请求对象
	init_request(&request);

	// 解析物品键，如果失败则返回错误信息
	if (SUCCEED != parse_item_key(item->key, &request))
	{
		// 设置结果消息，表示物品键格式错误
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
		// 跳转到out标签，结束函数执行
		goto out;
	}

	// 设置请求的lastlogsize属性
	request.lastlogsize = item->lastlogsize;

	// 判断请求键是否为net.tcp.service或net.udp.service，如果是，则检查服务状态
	if (0 == strcmp(request.key, "net.tcp.service") || 0 == strcmp(request.key, "net.udp.service"))
	{
		// 检查服务状态，如果成功则设置ret为SUCCEED
		if (SYSINFO_RET_OK == check_service(&request, item->interface.addr, result, 0))
			ret = SUCCEED;
	}
	else if (0 == strcmp(request.key, "net.tcp.service.perf") || 0 == strcmp(request.key, "net.udp.service.perf"))
	{
		// 判断请求键是否为net.tcp.service.perf或net.udp.service.perf，如果是，则检查性能数据
		if (SYSINFO_RET_OK == check_service(&request, item->interface.addr, result, 1))
			ret = SUCCEED;
	}
	else if (SUCCEED == get_vmware_function(request.key, &vmfunc))
	{
		// 如果找到了vmware函数，且不是空指针
		if (NULL != vmfunc)
		{
			// 判断是否有"vmware collector"进程运行，如果没有则返回错误信息
			if (0 == get_process_type_forks(ZBX_PROCESS_TYPE_VMWARE))
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "No \"vmware collector\" processes started."));
				goto out;
			}

			// 调用vmware函数，如果成功则设置ret为SUCCEED
			if (SYSINFO_RET_OK == vmfunc(&request, item->username, item->password, result))
				ret = SUCCEED;
		}
		else
			// 如果没有支持vmware checks，则返回错误信息
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for VMware checks was not compiled in."));
	}
	else if (0 == strcmp(request.key, ZBX_VMWARE_PREFIX "eventlog"))
	{
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
		// 检查是否支持vmware事件日志检查，如果支持则调用相应函数，否则返回错误信息
		if (SYSINFO_RET_OK == check_vcenter_eventlog(&request, item, result, add_results))
			ret = SUCCEED;
#else
		ZBX_UNUSED(add_results);
		// 如果没有支持vmware事件日志检查，则返回错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for VMware checks was not compiled in."));
#endif
	}
	else
	{
		// 如果请求键不是以上几种情况，则执行加载模块中的物品
		if (SUCCEED == process(item->key, PROCESS_MODULE_COMMAND, result))
			ret = SUCCEED;
	}

	if (NOTSUPPORTED == ret && !ISSET_MSG(result))
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Simple check is not supported."));

out:
	free_request(&request);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
