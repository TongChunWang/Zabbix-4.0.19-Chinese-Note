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

#include "../common/common.h"
#include "sysinfo.h"
#include <sys/mman.h>
#include <setjmp.h>
#include <signal.h>
#include "zbxalgo.h"
#include "hardware.h"
#include "zbxregexp.h"
#include "log.h"


static ZBX_THREAD_LOCAL volatile char sigbus_handler_set;
static ZBX_THREAD_LOCAL sigjmp_buf sigbus_jmp_buf;


static void sigbus_handler(int signal)
{
	siglongjmp(sigbus_jmp_buf, signal);
}

static void install_sigbus_handler(void)
{
	// 定义一个结构体 sigaction，用于存储信号处理程序的相关信息
	struct sigaction act;

	if (0 == sigbus_handler_set)
	{
		sigbus_handler_set = 1;
		act.sa_handler = &sigbus_handler;
		act.sa_flags = SA_NODEFER;
		sigemptyset(&act.sa_mask);
		sigaction(SIGBUS, &act, NULL);
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是移除已设置的信号处理程序。当调用这个函数时，它会检查sigbus_handler_set是否已设置，如果已设置，则将原有的信号处理程序替换为默认处理程序，并清空信号掩码，使信号不再被忽略。最后将sigbus_handler_set设置为0，表示已成功移除信号处理程序。
 ******************************************************************************/
// 定义一个静态函数remove_sigbus_handler，用于移除信号处理程序
static void remove_sigbus_handler(void)
{
	// 定义一个结构体sigaction，用于存储信号处理程序的相关信息
	struct sigaction act;

	// 判断sigbus_handler_set是否已设置，如果已设置，则执行以下操作
	if (0 != sigbus_handler_set)
	{
		// 设置act的结构体，将sa_handler设置为SIG_DFL，即默认处理程序
		act.sa_handler = SIG_DFL;
		// 设置act的sa_flags为SA_NODEFER，表示不忽略信号
		act.sa_flags = SA_NODEFER;
		// 清空act的sa_mask，使其为空
		sigemptyset(&act.sa_mask);
		// 使用sigaction函数替换原有的信号处理程序
		sigaction(SIGBUS, &act, NULL);
	}
	// 将sigbus_handler_set设置为0，表示已移除信号处理程序
	sigbus_handler_set = 0;
}


/******************************************************************************
 *                                                                            *
 * Comments: read the string #num from dmi data into a buffer                 *
 *                                                                            *
 ******************************************************************************/
static size_t	get_dmi_string(char *buf, int bufsize, unsigned char *data, int num)
{
	char	*c = (char *)data;

	if (0 == num)
		return 0;

	c += data[1];	/* skip to string data */

	while (1 < num)
	{
		c += strlen(c);
		c++;
		num--;
	}

	return zbx_snprintf(buf, bufsize, " %s", c);
}

static size_t	get_chassis_type(char *buf, int bufsize, int type)
{
	/* 引用自 System Management BIOS (SMBIOS) 参考规范 v2.7.1 */
	static const char	*chassis_types[] =
	{
		"",			/* 0x00 */
		"Other",
		"Unknown",
		"Desktop",
		"Low Profile Desktop",
		"Pizza Box",
		"Mini Tower",
		"Tower",
		"Portable",
		"LapTop",
		"Notebook",
		"Hand Held",
		"Docking Station",
		"All in One",
		"Sub Notebook",
		"Space-saving",
		"Lunch Box",
		"Main Server Chassis",
		"Expansion Chassis",
		"SubChassis",
		"Bus Expansion Chassis",
		"Peripheral Chassis",
		"RAID Chassis",
		"Rack Mount Chassis",
		"Sealed-case PC",
		"Multi-system chassis",
		"Compact PCI",
		"Advanced TCA",
		"Blade",
		"Blade Enclosure",	/* 0x1d (MAX_CHASSIS_TYPE) */
	};

	type = CHASSIS_TYPE_BITS & type;

	if (1 > type || MAX_CHASSIS_TYPE < type)
		return 0;

	return zbx_snprintf(buf, bufsize, " %s", chassis_types[type]);
}

static int	get_dmi_info(char *buf, int bufsize, volatile int flags)
{
	// 定义变量，用于存储临时数据
	volatile int		ret = SYSINFO_RET_FAIL, fd, offset = 0;
	unsigned char	*volatile smbuf = NULL, *data;
	void		*volatile mmp = NULL;
	volatile size_t	len, page, page_offset;
	static size_t	pagesize = 0;
	static int	smbios_status = SMBIOS_STATUS_UNKNOWN;
	static size_t	smbios_len, smbios;	/* length and address of SMBIOS table (if found) */

	// 打开系统表文件，读取DMI信息
	if (-1 != (fd = open(SYS_TABLE_FILE, O_RDONLY)))
	{
		ssize_t		nbytes;
		zbx_stat_t	file_buf;

		// 获取文件大小
		if (-1 == fstat(fd, &file_buf))
			goto close;

		// 分配内存存储DMI信息
		smbuf = (unsigned char *)zbx_malloc(NULL, file_buf.st_size);

		// 初始化DMI信息长度为0
		smbios_len = 0;

		// 循环读取文件中的DMI信息
		while (0 != (nbytes = read(fd, smbuf + smbios_len, file_buf.st_size - smbios_len)))
		{
			// 读取失败则退出循环
			if (-1 == nbytes)
				goto clean;

			// 更新DMI信息长度
			smbios_len += (size_t)nbytes;
		}
	}
	// 打开内存设备，查找DMI信息
	else if (-1 != (fd = open(DEV_MEM, O_RDONLY)))
	{
		// 查找DMI表的唯一次数
		if (SMBIOS_STATUS_UNKNOWN == smbios_status &&
			(size_t)-1 != (pagesize = sysconf(_SC_PAGESIZE)))
		{
			/* on some platforms mmap() result does not indicate that address is not available, */
			/* but then SIGBUS is raised accessing the memory */
			install_sigbus_handler();

			/* find smbios entry point - located between 0xF0000 and 0xFFFFF (according to the specs) */
			for(page = 0xf0000; page < 0xfffff; page += pagesize)
			{
				/* mmp needs to be a multiple of pagesize for munmap */
				if (MAP_FAILED == (mmp = mmap(0, pagesize, PROT_READ, MAP_SHARED, fd, page)))
					goto close;

				if (0 != sigsetjmp(sigbus_jmp_buf, 0)) /* we get here if memory address is not valid */
				{
					munmap(mmp, pagesize);
					goto close;
				}

				for(page_offset = 0; page_offset < pagesize; page_offset += 16)
				{
					data = (unsigned char *)mmp + page_offset;

					// 找到DMI表入口点
					if (0 == strncmp((char *)data, "_DMI_", 5))
					{
						smbios_len = data[7] << 8 | data[6];
						smbios = (size_t)data[11] << 24 | (size_t)data[10] << 16 |
								(size_t)data[9] << 8 | data[8];

						// 判断DMI表是否有效
						if (0 == smbios || 0 == smbios_len)
							smbios_status = SMBIOS_STATUS_ERROR;
						else
							smbios_status = SMBIOS_STATUS_OK;

						break;
					}
				}

				munmap(mmp, pagesize);
				if (SMBIOS_STATUS_UNKNOWN != smbios_status)
					break;
			}
		}

		if (SMBIOS_STATUS_OK != smbios_status)
		{
			smbios_status = SMBIOS_STATUS_ERROR;
			goto close;
		}

		smbuf = (unsigned char *)zbx_malloc(smbuf, smbios_len);

		len = smbios % pagesize;	/* mmp needs to be a multiple of pagesize for munmap */
		if (MAP_FAILED == (mmp = mmap(0, len + smbios_len, PROT_READ, MAP_SHARED, fd, smbios - len)))
			goto clean;

		if (0 == sigsetjmp(sigbus_jmp_buf, 0))
			memcpy(smbuf, (char *)mmp + len, smbios_len);

		munmap(mmp, len + smbios_len);
	}
	else
		return ret;

	data = smbuf;
	while (data + DMI_HEADER_SIZE <= smbuf + smbios_len)
	{
		if (1 == data[0])	/* system information */
		{
			if (0 != (flags & DMI_GET_VENDOR))
			{
				offset += get_dmi_string(buf + offset, bufsize - offset, data, data[4]);
				flags &= ~DMI_GET_VENDOR;
			}

			if (0 != (flags & DMI_GET_MODEL))
			{
				offset += get_dmi_string(buf + offset, bufsize - offset, data, data[5]);
				flags &= ~DMI_GET_MODEL;
			}

			if (0 != (flags & DMI_GET_SERIAL))
			{
				offset += get_dmi_string(buf + offset, bufsize - offset, data, data[7]);
				flags &= ~DMI_GET_SERIAL;
			}
		}
		else if (3 == data[0] && 0 != (flags & DMI_GET_TYPE))	/* chassis */
		{
			offset += get_chassis_type(buf + offset, bufsize - offset, data[5]);
			flags &= ~DMI_GET_TYPE;
		}

		if (0 == flags)
			break;

		data += data[1];			/* skip the main data */
		while (0 != data[0] || 0 != data[1])	/* string data ends with two nulls */
		{
			data++;
		}
		data += 2;
	}

	if (0 < offset)
		ret = SYSINFO_RET_OK;
clean:
	zbx_free(smbuf);
close:
	close(fd);
	remove_sigbus_handler();

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个请求结构体和一个结果结构体，根据请求结构体中的参数，获取对应的硬件信息，并将结果存储在结果结构体中返回。具体操作如下：
 *
 *1. 检查请求结构体中的参数数量，如果数量大于1，则返回失败。
 *2. 获取第一个请求参数，即模式字符串。
 *3. 根据模式字符串的不同，获取相应的硬件信息，包括类型、供应商、型号和序列号。
 *4. 如果模式字符串为空、空字符或等于\"full\"，则默认显示全部信息。
 *5. 如果获取硬件信息失败，返回失败，并携带错误信息。
 *6. 将获取到的硬件信息存储在结果结构体中，并返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，接收两个参数，一个是请求结构体指针，一个是结果结构体指针
int SYSTEM_HW_CHASSIS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个字符指针和一个字符数组，用于存储缓冲区
    char *mode, buf[MAX_STRING_LEN];
    // 定义一个整型变量，用于存储函数返回值
    int ret = SYSINFO_RET_FAIL;

    // 检查请求参数的数量，如果数量大于1，则返回失败
    if (1 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个请求参数，即模式字符串
    mode = get_rparam(request, 0);

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "full"))	/* show full info by default */
		ret = get_dmi_info(buf, sizeof(buf), DMI_GET_TYPE | DMI_GET_VENDOR | DMI_GET_MODEL | DMI_GET_SERIAL);
	else if (0 == strcmp(mode, "type"))
		ret = get_dmi_info(buf, sizeof(buf), DMI_GET_TYPE);
	else if (0 == strcmp(mode, "vendor"))
		ret = get_dmi_info(buf, sizeof(buf), DMI_GET_VENDOR);
	else if (0 == strcmp(mode, "model"))
		ret = get_dmi_info(buf, sizeof(buf), DMI_GET_MODEL);
	else if (0 == strcmp(mode, "serial"))
		ret = get_dmi_info(buf, sizeof(buf), DMI_GET_SERIAL);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_FAIL == ret)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain hardware information."));
		return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, buf + 1));	/* buf has a leading space */

	return ret;
}

