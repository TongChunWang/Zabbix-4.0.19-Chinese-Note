#include "common.h"

#ifdef HAVE_IPCSERVICE

#ifdef HAVE_LIBEVENT
#	include <event.h>
#endif

#include "zbxtypes.h"
#include "zbxalgo.h"
#include "log.h"
#include "zbxipcservice.h"

#define ZBX_IPC_PATH_MAX	sizeof(((struct sockaddr_un *)0)->sun_path)

#define ZBX_IPC_DATA_DUMP_SIZE		128

static char	ipc_path[ZBX_IPC_PATH_MAX] = {0};
static size_t	ipc_path_root_len = 0;

#define ZBX_IPC_CLIENT_STATE_NONE	0
#define ZBX_IPC_CLIENT_STATE_QUEUED	1

#define ZBX_IPC_ASYNC_SOCKET_STATE_NONE		0
#define ZBX_IPC_ASYNC_SOCKET_STATE_TIMEOUT	1
#define ZBX_IPC_ASYNC_SOCKET_STATE_ERROR	2

extern unsigned char	program_type;

/* IPC client, providing nonblocking connections through socket */
struct zbx_ipc_client
{
	zbx_ipc_socket_t	csocket;
	zbx_ipc_service_t	*service;

	zbx_uint32_t		rx_header[2];
	unsigned char		*rx_data;
	zbx_uint32_t		rx_bytes;
	zbx_queue_ptr_t		rx_queue;
	struct event		*rx_event;

	zbx_uint32_t		tx_header[2];
	unsigned char		*tx_data;
	zbx_uint32_t		tx_bytes;
	zbx_queue_ptr_t		tx_queue;
	struct event		*tx_event;

	zbx_uint64_t		id;
	unsigned char		state;

	zbx_uint32_t		refcount;
};

/*
 * Private API
 */

#define ZBX_IPC_HEADER_SIZE	(int)(sizeof(zbx_uint32_t) * 2)

#define ZBX_IPC_MESSAGE_CODE	0
#define ZBX_IPC_MESSAGE_SIZE	1

#if !defined(LIBEVENT_VERSION_NUMBER) || LIBEVENT_VERSION_NUMBER < 0x2000000
typedef int evutil_socket_t;

static struct event	*event_new(struct event_base *ev, evutil_socket_t fd, short what,
/******************************************************************************
 * *
 *整个代码块的主要目的是分配一个事件结构体，并将其注册到事件基类中。其中，事件结构体包含文件描述符、事件类型、回调函数和回调参数。通过 `event_set()` 函数设置事件的基本信息，然后使用 `event_base_set()` 函数将事件注册到事件基类中。最后，返回事件结构体的指针。
 ******************************************************************************/
/*
 * 定义一个函数，用于注册事件回调函数和参数。
 * 主要目的是：为事件处理器分配一个回调函数和相应的参数，并将它们注册到事件基类中。
 * 输出：返回一个指向分配的事件结构体的指针。
 */
void(*cb_func)(int, short, void *), void *cb_arg)
{
	// 定义一个结构体指针，用于存储事件信息
	struct event	*event;

	// 分配一个事件结构体的内存空间
	event = zbx_malloc(NULL, sizeof(struct event));

	// 设置事件结构体的基本信息，包括文件描述符、事件类型、回调函数和回调参数
	event_set(event, fd, what, cb_func, cb_arg);

	// 设置事件基类，将分配的事件结构体注册到事件基类中
	event_base_set(ev, event);

	// 返回分配的事件结构体的指针
	return event;
}


/******************************************************************************
 * *
 *这块代码的主要目的是释放事件结构体所占用的内存。函数接收一个 struct event 类型的指针作为参数，通过调用 event_del 函数删除与该事件相关的信息，然后使用 zbx_free 函数释放事件结构体占用的内存。这是一个典型的资源管理函数，确保在程序运行过程中正确处理资源分配与释放。
 ******************************************************************************/
static void	event_free(struct event *event) // 定义一个名为 event_free 的静态函数，传入一个 struct event 类型的指针
{
	// 调用 event_del(event) 函数，用于删除事件
	// 调用 zbx_free(event) 函数，用于释放事件结构体内存
/******************************************************************************
 * *
 *整个代码块的主要目的是定义两个回调函数（ipc_client_read_event_cb 和 ipc_client_write_event_cb）以及一个获取 IPC 通信路径的函数（ipc_get_path）。
 *
 *注释详细说明：
 *
 *1. 定义两个静态函数 ipc_client_read_event_cb 和 ipc_client_write_event_cb，分别为读事件和写事件的回调函数。在处理 IPC 通信时，这两个函数会被调用。
 *
 *2. 定义一个静态常量字符串 ipc_path，用于存储 IPC 通信的路径。通过 ipc_get_path 函数获取该路径。
 *
 *3. 在 ipc_path 字符串的末尾添加一个空字符，使其成为一个合法的字符串。
 *
 *4. 返回 ipc_path 字符串，用于获取 IPC 通信的路径。
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是根据给定的服务名（service_name）和程序类型（program_type）生成一个IPC通道路径。代码首先计算服务名的长度，然后根据程序类型选择不同的前缀。接下来，检查生成的IPC路径是否超过最大长度，如果超过，则返回错误信息。最后，按照一定的规则拼接IPC路径，并返回生成后的IPC路径。
 ******************************************************************************/
// 定义一个静态常量指针，用于存储IPC通道路径
static const char *ipc_make_path(const char *service_name, char **error)
{
	// 定义一些变量
	const char *prefix;          // 用于存储前缀字符串
	size_t path_len, offset, prefix_len; // 用于计算路径长度和偏移量

	// 计算service_name字符串的长度
	path_len = strlen(service_name);

	// 根据程序类型选择不同的前缀
	switch (program_type)
	{
		case ZBX_PROGRAM_TYPE_SERVER:
			prefix = ZBX_IPC_CLASS_PREFIX_SERVER;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_SERVER);
			break;
		case ZBX_PROGRAM_TYPE_PROXY_ACTIVE:
		case ZBX_PROGRAM_TYPE_PROXY_PASSIVE:
			prefix = ZBX_IPC_CLASS_PREFIX_PROXY;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_PROXY);
			break;
		case ZBX_PROGRAM_TYPE_AGENTD:
			prefix = ZBX_IPC_CLASS_PREFIX_AGENT;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_AGENT);
			break;
		default:
			prefix = ZBX_IPC_CLASS_PREFIX_NONE;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_NONE);
			break;
	}

	// 检查生成的IPC路径是否超过最大长度
	if (ZBX_IPC_PATH_MAX < ipc_path_root_len + path_len + 1 + ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_PREFIX) +
			ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_SUFFIX) + prefix_len)
	{
		// 如果超过最大长度，返回错误信息
		*error = zbx_dsprintf(*error,
				"Socket path \"%s%s%s%s%s\" exceeds maximum length of unix domain socket path.",
				ipc_path, ZBX_IPC_SOCKET_PREFIX, prefix, service_name, ZBX_IPC_SOCKET_SUFFIX);
		return NULL;
	}

	// 计算偏移量
	offset = ipc_path_root_len;

	// 拼接IPC路径
	memcpy(ipc_path + offset , ZBX_IPC_SOCKET_PREFIX, ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_PREFIX));
	offset += ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_PREFIX);
	memcpy(ipc_path + offset, prefix, prefix_len);
	offset += prefix_len;
	memcpy(ipc_path + offset, service_name, path_len);
	offset += path_len;
	memcpy(ipc_path + offset, ZBX_IPC_SOCKET_SUFFIX, ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_SUFFIX) + 1);

	// 返回生成的IPC路径
	return ipc_path;
}


	switch (program_type)
	{
		case ZBX_PROGRAM_TYPE_SERVER:
			prefix = ZBX_IPC_CLASS_PREFIX_SERVER;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_SERVER);
			break;
		case ZBX_PROGRAM_TYPE_PROXY_ACTIVE:
		case ZBX_PROGRAM_TYPE_PROXY_PASSIVE:
			prefix = ZBX_IPC_CLASS_PREFIX_PROXY;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_PROXY);
			break;
		case ZBX_PROGRAM_TYPE_AGENTD:
			prefix = ZBX_IPC_CLASS_PREFIX_AGENT;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_AGENT);
			break;
		default:
			prefix = ZBX_IPC_CLASS_PREFIX_NONE;
			prefix_len = ZBX_CONST_STRLEN(ZBX_IPC_CLASS_PREFIX_NONE);
			break;
	}

	if (ZBX_IPC_PATH_MAX < ipc_path_root_len + path_len + 1 + ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_PREFIX) +
			ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_SUFFIX) + prefix_len)
	{
		*error = zbx_dsprintf(*error,
				"Socket path \"%s%s%s%s%s\" exceeds maximum length of unix domain socket path.",
				ipc_path, ZBX_IPC_SOCKET_PREFIX, prefix, service_name, ZBX_IPC_SOCKET_SUFFIX);
		return NULL;
	}

	offset = ipc_path_root_len;
	memcpy(ipc_path + offset , ZBX_IPC_SOCKET_PREFIX, ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_PREFIX));
	offset += ZBX_CONST_STRLEN(ZBX_IPC_SOCKET_PREFIX);
	memcpy(ipc_path + offset, prefix, prefix_len);
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数 ipc_write_data，接收 4 个参数：
 * 参数 1：文件描述符（fd）
 * 参数 2：要写入的数据的起始地址（data）
 * 参数 3：数据长度（size）
 * 参数 4：发送数据长度（size_sent），函数结束后将会返回实际发送的数据长度
 * 
 * 函数主要目的是将数据写入到文件描述符 fd 对应的文件中，直到写入全部数据或遇到错误。
 * 
 * 注释如下：
 */
static int	ipc_write_data(int fd, const unsigned char *data, zbx_uint32_t size, zbx_uint32_t *size_sent)
{
	/* 定义一个局部变量 offset，用于记录已经写入的数据偏移量 */
	zbx_uint32_t	offset = 0;
	/* 定义一个局部变量 n，用于记录写入的数据长度 */
	int		n, ret = SUCCEED;

	/* 使用 while 循环，当 offset 不等于 size 时，继续执行以下操作：
	 * 循环内部主要目的是分多次写入数据，每次写入一部分数据，直到全部写入或遇到错误
	 */
	while (offset != size)
	{
		/* 调用 write 函数，将 data 加上 offset 对应的部分数据写入到文件描述符 fd 对应的文件中
		 * 参数 size - offset 表示要写入的数据长度
		 */
		n = write(fd, data + offset, size - offset);

		/* 判断 n 的值是否为 -1，如果是，说明发生错误
		 * 错误号分别为 EINTR、EWOULDBLOCK 和 EAGAIN 时，采取不同的处理方式：
		 * 1. EINTR：表示系统中断，继续执行循环
		 * 2. EWOULDBLOCK 或 EAGAIN：表示暂时无法写入数据，跳出循环
		 * 3. 其他错误：记录日志，返回错误状态码 FAIL，跳出循环
		 */
		if (-1 == n)
		{
			if (EINTR == errno)
				continue;

			if (EWOULDBLOCK == errno || EAGAIN == errno)
				break;

			zabbix_log(LOG_LEVEL_WARNING, "cannot write to IPC socket: %s", strerror(errno));
			ret = FAIL;
			break;
		}
		/* 否则，将 n 加到 offset 上，表示本次写入成功 */
		offset += n;
	}

	/* 函数结束后，将实际发送的数据长度（offset）赋值给 size_sent */
	*size_sent = offset;

	/* 返回函数执行结果，成功则返回 SUCCEED，失败则返回 FAIL */
	return ret;
}

	int		n, ret = SUCCEED;

	while (offset != size)
	{
		n = write(fd, data + offset, size - offset);

		if (-1 == n)
		{
			if (EINTR == errno)
				continue;

			if (EWOULDBLOCK == errno || EAGAIN == errno)
				break;

			zabbix_log(LOG_LEVEL_WARNING, "cannot write to IPC socket: %s", strerror(errno));
			ret = FAIL;
			break;
		}
		offset += n;
	}

	*size_sent = offset;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_read_data                                                    *
 *                                                                            *
 * Purpose: reads data from a socket                                          *
 *                                                                            *
 * Parameters: fd        - [IN] the socket file descriptor                    *
 *             data      - [IN] the data                                      *
 *             size      - [IN] the data size                                 *
 *             size_sent - [IN] the actual size read from socket              *
 *                                                                            *
 * Return value: SUCCEED - the data was successfully read                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: When reading data from non-blocking sockets SUCCEED will be      *
 *           returned also if there were no more data to read.                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从文件描述符fd指定的文件中读取数据，将数据存储在buffer缓冲区中，并返回实际读取到的数据长度。在读取数据过程中，如果遇到EINTR、EWOULDBLOCK或EAGAIN错误，函数会继续尝试读取；如果读取到文件末尾或发生其他错误，则返回失败。
 ******************************************************************************/
// 定义一个函数ipc_read_data，接收5个参数：
// int fd：文件描述符，用于标识要读取的文件
// unsigned char *buffer：用于存储读取到的数据的缓冲区
// zbx_uint32_t size：读取数据的最大长度
// zbx_uint32_t *read_size：用于存储实际读取到的数据长度
// 返回值：成功（SUCCEED）或失败（FAIL）
static int ipc_read_data(int fd, unsigned char *buffer, zbx_uint32_t size, zbx_uint32_t *read_size)
{
	int n; // 定义一个整型变量n，用于存储读取操作的结果

	// 初始化read_size为0，表示尚未读取任何数据
	*read_size = 0;

	// 使用while循环，当read函数返回-1时继续循环，直到读取到数据或到达最大长度
	while (-1 == (n = read(fd, buffer + *read_size, size - *read_size)))
	{
		// 判断errno是否为EINTR，如果是，则表示读取操作被中断，继续循环
		if (EINTR == errno)
			continue;

		// 判断errno是否为EWOULDBLOCK或EAGAIN，如果是，则表示读取操作被阻塞，返回成功
		if (EWOULDBLOCK == errno || EAGAIN == errno)
			return SUCCEED;

		// 如果是其他错误，返回失败
		return FAIL;
	}

	// 如果n为0，表示已经读取到文件末尾，返回失败
	if (0 == n)
		return FAIL;

	// 将读取到的数据长度累加到read_size中
	*read_size += n;

	// 读取操作成功，返回成功
	return SUCCEED;
}


