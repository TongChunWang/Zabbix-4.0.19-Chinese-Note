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
#include "log.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统的启动时间，并通过AGENT_REQUEST和AGENT_RESULT结构体将启动时间返回给调用者。代码分为两种情况来获取启动时间：一种是使用内核统计（ZONE）方式，另一种是使用UTMPX方式。在获取启动时间的过程中，如果遇到错误，将会设置相应的错误信息并返回。
 ******************************************************************************/
// 定义一个函数，获取系统启动时间，输入参数为一个AGENT_REQUEST结构体的指针，输出为一个AGENT_RESULT结构体的指针
int SYSTEM_BOOTTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;

#ifdef HAVE_ZONE_H
	// 判断是否支持zone，如果不支持，则使用UTMPX方式获取启动时间
	if (GLOBAL_ZONEID == getzoneid())
	{
#endif
		kstat_ctl_t	*kc;
		kstat_t		*kp;
		kstat_named_t	*kn;

		if (NULL == (kc = kstat_open()))
		{
			// 设置错误信息
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s",
					zbx_strerror(errno)));
			return ret;
		}

		// 查找内核统计中的system_misc项
		if (NULL == (kp = kstat_lookup(kc, "unix", 0, "system_misc")))
		{
			// 设置错误信息
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot look up in kernel statistics facility: %s",
					zbx_strerror(errno)));
			goto clean;
		}

		// 读取内核统计中的启动时间
		if (-1 == kstat_read(kc, kp, 0))
		{
			// 设置错误信息
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s",
					zbx_strerror(errno)));
			goto clean;
		}

		if (NULL == (kn = (kstat_named_t *)kstat_data_lookup(kp, "boot_time")))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot look up data in kernel statistics facility:"
					" %s", zbx_strerror(errno)));
			goto clean;
		}

		SET_UI64_RESULT(result, get_kstat_numeric_value(kn));
		ret = SYSINFO_RET_OK;
clean:
		kstat_close(kc);
#ifdef HAVE_ZONE_H
	}
	else
	{
		struct utmpx	utmpx_local, *utmpx;
		// 初始化utmpx_local结构体
		utmpx_local.ut_type = BOOT_TIME;

		// 打开utmpx文件
		setutxent();

		// 获取utmpx中的启动时间
		if (NULL != (utmpx = getutxid(&utmpx_local)))
		{
			// 设置结果
			SET_UI64_RESULT(result, utmpx->ut_xtime);
			ret = SYSINFO_RET_OK;
		}
		else
			// 设置错误信息
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain system boot time."));

		// 关闭utmpx文件
		endutxent();
	}
#endif

	return ret;
}

