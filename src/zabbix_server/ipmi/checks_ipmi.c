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

#include "checks_ipmi.h"

#ifdef HAVE_OPENIPMI

/* Theoretically it should be enough max 16 bytes for sensor ID and terminating '\0' (see SDR record format in IPMI */
/* v2 spec). OpenIPMI author Corey Minyard explained at	*/
/* www.mail-archive.com/openipmi-developer@lists.sourceforge.net/msg02013.html: */
/* "...Since you can use BCD and the field is 16 bytes max, you can get up to 32 bytes in the ID string. Adding the */
/* sensor sharing and that's another three bytes (I believe 142 is the maximum number you can get), so 35 bytes is  */
/* the maximum, I believe." */
#define IPMI_SENSOR_ID_SZ	36

/* delete inactive hosts after this period */
#define INACTIVE_HOST_LIMIT	3 * SEC_PER_HOUR

#include "log.h"

#include <OpenIPMI/ipmiif.h>
#include <OpenIPMI/ipmi_posix.h>
#include <OpenIPMI/ipmi_lan.h>
#include <OpenIPMI/ipmi_auth.h>

#define RETURN_IF_CB_DATA_NULL(x, y)							\
	if (NULL == (x))								\
	{										\
		zabbix_log(LOG_LEVEL_WARNING, "%s() called with cb_data:NULL", (y));	\
		return;									\
	}

// 定义一个联合体类型zbx_ipmi_sensor_value_t，包含两个成员：double类型的threshold和zbx_uint64_t类型的discrete
typedef union
{
    double		threshold;
    zbx_uint64_t	discrete;
}
zbx_ipmi_sensor_value_t;

// 定义一个结构体类型zbx_ipmi_sensor_t，包含以下成员：
// ipmi_sensor_t指针的传感器对象
// 字符串类型的传感器ID，长度为IPMI_SENSOR_ID_SZ
// 枚举类型ipmi_str_type_e的传感器ID类型，例如Unicode、BCD plus、6-bit ASCII packed、8-bit ASCII+Latin1等
// 传感器ID的字节长度
// zbx_ipmi_sensor_value_t类型的值，包含传感器读数类型（如阈值、离散值等）
// 传感器类型代码，如温度、电压、电流、风扇、物理安全（机箱入侵）等
// 传感器全名

typedef struct
{
    ipmi_sensor_t	*sensor;
    char		id[IPMI_SENSOR_ID_SZ];
    enum ipmi_str_type_e	id_type;	/* For sensors IPMI specifications mention Unicode, BCD plus, */
                                    /* 6-bit ASCII packed, 8-bit ASCII+Latin1.  */
    int			id_sz;		/* "id" value length in bytes */
    zbx_ipmi_sensor_value_t	value;
    int			reading_type;	/* "Event/Reading Type Code", e.g. Threshold, */
                                    /* Discrete, 'digital' Discrete. */
    int			type;		/* "Sensor Type Code", e.g. Temperature, Voltage, */
                                    /* Current, Fan, Physical Security (Chassis Intrusion), etc. */
    char		*full_name;
}
zbx_ipmi_sensor_t;

// 定义一个结构体类型zbx_ipmi_control_t，包含以下成员：
// ipmi_control_t指针的控制对象
// 字符串类型的控制名称
// 控制值的数量
// 整型数组的值，长度为num_values
// 控制的全名

typedef struct
{
    ipmi_control_t	*control;
    char		*c_name;
    int			num_values;
    int			*val;
    char		*full_name;
}
zbx_ipmi_control_t;

// 定义一个结构体类型zbx_ipmi_host，包含以下成员：
// 字符串类型的IP地址
// 端口号
// 认证类型
// 权限级别
// 返回值
// 用户名
// 密码
// 传感器列表
// 控制列表
// 传感器数量
// 控制数量
// IPMI会话指针
// 主机状态（是否已域）
// 完成状态
// 最后一次访问时间
// 主机域名序列号
// 错误信息
// 下一个zbx_ipmi_host结构体指针（单链表节点）

typedef struct zbx_ipmi_host
{
    char		*ip;
    int			port;
    int			authtype;
    int			privilege;
    int			ret;
    char		*username;
    char		*password;
    zbx_ipmi_sensor_t	*sensors;
    zbx_ipmi_control_t	*controls;
    int			sensor_count;
    int			control_count;
    ipmi_con_t		*con;
    int			domain_up;
    int			done;
    time_t		lastaccess;	/* Time of last access attempt. Used to detect and delete inactive */
                                /* (disabled) IPMI hosts from OpenIPMI to stop polling them. */
    unsigned int		domain_nr;	/* Domain number. It is converted to text string and used as */
                                /* domain name. */
    char		*err;
    struct zbx_ipmi_host	*next;
}
zbx_ipmi_host_t;

// 静态变量，存储域名序列号
static unsigned int	domain_nr = 0;

// 静态变量，存储监控的主机列表头指针
static zbx_ipmi_host_t	*hosts = NULL;

// 静态变量，存储OS处理器句柄
static os_handler_t	*os_hnd;

// 函数：static char *zbx_sensor_id_to_str(char *str, size_t str_sz, const char *id, enum ipmi_str_type_e id_type, int id_sz)
// 功能：将传感器ID转换为字符串，根据ID类型和长度填充字符缓冲区str
// 参数：
//   str：字符缓冲区，用于存储转换后的字符串
//   str_sz：字符缓冲区的最大长度
//   id：传感器ID
//   id_type：传感器ID类型，如Unicode、BCD plus等
//   id_sz：传感器ID的字节长度
// 返回值：转换后的字符串（str）

static char	*zbx_sensor_id_to_str(char *str, size_t str_sz, const char *id, enum ipmi_str_type_e id_type, int id_sz)
{
    // 确保缓冲区str的最小大小为35字节，以避免截断
    int	i;
    char	*p = str;
    size_t	id_len;

    // 如果id_sz为0，则id为空，返回空字符串
    if (0 == id_sz)
    {
        *str = '\0';
        return str;
    }

	if (IPMI_SENSOR_ID_SZ < id_sz)
	{
		zbx_strlcpy(str, "ILLEGAL-SENSOR-ID-SIZE", str_sz);
		THIS_SHOULD_NEVER_HAPPEN;
		return str;
	}

	switch (id_type)
	{
		case IPMI_ASCII_STR:
		case IPMI_UNICODE_STR:
			id_len = str_sz > (size_t)id_sz ? (size_t)id_sz : str_sz - 1;
			memcpy(str, id, id_len);
			*(str + id_len) = '\0';
			break;
		case IPMI_BINARY_STR:
			/* "BCD Plus" or "6-bit ASCII packed" encoding - print it as a hex string. */

            *p++ = '0';	/* 添加前缀，与ASCII/Unicode字符串区分 */
            *p++ = 'x';
            for (i = 0; i < id_sz; i++, p += 2)
            {
                zbx_snprintf(p, str_sz - (size_t)(2 + i + i), "%02x",
                            (unsigned int)(unsigned char)*(id + i));
            }
            *p = '\0';
            break;
        default:
            // 非法传感器ID类型，返回错误字符串
            zbx_strlcpy(str, "ILLEGAL-SENSOR-ID-TYPE", str_sz);
            THIS_SHOULD_NEVER_HAPPEN;
    }

    return str;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_get_ipmi_host                                                *
 *                                                                            *
 * Purpose: Find element in the global list 'hosts' using parameters as       *
 *          search criteria                                                   *
 *                                                                            *
 * Return value: pointer to list element with host data                       *
 *               NULL if not found                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据输入的IP、端口、认证类型、权限、用户名和密码查找对应的zbx_ipmi_host结构体指针。如果找到匹配的元素，则返回该元素的指针；否则返回NULL。
 ******************************************************************************/
// 定义一个静态函数zbx_get_ipmi_host，用于根据IP、端口、认证类型、权限、用户名和密码查找对应的zbx_ipmi_host结构体指针
static zbx_ipmi_host_t *zbx_get_ipmi_host(const char *ip, const int port, int authtype, int privilege,
                                          const char *username, const char *password)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "zbx_get_ipmi_host";
    zbx_ipmi_host_t *h; // 定义一个zbx_ipmi_host结构体指针，用于存储查找的结果

    // 记录日志，表示进入函数，输入参数为IP和端口
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d'", __function_name, ip, port);

    // 初始化指针h，使其指向hosts列表的头元素
    h = hosts;
    // 使用while循环遍历hosts列表
    while (NULL != h)
    {
        // 判断当前元素是否与输入的IP、端口、认证类型、权限、用户名和密码匹配
        if (0 == strcmp(ip, h->ip) && port == h->port && authtype == h->authtype &&
                privilege == h->privilege && 0 == strcmp(username, h->username) &&
                0 == strcmp(password, h->password))
        {
            // 匹配成功，跳出循环
            break;
        }

        // 遍历到下一个元素
        h = h->next;
    }

    // 记录日志，表示查找结束，输出查找结果的指针
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)h);

    // 返回查找到的zbx_ipmi_host结构体指针
    return h;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_allocate_ipmi_host                                           *
 *                                                                            *
 * Purpose: create a new element in the global list 'hosts'                   *
 *                                                                            *
 * Return value: pointer to the new list element with host data               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是分配一个zbx_ipmi_host_t结构体，并将其插入到hosts链表中。这个函数用于创建新的ipmi主机对象，并设置其属性，如IP地址、端口、认证类型、权限级别、用户名和密码。在这个过程中，还记录了函数执行的日志。
 ******************************************************************************/
// 定义一个静态函数zbx_allocate_ipmi_host，接收6个参数：ipmi主机的IP地址、端口、认证类型、权限级别、用户名和密码
static zbx_ipmi_host_t *zbx_allocate_ipmi_host(const char *ip, int port, int authtype, int privilege,
                                                const char *username, const char *password)
{
    // 定义一个静态常量，存储函数名
    const char *__function_name = "zbx_allocate_ipmi_host";
    zbx_ipmi_host_t *h;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d'", __function_name, ip, port);

	h = (zbx_ipmi_host_t *)zbx_malloc(NULL, sizeof(zbx_ipmi_host_t));

	memset(h, 0, sizeof(zbx_ipmi_host_t));

	h->ip = strdup(ip);
	h->port = port;
	h->authtype = authtype;
	h->privilege = privilege;
	h->username = strdup(username);
	h->password = strdup(password);
	h->domain_nr = domain_nr++;

    // 将新分配的结构体插入到hosts链表中，作为链表的头节点
    h->next = hosts;
    hosts = h;

    // 记录日志，表示函数执行完毕，输出分配的内存地址
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)h);

    // 返回分配的zbx_ipmi_host_t结构体指针
    return h;
}