static zbx_uint64_t	get_cpu_max_freq(int cpu_num)
{
	zbx_uint64_t	freq = FAIL;
	char		filename[MAX_STRING_LEN];
	FILE		*f;

	zbx_snprintf(filename, sizeof(filename), CPU_MAX_FREQ_FILE, cpu_num);

	f = fopen(filename, "r");

	if (NULL != f)
	{
		if (1 != fscanf(f, ZBX_FS_UI64, &freq))
			freq = FAIL;

		fclose(f);
	}

	return freq;
}

static size_t	print_freq(char *buffer, size_t size, int filter, int cpu, zbx_uint64_t maxfreq, zbx_uint64_t curfreq)
{
	size_t	offset = 0;

	if (HW_CPU_SHOW_MAXFREQ == filter && FAIL != (int)maxfreq)
	{
		if (HW_CPU_ALL_CPUS == cpu)
			offset += zbx_snprintf(buffer + offset, size - offset, " " ZBX_FS_UI64 "MHz", maxfreq / 1000);
		else
			offset += zbx_snprintf(buffer + offset, size - offset, " " ZBX_FS_UI64, maxfreq * 1000);
	}
	else if (HW_CPU_SHOW_CURFREQ == filter && FAIL != (int)curfreq)
	{
		if (HW_CPU_ALL_CPUS == cpu)
			offset += zbx_snprintf(buffer + offset, size - offset, " " ZBX_FS_UI64 "MHz", curfreq);
		else
			offset += zbx_snprintf(buffer + offset, size - offset, " " ZBX_FS_UI64, curfreq * 1000000);
	}
	else if (HW_CPU_SHOW_ALL == filter)
	{
		if (FAIL != (int)curfreq)
			offset += zbx_snprintf(buffer + offset, size - offset, " working at " ZBX_FS_UI64 "MHz", curfreq);

		if (FAIL != (int)maxfreq)
			offset += zbx_snprintf(buffer + offset, size - offset, " (maximum " ZBX_FS_UI64 "MHz)", maxfreq / 1000);
	}

	return offset;
}

