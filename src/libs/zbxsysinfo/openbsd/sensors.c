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
#include "zbxregexp.h"
#include "log.h"

#include <sys/sensors.h>

#ifdef HAVE_SENSORDEV

/******************************************************************************
 * *
 *整个代码块的主要目的是处理传感器数据，对不同类型的传感器进行单位转换，并根据任务类型对数据进行累计、求平均、求最大值和最小值操作。函数接收四个参数：
 *
 *1. `do_task`：任务类型，表示对数据如何处理。可能的值有：
 *   - ZBX_DO_ONE：将当前值作为单个数据点存储。
 *   - ZBX_DO_AVG：计算数据的平均值。
 *   - ZBX_DO_MAX：求数据中的最大值。
 *   - ZBX_DO_MIN：求数据中的最小值。
 *
 *2. `sensor`：一个指向传感器结构体的指针，结构体包含传感器的类型和值。
 *3. `aggr`：一个双精度浮点数的指针，用于存储累计值、平均值、最大值或最小值。
 *4. `cnt`：一个整数的指针，用于存储当前处理的数据点数量。
 ******************************************************************************/
// 定义一个计数传感器处理的函数
static void count_sensor(int do_task, const struct sensor *sensor, double *aggr, int *cnt)
{
    // 获取传感器值
    double value = sensor->value;

    // 根据传感器类型进行转换
    switch (sensor->type)
    {
        case SENSOR_TEMP:
            value = (value - 273150000) / 1000000;
            break;
        case SENSOR_VOLTS_DC:
        case SENSOR_VOLTS_AC:
        case SENSOR_AMPS:
        case SENSOR_LUX:
            value /= 1000000;
            break;
        case SENSOR_TIMEDELTA:
            value /= 1000000000;
            break;
        default:
            break;
    }

	(*cnt)++;

	switch (do_task)
	{
		case ZBX_DO_ONE:
			*aggr = value;
			break;
		case ZBX_DO_AVG:
			*aggr += value;
			break;
		case ZBX_DO_MAX:
			*aggr = (1 == *cnt ? value : MAX(*aggr, value));
			break;
		case ZBX_DO_MIN:
			*aggr = (1 == *cnt ? value : MIN(*aggr, value));
			break;
	}
}

static int get_device_sensors(int do_task, int *mib, const struct sensordev *sensordev, const char *name, double *aggr, int *cnt)
{
	// 判断 do_task 的值，如果是 ZBX_DO_ONE，则执行以下代码；否则，执行else块中的代码
	if (ZBX_DO_ONE == do_task)
	{
		// 定义一个整型变量 i，用于循环遍历传感器类型
		int i, len = 0;
		// 定义一个名为 sensor 的传感器结构体变量
		struct sensor sensor;
		// 定义一个整型变量 slen，用于存储 sensor 结构体的大小
		size_t slen = sizeof(sensor);

		// 遍历传感器类型，直到找到匹配的传感器名称
		for (i = 0; i < SENSOR_MAX_TYPES; i++)
		{
			// 判断传感器名称是否与当前传感器类型匹配
			if (0 == strncmp(name, sensor_type_s[i], len = strlen(sensor_type_s[i])))
				break;
		}

		// 如果遍历结束还未找到匹配的传感器类型，返回 FAIL
		if (i == SENSOR_MAX_TYPES)
			return FAIL;

		// 判断 name 字符串后缀是否为有效整数，如果不是，返回 FAIL
		if (SUCCEED != is_uint31(name + len, &mib[4]))
			return FAIL;

		// 保存当前传感器类型索引到 mib 数组中
		mib[3] = i;

		// 调用 sysctl 函数获取传感器信息，若失败，返回 FAIL
		if (-1 == sysctl(mib, 5, &sensor, &slen, NULL, 0))
			return FAIL;

		// 调用 count_sensor 函数处理传感器数据，并将结果存储在 aggr 和 cnt 指针所指向的内存空间中
		count_sensor(do_task, &sensor, aggr, cnt);
	}
	else
	{
		// 定义两个整型变量 i 和 j，用于循环遍历传感器类型和子类型
		int i, j;

		// 遍历传感器类型，直到找到匹配的传感器名称
		for (i = 0; i < SENSOR_MAX_TYPES; i++)
		{
			for (j = 0; j < sensordev->maxnumt[i]; j++)
			{
				char		human[64];
				struct sensor	sensor;
				size_t		slen = sizeof(sensor);

				zbx_snprintf(human, sizeof(human), "%s%d", sensor_type_s[i], j);

				if (NULL == zbx_regexp_match(human, name, NULL))
					continue;

				mib[3] = i;
				mib[4] = j;

				if (-1 == sysctl(mib, 5, &sensor, &slen, NULL, 0))
					return FAIL;

				count_sensor(do_task, &sensor, aggr, cnt);
			}
		}
	}

	return SUCCEED;
}