static zbx_ipmi_sensor_t	*zbx_get_ipmi_sensor(const zbx_ipmi_host_t *h, const ipmi_sensor_t *sensor)
{
	const char		*__function_name = "zbx_get_ipmi_sensor";
	int			i;
	zbx_ipmi_sensor_t	*s = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() phost:%p psensor:%p", __function_name, (const void *)h,
			(const void *)sensor);

	for (i = 0; i < h->sensor_count; i++)
	{
		if (h->sensors[i].sensor == sensor)
		{
			s = &h->sensors[i];
			break;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)s);

	return s;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的主机信息和传感器ID，在主机对应的传感器数组中查找匹配的传感器，并返回该传感器的指针。在查找过程中，优先选择阈值传感器。
 ******************************************************************************/
// 定义一个静态指针变量，用于存储zbx_ipmi_sensor_t结构体的指针
static zbx_ipmi_sensor_t *zbx_get_ipmi_sensor_by_id(const zbx_ipmi_host_t *h, const char *id)
{
    // 定义一个字符串指针，用于存储函数名
    const char *__function_name = "zbx_get_ipmi_sensor_by_id";
    // 定义一个整数变量i，用于循环计数
    int i;
    // 定义一个指向zbx_ipmi_sensor_t结构体的指针，初始值为NULL
    zbx_ipmi_sensor_t *s = NULL;

    // 使用zabbix_log记录调试信息，表示进入函数，输出传感器ID、主机IP和端口
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() sensor:'%s@[%s]:%d'", __function_name, id, h->ip, h->port);

	for (i = 0; i < h->sensor_count; i++)
	{
		if (0 == strcmp(h->sensors[i].id, id))
		{
			/* Some devices present a sensor as both a threshold sensor and a discrete sensor. We work */
			/* around this by preferring the threshold sensor in such case, as it is most widely used. */

			s = &h->sensors[i];

			if (IPMI_EVENT_READING_TYPE_THRESHOLD == s->reading_type)
				break;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)s);

	// 返回新分配的zbx_ipmi_sensor结构体实例
	return s;
}

static zbx_ipmi_sensor_t	*zbx_get_ipmi_sensor_by_full_name(const zbx_ipmi_host_t *h, const char *full_name)
{
	const char		*__function_name = "zbx_get_ipmi_sensor_by_full_name";
	int			i;
	zbx_ipmi_sensor_t	*s = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() sensor:'%s@[%s]:%d", __function_name, full_name, h->ip, h->port);

	for (i = 0; i < h->sensor_count; i++)
	{
		if (0 == strcmp(h->sensors[i].full_name, full_name))
		{
			s = &h->sensors[i];
			break;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)s);

	return s;
}

/******************************************************************************
 *                                                                            *
 * Function: get_domain_offset                                                *
 *                                                                            *
 * Purpose: Check if an item name starts from domain name and find the domain *
 *          name length                                                       *
 *                                                                            *
 * Parameters: h         - [IN] ipmi host                                     *
 *             full_name - [IN] item name                                     *
 *                                                                            *
 * Return value: 0 or offset for skipping the domain name                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算字符串 full_name 与域名字符串 domain_name 之间的偏移量。函数 get_domain_offset 接收两个参数，一个是 zbx_ipmi_host_t 结构体的指针 h，表示一个 IPMI 主机；另一个是字符串 full_name。
 *
 *函数首先定义一个字符数组 domain_name，用于存储域名。然后使用 zbx_snprintf 函数将 h 指向的域名转换为字符串，并存储在 domain_name 数组中。接下来计算 domain_name 字符串的长度，并将其存储在 size_t 类型的变量 offset 中。
 *
 *接着判断 domain_name 字符串是否等于 full_name 字符串的前 offset 个字符。如果不相等，将 offset 设置为 0。最后返回偏移量 offset。这样就可以根据 full_name 字符串和域名字符串之间的偏移量来获取域名。
 ******************************************************************************/
// 定义一个名为 get_domain_offset 的静态 size_t 类型函数，接收两个参数：
// 参数1：指向 zbx_ipmi_host_t 结构体的指针 h
// 参数2：字符串 full_name
// 声明一个静态函数，用于获取域名偏移量
static size_t	get_domain_offset(const zbx_ipmi_host_t *h, const char *full_name) // 定义函数原型，传入两个参数
{
	char	domain_name[IPMI_DOMAIN_NAME_LEN]; // 定义一个字符数组，用于存储域名
	size_t	offset; // 定义一个 size_t 类型的变量 offset，用于存储偏移量

	zbx_snprintf(domain_name, sizeof(domain_name), "%u", h->domain_nr); // 使用 zbx_snprintf 函数将 h 指向的域名转换为字符串，并存储在 domain_name 数组中
	offset = strlen(domain_name); // 获取 domain_name 字符串的长度，并将其存储在 offset 变量中

	if (offset >= strlen(full_name) || 0 != strncmp(domain_name, full_name, offset)) // 判断 domain_name 字符串是否等于 full_name 字符串的前 offset 个字符
		offset = 0; // 如果不相等，将 offset 设置为 0

	return offset; // 返回偏移量 offset
}


static zbx_ipmi_sensor_t	*zbx_allocate_ipmi_sensor(zbx_ipmi_host_t *h, ipmi_sensor_t *sensor)
{
	const char		*__function_name = "zbx_allocate_ipmi_sensor";
	char			id_str[2 * IPMI_SENSOR_ID_SZ + 1];
	zbx_ipmi_sensor_t	*s;
	char			id[IPMI_SENSOR_ID_SZ];
	enum ipmi_str_type_e	id_type;
	int			id_sz;
	size_t			sz;
	char			full_name[IPMI_SENSOR_NAME_LEN];

	id_sz = ipmi_sensor_get_id_length(sensor);
	memset(id, 0, sizeof(id));
	ipmi_sensor_get_id(sensor, id, sizeof(id));
	id_type = ipmi_sensor_get_id_type(sensor);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() sensor:'%s@[%s]:%d'", __function_name,
			zbx_sensor_id_to_str(id_str, sizeof(id_str), id, id_type, id_sz), h->ip, h->port);

	h->sensor_count++;
	sz = (size_t)h->sensor_count * sizeof(zbx_ipmi_sensor_t);

	if (NULL == h->sensors)
		h->sensors = (zbx_ipmi_sensor_t *)zbx_malloc(h->sensors, sz);
	else
		h->sensors = (zbx_ipmi_sensor_t *)zbx_realloc(h->sensors, sz);

	s = &h->sensors[h->sensor_count - 1];
	s->sensor = sensor;
	memcpy(s->id, id, sizeof(id));
	s->id_type = id_type;
	s->id_sz = id_sz;
	memset(&s->value, 0, sizeof(s->value));
	s->reading_type = ipmi_sensor_get_event_reading_type(sensor);
	s->type = ipmi_sensor_get_sensor_type(sensor);

	ipmi_sensor_get_name(s->sensor, full_name, sizeof(full_name));
	s->full_name = zbx_strdup(NULL, full_name + get_domain_offset(h, full_name));

	zabbix_log(LOG_LEVEL_DEBUG, "Added sensor: host:'%s:%d' id_type:%d id_sz:%d id:'%s' reading_type:0x%x "
			"('%s') type:0x%x ('%s') domain:'%u' name:'%s'", h->ip, h->port, (int)s->id_type, s->id_sz,
			zbx_sensor_id_to_str(id_str, sizeof(id_str), s->id, s->id_type, s->id_sz),
			(unsigned int)s->reading_type, ipmi_sensor_get_event_reading_type_string(s->sensor),
			(unsigned int)s->type, ipmi_sensor_get_sensor_type_string(s->sensor), h->domain_nr,
			s->full_name);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)s);

	return s;
}

static void	zbx_delete_ipmi_sensor(zbx_ipmi_host_t *h, const ipmi_sensor_t *sensor)
{
    // 定义一个静态函数，用于删除IPMI传感器
    // 参数：指向zbx_ipmi_host结构体的指针h，指向ipmi_sensor结构体的指针sensor

    const char	*__function_name = "zbx_delete_ipmi_sensor";
    char		id_str[2 * IPMI_SENSOR_ID_SZ + 1];
    int		i;
    size_t		sz;

    // 使用zabbix_log记录调试信息，表示进入该函数，输出函数名、主机指针、传感器指针
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() phost:%p psensor:%p", __function_name, (void *)h, (const void *)sensor);

    // 遍历主机h中的传感器数组
    for (i = 0; i < h->sensor_count; i++)
    {
        // 如果当前传感器不是要删除的传感器，继续遍历
        if (h->sensors[i].sensor != sensor)
            continue;

        // 计算zbx_ipmi_sensor_t结构体的大小
        sz = sizeof(zbx_ipmi_sensor_t);

		zabbix_log(LOG_LEVEL_DEBUG, "sensor '%s@[%s]:%d' deleted",
				zbx_sensor_id_to_str(id_str, sizeof(id_str), h->sensors[i].id, h->sensors[i].id_type,
				h->sensors[i].id_sz), h->ip, h->port);

		zbx_free(h->sensors[i].full_name);

		h->sensor_count--;
		if (h->sensor_count != i)
			memmove(&h->sensors[i], &h->sensors[i + 1], sz * (size_t)(h->sensor_count - i));
		h->sensors = (zbx_ipmi_sensor_t *)zbx_realloc(h->sensors, sz * (size_t)h->sensor_count);

		break;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查找zbx_ipmi_host结构体中的controls数组中，与传入的ipmi_control_t类型的控制器匹配的项，并返回该匹配项的指针。
 ******************************************************************************/
// 定义一个静态函数zbx_get_ipmi_control，接收两个参数：zbx_ipmi_host_t类型的指针h和ipmi_control_t类型的指针control
static zbx_ipmi_control_t *zbx_get_ipmi_control(const zbx_ipmi_host_t *h, const ipmi_control_t *control)
{
	// 定义一个静态字符串变量__function_name，用于存储函数名
	const char *__function_name = "zbx_get_ipmi_control";
	// 定义一个整型变量i，用于循环计数
	int			i;
	// 定义一个zbx_ipmi_control_t类型的指针变量c，初始值为NULL
	zbx_ipmi_control_t	*c = NULL;

	// 使用zabbix_log记录调试信息，输出函数名、传入的phost和pcontrol的地址
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() phost:%p pcontrol:%p", __function_name, (const void *)h,
			(const void *)control);

	// 遍历host结构体的controls数组，查找与传入的控制器control匹配的项
	for (i = 0; i < h->control_count; i++)
	{
		// 如果当前项的control与传入的控制器control相等，则找到匹配项
		if (h->controls[i].control == control)
		{
			// 保存匹配项的地址到指针变量c
			c = &h->controls[i];
			// 跳出循环
			break;
		}
	}

	// 使用zabbix_log记录调试信息，输出查找结束和找到的c的地址
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)c);

	// 返回找到的匹配项指针c
	return c;
}