int     SYSTEM_HW_CPU(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		ret = SYSINFO_RET_FAIL, filter, cpu, cur_cpu = -1, offset = 0;
	zbx_uint64_t	maxfreq = FAIL, curfreq = FAIL;
	char		line[MAX_STRING_LEN], name[MAX_STRING_LEN], tmp[MAX_STRING_LEN], buffer[MAX_BUFFER_LEN], *param;
	FILE		*f;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	param = get_rparam(request, 0);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "all"))
		cpu = HW_CPU_ALL_CPUS;	/* show all CPUs by default */
	else if (FAIL == is_uint31(param, &cpu))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	param = get_rparam(request, 1);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "full"))
		filter = HW_CPU_SHOW_ALL;	/* show full info by default */
	else if (0 == strcmp(param, "maxfreq"))
		filter = HW_CPU_SHOW_MAXFREQ;
	else if (0 == strcmp(param, "vendor"))
		filter = HW_CPU_SHOW_VENDOR;
	else if (0 == strcmp(param, "model"))
		filter = HW_CPU_SHOW_MODEL;
	else if (0 == strcmp(param, "curfreq"))
		filter = HW_CPU_SHOW_CURFREQ;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (f = fopen(HW_CPU_INFO_FILE, "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open " HW_CPU_INFO_FILE ": %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	*buffer = '\0';

	while (NULL != fgets(line, sizeof(line), f))
	{
		if (2 != sscanf(line, "%[^:]: %[^\n]", name, tmp))
			continue;

		if (0 == strncmp(name, "processor", 9))
		{
			if (-1 != cur_cpu && (HW_CPU_ALL_CPUS == cpu || cpu == cur_cpu))	/* print info about the previous cpu */
				offset += print_freq(buffer + offset, sizeof(buffer) - offset, filter, cpu, maxfreq, curfreq);

			curfreq = FAIL;
			cur_cpu = atoi(tmp);

			if (HW_CPU_ALL_CPUS != cpu && cpu != cur_cpu)
				continue;

			if (HW_CPU_ALL_CPUS == cpu || HW_CPU_SHOW_ALL == filter)
				offset += zbx_snprintf(buffer + offset, sizeof(buffer) - offset, "\nprocessor %d:", cur_cpu);

			if ((HW_CPU_SHOW_ALL == filter || HW_CPU_SHOW_MAXFREQ == filter) &&
					FAIL != (int)(maxfreq = get_cpu_max_freq(cur_cpu)))
			{
				ret = SYSINFO_RET_OK;
			}
		}

		if (HW_CPU_ALL_CPUS != cpu && cpu != cur_cpu)
			continue;

		if (0 == strncmp(name, "vendor_id", 9) && (HW_CPU_SHOW_ALL == filter || HW_CPU_SHOW_VENDOR == filter))
		{
			ret = SYSINFO_RET_OK;
			offset += zbx_snprintf(buffer + offset, sizeof(buffer) - offset, " %s", tmp);
		}
		else if (0 == strncmp(name, "model name", 10) && (HW_CPU_SHOW_ALL == filter || HW_CPU_SHOW_MODEL == filter))
		{
			ret = SYSINFO_RET_OK;
			offset += zbx_snprintf(buffer + offset, sizeof(buffer) - offset, " %s", tmp);
		}
		else if (0 == strncmp(name, "cpu MHz", 7) && (HW_CPU_SHOW_ALL == filter || HW_CPU_SHOW_CURFREQ == filter))
		{
			ret = SYSINFO_RET_OK;
			sscanf(tmp, ZBX_FS_UI64, &curfreq);
		}
	}

	zbx_fclose(f);

	if (SYSINFO_RET_FAIL == ret)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain CPU information."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 != cur_cpu && (HW_CPU_ALL_CPUS == cpu || cpu == cur_cpu))	/* print info about the last cpu */
		print_freq(buffer + offset, sizeof(buffer) - offset, filter, cpu, maxfreq, curfreq);

	SET_TEXT_RESULT(result, zbx_strdup(NULL, buffer + 1));	/* buf has a leading space or '\n' */

	return ret;
}

int	SYSTEM_HW_DEVICES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*type;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	type = get_rparam(request, 0);

	if (NULL == type || '\0' == *type || 0 == strcmp(type, "pci"))
		return EXECUTE_STR("lspci", result);	/* list PCI devices by default */
	else if (0 == strcmp(type, "usb"))
		return EXECUTE_STR("lsusb", result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}
}