int	GET_SENSOR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一些字符串指针和整型变量
    char *device, *name, *function;
    int do_task, mib[5], dev, cnt = 0;
    double aggr = 0;

    // 检查传入的请求参数数量，如果超过3个，则返回错误
    if (3 < request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        // 返回失败
        return SYSINFO_RET_FAIL;
    }

    // 获取请求参数中的设备名称
    device = get_rparam(request, 0);
    // 获取请求参数中的名称
    name = get_rparam(request, 1);
    // 获取请求参数中的函数
    function = get_rparam(request, 2);

    // 检查设备名称是否合法，如果不合法，则返回错误
    if (NULL == device || '\0' == *device)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        // 返回失败
        return SYSINFO_RET_FAIL;
    }

    // 检查名称是否合法，如果不合法，则返回错误
    if (NULL == name || '\0' == *name)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 返回失败
        return SYSINFO_RET_FAIL;
    }

    // 检查函数是否合法，如果不合法，则返回错误
    if (NULL == function || '\0' == *function)
        do_task = ZBX_DO_ONE;
    else if (0 == strcmp(function, "avg"))
        do_task = ZBX_DO_AVG;
    else if (0 == strcmp(function, "max"))
        do_task = ZBX_DO_MAX;
    else if (0 == strcmp(function, "min"))
        do_task = ZBX_DO_MIN;
    else

	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;

	for (dev = 0;; dev++)
	{
		struct sensordev	sensordev;
		size_t			sdlen = sizeof(sensordev);

		mib[2] = dev;

		if (-1 == sysctl(mib, 3, &sensordev, &sdlen, NULL, 0))
		{
			if (errno == ENXIO)
				continue;
			if (errno == ENOENT)
				break;

			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s",
					zbx_strerror(errno)));
			return SYSINFO_RET_FAIL;
		}

		if ((ZBX_DO_ONE == do_task && 0 == strcmp(sensordev.xname, device)) ||
				(ZBX_DO_ONE != do_task && NULL != zbx_regexp_match(sensordev.xname, device, NULL)))
		{
			if (SUCCEED != get_device_sensors(do_task, mib, &sensordev, name, &aggr, &cnt))
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain sensor information."));
				return SYSINFO_RET_FAIL;
			}
		}
	}

	if (0 == cnt)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain sensor information."));
		return SYSINFO_RET_FAIL;
	}

	if (ZBX_DO_AVG == do_task)
		SET_DBL_RESULT(result, aggr / cnt);
	else
		SET_DBL_RESULT(result, aggr);

	return SYSINFO_RET_OK;
}

#else
/******************************************************************************
 * *
 *整个代码块的主要目的是：检查代理程序是否支持 \"sensordev\" 结构。如果代理程序未编译支持该结构，则设置结果消息并返回失败码。
 ******************************************************************************/
// 定义一个函数，名为 GET_SENSOR，接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int GET_SENSOR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 设置结果消息，使用 zbx_strdup 函数复制一个字符串，表示代理程序未编译支持 "sensordev" 结构。
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"sensordev\" structure."));
    // 返回系统信息失败码，表示代理程序未能正确处理传感器数据。
    return SYSINFO_RET_FAIL;
}


#endif	/* HAVE_SENSORDEV */