static zbx_ipmi_control_t	*zbx_get_ipmi_control_by_name(const zbx_ipmi_host_t *h, const char *c_name)
{
    // 定义一个内部函数名，方便调试
    const char		*__function_name = "zbx_get_ipmi_control_by_name";
    int			i;
    zbx_ipmi_control_t	*c = NULL;

    // 记录日志，表示函数开始调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() control: %s@[%s]:%d", __function_name, c_name, h->ip, h->port);

    // 遍历IPMI主机中的控制项
    for (i = 0; i < h->control_count; i++)
    {
        // 判断当前控制项的名称是否与传入的名称相同
        if (0 == strcmp(h->controls[i].c_name, c_name))
        {
            // 找到匹配的控制项，将其指针存储在c变量中
            c = &h->controls[i];
            break;
        }
    }

    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)c);

    // 返回找到的控制项指针
    return c;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的主机h和控制项全名full_name，在主机h的控制项数组中查找匹配的全名，找到后返回对应的zbx_ipmi_control_t结构体指针。
 ******************************************************************************/
// 定义一个静态局部指针变量，用于存储指向zbx_ipmi_control_t结构体的指针
static zbx_ipmi_control_t	*zbx_get_ipmi_control_by_full_name(const zbx_ipmi_host_t *h, const char *full_name)
{
	// 定义一个局部字符串指针，用于存储函数名
	const char		*__function_name = "zbx_get_ipmi_control_by_full_name";
	// 定义一个整型变量i，用于循环计数
	int			i;
	// 定义一个指向zbx_ipmi_control_t结构体的指针变量c，初始值为NULL
	zbx_ipmi_control_t	*c = NULL;

	// 使用zabbix_log函数记录调试信息，输出函数名、控制项全名、主机IP和端口
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() control:'%s@[%s]:%d", __function_name, full_name, h->ip, h->port);

	// 遍历主机h中的控制项数组
	for (i = 0; i < h->control_count; i++)
	{
		// 判断当前控制项的全名是否与传入的全名相同
		if (0 == strcmp(h->controls[i].full_name, full_name))
		{
			// 如果相同，则将指针c指向当前控制项
			c = &h->controls[i];
			// 跳出循环
			break;
		}
	}

	// 使用zabbix_log函数记录调试信息，输出函数名和找到的控制项指针
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)c);

	// 返回找到的控制项指针c
	return c;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是分配一个新的zbx_ipmi_control结构体，并将其添加到zbx_ipmi_host结构的controls数组中。在这个过程中，代码完成了以下操作：
 *
 *1. 获取control的id和名称。
 *2. 计算domain偏移量。
 *3. 分配并初始化一个新的zbx_ipmi_control结构体。
 *4. 设置zbx_ipmi_control结构体的成员变量。
 *5. 分配并初始化control的值数组。
 *6. 将新的zbx_ipmi_control结构体添加到zbx_ipmi_host结构的controls数组中。
 *7. 释放不再使用的内存。
 *8. 输出调试日志。
 *
 *最后，函数返回新分配的zbx_ipmi_control结构体指针。
 ******************************************************************************/
// 定义一个静态函数zbx_allocate_ipmi_control，接收两个参数：zbx_ipmi_host_t类型的指针h和ipmi_control_t类型的指针control
static zbx_ipmi_control_t *zbx_allocate_ipmi_control(zbx_ipmi_host_t *h, ipmi_control_t *control)
{
    // 定义一些变量，包括字符串指针、大小度和指针等
    const char		*__function_name = "zbx_allocate_ipmi_control";
    size_t			sz, dm_sz;
    zbx_ipmi_control_t	*c;
    char			*c_name = NULL;
    char			full_name[IPMI_SENSOR_NAME_LEN];

	sz = (size_t)ipmi_control_get_id_length(control);
	c_name = (char *)zbx_malloc(c_name, sz + 1);
	ipmi_control_get_id(control, c_name, sz);

	ipmi_control_get_name(control, full_name, sizeof(full_name));
	dm_sz = get_domain_offset(h, full_name);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Added control: host'%s:%d' id:'%s' domain:'%u' name:'%s'",
			__function_name, h->ip, h->port, c_name, h->domain_nr, full_name + dm_sz);

	h->control_count++;
	sz = (size_t)h->control_count * sizeof(zbx_ipmi_control_t);

	if (NULL == h->controls)
		h->controls = (zbx_ipmi_control_t *)zbx_malloc(h->controls, sz);
	else
		h->controls = (zbx_ipmi_control_t *)zbx_realloc(h->controls, sz);

	c = &h->controls[h->control_count - 1];

	memset(c, 0, sizeof(zbx_ipmi_control_t));

	c->control = control;
	c->c_name = c_name;
	c->num_values = ipmi_control_get_num_vals(control);
	sz = sizeof(int) * (size_t)c->num_values;
	c->val = (int *)zbx_malloc(c->val, sz);
	memset(c->val, 0, sz);
	c->full_name = zbx_strdup(NULL, full_name + dm_sz);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __function_name, (void *)c);

	return c;
}

static void	zbx_delete_ipmi_control(zbx_ipmi_host_t *h, const ipmi_control_t *control)
{
	const char	*__function_name = "zbx_delete_ipmi_control";
	int		i;
	size_t		sz;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() phost:%p pcontrol:%p", __function_name, (void *)h, (const void *)control);

	for (i = 0; i < h->control_count; i++)
	{
		if (h->controls[i].control != control)
			continue;

		sz = sizeof(zbx_ipmi_control_t);

		zabbix_log(LOG_LEVEL_DEBUG, "control '%s@[%s]:%d' deleted", h->controls[i].c_name, h->ip, h->port);

		zbx_free(h->controls[i].c_name);
		zbx_free(h->controls[i].val);
		zbx_free(h->controls[i].full_name);

		h->control_count--;
		if (h->control_count != i)
			memmove(&h->controls[i], &h->controls[i + 1], sz * (size_t)(h->control_count - i));
		h->controls = (zbx_ipmi_control_t *)zbx_realloc(h->controls, sz * (size_t)h->control_count);

		break;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/* callback function invoked from OpenIPMI */
static void	zbx_got_thresh_reading_cb(ipmi_sensor_t *sensor, int err, enum ipmi_value_present_e value_present,
		unsigned int raw_value, double val, ipmi_states_t *states, void *cb_data)
{
	const char		*__function_name = "zbx_got_thresh_reading_cb";
	char			id_str[2 * IPMI_SENSOR_ID_SZ + 1];
	zbx_ipmi_host_t		*h = (zbx_ipmi_host_t *)cb_data;
	zbx_ipmi_sensor_t	*s;

	ZBX_UNUSED(raw_value);

	RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (0 != err)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() fail: %s", __function_name, zbx_strerror(err));

		h->err = zbx_dsprintf(h->err, "error 0x%x while reading threshold sensor", (unsigned int)err);
		h->ret = NOTSUPPORTED;
		goto out;
	}

	if (0 == ipmi_is_sensor_scanning_enabled(states) || 0 != ipmi_is_initial_update_in_progress(states))
	{
		h->err = zbx_strdup(h->err, "sensor data is not available");
		h->ret = NOTSUPPORTED;
		goto out;
	}

	s = zbx_get_ipmi_sensor(h, sensor);

	if (NULL == s)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		h->err = zbx_strdup(h->err, "fatal error");
		h->ret = NOTSUPPORTED;
		goto out;
	}

	switch (value_present)
	{
		case IPMI_NO_VALUES_PRESENT:
		case IPMI_RAW_VALUE_PRESENT:
			h->err = zbx_strdup(h->err, "no value present for threshold sensor");
			h->ret = NOTSUPPORTED;
			break;
		case IPMI_BOTH_VALUES_PRESENT:
			s->value.threshold = val;

			if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
			{
				const char	*percent = "", *base, *mod_use = "", *modifier = "", *rate;
				const char	*e_string, *s_type_string, *s_reading_type_string;

				e_string = ipmi_entity_get_entity_id_string(ipmi_sensor_get_entity(sensor));
				s_type_string = ipmi_sensor_get_sensor_type_string(sensor);
				s_reading_type_string = ipmi_sensor_get_event_reading_type_string(sensor);
				base = ipmi_sensor_get_base_unit_string(sensor);

				if (0 != ipmi_sensor_get_percentage(sensor))
					percent = "%";

				switch (ipmi_sensor_get_modifier_unit_use(sensor))
				{
					case IPMI_MODIFIER_UNIT_NONE:
						break;
					case IPMI_MODIFIER_UNIT_BASE_DIV_MOD:
						mod_use = "/";
						modifier = ipmi_sensor_get_modifier_unit_string(sensor);
						break;
					case IPMI_MODIFIER_UNIT_BASE_MULT_MOD:
						mod_use = "*";
						modifier = ipmi_sensor_get_modifier_unit_string(sensor);
						break;
					default:
						THIS_SHOULD_NEVER_HAPPEN;
				}
				rate = ipmi_sensor_get_rate_unit_string(sensor);

				zabbix_log(LOG_LEVEL_DEBUG, "Value [%s | %s | %s | %s | " ZBX_FS_DBL "%s %s%s%s%s]",
						zbx_sensor_id_to_str(id_str, sizeof(id_str), s->id, s->id_type,
						s->id_sz), e_string, s_type_string, s_reading_type_string, val, percent,
						base, mod_use, modifier, rate);
			}
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}
out:
	h->done = 1;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}