/******************************************************************************
 * *
 ******************************************************************************/
/* 定义一个函数 ipc_read_data_full，接收 4 个参数：
 * 参数 1：文件描述符（int fd）
 * 参数 2：缓冲区指针（unsigned char *buffer）
 * 参数 3：缓冲区大小（zbx_uint32_t size）
 * 参数 4：读取大小指针（zbx_uint32_t *read_size）
 * 该函数的主要目的是从文件描述符中读取数据到缓冲区，并返回读取的结果。
 */
static int	ipc_read_data_full(int fd, unsigned char *buffer, zbx_uint32_t size, zbx_uint32_t *read_size)
{
	/* 定义一个 int 类型的变量 ret，初始值为 FAIL，用于存储函数执行结果 */
	int		ret = FAIL;
	/* 定义一个 zbx_uint32_t 类型的变量 offset，用于记录当前读取的位置 */
	zbx_uint32_t	offset = 0, chunk_size;

	/* 初始化读取大小指针为 0 */
	*read_size = 0;

	/* 使用 while 循环不断读取数据，直到缓冲区大小 size 为止 */
	while (offset < size)
	{
		/* 调用 ipc_read_data 函数读取数据，并将读取的大小存储在 chunk_size 变量中 */
		if (FAIL == ipc_read_data(fd, buffer + offset, size - offset, &chunk_size))
			/* 如果读取失败，跳转到 out 标签处 */
			goto out;

		/* 如果读取到的数据块大小为 0，说明已经读取完毕，跳出循环 */
		if (0 == chunk_size)
			break;

		/* 更新 offset，继续读取下一块数据 */
		offset += chunk_size;
	}

	/* 读取成功，将 ret 设置为 SUCCEED */
	ret = SUCCEED;
out:
	/* 输出读取的大小 */
	*read_size = offset;

	/* 返回读取结果 */
	return ret;
}

	*read_size = 0;

	while (offset < size)
	{
		if (FAIL == ipc_read_data(fd, buffer + offset, size - offset, &chunk_size))
			goto out;

		if (0 == chunk_size)
			break;

		offset += chunk_size;
	}

	ret = SUCCEED;
out:
	*read_size = offset;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_socket_write_message                                         *
 *                                                                            *
 * Purpose: writes IPC message to socket                                      *
 *                                                                            *
 * Parameters: csocket - [IN] the IPC socket                                  *
 *             code    - [IN] the message code                                *
 *             data    - [IN] the data                                        *
 *             size    - [IN] the data size                                   *
 *             tx_size - [IN] the actual size written to socket               *
 *                                                                            *
 * Return value: SUCCEED - no socket errors were detected. Either the data or *
 *                         a part of it was written to socket or a write to   *
 *                         non-blocking socket would block                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: When using non-blocking sockets the tx_size parameter must be    *
 *           checked in addition to return value to tell if the message was   *
 *           sent successfully.                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数，用于向IPC套接字发送消息。函数接收五个参数：一个指向zbx_ipc_socket_t结构体的指针、消息代码、消息数据、消息长度和指向发送长度变量的指针。在函数中，首先将消息代码和长度存储在buffer数组中，然后判断buffer是否足够大，如果足够大，则将消息数据复制到buffer中。接着调用ipc_write_data函数分两次发送buffer中的数据，最后发送剩余的消息数据。函数返回发送结果。
 ******************************************************************************/
