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

#ifdef KERNEL_2_4
#define DEVICE_DIR	"/proc/sys/dev/sensors"
#else
#define DEVICE_DIR	"/sys/class/hwmon"
static const char	*locations[] = {"", "/device", NULL};
#endif

#define ATTR_MAX	128

/******************************************************************************
 * *
 *整个代码块的主要目的是读取传感器数据文件，根据不同的任务类型（ZBX_DO_ONE、ZBX_DO_AVG、ZBX_DO_MAX、ZBX_DO_MIN）对数据进行处理，并将处理后的结果存储在对应的累计值（aggr）中。同时，还对计数器（cnt）进行递增操作，以记录当前处理的数据行数。
 ******************************************************************************/
static void count_sensor(int do_task, const char *filename, double *aggr, int *cnt)
{
    // 定义文件指针，用于读取文件
    FILE *f;
    // 定义一个字符数组，用于存储文件中的一行数据
    char line[MAX_STRING_LEN];
    // 定义一个双精度浮点型变量，用于存储传感器数据
    double value;

    // 尝试以只读模式打开文件
    if (NULL == (f = fopen(filename, "r")))
        // 如果打开文件失败，直接返回
        return;

    // 尝试从文件中读取一行数据
    if (NULL == fgets(line, sizeof(line), f))
    {
        // 如果读取数据失败，关闭文件并返回
        zbx_fclose(f);
        return;
    }

    // 关闭文件
    zbx_fclose(f);

    // 判断是否为内核2.4版本，如果是，使用以下格式读取传感器数据
#ifdef KERNEL_2_4
	if (1 == sscanf(line, "%*f\t%*f\t%lf\n", &value))
	{
#else
    if (1 == sscanf(line, "%lf", &value))
    {
        // 如果文件名中不包含"fan"字符串，将读取到的数值除以1000
        if (NULL == strstr(filename, "fan"))
            value = value / 1000;
#endif
        // 计数器加1
        (*cnt)++;

        // 根据任务类型（ZBX_DO_ONE、ZBX_DO_AVG、ZBX_DO_MAX、ZBX_DO_MIN）执行相应操作
        switch (do_task)
        {
            case ZBX_DO_ONE:
                // 任务类型为ZBX_DO_ONE时，直接将读取到的值作为结果
                *aggr = value;
                break;
            case ZBX_DO_AVG:
                // 任务类型为ZBX_DO_AVG时，将读取到的值累加到累计值中
                *aggr += value;
                break;
            case ZBX_DO_MAX:
                // 任务类型为ZBX_DO_MAX时，将读取到的值作为最大值
                *aggr = (1 == *cnt ? value : MAX(*aggr, value));
                break;
			case ZBX_DO_MIN:
				*aggr = (1 == *cnt ? value : MIN(*aggr, value));
				break;
		}
	}
}
#ifndef KERNEL_2_4
/*********************************************************************************
 *                                                                               *
 * Function: sysfs_read_attr                                                     *
 *                                                                               *
 * Purpose: locate and read the name attribute of a sensor from sysfs            *
 *                                                                               *
 * Parameters:  device        - [IN] the path to sensor data in sysfs            *
 *              attribute     - [OUT] the sensor name                            *
 *                                                                               *
 * Return value: Subfolder where the sensor name file was found or NULL          *
 *                                                                               *
 * Comments: attribute string must be freed by caller after it's been used.      *
 *                                                                               *
 *********************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从指定的设备（device）的 sysfs 目录下读取属性（attribute）值。输出结果为一个字符串（字符串数组的一个元素），存储在 attribute 指针所指向的内存空间中。如果找不到匹配的设备或属性，返回 NULL。
 ******************************************************************************/
// 定义一个静态常量指针，指向一个字符串
static const char *sysfs_read_attr(const char *device, char **attribute)
{
	// 定义一个指向字符串数组的指针，用于遍历
	const char	**location;
	// 定义一个字符串，用于存储文件路径
	char		path[MAX_STRING_LEN], buf[ATTR_MAX], *p;
	// 定义一个指向文件的指针
	FILE		*f;

	// 使用一个循环遍历 locations 数组
	for (location = locations; NULL != *location; location++)
	{
		// 拼接设备名和 location，形成文件路径
		zbx_snprintf(path, MAX_STRING_LEN, "%s%s/name", device, *location);

		// 打开文件，若成功则继续执行
		if (NULL != (f = fopen(path, "r")))
		{
			// 从文件中读取一行内容，存储在 buf 缓冲区
			p = fgets(buf, ATTR_MAX, f);
			// 关闭文件
			zbx_fclose(f);

			if (NULL == p)
				break;

			/* Last byte is a '\n'; chop that off */
			buf[strlen(buf) - 1] = '\0';

			if (NULL != attribute)
				*attribute = zbx_strdup(*attribute, buf);

			return *location;
		}
	}

	return NULL;
}
/* 定义一个C语言函数，用于获取设备信息 */
static int get_device_info(const char *dev_path, const char *dev_name, char *device_info, const char **name_subfolder)
{
	/* 初始化返回值和一些变量 */
	int ret = FAIL;
	unsigned int addr;
	ssize_t sub_len;
	char *subsys, *prefix = NULL, linkpath[MAX_STRING_LEN], subsys_path[MAX_STRING_LEN];

	/* 忽略没有name属性的设备 */
	if (NULL == (*name_subfolder = sysfs_read_attr(dev_path, &prefix)))
		goto out;

	/* 如果dev_name为空，表示虚拟设备 */
	if (NULL == dev_name)
	{
		/* 假设虚拟设备是唯一的 */
		zbx_snprintf(device_info, MAX_STRING_LEN, "%s-virtual-0", prefix);
		ret = SUCCEED;

		goto out;
	}

	/* 查找总线类型 */
	zbx_snprintf(linkpath, MAX_STRING_LEN, "%s/device/subsystem", dev_path);

	sub_len = readlink(linkpath, subsys_path, MAX_STRING_LEN - 1);

	/* 如果没有找到subsystem symlink，尝试找bus symlink */
	if (0 > sub_len && ENOENT == errno)
	{
		/* 回退到"bus"链接，适用于kernels <= 2.6.17 */
		zbx_snprintf(linkpath, MAX_STRING_LEN, "%s/device/bus", dev_path);
		sub_len = readlink(linkpath, subsys_path, MAX_STRING_LEN - 1);
	}

	/* 如果没有找到bus symlink */
	if (0 > sub_len)
	{
		/* 对于较旧的kernels（<= 2.6.11），没有subsystem或bus symlink */
		if (errno == ENOENT)
			subsys = NULL;
		else
			goto out;
	}
	else
	{
		subsys_path[sub_len] = '\0';
		subsys = strrchr(subsys_path, '/') + 1;
	}

	/* 根据subsys判断设备类型并进行相应的处理 */
	if ((NULL == subsys || 0 == strcmp(subsys, "i2c")))
	{
		short int bus_i2c;

		if (2 != sscanf(dev_name, "%hd-%x", &bus_i2c, &addr))
			goto out;

		/* 判断是否为legacy ISA */
		if (9191 == bus_i2c)
		{
			zbx_snprintf(device_info, MAX_STRING_LEN, "%s-isa-%04x", prefix, addr);
		}
		else
		{
			const char *bus_subfolder;
			char *bus_attr = NULL, bus_path[MAX_STRING_LEN];

			zbx_snprintf(bus_path, sizeof(bus_path), "/sys/class/i2c-adapter/i2c-%d", bus_i2c);
			bus_subfolder = sysfs_read_attr(bus_path, &bus_attr);

			if (NULL != bus_subfolder && '\0' != *bus_subfolder)
			{
				if (0 != strncmp(bus_attr, "ISA ", 4))
				{
					zbx_free(bus_attr);
					goto out;
				}

				zbx_snprintf(device_info, MAX_STRING_LEN, "%s-isa-%04x", prefix, addr);
			}
			else
				zbx_snprintf(device_info, MAX_STRING_LEN, "%s-i2c-%hd-%02x", prefix, bus_i2c, addr);

			zbx_free(bus_attr);
		}

		ret = SUCCEED;
	}
	/* 处理其他设备类型，如spi、pci、platform、acpi、hid等 */
	else if (0 == strcmp(subsys, "spi"))
	{
		int address;
		short int bus_spi;

		/* SPI */
		if (2 != sscanf(dev_name, "spi%hd.%d", &bus_spi, &address))
			goto out;

		zbx_snprintf(device_info, MAX_STRING_LEN, "%s-spi-%hd-%x", prefix, bus_spi, (unsigned int)address);

		ret = SUCCEED;
	}
	/* 处理其他设备类型，如pci、platform、acpi、hid等 */
	else if (0 == strcmp(subsys, "pci"))
	{
		unsigned int domain, bus, slot, fn;

		/* PCI */
		if (4 != sscanf(dev_name, "%x:%x:%x.%x", &domain, &bus, &slot, &fn))
			goto out;

		addr = (domain << 16) + (bus << 8) + (slot << 3) + fn;
		zbx_snprintf(device_info, MAX_STRING_LEN, "%s-pci-%04x", prefix, addr);

		ret = SUCCEED;
	}
	/* 处理其他设备类型，如platform、acpi、hid等 */
	else if (0 == strcmp(subsys, "platform") || 0 == strcmp(subsys, "of_platform"))
	{
		int address;

		/* 必须是新的ISA（平台驱动） */
		if (1 != sscanf(dev_name, "%*[a-z0-9_].%d", &address))
			address = 0;

		zbx_snprintf(device_info, MAX_STRING_LEN, "%s-isa-%04x", prefix, (unsigned int)address);

		ret = SUCCEED;
	}
	/* 处理其他设备类型，如acpi、hid等 */
	else if (0 == strcmp(subsys, "acpi"))
	{
		/* 假设acpi设备是唯一的 */
		zbx_snprintf(device_info, MAX_STRING_LEN, "%s-acpi-0", prefix);

		ret = SUCCEED;
	}
	/* 处理其他设备类型，如hid等 */
	else if (0 == strcmp(subsys, "hid"))
	{
		unsigned int bus, vendor, product;

		/* 假设hid设备是唯一的 */
		if (4 != sscanf(dev_name, "%x:%x:%x.%x", &bus, &vendor, &product, &addr))
			goto out;

		zbx_snprintf(device_info, MAX_STRING_LEN, "%s-hid-%hd-%x", prefix, (short int)bus, addr);

		ret = SUCCEED;
	}
out:
	zbx_free(prefix);

	return ret;
}
#endif

static void	get_device_sensors(int do_task, const char *device, const char *name, double *aggr, int *cnt)
{
	// 定义一个字符串缓冲区，用于存储传感器名称
	char sensorname[MAX_STRING_LEN];

#ifdef KERNEL_2_4
	// 如果任务类型为 ZBX_DO_ONE
	if (ZBX_DO_ONE == do_task)
	{
		// 拼接设备、设备和传感器名称，形成传感器文件路径
		zbx_snprintf(sensorname, sizeof(sensorname), "%s/%s/%s", DEVICE_DIR, device, name);
		// 统计传感器值
		count_sensor(do_task, sensorname, aggr, cnt);
	}
	else
	{
		DIR		*devicedir = NULL, *sensordir = NULL;
		struct dirent	*deviceent, *sensorent;
		char		devicename[MAX_STRING_LEN];

		if (NULL == (devicedir = opendir(DEVICE_DIR)))
			return;

		while (NULL != (deviceent = readdir(devicedir)))
		{
			// 跳过 "." 和 ".."
			if (0 == strcmp(deviceent->d_name, ".") || 0 == strcmp(deviceent->d_name, ".."))
				continue;

			// 判断设备名称是否匹配
			if (NULL == zbx_regexp_match(deviceent->d_name, device, NULL))
				continue;

			zbx_snprintf(devicename, sizeof(devicename), "%s/%s", DEVICE_DIR, deviceent->d_name);

			if (NULL == (sensordir = opendir(devicename)))
				continue;

			while (NULL != (sensorent = readdir(sensordir)))
			{
				// 跳过 "." 和 ".."
				if (0 == strcmp(sensorent->d_name, ".") || 0 == strcmp(sensorent->d_name, ".."))
					continue;

				// 判断传感器名称是否匹配
				if (NULL == zbx_regexp_match(sensorent->d_name, name, NULL))
					continue;

				// 拼接传感器文件路径，并统计传感器值
				zbx_snprintf(sensorname, sizeof(sensorname), "%s/%s", devicename, sensorent->d_name);
				count_sensor(do_task, sensorname, aggr, cnt);
			}
			closedir(sensordir);
		}
		closedir(devicedir);
	}
#else
	// 打开传感器目录
	DIR		*sensordir = NULL, *devicedir = NULL;
	struct dirent	*sensorent, *deviceent;
	char		hwmon_dir[MAX_STRING_LEN], devicepath[MAX_STRING_LEN], deviced[MAX_STRING_LEN],
			device_info[MAX_STRING_LEN], regex[MAX_STRING_LEN], *device_p;
	const char	*subfolder;
	int		err;

	zbx_snprintf(hwmon_dir, sizeof(hwmon_dir), "%s", DEVICE_DIR);

	if (NULL == (devicedir = opendir(hwmon_dir)))
		return;
	while (NULL != (deviceent = readdir(devicedir)))
	{
		ssize_t	dev_len;
		// 跳过 "." 和 ".."
		if (0 == strcmp(deviceent->d_name, ".") || 0 == strcmp(deviceent->d_name, ".."))
			continue;

		// 获取设备路径
		zbx_snprintf(devicepath, sizeof(devicepath), "%s/%s/device", DEVICE_DIR, deviceent->d_name);
		dev_len = readlink(devicepath, deviced, MAX_STRING_LEN - 1);
		zbx_snprintf(devicepath, sizeof(devicepath), "%s/%s", DEVICE_DIR, deviceent->d_name);

		if (0 > dev_len)
		{
			/* No device link? Treat device as virtual */
			err = get_device_info(devicepath, NULL, device_info, &subfolder);
		}
		else
		{
			deviced[dev_len] = '\0';
			device_p = strrchr(deviced, '/') + 1;

			if (0 == strcmp(device, device_p))
			{
				zbx_snprintf(device_info, sizeof(device_info), "%s", device);
				err = (NULL != (subfolder = sysfs_read_attr(devicepath, NULL)) ? SUCCEED : FAIL);
			}
			else
				err = get_device_info(devicepath, device_p, device_info, &subfolder);
		}

		if (SUCCEED == err && 0 == strcmp(device_info, device))
		{
			zbx_snprintf(devicepath, sizeof(devicepath), "%s/%s%s", DEVICE_DIR, deviceent->d_name,
					subfolder);
		if (ZBX_DO_ONE == do_task)
		{
			// 拼接传感器文件路径，并统计传感器值
			zbx_snprintf(sensorname, sizeof(sensorname), "%s/%s_input", devicepath, name);
			count_sensor(do_task, sensorname, aggr, cnt);
		}
		else
		{
			// 定义一个正则表达式，用于匹配传感器名称
				zbx_snprintf(regex, sizeof(regex), "%s[0-9]*_input", name);

			// 打开传感器目录
			if (NULL == (sensordir = opendir(devicepath)))
				goto out;

			while (NULL != (sensorent = readdir(sensordir)))
			{
				// 跳过 "." 和 ".."
					if (0 == strcmp(sensorent->d_name, ".") ||
							0 == strcmp(sensorent->d_name, ".."))
					continue;

				// 判断传感器名称是否匹配
				if (NULL == zbx_regexp_match(sensorent->d_name, regex, NULL))
					continue;

				// 拼接传感器文件路径，并统计传感器值
				zbx_snprintf(sensorname, sizeof(sensorname), "%s/%s", devicepath,
						sensorent->d_name);
				count_sensor(do_task, sensorname, aggr, cnt);
			}
			closedir(sensordir);
			}
		}
	}
out:
	closedir(devicedir);
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收用户输入的传感器请求，根据请求中的参数执行相应的操作（如求平均值、最大值、最小值等），并将结果输出。在此过程中，还对用户输入的参数进行了严格的检查，确保请求的合法性。
 ******************************************************************************/
// 定义一个名为 GET_SENSOR 的函数，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int	GET_SENSOR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针和整型变量，用于后续操作
	char	*device, *name, *function;
	int	do_task, cnt = 0;
	double	aggr = 0;

	// 检查 request 中的参数数量，如果超过3个，则返回错误信息
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	device = get_rparam(request, 0);
	name = get_rparam(request, 1);
	function = get_rparam(request, 2);

	if (NULL == device || '\0' == *device)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == name || '\0' == *name)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

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

	if (ZBX_DO_ONE != do_task && 0 != isdigit(name[strlen(name) - 1]))
		do_task = ZBX_DO_ONE;

	if (ZBX_DO_ONE != do_task && 0 == isalpha(name[strlen(name) - 1]))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Generic sensor name must be specified for selected mode."));
		return SYSINFO_RET_FAIL;
	}

	get_device_sensors(do_task, device, name, &aggr, &cnt);

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