/* callback function invoked from OpenIPMI */
/******************************************************************************
 * 以下是我为您注释好的代码块：
 *
 *
 *
 *这段代码的主要目的是处理IPMI传感器读取离散状态的回调函数。当传感器读取到离散状态时，该函数会被调用。函数首先检查传感器数据是否可用，如果不可用，则返回错误信息。接下来，它获取传感器对应的IPMI传感器结构体，如果结构体为空，也表示发生错误，记录错误信息并返回。如果读取传感器数据时发生错误，同样记录错误信息并返回。
 *
 *在处理离散传感器数据时，函数按照16位离散值的最大数量循环读取传感器的状态。对于每个状态，它检查当前状态是否已设置，如果已设置，将相应位设置为1。最后，函数设置处理完毕标志，并记录日志表示函数执行结束。
 ******************************************************************************/
static void zbx_got_discrete_states_cb(ipmi_sensor_t *sensor, int err, ipmi_states_t *states, void *cb_data)
{
    // 定义一个常量，表示回调函数的名称
    const char *__function_name = "zbx_got_discrete_states_cb";
    // 定义一个字符串数组，用于存储传感器ID
    char id_str[2 * IPMI_SENSOR_ID_SZ + 1];
    // 定义一些变量，用于处理传感器数据
    int id, i, val, ret, is_state_set;
    zbx_ipmi_host_t *h = (zbx_ipmi_host_t *)cb_data;
    zbx_ipmi_sensor_t *s;

    // 检查cb_data是否为空，若为空则直接返回
    RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断传感器是否正在扫描或初始更新是否正在进行，若是则返回错误信息
    if (0 == ipmi_is_sensor_scanning_enabled(states) || 0 != ipmi_is_initial_update_in_progress(states))
    {
        h->err = zbx_strdup(h->err, "sensor data is not available");
        h->ret = NOTSUPPORTED;
        goto out;
    }

    // 获取传感器对应的IPMI传感器结构体
    s = zbx_get_ipmi_sensor(h, sensor);

    // 如果s为空，表示发生致命错误，记录错误信息并返回
    if (NULL == s)
    {
        THIS_SHOULD_NEVER_HAPPEN;
        h->err = zbx_strdup(h->err, "fatal error");
        h->ret = NOTSUPPORTED;
        goto out;
    }

    // 如果err不为0，表示读取离散传感器数据时发生错误，记录错误信息并返回
    if (0 != err)
    {
        h->err = zbx_dsprintf(h->err, "error 0x%x while reading a discrete sensor %s@[%s]:%d",
                            (unsigned int)err,
                            zbx_sensor_id_to_str(id_str, sizeof(id_str), s->id, s->id_type, s->id_sz), h->ip,
                            h->port);
        h->ret = NOTSUPPORTED;
        goto out;
    }

    // 获取传感器对应的实体ID
    id = ipmi_entity_get_entity_id(ipmi_sensor_get_entity(sensor));

	/* Discrete values are 16-bit. We're storing them into a 64-bit uint. */
#define MAX_DISCRETE_STATES	15

	s->value.discrete = 0;
	for (i = 0; i < MAX_DISCRETE_STATES; i++)
	{
		ret = ipmi_sensor_discrete_event_readable(sensor, i, &val);
		if (0 != ret || 0 == val)
			continue;

        is_state_set = ipmi_is_state_set(states, i);

        // 记录日志，表示状态设置情况
        zabbix_log(LOG_LEVEL_DEBUG, "State [%s | %s | %s | %s | state %d value is %d]",
                   zbx_sensor_id_to_str(id_str, sizeof(id_str), s->id, s->id_type, s->id_sz),
                   ipmi_get_entity_id_string(id), ipmi_sensor_get_sensor_type_string(sensor),
                   ipmi_sensor_get_event_reading_type_string(sensor), i, is_state_set);

		if (0 != is_state_set)
			s->value.discrete |= 1 << i;
	}
#undef MAX_DISCRETE_STATES
out:
	h->done = 1;

    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_perform_openipmi_ops                                         *
 *                                                                            *
 * Purpose: Pass control to OpenIPMI library to process events                *
 *                                                                            *
 * Return value: SUCCEED - no errors                                          *
 *               FAIL - an error occurred while processing events             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_perform_openipmi_ops的函数，该函数接收一个zbx_ipmi_host_t类型的指针h和一个字符串指针func_name作为参数。函数内部首先记录调试信息，然后设置一个超时时间，进入一个无限循环。在循环中，调用os_hnd->perform_one_op函数，若返回值为0，表示操作成功，继续循环；若返回值不为0，表示操作失败，记录错误信息并返回FAIL。当循环结束时，表示所有操作完成，记录调试信息并返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数zbx_perform_openipmi_ops，接收两个参数，一个是zbx_ipmi_host_t类型的指针h，另一个是字符串指针func_name
static int	zbx_perform_openipmi_ops(zbx_ipmi_host_t *h, const char *func_name)
{
	// 定义一个常量字符串__function_name，值为"zbx_perform_openipmi_ops"
	const char	*__function_name = "zbx_perform_openipmi_ops";
	// 定义一个结构体timeval类型的变量tv，用于设置超时
	struct timeval	tv;

	// 使用zabbix_log记录调试信息，输出函数名、主机名、端口、调用函数名等信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d' phost:%p from %s()", __function_name, h->ip, h->port,
			(void *)h, func_name);

	// 设置tv的秒数和微秒数，表示一个操作的超时时间
	tv.tv_sec = 10;		/* set timeout for one operation */
	tv.tv_usec = 0;

	// 进入一个无限循环，直到h->done不为0
	while (0 == h->done)
	{
		// 定义一个整型变量res，用于存储os_hnd->perform_one_op的返回值
		int	res;

		// 调用os_hnd->perform_one_op函数，若返回值为0，表示操作成功，继续循环
		if (0 == (res = os_hnd->perform_one_op(os_hnd, &tv)))
			continue;

		// 记录调试信息，输出函数名、调用函数名和错误信息
		zabbix_log(LOG_LEVEL_DEBUG, "End %s() from %s(): error: %s", __function_name, func_name,
				zbx_strerror(res));

		// 如果res不等于0，表示操作失败，返回FAIL
		return FAIL;
	}

	// 循环结束后，记录调试信息，输出函数名和调用函数名
	zabbix_log(LOG_LEVEL_DEBUG, "End %s() from %s()", __function_name, func_name);

	// 操作成功，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_perform_all_openipmi_ops                                     *
 *                                                                            *
 * Purpose: Pass control to OpenIPMI library to process all internal events   *
 *                                                                            *
 * Parameters: timeout - [IN] timeout (in seconds) for processing single      *
 *                            operation; processing multiple operations may   *
 *                            take more time                                  *
 *                                                                            *
 *****************************************************************************/
void	zbx_perform_all_openipmi_ops(int timeout)
{
	/* Before OpenIPMI v2.0.26, perform_one_op() did not modify timeout argument.   */
	/* Starting with OpenIPMI v2.0.26, perform_one_op() updates timeout argument.   */
	/* To make sure that the loop works consistently with all versions of OpenIPMI, */
	/* initialize timeout argument for perform_one_op() inside the loop.            */

	for (;;)
	{
		struct timeval	tv;
		double		start_time;
		int		res;

		tv.tv_sec = timeout;
		tv.tv_usec = 0;

		start_time = zbx_time();

		/* perform_one_op() returns 0 on success, errno on failure (timeout means success) */
		if (0 != (res = os_hnd->perform_one_op(os_hnd, &tv)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "IPMI error: %s", zbx_strerror(res));
			break;
		}

		/* If execution of perform_one_op() took more time than specified in timeout argument, assume that  */
		/* perform_one_op() timed out and break the loop.                                                   */
		/* If it took less than specified in timeout argument, assume that some operation was performed and */
		/* there may be more operations to be performed.                                                    */
		if (zbx_time() - start_time >= timeout)
		{
			break;
		}
	}
}

static void	zbx_read_ipmi_sensor(zbx_ipmi_host_t *h, const zbx_ipmi_sensor_t *s)
{
	/* 定义一个常量字符串，表示函数名称 */
	const char *__function_name = "zbx_read_ipmi_sensor";
	/* 定义一个字符数组，用于存储传感器ID字符串 */
	char id_str[2 * IPMI_SENSOR_ID_SZ + 1];
	/* 定义一个整型变量，用于存储函数返回值 */
	int ret;
	/* 定义一个指向字符串的指针，用于存储传感器读取类型字符串 */
	const char *s_reading_type_string;

	/* 将传感器详细信息复制到id_str数组中，方便后续处理和错误提示 */
	zbx_sensor_id_to_str(id_str, sizeof(id_str), s->id, s->id_type, s->id_sz);

	/* 记录日志，显示传感器的相关信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() sensor:'%s@[%s]:%d'", __function_name, id_str, h->ip, h->port);

	/* 初始化返回值和操作完成标志 */
	h->ret = SUCCEED;
	h->done = 0;

	/* 根据传感器读取类型进行切换处理 */
	switch (s->reading_type)
	{
		/* 处理IPMI_EVENT_READING_TYPE_THRESHOLD类型的传感器 */
		case IPMI_EVENT_READING_TYPE_THRESHOLD:
			if (0 != (ret = ipmi_sensor_get_reading(s->sensor, zbx_got_thresh_reading_cb, h)))
			{
				/* 传感器可能在读取过程中消失，所以不使用指针操作 */
				h->err = zbx_dsprintf(h->err, "Cannot read sensor \"%s\"."
						" ipmi_sensor_get_reading() return error: 0x%x", id_str,
						(unsigned int)ret);
				h->ret = NOTSUPPORTED;
				goto out;
			}
			break;
		/* 处理其他类型的传感器，如IPMI_EVENT_READING_TYPE_DISCRETE_XXX等 */
		case IPMI_EVENT_READING_TYPE_DISCRETE_USAGE:
		case IPMI_EVENT_READING_TYPE_DISCRETE_STATE:
		case IPMI_EVENT_READING_TYPE_DISCRETE_PREDICTIVE_FAILURE:
		case IPMI_EVENT_READING_TYPE_DISCRETE_LIMIT_EXCEEDED:
		case IPMI_EVENT_READING_TYPE_DISCRETE_PERFORMANCE_MET:
		case IPMI_EVENT_READING_TYPE_DISCRETE_SEVERITY:
		case IPMI_EVENT_READING_TYPE_DISCRETE_DEVICE_PRESENCE:
		case IPMI_EVENT_READING_TYPE_DISCRETE_DEVICE_ENABLE:
		case IPMI_EVENT_READING_TYPE_DISCRETE_AVAILABILITY:
		case IPMI_EVENT_READING_TYPE_DISCRETE_REDUNDANCY:
		case IPMI_EVENT_READING_TYPE_DISCRETE_ACPI_POWER:
		case IPMI_EVENT_READING_TYPE_SENSOR_SPECIFIC:
		case 0x70:	/* reading types 70h-7Fh are for OEM discrete sensors */
		case 0x71:
		case 0x72:
		case 0x73:
		case 0x74:
		case 0x75:
		case 0x76:
		case 0x77:
		case 0x78:
		case 0x79:
		case 0x7a:
		case 0x7b:
		case 0x7c:
		case 0x7d:
		case 0x7e:
		case 0x7f:
			if (0 != (ret = ipmi_sensor_get_states(s->sensor, zbx_got_discrete_states_cb, h)))
			{
				/* do not use pointer to sensor here - the sensor may have disappeared during */
				/* ipmi_sensor_get_states(), as domain might be closed due to communication failure */
				h->err = zbx_dsprintf(h->err, "Cannot read sensor \"%s\"."
						" ipmi_sensor_get_states() return error: 0x%x", id_str,
						(unsigned int)ret);
				h->ret = NOTSUPPORTED;
				goto out;
			}
			break;
		default:
			s_reading_type_string = ipmi_sensor_get_event_reading_type_string(s->sensor);

			h->err = zbx_dsprintf(h->err, "Cannot read sensor \"%s\"."
					" IPMI reading type \"%s\" is not supported", id_str, s_reading_type_string);
			h->ret = NOTSUPPORTED;
			goto out;
	}

	zbx_perform_openipmi_ops(h, __function_name);	/* ignore returned result */
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}

/* callback function invoked from OpenIPMI */
static void	zbx_got_control_reading_cb(ipmi_control_t *control, int err, int *val, void *cb_data)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "zbx_got_control_reading_cb";
    // 类型转换，将cb_data指向zbx_ipmi_host_t结构体
    zbx_ipmi_host_t *h = (zbx_ipmi_host_t *)cb_data;
    // 定义一个整数变量n，用于循环计数
    int n;
    // 定义一个zbx_ipmi_control_t类型的指针变量c，用于存储控制信息
    zbx_ipmi_control_t *c;
    // 定义一个字符串指针变量e_string，用于存储实体ID字符串
    const char *e_string;
    // 定义一个size_t类型的变量sz，用于存储内存拷贝的大小
    size_t sz;

    // 检查cb_data是否为空，若为空则返回错误
    RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 如果err不为0，表示出错
    if (0 != err)
    {
        // 记录日志，表示出错信息
        zabbix_log(LOG_LEVEL_DEBUG, "%s() fail: %s", __function_name, zbx_strerror(err));

        // 设置错误信息
        h->err = zbx_dsprintf(h->err, "error 0x%x while reading control", (unsigned int)err);
        // 设置返回状态码为NOTSUPPORTED
        h->ret = NOTSUPPORTED;
        // 跳转到out标签，结束函数执行
        goto out;
    }

    // 从host结构体中获取控制信息
    c = zbx_get_ipmi_control(h, control);

    // 如果c为空，表示发生致命错误
    if (NULL == c)
    {
        THIS_SHOULD_NEVER_HAPPEN;
        // 设置错误信息
        h->err = zbx_strdup(h->err, "fatal error");
        // 设置返回状态码为NOTSUPPORTED
        h->ret = NOTSUPPORTED;
        // 跳转到out标签，结束函数执行
        goto out;
    }

    // 如果控制项num_values为0，表示发生错误
    if (c->num_values == 0)
    {
        THIS_SHOULD_NEVER_HAPPEN;
        // 设置错误信息
        h->err = zbx_strdup(h->err, "no value present for control");
        // 设置返回状态码为NOTSUPPORTED
        h->ret = NOTSUPPORTED;
        // 跳转到out标签，结束函数执行
        goto out;
    }

    // 获取实体ID字符串
    e_string = ipmi_entity_get_entity_id_string(ipmi_control_get_entity(control));

    // 遍历控制项的值
    for (n = 0; n < c->num_values; n++)
    {
        // 记录日志，表示控制项的值
        zabbix_log(LOG_LEVEL_DEBUG, "control values [%s | %s | %d:%d]",
                  c->c_name, e_string, n + 1, val[n]);
    }

    // 计算内存拷贝的大小
    sz = sizeof(int) * (size_t)c->num_values;
    // 拷贝数据到控制项的值数组中
    memcpy(c->val, val, sz);


out:
    // 设置host结构体的done标志为1，表示任务已完成
    h->done = 1;

    // 记录日志，表示函数执行结果



	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}