// 定义一个函数，用于向IPC套接字发送消息
static int ipc_socket_write_message(zbx_ipc_socket_t *csocket, zbx_uint32_t code, const unsigned char *data,
                                   zbx_uint32_t size, zbx_uint32_t *tx_size)
{
    // 定义一些变量
    int ret;
    zbx_uint32_t size_data, buffer[ZBX_IPC_SOCKET_BUFFER_SIZE / sizeof(zbx_uint32_t)];

    // 将代码和消息长度存储到buffer数组中
    buffer[0] = code;
    buffer[1] = size;

    // 判断buffer是否足够大，如果足够大，则将消息数据复制到buffer中
    if (ZBX_IPC_SOCKET_BUFFER_SIZE - ZBX_IPC_HEADER_SIZE >= size)
    {
        if (0 != size)
            memcpy(buffer + 2, data, size);

        // 调用ipc_write_data函数发送消息
        return ipc_write_data(csocket->fd, (unsigned char *)buffer, size + ZBX_IPC_HEADER_SIZE, tx_size);
    }

    // 如果buffer不够大，分多次发送
    if (FAIL == ipc_write_data(csocket->fd, (unsigned char *)buffer, ZBX_IPC_HEADER_SIZE, tx_size))
        return FAIL;

    // 非阻塞套接字情况下，可能只会发送部分头部
    if (ZBX_IPC_HEADER_SIZE != *tx_size)
        return SUCCEED;

    // 发送剩余的消息数据
    ret = ipc_write_data(csocket->fd, data, size, &size_data);
    *tx_size += size_data;

    // 返回发送结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_read_buffer                                                  *
 *                                                                            *
 * Purpose: reads message header and data from buffer                         *
 *                                                                            *
 * Parameters: header      - [IN/OUT] the message header                      *
 *             data        - [OUT] the message data                           *
 *             rx_bytes    - [IN] the number of bytes stored in message       *
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于解析缓冲区中的IPC消息
 * 
 * 参数：
 *   buffer - 输入缓冲区
 *   size - 缓冲区大小
 *   read_size - 读取字节数输出参数
 * 
 * 返回值：
 *   SUCCEED - 消息解析成功
 *   FAIL - 数据不足
 * 
 * 注释：
 *   1. 检查缓冲区中是否包含完整的IPC消息头，如果不包含，则返回FAIL
 *   2. 如果缓冲区中包含完整的IPC消息头，则解析消息长度，并分配消息体内存
 *   3. 拷贝消息体数据到分配的内存中
 *   4. 检查是否拷贝完毕，如果完毕，则返回SUCCEED，否则返回FAIL
 */
static int	ipc_read_buffer(zbx_uint32_t *header, unsigned char **data, zbx_uint32_t rx_bytes,
		const unsigned char *buffer, zbx_uint32_t size, zbx_uint32_t *read_size)
{
	zbx_uint32_t	copy_size, data_size, data_offset;

	/* 初始化读取字节数为0 */
	*read_size = 0;

	/* 检查缓冲区中是否包含完整的IPC消息头 */
	if (ZBX_IPC_HEADER_SIZE > rx_bytes)
	{
		/* 如果缓冲区中剩余的字节数不足以拷贝完整的消息头，则拷贝剩余的字节数 */
		copy_size = MIN(ZBX_IPC_HEADER_SIZE - rx_bytes, size);
		memcpy((char *)header + rx_bytes, buffer, copy_size);
		/* 更新读取字节数 */
		*read_size += copy_size;

		/* 如果缓冲区中剩余的字节数不足以容纳完整的消息体，则返回FAIL */
		if (ZBX_IPC_HEADER_SIZE > rx_bytes + copy_size)
			return FAIL;

		data_size = header[ZBX_IPC_MESSAGE_SIZE];

		/* 如果消息体长度为0，说明消息解析完毕，返回SUCCEED */
		if (0 == data_size)
		{
			*data = NULL;
			return SUCCEED;
		}

		/* 分配消息体内存 */
		*data = (unsigned char *)zbx_malloc(NULL, data_size);
		data_offset = 0;
	}
	else
	{
		data_size = header[ZBX_IPC_MESSAGE_SIZE];
		data_offset = rx_bytes - ZBX_IPC_HEADER_SIZE;
	}

	/* 拷贝消息体数据到分配的内存中 */
	copy_size = MIN(data_size - data_offset, size - *read_size);
	memcpy(*data + data_offset, buffer + *read_size, copy_size);
	/* 更新读取字节数 */
	*read_size += copy_size;

	/* 检查是否拷贝完毕，如果完毕，则返回SUCCEED，否则返回FAIL */
	return (rx_bytes + *read_size == data_size + ZBX_IPC_HEADER_SIZE ? SUCCEED : FAIL);
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_message_is_completed                                         *
 *                                                                            *
 * Purpose: checks if IPC message has been completed                          *
 *                                                                            *
 * Parameters: header   - [IN] the message header                             *
 *             rx_bytes - [IN] the number of bytes set in message             *
/******************************************************************************
 * *
 *这块代码的主要目的是从IPC套接字中读取消息。函数`ipc_socket_read_message`接收四个参数：一个指向`zbx_ipc_socket_t`结构体的指针`csocket`，一个指向`zbx_uint32_t`类型的指针`header`，一个指向`unsigned char`类型的指针`data`，以及一个指向`zbx_uint32_t`类型的指针`rx_bytes`。
 *
 *函数内部首先检查套接字缓冲区中是否有足够的数据供读取，如果有，则直接读取数据。如果缓冲区中的数据不足，则不断尝试读取数据直到消息完整或无法再读取。在读取数据的过程中，可能会遇到缓冲区大小不足的情况，此时会直接读取数据到消息缓冲区。最后，将读取到的数据和消息头一起返回。
 ******************************************************************************/
/* 定义一个函数，用于从IPC套接字中读取消息 */
static int ipc_socket_read_message(zbx_ipc_socket_t *csocket, zbx_uint32_t *header, unsigned char **data,
                                   zbx_uint32_t *rx_bytes)
{
	/* 定义一些变量，用于记录数据大小、偏移量、读取大小等 */
	zbx_uint32_t data_size, offset, read_size = 0;
	int ret = FAIL;

	/* 尝试从套接字缓冲区中读取消息 */
	if (csocket->rx_buffer_bytes > csocket->rx_buffer_offset)
	{
		ret = ipc_read_buffer(header, data, rx_bytes, csocket->rx_buffer + csocket->rx_buffer_offset,
		                     csocket->rx_buffer_bytes - csocket->rx_buffer_offset, &read_size);

		/* 更新缓冲区偏移量和已读取字节数 */
		csocket->rx_buffer_offset += read_size;
		*rx_bytes += read_size;

		/* 如果读取成功，跳出循环 */
		if (SUCCEED == ret)
			goto out;
	}

	/* 缓冲区中数据不足，尝试继续读取直到消息完整或无法再读取 */
	while (SUCCEED != ret)
	{
		/* 重置缓冲区偏移量和已读取字节数 */
		csocket->rx_buffer_offset = 0;
		csocket->rx_buffer_bytes = 0;

		/* 如果读取的字节数小于IPC头部长度，说明还未读取到完整消息 */
		if (ZBX_IPC_HEADER_SIZE < *rx_bytes)
		{
			offset = *rx_bytes - ZBX_IPC_HEADER_SIZE;
			data_size = header[ZBX_IPC_MESSAGE_SIZE] - offset;

			/* 如果数据长度超过缓冲区大小的一定比例，直接读取到消息缓冲区 */
			if (ZBX_IPC_SOCKET_BUFFER_SIZE * 0.75 < data_size)
			{
				ret = ipc_read_data_full(csocket->fd, *data + offset, data_size, &read_size);
				*rx_bytes += read_size;
				goto out;
			}
		}

		/* 否则，尝试从套接字中读取数据 */
		if (FAIL == ipc_read_data(csocket->fd, csocket->rx_buffer, ZBX_IPC_SOCKET_BUFFER_SIZE, &read_size))
			goto out;

		/* 如果读取的字节数为0，说明套接字为非阻塞模式，直接返回成功 */
		if (0 == read_size)
		{
			ret = SUCCEED;
			goto out;
		}

		/* 更新缓冲区大小和偏移量 */
		csocket->rx_buffer_bytes = read_size;

		/* 再次尝试读取缓冲区中的数据 */
		ret = ipc_read_buffer(header, data, rx_bytes, csocket->rx_buffer, csocket->rx_buffer_bytes,
		                     &read_size);

		/* 更新缓冲区偏移量和已读取字节数 */
		csocket->rx_buffer_offset += read_size;
		*rx_bytes += read_size;
	}

out:
	/* 返回读取结果 */
	return ret;
}


		csocket->rx_buffer_offset += read_size;
		*rx_bytes += read_size;

		if (SUCCEED == ret)
			goto out;
	}

	/* not enough data in socket buffer, try to read more until message is completed or no data to read */
	while (SUCCEED != ret)
	{
		csocket->rx_buffer_offset = 0;
		csocket->rx_buffer_bytes = 0;

		if (ZBX_IPC_HEADER_SIZE < *rx_bytes)
		{
			offset = *rx_bytes - ZBX_IPC_HEADER_SIZE;
			data_size = header[ZBX_IPC_MESSAGE_SIZE] - offset;

			/* long messages will be read directly into message buffer */
			if (ZBX_IPC_SOCKET_BUFFER_SIZE * 0.75 < data_size)
			{
				ret = ipc_read_data_full(csocket->fd, *data + offset, data_size, &read_size);
				*rx_bytes += read_size;
				goto out;
			}
		}

		if (FAIL == ipc_read_data(csocket->fd, csocket->rx_buffer, ZBX_IPC_SOCKET_BUFFER_SIZE, &read_size))
			goto out;

		/* it's possible that nothing will be read on non-blocking sockets, return success */
		if (0 == read_size)
		{
			ret = SUCCEED;
			goto out;
		}

		csocket->rx_buffer_bytes = read_size;

		ret = ipc_read_buffer(header, data, *rx_bytes, csocket->rx_buffer, csocket->rx_buffer_bytes,
				&read_size);

		csocket->rx_buffer_offset += read_size;
		*rx_bytes += read_size;
	}
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_client_free_event                                            *
 *                                                                            *
 * Purpose: frees client's libevent event                                     *
 *                                                                            *
 * Parameters: client - [IN] the client                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放zbx_ipc_client_t结构体中的接收事件和发送事件。在函数内部，首先判断接收事件和发送事件是否不为空，如果不为空，则使用event_free函数分别释放它们，并将指针设置为NULL，防止野指针。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_ipc_client_t结构体中的接收事件和发送事件
static void ipc_client_free_events(zbx_ipc_client_t *client)
{
    // 判断client->rx_event是否不为空，如果不为空，则执行以下操作：
    if (NULL != client->rx_event)
    {
/******************************************************************************
 * *
 *整个代码块的主要目的是释放zbx_ipc_client_t结构体所占用的资源。具体包括以下几个步骤：
 *
 *1. 释放客户端的事件队列。
 *2. 关闭客户端的套接字。
 *3. 遍历接收队列，释放其中的消息，并销毁接收队列。
 *4. 释放接收数据的内存。
 *5. 遍历发送队列，释放其中的消息，并销毁发送队列。
 *6. 再次释放客户端的事件队列。
 *7. 释放客户端本身。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_ipc_client_t结构体的资源
static void ipc_client_free(zbx_ipc_client_t *client)
{
    // 定义一个zbx_ipc_message_t类型的指针，用于存储从队列中获取的消息
    zbx_ipc_message_t *message;

    // 释放客户端的事件队列
    ipc_client_free_events(client);

    // 关闭客户端的套接字
    zbx_ipc_socket_close(&client->csocket);

    // 遍历接收队列，释放其中的消息
    while (NULL != (message = (zbx_ipc_message_t *)zbx_queue_ptr_pop(&client->rx_queue)))
    {
        // 释放消息对象
        zbx_ipc_message_free(message);
    }

    // 销毁接收队列
    zbx_queue_ptr_destroy(&client->rx_queue);

    // 释放接收数据的内存
    zbx_free(client->rx_data);

    // 遍历发送队列，释放其中的消息
    while (NULL != (message = (zbx_ipc_message_t *)zbx_queue_ptr_pop(&client->tx_queue)))
    {
        // 释放消息对象
        zbx_ipc_message_free(message);
    }

    // 销毁发送队列
    zbx_queue_ptr_destroy(&client->tx_queue);

    // 再次释放客户端的事件队列
    ipc_client_free_events(client);

    // 释放客户端本身
    zbx_free(client);
}

 * Parameters: client - [IN] the client to free                               *
 *                                                                            *
 ******************************************************************************/
static void	ipc_client_free(zbx_ipc_client_t *client)
{
	zbx_ipc_message_t	*message;

	ipc_client_free_events(client);
	zbx_ipc_socket_close(&client->csocket);

	while (NULL != (message = (zbx_ipc_message_t *)zbx_queue_ptr_pop(&client->rx_queue)))
		zbx_ipc_message_free(message);

	zbx_queue_ptr_destroy(&client->rx_queue);
	zbx_free(client->rx_data);

	while (NULL != (message = (zbx_ipc_message_t *)zbx_queue_ptr_pop(&client->tx_queue)))
		zbx_ipc_message_free(message);

	zbx_queue_ptr_destroy(&client->tx_queue);
	zbx_free(client->tx_data);

	ipc_client_free_events(client);

	zbx_free(client);
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_client_push_rx_message                                       *
 *                                                                            *
 * Purpose: adds message to received messages queue                           *
 *                                                                            *
 * Parameters: client - [IN] the client to read                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：分配一个zbx_ipc_message_t类型的消息对象，将其代码、大小和数据设置为从客户端接收到的值，然后将该消息对象推向客户端的接收队列。最后，清空客户端的接收数据指针和接收字节数。
 ******************************************************************************/
// 定义一个静态函数，用于向客户端的接收队列中推送消息
static void ipc_client_push_rx_message(zbx_ipc_client_t *client)
{
	// 定义一个指向zbx_ipc_message_t类型的指针，用于存储消息对象
	zbx_ipc_message_t *message;

	// 分配一块内存，用于存储zbx_ipc_message_t类型的消息对象
	message = (zbx_ipc_message_t *)zbx_malloc(NULL, sizeof(zbx_ipc_message_t));

	// 设置消息对象的代码
	message->code = client->rx_header[ZBX_IPC_MESSAGE_CODE];

	// 设置消息对象的大小
	message->size = client->rx_header[ZBX_IPC_MESSAGE_SIZE];

	// 设置消息对象的数据指针
	message->data = client->rx_data;

	// 将消息对象推向客户端的接收队列
	zbx_queue_ptr_push(&client->rx_queue, message);

	// 清空客户端的接收数据指针和接收字节数
	client->rx_data = NULL;
	client->rx_bytes = 0;
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_client_pop_tx_message                                        *
 *                                                                            *
 * Purpose: prepares to send the next message in send queue                   *
 *                                                                            *
 * Parameters: client - [IN] the client                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是处理IPC（ inter-process communication，进程间通信）客户端的发送消息队列。当客户端接收到一个消息时，该函数会被调用。函数首先释放之前的发送数据缓冲区，然后从发送队列中取出一个消息。接着，计算消息的总长度（包括头部和数据部分），并设置消息代码域和消息长度域的值。最后，将消息数据存储在发送数据缓冲区中，并释放当前消息对象。整个过程完成后，继续处理下一个消息。
 ******************************************************************************/
// 定义一个静态函数，用于处理IPC客户端的发送消息队列
static void ipc_client_pop_tx_message(zbx_ipc_client_t *client)
{
	// 定义一个指向消息对象的指针
	zbx_ipc_message_t *message;

	// 释放客户端的发送数据缓冲区
	zbx_free(client->tx_data);
	// 重置客户端的发送字节数为0
	client->tx_bytes = 0;

	// 从客户端的发送队列中取出一个消息
	if (NULL == (message = (zbx_ipc_message_t *)zbx_queue_ptr_pop(&client->tx_queue)))
		// 如果队列为空，直接返回
		return;

	// 计算消息总长度，包括头部和数据部分
	client->tx_bytes = ZBX_IPC_HEADER_SIZE + message->size;
	// 设置消息代码域值
	client->tx_header[ZBX_IPC_MESSAGE_CODE] = message->code;
	// 设置消息长度域值
	client->tx_header[ZBX_IPC_MESSAGE_SIZE] = message->size;
	// 设置发送数据缓冲区的数据指针
	client->tx_data = message->data;
	// 释放当前消息对象
	zbx_free(message);
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_client_read                                                  *
 *                                                                            *
 * Purpose: reads data from IPC service client                                *
 *                                                                            *
 * Parameters: client - [IN] the client to read                               *
 *                                                                            *
 * Return value:  FAIL - read error/connection was closed                     *
 *                                                                            *
 * Comments: This function reads data from socket, parses it and adds         *
 *           parsed messages to received messages queue.                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于读取客户端套接字中的消息。首先，通过 do-while 循环不断地调用 ipc_socket_read_message 函数从套接字中读取消息，并将读取到的消息放入 client->rx_data 缓冲区。如果读取消息失败，释放 client->rx_data 缓冲区，并将 client->rx_bytes 置为 0。当读取到完整消息时，将消息推入接收消息队列，然后继续读取下一条消息。直到读取操作失败或读取到完整消息为止。最后，返回读取操作成功的结果。
 ******************************************************************************/
// 定义一个名为 ipc_client_read 的静态函数，参数为一个指向 zbx_ipc_client_t 结构体的指针 client
static int ipc_client_read(zbx_ipc_client_t *client)
{
	// 定义一个整型变量 rc 用于存放函数执行结果
	int rc;

	// 使用 do-while 循环进行读取操作
	do
	{
		// 调用 ipc_socket_read_message 函数从套接字中读取消息，并将读取到的消息放入 client->rx_data 缓冲区
		if (FAIL == ipc_socket_read_message(&client->csocket, client->rx_header, &client->rx_data,
				&client->rx_bytes))
		{
			// 如果读取消息失败，释放 client->rx_data 缓冲区，并将 client->rx_bytes 置为 0
			zbx_free(client->rx_data);
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个IPC客户端的写入功能。该函数接收一个zbx_ipc_client_t类型的指针作为参数，该指针指向一个IPC客户端的结构体。函数内部首先获取客户端发送数据的大小，然后根据发送数据的大小和客户端发送的数据字节数进行分多次写入数据。在写入数据的过程中，如果遇到写入失败的情况，函数返回FAIL。如果所有数据均成功写入，函数调用ipc_client_pop_tx_message弹出发送队列中的消息，并返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数，用于处理IPC客户端写入数据
static int ipc_client_write(zbx_ipc_client_t *client)
{
	// 定义两个zbx_uint32_t类型的变量，分别为data_size和write_size，用于存储数据大小和写入大小
	zbx_uint32_t data_size, write_size;

	// 获取客户端发送数据的大小，存储在data_size变量中
	data_size = client->tx_header[ZBX_IPC_MESSAGE_SIZE];

	// 判断data_size是否小于客户端发送的数据字节数（tx_bytes）
	if (data_size < client->tx_bytes)
	{
		// 计算需要写入的数据大小（size）和偏移量（offset）
		zbx_uint32_t size, offset;
		size = client->tx_bytes - data_size;
		offset = ZBX_IPC_HEADER_SIZE - size;

		// 调用ipc_write_data函数，将数据写入到socket中
		if (SUCCEED != ipc_write_data(client->csocket.fd, (unsigned char *)client->tx_header + offset, size,
				&write_size))
		{
			// 如果写入数据失败，返回FAIL
			return FAIL;
		}

		// 更新客户端发送数据字节数
		client->tx_bytes -= write_size;

		// 如果data_size仍然小于客户端发送的数据字节数，说明写入成功，返回SUCCEED
		if (data_size < client->tx_bytes)
			return SUCCEED;
	}

	// 当客户端发送数据字节数大于0时，进入循环，分多次写入数据
	while (0 < client->tx_bytes)
	{
		// 调用ipc_write_data函数，将数据写入到socket中
		if (SUCCEED != ipc_write_data(client->csocket.fd, client->tx_data + data_size - client->tx_bytes,
				client->tx_bytes, &write_size))
		{
			// 如果写入数据失败，返回FAIL
			return FAIL;
		}

		// 如果write_size等于0，说明数据写入成功，返回SUCCEED
		if (0 == write_size)
			return SUCCEED;

		// 更新客户端发送数据字节数
		client->tx_bytes -= write_size;
	}

	// 当客户端发送数据字节数为0时，调用ipc_client_pop_tx_message函数，弹出发送队列中的消息
	if (0 == client->tx_bytes)
		ipc_client_pop_tx_message(client);

	// 写入数据成功，返回SUCCEED
	return SUCCEED;
}

		if (SUCCEED != ipc_write_data(client->csocket.fd, (unsigned char *)client->tx_header + offset, size,
				&write_size))
		{
			return FAIL;
		}

		client->tx_bytes -= write_size;

		if (data_size < client->tx_bytes)
			return SUCCEED;
	}

	while (0 < client->tx_bytes)
	{
		if (SUCCEED != ipc_write_data(client->csocket.fd, client->tx_data + data_size - client->tx_bytes,
				client->tx_bytes, &write_size))
		{
			return FAIL;
		}

		if (0 == write_size)
			return SUCCEED;

		client->tx_bytes -= write_size;
	}

	if (0 == client->tx_bytes)
		ipc_client_pop_tx_message(client);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_service_pop_client                                           *
 *                                                                            *
 * Purpose: gets the next client with messages/closed socket from recv queue  *
 *                                                                            *
 * Parameters: service - [IN] the IPC service                                 *
 *                                                                            *
 * Return value: The client with messages/closed socket                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从zbx_ipc_service类型的服务器的接收队列中弹出一个zbx_ipc_client_t类型的客户端对象。如果成功弹出客户端对象，则将客户端对象的状态设置为ZBX_IPC_CLIENT_STATE_NONE，然后返回这个客户端对象。
 ******************************************************************************/
// 定义一个静态局部变量，指向zbx_ipc_client_t类型的指针
static zbx_ipc_client_t *ipc_service_pop_client(zbx_ipc_service_t *service)
{
	// 定义一个zbx_ipc_client_t类型的指针，命名为client
	zbx_ipc_client_t	*client;

	// 判断zbx_queue_ptr_pop()函数的返回值是否不为NULL，它从服务器的接收队列中弹出一个zbx_ipc_client_t类型的对象
	if (NULL != (client = (zbx_ipc_client_t *)zbx_queue_ptr_pop(&service->clients_recv)))
	{
		// 将client的状态设置为ZBX_IPC_CLIENT_STATE_NONE，表示客户端已断开连接
		client->state = ZBX_IPC_CLIENT_STATE_NONE;
	}

	// 返回从接收队列中弹出的客户端对象
	return client;
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_service_push_client                                          *
 *                                                                            *
 * Purpose: pushes client to the recv queue if needed                         *
 *                                                                            *
 * Parameters: service - [IN] the IPC service                                 *
 *             client  - [IN] the IPC client                                  *
 *                                                                            *
 * Comments: The client is pushed to the recv queue if it isn't already there *
 *           and there is messages to return or the client connection was     *
 *           closed.                                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于向IPC服务推送客户端。当客户端的状态不是等待状态，且接收队列不为空且存在接收事件时，将客户端添加到服务器的接收队列中，使其准备好接收数据。整个代码块的功能可以概括为以下几点：
 *
 *1. 判断客户端状态，如果为等待状态，则直接返回，不进行推送。
 *2. 判断接收队列中是否为空，且客户端有接收事件，如果满足条件，则直接返回，不进行推送。
 *3. 将客户端状态设置为等待状态。
 *4. 将客户端添加到服务器的接收队列中，使其准备好接收数据。
 ******************************************************************************/
// 定义一个静态函数，用于向IPC服务推送客户端
/******************************************************************************
 * *
 *整个代码块的主要目的是用于添加一个新的客户端到IPC服务中。具体操作如下：
 *
 *1. 定义常量和变量，如函数名、下一个客户端ID等。
 *2. 分配内存创建一个新的客户端结构体，并将其初始化。
 *3. 获取客户端套接字的标志，并设置为非阻塞模式。
 *4. 设置客户端的输入和输出缓冲区，以及客户端ID、状态和引用计数。
 *5. 创建客户端的接收和发送队列。
 *6. 设置客户端的服务器对象。
 *7. 创建客户端的读事件和写事件，并添加事件监听。
 *8. 将新创建的客户端添加到服务器客户端列表中。
 *9. 记录日志，表示函数执行完毕。
 ******************************************************************************/
static void ipc_service_add_client(zbx_ipc_service_t *service, int fd)
{
    // 定义一个常量字符串，表示当前函数名
    const char *__function_name = "ipc_service_add_client";
    // 定义一个静态变量，用于存储下一个客户端ID
    static zbx_uint64_t next_clientid = 1;
    // 定义一个指向客户端结构的指针
    zbx_ipc_client_t *client;
    // 获取文件描述符的标志
    int flags;

    // 记录日志，表示进入函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 分配内存，创建一个新的客户端结构体
    client = (zbx_ipc_client_t *)zbx_malloc(NULL, sizeof(zbx_ipc_client_t));
    // 将客户端结构体清零
    memset(client, 0, sizeof(zbx_ipc_client_t));

    // 获取客户端套接字标志
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
    {
        // 记录日志，表示获取套接字标志失败
        zabbix_log(LOG_LEVEL_CRIT, "cannot get IPC client socket flags");
        // 退出程序
        exit(EXIT_FAILURE);
    }

    // 设置客户端套接字为非阻塞模式
    if (-1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK))
    {
        // 记录日志，表示设置非阻塞模式失败
        zabbix_log(LOG_LEVEL_CRIT, "cannot set non-blocking mode for IPC client socket");
        // 退出程序
        exit(EXIT_FAILURE);
    }

    // 设置客户端套接字的输入缓冲区和输出缓冲区
    client->csocket.fd = fd;
    client->csocket.rx_buffer_bytes = 0;
    client->csocket.rx_buffer_offset = 0;
    // 设置客户端ID
    client->id = next_clientid++;
    // 设置客户端状态
    client->state = ZBX_IPC_CLIENT_STATE_NONE;
    // 设置客户端引用计数
    client->refcount = 1;

    // 创建客户端的接收和发送队列
    zbx_queue_ptr_create(&client->rx_queue);
    zbx_queue_ptr_create(&client->tx_queue);

    // 设置客户端的服务器对象
    client->service = service;
    // 创建客户端读事件和写事件
    client->rx_event = event_new(service->ev, fd, EV_READ | EV_PERSIST, ipc_client_read_event_cb, (void *)client);
    client->tx_event = event_new(service->ev, fd, EV_WRITE | EV_PERSIST, ipc_client_write_event_cb, (void *)client);
    // 添加事件监听
    event_add(client->rx_event, NULL);

    // 将新创建的客户端添加到服务器客户端列表中
    zbx_vector_ptr_append(&service->clients, client);

    // 记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s() clientid:" ZBX_FS_UI64, __function_name, client->id);
}


	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot get IPC client socket flags");
		exit(EXIT_FAILURE);
	}

	if (-1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot set non-blocking mode for IPC client socket");
		exit(EXIT_FAILURE);
	}

	client->csocket.fd = fd;
	client->csocket.rx_buffer_bytes = 0;
	client->csocket.rx_buffer_offset = 0;
	client->id = next_clientid++;
	client->state = ZBX_IPC_CLIENT_STATE_NONE;
	client->refcount = 1;

	zbx_queue_ptr_create(&client->rx_queue);
	zbx_queue_ptr_create(&client->tx_queue);

	client->service = service;
	client->rx_event = event_new(service->ev, fd, EV_READ | EV_PERSIST, ipc_client_read_event_cb, (void *)client);
	client->tx_event = event_new(service->ev, fd, EV_WRITE | EV_PERSIST, ipc_client_write_event_cb, (void *)client);
	event_add(client->rx_event, NULL);

	zbx_vector_ptr_append(&service->clients, client);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() clientid:" ZBX_FS_UI64, __function_name, client->id);
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_service_remove_client                                        *
 *                                                                            *
 * Purpose: removes IPC service client                                        *
 *                                                                            *
 * Parameters: service - [IN] the IPC service                                 *
 *             client  - [IN] the client to remove                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从给定的 ipc 服务中移除一个指定的客户端。函数 `ipc_service_remove_client` 接收两个参数，分别是 ipc 服务和要移除的客户端。在函数内部，首先遍历服务中的客户端列表，直到找到要移除的客户端。找到目标客户端后，使用无序删除算法从客户端列表中移除该客户端。整个循环结束后，如果没有找到要移除的客户端，则不会执行任何操作。
 ******************************************************************************/
// 定义一个静态函数，用于从 ipc 服务中移除一个客户端
static void ipc_service_remove_client(zbx_ipc_service_t *service, zbx_ipc_client_t *client)
{
	// 定义一个循环变量 i，用于遍历服务中的客户端列表
	int i;

	// 遍历服务中的客户端列表，直到找到要移除的客户端
	for (i = 0; i < service->clients.values_num; i++)
	{
		// 如果当前客户端等于要移除的客户端，则执行以下操作
		if (service->clients.values[i] == client)
		{
			// 使用无序删除算法从客户端列表中移除当前客户端
			zbx_vector_ptr_remove_noorder(&service->clients, i);
/******************************************************************************
 * *
 *这块代码的主要目的是处理IPC客户端的读事件。当客户端发生读事件时，会调用这个回调函数。在这个函数中，首先忽略传入的fd和what参数，然后调用ipc_client_read函数处理读事件。如果处理失败，则释放客户端的事件资源并从服务中移除。最后，将客户端重新推入服务中的队列，以便继续处理其他事件。
 ******************************************************************************/
// 定义一个回调函数，用于处理IPC客户端的读事件
static void ipc_client_read_event_cb(evutil_socket_t fd, short what, void *arg)
{
    // 将传入的参数转换为zbx_ipc_client_t类型的指针
    zbx_ipc_client_t *client = (zbx_ipc_client_t *)arg;

    // 忽略fd和what参数，不需要处理它们
    ZBX_UNUSED(fd);
    ZBX_UNUSED(what);

    // 调用ipc_client_read函数处理客户端读事件，并将结果存储在client指针中
    if (SUCCEED != ipc_client_read(client))
    {
        // 如果ipc_client_read函数执行失败，释放客户端的事件资源并从服务中移除
        ipc_client_free_events(client);
        ipc_service_remove_client(client->service, client);
    }

    // 将客户端重新推入服务中的队列
    ipc_service_push_client(client->service, client);
}

{
	zbx_ipc_client_t	*client = (zbx_ipc_client_t *)arg;

	ZBX_UNUSED(fd);
	ZBX_UNUSED(what);

	if (SUCCEED != ipc_client_read(client))
	{
		ipc_client_free_events(client);
		ipc_service_remove_client(client->service, client);
	}

	ipc_service_push_client(client->service, client);
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_client_write_event_cb                                        *
 *                                                                            *
 * Purpose: service client write event libevent callback                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理异步套接字写事件。当套接字有数据需要发送时，调用 ipc_client_write 函数发送数据。如果发送失败，记录日志并释放客户端事件。当发送字节数为0时，删除发送事件。
 ******************************************************************************/
// 定义一个回调函数，用于处理异步套接字写事件
static void ipc_async_socket_write_event_cb(evutil_socket_t fd, short what, void *arg)
{
    // 将传入的参数转换为 zbx_ipc_async_socket_t 类型的指针
    zbx_ipc_async_socket_t *asocket = (zbx_ipc_async_socket_t *)arg;

    // 忽略 fd 和 what 参数，它们在本函数中不需要使用
    ZBX_UNUSED(fd);
    ZBX_UNUSED(what);

    // 调用 ipc_client_write 函数向 IPC 客户端发送数据
    if (SUCCEED != ipc_client_write(asocket->client))
    {
        // 如果发送数据失败，记录日志并释放客户端事件
        zabbix_log(LOG_LEVEL_CRIT, "无法向 IPC 客户端发送数据");
        ipc_client_free_events(asocket->client);
        zbx_ipc_socket_close(&asocket->client->csocket);
        asocket->state = ZBX_IPC_ASYNC_SOCKET_STATE_ERROR;
        return;
    }

    // 如果客户端的发送字节数为0，删除发送事件
    if (0 == asocket->client->tx_bytes)
        event_del(asocket->client->tx_event);
}


		// 关闭客户端连接
		zbx_ipc_client_close(client);

		// 返回，结束此次处理
		return;
	}

	// 判断客户端发送缓冲区是否有数据，如果没有，则取消write事件
	if (0 == client->tx_bytes)
		// 取消write事件
		event_del(client->tx_event);
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_async_socket_write_event_cb                                  *
 *                                                                            *
 * Purpose: asynchronous socket write event libevent callback                 *
 *                                                                            *
 ******************************************************************************/
static void	ipc_async_socket_write_event_cb(evutil_socket_t fd, short what, void *arg)
{
	zbx_ipc_async_socket_t	*asocket = (zbx_ipc_async_socket_t *)arg;

	ZBX_UNUSED(fd);
	ZBX_UNUSED(what);

	if (SUCCEED != ipc_client_write(asocket->client))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot send data to IPC client");
		ipc_client_free_events(asocket->client);
		zbx_ipc_socket_close(&asocket->client->csocket);
		asocket->state = ZBX_IPC_ASYNC_SOCKET_STATE_ERROR;
		return;
	}

	if (0 == asocket->client->tx_bytes)
		event_del(asocket->client->tx_event);
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_async_socket_read_event_cb                                   *
 *                                                                            *
 * Purpose: asynchronous socket read event libevent callback                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是处理异步套接字读事件。当异步套接字接收到数据时，会调用这个回调函数。函数首先忽略传入的fd和what参数，然后读取客户端数据。如果读取失败，就释放客户端事件并将异步套接字状态设置为ERROR。
 ******************************************************************************/
// 定义一个回调函数，用于处理异步套接字读事件
static void ipc_async_socket_read_event_cb(evutil_socket_t fd, short what, void *arg)
{
    // 类型转换，将传入的参数arg转换为zbx_ipc_async_socket_t指针
    zbx_ipc_async_socket_t *asocket = (zbx_ipc_async_socket_t *)arg;

    // 忽略fd和what参数，不需要使用它们
    ZBX_UNUSED(fd);
    ZBX_UNUSED(what);

    // 调用ipc_client_read函数读取客户端数据
    if (SUCCEED != ipc_client_read(asocket->client))
    {
        // 如果读取失败，调用ipc_client_free_events释放客户端事件
        // 并将异步套接字状态设置为ERROR
        ipc_client_free_events(asocket->client);
        asocket->state = ZBX_IPC_ASYNC_SOCKET_STATE_ERROR;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_async_socket_timer_cb                                        *
 *                                                                            *
 * Purpose: timer callback                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个回调函数，用于处理异步套接字超时事件。当异步套接字超时时，系统会调用这个函数。在此函数中，首先将传入的参数转换为zbx_ipc_async_socket_t类型的指针，然后忽略fd和what参数，因为它们在本函数中无用。最后，设置异步套接字的状态为超时。
 ******************************************************************************/
// 定义一个回调函数，用于处理异步套接字超时事件
static void ipc_async_socket_timer_cb(evutil_socket_t fd, short what, void *arg)
{
	// 将传入的参数转换为zbx_ipc_async_socket_t类型的指针
/******************************************************************************
 * *
 *整个代码块的主要目的是处理IPC（进程间通信）服务的连接接受。函数`ipc_service_accept`接受一个`zbx_ipc_service_t`类型的指针作为参数，该类型可能是一个描述IPC服务状态的结构体。函数内部使用while循环不断尝试`accept()`函数，用于接收新的连接。如果连接接受失败，会打印错误日志并退出程序。如果连接接受成功，将新连接添加到服务中。最后，打印调试日志表示函数执行完毕。
 ******************************************************************************/
// 定义一个静态函数，用于处理IPC服务的接受连接
static void ipc_service_accept(zbx_ipc_service_t *service)
{
    // 定义一个字符串指针，用于存储函数名
    const char *__function_name = "ipc_service_accept";
    // 定义一个整型变量，用于存储文件描述符
    int fd;

    // 打印调试日志，表示进入函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 使用while循环，不断尝试accept()函数，直到成功为止
    while (-1 == (fd = accept(service->fd, NULL, NULL)))
    {
        // 如果errno不是EINTR，说明接受连接失败
        if (EINTR != errno)
        {
            // 如果有一次连接未接受，libevent会不断调用回调函数，最好直接退出程序，让其他进程停止
            zabbix_log(LOG_LEVEL_CRIT, "cannot accept incoming IPC connection: %s", zbx_strerror(errno));
            // 退出程序，返回失败
            exit(EXIT_FAILURE);
        }
    }

    // 成功接受连接后，将新连接添加到服务中
    ipc_service_add_client(service, fd);

    // 打印调试日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	const char	*__function_name = "ipc_service_accept";
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个zbx_ipc_message_t类型的指针，该指针指向一个包含消息代码、消息大小和数据的结构体。函数ipc_message_create接收三个参数：消息代码、数据指针和数据大小。如果数据大小不为0，函数将为消息结构体分配内存并复制数据；如果数据大小为0，则不分配内存。最后，返回创建好的消息结构体指针。
 ******************************************************************************/
// 定义一个函数ipc_message_create，用于创建一个zbx_ipc_message_t类型的指针，该指针指向一个包含消息代码、消息大小和数据的结构体。
static zbx_ipc_message_t	*ipc_message_create(zbx_uint32_t code, const unsigned char *data, zbx_uint32_t size)
{
	// 定义一个指向zbx_ipc_message_t类型的指针message，用于存储创建的消息结构体。
	zbx_ipc_message_t	*message;

	// 为message分配内存，使其成为一个合法的zbx_ipc_message_t类型的指针。
	message = (zbx_ipc_message_t *)zbx_malloc(NULL, sizeof(zbx_ipc_message_t));

	// 设置消息结构体中的代码字段值
	message->code = code;

	// 设置消息结构体中的大小字段值
	message->size = size;

	// 如果数据大小不为0，则分配内存用于存储数据
	if (0 != size)
	{
		// 为消息结构体中的数据字段分配内存
		message->data = (unsigned char *)zbx_malloc(NULL, size);

		// 使用memcpy函数将数据从传入的data参数复制到消息结构体的数据字段中
		memcpy(message->data, data, size);
	}
	else
		// 如果数据大小为0，则将消息结构体的数据字段设置为NULL
		message->data = NULL;

	// 返回创建好的消息结构体指针
	return message;
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_message_create                                               *
 *                                                                            *
 * Purpose: creates IPC message                                               *
 *                                                                            *
 * Parameters: code    - [IN] the message code                                *
 *             data    - [IN] the data                                        *
 *             size    - [IN] the data size                                   *
 *                                                                            *
 * Return value: The created message.                                         *
 *                                                                            *
 ******************************************************************************/
static zbx_ipc_message_t	*ipc_message_create(zbx_uint32_t code, const unsigned char *data, zbx_uint32_t size)
{
	zbx_ipc_message_t	*message;

	message = (zbx_ipc_message_t *)zbx_malloc(NULL, sizeof(zbx_ipc_message_t));

	message->code = code;
	message->size = size;

	if (0 != size)
	{
		message->data = (unsigned char *)zbx_malloc(NULL, size);
		memcpy(message->data, data, size);
	}
	else
		message->data = NULL;

	return message;
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的 severity 和 msg 参数，输出一条日志。日志级别根据 severity 参数的值进行设置。
 ******************************************************************************/
// 定义一个名为 ipc_service_event_log_cb 的静态函数，它接受两个参数：一个整数类型的 severity 和一个字符串类型的 msg。
static void ipc_service_event_log_cb(int severity, const char *msg)
{
	// 定义一个整数类型的变量 loglevel，用于存储日志级别。
	int loglevel;

	// 使用 switch 语句根据 severity 参数的不同值，对 loglevel 进行赋值。
	switch (severity)
	{
		// 当 severity 为 _EVENT_LOG_DEBUG 时，将 loglevel 设置为 LOG_LEVEL_TRACE。
		case _EVENT_LOG_DEBUG:
			loglevel = LOG_LEVEL_TRACE;
			break;

		// 当 severity 为 _EVENT_LOG_MSG 时，将 loglevel 设置为 LOG_LEVEL_DEBUG。
		case _EVENT_LOG_MSG:
			loglevel = LOG_LEVEL_DEBUG;
			break;

		// 当 severity 为 _EVENT_LOG_WARN 时，将 loglevel 设置为 LOG_LEVEL_WARNING。
		case _EVENT_LOG_WARN:
			loglevel = LOG_LEVEL_WARNING;
			break;

		// 当 severity 为 _EVENT_LOG_ERR 时，将 loglevel 设置为 LOG_LEVEL_DEBUG。
		case _EVENT_LOG_ERR:
			loglevel = LOG_LEVEL_DEBUG;
			break;

		// 当 severity 不是以上四种情况时，将 loglevel 设置为 LOG_LEVEL_DEBUG。
		default:
			loglevel = LOG_LEVEL_DEBUG;
			break;
	}

	// 使用 zabbix_log 函数，根据 loglevel 参数的值，输出一条日志。日志内容为 "IPC service: %s"，其中 %s 位置会被 msg 参数的值填充。
	zabbix_log(loglevel, "IPC service: %s", msg);
}

			loglevel = LOG_LEVEL_DEBUG;
			break;
		default:
			loglevel = LOG_LEVEL_DEBUG;
			break;
	}

	zabbix_log(loglevel, "IPC service: %s", msg);
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_service_init_libevent                                        *
 *                                                                            *
 * Purpose: initialize libevent library                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *注释详细说明：
 *
 *1. 定义一个名为ipc_service_init_libevent的静态函数，表示这个函数只在编译时被调用一次，用于初始化libevent库。
 *2. 使用event_set_log_callback()函数设置libevent的日志回调函数，这个回调函数可以在libevent运行时输出日志信息。
 *3. ipc_service_event_log_cb是日志回调函数，它在libevent库中起到记录日志的作用。当libevent运行时，发生的各种事件会通过这个回调函数进行日志记录。
 *4. 整个代码块的主要目的是初始化libevent库，并为libevent设置日志回调函数，以便在运行过程中记录相关事件。
 ******************************************************************************/
// 定义一个静态函数，用于初始化libevent库
static void ipc_service_init_libevent(void)
{
    // 设置libevent的日志回调函数
    event_set_log_callback(ipc_service_event_log_cb);
}

// 整个代码块的主要目的是初始化libevent库，并为libevent设置日志回调函数


/******************************************************************************
 *                                                                            *
 * Function: ipc_service_free_libevent                                        *
 *                                                                            *
 * Purpose: uninitialize libevent library                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是释放libevent库的相关资源。在这个函数中，没有看到实际的释放操作，可能是遗漏了部分代码。这个函数的作用是在程序运行结束后，确保libevent库所占用的资源得到正确释放。这样可以避免内存泄漏等问题。
 ******************************************************************************/
// 定义一个静态函数，用于释放libevent库的相关资源
static void ipc_service_free_libevent(void)
{
    // 这里可以理解为释放libevent库中使用到的内存空间
    // 由于这个函数是静态的，所以它只能在定义它的源文件中被调用
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个回调函数，用于处理客户端连接事件。当客户端连接到服务端时，该回调函数会被调用。在此函数中，首先将传入的参数转换为 zbx_ipc_service_t 类型的指针，然后忽略 fd 和 what 参数，因为它们在此处不需要使用。最后，调用 ipc_service_accept 函数，对客户端连接事件进行处理。整个代码块的功能可以概括为处理客户端连接事件并调用相应的事件处理函数。
 ******************************************************************************/
// 定义一个回调函数，用于处理客户端连接事件
static void ipc_service_client_connected_cb(evutil_socket_t fd, short what, void *arg)
{
	// 将传入的参数转换为 zbx_ipc_service_t 类型的指针
	zbx_ipc_service_t *service = (zbx_ipc_service_t *)arg;

	// 忽略 fd 和 what 参数，不需要使用它们
	ZBX_UNUSED(fd);
	ZBX_UNUSED(what);

	// 调用 ipc_service_accept 函数，处理客户端连接事件
	ipc_service_accept(service);
}

{
	zbx_ipc_service_t	*service = (zbx_ipc_service_t *)arg;

	ZBX_UNUSED(fd);
	ZBX_UNUSED(what);

	ipc_service_accept(service);
}

/******************************************************************************
 *                                                                            *
 * Function: ipc_service_timer_cb                                             *
 *                                                                            *
 * Purpose: timer callback                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个回调函数，用于处理IPC（进程间通信）中的定时器事件。当定时器事件触发时，这个回调函数会被调用。在函数内部，作者使用了`ZBX_UNUSED`宏来忽略传入的参数，因为在处理定时器事件时并不需要使用这些参数。
 ******************************************************************************/
// 定义一个回调函数，用于处理IPC（进程间通信）中的定时器事件
static void ipc_service_timer_cb(evutil_socket_t fd, short what, void *arg)
{
    // 忽略传入的参数，因为在回调函数中不需要使用它们
    ZBX_UNUSED(fd);
    ZBX_UNUSED(what);
    ZBX_UNUSED(arg);
}


/******************************************************************************
 *                                                                            *
 * Function: ipc_check_running_service                                        *
 *                                                                            *
 * Purpose: checks if an IPC service is already running                       *
 *                                                                            *
 * Parameters: service_name - [IN]                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查指定的服务是否在运行。函数ipc_check_running_service接受一个字符串参数service_name，用于表示要检查的服务名称。函数内部使用zbx_ipc_socket_open创建一个IPC套接字，并尝试连接到服务。如果连接成功，则关闭套接字并返回SUCCEED；如果连接失败，则释放错误信息并返回失败状态。
 ******************************************************************************/
// 定义一个静态函数，用于检查指定的服务是否在运行
static int ipc_check_running_service(const char *service_name)
{
	// 定义一个zbx_ipc_socket_t类型的变量csocket，用于存放IPC套接字
	zbx_ipc_socket_t	csocket;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_ipc_socket_open`的函数，该函数用于创建并连接到一个UNIX域套接字，以便与远程服务进行通信。如果连接成功，函数返回SUCCEED，否则返回FAIL。在连接过程中，如果超时或发生错误，函数会记录错误信息并退出。整个代码块包括错误处理和日志记录，以提高程序的稳定性和可维护性。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_socket_open";

// 定义结构体变量
struct sockaddr_un addr;
time_t start;
struct timespec ts = {0, 100000000};
const char *socket_path;

// 定义错误码
int ret = FAIL;

// 进入函数，记录日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

// 生成socket_path，若生成失败，记录错误信息并退出函数
if (NULL == (socket_path = ipc_make_path(service_name, error)))
{
    goto out;
}

// 创建UNIX域套接字
if (-1 == (csocket->fd = socket(AF_UNIX, SOCK_STREAM, 0)))
{
    *error = zbx_dsprintf(*error, "Cannot create client socket: %s.", zbx_strerror(errno));
    goto out;
}

// 初始化套接字地址
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;
memcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

// 开始计时
start = time(NULL);

// 循环尝试连接套接字，直到成功或超时
while (0 != connect(csocket->fd, (struct sockaddr*)&addr, sizeof(addr)))
{
    if (0 == timeout || time(NULL) - start > timeout)
    {
        *error = zbx_dsprintf(*error, "Cannot connect to service \"%s\": %s.", service_name,
                             zbx_strerror(errno));
        close(csocket->fd);
        goto out;
    }

    // 休眠一段时间后继续尝试连接
    nanosleep(&ts, NULL);
}

// 初始化接收缓冲区
csocket->rx_buffer_bytes = 0;
csocket->rx_buffer_offset = 0;

// 连接成功，设置返回码
ret = SUCCEED;

// 结束函数，记录日志
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

// 返回整个函数的执行结果
return ret;

// 错误处理标签，用于跳出循环
out:

	const char		*socket_path;
	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (NULL == (socket_path = ipc_make_path(service_name, error)))
		goto out;

	if (-1 == (csocket->fd = socket(AF_UNIX, SOCK_STREAM, 0)))
	{
		*error = zbx_dsprintf(*error, "Cannot create client socket: %s.", zbx_strerror(errno));
		goto out;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

	start = time(NULL);

	while (0 != connect(csocket->fd, (struct sockaddr*)&addr, sizeof(addr)))
	{
		if (0 == timeout || time(NULL) - start > timeout)
		{
			*error = zbx_dsprintf(*error, "Cannot connect to service \"%s\": %s.", service_name,
					zbx_strerror(errno));
			close(csocket->fd);
			goto out;
		}

		nanosleep(&ts, NULL);
	}

	csocket->rx_buffer_bytes = 0;
	csocket->rx_buffer_offset = 0;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_socket_close                                             *
 *                                                                            *
 * Purpose: closes socket to an IPC service                                   *
 *                                                                            *
 * Parameters: csocket - [IN/OUT] the IPC socket to close                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于关闭一个zbx_ipc_socket。函数接收一个zbx_ipc_socket_t类型的指针作为参数，如果该指针指向的socket文件描述符有效，则调用close()函数关闭socket，并将socket文件描述符设置为-1，表示已关闭。在整个过程中，通过记录日志来表示函数的执行情况。
 ******************************************************************************/
// 定义一个函数，用于关闭zbx_ipc_socket
// 参数：zbx_ipc_socket_t类型的指针，指向要关闭的socket
void zbx_ipc_socket_close(zbx_ipc_socket_t *csocket)
{
	// 定义一个字符串，用于存储函数名
	const char *__function_name = "zbx_ipc_socket_close";

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断csocket指向的socket文件描述符是否有效
	if (-1 != csocket->fd)
	{
		// 关闭socket文件描述符
		close(csocket->fd);

		// 将socket文件描述符设置为-1，表示已关闭
		csocket->fd = -1;
	}

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_ipc_socket_write`的函数，该函数用于通过IPC套接字发送消息。函数接收四个参数：一个指向`zbx_ipc_socket_t`结构体的指针`csocket`，一个`zbx_uint32_t`类型的代码`code`，一个指向`unsigned char`类型的指针`data`，以及一个`zbx_uint32_t`类型的消息大小`size`。函数首先调用`ipc_socket_write_message`发送消息，然后判断发送结果。如果发送成功，函数返回`SUCCEED`，否则返回`FAIL`。在函数入口和出口处，分别打印日志以记录函数的执行情况。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_socket_write";
int		ret;
zbx_uint32_t	size_sent;

// 进入函数，打印日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

// 调用ipc_socket_write_message函数，发送消息
if (SUCCEED == ipc_socket_write_message(csocket, code, data, size, &size_sent) &&
    // 判断发送消息是否成功，成功条件是发送的字节数等于期望的字节数加ZBX_IPC_HEADER_SIZE
    size_sent == size + ZBX_IPC_HEADER_SIZE)
{
    ret = SUCCEED;
}
else
    // 发送消息失败，返回FAIL
    ret = FAIL;

// 退出函数，打印日志
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

// 返回发送结果
return ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (SUCCEED == ipc_socket_write_message(csocket, code, data, size, &size_sent) &&
			size_sent == size + ZBX_IPC_HEADER_SIZE)
	{
		ret = SUCCEED;
	}
	else
/******************************************************************************
 * *
 *整个代码块的主要目的是从zbx_ipc_socket中读取一条消息，并将读取到的消息代码、消息大小和数据存储在zbx_ipc_message结构体中。如果读取到的消息不完整，则释放缓冲区并返回失败。读取成功后，根据日志级别输出调试信息。最后返回读取消息的成功与否。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_socket_read";
int		ret = FAIL;

// 定义接收数据的缓冲区和消息头的大小
zbx_uint32_t	rx_bytes = 0, header[2];
unsigned char	*data = NULL;

// 进入函数调试日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

// 调用ipc_socket_read_message()函数从套接字中读取消息，并将读取的数据存储在data缓冲区中，rx_bytes表示已读取的数据长度
if (SUCCEED != ipc_socket_read_message(csocket, header, &data, &rx_bytes))
{
    // 如果读取消息失败，跳转到out标签处
    goto out;
}

// 判断消息头中的标志位，判断消息是否完整
if (SUCCEED != ipc_message_is_completed(header, rx_bytes))
{
    // 如果消息不完整，释放data缓冲区，跳转到out标签处
    zbx_free(data);
    goto out;
}

// 提取消息代码、消息大小和数据指针
message->code = header[ZBX_IPC_MESSAGE_CODE];
message->size = header[ZBX_IPC_MESSAGE_SIZE];
message->data = data;

// 如果日志级别为TRACE，格式化消息并输出调试日志
if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
{
    char	*msg = NULL;

    zbx_ipc_message_format(message, &msg);

    zabbix_log(LOG_LEVEL_DEBUG, "%s() %s", __function_name, msg);

    zbx_free(msg);
}

// 设置返回值
ret = SUCCEED;

// 结束函数调试日志
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

// 返回函数结果
return ret;

	}

	message->code = header[ZBX_IPC_MESSAGE_CODE];
	message->size = header[ZBX_IPC_MESSAGE_SIZE];
	message->data = data;

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		char	*msg = NULL;

		zbx_ipc_message_format(message, &msg);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() %s", __function_name, msg);

		zbx_free(msg);
	}

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_message_free                                             *
 *                                                                            *
 * Purpose: frees the resources allocated to store IPC message data           *
 *                                                                            *
 * Parameters: message - [IN] the message to free                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx_ipc_message结构体的内存。当调用这个函数时，它会首先检查message指针是否为空，如果不为空，则依次释放message结构体中的数据内存和message结构体本身的内存。这样可以确保在使用完zbx_ipc_message结构体后，正确地释放其占用的内存资源。
 ******************************************************************************/
// 定义一个函数，用于释放zbx_ipc_message结构体的内存
void zbx_ipc_message_free(zbx_ipc_message_t *message)
{
	// 判断message指针是否为空，如果不为空，则执行以下操作
	if (NULL != message)
	{
		// 释放message结构体中的数据内存
		zbx_free(message->data);
		// 释放message结构体本身的内存
		zbx_free(message);
	}
}


/******************************************************************************
 * *
 *整个代码块的主要目的是清理IPC消息，通过调用zbx_free()函数释放消息结构体中分配的内存。这在进程间通信中非常重要，以确保资源正确释放，避免内存泄漏等问题。
 ******************************************************************************/
// 定义一个函数void zbx_ipc_message_clean(zbx_ipc_message_t *message)，这个函数的参数是一个指向zbx_ipc_message_t结构体的指针message。
// 函数名zbx_ipc_message_clean，根据名称可以猜测它的主要目的是清理（clean）IPC（Inter-Process Communication，进程间通信）消息。

void	zbx_ipc_message_clean(zbx_ipc_message_t *message)
{
	// 定义一个指针变量message指向的结构体zbx_ipc_message_t，这里省略了结构体的定义，实现在清理函数中。
	
	// 使用zbx_free()函数释放message结构体中的data成员所指向的内存空间。
	// 这里的意思是，在调用这个函数时，传入的message参数应该已经分配了内存，并且需要清理。
	// zbx_free()函数会自动计算data成员指向的内存地址，并释放该内存。
}

 *                                                                            *
 * Purpose: frees the resources allocated to store IPC message data           *
 *                                                                            *
 * Parameters: message - [IN] the message to clean                            *
 *                                                                            *
 ******************************************************************************/
void	zbx_ipc_message_clean(zbx_ipc_message_t *message)
{
	zbx_free(message->data);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_message_init                                             *
 *                                                                            *
 * Purpose: initializes IPC message                                           *
 *                                                                            *
 * Parameters: message - [IN] the message to initialize                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：初始化一个zbx_ipc_message_t类型的结构体变量。通过调用memset函数，将结构体变量的所有成员变量清零。这样，在使用这个结构体变量之前，对其进行了初始化，确保其成员变量具有合适的初始值。
 *
 *```
 ******************************************************************************/
// 定义一个函数，名为 zbx_ipc_message_init，接收一个zbx_ipc_message_t类型的指针作为参数
void zbx_ipc_message_init(zbx_ipc_message_t *message)
{
    // 使用memset函数，将message指向的内存区域清零，清零的范围是zbx_ipc_message_t类型的大小
    memset(message, 0, sizeof(zbx_ipc_message_t));
}


/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是格式化一个IPC消息数据。该函数接收一个zbx_ipc_message_t类型的指针（包含消息的代码、大小和数据）以及一个指向输出数据的指针。函数首先根据消息的大小分配足够的内存空间，然后遍历消息中的每个字节，将其格式化为两位十六进制字符，并在输出中添加分隔符。最后，确保输出字符串以空字符结尾。
 ******************************************************************************/
// 定义一个函数，用于格式化IPC消息数据
void zbx_ipc_message_format(const zbx_ipc_message_t *message, char **data)
{
    // 定义两个变量，用于存储数据分配大小和数据偏移量
    size_t data_alloc = ZBX_IPC_DATA_DUMP_SIZE * 4 + 32, data_offset = 0;
    zbx_uint32_t i, data_num;

    // 如果传入的消息指针为空，直接返回
    if (NULL == message)
        return;

    // 获取消息中的数据数量
    data_num = message->size;

    // 如果数据数量大于ZBX_IPC_DATA_DUMP_SIZE，则将其设置为ZBX_IPC_DATA_DUMP_SIZE
    if (ZBX_IPC_DATA_DUMP_SIZE < data_num)
        data_num = ZBX_IPC_DATA_DUMP_SIZE;

    // 为数据分配内存空间，并指向该内存空间的指针
    *data = (char *)zbx_malloc(*data, data_alloc);

    // 格式化输出消息的代码、大小和数据
    zbx_snprintf_alloc(data, &data_alloc, &data_offset, "code:%u size:%u data:", message->code, message->size);

    // 遍历数据中的每个字节
    for (i = 0; i < data_num; i++)
    {
        // 如果当前字节不是最后一个字节，则在输出中添加分隔符
        if (0 != i)
            zbx_strcpy_alloc(data, &data_alloc, &data_offset, (0 == (i & 7) ? " | " : " "));

        // 格式化输出当前字节，占两位十六进制字符
        zbx_snprintf_alloc(data, &data_alloc, &data_offset, "%02x", (int)message->data[i]);
    }

    // 确保数据字符串以空字符结尾
    (*data)[data_offset] = '\0';
}

	{
		if (0 != i)
			zbx_strcpy_alloc(data, &data_alloc, &data_offset, (0 == (i & 7) ? " | " : " "));

		zbx_snprintf_alloc(data, &data_alloc, &data_offset, "%02x", (int)message->data[i]);
	}

	(*data)[data_offset] = '\0';
}
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化IPC服务环境。该函数接收一个路径参数（path）和一个错误指针（error），判断给定的路径是否符合IPC服务环境的要求，如是否为目录、是否具有读写权限等。如果符合要求，将路径复制到ipc_path数组，并调用ipc_service_init_libevent()函数初始化libevent库。最后，返回成功或失败的结果。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_service_init_env";
struct stat	fs;
int		ret = FAIL;

// 打印调试日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s() path:%s", __function_name, path);

// 判断IPC服务环境是否已经初始化
if (0 != ipc_path_root_len)
{
    // 打印错误信息，并跳转到out标签
    *error = zbx_dsprintf(*error, "The IPC service environment has been already initialized with"
                            " root directory at \"%s\".", ipc_get_path());
    goto out;
}

// 获取指定路径的文件状态信息
if (0 != stat(path, &fs))
{
    // 打印错误信息，并跳转到out标签
    *error = zbx_dsprintf(*error, "Failed to stat the specified path \"%s\": %s.", path,
                            zbx_strerror(errno));
    goto out;
}

// 判断路径是否为目录
if (0 == S_ISDIR(fs.st_mode))
{
    // 打印错误信息，并跳转到out标签
    *error = zbx_dsprintf(*error, "The specified path \"%s\" is not a directory.", path);
    goto out;
}

// 判断是否具有读写权限
if (0 != access(path, W_OK | R_OK))
{
    // 打印错误信息，并跳转到out标签
    *error = zbx_dsprintf(*error, "Cannot access path \"%s\": %s.", path, zbx_strerror(errno));
    goto out;
}

// 保存IPC路径根目录长度
ipc_path_root_len = strlen(path);

// 检查IPC根路径长度是否合法
if (ZBX_IPC_PATH_MAX < ipc_path_root_len + 3)
{
    // 打印错误信息，并跳转到out标签
    *error = zbx_dsprintf(*error, "The IPC root path \"%s\" is too long.", path);
    goto out;
}

// 复制IPC路径到ipc_path数组
memcpy(ipc_path, path, ipc_path_root_len + 1);

// 去掉路径末尾的'/'，并循环直到ipc_path_root_len大于1
while (1 < ipc_path_root_len && '/' == ipc_path[ipc_path_root_len - 1])
    ipc_path[--ipc_path_root_len] = '\0';

// 初始化libevent库
ipc_service_init_libevent();

// 设置返回值
ret = SUCCEED;

out:
    // 打印调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回结果
    return ret;
}

	if (0 != ipc_path_root_len)
	{
		*error = zbx_dsprintf(*error, "The IPC service environment has been already initialized with"
				" root directory at \"%s\".", ipc_get_path());
		goto out;
	}

	if (0 != stat(path, &fs))
	{
		*error = zbx_dsprintf(*error, "Failed to stat the specified path \"%s\": %s.", path,
				zbx_strerror(errno));
		goto out;
	}

	if (0 == S_ISDIR(fs.st_mode))
	{
		*error = zbx_dsprintf(*error, "The specified path \"%s\" is not a directory.", path);
		goto out;
	}

	if (0 != access(path, W_OK | R_OK))
	{
		*error = zbx_dsprintf(*error, "Cannot access path \"%s\": %s.", path, zbx_strerror(errno));
		goto out;
	}

	ipc_path_root_len = strlen(path);
	if (ZBX_IPC_PATH_MAX < ipc_path_root_len + 3)
	{
		*error = zbx_dsprintf(*error, "The IPC root path \"%s\" is too long.", path);
		goto out;
	}

	memcpy(ipc_path, path, ipc_path_root_len + 1);

	while (1 < ipc_path_root_len && '/' == ipc_path[ipc_path_root_len - 1])
		ipc_path[--ipc_path_root_len] = '\0';

/******************************************************************************
 * *
 *整个代码块的主要目的是启动一个IPC服务。具体来说，这个函数做了以下事情：
 *
 *1. 初始化日志和错误指针。
 *2. 设置文件权限掩码。
 *3. 生成套接字文件路径。
 *4. 检查套接字文件是否存在且可写，如果被其他进程占用或已运行，则打印错误信息并退出。
 *5. 创建套接字。
 *6. 初始化套接字地址。
 *7. 绑定套接字。
 *8. 监听套接字。
 *9. 保存服务名。
 *10. 创建客户端列表和接收客户端消息的队列。
 *11. 初始化事件基。
 *12. 创建客户端连接事件。
 *13. 创建定时器事件。
 *14. 设置服务启动成功。
 *15. 恢复文件权限掩码。
 *16. 打印日志。
 *17. 返回服务启动结果。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_service_start";

// 定义套接字地址结构体
struct sockaddr_un addr;

// 定义错误指针
char **error;

// 初始化返回值
int ret = FAIL;

// 设置文件权限掩码
mode_t mode;

// 打印日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s() service:%s", __function_name, service_name);

// 设置文件权限掩码为077
mode = umask(077);

// 生成套接字文件路径
if (NULL == (socket_path = ipc_make_path(service_name, error)))
{
    // 打印错误信息并退出
    goto out;
}

// 检查套接字文件是否存在且可写
if (0 == access(socket_path, F_OK))
{
    // 如果文件被其他进程占用，打印错误信息并退出
    if (0 != access(socket_path, W_OK))
    {
        *error = zbx_dsprintf(*error, "The file \"%s\" is used by another process.", socket_path);
        goto out;
    }

    // 如果服务已运行，打印错误信息并退出
    if (SUCCEED == ipc_check_running_service(service_name))
    {
        *error = zbx_dsprintf(*error, "\"%s\" service is already running.", service_name);
        goto out;
    }

    // 删除套接字文件
    unlink(socket_path);
}

// 创建套接字
if (-1 == (service->fd = socket(AF_UNIX, SOCK_STREAM, 0)))
{
    // 打印错误信息并退出
    *error = zbx_dsprintf(*error, "Cannot create socket: %s.", zbx_strerror(errno));
    goto out;
}

// 初始化套接字地址
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;
memcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

// 绑定套接字
if (0 != bind(service->fd, (struct sockaddr*)&addr, sizeof(addr)))
{
    // 打印错误信息并退出
    *error = zbx_dsprintf(*error, "Cannot bind socket to \"%s\": %s.", socket_path, zbx_strerror(errno));
    goto out;
}

// 监听套接字
if (0 != listen(service->fd, SOMAXCONN))
{
    // 打印错误信息并退出
    *error = zbx_dsprintf(*error, "Cannot listen socket: %s.", zbx_strerror(errno));
    goto out;
}

// 保存服务名
service->path = zbx_strdup(NULL, service_name);

// 创建客户端列表
zbx_vector_ptr_create(&service->clients);

// 创建接收客户端消息的队列
zbx_queue_ptr_create(&service->clients_recv);

// 初始化事件基
service->ev = event_base_new();

// 创建客户端连接事件
service->ev_listener = event_new(service->ev, service->fd, EV_READ | EV_PERSIST,
                                ipc_service_client_connected_cb, service);
event_add(service->ev_listener, NULL);

// 创建定时器事件
service->ev_timer = event_new(service->ev, -1, 0, ipc_service_timer_cb, service);

// 设置服务启动成功
ret = SUCCEED;

out:
// 恢复文件权限掩码
umask(mode);

// 打印日志
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

return ret;

		goto out;

	if (0 == access(socket_path, F_OK))
	{
		if (0 != access(socket_path, W_OK))
		{
			*error = zbx_dsprintf(*error, "The file \"%s\" is used by another process.", socket_path);
			goto out;
		}

		if (SUCCEED == ipc_check_running_service(service_name))
		{
			*error = zbx_dsprintf(*error, "\"%s\" service is already running.", service_name);
			goto out;
		}

		unlink(socket_path);
	}

	if (-1 == (service->fd = socket(AF_UNIX, SOCK_STREAM, 0)))
	{
		*error = zbx_dsprintf(*error, "Cannot create socket: %s.", zbx_strerror(errno));
		goto out;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

	if (0 != bind(service->fd, (struct sockaddr*)&addr, sizeof(addr)))
	{
		*error = zbx_dsprintf(*error, "Cannot bind socket to \"%s\": %s.", socket_path, zbx_strerror(errno));
		goto out;
	}

	if (0 != listen(service->fd, SOMAXCONN))
	{
		*error = zbx_dsprintf(*error, "Cannot listen socket: %s.", zbx_strerror(errno));
		goto out;
	}

	service->path = zbx_strdup(NULL, service_name);
	zbx_vector_ptr_create(&service->clients);
	zbx_queue_ptr_create(&service->clients_recv);

	service->ev = event_base_new();
	service->ev_listener = event_new(service->ev, service->fd, EV_READ | EV_PERSIST,
			ipc_service_client_connected_cb, service);
	event_add(service->ev_listener, NULL);

	service->ev_timer = event_new(service->ev, -1, 0, ipc_service_timer_cb, service);

	ret = SUCCEED;
out:
	umask(mode);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_service_close                                            *
 *                                                                            *
 * Purpose: closes IPC service and frees the resources allocated by it        *
 *                                                                            *
 * Parameters: service - [IN/OUT] the IPC service                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是关闭一个IPC服务，具体操作如下：
 *1. 关闭服务对应的文件描述符（fd）。
 *2. 遍历服务中的客户端列表，释放每个客户端的结构体内存。
 *3. 释放服务路径内存。
 *4. 销毁客户端列表和接收客户端消息的队列。
 *5. 释放服务中的定时器事件、监听器事件和事件基座。
 *6. 打印日志，表示进入和结束函数__function_name()。
 ******************************************************************************/
// 定义一个函数void zbx_ipc_service_close(zbx_ipc_service_t *service)，用于关闭一个IPC服务
void	zbx_ipc_service_close(zbx_ipc_service_t *service)
/******************************************************************************
 * /
 ******************************************************************************/* ******************************************************************************
 * 函数名：zbx_ipc_service_recv
 * 参数：
 *   zbx_ipc_service_t *service - IPC服务结构体指针
 *   int timeout - 超时时间
 *   zbx_ipc_client_t **client - 客户端指针，接收后指向已连接的客户端
 *   zbx_ipc_message_t **message - 消息指针，接收后指向接收到的消息
 * 返回值：
 *   成功：ZBX_IPC_RECV_IMMEDIATE 或 ZBX_IPC_RECV_WAIT 或 ZBX_IPC_RECV_TIMEOUT
 *   失败：其他错误码
 * 注释：
 *   该函数用于处理IPC服务中的接收事件，即处理接收到的客户端请求消息。
 *   当接收到消息时，函数返回ZBX_IPC_RECV_WAIT；
 *   当超时未收到消息时，函数返回ZBX_IPC_RECV_TIMEOUT；
 *   若超时时间为0或无限大，函数将持续等待直到接收到消息或超时。
 *******************************************************************************/

int	zbx_ipc_service_recv(zbx_ipc_service_t *service, int timeout, zbx_ipc_client_t **client,
		zbx_ipc_message_t **message)
{
	/* 定义日志级别 */
	const char	*__function_name = "zbx_ipc_service_recv";

	/* 初始化变量 */
	int		ret, flags;

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() timeout:%d", __function_name, timeout);

	/* 判断超时时间是否为0，若不是，设置定时器 */
	if (timeout != 0 && SUCCEED == zbx_queue_ptr_empty(&service->clients_recv))
	{
		if (ZBX_IPC_WAIT_FOREVER != timeout)
		{
			struct timeval	tv = {timeout, 0};
			evtimer_add(service->ev_timer, &tv);
		}
		flags = EVLOOP_ONCE;
	}
	else
		flags = EVLOOP_NONBLOCK;

	/* 循环监听事件 */
	event_base_loop(service->ev, flags);

	/* 获取客户端 */
	if (NULL != (*client = ipc_service_pop_client(service)))
	{
		/* 获取消息 */
		if (NULL != (*message = (zbx_ipc_message_t *)zbx_queue_ptr_pop(&(*client)->rx_queue)))
		{
			/* 打印调试日志 */
			if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
			{
				char	*data = NULL;

				zbx_ipc_message_format(*message, &data);
				zabbix_log(LOG_LEVEL_DEBUG, "%s() %s", __function_name, data);

				zbx_free(data);
			}

			/* 将客户端重新加入队列 */
			ipc_service_push_client(service, *client);
			/* 增加客户端引用计数 */
			zbx_ipc_client_addref(*client);
		}

		/* 判断返回值 */
		if (EVLOOP_NONBLOCK == flags)
			ret = ZBX_IPC_RECV_IMMEDIATE;
		else
			ret = ZBX_IPC_RECV_WAIT;
	}
	else
	{	/* 未接收到消息，返回超时状态 */
		ret = ZBX_IPC_RECV_TIMEOUT;
		*client = NULL;
		*message = NULL;
	}

	/* 删除定时器 */
	evtimer_del(service->ev_timer);

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, ret);

	return ret;
}
 *               ZBX_IPC_RECV_TIMEOUT   - returned after timeout expired      *
 *                                                                            *
 ******************************************************************************/
int	zbx_ipc_service_recv(zbx_ipc_service_t *service, int timeout, zbx_ipc_client_t **client,
		zbx_ipc_message_t **message)
{
	const char	*__function_name = "zbx_ipc_service_recv";

	int		ret, flags;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() timeout:%d", __function_name, timeout);

	if (timeout != 0 && SUCCEED == zbx_queue_ptr_empty(&service->clients_recv))
	{
		if (ZBX_IPC_WAIT_FOREVER != timeout)
		{
			struct timeval	tv = {timeout, 0};
			evtimer_add(service->ev_timer, &tv);
		}
		flags = EVLOOP_ONCE;
	}
	else
		flags = EVLOOP_NONBLOCK;

	event_base_loop(service->ev, flags);

	if (NULL != (*client = ipc_service_pop_client(service)))
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_ipc_client_send`的函数，该函数用于向客户端发送数据。函数接收四个参数：一个`zbx_ipc_client_t`类型的指针`client`，一个`zbx_uint32_t`类型的`code`，一个指向`unsigned char`类型的指针`data`，以及一个`zbx_uint32_t`类型的`size`。函数返回一个整数类型的结果。
 *
 *在函数内部，首先创建一个日志记录，表示进入函数并记录客户端ID。然后，检查发送缓冲区是否已有数据。如果有数据，则创建一个新的消息对象，将其添加到发送队列中，并发送成功，返回SUCCEED。
 *
 *如果没有数据，则尝试将消息发送到socket。如果发送失败，跳转到out标签，表示发送失败。如果发送成功，检查发送的数据长度是否符合预期。如果不符合，则重新构建发送缓冲区，并将事件添加到发送队列中。最后，发送成功，返回SUCCEED。在整个过程中，记录日志以跟踪函数的执行情况。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_client_send";
zbx_uint32_t		tx_size = 0;
zbx_ipc_message_t	*message;
int			ret = FAIL;

// 记录日志，表示进入函数，客户端ID
zabbix_log(LOG_LEVEL_DEBUG, "In %s() clientid:" ZBX_FS_UI64, __function_name, client->id);

// 如果客户端发送缓冲区已有数据，创建一个新的消息对象，将其添加到发送队列中，发送成功，返回SUCCEED
if (0 != client->tx_bytes)
{
    message = ipc_message_create(code, data, size);
    zbx_queue_ptr_push(&client->tx_queue, message);
    ret = SUCCEED;
    goto out;
}

// 如果发送消息到socket失败，跳转到out标签，表示发送失败
if (FAIL == ipc_socket_write_message(&client->csocket, code, data, size, &tx_size))
    goto out;

// 检查发送的数据长度是否符合预期，如果不符合，则重新构建发送缓冲区
if (tx_size != ZBX_IPC_HEADER_SIZE + size)
{
    client->tx_header[ZBX_IPC_MESSAGE_CODE] = code;
    client->tx_header[ZBX_IPC_MESSAGE_SIZE] = size;
    client->tx_data = (unsigned char *)zbx_malloc(NULL, size);
    memcpy(client->tx_data, data, size);
    client->tx_bytes = ZBX_IPC_HEADER_SIZE + size - tx_size;
    event_add(client->tx_event, NULL);
}

// 发送成功，返回SUCCEED
ret = SUCCEED;

// 记录日志，表示函数结束，输出发送结果
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

// 返回发送结果
return ret;

 * Purpose: Sends IPC message to client                                       *
 *                                                                            *
 * Parameters: client - [IN] the IPC client                                   *
 *             code   - [IN] the message code                                 *
 *             data   - [IN] the data                                         *
 *             size   - [IN] the data size                                    *
 *                                                                            *
 * Comments: If data can't be written directly to socket (buffer full) then   *
 *           the message is queued and sent during zbx_ipc_service_recv()     *
 *           messaging loop whenever socket becomes ready.                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_ipc_client_send(zbx_ipc_client_t *client, zbx_uint32_t code, const unsigned char *data, zbx_uint32_t size)
{
	const char		*__function_name = "zbx_ipc_client_send";
	zbx_uint32_t		tx_size = 0;
	zbx_ipc_message_t	*message;
	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() clientid:" ZBX_FS_UI64, __function_name, client->id);

	if (0 != client->tx_bytes)
	{
		message = ipc_message_create(code, data, size);
		zbx_queue_ptr_push(&client->tx_queue, message);
		ret = SUCCEED;
		goto out;
	}

	if (FAIL == ipc_socket_write_message(&client->csocket, code, data, size, &tx_size))
		goto out;

	if (tx_size != ZBX_IPC_HEADER_SIZE + size)
	{
		client->tx_header[ZBX_IPC_MESSAGE_CODE] = code;
		client->tx_header[ZBX_IPC_MESSAGE_SIZE] = size;
		client->tx_data = (unsigned char *)zbx_malloc(NULL, size);
		memcpy(client->tx_data, data, size);
		client->tx_bytes = ZBX_IPC_HEADER_SIZE + size - tx_size;
		event_add(client->tx_event, NULL);
	}

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_client_close                                             *
 *                                                                            *
 * Purpose: closes client socket and frees resources allocated for client     *
 *                                                                            *
 * Parameters: client - [IN] the IPC client                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是关闭一个客户端的连接，并释放相关资源。具体步骤如下：
 *
 *1. 记录函数入口日志。
 *2. 释放客户端的事件。
 *3. 关闭客户端的socket连接。
 *4. 从服务中移除客户端。
 *5. 从服务器的接收客户端队列中移除客户端。
 *6. 释放客户端资源。
 *7. 记录函数出口日志。
 ******************************************************************************/
void	zbx_ipc_client_close(zbx_ipc_client_t *client)
{                           // 定义一个函数zbx_ipc_client_close，传入参数为一个zbx_ipc_client_t类型的指针
{
	const char	*__function_name = "zbx_ipc_client_close";

	// 使用zabbix_log记录函数入口日志，LOG_LEVEL_DEBUG表示记录调试日志，__function_name为日志标签
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 调用ipc_client_free_events函数，释放client所关联的事件
	ipc_client_free_events(client);

	// 调用zbx_ipc_socket_close函数，关闭client的socket连接
	zbx_ipc_socket_close(&client->csocket);

	// 调用ipc_service_remove_client函数，从服务中移除client
	ipc_service_remove_client(client->service, client);

	// 调用zbx_queue_ptr_remove_value函数，从服务器的接收客户端队列中移除client
	zbx_queue_ptr_remove_value(&client->service->clients_recv, client);

	// 调用zbx_ipc_client_release函数，释放client资源
	zbx_ipc_client_release(client);

	// 使用zabbix_log记录函数出口日志，LOG_LEVEL_DEBUG表示记录调试日志，__function_name为日志标签
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *这块代码的主要目的是：增加客户端的引用计数。当其他模块需要使用这个客户端时，会调用这个函数，使得客户端的引用计数增加。这样做可以确保在多个模块共享同一个客户端时，客户端的生命周期能够得到正确管理。
 ******************************************************************************/
// 定义一个函数，名为 zbx_ipc_client_addref，参数为一个指向 zbx_ipc_client_t 类型的指针 client。
/******************************************************************************
 * *
 *这段代码的主要目的是：当zbx_ipc_client_t类型的指针client不再被引用时，释放其所占用的资源。
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个异步套接字打开功能，该功能用于后续进行异步通信。代码首先初始化相关结构体，然后尝试打开IPC客户端套接字。成功打开后，将套接字设置为非阻塞模式，并创建相关事件用于处理接收和发送数据。最后，返回套接字打开状态。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_async_socket_open";
int			ret = FAIL, flags;

// 打印日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

// 初始化asocket结构体
memset(asocket, 0, sizeof(zbx_ipc_async_socket_t));

// 分配内存并初始化asocket->client
asocket->client = (zbx_ipc_client_t *)zbx_malloc(NULL, sizeof(zbx_ipc_client_t));
memset(asocket->client, 0, sizeof(zbx_ipc_client_t));

// 尝试打开IPC客户端套接字
if (SUCCEED != zbx_ipc_socket_open(&asocket->client->csocket, service_name, timeout, error))
{
    // 失败则释放内存并退出
    zbx_free(asocket->client);
    goto out;
}

// 获取IPC客户端套接字的标志位
if (-1 == (flags = fcntl(asocket->client->csocket.fd, F_GETFL, 0)))
{
    // 打印日志并退出
    zabbix_log(LOG_LEVEL_CRIT, "cannot get IPC client socket flags");
    exit(EXIT_FAILURE);
}

// 设置套接字为非阻塞模式
if (-1 == fcntl(asocket->client->csocket.fd, F_SETFL, flags | O_NONBLOCK))
{
    // 打印日志并退出
    zabbix_log(LOG_LEVEL_CRIT, "cannot set non-blocking mode for IPC client socket");
    exit(EXIT_FAILURE);
}

// 创建事件基
asocket->ev = event_base_new();

// 创建读事件，用于处理接收数据
asocket->ev_timer = event_new(asocket->ev, -1, 0, ipc_async_socket_timer_cb, asocket);
asocket->client->rx_event = event_new(asocket->ev, asocket->client->csocket.fd, EV_READ | EV_PERSIST,
                                      ipc_async_socket_read_event_cb, (void *)asocket);

// 创建写事件，用于处理发送数据
asocket->client->tx_event = event_new(asocket->ev, asocket->client->csocket.fd, EV_WRITE | EV_PERSIST,
                                      ipc_async_socket_write_event_cb, (void *)asocket);

// 添加事件到事件基
event_add(asocket->client->rx_event, NULL);

// 设置asocket状态
asocket->state = ZBX_IPC_ASYNC_SOCKET_STATE_NONE;

// 成功打开套接字
ret = SUCCEED;

out:
    // 打印日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
    return ret;
}

    return (NULL == client->rx_event ? FAIL : SUCCEED);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_async_socket_open                                        *
 *                                                                            *
 * Purpose: opens asynchronous socket to IPC service client                   *
 *                                                                            *
 * Parameters: client       - [OUT] the IPC service client                    *
 *             service_name - [IN] the IPC service name                       *
 *             timeout      - [IN] the connection timeout                     *
 *             error        - [OUT] the error message                         *
 *                                                                            *
 * Return value: SUCCEED - the socket was successfully opened                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_ipc_async_socket_open(zbx_ipc_async_socket_t *asocket, const char *service_name, int timeout, char **error)
{
	const char		*__function_name = "zbx_ipc_async_socket_open";
	int			ret = FAIL, flags;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	memset(asocket, 0, sizeof(zbx_ipc_async_socket_t));
	asocket->client = (zbx_ipc_client_t *)zbx_malloc(NULL, sizeof(zbx_ipc_client_t));
	memset(asocket->client, 0, sizeof(zbx_ipc_client_t));

	if (SUCCEED != zbx_ipc_socket_open(&asocket->client->csocket, service_name, timeout, error))
	{
		zbx_free(asocket->client);
		goto out;
	}

	if (-1 == (flags = fcntl(asocket->client->csocket.fd, F_GETFL, 0)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot get IPC client socket flags");
		exit(EXIT_FAILURE);
	}

	if (-1 == fcntl(asocket->client->csocket.fd, F_SETFL, flags | O_NONBLOCK))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot set non-blocking mode for IPC client socket");
		exit(EXIT_FAILURE);
	}

	asocket->ev = event_base_new();
	asocket->ev_timer = event_new(asocket->ev, -1, 0, ipc_async_socket_timer_cb, asocket);
	asocket->client->rx_event = event_new(asocket->ev, asocket->client->csocket.fd, EV_READ | EV_PERSIST,
			ipc_async_socket_read_event_cb, (void *)asocket);
	asocket->client->tx_event = event_new(asocket->ev, asocket->client->csocket.fd, EV_WRITE | EV_PERSIST,
			ipc_async_socket_write_event_cb, (void *)asocket);
	event_add(asocket->client->rx_event, NULL);

	asocket->state = ZBX_IPC_ASYNC_SOCKET_STATE_NONE;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
	return ret;
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *这块代码的主要目的是用于关闭一个异步套接字。函数接收一个`zbx_ipc_async_socket_t`类型的指针作为参数。在函数内部，首先释放异步套接字所属的客户端、时间事件和基事件。然后记录日志，表示进入和结束该函数。整个函数的执行过程较为简单，主要是进行资源释放。
 ******************************************************************************/
// 定义一个函数，用于关闭异步套接字
void zbx_ipc_async_socket_close(zbx_ipc_async_socket_t *asocket)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "zbx_ipc_async_socket_close";

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 释放异步套接字所属的客户端
    ipc_client_free(asocket->client);

    // 释放异步套接字的时间事件
    event_free(asocket->ev_timer);

    // 释放异步套接字的基事件
    event_base_free(asocket->ev);

    // 记录日志，表示结束该函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	ipc_client_free(asocket->client);

	event_free(asocket->ev_timer);
	event_base_free(asocket->ev);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_async_socket_send                                        *
 *                                                                            *
 * Purpose: Sends message through asynchronous IPC socket                     *
 *                                                                            *
 * Parameters: asocket - [IN] the asynchronous IPC socket                     *
 *             code    - [IN] the message code                                *
 *             data    - [IN] the data                                        *
 *             size    - [IN] the data size                                   *
 *                                                                            *
 * Comments: If data can't be written directly to socket (buffer full) then   *
 *           the message is queued and sent during zbx_ipc_async_socket_recv()*
 *           or zbx_ipc_async_socket_flush() functions whenever socket becomes*
 *           ready.                                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_ipc_async_socket_send 的函数，该函数通过异步套接字将数据发送给客户端。函数传入四个参数，分别为套接字客户端、代码、数据和数据大小。函数首先记录调用日志，然后调用 zbx_ipc_client_send 函数发送数据，最后记录发送结果并返回。
 ******************************************************************************/
// 定义一个名为 zbx_ipc_async_socket_send 的函数，传入参数为一个 zbx_ipc_async_socket_t 类型的指针、一个 zbx_uint32_t 类型的代码、一个指向 unsigned char 类型的指针（数据）和一个 zbx_uint32_t 类型的数据大小。

int zbx_ipc_async_socket_send(zbx_ipc_async_socket_t *asocket, zbx_uint32_t code, const unsigned char *data, zbx_uint32_t size)
{
	// 定义一个常量字符串，用于表示函数名
	const char *__function_name = "zbx_ipc_async_socket_send";
	// 定义一个变量，用于存储函数返回值，初始值为 FAIL
	int			ret = FAIL;

	// 记录函数调用日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 调用 zbx_ipc_client_send 函数，将数据通过异步套接字发送给客户端，传入参数分别为套接字客户端、代码、数据和数据大小
	ret = zbx_ipc_client_send(asocket->client, code, data, size);

	// 记录函数调用日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回 zbx_ipc_client_send 函数的返回值
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_async_socket_recv                                        *
 *                                                                            *
 * Purpose: receives message through asynchronous IPC socket                  *
 *                                                                            *
 * Parameters: asocket - [IN] the asynchronous IPC socket                     *
 *             timeout - [IN] the timeout in seconds, 0 is used for           *
 *                            nonblocking call and ZBX_IPC_WAIT_FOREVER is    *
 *                            used for blocking call without timeout          *
 *             message - [OUT] the received message or NULL if the client     *
 *                             connection was closed.                         *
/******************************************************************************
 * *
 *整个代码块的主要目的是用于异步套接字接收数据。该函数接收一个zbx_ipc_async_socket_t类型的指针，一个超时时间，以及一个zbx_ipc_message_t类型的指针。在函数内部，首先检查消息队列是否为空，如果为空且超时不为0，则设置定时器。接着，根据套接字状态和超时设置，循环接收事件。当接收到消息时，对其进行格式化并打印。最后，根据是否收到消息或套接字状态是否正常，返回函数结果。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_ipc_async_socket_recv";

// 定义变量，用于存储返回值、 flags、以及接收到的消息
int ret, flags;
zbx_ipc_message_t *message;

// 记录日志，表示进入函数，并打印超时时间
zabbix_log(LOG_LEVEL_DEBUG, "In %s() timeout:%d", __function_name, timeout);

// 判断消息队列是否为空，如果为空且超时不为0，则设置定时器
if (timeout != 0 && SUCCEED == zbx_queue_ptr_empty(&asocket->client->rx_queue))
{
    if (ZBX_IPC_WAIT_FOREVER != timeout)
    {
        struct timeval tv = {timeout, 0};
        evtimer_add(asocket->ev_timer, &tv);
    }
    flags = EVLOOP_ONCE;
}
else
    flags = EVLOOP_NONBLOCK;

// 如果套接字状态正常，则循环接收事件
if (ZBX_IPC_ASYNC_SOCKET_STATE_ERROR != asocket->state)
    event_base_loop(asocket->ev, flags);

// 获取消息队列中的消息，并将其指针指向消息结构体
if (NULL != (*message = (zbx_ipc_message_t *)zbx_queue_ptr_pop(&asocket->client->rx_queue)))
{
    // 如果日志级别允许，格式化消息并打印
    if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
    {
        char *data = NULL;

        zbx_ipc_message_format(*message, &data);
        zabbix_log(LOG_LEVEL_DEBUG, "%s() %s", __function_name, data);

        zbx_free(data);
    }
}

// 判断是否收到消息或套接字状态是否正常，决定返回值
if (NULL != *message || ZBX_IPC_ASYNC_SOCKET_STATE_ERROR != asocket->state)
    ret = SUCCEED;
else
    ret = FAIL;

// 删除定时器
evtimer_del(asocket->ev_timer);

// 记录日志，表示函数结束，并打印返回值
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, ret);

// 返回函数结果
return ret;


	if (NULL != *message || ZBX_IPC_ASYNC_SOCKET_STATE_ERROR != asocket->state)
		ret = SUCCEED;
	else
		ret = FAIL;

	evtimer_del(asocket->ev_timer);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, ret);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_async_socket_flush                                       *
 *                                                                            *
 * Purpose: flushes unsent through asynchronous IPC socket                    *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是：处理套接字数据发送缓冲区内的数据，并在超时时间内等待客户端连接成功。如果连接成功，则将套接字状态重置为正常状态，并返回操作成功。否则，直接返回操作失败。在整个过程中，使用了事件循环和定时器来控制超时等待。
 ******************************************************************************/
// 定义函数名
const char *__function_name = "zbx_ipc_async_socket_flush";

// 定义变量
int		ret = FAIL, flags;

// 打印日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s() timeout:%d", __function_name, timeout);

// 判断发送缓冲区是否有数据，如果没有，直接返回成功
if (0 == asocket->client->tx_bytes)
{
    ret = SUCCEED;
    goto out;
}

// 判断套接字状态，如果为错误状态，直接退出
if (ZBX_IPC_ASYNC_SOCKET_STATE_ERROR == asocket->state)
    goto out;

// 重置套接字状态
asocket->state = ZBX_IPC_ASYNC_SOCKET_STATE_NONE;

// 如果有超时时间
if (0 != timeout)
{
    // 如果超时时间不是无限等待
    if (ZBX_IPC_WAIT_FOREVER != timeout)
    {
        struct timeval	tv = {timeout, 0};
        evtimer_add(asocket->ev_timer, &tv);
    }
    // 设置事件循环标志
    flags = EVLOOP_ONCE;
}
else
    flags = EVLOOP_NONBLOCK;

// 循环等待事件
do
{
    event_base_loop(asocket->ev, flags);

    // 检查客户端是否连接成功
    if (SUCCEED != zbx_ipc_client_connected(asocket->client))
        goto out;
}
while (0 != timeout && 0 != asocket->client->tx_bytes && ZBX_IPC_ASYNC_SOCKET_STATE_NONE == asocket->state);

// 如果套接字状态不是错误状态
if (ZBX_IPC_ASYNC_SOCKET_STATE_ERROR != asocket->state)
{
    // 设置操作成功，并重置客户端状态
    ret = SUCCEED;
    asocket->state = ZBX_IPC_CLIENT_STATE_NONE;
}

// 退出循环
goto out;

// 删除定时器
evtimer_del(asocket->ev_timer);

// 打印日志
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, ret);

// 返回操作结果
return ret;

	if (ZBX_IPC_ASYNC_SOCKET_STATE_ERROR != asocket->state)
	{
		ret = SUCCEED;
		asocket->state = ZBX_IPC_CLIENT_STATE_NONE;
	}
out:
	evtimer_del(asocket->ev_timer);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, ret);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipc_async_socket_check_unsent                                *
 *                                                                            *
 * Purpose: checks if there are data to be sent                               *
 *                                                                            *
 * Parameters: client  - [IN] the IPC service client                          *
 *                                                                            *
 * Return value: SUCCEED - there are messages queued to be sent               *
 *               FAIL    - all data has been sent                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查异步套接字的状态，判断数据是否已发送。具体来说，它比较`asocket->client->tx_bytes`的值和0的大小。如果`tx_bytes`为0，说明数据尚未发送，返回FAIL表示失败；如果`tx_bytes`不为0，说明数据已经发送，返回SUCCEED表示成功。
 ******************************************************************************/
// 定义一个函数zbx_ipc_async_socket_check_unsent，接收一个zbx_ipc_async_socket_t类型的指针作为参数
int zbx_ipc_async_socket_check_unsent(zbx_ipc_async_socket_t *asocket)
{
    // 判断asocket->client->tx_bytes的值是否为0
    if (0 == asocket->client->tx_bytes)
    {
        // 如果tx_bytes为0，返回FAIL，表示失败
        return FAIL;
    }
    // 如果tx_bytes不为0，返回SUCCEED，表示成功
    else
    {
        return SUCCEED;
    }
}



#endif