int     SYSTEM_HW_MACADDR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义变量，后续操作所需的缓冲区大小
	size_t			offset;
	int			s, i, show_names;
	char			*format, *p, *regex, address[MAX_STRING_LEN], buffer[MAX_STRING_LEN];
	struct ifreq		*ifr;
	struct ifconf		ifc;
	zbx_vector_str_t	addresses;

	// 检查参数数量，如果超过2个，返回错误
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（正则表达式）
	regex = get_rparam(request, 0);
	// 获取第二个参数（输出格式）
	format = get_rparam(request, 1);

	// 根据格式判断是否显示接口名称
	if (NULL == format || '\0' == *format || 0 == strcmp(format, "full"))
		show_names = 1;	/* show interface names */
	else if (0 == strcmp(format, "short"))
		show_names = 0;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 创建套接字
	if (-1 == (s = socket(AF_INET, SOCK_DGRAM, 0)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create socket: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	/* get the interface list */
	ifc.ifc_len = sizeof(buffer);
	ifc.ifc_buf = buffer;
	if (-1 == ioctl(s, SIOCGIFCONF, &ifc))
	{
		close(s);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot set socket parameters: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}
	ifr = ifc.ifc_req;

	zbx_vector_str_create(&addresses);
	zbx_vector_str_reserve(&addresses, 8);

	/* go through the list */
	for (i = ifc.ifc_len / sizeof(struct ifreq); 0 < i--; ifr++)
	{
		if (NULL != regex && '\0' != *regex && NULL == zbx_regexp_match(ifr->ifr_name, regex, NULL))
			continue;

		if (-1 != ioctl(s, SIOCGIFFLAGS, ifr) &&		/* get the interface */
				0 == (ifr->ifr_flags & IFF_LOOPBACK) &&	/* skip loopback interface */
				-1 != ioctl(s, SIOCGIFHWADDR, ifr))	/* get the MAC address */
		{
			offset = 0;

			if (1 == show_names)
				offset += zbx_snprintf(address + offset, sizeof(address) - offset, "[%s  ", ifr->ifr_name);

			zbx_snprintf(address + offset, sizeof(address) - offset, "%.2hx:%.2hx:%.2hx:%.2hx:%.2hx:%.2hx",
					(unsigned short int)(unsigned char)ifr->ifr_hwaddr.sa_data[0],
					(unsigned short int)(unsigned char)ifr->ifr_hwaddr.sa_data[1],
					(unsigned short int)(unsigned char)ifr->ifr_hwaddr.sa_data[2],
					(unsigned short int)(unsigned char)ifr->ifr_hwaddr.sa_data[3],
					(unsigned short int)(unsigned char)ifr->ifr_hwaddr.sa_data[4],
					(unsigned short int)(unsigned char)ifr->ifr_hwaddr.sa_data[5]);

			if (0 == show_names && FAIL != zbx_vector_str_search(&addresses, address, ZBX_DEFAULT_STR_COMPARE_FUNC))
				continue;

			zbx_vector_str_append(&addresses, zbx_strdup(NULL, address));
		}
	}

	offset = 0;

	if (0 != addresses.values_num)
	{
		zbx_vector_str_sort(&addresses, ZBX_DEFAULT_STR_COMPARE_FUNC);

		for (i = 0; i < addresses.values_num; i++)
		{
			if (1 == show_names && NULL != (p = strchr(addresses.values[i], ' ')))
				*p = ']';

			offset += zbx_snprintf(buffer + offset, sizeof(buffer) - offset, "%s, ", addresses.values[i]);
			zbx_free(addresses.values[i]);
		}

		offset -= 2;
	}

	buffer[offset] = '\0';

	SET_STR_RESULT(result, zbx_strdup(NULL, buffer));

	zbx_vector_str_destroy(&addresses);
	close(s);

	return SYSINFO_RET_OK;
}