/* callback function invoked from OpenIPMI */
/******************************************************************************
 * *
 *整个代码块的主要目的是处理IPMI控制设置回调函数。当接收到控制设置信息时，函数会首先检查回调数据是否有效，然后判断错误码是否为0。如果错误码不为0，则会记录日志并设置相应的错误信息、返回码和任务状态。如果获取到的控制信息为空，则会记录日志并设置致命错误、返回码和任务状态。最后，如果控制设置成功，则会记录日志并设置任务状态为已完成。整个函数在执行完毕后，还会记录日志表示函数执行结束及返回结果。
 ******************************************************************************/
static void zbx_got_control_setting_cb(ipmi_control_t *control, int err, void *cb_data)
{
    // 定义一个内部函数，用于处理IPMI控制设置回调
    // 参数：control：IPMI控制设置信息
    //       err：错误码
    //       cb_data：回调数据

    const char *__function_name = "zbx_got_control_setting_cb";
    zbx_ipmi_host_t *h = (zbx_ipmi_host_t *)cb_data;
    zbx_ipmi_control_t *c;

    // 检查回调数据是否有效，若无效则直接返回
    RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断错误码是否为0，若不为0则表示有错误
    if (0 != err)
    {
        // 记录日志，表示函数执行失败及错误信息
        zabbix_log(LOG_LEVEL_DEBUG, "%s() fail: %s", __function_name, zbx_strerror(err));

        // 设置错误信息
        h->err = zbx_dsprintf(h->err, "error 0x%x while set control", (unsigned int)err);
        // 设置返回码为NOTSUPPORTED，表示不支持
        h->ret = NOTSUPPORTED;
        // 设置done为1，表示任务已完成
        h->done = 1;
        // 返回
        return;
    }

	c = zbx_get_ipmi_control(h, control);

	if (NULL == c)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		h->err = zbx_strdup(h->err, "fatal error");
		h->ret = NOTSUPPORTED;
		h->done = 1;
		return;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "set value completed for control %s@[%s]:%d", c->c_name, h->ip, h->port);

	h->done = 1;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}

static void	zbx_read_ipmi_control(zbx_ipmi_host_t *h, const zbx_ipmi_control_t *c)
{
	// 定义一个字符串指针__function_name，用于存储函数名
	const char *__function_name = "zbx_read_ipmi_control";
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;
	// 定义一个字符数组control_name，用于存储控制名称
	char control_name[128];


	zabbix_log(LOG_LEVEL_DEBUG, "In %s() control:'%s@[%s]:%d'", __function_name, c->c_name, h->ip, h->port);

	if (0 == ipmi_control_is_readable(c->control))
	{
		h->err = zbx_strdup(h->err, "control is not readable");
		h->ret = NOTSUPPORTED;
		goto out;
	}

	/* copy control name - it can go away and we won't be able to make an error message */
	zbx_strlcpy(control_name, c->c_name, sizeof(control_name));

	h->ret = SUCCEED;
	h->done = 0;

	if (0 != (ret = ipmi_control_get_val(c->control, zbx_got_control_reading_cb, h)))
	{
		/* do not use pointer to control here - the control may have disappeared during */
		/* ipmi_control_get_val(), as domain might be closed due to communication failure */
		h->err = zbx_dsprintf(h->err, "Cannot read control %s. ipmi_control_get_val() return error: 0x%x",
				control_name, (unsigned int)ret);
		h->ret = NOTSUPPORTED;
		goto out;
	}

	zbx_perform_openipmi_ops(h, __function_name);	/* ignore returned result */
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}

/******************************************************************************
 * *
 *整个代码块的主要目的是设置 IPMI 控制器的值。函数接收三个参数：一个指向 zbx_ipmi_host_t 结构体的指针、一个指向 zbx_ipmi_control_t 结构体的指针和一个整数值。在函数中，首先记录日志，显示控制器的名称、IP 地址、端口和设置的值。然后判断设置的控制器是否可设置，如果不可设置，则返回错误。接下来复制控制器名称，设置控制器的值，并调用 ipmi_control_set_val() 函数设置控制器值。最后执行 OpenIPMI 操作并记录日志。
 ******************************************************************************/
static void zbx_set_ipmi_control(zbx_ipmi_host_t *h, zbx_ipmi_control_t *c, int value)
{
    /* 定义一个函数名，方便调试 */
    const char *__function_name = "zbx_set_ipmi_control";
    int		ret;
    char		control_name[128];	/* 内部定义的 CONTROL_ID_LEN 为 32 in OpenIPMI 2.0.22 */

    /* 记录日志，显示控制器的名称、IP 地址、端口和设置的值 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() control:'%s@[%s]:%d' value:%d",
               __function_name, c->c_name, h->ip, h->port, value);

    /* 如果设置了控制器，但是没有值，则不应该发生这种情况 */
    if (c->num_values == 0)
    {
        THIS_SHOULD_NEVER_HAPPEN;
        h->err = zbx_strdup(h->err, "no value present for control");
        h->ret = NOTSUPPORTED;
        h->done = 1;
        goto out;
    }

    /* 判断该控制器是否可设置 */
    if (0 == ipmi_control_is_settable(c->control))
    {
        h->err = zbx_strdup(h->err, "control is not settable");
        h->ret = NOTSUPPORTED;
        goto out;
    }

    /* 复制控制器名称，以免在错误消息中丢失 */
    zbx_strlcpy(control_name, c->c_name, sizeof(control_name));

    /* 设置控制器的值 */
    c->val[0] = value;
    h->ret = SUCCEED;
    h->done = 0;

    /* 设置控制器值，并回调 zbx_got_control_setting_cb 函数 */
    if (0 != (ret = ipmi_control_set_val(c->control, c->val, zbx_got_control_setting_cb, h)))
    {
        /* 不要在此处使用控制器指针，因为控制器可能在 ipmi_control_set_val() 期间消失 */
        /* 例如，由于通信故障，域可能已关闭 */
        h->err = zbx_dsprintf(h->err, "Cannot set control %s. ipmi_control_set_val() return error: 0x%x",
                            control_name, (unsigned int)ret);
        h->ret = NOTSUPPORTED;
        goto out;
    }

    /* 执行 OpenIPMI 操作 */
    zbx_perform_openipmi_ops(h, __function_name);  /* 忽略返回结果 */

out:
    /* 记录日志，显示函数结束和返回结果 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}


/* callback function invoked from OpenIPMI */
/******************************************************************************
 * *
 *整个代码块的主要目的是处理IPMI传感器的变化事件。当传感器发生添加、删除或变化时，调用此回调函数进行相应的处理。根据传感器的状态和操作类型，执行相应的操作，如分配内存、删除传感器等。同时，通过日志记录函数调用过程和相关信息，以便于调试和监控。
 ******************************************************************************/
// 定义一个回调函数，用于处理IPMI传感器变化事件
static void zbx_sensor_change_cb(enum ipmi_update_e op, ipmi_entity_t *ent, ipmi_sensor_t *sensor, void *cb_data)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "zbx_sensor_change_cb";
    //  cast to zbx_ipmi_host_t 类型的指针，存储主机信息
    zbx_ipmi_host_t *h = (zbx_ipmi_host_t *)cb_data;

    // 检查cb_data是否为空，若为空则直接返回
    RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

    // 记录日志，显示函数调用信息，包括主机名、端口、IPMI实体、传感器和操作类型
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d' phost:%p ent:%p sensor:%p op:%d",
               __function_name, h->ip, h->port, (void *)h, (void *)ent, (void *)sensor, (int)op);

    // 忽略不可读取的传感器（例如仅事件）
    if (0 != ipmi_sensor_get_is_readable(sensor))
    {
        // 根据操作类型进行切换
        switch (op)
        {
            // 当传感器被添加时，如果尚未分配内存，则分配内存并初始化
            case IPMI_ADDED:
                if (NULL == zbx_get_ipmi_sensor(h, sensor))
                    zbx_allocate_ipmi_sensor(h, sensor);
                break;
            // 当传感器被删除时，执行删除操作
            case IPMI_DELETED:
                zbx_delete_ipmi_sensor(h, sensor);
                break;
            // 当传感器发生变化时，执行相关操作（此处未给出具体操作，留空）
            case IPMI_CHANGED:
                break;
            // 默认情况下，不应发生此类操作
            default:
                THIS_SHOULD_NEVER_HAPPEN;
        }
    }

    // 记录日志，显示函数调用结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/* callback function invoked from OpenIPMI */
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 IPMI 操作类型（添加、删除和更改）的回调函数。当 IPMI 实体发生变化时，该函数会被调用。根据操作类型，函数会对 IPMI 控制项进行相应的处理，如分配新控制项、删除控制项等。同时，函数还记录了调试日志，方便开发人员了解函数运行情况。
 ******************************************************************************/
// 定义一个名为 zbx_control_change_cb 的静态函数，该函数接受 5 个参数：
// 1. 枚举类型变量 op，表示 IPMI 操作类型；
// 2. 指向 ipmi_entity_t 类型的指针 ent，表示 IPMI 实体；
// 3. 指向 ipmi_control_t 类型的指针 control，表示 IPMI 控制项；
// 4. 指向 void 类型的指针 cb_data，用于回调函数的数据；
// 5. 空字符串指针，用于函数内部日志记录。
static void zbx_control_change_cb(enum ipmi_update_e op, ipmi_entity_t *ent, ipmi_control_t *control, void *cb_data)
{
	const char	*__function_name = "zbx_control_change_cb";
	zbx_ipmi_host_t	*h = (zbx_ipmi_host_t *)cb_data;

    // 检查 cb_data 是否为空，若为空则返回
    RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d' phost:%p ent:%p control:%p op:%d",
			__function_name, h->ip, h->port, (void *)h, (void *)ent, (void *)control, (int)op);

	switch (op)
	{
		case IPMI_ADDED:
			if (NULL == zbx_get_ipmi_control(h, control))
				zbx_allocate_ipmi_control(h, control);
			break;
		case IPMI_DELETED:
			zbx_delete_ipmi_control(h, control);
			break;
		case IPMI_CHANGED:
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

    // 记录日志：表示 zbx_entity_change_cb 函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/* callback function invoked from OpenIPMI */
static void	zbx_entity_change_cb(enum ipmi_update_e op, ipmi_domain_t *domain, ipmi_entity_t *entity, void *cb_data)
{
	const char	*__function_name = "zbx_entity_change_cb";
	int		ret;
	zbx_ipmi_host_t	*h = (zbx_ipmi_host_t *)cb_data;

	RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		char	entity_name[IPMI_ENTITY_NAME_LEN];

		ipmi_entity_get_name(entity, entity_name, sizeof(entity_name));

		zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d' phost:%p domain:%p entity:%p:'%s' op:%d",
				__function_name, h->ip, h->port, (void *)h, (void *)domain, (void *)entity, entity_name,
				(int)op);
	}

	if (op == IPMI_ADDED)
	{
		if (0 != (ret = ipmi_entity_add_sensor_update_handler(entity, zbx_sensor_change_cb, h)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "ipmi_entity_set_sensor_update_handler() return error: 0x%x",
					(unsigned int)ret);
		}

		if (0 != (ret = ipmi_entity_add_control_update_handler(entity, zbx_control_change_cb, h)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "ipmi_entity_add_control_update_handler() return error: 0x%x",
					(unsigned int)ret);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/* callback function invoked from OpenIPMI */
static void	zbx_domain_closed_cb(void *cb_data)
{
	const char	*__function_name = "zbx_domain_closed_cb";
	zbx_ipmi_host_t	*h = (zbx_ipmi_host_t *)cb_data;

	RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() phost:%p host:'[%s]:%d'", __function_name, (void *)h, h->ip, h->port);

	h->domain_up = 0;
	h->done = 1;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/* callback function invoked from OpenIPMI */
static void	zbx_connection_change_cb(ipmi_domain_t *domain, int err, unsigned int conn_num, unsigned int port_num,
		int still_connected, void *cb_data)
{
	/* this function is called when a connection comes up or goes down */

	const char	*__function_name = "zbx_connection_change_cb";
	int		ret;
	zbx_ipmi_host_t	*h = (zbx_ipmi_host_t *)cb_data;

	RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d' phost:%p domain:%p err:%d conn_num:%u port_num:%u"
			" still_connected:%d cb_data:%p", __function_name, h->ip, h->port, (void *)h, (void *)domain,
			err, conn_num, port_num, still_connected, cb_data);

	if (0 != err)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() fail: %s", __function_name, zbx_strerror(err));

		h->err = zbx_dsprintf(h->err, "cannot connect to IPMI host: %s", zbx_strerror(err));
		h->ret = NETWORK_ERROR;

		if (0 != (ret = ipmi_domain_close(domain, zbx_domain_closed_cb, h)))
			zabbix_log(LOG_LEVEL_DEBUG, "cannot close IPMI domain: [0x%x]", (unsigned int)ret);

		goto out;
	}

	if (0 != (ret = ipmi_domain_add_entity_update_handler(domain, zbx_entity_change_cb, h)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "ipmi_domain_add_entity_update_handler() return error: [0x%x]",
				(unsigned int)ret);
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(h->ret));
}

/* callback function invoked from OpenIPMI */
static void	zbx_domain_up_cb(ipmi_domain_t *domain, void *cb_data)
{
	const char	*__function_name = "zbx_domain_up_cb";
	zbx_ipmi_host_t	*h = (zbx_ipmi_host_t *)cb_data;

	RETURN_IF_CB_DATA_NULL(cb_data, __function_name);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d' domain:%p cb_data:%p", __function_name, h->ip,
			h->port, (void *)domain, cb_data);

	h->domain_up = 1;
	h->done = 1;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

static void	zbx_vlog(os_handler_t *handler, const char *format, enum ipmi_log_type_e log_type, va_list ap)
{
	// 定义两个字符数组 type 和 str，分别用于存储日志类型和日志内容。
	char type[8], str[MAX_STRING_LEN];

	// 声明 unused 变量，但未使用。
	ZBX_UNUSED(handler);

	// 使用 switch 语句根据 log_type 的值分别设置 type 数组的内容，
	// 这里使用了字符串拷贝函数 zbx_strlcpy 简化字符串操作。
	switch (log_type)
	{
		case IPMI_LOG_INFO: zbx_strlcpy(type, "INFO: ", sizeof(type)); break;
		case IPMI_LOG_WARNING: zbx_strlcpy(type, "WARN: ", sizeof(type)); break;
		case IPMI_LOG_SEVERE: zbx_strlcpy(type, "SEVR: ", sizeof(type)); break;
		case IPMI_LOG_FATAL: zbx_strlcpy(type, "FATL: ", sizeof(type)); break;
		case IPMI_LOG_ERR_INFO: zbx_strlcpy(type, "EINF: ", sizeof(type)); break;
		case IPMI_LOG_DEBUG_START:
		case IPMI_LOG_DEBUG: zbx_strlcpy(type, "DEBG: ", sizeof(type)); break;
		case IPMI_LOG_DEBUG_CONT:
		case IPMI_LOG_DEBUG_END: *type = '\0'; break;
		default: THIS_SHOULD_NEVER_HAPPEN;
	}

	// 使用 vsnprintf 函数根据 format 参数和 ap 参数格式化字符串 str，
	// 并将结果存储在 str 数组中。注意，这里使用了 sizeof(str) 作为缓冲区大小。
	zbx_vsnprintf(str, sizeof(str), format, ap);

	// 使用 zabbix_log 函数输出日志，其中 LOG_LEVEL_DEBUG 表示日志级别，
	// %s 表示要插入 type 数组中的字符串，%s 表示要插入 str 数组中的字符串。
	zabbix_log(LOG_LEVEL_DEBUG, "%s%s", type, str);
}

int	zbx_init_ipmi_handler(void)
{
	const char	*__function_name = "zbx_init_ipmi_handler";

	int		res, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (NULL == (os_hnd = ipmi_posix_setup_os_handler()))
	{
		zabbix_log(LOG_LEVEL_WARNING, "unable to allocate IPMI handler");
		goto out;
	}

	os_hnd->set_log_handler(os_hnd, zbx_vlog);

	if (0 != (res = ipmi_init(os_hnd)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "unable to initialize the OpenIPMI library."
				" ipmi_init() return error: 0x%x", (unsigned int)res);
		goto out;
	}

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

static void	zbx_free_ipmi_host(zbx_ipmi_host_t *h)
{
	const char	*__function_name = "zbx_free_ipmi_host";
	int		i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d' h:%p", __function_name, h->ip, h->port, (void *)h);

	for (i = 0; i < h->control_count; i++)
	{
		zbx_free(h->controls[i].c_name);
		zbx_free(h->controls[i].val);
	}

	zbx_free(h->sensors);
	zbx_free(h->controls);
	zbx_free(h->ip);
	zbx_free(h->username);
	zbx_free(h->password);
	zbx_free(h->err);

    // 释放h指向的内存
    zbx_free(h);

    // 使用zabbix_log记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：释放与 IPMI 相关的资源，包括 hosts 链表中的zbx_ipmi_host_t结构体以及 os_hnd 指向的 os_handler 所占用的资源。
 ******************************************************************************/
// 定义一个名为 zbx_free_ipmi_handler 的函数，该函数为 void 类型，即无返回值。
void	zbx_free_ipmi_handler(void)
{
	// 定义一个常量字符串指针 __function_name，用于存储函数名
	const char	*__function_name = "zbx_free_ipmi_handler";

	// 使用 zabbix_log 函数记录调试日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 使用 while 循环遍历 hosts 链表（ hosts 指针不为 NULL 时）
	while (NULL != hosts)
	{
		// 定义一个名为 h 的指针，用于指向 zbx_ipmi_host_t 类型的结构体
		zbx_ipmi_host_t	*h;

		// 将 h 指针初始化为 hosts（即第一个节点）
		h = hosts;
		// 移动 hosts 指针，指向下一个节点
		hosts = hosts->next;

		// 调用 zbx_free_ipmi_host 函数，释放 h 指向的结构体所占用的资源
		zbx_free_ipmi_host(h);
	}

	// 调用 os_hnd 指向的 os_handler 的 free_os_handler 方法，释放相关资源
	os_hnd->free_os_handler(os_hnd);

	// 使用 zabbix_log 函数记录调试日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


static zbx_ipmi_host_t	*zbx_init_ipmi_host(const char *ip, int port, int authtype, int privilege, const char *username,
		const char *password)
{
	const char		*__function_name = "zbx_init_ipmi_host";
	zbx_ipmi_host_t		*h;
	ipmi_open_option_t	options[4];

	/* Although we use only one address and port we pass them in 2-element arrays. The reason is */
	/* OpenIPMI v.2.0.16 - 2.0.24 file lib/ipmi_lan.c, function ipmi_lanp_setup_con() ending with loop */
	/* in OpenIPMI file lib/ipmi_lan.c, function ipmi_lanp_setup_con() ending with */
	/*    for (i=0; i<MAX_IP_ADDR; i++) {           */
	/*        if (!ports[i])                        */
	/*            ports[i] = IPMI_LAN_STD_PORT_STR; */
	/*    }                                         */
	/* MAX_IP_ADDR is '#define MAX_IP_ADDR 2' in OpenIPMI and not available to library users. */
	/* The loop is running two times regardless of number of addresses supplied by the caller, so we use */
	/* 2-element arrays to match OpenIPMI internals. */
	char			*addrs[2] = {NULL}, *ports[2] = {NULL};

	char			domain_name[11];	/* max int length */
	int			ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'[%s]:%d'", __function_name, ip, port);

	/* Host already in the list? */

	if (NULL != (h = zbx_get_ipmi_host(ip, port, authtype, privilege, username, password)))
	{
		if (1 == h->domain_up)
			goto out;
	}
	else
		h = zbx_allocate_ipmi_host(ip, port, authtype, privilege, username, password);

	h->ret = SUCCEED;
	h->done = 0;

	addrs[0] = strdup(h->ip);
	ports[0] = zbx_dsprintf(NULL, "%d", h->port);

	if (0 != (ret = ipmi_ip_setup_con(addrs, ports, 1,
			h->authtype == -1 ? (unsigned int)IPMI_AUTHTYPE_DEFAULT : (unsigned int)h->authtype,
			(unsigned int)h->privilege, h->username, strlen(h->username),
			h->password, strlen(h->password), os_hnd, NULL, &h->con)))
	{
		h->err = zbx_dsprintf(h->err, "Cannot connect to IPMI host [%s]:%d."
				" ipmi_ip_setup_con() returned error 0x%x",
				h->ip, h->port, (unsigned int)ret);
		h->ret = NETWORK_ERROR;
		goto out;
	}

	if (0 != (ret = h->con->start_con(h->con)))
	{
		h->err = zbx_dsprintf(h->err, "Cannot connect to IPMI host [%s]:%d."
				" start_con() returned error 0x%x",
				h->ip, h->port, (unsigned int)ret);
		h->ret = NETWORK_ERROR;
		goto out;
	}

	options[0].option = IPMI_OPEN_OPTION_ALL;
	options[0].ival = 0;
	options[1].option = IPMI_OPEN_OPTION_SDRS;		/* scan SDRs */
	options[1].ival = 1;
	options[2].option = IPMI_OPEN_OPTION_IPMB_SCAN;		/* scan IPMB bus to find out as much as possible */
	options[2].ival = 1;
	options[3].option = IPMI_OPEN_OPTION_LOCAL_ONLY;	/* scan only local resources */
	options[3].ival = 1;

	zbx_snprintf(domain_name, sizeof(domain_name), "%u", h->domain_nr);

	if (0 != (ret = ipmi_open_domain(domain_name, &h->con, 1, zbx_connection_change_cb, h, zbx_domain_up_cb, h,
			options, ARRSIZE(options), NULL)))
	{
		h->err = zbx_dsprintf(h->err, "Cannot connect to IPMI host [%s]:%d. ipmi_open_domain() failed: %s",
				h->ip, h->port, zbx_strerror(ret));
		h->ret = NETWORK_ERROR;
		goto out;
	}

	zbx_perform_openipmi_ops(h, __function_name);	/* ignore returned result */
out:
	zbx_free(addrs[0]);
	zbx_free(ports[0]);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p domain_nr:%u", __function_name, (void *)h, h->domain_nr);

	return h;
}

static ipmi_domain_id_t	domain_id;		/* global variable for passing OpenIPMI domain ID between callbacks */
static int		domain_id_found;	/* A flag to indicate whether the 'domain_id' carries a valid value. */
						/* Values: 0 - not found, 1 - found. The flag is used because we */
						/* cannot set 'domain_id' to NULL. */
static int		domain_close_ok;

/* callback function invoked from OpenIPMI */
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个回调函数，用于在IPMI（智能平台管理接口）系统中通过域名获取对应的域名ID。当域名与预搜索的域名匹配时，将域名ID存储到全局变量中。
 ******************************************************************************/
/* 定义一个回调函数，用于通过域名获取域名ID */
static void zbx_get_domain_id_by_name_cb(ipmi_domain_t *domain, void *cb_data)
{
	char	name[IPMI_DOMAIN_NAME_LEN], *domain_name = (char *)cb_data;

	/* 检查cb_data是否为空，若为空则返回错误 */
	RETURN_IF_CB_DATA_NULL(cb_data, "zbx_get_domain_id_by_name_cb");

	/* 从'domain'指针中获取域名 */
	ipmi_domain_get_name(domain, name, sizeof(name));

	/* if the domain name matches the name we are searching for then store the domain ID into global variable */
	if (0 == strcmp(domain_name, name))
	{
		domain_id = ipmi_domain_convert_to_id(domain);
		domain_id_found = 1;
	}
}

/* callback function invoked from OpenIPMI */
static void	zbx_domain_close_cb(ipmi_domain_t *domain, void *cb_data)
{
    // 定义一个zbx_ipmi_host_t类型的指针h，将其指向cb_data
    zbx_ipmi_host_t *h = (zbx_ipmi_host_t *)cb_data;
    // 定义一个整型变量ret，用于存储函数返回值
    int ret;

	RETURN_IF_CB_DATA_NULL(cb_data, "zbx_domain_close_cb");

	if (0 != (ret = ipmi_domain_close(domain, zbx_domain_closed_cb, h)))
		zabbix_log(LOG_LEVEL_DEBUG, "cannot close IPMI domain: [0x%x]", (unsigned int)ret);
	else
		domain_close_ok = 1;
}

static int	zbx_close_inactive_host(zbx_ipmi_host_t *h)
{
	const char	*__function_name = "zbx_close_inactive_host";

	/* 定义一个字符串变量 __function_name 用于存储函数名，方便后续调试输出 */
	char		domain_name[11];	/* 存储域名长度 */
	int		ret = FAIL;

	/* 打印日志，记录函数调用和主机IP */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s(): %s", __function_name, h->ip);

	/* 格式化域名字符串，将其转换为整数形式 */
	zbx_snprintf(domain_name, sizeof(domain_name), "%u", h->domain_nr);

	/* 遍历OpenIPMI库中的域名列表，查找要关闭的域名 */
	domain_id_found = 0;
	ipmi_domain_iterate_domains(zbx_get_domain_id_by_name_cb, domain_name);

	/* 设置标志位，表示域名操作完成 */
	h->done = 0;
	domain_close_ok = 0;

	/* 如果找到了要关闭的域名，进行关闭操作 */
	if (1 == domain_id_found)
	{
		int	res;

		/* 调用ipmi_domain_pointer_cb关闭域名，若失败则打印错误日志 */
		if (0 != (res = ipmi_domain_pointer_cb(domain_id, zbx_domain_close_cb, h)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s(): ipmi_domain_pointer_cb() return error: %s", __function_name,
					zbx_strerror(res));
			goto out;
		}

		/* 检查域名关闭操作是否成功，若失败则退出 */
		if (1 != domain_close_ok || SUCCEED != zbx_perform_openipmi_ops(h, __function_name))
			goto out;
	}

	/* 域名成功关闭或未找到 */
	zbx_free_ipmi_host(h);
	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *代码主要目的是删除链表中长时间未活动的IPMI主机。整个代码块的功能如下：
 *
 *1. 定义函数名常量和日志级别。
 *2. 初始化指向主机链表的指针、前驱指针和后继指针。
 *3. 记录函数开始执行的日志。
 *4. 使用循环遍历主机链表，判断每个主机最后一次访问时间与当前时间之间的差值是否大于规定的阈值。
 *5. 如果大于阈值，则尝试关闭该非活动主机，并更新链表指针。
 *6. 记录函数执行完毕的日志。
 ******************************************************************************/
void	zbx_delete_inactive_ipmi_hosts(time_t last_check)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "zbx_delete_inactive_ipmi_hosts";

	// 定义一个指向主机链表的指针，以及前驱指针和后继指针
	zbx_ipmi_host_t	*h = hosts, *prev = NULL, *next;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 使用循环遍历主机链表
	while (NULL != h)
	{
		// 判断当前主机最后一次访问时间与当前时间之间的差值是否大于规定的阈值
		if (last_check - h->lastaccess > INACTIVE_HOST_LIMIT)
		{
			// 保存后继指针
			next = h->next;

			// 调用函数关闭非活动主机，并判断是否成功
			if (SUCCEED == zbx_close_inactive_host(h))
			{
				// 如果前驱指针为空，则将后继指针指向下一个主机
				if (NULL == prev)
					hosts = next;
				// 如果前驱指针存在，则更新前驱指针的下一个指针
				else
					prev->next = next;

				// 更新当前主机指针
				h = next;

				// 继续遍历链表
				continue;
			}
		}

		// 更新前驱指针
		prev = h;
		// 更新当前主机指针
		h = h->next;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: has_name_prefix                                                  *
 *                                                                            *
 * Purpose: Check if a string starts with one of predefined prefixes and      *
 *          set prefix length                                                 *
 *                                                                            *
 * Parameters: str        - [IN] string to examine                            *
 *             prefix_len - [OUT] length of the prefix                        *
 *                                                                            *
 * Return value: 1 - the string starts with the name prefix,                  *
 *               0 - otherwise (no prefix or other prefix was found)          *
 *                                                                            *
 ******************************************************************************/
static int	has_name_prefix(const char *str, size_t *prefix_len)
{
#define ZBX_ID_PREFIX	"id:"
#define ZBX_NAME_PREFIX	"name:"

	const size_t	id_len = sizeof(ZBX_ID_PREFIX) - 1, name_len = sizeof(ZBX_NAME_PREFIX) - 1;

	if (0 == strncmp(str, ZBX_NAME_PREFIX, name_len))
	{
		*prefix_len = name_len;
		return 1;
	}


	if (0 == strncmp(str, ZBX_ID_PREFIX, id_len))
		*prefix_len = id_len;
	else
		*prefix_len = 0;

	return 0;

#undef ZBX_ID_PREFIX
#undef ZBX_NAME_PREFIX
}


int	get_value_ipmi(zbx_uint64_t itemid, const char *addr, unsigned short port, signed char authtype,
		unsigned char privilege, const char *username, const char *password, const char *sensor, char **value)
{
	const char		*__function_name = "get_value_ipmi";
	zbx_ipmi_host_t		*h;
	zbx_ipmi_sensor_t	*s;
	zbx_ipmi_control_t	*c = NULL;
	size_t			offset;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" ZBX_FS_UI64, __function_name, itemid);

	if (NULL == os_hnd)
	{
		*value = zbx_strdup(*value, "IPMI handler is not initialised.");
		return CONFIG_ERROR;
	}

	h = zbx_init_ipmi_host(addr, port, authtype, privilege, username, password);

	h->lastaccess = time(NULL);

	if (0 == h->domain_up)
	{
		if (NULL != h->err)
			*value = zbx_strdup(*value, h->err);

		return h->ret;
	}

	if (0 == has_name_prefix(sensor, &offset))
	{
		if (NULL == (s = zbx_get_ipmi_sensor_by_id(h, sensor + offset)))
			c = zbx_get_ipmi_control_by_name(h, sensor + offset);
	}
	else
	{
		if (NULL == (s = zbx_get_ipmi_sensor_by_full_name(h, sensor + offset)))
			c = zbx_get_ipmi_control_by_full_name(h, sensor + offset);
	}

	if (NULL == s && NULL == c)
	{
		*value = zbx_dsprintf(*value, "sensor or control %s@[%s]:%d does not exist", sensor, h->ip, h->port);
		return NOTSUPPORTED;
	}

	if (NULL != s)
		zbx_read_ipmi_sensor(h, s);
	else
		zbx_read_ipmi_control(h, c);

	if (h->ret != SUCCEED)
	{
		if (NULL != h->err)
			*value = zbx_strdup(*value, h->err);

		return h->ret;
	}

	if (NULL != s)
	{
		if (IPMI_EVENT_READING_TYPE_THRESHOLD == s->reading_type)
			*value = zbx_dsprintf(*value, ZBX_FS_DBL, s->value.threshold);
		else
			*value = zbx_dsprintf(*value, ZBX_FS_UI64, s->value.discrete);
	}

	if (NULL != c)
		*value = zbx_dsprintf(*value, "%d", c->val[0]);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s value:%s", __function_name, zbx_result_string(h->ret),
			ZBX_NULL2EMPTY_STR(*value));

	return h->ret;
}

/* function 'zbx_parse_ipmi_command' requires 'c_name' with size 'ITEM_IPMI_SENSOR_LEN_MAX' */
int	zbx_parse_ipmi_command(const char *command, char *c_name, int *val, char *error, size_t max_error_len)
{
	const char	*__function_name = "zbx_parse_ipmi_command";

// 定义变量
const char *p;
size_t		sz_c_name;
int		ret = FAIL;

// 打印调试日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s() command:'%s'", __function_name, command);

	while ('\0' != *command && NULL != strchr(" \t", *command))
		command++;

	for (p = command; '\0' != *p && NULL == strchr(" \t", *p); p++)
		;

// 检查是否找到了有效字符
if (0 == (sz_c_name = p - command))
{
	// 错误处理：命令为空
	zbx_strlcpy(error, "IPMI command is empty", max_error_len);
	goto fail;
}

	if (ITEM_IPMI_SENSOR_LEN_MAX <= sz_c_name)
	{
		zbx_snprintf(error, max_error_len, "IPMI command is too long [%.*s]", (int)sz_c_name, command);
		goto fail;
	}

	memcpy(c_name, command, sz_c_name);
	c_name[sz_c_name] = '\0';

	while ('\0' != *p && NULL != strchr(" \t", *p))
		p++;

	if ('\0' == *p || 0 == strcasecmp(p, "on"))
		*val = 1;
	else if (0 == strcasecmp(p, "off"))
		*val = 0;
	else if (SUCCEED != is_uint31(p, val))
	{
		zbx_snprintf(error, max_error_len, "IPMI command value is not supported [%s]", p);
		goto fail;
	}

	ret = SUCCEED;
fail:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

int	zbx_set_ipmi_control_value(zbx_uint64_t hostid, const char *addr, unsigned short port, signed char authtype,
		unsigned char privilege, const char *username, const char *password, const char *sensor,
		int value, char **error)
{
	const char		*__function_name = "zbx_set_ipmi_control_value";
	zbx_ipmi_host_t		*h;
	zbx_ipmi_control_t	*c;
	size_t			offset;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() hostid:" ZBX_FS_UI64 "control:%s value:%d",
			__function_name, hostid, sensor, value);

	if (NULL == os_hnd)
	{
		*error = zbx_strdup(*error, "IPMI handler is not initialized.");
		zabbix_log(LOG_LEVEL_DEBUG, "%s", *error);
		return NOTSUPPORTED;
	}

	h = zbx_init_ipmi_host(addr, port, authtype, privilege, username, password);

	if (0 == h->domain_up)
	{
		if (NULL != h->err)
		{
			*error = zbx_strdup(*error, h->err);
			zabbix_log(LOG_LEVEL_DEBUG, "%s", h->err);
		}
		return h->ret;
	}

	if (0 == has_name_prefix(sensor, &offset))
		c = zbx_get_ipmi_control_by_name(h, sensor + offset);
	else
		c = zbx_get_ipmi_control_by_full_name(h, sensor + offset);

	if (NULL == c)
	{
		*error = zbx_dsprintf(*error, "Control \"%s\" at address \"%s:%d\" does not exist.", sensor, h->ip, h->port);
		zabbix_log(LOG_LEVEL_DEBUG, "%s", *error);
		return NOTSUPPORTED;
	}

	zbx_set_ipmi_control(h, c, value);

	if (h->ret != SUCCEED)
	{
		if (NULL != h->err)
		{
			*error = zbx_strdup(*error, h->err);
			zabbix_log(LOG_LEVEL_DEBUG, "%s", h->err);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);\

	return h->ret;
}

#endif	/* HAVE_OPENIPMI */
