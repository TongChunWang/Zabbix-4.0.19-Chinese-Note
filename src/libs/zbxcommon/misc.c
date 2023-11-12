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
#include "log.h"
#include "setproctitle.h"

/* scheduler support */

#define ZBX_SCHEDULER_FILTER_DAY	1
#define ZBX_SCHEDULER_FILTER_HOUR	2
#define ZBX_SCHEDULER_FILTER_MINUTE	3
#define ZBX_SCHEDULER_FILTER_SECOND	4

typedef struct
{
	int	start_day;	/* day of week when period starts */
	int	end_day;	/* day of week when period ends, included */
	int	start_time;	/* number of seconds from the beginning of the day when period starts */
	int	end_time;	/* number of seconds from the beginning of the day when period ends, not included */
}
zbx_time_period_t;

typedef struct zbx_flexible_interval
{
	zbx_time_period_t		period;
	int				delay;

	struct zbx_flexible_interval	*next;
}
zbx_flexible_interval_t;

typedef struct zbx_scheduler_filter
{
	int				start;
	int				end;
	int				step;

	struct zbx_scheduler_filter	*next;
}
zbx_scheduler_filter_t;

typedef struct zbx_scheduler_interval
{
	zbx_scheduler_filter_t		*mdays;
	zbx_scheduler_filter_t		*wdays;
	zbx_scheduler_filter_t		*hours;
	zbx_scheduler_filter_t		*minutes;
	zbx_scheduler_filter_t		*seconds;

	int				filter_level;

	struct zbx_scheduler_interval	*next;
}
zbx_scheduler_interval_t;

struct zbx_custom_interval
{
	zbx_flexible_interval_t		*flexible;
	zbx_scheduler_interval_t	*scheduling;
};

const int	INTERFACE_TYPE_PRIORITY[INTERFACE_TYPE_COUNT] =
{
	INTERFACE_TYPE_AGENT,
	INTERFACE_TYPE_SNMP,
	INTERFACE_TYPE_JMX,
	INTERFACE_TYPE_IPMI
};

static ZBX_THREAD_LOCAL volatile sig_atomic_t	zbx_timed_out;	/* 0 - no timeout occurred, 1 - SIGALRM took place */

#ifdef _WINDOWS

char	ZABBIX_SERVICE_NAME[ZBX_SERVICE_NAME_LEN] = APPLICATION_NAME;
char	ZABBIX_EVENT_SOURCE[ZBX_SERVICE_NAME_LEN] = APPLICATION_NAME;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为__zbx_stat的函数，该函数接收一个UTF-8编码的路径和一个zbx_stat_t类型的指针，用于存储文件状态信息。函数首先将路径转换为宽字符编码，然后调用_wstat64函数获取文件状态信息。如果返回值为-1，表示操作失败。接下来，判断文件是否为目录或者文件大小是否不为0，如果是，则跳过该文件。针对符号链接的特殊情况，函数尝试打开文件并使用fstat64重新获取文件状态信息。最后，释放内存并返回函数执行结果。
 ******************************************************************************/
// 定义一个C语言函数，名为__zbx_stat，接收两个参数：一个const char类型的指针path，和一个zbx_stat_t类型的指针buf。
int	__zbx_stat(const char *path, zbx_stat_t *buf)
{
	// 定义一个整型变量ret，用于存储函数返回值，以及一个整型变量fd，用于存储文件描述符。
	int	ret, fd;
	// 定义一个宽字符指针wpath，用于存储路径的宽字符版本。
	wchar_t	*wpath;

	// 将path的UTF-8编码转换为宽字符编码，并存储在wpath指向的内存区域。
	wpath = zbx_utf8_to_unicode(path);

	// 调用_wstat64函数，将wpath对应的文件状态信息存储在buf指向的内存区域。如果返回值为-1，表示操作失败，跳转到out标签。
	if (-1 == (ret = _wstat64(wpath, buf)))
		goto out;

	// 判断buf中的st_mode是否为目录（S_ISDIR()），或者st_size是否不为0。如果是，表示文件不符合要求，跳转到out标签。
	if (0 != S_ISDIR(buf->st_mode) || 0 != buf->st_size)
		goto out;

	// 以下代码段是为了处理符号链接的特殊情况：_wstat64返回0表示文件大小为0，但实际上可能是符号链接。
	/* In the case of symlinks _wstat64 returns zero file size.   */
	/* Try to work around it by opening the file and using fstat. */

	// 初始化ret为-1，表示后续操作可能会失败。
	ret = -1;

	// 如果不等于-1，打开wpath指向的文件，并使用fstat64获取文件状态信息。
	if (-1 != (fd = _wopen(wpath, O_RDONLY)))
	{
		// 将fstat64的返回值存储在ret中，然后关闭文件。
		ret = _fstat64(fd, buf);
		_close(fd);
	}
out:
	// 释放wpath指向的内存。
	zbx_free(wpath);

	// 返回ret，表示函数执行结果。
	return ret;
}


#endif

/******************************************************************************
 *                                                                            *
 * Function: get_program_name                                                 *
 *                                                                            *
 * Purpose: return program name without path                                  *
 *                                                                            *
 * Parameters: path                                                           *
 *                                                                            *
 * Return value: program name without path                                    *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示，主要目的是从给定的程序路径中提取程序名称，并将提取到的程序名称存储在传入的const char类型的指针变量中。
 ******************************************************************************/
/*
 * 定义一个C语言函数，名为get_program_name，接收两个参数：
 * 1. 一个const char类型的指针path，表示程序的路径；
 * 2. 一个const char类型的指针，用于存储程序名称的变量。
 * 
 * 函数的主要目的是从给定的路径中提取程序名称。
 * 
 * 函数的返回值是一个const char类型的指针，指向程序名称。
 */
const char *get_program_name(const char *path)
{
	/* 定义一个const char类型的指针filename，初始值为NULL。
	 * 该指针用于存储提取到的程序名称。
	 */
	const char	*filename = NULL;

	/* 采用for循环，从path字符串中查找程序名称。
	 * 循环条件：path字符串不为空且路径字符不为'\0'。
	 */
	for (filename = path; path && *path; path++)
	{
		/* 如果当前路径字符是反斜杠（'\'）或正斜杠（'/'），
		 * 则将filename指针指向path字符串的后一个字符，
		 * 即提取路径中的文件名。
		 */
		if ('\\' == *path || '/' == *path)
			filename = path + 1;
	}

	/* 返回提取到的程序名称，即filename指针所指向的字符串。
	 * 注意：这里返回的是一个const char类型的指针，不可修改。
	 */
	return filename;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_timespec                                                     *
 *                                                                            *
 * Purpose: Gets the current time.                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: Time in seconds since midnight (00:00:00),                       *
 *           January 1, 1970, coordinated universal time (UTC).               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为 `zbx_timespec` 的函数，该函数接收一个 `zbx_timespec_t` 类型的指针作为参数，用于计算和纠正系统时间戳。该函数首先根据不同的操作系统（Windows 和 Linux）获取系统时间，然后对时间戳进行纠正，最后将纠正后的时间戳存储回原结构体变量。整个代码采用 C 语言编写，并使用了线程局部变量（`ZBX_THREAD_LOCAL`）来保存上一个时间点和纠正值，以实现跨线程的资源共享。
 ******************************************************************************/
void	zbx_timespec(zbx_timespec_t *ts)
{
	// 定义一个全局结构体变量 last_ts，用于存储上一个时间点的时间戳
	ZBX_THREAD_LOCAL static zbx_timespec_t	last_ts = {0, 0};
	// 定义一个全局整型变量 corr，用于计算时间戳之间的纠正值
	ZBX_THREAD_LOCAL static int		corr = 0;

#ifdef _WINDOWS
	// 定义一个全局静态 LARGE_INTEGER 类型变量 tickPerSecond，用于存储系统每秒 tick 数
	ZBX_THREAD_LOCAL static LARGE_INTEGER	tickPerSecond = {0};
	// 定义一个 _timeb 类型变量 tb，用于存储系统时间
	struct _timeb				tb;
#else
	// 定义一个 timeval 类型变量 tv，用于存储系统时间
	struct timeval	tv;
	// 定义一个整型变量 rc，用于存储 gettimeofday 函数的返回值
	int		rc = -1;
#	ifdef HAVE_TIME_CLOCK_GETTIME
	// 定义一个 struct timespec 类型变量 tp，用于存储系统时间
	struct timespec	tp;
#	endif
#endif

#ifdef _WINDOWS
	// 如果 tickPerSecond 的整数部分为 0，则查询系统性能计数器获取 tick 频率
	if (0 == tickPerSecond.QuadPart)
		QueryPerformanceFrequency(&tickPerSecond);

	// 使用 _ftime 函数获取当前系统时间并存储在 tb 结构体中
	_ftime(&tb);

	// 將 tb 中的时间戳转换为 ts 指针所指向的结构体变量
	ts->sec = (int)tb.time;
	ts->ns = tb.millitm * 1000000;

	// 如果 tickPerSecond 的整数部分不为 0
	if (0 != tickPerSecond.QuadPart)
	{
		// 定义一个 LARGE_INTEGER 类型变量 tick，用于存储当前性能计数器值
		LARGE_INTEGER	tick;

		// 查询性能计数器值
		if (TRUE == QueryPerformanceCounter(&tick))
		{
			// 定义一个静态 LARGE_INTEGER 类型变量 last_tick，用于存储上一个时间点的 tick 值
			ZBX_THREAD_LOCAL static LARGE_INTEGER	last_tick = {0};

			// 如果 last_tick 的值不为 0
			if (0 < last_tick.QuadPart)
			{
				// 定义两个 LARGE_INTEGER 类型变量 qpc_tick 和 ntp_tick，用于计算纠正值
				LARGE_INTEGER	qpc_tick = {0}, ntp_tick = {0};

				/* _ftime () 返回的时间精度为毫秒，但 'ns' 可能增加至最多 1 毫秒 */
				if (last_ts.sec == ts->sec && last_ts.ns > ts->ns && 1000000 > (last_ts.ns - ts->ns))
				{
					// 使用 last_ts 作为时间戳，避免计算
					ts->ns = last_ts.ns;
				}
				else
				{
					// 计算 NTP 时间戳
					ntp_tick.QuadPart = tickPerSecond.QuadPart * (ts->sec - last_ts.sec) +
							tickPerSecond.QuadPart * (ts->ns - last_ts.ns) / 1000000000;
				}

				/* 主机系统时间可能回退，此时纠正值不合理 */
				if (0 <= ntp_tick.QuadPart)
					qpc_tick.QuadPart = tick.QuadPart - last_tick.QuadPart - ntp_tick.QuadPart;

				/* 如果 qpc_tick 的值在 0 和 tickPerSecond 之间，则进行纠正 */
				if (0 < qpc_tick.QuadPart && qpc_tick.QuadPart < tickPerSecond.QuadPart)
				{
					int	ns = (int)(1000000000 * qpc_tick.QuadPart / tickPerSecond.QuadPart);

					// 如果 NS 值小于 1 毫秒，则直接加到 ts->ns 上
					if (1000000 > ns)
					{
						ts->ns += ns;

						// 如果 NS 值超过 1 毫秒，则进行循环累加，直至不超过 1 毫秒
						while (ts->ns >= 1000000000)
						{
							ts->sec++;
							ts->ns -= 1000000000;
						}
					}
				}
			}

			// 更新 last_tick 为当前性能计数器值
			last_tick = tick;
		}
	}
#else	/* not _WINDOWS */
#ifdef HAVE_TIME_CLOCK_GETTIME
	// 如果 gettimeofday 返回值不为 0
	if (0 == (rc = clock_gettime(CLOCK_REALTIME, &tp)))
	{
		// 将tp 转换为 ts 指针所指向的结构体变量
		ts->sec = (int)tp.tv_sec;
		ts->ns = (int)tp.tv_nsec;
	}
#endif	/* HAVE_TIME_CLOCK_GETTIME */

	// 如果 rc 不为 0，则使用 time 函数获取当前时间并存储在 ts 指针所指向的结构体变量中
	if (0 != rc && 0 == (rc = gettimeofday(&tv, NULL)))
	{
		ts->sec = (int)tv.tv_sec;
		ts->ns = (int)tv.tv_usec * 1000;
	}

	// 如果 rc 仍然不为 0
	if (0 != rc)
	{
		// 使用 time 函数获取当前时间并存储在 ts 指针所指向的结构体变量中
		ts->sec = (int)time(NULL);
		ts->ns = 0;
	}
#endif	/* not _WINDOWS */

	// 如果当前时间戳与上一个时间戳相同，则累加纠正值
	if (last_ts.ns == ts->ns && last_ts.sec == ts->sec)
	{
		ts->ns += ++corr;

		// 如果 NS 值超过 1 毫秒，则进行循环累加，直至不超过 1 毫秒
		while (ts->ns >= 1000000000)
		{
			ts->sec++;
			ts->ns -= 1000000000;
		}
	}
	else
	{
		// 更新 last_ts 为当前时间戳
		last_ts.sec = ts->sec;
		last_ts.ns = ts->ns;
		corr = 0;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_time                                                         *
 *                                                                            *
 * Purpose: Gets the current time.                                            *
 *                                                                            *
 * Return value: Time in seconds                                              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: Time in seconds since midnight (00:00:00),                       *
 *           January 1, 1970, coordinated universal time (UTC).               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是获取当前时间，并将秒和纳秒部分相加，得到一个以秒为单位的时间值。最后将这个时间值作为 double 类型返回。
 *
 *注释详细说明：
 *1. 定义一个名为 zbx_time 的函数，无返回值。
 *2. 定义一个名为 ts 的 zbx_timespec 结构体变量，用于存储获取到的当前时间。
 *3. 调用 zbx_timespec 函数，获取当前时间，并将结果存储在 ts 结构体中。
 *4. 将 ts 结构体中的秒（sec）和纳秒（ns）部分分别转换为 double 类型。
 *5. 在秒的基础上，添加纳秒部分的 1.0e-9（一个非常小的数值），得到一个以秒为单位的时间值。
 *6. 返回经过处理的时间值。
 ******************************************************************************/
// 定义一个名为 zbx_time 的函数，该函数为 void 类型（无返回值）
double zbx_time(void)
{
	// 定义一个名为 ts 的 zbx_timespec 结构体变量
	zbx_timespec_t	ts;

	// 调用 zbx_timespec 函数，获取当前时间，并将结果存储在 ts 结构体中
	zbx_timespec(&ts);

	// 将 ts 结构体中的秒（sec）和纳秒（ns）部分分别转换为 double 类型，
	// 然后将它们相加，再加上 1.0e-9（一个非常小的数值），最后返回结果
	return (double)ts.sec + 1.0e-9 * (double)ts.ns;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_current_time                                                 *
 *                                                                            *
 * Purpose: Gets the current time including UTC offset                        *
 *                                                                            *
 * Return value: Time in seconds                                              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取当前时间（以秒为单位），并计算出当前时间与1970年1月1日之间的秒数差。通过调用zbx_time()函数获取当前时间，然后加上ZBX_JAN_1970_IN_SEC常量，最后返回计算得到的秒数差。
 ******************************************************************************/
/*
 * double zbx_current_time(void) 函数定义，用于获取当前时间（以秒为单位）
 * 主要目的是：计算当前时间与1970年1月1日（UNIX时间戳的开始）的秒数之差
 * 输入：无
 * 输出：返回一个double类型的值，表示当前时间与1970年1月1日之间的秒数差
 */

double	zbx_current_time(void)
{
	/* 调用zbx_time()函数获取当前时间（以秒为单位） */
	double current_time = zbx_time();

	/* 添加ZBX_JAN_1970_IN_SEC常量，表示1970年1月1日的秒数 */
	current_time += ZBX_JAN_1970_IN_SEC;

	/* 返回计算得到的当前时间与1970年1月1日之间的秒数差 */
	return current_time;
}


/******************************************************************************
 *                                                                            *
 * Function: is_leap_year                                                     *
 *                                                                            *
 * Return value:  SUCCEED - year is a leap year                               *
 *                FAIL    - year is not a leap year                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断给定年份是否为闰年。函数`is_leap_year`接受一个整数参数`year`，根据以下规则进行判断：
 *
 *1. 如果年份能被4整除，继续判断是否能被100整除；
 *2. 如果年份能被100整除，继续判断是否能被400整除；
 *3. 如果年份能被400整除，则为闰年，返回成功；
 *4. 如果年份不能被4、100或400整除，则为平年，返回失败。
 *
 *函数返回两个常量值之一：`SUCCEED`（表示闰年）或`FAIL`（表示平年）。
 ******************************************************************************/
// 定义一个函数is_leap_year，用于判断给定年份是否为闰年
static int is_leap_year(int year)
{
    // 判断年份是否为0，如果不是，继续执行后续判断
    if (0 != year)
    {
        // 判断年份是否能被4整除，如果能，继续执行后续判断
        if (0 == year % 4)
        {
            // 判断年份是否能被100整除，如果能，继续执行后续判断
            if (0 != year % 100)
            {
                // 如果年份能被400整除，则为闰年，返回成功
                if (0 == year % 400)
                {
                    return SUCCEED;
                }
            }
            // 如果年份不能被400整除，但能被100整除，则为闰年，返回成功
            else
            {
                return SUCCEED;
            }
        }
        // 如果年份不能被4整除，继续判断是否能被100整除
        else if (0 != year % 100)
        {
            // 如果年份能被400整除，则为闰年，返回成功
            if (0 == year % 400)
            {
                return SUCCEED;
            }
        }
    }
    // 如果年份为0，或者年份不能被4或100整除，则为平年，返回失败
    else
    {
        return FAIL;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_get_time                                                     *
 *                                                                            *
 * Purpose:                                                                   *
 *     get current time and store it in memory locations provided by caller   *
 *                                                                            *
 * Parameters:                                                                *
 *     tm           - [OUT] broken-down representation of the current time    *
 *     milliseconds - [OUT] milliseconds since the previous second            *
 *     tz           - [OUT] local time offset from UTC (optional)             *
 *                                                                            *
 * Comments:                                                                  *
 *     On Windows localtime() and gmtime() return pointers to static,         *
 *     thread-local storage locations. On Unix localtime() and gmtime() are   *
 *     not thread-safe and re-entrant as they return pointers to static       *
 *     storage locations which can be overwritten by localtime(), gmtime()    *
 *     or other time functions in other threads or signal handlers. To avoid  *
 *     this we use localtime_r() and gmtime_r().                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取当前时间，并将结果存储在tm结构体、毫秒数和时区信息中。具体来说：
 *
 *1. 获取当前时间，并存储在current_time结构体中。
 *2. 将current_time转换为本地时间，存储在tm结构体中。
 *3. 获取当前时间的毫秒数，并存储在milliseconds中。
 *4. 如果提供了时区信息（tz不为空），则计算时区偏移，并设置时区信息的符号、小时和分钟。
 *
 *需要注意的是，此代码块针对Windows和Linux/Unix系统做了不同的处理。在Windows系统中，使用_ftime()和localtime()函数获取当前时间和本地时间。在Linux/Unix系统中，使用gettimeofday()和gmtime_r()函数获取当前时间和UTC时间。
 ******************************************************************************/
void	zbx_get_time(struct tm *tm, long *milliseconds, zbx_timezone_t *tz)
{
#ifdef _WINDOWS
	// 定义一个结构体变量current_time，用于存储当前时间
	struct _timeb	current_time;

	// 在Windows系统下使用_ftime()函数获取当前时间，并存储在current_time中
	_ftime(&current_time);
	// 将current_time转换为本地时间，存储在tm中
	*tm = *localtime(&current_time.time);
	// 获取当前时间的毫秒数，并存储在milliseconds中
	*milliseconds = current_time.millitm;
#else
	struct timeval	current_time;

	gettimeofday(&current_time, NULL);
	localtime_r(&current_time.tv_sec, tm);
	*milliseconds = current_time.tv_usec / 1000;
#endif
	if (NULL != tz)
	{
#ifdef HAVE_TM_TM_GMTOFF
#	define ZBX_UTC_OFF	tm->tm_gmtoff
#else
#	define ZBX_UTC_OFF	offset
		long		offset;
		struct tm	tm_utc;
#ifdef _WINDOWS
		tm_utc = *gmtime(&current_time.time);	/* gmtime() cannot return NULL if called with valid parameter */
#else
		// 将当前时间转换为UTC时间，存储在tm_utc中
		gmtime_r(&current_time.tv_sec, &tm_utc);
#endif
		// 计算时区偏移
		offset = (tm->tm_yday - tm_utc.tm_yday) * SEC_PER_DAY + (tm->tm_hour - tm_utc.tm_hour) * SEC_PER_HOUR +
				(tm->tm_min - tm_utc.tm_min) * SEC_PER_MIN;

		// 调整偏移量，使时间向前推进
		while (tm->tm_year > tm_utc.tm_year)
			offset += (SUCCEED == is_leap_year(tm_utc.tm_year++) ? SEC_PER_YEAR + SEC_PER_DAY : SEC_PER_YEAR);

		// 调整偏移量，使时间向后推进
		while (tm->tm_year < tm_utc.tm_year)
			offset -= (SUCCEED == is_leap_year(--tm_utc.tm_year) ? SEC_PER_YEAR + SEC_PER_DAY : SEC_PER_YEAR);
#endif
		// 设置时区信息的符号、小时和分钟
		tz->tz_sign = (0 <= ZBX_UTC_OFF ? '+' : '-');
		tz->tz_hour = labs(ZBX_UTC_OFF) / SEC_PER_HOUR;
		tz->tz_min = (labs(ZBX_UTC_OFF) - tz->tz_hour * SEC_PER_HOUR) / SEC_PER_MIN;
		/* 假设没有剩余的秒，例如历史时期的Asia/Riyadh87、Asia/Riyadh88和Asia/Riyadh89 */
#undef ZBX_UTC_OFF
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_utc_time                                                     *
 *                                                                            *
 * Purpose: get UTC time from time from broken down time elements             *
 *                                                                            *
 * Parameters:                                                                *
 *     year  - [IN] year (1970-...)                                           *
 *     month - [IN] month (1-12)                                              *
 *     mday  - [IN] day of month (1-..., depending on month and year)         *
 *     hour  - [IN] hours (0-23)                                              *
 *     min   - [IN] minutes (0-59)                                            *
 *     sec   - [IN] seconds (0-61, leap seconds are not strictly validated)   *
 *     t     - [OUT] Epoch timestamp                                          *
 *                                                                            *
 * Return value:  SUCCEED - date is valid and resulting timestamp is positive *
 *                FAIL - otherwise                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算给定年月日时的UTC时间，并将结果存储在指针t所指向的整数变量中。如果计算成功，函数返回SUCCEED，否则返回FAIL。在这个过程中，使用了静态常量数组month_day和epoch_year，以及自定义宏ZBX_LEAP_YEARS。函数首先判断输入的参数是否合法，然后根据合法的参数计算UTC时间，并将结果存储在指针t所指向的整数变量中。
 ******************************************************************************/
// 定义一个函数zbx_utc_time，接收6个整数参数year、mon、mday、hour、min、sec，以及一个整数指针t作为输出参数。
// 该函数计算从1970年1月1日到给定年月日时的UTC时间，并将结果存储在指针t所指向的整数变量中。
// 如果计算成功，函数返回SUCCEED，否则返回FAIL。

int	zbx_utc_time(int year, int mon, int mday, int hour, int min, int sec, int *t)
{
    // 定义一个常量，表示在给定年份之前但未包括的年份中的闰年数量
    #define ZBX_LEAP_YEARS(year)	(((year) - 1) / 4 - ((year) - 1) / 100 + ((year) - 1) / 400)

    // 定义一个静态常量数组，表示从非闰年年初到给定月份的天数
    static const int	month_day[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    // 定义一个静态常量，表示1970年作为时间戳的起始年份
    static const int	epoch_year = 1970;

    // 判断给定的年、月、日、时、分、秒是否合法
    if (epoch_year <= year && 1 <= mon && mon <= 12 && 1 <= mday && mday <= zbx_day_in_month(year, mon) &&
        0 <= hour && hour <= 23 && 0 <= min && min <= 59 && 0 <= sec && sec <= 61) {

        // 计算UTC时间
        int utc_time = 0;
        utc_time = (year - epoch_year) * SEC_PER_YEAR +
                  (ZBX_LEAP_YEARS(2 < mon ? year + 1 : year) - ZBX_LEAP_YEARS(epoch_year)) * SEC_PER_DAY +
                  (month_day[mon - 1] + mday - 1) * SEC_PER_DAY + hour * SEC_PER_HOUR + min * SEC_PER_MIN + sec;

        // 将计算得到的UTC时间存储到指针t所指向的整数变量中
        *t = utc_time;

        // 返回成功
        return SUCCEED;
    }

    // 返回失败
    return FAIL;

    // 取消定义ZBX_LEAP_YEARS宏
    #undef ZBX_LEAP_YEARS
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_day_in_month                                                 *
 *                                                                            *
 * Purpose: returns number of days in a month                                 *
 *                                                                            *
 * Parameters:                                                                *
 *     year  - [IN] year                                                      *
 *     mon   - [IN] month (1-12)                                              *
 *                                                                            *
 * Return value: 28-31 depending on number of days in the month, defaults to  *
 *               30 if the month is outside of allowed range                  *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算非闰年每月的天数。函数接受两个参数，year表示年份，mon表示月份。首先定义一个常量数组month存储非闰年每月天数。然后判断输入的月份是否在1到12之间，如果在，则根据闰年条件加1天（如果是二月），否则不加。最后返回对应月份的天数。如果月份不在1到12之间，则默认返回30天。
 ******************************************************************************/
// 定义一个函数zbx_day_in_month，用于计算非闰年每月天数
int	zbx_day_in_month(int year, int mon)
{
	/* 定义一个常量数组，存储非闰年每月天数 */
	static const unsigned char	month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	/* 判断月份是否在1到12之间，如果在，则是合法的月份 */
	if (1 <= mon && mon <= 12) {
		/* 如果是闰年的二月，加1天 */
		return month[mon - 1] + (2 == mon && SUCCEED == is_leap_year(year) ? 1 : 0);
	}

	/* 如果月份不在1到12之间，则默认返回30天 */
	return 30;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_calloc2                                                      *
 *                                                                            *
 * Purpose: allocates nmemb * size bytes of memory and fills it with zeros    *
 *                                                                            *
 * Return value: returns a pointer to the newly allocated memory              *
 *                                                                            *
 * Author: Eugene Grigorjev, Rudolfs Kreicbergs                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是为一个名为zbx_calloc2的函数分配内存。该函数接收5个参数，分别是文件名、行号、旧指针、内存块数量和每个块的大小。在分配内存的过程中，会尝试多次分配，直到成功或达到最大尝试次数为止。如果分配成功，返回分配的内存地址；如果分配失败，输出错误日志并退出程序。
 ******************************************************************************/
void *zbx_calloc2(const char *filename, int line, void *old, size_t nmemb, size_t size)
{
	// 定义一个函数zbx_calloc2，接收5个参数，分别是文件名、行号、旧指针、内存块数量和每个块的大小

	int	max_attempts; // 定义一个变量max_attempts，初始值为10，用于记录分配内存的最大尝试次数
	void	*ptr = NULL; // 定义一个指针变量ptr，初始值为NULL，用于存储分配的内存地址

	/* old pointer must be NULL */
	// 如果旧指针不为NULL，则输出警告日志，并请求开发者关注这个问题
	if (NULL != old)
	{
		zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] zbx_calloc: allocating already allocated memory. "
				"Please report this to Zabbix developers.",
				filename, line);
	}

	for (
		// 初始化max_attempts为10，nmemb为传入的参数，但最多不超过1，size也同理
		max_attempts = 10, nmemb = MAX(nmemb, 1), size = MAX(size, 1);
		// 只要max_attempts大于0且ptr为NULL，就继续尝试分配内存
		0 < max_attempts && NULL == ptr;
		// 尝试分配内存，分配失败则继续循环，分配成功则更新ptr
		ptr = calloc(nmemb, size), max_attempts--
	);

	// 如果ptr不为NULL，说明内存分配成功，返回分配的内存地址
	if (NULL != ptr)
		return ptr;

	zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] zbx_calloc: out of memory. Requested " ZBX_FS_SIZE_T " bytes.",
			filename, line, (zbx_fs_size_t)size);

	// 内存分配失败，输出错误日志，并退出程序
	exit(EXIT_FAILURE);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_malloc2                                                      *
 *                                                                            *
 * Purpose: allocates size bytes of memory                                    *
 *                                                                            *
 * Return value: returns a pointer to the newly allocated memory              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_malloc2 的函数，该函数用于分配内存。函数接收四个参数：一个字符串指针 filename、一个整型指针 line、一个旧指针 old 和一个大小为 size 的指针。该函数尝试分配 size 大小的内存，并循环尝试最多 max_attempts 次。如果分配成功，返回分配到的内存地址；如果分配失败，输出错误日志并退出程序。在整个过程中，确保旧指针为 NULL，以避免重复分配内存。
 ******************************************************************************/
void *zbx_malloc2(const char *filename, int line, void *old, size_t size)
{
	// 定义一个整型变量 max_attempts，用于记录分配内存的最大尝试次数
	int	max_attempts;
	// 定义一个指向空白的指针 ptr，用于存储分配到的内存地址
	void	*ptr = NULL;

	/* old pointer must be NULL */
	// 如果 old 指针不为 NULL，则输出警告日志，并请求开发者报告此问题
	if (NULL != old)
	{
		zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] zbx_malloc: allocating already allocated memory. "
				"Please report this to Zabbix developers.",
				filename, line);
	}

	for (
		// 初始化 max_attempts 为 10，size 为分配内存的最小值（这里设置为 1）
		max_attempts = 10, size = MAX(size, 1);
		// 循环分配内存，直到成功分配或尝试次数达到上限
		0 < max_attempts && NULL == ptr;
		// 分配 size 大小的内存，并将 max_attempts 减一
		ptr = malloc(size), max_attempts--
	);

	// 如果分配内存成功，返回分配到的内存地址
	if (NULL != ptr)
		return ptr;

	// 如果没有分配到内存，输出错误日志，并退出程序
	zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] zbx_malloc: out of memory. Requested " ZBX_FS_SIZE_T " bytes.",
			filename, line, (zbx_fs_size_t)size);

	// 退出程序，返回 EXIT_FAILURE 状态码
	exit(EXIT_FAILURE);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_realloc2                                                     *
 *                                                                            *
 * Purpose: changes the size of the memory block pointed to by old            *
 *          to size bytes                                                     *
 *                                                                            *
 * Return value: returns a pointer to the newly allocated memory              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：提供一个重新分配内存的函数zbx_realloc2，该函数根据传入的文件名、行号、旧内存地址和所需内存大小尝试重新分配内存。当达到最大尝试次数后，若仍未分配成功，则记录日志并退出程序。
 ******************************************************************************/
void *zbx_realloc2(const char *filename, int line, void *old, size_t size)
{
    // 定义一个函数，用于动态分配内存
    // 参数1：文件名，用于调试输出
    // 参数2：行号，用于调试输出
    // 参数3：旧的内存地址，用于传递给realloc函数
    // 参数4：所需内存大小

    int	max_attempts; // 最大尝试次数
    void	*ptr = NULL; // 用于存储重新分配后的内存地址

    for (
        // 初始化最大尝试次数为10，每次增加1，直到成功分配内存或达到最大尝试次数
        max_attempts = 10, size = MAX(size, 1);
        0 < max_attempts && NULL == ptr;
        // 尝试重新分配内存，若失败则减少最大尝试次数
        ptr = realloc(old, size), max_attempts--
    );

    // 如果重新分配成功，返回新分配的内存地址
    if (NULL != ptr)
        return ptr;

    // 如果在最大尝试次数内仍未分配成功，记录日志并退出程序
    zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] zbx_realloc: out of memory. Requested " ZBX_FS_SIZE_T " bytes.",
                filename, line, (zbx_fs_size_t)size);

    exit(EXIT_FAILURE);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串复制功能，即将传入的const char *str字符串复制到一个新的字符串中，并将新字符串的指针返回。在复制过程中，尝试分配10次内存，如果分配成功则返回新字符串的指针，否则记录日志并退出程序。
 ******************************************************************************/
// 定义一个函数zbx_strdup2，接收四个参数：
// 参数1：const char *filename，文件名
// 参数2：int line，行号
// 参数3：char *old，旧字符串指针
// 参数4：const char *str，需要复制的新字符串
// 函数返回值：char *，指向新字符串的指针

char	*zbx_strdup2(const char *filename, int line, char *old, const char *str)
{
	// 定义一个整型变量retry，用于循环次数
	// 初始化为10
	int	retry;
	// 定义一个字符型指针变量ptr，初始化为NULL
	char	*ptr = NULL;

	// 调用zbx_free函数，释放old指向的内存
	zbx_free(old);

	// 使用for循环，循环次数为10次
	// 每次循环将strdup(str)的返回值赋值给ptr，然后retry减1
	for (retry = 10; 0 < retry && NULL == ptr; ptr = strdup(str), retry--)
		;

	// 如果ptr不为NULL，说明内存分配成功，直接返回ptr
	if (NULL != ptr)
		return ptr;

	// 如果没有分配到内存，记录日志并退出程序
	zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] zbx_strdup: out of memory. Requested " ZBX_FS_SIZE_T " bytes.",
			filename, line, (zbx_fs_size_t)(strlen(str) + 1));

	// 退出程序，返回值为EXIT_FAILURE
	exit(EXIT_FAILURE);
}


/****************************************************************************************
 *                                                                                      *
 * Function: zbx_guaranteed_memset                                                      *
 *                                                                                      *
 * Purpose: For overwriting sensitive data in memory.                                   *
 *          Similar to memset() but should not be optimized out by a compiler.          *
 *                                                                                      *
 * Derived from:                                                                        *
 *   http://www.dwheeler.com/secure-programs/Secure-Programs-HOWTO/protect-secrets.html *
 * See also:                                                                            *
 *   http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1381.pdf on secure_memset()       *
 *                                                                                      *
 ****************************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个保证内存设置安全的函数，通过逐个字符地将指定值设置到指定内存区域中。该函数适用于需要安全设置内存的场景，例如操作系统、嵌入式系统等对内存操作有严格要求的场景。
 ******************************************************************************/
/*
 * 这个函数名为：zbx_guaranteed_memset，它是一个C语言函数，
 * 主要作用是用于设置内存区域的字符值。
 * 
 * 函数原型：void *zbx_guaranteed_memset(void *v, int c, size_t n);
 * 参数说明：
 *   v：指向要填充的内存区域的指针。
 *   c：要设置的字符值。
 *   n：要设置的字符数量。
 *
 * 函数返回值：void指针，指向原来的内存区域。
 */

void	*zbx_guaranteed_memset(void *v, int c, size_t n)
{
	// 将传入的指针v转换为volatile signed char类型，这样可以确保内存操作的稳定性
	volatile signed char	*p = (volatile signed char *)v;

	// 使用while循环逐个字符地设置内存区域
	while (0 != n--)
	{
		// 将字符c转换为signed char类型，然后将其存储到指针p所指向的内存位置
		*p++ = (signed char)c;
	}

	// 循环结束后，返回原来的内存区域指针v
	return v;
}


/******************************************************************************
 *                                                                            *
 * Function: __zbx_zbx_setproctitle                                           *
 *                                                                            *
 * Purpose: set process title                                                 *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是设置进程标题。该函数接收一个格式化字符串作为参数，将其填充到进程标题中，然后根据系统支持的函数设置进程标题。同时，记录一条日志表示设置进程标题的函数名和新的进程标题。
 ******************************************************************************/
void zbx_setproctitle(const char *fmt, ...)
{
    // 定义一个宏，用于设置进程标题
    // 参数：fmt，格式化字符串，用于填充进程标题

#if defined(HAVE_FUNCTION_SETPROCTITLE) || defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "__zbx_zbx_setproctitle";
    // 定义一个字符数组，用于存储进程标题
    char title[MAX_STRING_LEN];
    // 定义一个变量，用于存储格式化字符串的参数列表
    va_list args;

    // 开始解析格式化字符串的参数列表
    va_start(args, fmt);
    // 使用zbx_vsnprintf函数将格式化字符串填充到title数组中
    zbx_vsnprintf(title, sizeof(title), fmt, args);
    // 结束解析格式化字符串的参数列表
    va_end(args);

    // 记录日志，表示设置进程标题的函数名和新的进程标题
    zabbix_log(LOG_LEVEL_DEBUG, "%s() title:'%s'", __function_name, title);

#endif

#if defined(HAVE_FUNCTION_SETPROCTITLE)
    // 如果系统支持setproctitle函数，则使用该函数设置进程标题
    setproctitle("%s", title);
#elif defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
    // 如果系统支持setproctitle_set_status函数，则使用该函数设置进程标题
    setproctitle_set_status(title);
#endif
}


/******************************************************************************
 *                                                                            *
 * Function: check_time_period                                                *
 *                                                                            *
 * Purpose: check if current time is within given period                      *
 *                                                                            *
 * Parameters: period - [IN] preprocessed time period                         *
 *             tm     - [IN] broken-down time for comparison                  *
 *                                                                            *
 * Return value: FAIL - out of period, SUCCEED - within the period            *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的时间周期（period）和当前时间（tm）是否符合要求。如果符合，返回0，否则返回1。其中，时间周期包括开始和结束的一天、小时、分钟和秒。当前时间也是以秒为单位。通过计算和比较这些值，判断是否在时间周期范围内。
 ******************************************************************************/
/* 定义一个函数，用于检查给定的时间周期是否符合要求
 * 参数：period 表示时间周期结构体，tm 表示时间结构体
 * 返回值：如果符合时间周期要求，返回0，否则返回1
 */
static int	check_time_period(const zbx_time_period_t period, struct tm *tm)
{
	/* 定义两个变量，分别表示一天中的第几天和当前时间（秒）
	 * 初始化：day 等于0，time 等于0
	 */
	int		day, time;

	/* 计算一天中的第几天，如果tm中的tm_wday为0，则表示是一周的最后一项，否则表示当前项
	 */
	day = 0 == tm->tm_wday ? 7 : tm->tm_wday;

	/* 计算当前时间，单位为秒
	 * 计算方式：每小时有3600秒，每分钟有60秒，当前秒数为tm->tm_sec
	 */
	time = SEC_PER_HOUR * tm->tm_hour + SEC_PER_MIN * tm->tm_min + tm->tm_sec;

	/* 判断条件：时间周期开始的一天不超过当前天数，当前天数不超过时间周期的结束一天，
	 * 且时间周期开始的秒数不超过当前秒数，当前秒数小于时间周期的结束秒数
	 * 如果是，返回0（表示符合时间周期要求），否则返回1（表示不符合）
	 */
	return period.start_day <= day && day <= period.end_day && period.start_time <= time && time < period.end_time ?
			SUCCEED : FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: get_current_delay                                                *
 *                                                                            *
 * Purpose: return delay value that is currently applicable                   *
 *                                                                            *
 * Parameters: default_delay  - [IN] default delay value, can be overridden   *
 *             flex_intervals - [IN] preprocessed flexible intervals          *
 *             now            - [IN] current time                             *
 *                                                                            *
 * Return value: delay value - either default or minimum delay value          *
 *                             out of all applicable intervals                *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev, Aleksandrs Saveljevs        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是查找一个合适的时间间隔，并根据查找结果返回对应的延迟时间。查找的过程中，首先遍历柔性时间间隔链表，然后判断当前时间是否在链表中的时间间隔内，如果满足条件，则更新延迟时间。最后，如果没有找到合适的时间间隔，则返回默认延迟时间。
 ******************************************************************************/
// 定义一个名为 get_current_delay 的静态函数，该函数接收三个参数：
// int default_delay：默认延迟时间；
// const zbx_flexible_interval_t *flex_intervals：柔性时间间隔链表的头指针；
// time_t now：当前时间。
static int get_current_delay(int default_delay, const zbx_flexible_interval_t *flex_intervals, time_t now)
{
	// 定义一个名为 current_delay 的整型变量，并初始化为 -1，用于存储当前延迟时间。
	int		current_delay = -1;

	// 使用 while 循环遍历 flex_intervals 指向的柔性时间间隔链表。
	while (NULL != flex_intervals)
	{
		// 判断当前延迟时间（current_delay）是否小于等于 flex_intervals 指向的时间间隔的延迟时间（flex_intervals->delay）。
		// 如果满足条件，再判断当前时间（now）是否在 flex_intervals 指向的时间间隔内。
		// 如果以上两个条件都满足，则更新 current_delay 为 flex_intervals 指向的时间间隔的延迟时间。
		if ((-1 == current_delay || flex_intervals->delay < current_delay) &&
				SUCCEED == check_time_period(flex_intervals->period, localtime(&now)))
		{
			current_delay = flex_intervals->delay;
		}

		// 遍历结束后，将 flex_intervals 指向下一个时间间隔。
		flex_intervals = flex_intervals->next;
	}

	// 如果 current_delay 的值为 -1，说明没有找到合适的时间间隔，则返回默认延迟时间；
	// 否则，返回当前延迟时间。
	return -1 == current_delay ? default_delay : current_delay;
}


/******************************************************************************
 *                                                                            *
 * Function: get_next_delay_interval                                          *
 *                                                                            *
 * Purpose: return time when next delay settings take effect                  *
 *                                                                            *
 * Parameters: flex_intervals - [IN] preprocessed flexible intervals          *
 *             now            - [IN] current time                             *
 *             next_interval  - [OUT] start of next delay interval            *
 *                                                                            *
 * Return value: SUCCEED - there is a next interval                           *
 *               FAIL - otherwise (in this case, next_interval is unaffected) *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev, Aleksandrs Saveljevs        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算下一个延迟间隔的时间。该函数接收三个参数：一个指向zbx_flexible_interval_t结构体的指针（flex_intervals），一个时间戳（now），和一个指向时间戳的指针（next_interval）。函数首先判断flex_intervals是否为空，若为空则返回失败。接着，将now转换为tm结构体指针，并计算当前日期和时间。然后遍历flex_intervals链表，判断当前日期和时间是否在时间周期范围内。根据判断结果，更新下一个间隔值。最后，计算下一个延迟间隔的时间，并将其赋值给next_interval，返回成功。
 ******************************************************************************/
/* 定义一个函数，用于获取下一个延迟间隔 */
static int get_next_delay_interval(const zbx_flexible_interval_t *flex_intervals, time_t now, time_t *next_interval)
{
	/* 定义变量，用于保存日期、时间、下一个间隔等 */
	int day, time, next = 0, candidate;
	struct tm *tm;

	/* 判断传入的flex_intervals是否为空，若为空则返回失败 */
	if (NULL == flex_intervals)
		return FAIL;

	/* 将now转换为tm结构体指针 */
	tm = localtime(&now);
	/* 计算当前日期和时间 */
	day = 0 == tm->tm_wday ? 7 : tm->tm_wday;
	time = SEC_PER_HOUR * tm->tm_hour + SEC_PER_MIN * tm->tm_min + tm->tm_sec;

	/* 遍历flex_intervals链表 */
	for (; NULL != flex_intervals; flex_intervals = flex_intervals->next)
	{
		/* 获取链表中的时间周期结构体指针 */
		const zbx_time_period_t *p = &flex_intervals->period;

		/* 判断当前日期和时间是否在时间周期范围内 */
		if (p->start_day <= day && day <= p->end_day && time < p->end_time) /* 今天活跃 */
		{
			/* 如果当前时间小于时间周期的开始时间，则设置候选值为开始时间 */
			if (time < p->start_time) /* 尚未今天活跃 */
				candidate = p->start_time;
			/* 否则，当前时间即为活跃时间 */
			else /* 当前活跃 */
				candidate = p->end_time;
		}
		else if (day < p->end_day) /* 本周活跃 */
		{
			/* 如果当前日期小于时间周期的开始日期，则设置候选值为本周开始时间 */
			if (day < p->start_day) /* 尚未本周活跃 */
				candidate = SEC_PER_DAY * (p->start_day - day) + p->start_time;
			/* 否则，候选值为本周活跃时间的下一天 */
			else /* 已本周活跃且在本周结束前至少活跃一次 */
				candidate = SEC_PER_DAY + p->start_time; /* 因此，明天活跃 */
		}
		else /* 下周活跃 */
			candidate = SEC_PER_DAY * (p->start_day + 7 - day) + p->start_time;

		/* 更新下一个间隔值，若当前候选值大于0且大于上一个间隔值，则更新下一个间隔值为候选值 */
		if (0 == next || next > candidate)
			next = candidate;
	}

	/* 如果下一个间隔值为0，则返回失败 */
	if (0 == next)
		return FAIL;

	/* 计算下一个延迟间隔的时间，并将其赋值给next_interval */
	*next_interval = now - time + next;
	/* 返回成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: time_parse                                                       *
 *                                                                            *
 * Purpose: parses time of day                                                *
 *                                                                            *
 * Parameters: time       - [OUT] number of seconds since the beginning of    *
 *                            the day corresponding to a given time of day    *
 *             text       - [IN] text to parse                                *
 *             len        - [IN] number of characters available for parsing   *
 *             parsed_len - [OUT] number of characters recognized as time     *
 *                                                                            *
 * Return value: SUCCEED - text was successfully parsed as time of day        *
 *               FAIL    - otherwise (time and parsed_len remain untouched)   *
 *                                                                            *
 * Comments: !!! Don't forget to sync code with PHP !!!                       *
 *           Supported formats are hh:mm, h:mm and 0h:mm; 0 <= hours <= 24;   *
 *           0 <= minutes <= 59; if hours == 24 then minutes must be 0.       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析一个时间字符串，将其转换为小时和分钟的整数值。输入为一个整数指针、一个字符串、字符串长度和一个解析后的长度指针。输出为一个布尔值，表示解析是否成功，以及解析后的时间值。
 ******************************************************************************/
// 定义一个静态函数time_parse，用于解析时间字符串
static int	time_parse(int *time, const char *text, int len, int *parsed_len)
{
	// 保存原始字符串长度
	const int	old_len = len;
	const char	*ptr;
	int		hours, minutes;

	// 遍历字符串，查找小时和分钟的分隔符
	for (ptr = text; 0 < len && 0 != isdigit(*ptr) && 2 >= ptr - text; len--, ptr++)
		;

	// 判断是否成功找到小时和分钟的分隔符
	if (SUCCEED != is_uint_n_range(text, ptr - text, &hours, sizeof(hours), 0, 24))
		return FAIL;

	// 跳过分隔符
	if (0 >= len-- || ':' != *ptr++)
		return FAIL;

	// 遍历字符串，查找分钟
	for (text = ptr; 0 < len && 0 != isdigit(*ptr) && 2 >= ptr - text; len--, ptr++)
		;

	// 判断是否成功找到分钟
	if (2 != ptr - text)
		return FAIL;

	// 判断小时和分钟是否合法
	if (SUCCEED != is_uint_n_range(text, 2, &minutes, sizeof(minutes), 0, 59))
		return FAIL;

	// 检查小时和分钟的组合是否合法
	if (24 == hours && 0 != minutes)
		return FAIL;

	// 计算解析后的字符串长度
	*parsed_len = old_len - len;
	// 计算时间值
	*time = SEC_PER_HOUR * hours + SEC_PER_MIN * minutes;
	// 返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: time_period_parse                                                *
 *                                                                            *
 * Purpose: parses time period                                                *
 *                                                                            *
 * Parameters: period - [OUT] time period structure                           *
 *             text   - [IN] text to parse                                    *
 *             len    - [IN] number of characters available for parsing       *
 *                                                                            *
 * Return value: SUCCEED - text was successfully parsed as time period        *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: !!! Don't forget to sync code with PHP !!!                       *
 *           Supported format is d[-d],time-time where 1 <= d <= 7            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个时间周期，输入为一个字符串，输出为一个zbx_time_period_t结构体。该函数名为time_period_parse，接收三个参数：一个指向zbx_time_period_t结构体的指针、一个字符串指针和一个整型变量。函数首先检查输入的字符串长度和首个字符，然后解析出start_day和end_day。接下来，解析start_time和end_time，并检查它们之间的大小关系。如果解析过程中遇到任何错误，函数将返回FAIL，否则返回SUCCEED。
 ******************************************************************************/
// 定义一个函数，用于解析时间周期
static int time_period_parse(zbx_time_period_t *period, const char *text, int len)
{
	// 定义一个整型变量，用于存储解析后的字符串长度
	int parsed_len;

	// 检查输入的字符串长度是否大于等于0，如果不是，返回失败
	if (0 >= len--) // len-- 表示减1，这里实际操作是先将len的值赋给0，然后len自减1
		return FAIL;

	// 检查输入的字符是否为1到7之间的数字，如果不是，返回失败
	if ('1' > *text || '7' < *text)
		return FAIL;

	// 将字符串的首字符转换为数字，并存储到period结构的start_day成员变量中
	period->start_day = *text++ - '0';

	// 检查剩余的字符串长度是否大于0，如果不是，返回失败
	if (0 >= len)
		return FAIL;

	// 检查当前字符是否为减号（'-'），如果是，执行以下操作
	if ('-' == *text)
	{
		// 跳过减号，并减1的字符串长度
		text++;
		len--;

		// 检查剩余的字符串长度是否大于0，如果不是，返回失败
		if (0 >= len--) // len-- 表示减1，这里实际操作是先将len的值赋给0，然后len自减1
			return FAIL;

		// 检查当前字符是否为1到7之间的数字，如果不是，返回失败
		if ('1' > *text || '7' < *text)
			return FAIL;

		// 将字符串的首字符转换为数字，并存储到period结构的end_day成员变量中
		period->end_day = *text++ - '0';

		// 检查start_day是否大于end_day，如果是，返回失败
		if (period->start_day > period->end_day)
			return FAIL;
	}
	else
	{
		// 如果当前字符不是减号，将start_day设置为end_day，即表示一天
		period->end_day = period->start_day;
	}

	// 检查剩余的字符串长度是否大于0，如果不是，返回失败
	// 检查当前字符是否为逗号（','），如果不是，返回失败
	if (0 >= len-- || ',' != *text++)
		return FAIL;

	// 调用time_parse函数，解析start_time和end_time，并将结果存储到period结构中
	if (SUCCEED != time_parse(&period->start_time, text, len, &parsed_len))
		return FAIL;

	// 更新text和len的值，表示已经解析了start_time
	text += parsed_len;
	len -= parsed_len;

	if (0 >= len-- || '-' != *text++)
		return FAIL;

	// 调用time_parse函数，解析end_time，并将结果存储到period结构中
	if (SUCCEED != time_parse(&period->end_time, text, len, &parsed_len))
		return FAIL;

	// 检查start_time是否大于end_time，如果是，返回失败
	if (period->start_time >= period->end_time)
		return FAIL;

	// 检查剩余的字符串长度是否大于0，如果不是，返回失败
	if (0 != (len -= parsed_len))
		return FAIL;

	// 如果一切解析顺利，返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_check_time_period                                            *
 *                                                                            *
 * Purpose: validate time period and check if specified time is within it     *
 *                                                                            *
 * Parameters: period - [IN] semicolon-separated list of time periods in one  *
 *                           of the following formats:                        *
 *                             d1-d2,h1:m1-h2:m2                              *
 *                             or d1,h1:m1-h2:m2                              *
 *             time   - [IN] time to check                                    *
 *             res    - [OUT] check result:                                   *
 *                              SUCCEED - if time is within period            *
 *                              FAIL    - otherwise                           *
 *                                                                            *
 * Return value: validation result (SUCCEED - valid, FAIL - invalid)          *
 *                                                                            *
 * Comments:   !!! Don't forget to sync code with PHP !!!                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查一个给定时间是否符合一个包含多个时间周期的时间范围。函数接收一个表示时间周期的字符串、一个时间变量和一个用于存储判断结果的整型指针。函数首先将给定时间转换为本地时间，然后解析时间周期字符串，逐个检查时间周期是否符合要求。如果所有时间周期都符合要求，函数返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个函数zbx_check_time_period，接收三个参数：
// 1. 一个字符串指针period，表示时间周期；
// 2. 一个time_t类型的时间变量time；
// 3. 一个整型指针res，用于存储判断结果。
int	zbx_check_time_period(const char *period, time_t time, int *res)
{
	int			res_total = FAIL;
	const char		*next;
	struct tm		*tm;
	zbx_time_period_t	tp;

	tm = localtime(&time);

	next = strchr(period, ';');
	while  (SUCCEED == time_period_parse(&tp, period, (NULL == next ? (int)strlen(period) : (int)(next - period))))
	{
		if (SUCCEED == check_time_period(tp, tm))
			res_total = SUCCEED;	/* no short-circuits, validate all periods before return */

		if (NULL == next)
		{
			*res = res_total;
			return SUCCEED;
		}

		period = next + 1;
		next = strchr(period, ';');
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: flexible_interval_free                                           *
 *                                                                            *
 * Purpose: frees flexible interval                                           *
 *                                                                            *
 * Parameters: interval - [IN] flexible interval                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是遍历一个 flexible_interval 类型的链表，并使用 zbx_free 函数释放每个 flexible_interval 结构体的内存。函数 flexible_interval_free 接受一个 flexible_interval 类型的指针作为参数。在循环中，首先获取下一个 flexible_interval 结构体的指针，然后释放当前 flexible_interval 结构体的内存。当遍历到链表的末尾时，循环结束。
 ******************************************************************************/
// 定义一个静态函数 flexible_interval_free，用于释放 flexible_interval 类型的结构体内存
static void	flexible_interval_free(zbx_flexible_interval_t *interval)
{
	// 定义一个指针 interval_next，用于指向下一个 flexible_interval 结构体
	zbx_flexible_interval_t	*interval_next;

	// 使用 for 循环遍历 interval 指向的 flexible_interval 链表
	for (; NULL != interval; interval = interval_next)
	{
		// 获取下一个 flexible_interval 结构体的指针
		interval_next = interval->next;

		// 使用 zbx_free 函数释放当前 flexible_interval 结构体的内存
		zbx_free(interval);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: flexible_interval_parse                                          *
 *                                                                            *
 * Purpose: parses flexible interval                                          *
 *                                                                            *
 * Parameters: interval - [IN/OUT] the first interval                         *
 *             text     - [IN] the text to parse                              *
 *             len      - [IN] the text length                                *
 *                                                                            *
 * Return value: SUCCEED - the interval was successfully parsed               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: !!! Don't forget to sync code with PHP !!!                       *
 *           Supported format is delay/period                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个包含灵活时间间隔的字符串。首先，遍历字符串，找到第一个'/'或者字符串结束的位置。然后，判断字符串是否包含时间后缀，并将结果存储在interval结构的delay字段中。接下来，跳过'/'，继续遍历字符串，直到字符串结束。最后，调用time_period_parse函数，解析时间间隔部分的字符串。如果整个解析过程顺利，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个C函数，用于解析灵活的时间间隔字符串
// 输入参数：
//   interval：指向zbx_flexible_interval_t结构体的指针，用于存储解析结果
//   text：包含时间间隔字符串的字符数组
//   len：时间间隔字符串的长度
// 返回值：
//   成功：SUCCEED
//   失败：FAIL
static int	flexible_interval_parse(zbx_flexible_interval_t *interval, const char *text, int len)
{
	// 定义一个指向字符数组的指针，用于遍历字符串
	const char	*ptr;

	// 遍历字符串，直到遇到第一个'/'或者字符串结束
	for (ptr = text; 0 < len && '\0' != *ptr && '/' != *ptr; len--, ptr++)
		;

	// 判断字符串是否包含时间后缀，并将结果存储在interval结构的delay字段中
	if (SUCCEED != is_time_suffix(text, &interval->delay, (int)(ptr - text)))
		return FAIL;

	// 跳过'/'，继续遍历字符串
	if (0 >= len-- || '/' != *ptr++)
		return FAIL;

	// 调用time_period_parse函数，解析时间间隔部分的字符串
	return time_period_parse(&interval->period, ptr, len);
}


/******************************************************************************
 *                                                                            *
 * Function: calculate_dayofweek                                              *
 *                                                                            *
 * Purpose: calculates day of week                                            *
 *                                                                            *
 * Parameters: year - [IN] the year (>1752)                                   *
 *             mon  - [IN] the month (1-12)                                   *
 *             mday - [IN] the month day (1-31)                               *
 *                                                                            *
 * Return value: The day of week: 1 - Monday, 2 - Tuesday, ...                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是计算给定年、月、日的星期几。函数`calculate_dayofweek`接收三个参数：年份、月份和日期，然后根据这些参数计算出对应的星期几（1-7）。
 *
 *代码注释详细说明如下：
 *
 *1. 定义一个静态数组`mon_table`，存储每个月的天数差值。这是为了方便计算公式中的月份差值。
 *2. 如果给定的月份`mon`小于3，将年份`year`减1。这是因为公元1年只有12个月，所以月份小于3的年份实际上是公元前的年份。
 *3. 计算公式：年份 + 年份 / 4 - 年份 / 100 + 年份 / 400 + 月份差值 + 日期 - 1）% 7 + 1。这个公式是根据格里高利历（公历）制定的，用于计算任意一年的任意一天是星期几。
 *4. 返回计算结果，即星期几（1-7）。
 ******************************************************************************/
/* 定义一个函数，计算给定年、月、日的星期几（1-7）
 * 参数：
 *   year：年份
 *   mon：月份
 *   mday：日期
 * 返回值：
 *   1-7之间的整数，表示星期几
 */
static int	calculate_dayofweek(int year, int mon, int mday)
{
	// 定义一个静态数组，存储每个月的天数差值
	static int	mon_table[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

	// 如果月份小于3，年份减1，因为公元1年只有12个月
	if (mon < 3)
	{
		year--;
	}

	// 计算公式：年份 + 年份 / 4 - 年份 / 100 + 年份 / 400 + 月份差值 + 日期 - 1）% 7 + 1
	return (year + year / 4 - year / 100 + year / 400 + mon_table[mon - 1] + mday - 1) % 7 + 1;
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_filter_free                                            *
 *                                                                            *
 * Purpose: frees scheduler interval filter                                   *
 *                                                                            *
 * Parameters: filter - [IN] scheduler interval filter                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放zbx调度器过滤器链表中的所有过滤器内存。函数通过遍历链表中的每个过滤器，并依次释放其内存，从而实现内存的回收。整个代码块的功能可以概括为：使用循环遍历过滤器链表，逐个释放链表中的过滤器对象。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx调度器过滤器的内存
static void scheduler_filter_free(zbx_scheduler_filter_t *filter)
{
	// 定义一个指向下一个过滤器的指针
	zbx_scheduler_filter_t	*filter_next;

	// 使用一个for循环，遍历所有过滤器
	for (; NULL != filter; filter = filter_next)
	{
		// 保存当前过滤器的下一个过滤器指针
		filter_next = filter->next;

		// 释放当前过滤器的内存
		zbx_free(filter);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_interval_free                                          *
 *                                                                            *
 * Purpose: frees scheduler interval                                          *
 *                                                                            *
 * Parameters: interval - [IN] scheduler interval                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放一组调度器间隔结构（包括月份、星期、小时、分钟和秒等过滤器）以及对应的内存。函数`scheduler_interval_free`接收一个指向调度器间隔结构的指针，遍历该指针所指向的所有间隔结构，并依次释放各个过滤器以及对应的内存。在整个过程中，下一个间隔结构的指针会被保存在`interval_next`变量中，以便后续操作。
 ******************************************************************************/
// 定义一个静态函数，用于释放调度器间隔数据结构
static void scheduler_interval_free(zbx_scheduler_interval_t *interval)
{
	// 定义一个指针，用于指向下一个间隔结构
	zbx_scheduler_interval_t *interval_next;

	// 使用一个for循环，遍历所有有效的间隔结构
	for (; NULL != interval; interval = interval_next)
	{
		// 保存下一个间隔结构的指针，以便后续操作
		interval_next = interval->next;

		// 调用函数，释放mdays（月份）过滤器
		scheduler_filter_free(interval->mdays);

		// 调用函数，释放wdays（星期）过滤器
		scheduler_filter_free(interval->wdays);

		// 调用函数，释放hours（小时）过滤器
		scheduler_filter_free(interval->hours);

		// 调用函数，释放minutes（分钟）过滤器
		scheduler_filter_free(interval->minutes);

		// 调用函数，释放seconds（秒）过滤器
		scheduler_filter_free(interval->seconds);

		// 释放当前间隔结构占用的内存
		zbx_free(interval);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_parse_filter_r                                         *
 *                                                                            *
 * Purpose: parses text string into scheduler filter                          *
 *                                                                            *
 * Parameters: filter  - [IN/OUT] the first filter                            *
 *             text    - [IN] the text to parse                               *
 *             len     - [IN/OUT] the number of characters left to parse      *
 *             min     - [IN] the minimal time unit value                     *
 *             max     - [IN] the maximal time unit value                     *
 *             var_len - [IN] the maximum number of characters for a filter   *
 *                       variable (<from>, <to>, <step>)                      *
 *                                                                            *
 * Return value: SUCCEED - the filter was successfully parsed                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function recursively calls itself for each filter fragment. *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析一个包含数字范围的C语言字符串，将其转换为一个调度器过滤器结构体数组。该函数接受一个指向过滤器数组的指针、过滤器字符串、字符串长度、最小值、最大值和变量长度作为输入参数。在解析过程中，函数会检查字符串中的数字序列、起始位置、结束位置和步长，并根据给定范围创建一个新的过滤器结构体。如果成功解析，函数将更新过滤器数组指针。
 ******************************************************************************/
// 定义一个静态函数，用于解析调度器过滤器
static int scheduler_parse_filter_r(zbx_scheduler_filter_t **filter, const char *text, int *len, int min, int max, int var_len)
{
	// 定义变量，用于存储起始位置、结束位置和步长
	int start = 0, end = 0, step = 1;
	const char *pstart, *pend;
	zbx_scheduler_filter_t *filter_new;

	// 初始化指针
	pstart = pend = text;

	// 遍历字符串，查找数字序列
	while (0 != isdigit(*pend) && 0 < *len)
	{
		pend++;
		(*len)--;
	}

	// 如果起始位置和结束位置不为空，且长度不超过变量长度
	if (pend != pstart)
	{
		if (pend - pstart > var_len)
			return FAIL;

		// 检查起始位置是否在指定范围内
		if (SUCCEED != is_uint_n_range(pstart, pend - pstart, &start, sizeof(start), min, max))
			return FAIL;

		// 检查是否为范围模式
		if ('-' == *pend)
		{
			pstart = pend + 1;

			// 遍历结束位置
			do
			{
				pend++;
				(*len)--;
			}
			while (0 != isdigit(*pend) && 0 < *len);

			// 检查结束位置是否在指定范围内，且起始位置小于等于结束位置
			if (pend == pstart || pend - pstart > var_len)
				return FAIL;

			if (SUCCEED != is_uint_n_range(pstart, pend - pstart, &end, sizeof(end), min, max))
				return FAIL;

			if (end < start)
				return FAIL;
		}
		else
		{
			// 步长仅在指定范围内有效
			if ('/' == *pend)
				return FAIL;

			end = start;
		}
	}
	else
	{
		// 设置起始位置和结束位置为最小和最大值
		start = min;
		end = max;
	}

	// 检查步长字符串
	if ('/' == *pend)
	{
		pstart = pend + 1;

		// 遍历步长字符串
		do
		{
			pend++;
			(*len)--;
		}
		while (0 != isdigit(*pend) && 0 < *len);

		// 检查步长是否在指定范围内
		if (pend == pstart || pend - pstart > var_len)
			return FAIL;

		if (SUCCEED != is_uint_n_range(pstart, pend - pstart, &step, sizeof(step), 1, end - start))
			return FAIL;
	}
	else
	{
		// 如果步长字符串为空，检查下一个字符是否为逗号
		if (pend == text)
			return FAIL;
	}

	// 检查是否还有下一个过滤器
	if (',' == *pend)
	{
		// 遍历下一个过滤器
		if (0 == --(*len))
			return FAIL;

		pend++;

		// 递归调用解析过滤器
		if (SUCCEED != scheduler_parse_filter_r(filter, pend, len, min, max, var_len))
			return FAIL;
	}

	// 分配新过滤器内存
	filter_new = (zbx_scheduler_filter_t *)zbx_malloc(NULL, sizeof(zbx_scheduler_filter_t));
	filter_new->start = start;
	filter_new->end = end;
	filter_new->step = step;
	filter_new->next = *filter;
	*filter = filter_new;

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_parse_filter                                           *
 *                                                                            *
 * Purpose: parses text string into scheduler filter                          *
 *                                                                            *
 * Parameters: filter  - [IN/OUT] the first filter                            *
 *             text    - [IN] the text to parse                               *
 *             len     - [IN/OUT] the number of characters left to parse      *
 *             min     - [IN] the minimal time unit value                     *
 *             max     - [IN] the maximal time unit value                     *
 *             var_len - [IN] the maximum number of characters for a filter   *
 *                       variable (<from>, <to>, <step>)                      *
 *                                                                            *
 * Return value: SUCCEED - the filter was successfully parsed                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function will fail if a filter already exists. This         *
 *           user from defining multiple filters of the same time unit in a   *
 *           single interval. For example: h0h12 is invalid filter and its    *
 *           parsing must fail.                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个字符串 `text`，并根据给定的 `min`、`max` 和 `var_len` 参数生成一个过滤器。最后将解析后的过滤器存储在 `filter` 指针指向的空间中。整个代码块分为两部分：
 *
 *1. 首先检查传入的 `filter` 指针是否为空，如果不为空，则直接返回错误码 `FAIL`。
 *2. 如果 `filter` 指针为空，则调用 `scheduler_parse_filter_r` 函数解析字符串 `text`，并根据给定的参数生成过滤器。最后将生成后的过滤器存储在 `filter` 指针指向的空间中。
 ******************************************************************************/
// 定义一个静态函数，用于解析调度器过滤器
static int scheduler_parse_filter(zbx_scheduler_filter_t **filter, const char *text, int *len, int min, int max, int var_len)
{
    // 检查传入的过滤器指针是否为空，如果不为空，则返回错误码FAIL
    if (NULL != *filter)
        return FAIL;

    // 调用 scheduler_parse_filter_r 函数解析过滤器，并将结果存储在 filter 指针指向的空间中
    return scheduler_parse_filter_r(filter, text, len, min, max, var_len);
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_interval_parse                                         *
 *                                                                            *
 * Purpose: parses scheduler interval                                         *
 *                                                                            *
 * Parameters: interval - [IN/OUT] the first interval                         *
 *             text     - [IN] the text to parse                              *
 *             len      - [IN] the text length                                *
 *                                                                            *
 * Return value: SUCCEED - the interval was successfully parsed               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析一个表示调度器时间间隔的配置字符串。该字符串可能包含小时、分钟、工作日等时间信息。函数`scheduler_interval_parse`接收一个指向配置结构的指针、一个表示配置字符串的字符指针和一个表示字符串长度的整数。函数首先检查字符串长度是否为0，如果为0则返回失败。然后遍历字符串，根据遇到的字符切换到不同的时间解析逻辑。最后返回解析结果。
 ******************************************************************************/
// 定义一个静态函数，用于解析调度器时间间隔配置
static int scheduler_interval_parse(zbx_scheduler_interval_t *interval, const char *text, int len)
{
	// 初始化返回值
	int ret = SUCCEED;

	// 如果输入的字符串长度为0，返回失败
	if (0 == len)
		return FAIL;

	// 遍历字符串
	while (SUCCEED == ret && 0 != len)
	{
		// 记录当前字符串长度
		int	old_len = len--;

		// 切换到下一个字符
		switch (*text)
		{
			// 遇到空字符，返回失败
			case '\0':
				return FAIL;

			// 遇到'h'，表示解析小时数，检查过滤器级别是否满足要求
			case 'h':
				if (ZBX_SCHEDULER_FILTER_HOUR < interval->filter_level)
					return FAIL;

				// 调用函数解析小时数，并更新过滤器级别
				ret = scheduler_parse_filter(&interval->hours, text + 1, &len, 0, 23, 2);
				interval->filter_level = ZBX_SCHEDULER_FILTER_HOUR;

				break;

			// 遇到's'，表示解析秒数，检查过滤器级别是否满足要求
			case 's':
				if (ZBX_SCHEDULER_FILTER_SECOND < interval->filter_level)
					return FAIL;

				// 调用函数解析秒数，并更新过滤器级别
				ret = scheduler_parse_filter(&interval->seconds, text + 1, &len, 0, 59, 2);
				interval->filter_level = ZBX_SCHEDULER_FILTER_SECOND;

				break;

			// 遇到'w'，表示解析工作日，检查过滤器级别是否满足要求
			case 'w':
				if ('d' != text[1])
					return FAIL;

				if (ZBX_SCHEDULER_FILTER_DAY < interval->filter_level)
					return FAIL;
				// 调用函数解析工作日，并更新过滤器级别
				len--;
				ret = scheduler_parse_filter(&interval->wdays, text + 2, &len, 1, 7, 1);
				interval->filter_level = ZBX_SCHEDULER_FILTER_DAY;

				break;

			// 遇到'm'，表示解析分钟数，检查过滤器级别是否满足要求
			case 'm':
				if ('d' == text[1])
				{
					// 如果未设置工作日，或者工作日已设置，则返回失败
					if (ZBX_SCHEDULER_FILTER_DAY < interval->filter_level ||
							NULL != interval->wdays)
					{
						return FAIL;
					}

					// 调用函数解析月度分钟数，并更新过滤器级别
					len--;
					ret = scheduler_parse_filter(&interval->mdays, text + 2, &len, 1, 31, 2);
					interval->filter_level = ZBX_SCHEDULER_FILTER_DAY;
				}
				else
				{
					// 调用函数解析分钟数，并更新过滤器级别
					if (ZBX_SCHEDULER_FILTER_MINUTE < interval->filter_level)
						return FAIL;

					ret = scheduler_parse_filter(&interval->minutes, text + 1, &len, 0, 59, 2);
					interval->filter_level = ZBX_SCHEDULER_FILTER_MINUTE;
				}

				break;

			// 遇到其他字符，返回失败
			default:
				return FAIL;
		}

		// 更新字符串指针
		text += old_len - len;
	}

	// 返回解析结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_get_nearest_filter_value                               *
 *                                                                            *
 * Purpose: gets the next nearest value that satisfies the filter chain       *
 *                                                                            *
 * Parameters: filter - [IN] the filter chain                                 *
 *             value  - [IN] the current value                                *
 *                      [OUT] the next nearest value (>= than input value)    *
 *                                                                            *
 * Return value: SUCCEED - the next nearest value was successfully found      *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是查找给定值最近的过滤器，并将该过滤器的起始值赋给`value`。在遍历过滤器链表的过程中，找到第一个匹配给定值的过滤器，并计算下一个最近的过滤器。如果找到匹配的过滤器，则更新`value`并返回成功；否则，返回失败。
 ******************************************************************************/
/* 定义一个静态函数，用于获取离给定值最近的过滤器值 */
static int	scheduler_get_nearest_filter_value(const zbx_scheduler_filter_t *filter, int *value)
{
    /* 初始化指向下一个过滤器的指针 */
    const zbx_scheduler_filter_t	*filter_next = NULL;

    /* 遍历链表中的所有过滤器 */
    for (; NULL != filter; filter = filter->next)
    {
        /* 查找匹配的过滤器 */
        if (filter->start <= *value && *value <= filter->end)
        {
            int	next = *value, offset;

            /* 应用步长 */
            offset = (next - filter->start) % filter->step;
            if (0 != offset)
                next += filter->step - offset;

            /* 计算值仍在过滤器范围内，则更新值并返回成功 */
            if (next <= filter->end)
            {
                *value = next;
                return SUCCEED;
            }
        }

        /* 查找下一个最近的过滤器 */
            if (filter->start > *value && (NULL == filter_next || filter_next->start > filter->start))
                filter_next = filter;
        }
    }

    /* 值不在任何过滤器的范围内，但我们有下一个最近的过滤器 */
    if (NULL != filter_next)
    {
        *value = filter_next->start;
        return SUCCEED;
    }

    /* 如果没有找到合适的过滤器，返回失败 */
    return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_get_wday_nextcheck                                     *
 *                                                                            *
 * Purpose: calculates the next day that satisfies the week day filter        *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             tm       - [IN/OUT] the input/output date & time               *
 *                                                                            *
 * Return value: SUCCEED - the next day was found                             *
 *               FAIL    - the next day satisfying week day filter was not    *
 *                         found in the current month                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取下一个检查时间点的星期几。函数接收两个参数，一个是指向zbx_scheduler_interval_t结构体的指针，另一个是指向struct tm结构体的指针。在函数内部，首先计算当前日期对应的星期几值，然后获取距离当前日期最近的星期几。如果失败，将日期调整到下一个星期，重置星期几，并重新尝试。最后，根据星期几的偏移调整日期，并检查调整后的日期是否有效。如果有效，返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于获取下一个检查时间点的星期几
static int scheduler_get_wday_nextcheck(const zbx_scheduler_interval_t *interval, struct tm *tm)
{
    // 定义两个整型变量value_now和value_next，用于存储当前和下一个星期几的值
    int value_now, value_next;

    // 如果interval->wdays为空，返回成功
    if (NULL == interval->wdays)
        return SUCCEED;

    // 计算当前日期对应的星期几值
    value_now = value_next = calculate_dayofweek(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

    /* 获取距离当前日期最近的星期几 */
    if (SUCCEED != scheduler_get_nearest_filter_value(interval->wdays, &value_next))
    {
        /* 失败情况下，将日期调整到下一个星期，重置星期几，并重新尝试 */
        tm->tm_mday += 7 - value_now + 1;
        value_now = value_next = 1;

        if (SUCCEED != scheduler_get_nearest_filter_value(interval->wdays, &value_next))
        {
            /* 一个有效的星期几过滤器必须始终匹配新的一周的某一天 */
            THIS_SHOULD_NEVER_HAPPEN;
            return FAIL;
        }
    }

    /* 根据星期几的偏移调整日期 */
    tm->tm_mday += value_next - value_now;

    /* 检查调整后的日期是否有效 */
    return (tm->tm_mday <= zbx_day_in_month(tm->tm_year + 1970, tm->tm_mon + 1) ? SUCCEED : FAIL);
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_validate_wday_filter                                   *
 *                                                                            *
 * Purpose: checks if the specified date satisfies week day filter            *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             tm       - [IN] the date & time to validate                    *
 *                                                                            *
 * Return value: SUCCEED - the input date satisfies week day filter           *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证给定的时间戳是否匹配调度器中的周过滤器。函数接收两个参数，一个是指向zbx_scheduler_interval_t类型的指针，另一个是指向struct tm类型的指针。函数首先判断周过滤器是否为空，若为空则直接返回成功。接着计算给定时间戳的星期几，然后遍历周过滤器链表，判断每个过滤器的start和end是否包含计算出的星期几。如果找到匹配的过滤器，应用过滤器的步长，并判断计算出的时间戳是否仍在过滤器范围内。如果找到匹配的过滤器且时间戳仍在过滤器范围内，则返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于验证调度器中的周过滤器是否匹配给定的时间戳
static int scheduler_validate_wday_filter(const zbx_scheduler_interval_t *interval, struct tm *tm)
{
	// 声明一个指向zbx_scheduler_filter_t类型的指针
	const zbx_scheduler_filter_t	*filter;
	// 声明一个整型变量value，用于存储计算结果
	int				value;

	// 判断interval->wdays是否为空，若为空则返回成功，表示没有周过滤器
	if (NULL == interval->wdays)
		return SUCCEED;

	// 计算给定时间戳的星期几
	value = calculate_dayofweek(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

	// 遍历interval->wdays链表中的每个过滤器
	for (filter = interval->wdays; NULL != filter; filter = filter->next)
	{
		// 判断当前过滤器的start和end是否包含计算出的value
		if (filter->start <= value && value <= filter->end)
		{
			// 声明两个整型变量next和offset，用于计算下一步的时间戳
			int	next = value, offset;

			// 应用过滤器的步长
			offset = (next - filter->start) % filter->step;
			if (0 != offset)
				next += filter->step - offset;

			// 判断计算出的next是否仍在过滤器范围内
			if (next <= filter->end)
				return SUCCEED;
		}
	}

	// 如果没有找到匹配的过滤器，返回失败
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_get_day_nextcheck                                      *
 *                                                                            *
 * Purpose: calculates the next day that satisfies month and week day filters *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             tm       - [IN/OUT] the input/output date & time               *
 *                                                                            *
 * Return value: SUCCEED - the next day was found                             *
 *               FAIL    - the next day satisfying day filters was not        *
 *                         found in the current month                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取下一个检查时间。函数`scheduler_get_day_nextcheck`接收两个参数，一个`zbx_scheduler_interval_t`类型的指针（表示计划任务间隔），和一个`struct tm`类型的指针（表示当前时间）。函数首先检查提供的`tm`结构是否有有效的日期格式，然后根据计划任务间隔和当前时间来计算下一个检查时间。如果计划任务间隔中包含周过滤条件，函数会遍历月份的天数，直到找到符合周过滤条件的下一个日期或遍历结束。如果找到符合周过滤条件的日期，则返回SUCCEED，表示找到下一个检查时间；否则，返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于获取下一个检查时间
static int scheduler_get_day_nextcheck(const zbx_scheduler_interval_t *interval, struct tm *tm)
{
	int	tmp; // 定义一个临时变量tmp

	/* 首先检查提供的tm结构是否有有效的日期格式 */
	if (FAIL == zbx_utc_time(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
			&tmp))
	{
		return FAIL; // 如果日期格式无效，返回FAIL
	}

	if (NULL == interval->mdays) // 如果interval结构中的mdays为空
		return scheduler_get_wday_nextcheck(interval, tm); // 则调用get_wday_nextcheck函数获取下一个检查时间

	/* 遍历月份的天数，直到找到符合周过滤条件的下一个日期或遍历结束 */
	while (SUCCEED == scheduler_get_nearest_filter_value(interval->mdays, &tm->tm_mday))
	{
		/* 检查日期是否有效（还没有遍历到月底） */
		if (tm->tm_mday > zbx_day_in_month(tm->tm_year + 1970, tm->tm_mon + 1))
			break;

		if (SUCCEED == scheduler_validate_wday_filter(interval, tm)) // 如果找到符合周过滤条件的日期
			return SUCCEED; // 返回SUCCEED，表示找到下一个检查时间

		tm->tm_mday++; // 否则，月份天数加1

		/* 检查日期是否有效（还没有遍历到月底） */
		if (tm->tm_mday > zbx_day_in_month(tm->tm_year + 1970, tm->tm_mon + 1))
			break;
	}

	return FAIL; // 如果没有找到下一个检查时间，返回FAIL
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_get_filter_nextcheck                                   *
 *                                                                            *
 * Purpose: calculates the time/day that satisfies the specified filter       *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             level    - [IN] the filter level, see ZBX_SCHEDULER_FILTER_*   *
 *                        defines                                             *
 *             tm       - [IN/OUT] the input/output date & time               *
 *                                                                            *
 * Return value: SUCCEED - the next time/day was found                        *
 *               FAIL    - the next time/day was not found on the current     *
 *                         filter level                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的过滤器级别、时间间隔和时间结构体，判断当前时间是否满足过滤器条件。代码中使用了switch语句根据过滤器级别初始化数据，然后判断当前值与最大值的关系，最后调用另一个函数获取最近的过滤器值。如果满足过滤器条件，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
static int	scheduler_get_filter_nextcheck(const zbx_scheduler_interval_t *interval, int level, struct tm *tm)
{
	/* 定义一个指向zbx_scheduler_filter_t结构体的指针 */
	const zbx_scheduler_filter_t	*filter;
	/* 定义一个整型指针，用于存储tm结构体中的小时、分钟、秒等值 */
	int				max, *value;

	/* 根据filter level初始化数据 */
	switch (level)
	{
		/* 当filter level为ZBX_SCHEDULER_FILTER_DAY时，调用另一个函数获取下一检查时间 */
		case ZBX_SCHEDULER_FILTER_DAY:
			return scheduler_get_day_nextcheck(interval, tm);
		/* 当filter level为ZBX_SCHEDULER_FILTER_HOUR时，设置最大值为23，并将filter和value指向tm结构体中的小时、分钟、秒等值 */
		case ZBX_SCHEDULER_FILTER_HOUR:
			max = 23;
			filter = interval->hours;
			value = &tm->tm_hour;
			break;
		/* 当filter level为ZBX_SCHEDULER_FILTER_MINUTE时，设置最大值为59，并将filter和value指向tm结构体中的小时、分钟、秒等值 */
		case ZBX_SCHEDULER_FILTER_MINUTE:
			max = 59;
			filter = interval->minutes;
			value = &tm->tm_min;
			break;
		/* 当filter level为ZBX_SCHEDULER_FILTER_SECOND时，设置最大值为59，并将filter和value指向tm结构体中的小时、分钟、秒等值 */
		case ZBX_SCHEDULER_FILTER_SECOND:
			max = 59;
			filter = interval->seconds;
			value = &tm->tm_sec;
			break;
		/* 当filter level为其他值时，表示错误，返回FAIL */
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	/* 如果当前值大于最大值，返回FAIL */
	if (max < *value)
		return FAIL;

	/* 处理未指定的过滤器（默认过滤器） */
	if (NULL == filter)
	{
		/* 空过滤器匹配所有有效值，如果过滤器级别小于间隔过滤器级别。例如，如果间隔过滤器级别为分钟 - m30，则小时过滤器匹配所有小时。 */
		if (interval->filter_level > level)
			return SUCCEED;

		/* 如果过滤器级别大于间隔过滤器级别，则过滤器仅匹配0值。例如，如果间隔过滤器级别为分钟 - m30，则秒过滤器匹配第0秒。 */
		return 0 == *value ? SUCCEED : FAIL;
	}

	/* 调用另一个函数，获取最近的过滤器值 */
	return scheduler_get_nearest_filter_value(filter, value);
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_apply_day_filter                                       *
 *                                                                            *
 * Purpose: applies day filter to the specified time/day calculating the next *
 *          scheduled check                                                   *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             tm       - [IN/OUT] the input/output date & time               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是应用一天的时间过滤器。该函数接收两个参数，一个是 zbx_scheduler_interval_t 类型的指针 interval，表示时间间隔；另一个是 struct tm 类型的指针 tm，表示时间结构体。
 *
 *函数内部首先提取 tm 结构体中的月份、日期和年份，然后循环调用 scheduler_get_filter_nextcheck 函数，直到返回值不为 SUCCEED。在循环过程中，如果月份加一后大于11，说明需要进入下一年，于是重置月份为0，年份加一。日期重置为1。
 *
 *当循环结束时，判断日期、月份或年份是否发生了变化，如果发生变化，则将小时、分钟和秒重置为0。这样就实现了根据时间变化应用一天的时间过滤器。
 ******************************************************************************/
// 定义一个名为 scheduler_apply_day_filter 的静态函数
static void	scheduler_apply_day_filter(zbx_scheduler_interval_t *interval, struct tm *tm) // 接收两个参数，一个是 zbx_scheduler_interval_t 类型的指针 interval，另一个是 struct tm 类型的指针 tm
{
	int	day = tm->tm_mday, mon = tm->tm_mon, year = tm->tm_year; // 分别提取 tm 结构体中的月份、日期和年份

	while (SUCCEED != scheduler_get_filter_nextcheck(interval, ZBX_SCHEDULER_FILTER_DAY, tm)) // 循环调用 scheduler_get_filter_nextcheck 函数，直到返回值不为 SUCCEED
	{
		if (11 < ++tm->tm_mon) // 如果月份加一后大于11，即进入下一年
		{
			tm->tm_mon = 0; // 重置月份为0
			tm->tm_year++; // 年份加一
		}

		tm->tm_mday = 1; // 日期重置为1
	}

	/* reset hours, minutes and seconds if the day has been changed */ // 如果日期、月份或年份发生了变化，则重置小时、分钟和秒
	if (tm->tm_mday != day || tm->tm_mon != mon || tm->tm_year != year)
	{
		tm->tm_hour = 0;
		tm->tm_min = 0;
		tm->tm_sec = 0;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_apply_hour_filter                                      *
 *                                                                            *
 * Purpose: applies hour filter to the specified time/day calculating the     *
 *          next scheduled check                                              *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             tm       - [IN/OUT] the input/output date & time               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是应用小时过滤器。首先获取当前时间结构体中的小时值，然后循环查询下一个小时过滤器检查时间。当当天已过时，小时数加1，并重置小时数为0。当天发生更改时，重新应用天过滤器。当天未更改，但小时数已更改时，重置分钟和秒。
 ******************************************************************************/
/* 定义一个静态函数，用于应用小时过滤器 */
static void scheduler_apply_hour_filter(zbx_scheduler_interval_t *interval, struct tm *tm)
{
	/* 获取当前小时 */
	int hour = tm->tm_hour;

	/* 循环查询下一个小时过滤器检查时间 */
	while (SUCCEED != scheduler_get_filter_nextcheck(interval, ZBX_SCHEDULER_FILTER_HOUR, tm))
	{
		/* 当天已过，小时数加1 */
		tm->tm_mday++;
		/* 重置小时数为0 */
		tm->tm_hour = 0;

		/* 当天已更改，重新应用天过滤器 */
		scheduler_apply_day_filter(interval, tm);
	}

	/* 当天未更改，但小时数已更改，重置分钟和秒 */
	if (tm->tm_hour != hour)
	{
		tm->tm_min = 0;
		tm->tm_sec = 0;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_apply_minute_filter                                    *
 *                                                                            *
 * Purpose: applies minute filter to the specified time/day calculating the   *
 *          next scheduled check                                              *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             tm       - [IN/OUT] the input/output date & time               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是应用分钟过滤器。它接收两个参数，一个`zbx_scheduler_interval_t`类型的指针`interval`和一个`struct tm`类型的指针`tm`。在循环中，代码会等待下一次过滤器检查的到来，当到达下一次检查时，会更新小时和分钟值。如果小时值发生更改，还需要重新应用小时过滤器。当分钟值发生更改时，将秒值重置为0。整个过程使用结构体`tm`来保存时间信息。
 ******************************************************************************/
static void scheduler_apply_minute_filter(zbx_scheduler_interval_t *interval, struct tm *tm)
{
    /* 定义一个静态函数，用于应用分钟过滤器 */

    int min = tm->tm_min; // 获取当前分钟值

    while (SUCCEED != scheduler_get_filter_nextcheck(interval, ZBX_SCHEDULER_FILTER_MINUTE, tm))
    {
        /* 如果下一次过滤器检查还未到达，则循环等待 */

        tm->tm_hour++; // 小时值加1
        tm->tm_min = 0; // 分钟值重置为0

        /* 小时值已更改，需要重新应用小时过滤器 */
        scheduler_apply_hour_filter(interval, tm);
    }

    /* 如果分钟值已更改，重置秒值 */
    if (tm->tm_min != min)
        tm->tm_sec = 0;
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_apply_second_filter                                    *
 *                                                                            *
 * Purpose: applies second filter to the specified time/day calculating the   *
 *          next scheduled check                                              *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             tm       - [IN/OUT] the input/output date & time               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是应用第二个过滤器（秒级别）到给定的时间间隔（interval）和时间结构体（tm）上。具体操作是使用一个无限循环，不断检查过滤器是否需要应用。当需要应用过滤器时，先将分钟数加1，然后将秒数重置为0，最后重新应用分钟过滤器。整个过程中，使用了zbx_scheduler_interval_t结构和struct tm结构来存储和处理时间信息。
 ******************************************************************************/
// 定义一个静态函数，用于应用第二个过滤器（秒级别）
static void scheduler_apply_second_filter(zbx_scheduler_interval_t *interval, struct tm *tm)
{
    // 使用一个无限循环来不断检查过滤器是否需要应用
    while (SUCCEED != scheduler_get_filter_nextcheck(interval, ZBX_SCHEDULER_FILTER_SECOND, tm))
    {
        // 当前分钟数加1
        tm->tm_min++;
        // 秒数重置为0
        tm->tm_sec = 0;

        /* 分钟已经改变，我们需要重新应用分钟过滤器 */
        scheduler_apply_minute_filter(interval, tm);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_find_dst_change                                        *
 *                                                                            *
 * Purpose: finds daylight saving change time inside specified time period    *
 *                                                                            *
 * Parameters: time_start - [IN] the time period start                        *
 *             time_end   - [IN] the time period end                          *
 *                                                                            *
 * Return Value: Time when the daylight saving changes should occur.          *
 *                                                                            *
 * Comments: The calculated time is cached and reused if it first the         *
 *           specified period.                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算两个时间戳之间的夏令时变化。输入两个时间戳（time_start和time_end），输出它们之间的夏令时变化。代码首先判断当前夏令时是否需要更新，然后通过循环寻找起始、中间和结束时间点，最后计算出夏令时的变化并返回。
 ******************************************************************************/
// 定义一个静态变量，用于存储计算结果
static time_t	scheduler_find_dst_change(time_t time_start, time_t time_end) // 定义一个函数，接收两个时间戳作为参数
{
	static time_t	// 定义一个静态变量，用于存储夏令时变量
	time_dst = 0;
	struct tm	*tm; // 定义一个结构体指针，用于存储时间信息
	time_t		time_mid; // 定义一个时间戳变量，用于存储中间时间
	int		// 定义一个整型变量，用于存储计算结果
	start, end, mid, dst_start;

	if (time_dst < time_start || time_dst > time_end) // 如果当前夏令时变量小于时间戳start，或大于时间戳time_end
	{
		/* assume that daylight saving will change only on 0 seconds */ // 假设夏令时的变更只发生在整秒
		start = time_start / 60; // 将时间戳转换为分钟
		end = time_end / 60; // 将时间戳转换为分钟

		tm = localtime(&time_start); // 使用localtime函数将时间戳转换为结构体
		dst_start = tm->tm_isdst; // 获取夏令时变量

		while (end > start + 1) // 当end大于start+1时，进行循环
		{
			mid = (start + end) / 2; // 计算中间时间
			time_mid = mid * 60; // 将分钟转换为时间戳

			tm = localtime(&time_mid); // 使用localtime函数将时间戳转换为结构体

			if (tm->tm_isdst == dst_start) // 如果当前时间段的夏令时变量与dst_start相同
				start = mid; // 更新start为mid
			else
				end = mid; // 更新end为mid
		}

		time_dst = end * 60; // 计算最终夏令时变量
	}

	return time_dst; // 返回计算得到的夏令时变量
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_tm_inc                                                 *
 *                                                                            *
 * Purpose: increment struct tm value by one second                           *
 *                                                                            *
 * Parameters: tm - [IN/OUT] the tm structure to increment                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对一个 `struct tm` 类型的结构体进行时间调整，将其调整为下一个小时、下一天、下一月和下一年的时间。具体操作如下：
 *
 *1. 调整秒数，如果加一后不超过60，继续执行下一行代码。
 *2. 调整分钟数，如果加一后不超过60，继续执行下一行代码。
 *3. 调整小时数，如果加一后不超过24，继续执行下一行代码。
 *4. 调整月天数，如果加一后不超过一年中的天数，继续执行下一行代码。
 *5. 调整月份，如果加一后不超过12，继续执行下一行代码。
 *6. 调整年份，年份加一。
 *
 *当以上所有条件都满足时，函数执行完毕，返回 void 类型，不占用返回值。
 ******************************************************************************/
// 声明一个名为 scheduler_tm_inc 的静态函数，参数为一个 struct tm 类型的指针 tm
static void	scheduler_tm_inc(struct tm *tm) // 函数 scheduler_tm_inc 的实现，接收一个 struct tm 类型的指针作为参数
{
	if (60 > ++tm->tm_sec) // 如果当前秒数加一后不超过60，执行下一行代码
		return; // 结束函数执行

	tm->tm_sec = 0; // 秒数清零，进入下一分钟
	if (60 > ++tm->tm_min) // 如果当前分钟数加一后不超过60，执行下一行代码
		return; // 结束函数执行

	tm->tm_min = 0; // 分钟数清零，进入下一小时
	if (24 > ++tm->tm_hour) // 如果当前小时数加一后不超过24，执行下一行代码
		return; // 结束函数执行

	tm->tm_hour = 0; // 小时数清零，进入下一天
	if (zbx_day_in_month(tm->tm_year + 1900, tm->tm_mon + 1) >= ++tm->tm_mday) // 如果当前月天数加一后不超过一年中的天数，执行下一行代码
		return; // 结束函数执行

	tm->tm_mday = 1; // 月天数清零，进入下一月
	if (12 > ++tm->tm_mon) // 如果当前月份加一后不超过12，执行下一行代码
		return; // 结束函数执行

	tm->tm_mon = 0; // 月份清零，进入下一年
	tm->tm_year++; // 年份加一
	return; // 函数执行完毕，返回 void 类型，不占用返回值
}


/******************************************************************************
 *                                                                            *
 * Function: scheduler_get_nextcheck                                          *
 *                                                                            *
 * Purpose: finds the next timestamp satisfying one of intervals.             *
 *                                                                            *
 * Parameters: interval - [IN] the scheduler interval                         *
 *             now      - [IN] the current timestamp                          *
 *                                                                            *
 * Return Value: Timestamp when the next check must be scheduled.             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算并返回一个下一个检查时间。该函数接收两个参数，一个指向zbx_scheduler_interval_t结构体的指针，另一个是一个时间戳。函数首先将当前时间转换为结构体变量，然后遍历interval链表，对每个间隔应用日、小时、分钟和秒过滤器，以计算下一个检查时间。在计算过程中，还会考虑夏令时变化的影响。最后，返回下一个检查时间。
 ******************************************************************************/
// 定义一个静态函数，用于获取下一个检查时间
static time_t	scheduler_get_nextcheck(zbx_scheduler_interval_t *interval, time_t now)
{
	// 定义一个结构体变量，用于存储时间信息
	struct tm	tm_start, tm, tm_dst;
	// 定义一个时间变量，用于存储下一个检查时间
	time_t		nextcheck = 0, current_nextcheck;

	// 使用本地时间转换函数将当前时间转换为结构体变量tm_start
	tm_start = *(localtime(&now));

	// 遍历interval链表，查找下一个检查时间
	for (; NULL != interval; interval = interval->next)
	{
		// 复制tm_start的时间信息到tm变量
		tm = tm_start;

		// 采用循环方式，不断尝试计算下一个检查时间
		do
		{
			// 递增时间变量tm，并应用日、小时、分钟和秒过滤器
			scheduler_tm_inc(&tm);
			scheduler_apply_day_filter(interval, &tm);
			scheduler_apply_hour_filter(interval, &tm);
			scheduler_apply_minute_filter(interval, &tm);
			scheduler_apply_second_filter(interval, &tm);

			// 设置tm变量中的夏令时信息
			tm.tm_isdst = tm_start.tm_isdst;

		}
		while (-1 == (current_nextcheck = mktime(&tm)));

		// 获取当前下一个检查时间对应的时间结构体变量tm_dst
		tm_dst = *(localtime(&current_nextcheck));
		// 判断tm_dst的夏令时信息是否与tm_start不同
		if (tm_dst.tm_isdst != tm_start.tm_isdst)
		{
			// 计算夏令时变化的时间差
			int	dst = tm_dst.tm_isdst;
			time_t	time_dst;

			// 查找夏令时变化的时间点
			time_dst = scheduler_find_dst_change(now, current_nextcheck);
			// 更新tm_dst的时间信息
			tm_dst = *localtime(&time_dst);

			// 应用日、小时、分钟和秒过滤器，更新tm_dst的时间
			scheduler_apply_day_filter(interval, &tm_dst);
			scheduler_apply_hour_filter(interval, &tm_dst);
			scheduler_apply_minute_filter(interval, &tm_dst);
			scheduler_apply_second_filter(interval, &tm_dst);

			// 设置tm_dst的夏令时信息
			tm_dst.tm_isdst = dst;
			// 更新当前下一个检查时间
			current_nextcheck = mktime(&tm_dst);
		}

		// 判断当前下一个检查时间是否比之前的下一个检查时间更小，如果是则更新nextcheck
		if (0 == nextcheck || current_nextcheck < nextcheck)
			nextcheck = current_nextcheck;
	}

	// 返回下一个检查时间
	return nextcheck;
}


/******************************************************************************
 *                                                                            *
 * Function: parse_user_macro                                                 *
 *                                                                            *
 * Purpose: parses user macro and finds it's length                           *
 *                                                                            *
 * Parameters: str  - [IN] string to check                                    *
 *             len  - [OUT] length of macro                                   *
 *                                                                            *
 * Return Value:                                                              *
 *     SUCCEED - the macro was parsed successfully.                           *
 *     FAIL    - the macro parsing failed, the content of output variables    *
 *               is not defined.                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个用户自定义的宏，并将解析结果（包括宏名和上下文）存储在指定的变量中。如果解析失败，返回失败；如果成功，返回成功并更新 len 指向的内存地址中的值。
 ******************************************************************************/
// 定义一个名为 parse_user_macro 的静态函数，该函数接收两个参数：一个字符指针 str 以及一个整数指针 len。
static int	parse_user_macro(const char *str, int *len)
{
	// 定义三个整数变量 macro_r、context_l 和 context_r，用于存储解析结果。
	int	macro_r, context_l, context_r;

	// 检查字符串的开头是否为 '{'，如果不是，则返回失败。
	if ('{' != *str || '$' != *(str + 1) || SUCCEED != zbx_user_macro_parse(str, &macro_r, &context_l, &context_r))
		// 如果解析失败，返回 FAIL。
		return FAIL;

	// 计算宏的长度，并将其存储在 len 指向的内存地址中。
	*len = macro_r + 1;

	// 解析成功，返回 SUCCEED。
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: parse_simple_interval                                            *
 *                                                                            *
 * Purpose: parses user macro and finds it's length                           *
 *                                                                            *
 * Parameters: str   - [IN] string to check                                   *
 *             len   - [OUT] length simple interval string until separator    *
 *             sep   - [IN] separator to calculate length                     *
 *             value - [OUT] interval value                                   *
 *                                                                            *
 * Return Value:                                                              *
 *     SUCCEED - the macro was parsed successfully.                           *
 *     FAIL    - the macro parsing failed, the content of output variables    *
 *               is not defined.                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个简单的字符串间隔，如：\"10-20\"。函数接受四个参数，分别是字符串、字符串长度指针、分隔符和值指针。函数首先查找分隔符的位置，然后根据分隔符的位置计算字符串的长度，最后返回成功或失败。如果分隔符不存在，则整个字符串都是值，长度等于字符串长度。
 ******************************************************************************/
/* 定义一个函数，用于解析简单的字符串间隔，例如："10-20" --> 10, 20
 * 参数：
 *   str：表示字符串，例如："10-20"
 *   len：指向整数的指针，表示字符串的长度
 *   sep：表示间隔的字符，例如：'-'
 *   value：指向整数的指针，表示解析后的值
 * 返回值：
 *   SUCCEED：成功
 *   FAIL：失败
 */
static int	parse_simple_interval(const char *str, int *len, char sep, int *value)
{
	/* 定义一个常量指针，用于指向字符串的分隔符 */
	const char	*delim;

	/* 检查字符串是否包含分隔符，如果不包含，直接返回失败 */
	if (SUCCEED != is_time_suffix(str, value,
			(int)(NULL == (delim = strchr(str, sep)) ? ZBX_LENGTH_UNLIMITED : delim - str)))
	{
		return FAIL;
	}

	/* 如果分隔符不存在，表示整个字符串都是值，此时 len 等于字符串长度 */
	*len = NULL == delim ? (int)strlen(str) : delim - str;

	/* 解析成功，返回 SUCCEED */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_validate_interval                                            *
 *                                                                            *
 * Purpose: validate update interval, flexible and scheduling intervals       *
 *                                                                            *
 * Parameters: str   - [IN] string to check                                   *
 *             error - [OUT] validation error                                 *
 *                                                                            *
 * Return Value:                                                              *
 *     SUCCEED - parsed successfully.                                         *
 *     FAIL    - parsing failed.                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是验证一个字符串表示的更新间隔是否合法。输入字符串可以是简单的整数间隔，也可以是包含用户自定义宏和时间周期的复杂间隔。代码首先尝试解析用户自定义宏，然后解析简单间隔。如果解析失败，它会尝试解析更复杂的自定义间隔。在解析过程中，代码会检查间隔是否符合要求，例如不超过一天。如果解析成功，函数返回SUCCEED，否则返回FAIL并打印错误信息。
 ******************************************************************************/
int zbx_validate_interval(const char *str, char **error)
{
	// 定义变量
	int		simple_interval, interval, len, custom = 0, macro;
	const char	*delim;

	// 解析用户自定义宏
	if (SUCCEED == parse_user_macro(str, &len) && ('\0' == *(delim = str + len) || ';' == *delim))
	{
		// 如果结尾为空或分号，设置简单间隔为1
		if ('\0' == *delim)
			delim = NULL;

		simple_interval = 1;
	}
	else if (SUCCEED == parse_simple_interval(str, &len, ';', &simple_interval))
	{
		// 如果结尾为空或分号，设置简单间隔
		if ('\0' == *(delim = str + len))
			delim = NULL;
	}
	else
	{
		// 解析失败，打印错误信息并返回FAIL
		*error = zbx_dsprintf(*error, "Invalid update interval \"%.*s\".",
				NULL == (delim = strchr(str, ';')) ? (int)strlen(str) : (int)(delim - str), str);
		return FAIL;
	}

	// 循环解析分隔符后面的自定义间隔
	while (NULL != delim)
	{
		str = delim + 1;

		// 解析用户自定义宏或简单间隔
		if ((SUCCEED == (macro = parse_user_macro(str, &len)) ||
				SUCCEED == parse_simple_interval(str, &len, '/', &interval)) &&
				'/' == *(delim = str + len))
		{
			zbx_time_period_t period;

			custom = 1;

			// 如果解析成功，继续处理下一个字符
			if (SUCCEED == macro)
				interval = 1;

			if (0 == interval && 0 == simple_interval)
			{
				*error = zbx_dsprintf(*error, "Invalid flexible interval \"%.*s\".", (int)(delim - str),
						str);
				return FAIL;
			}

			str = delim + 1;

			if (SUCCEED == parse_user_macro(str, &len) && ('\0' == *(delim = str + len) || ';' == *delim))
			{
				if ('\0' == *delim)
					delim = NULL;

				continue;
			}

			if (SUCCEED == time_period_parse(&period, str,
					NULL == (delim = strchr(str, ';')) ? (int)strlen(str) : (int)(delim - str)))
			{
				// 继续处理下一个字符
				continue;
			}

			*error = zbx_dsprintf(*error, "Invalid flexible period \"%.*s\".",
					NULL == delim ? (int)strlen(str) : (int)(delim - str), str);
			return FAIL;
		}
		else
		{
			// 解析自定义间隔
			zbx_scheduler_interval_t	*new_interval;

			custom = 1;

			if (SUCCEED == macro && ('\0' == *(delim = str + len) || ';' == *delim))
			{
				if ('\0' == *delim)
					delim = NULL;

				// 继续处理下一个字符
				continue;
			}

			new_interval = (zbx_scheduler_interval_t *)zbx_malloc(NULL, sizeof(zbx_scheduler_interval_t));
			memset(new_interval, 0, sizeof(zbx_scheduler_interval_t));

			// 解析成功，释放内存并继续处理下一个字符
			if (SUCCEED == scheduler_interval_parse(new_interval, str,
					NULL == (delim = strchr(str, ';')) ? (int)strlen(str) : (int)(delim - str)))
			{
				scheduler_interval_free(new_interval);
				continue;
			}
			// 解析失败，释放内存并打印错误信息
			scheduler_interval_free(new_interval);

			*error = zbx_dsprintf(*error, "Invalid custom interval \"%.*s\".",
					NULL == delim ? (int)strlen(str) : (int)(delim - str), str);

			return FAIL;
		}
	}

	// 检查更新间隔是否合法，打印错误信息并返回FAIL if 不合法
	if ((0 == custom && 0 == simple_interval) || SEC_PER_DAY < simple_interval)
	{
		*error = zbx_dsprintf(*error, "Invalid update interval \"%d\"", simple_interval);
		return FAIL;
	}

	// 解析成功，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_interval_preproc                                             *
 *                                                                            *
 * Purpose: parses item and low-level discovery rule update intervals         *
 *                                                                            *
 * Parameters: interval_str     - [IN] update interval string to parse        *
 *             simple_interval  - [OUT] simple update interval                *
 *             custom_intervals - [OUT] flexible and scheduling intervals     *
 *             error            - [OUT] error message                         *
 *                                                                            *
 * Return value: SUCCEED - intervals are valid                                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: !!! Don't forget to sync code with PHP !!!                       *
 *           Supported format:                                                *
 *             SimpleInterval, {";", FlexibleInterval | SchedulingInterval};  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码的主要目的是解析一个由多个时间间隔组成的字符串，这些时间间隔可能是灵活间隔或调度间隔。解析完成后，将结果存储在一个结构体数组中，并返回给调用者。如果解析失败，则释放内存并返回错误信息。
 ******************************************************************************/
int zbx_interval_preproc(const char *interval_str, int *simple_interval, zbx_custom_interval_t **custom_intervals, char **error)
{
	/* 定义变量 */
	zbx_flexible_interval_t *flexible = NULL;
	zbx_scheduler_interval_t *scheduling = NULL;
	const char *delim, *interval_type;

	/* 检查时间间隔字符串是否合法 */
	if (SUCCEED != is_time_suffix(interval_str, simple_interval,
			(int)(NULL == (delim = strchr(interval_str, ';')) ? ZBX_LENGTH_UNLIMITED : delim - interval_str)))
	{
		interval_type = "update";
		goto fail;
	}

	/* 如果不需要定制间隔，直接返回成功 */
	if (NULL == custom_intervals)
		return SUCCEED;

	/* 遍历时间间隔字符串中的每个间隔 */
	while (NULL != delim)
	{
		interval_str = delim + 1;
		delim = strchr(interval_str, ';');

		/* 如果是数字，则解析为灵活间隔 */
		if (0 != isdigit(*interval_str))
		{
			zbx_flexible_interval_t *new_interval;

			new_interval = (zbx_flexible_interval_t *)zbx_malloc(NULL, sizeof(zbx_flexible_interval_t));

			/* 解析灵活间隔 */
			if (SUCCEED != flexible_interval_parse(new_interval, interval_str,
					(NULL == delim ? (int)strlen(interval_str) : (int)(delim - interval_str))) ||
					(0 == *simple_interval && 0 == new_interval->delay))
			{
				zbx_free(new_interval);
				interval_type = "flexible";
				goto fail;
			}

			new_interval->next = flexible;
			flexible = new_interval;
		}
		else
		{
			/* 否则，解析为调度间隔 */
			zbx_scheduler_interval_t *new_interval;

			new_interval = (zbx_scheduler_interval_t *)zbx_malloc(NULL, sizeof(zbx_scheduler_interval_t));
			memset(new_interval, 0, sizeof(zbx_scheduler_interval_t));

			/* 解析调度间隔 */
			if (SUCCEED != scheduler_interval_parse(new_interval, interval_str,
					(NULL == delim ? (int)strlen(interval_str) : (int)(delim - interval_str))))
			{
				scheduler_interval_free(new_interval);
				interval_type = "scheduling";
				goto fail;
			}

			new_interval->next = scheduling;
			scheduling = new_interval;
		}
	}

	/* 检查解析后的间隔是否合法 */
	if ((NULL == flexible && NULL == scheduling && 0 == *simple_interval) || SEC_PER_DAY < *simple_interval)
	{
		interval_type = "update";
		goto fail;
	}

	/* 分配内存并返回成功 */
	*custom_intervals = (zbx_custom_interval_t *)zbx_malloc(NULL, sizeof(zbx_custom_interval_t));
	(*custom_intervals)->flexible = flexible;
	(*custom_intervals)->scheduling = scheduling;

	return SUCCEED;
fail:
	/* 解析失败，释放内存并返回错误信息 */
	if (NULL != error)
	{
		*error = zbx_dsprintf(*error, "Invalid %s interval \"%.*s\".", interval_type,
				(NULL == delim ? (int)strlen(interval_str) : (int)(delim - interval_str)),
				interval_str);
	}

	flexible_interval_free(flexible);
	scheduler_interval_free(scheduling);

	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_custom_interval_free                                         *
 *                                                                            *
 * Purpose: frees custom update intervals                                     *
 *                                                                            *
 * Parameters: custom_intervals - [IN] custom intervals                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放一个zbx_custom_interval结构体数组所占用的内存。在这个函数中，首先释放数组中的第一个元素（flexible_interval结构体），然后释放第二个元素（scheduler_interval结构体），最后释放整个数组（zbx_custom_interval结构体）。
 ******************************************************************************/
// 定义一个函数，用于释放zbx_custom_interval结构体数组中的内存
void zbx_custom_interval_free(zbx_custom_interval_t *custom_intervals)
{
    // 首先，释放custom_intervals指向的第一个flexible_interval结构体的内存
    flexible_interval_free(custom_intervals->flexible);

    // 接着，释放custom_intervals指向的第二个scheduler_interval结构体的内存
    scheduler_interval_free(custom_intervals->scheduling);

    // 最后，释放custom_intervals本身所占用的内存
    zbx_free(custom_intervals);
}


/******************************************************************************
 *                                                                            *
 * Function: calculate_item_nextcheck                                         *
 *                                                                            *
 * Purpose: calculate nextcheck timestamp for item                            *
 *                                                                            *
 * Parameters: seed             - [IN] the seed value applied to delay to     *
 *                                     spread item checks over the delay      *
 *                                     period                                 *
 *             item_type        - [IN] the item type                          *
 *             simple_interval  - [IN] default delay value, can be overridden *
 *             custom_intervals - [IN] preprocessed custom intervals          *
 *             now              - [IN] current timestamp                      *
 *                                                                            *
 * Return value: nextcheck value                                              *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 * Comments: if item check is forbidden with delay=0 (default and flexible),  *
 *           a timestamp very far in the future is returned                   *
 *                                                                            *
 *           Old algorithm: now+delay                                         *
 *           New one: preserve period, if delay==5, nextcheck = 0,5,10,15,... *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是计算出一个物品的下一个检查时间。根据物品的类型和配置，通过不同的算法计算出下一个检查时间点。输出结果为一个整数，表示下一个检查时间。代码中使用了递归调用，处理了不同类型的物品和自定义间隔，同时还考虑了计划检查时间的影响。
 ******************************************************************************/
// 定义一个函数，计算下一个检查时间点
int calculate_item_nextcheck(zbx_uint64_t seed, int item_type, int simple_interval,
                            const zbx_custom_interval_t *custom_intervals, time_t now)
{
    int	nextcheck = 0; // 初始化下一个检查时间为0

    /* special processing of active items to see better view in queue */
    if (ITEM_TYPE_ZABBIX_ACTIVE == item_type) // 如果物品类型为ZABBIX_ACTIVE
    {
        if (0 != simple_interval) // 如果简单间隔不为0
            nextcheck = (int)now + simple_interval; // 下一个检查时间为当前时间加上简单间隔
        else
            nextcheck = ZBX_JAN_2038; // 否则，下一个检查时间为2038年1月1日
    }
    else // 如果物品类型不是ZABBIX_ACTIVE
    {
        int	current_delay = 0, attempt = 0; // 初始化当前延迟和尝试次数为0
        time_t	next_interval, t, tmax, scheduled_check = 0; // 初始化下一个间隔、当前时间、最大时间为0，计划检查时间为0

        /* first try to parse out and calculate scheduled intervals */
        if (NULL != custom_intervals) // 如果自定义间隔不为空
            scheduled_check = scheduler_get_nextcheck(custom_intervals->scheduling, now); // 计算当前计划的检查时间

        /* Try to find the nearest 'nextcheck' value with condition */
        /* 'now' < 'nextcheck' < 'now' + SEC_PER_YEAR. If it is not */
        /* possible to check the item within a year, fail. */

        t = now;
        tmax = now + SEC_PER_YEAR;

        while (t < tmax)
        {
            /* calculate 'nextcheck' value for the current interval */
            if (NULL != custom_intervals) // 如果自定义间隔不为空
                current_delay = get_current_delay(simple_interval, custom_intervals->flexible, t); // 计算当前间隔的当前延迟
            else
                current_delay = simple_interval; // 否则，当前延迟为简单间隔

            if (0 != current_delay) // 如果当前延迟不为0
            {
                nextcheck = current_delay * (int)(t / (time_t)current_delay) +
                            (int)(seed % (zbx_uint64_t)current_delay); // 计算下一个检查时间

                if (0 == attempt)
                {
                    while (nextcheck <= t)
                        nextcheck += current_delay; // 下一个检查时间在当前时间之前，不断向后推移
                }
                else
                {
                    while (nextcheck < t)
                        nextcheck += current_delay; // 下一个检查时间在当前时间之后，不断向前推移
                }
            }
            else
                nextcheck = ZBX_JAN_2038; // 否则，下一个检查时间为2038年1月1日

            if (NULL == custom_intervals) // 如果自定义间隔为空
                break;

            /* 'nextcheck' < end of the current interval ? */
            /* the end of the current interval is the beginning of the next interval - 1 */
            if (FAIL != get_next_delay_interval(custom_intervals->flexible, t, &next_interval) &&
                    nextcheck >= next_interval) // 如果下一个间隔不为空，且下一个检查时间大于等于下一个间隔的开始时间
            {
                /* 'nextcheck' is beyond the current interval */
                t = next_interval; // 更新当前时间为下一个间隔的开始时间
                attempt++; // 尝试次数加1
            }
            else
                break; /* nextcheck is within the current interval */
        }

        if (0 != scheduled_check && scheduled_check < nextcheck) // 如果计划检查时间小于下一个检查时间
            nextcheck = (int)scheduled_check; // 更新下一个检查时间为计划检查时间
    }

    return nextcheck; // 返回计算出的下一个检查时间
}

/******************************************************************************
 *                                                                            *
 * Function: calculate_item_nextcheck_unreachable                             *
 *                                                                            *
 * Purpose: calculate nextcheck timestamp for item on unreachable host        *
 *                                                                            *
 * Parameters: simple_interval  - [IN] default delay value, can be overridden *
 *             custom_intervals - [IN] preprocessed custom intervals          *
 *             disable_until    - [IN] timestamp for next check               *
 *                                                                            *
 * Return value: nextcheck value                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算出一个下一个检查时间点，该时间点满足一定条件（如在一年的时间内），并根据已有的调度检查时间点和自定义间隔进行调整。输出结果为计算得到的下一个检查时间点。
 ******************************************************************************/
// 定义一个函数，计算下一个检查时间点，参数为简单间隔、自定义间隔数组和禁用时间
int calculate_item_nextcheck_unreachable(int simple_interval, const zbx_custom_interval_t *custom_intervals, time_t disable_until)
{
	int	nextcheck = 0; // 定义一个变量nextcheck，用于存储下一个检查时间点
	time_t	next_interval, tmax, scheduled_check = 0; // 定义时间变量，分别为下一个间隔、最大时间点和调度检查时间

	/* 首先尝试解析并计算计划中的间隔 */
	if (NULL != custom_intervals) // 如果自定义间隔数组不为空
		scheduled_check = scheduler_get_nextcheck(custom_intervals->scheduling, disable_until); // 调用函数获取下一个检查时间点

	/* 尝试找到满足条件的最近'nextcheck'值 */
	/* 'now' < 'nextcheck' < 'now' + SEC_PER_YEAR。如果在一年的时间内无法检查项目，则失败 */

	nextcheck = disable_until; // 初始化nextcheck为禁用时间
	tmax = disable_until + SEC_PER_YEAR; // 计算最大时间点，即禁用时间加一年

	if (NULL != custom_intervals) // 如果自定义间隔数组不为空
	{
		while (nextcheck < tmax) // 循环查找下一个检查时间点
		{
			if (0 != get_current_delay(simple_interval, custom_intervals->flexible, nextcheck)) // 如果计算当前延迟失败
				break; // 跳出循环

			/* 查找灵活间隔的变化 */
			if (FAIL == get_next_delay_interval(custom_intervals->flexible, nextcheck, &next_interval)) // 如果获取下一个延迟间隔失败
			{
				nextcheck = ZBX_JAN_2038; // 设置下一个检查时间为2038年1月1日，视为无法达到的检查时间点
				break;
			}
			nextcheck = next_interval; // 更新下一个检查时间点
		}
	}

	if (0 != scheduled_check && scheduled_check < nextcheck) // 如果调度检查时间点不为0且小于下一个检查时间点
		nextcheck = (int)scheduled_check; // 更新下一个检查时间点

	return nextcheck; // 返回计算得到的下一个检查时间点
}

/******************************************************************************
 *                                                                            *
 * Function: calculate_proxy_nextcheck                                        *
 *                                                                            *
 * Purpose: calculate nextcheck timestamp for passive proxy                   *
 *                                                                            *
 * Parameters: hostid - [IN] host identificator from database                 *
 *             delay  - [IN] default delay value, can be overridden           *
 *             now    - [IN] current timestamp                                *
 *                                                                            *
 * Return value: nextcheck value                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算出一个延迟时间，使得在给定主机ID的情况下，这个延迟时间与当前时间之间的差值最小。输出结果为一个时间类型值（time_t）。在这个函数中，我们先计算下一个检查时间，然后判断它是否小于等于当前时间，如果是，则不断增加延迟，直到下一个检查时间大于当前时间。最后返回计算出的下一个检查时间。
 ******************************************************************************/
// 定义一个函数calculate_proxy_nextcheck，接收三个参数：
// zbx_uint64_t hostid：主机ID
// unsigned int delay：延迟时间
// time_t now：当前时间
time_t	calculate_proxy_nextcheck(zbx_uint64_t hostid, unsigned int delay, time_t now)
{
	// 定义一个时间类型变量nextcheck，用于存储计算结果
	time_t	nextcheck;

	// 计算下一个检查时间：
	nextcheck = delay * (now / delay) + (unsigned int)(hostid % delay);
	// 这里采用了求余运算，目的是使下一个检查时间与主机ID相关，从而实现动态调整

	// 判断下一个检查时间是否小于等于当前时间，如果是，则不断增加延迟，直到大于当前时间
	while (nextcheck <= now)
		nextcheck += delay;

	// 返回计算出的下一个检查时间
	return nextcheck;
}


/******************************************************************************
 *                                                                            *
 * Function: is_ip4                                                           *
 *                                                                            *
 * Purpose: is string IPv4 address                                            *
 *                                                                            *
 * Parameters: ip - string                                                    *
 *                                                                            *
 * Return value: SUCCEED - is IPv4 address                                    *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个输入的字符串是否符合IPv4地址格式。如果符合，返回SUCCEED，否则返回FAIL。在判断过程中，代码逐个字符分析ip字符串，统计数字和点的位置，以确定是否符合IPv4地址格式。同时，记录日志以便于调试和查看执行过程。
 ******************************************************************************/
int is_ip4(const char *ip)
{
	// 定义一个字符串常量，表示函数名
	const char *__function_name = "is_ip4";
	// 定义一个指向ip字符串的指针
	const char *p = ip;
	// 定义一些变量，用于统计数字和点的位置
	int		digits = 0, dots = 0, res = FAIL, octet = 0;

	// 记录日志，表示函数开始执行，输入的ip地址
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() ip:'%s'", __function_name, ip);

	// 遍历ip字符串中的每个字符
	while ('\0' != *p)
	{
		// 如果当前字符是数字，则进行以下操作：
		if (0 != isdigit(*p))
		{
			// 将当前数字乘以10，并加上前一个数字的值，存储到octet变量中
			octet = octet * 10 + (*p - '0');
			// 增加数字计数器
			digits++;
		}
		// 如果当前字符是点，则进行以下操作：
		else if ('.' == *p)
		{
			// 检查数字计数器和octet的值是否符合IPv4地址的要求
			if (0 == digits || 3 < digits || 255 < octet)
				break;

			// 重置数字计数器和octet的值
			digits = 0;
			octet = 0;
			// 增加点计数器
			dots++;
		}
		// 如果当前字符不是数字或点，则重置数字计数器
		else
		{
			digits = 0;
			break;
		}

		// 移动指针，指向下一个字符
		p++;
	}
	// 判断是否符合IPv4地址要求，如果符合，更新res值为SUCCEED
	if (3 == dots && 1 <= digits && 3 >= digits && 255 >= octet)
		res = SUCCEED;

	// 记录日志，表示函数执行结束，返回结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(res));

	// 返回res值，表示判断结果
	return res;
}


/******************************************************************************
 *                                                                            *
 * Function: is_ip6                                                           *
 *                                                                            *
 * Purpose: is string IPv6 address                                            *
 *                                                                            *
 * Parameters: ip - string                                                    *
 *                                                                            *
 * Return value: SUCCEED - is IPv6 address                                    *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个字符串是否为IPv6地址。函数`is_ip6`接受一个字符串作为输入，然后根据IPv6地址的规则进行判断。如果符合IPv6地址的特征，则返回SUCCEED，否则返回FAIL。在判断过程中，使用了多个变量（如xdigits、colons、dbl_colons等）来记录ip地址的特征，以便进行详细的判断。此外，还使用了调试日志记录函数的执行过程。
 ******************************************************************************/
/* 定义一个函数，判断输入的字符串是否为IPv6地址
 * 参数：ip，输入的字符串
 * 返回值：判断结果，FAIL表示不是IPv6地址，SUCCEED表示是IPv6地址
 */
int is_ip6(const char *ip)
{
	/* 定义一些变量，用于记录ip地址的特征 */
	const char *__function_name = "is_ip6";
	const char *p = ip, *last_colon;
	int		xdigits = 0, only_xdigits = 0, colons = 0, dbl_colons = 0, res;

	/* 记录调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() ip:'%s'", __function_name, ip);

	/* 遍历ip地址字符串 */
	while ('\0' != *p)
	{
		/* 如果是十六进制数字，则累加xdigits计数器，并设置only_xdigits为1 */
		if (0 != isxdigit(*p))
		{
			xdigits++;
			only_xdigits = 1;
		}
		/* 如果是':'，则进行以下判断：
		 * 1. 如果xdigits为0且colons大于0，表示连续的零段被替换为双冒号
		 * 2. 如果xdigits大于4或dbl_colons大于1，则退出循环
		 * 3. 否则，重置xdigits和colons计数器 */
		else if (':' == *p)
		{
			if (0 == xdigits && 0 < colons)
			{
				/* consecutive sections of zeros are replaced with a double colon */
				only_xdigits = 1;
				dbl_colons++;
			}

			if (4 < xdigits || 1 < dbl_colons)
				break;

			xdigits = 0;
			colons++;
		}
		/* 如果是其他字符，则设置only_xdigits为0并跳出循环 */
		else
		{
			only_xdigits = 0;
			break;
		}

		p++;
	}

	/* 判断结果：
	 * 1. 如果colons大于2或小于7，或者dbl_colons大于1，或者xdigits大于4，则返回FAIL
	 * 2. 如果only_xdigits为1，则返回SUCCEED
	 * 3. 如果colons小于7且last_colon指向的是ipv4映射地址，则调用is_ip4()函数判断
	 * 4. 否则，返回FAIL
 */
	if (2 > colons || 7 < colons || 1 < dbl_colons || 4 < xdigits)
		res = FAIL;
	else if (1 == only_xdigits)
		res = SUCCEED;
	else if (7 > colons && (last_colon = strrchr(ip, ':')) < p)
		res = is_ip4(last_colon + 1);	/* past last column is ipv4 mapped address */
	else
		res = FAIL;

	/* 记录调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(res));

	/* 返回判断结果 */
	return res;
}


/******************************************************************************
 *                                                                            *
 * Function: is_supported_ip                                                  *
 *                                                                            *
 * Purpose: is string IP address of supported version                         *
 *                                                                            *
 * Parameters: ip - string                                                    *
 *                                                                            *
 * Return value: SUCCEED - is IP address                                      *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检测给定的IP地址是否被支持。它首先使用`is_ip4()`函数检测IP地址是否为IPv4类型，如果检测结果为IPv4，则返回SUCCEED，表示支持。接着，使用`is_ip6()`函数检测IP地址是否为IPv6类型，如果检测结果为IPv6，则返回SUCCEED，表示支持。如果在上述检测中都没有找到支持的IP类型，则返回FAIL，表示不支持。
 ******************************************************************************/
// 定义一个函数，用于判断给定的IP地址是否被支持
int is_supported_ip(const char *ip)
{

    // 使用is_ip4()函数检测IP地址是否为IPv4类型
    if (SUCCEED == is_ip4(ip))
    {
        // 如果检测结果为IPv4，则返回SUCCEED，表示支持
        return SUCCEED;
    }

    // 启用IPv6检测，如果定义了HAVE_IPV6宏，则继续执行以下代码
    #ifdef HAVE_IPV6
    // 使用is_ip6()函数检测IP地址是否为IPv6类型
    if (SUCCEED == is_ip6(ip))
    {
        // 如果检测结果为IPv6，则返回SUCCEED，表示支持
        return SUCCEED;
    }
    #endif

    // 如果在上述检测中都没有找到支持的IP类型，则返回FAIL，表示不支持
    return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: is_ip                                                            *
 *                                                                            *
 * Purpose: is string IP address                                              *
 *                                                                            *
 * Parameters: ip - string                                                    *
 *                                                                            *
 * Return value: SUCCEED - is IP address                                      *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 *int\tis_ip(const char *ip)
 *{
 *\treturn SUCCEED == is_ip4(ip) ? SUCCEED : is_ip6(ip);
 *}
 *
 */
/**
 * * 函数is_ip的主要目的是判断一个字符串表示的IP地址的格式，返回0表示IP地址格式正确，非0表示格式错误。
 * * 该函数接收一个const char类型的指针作为参数，表示待判断的IP地址字符串。
 * * 函数内部首先调用is_ip4函数判断IP地址是否为IPv4格式，如果为IPv4格式且格式正确，则直接返回0。
 * * 如果IP地址不是IPv4格式，则调用is_ip6函数判断IP地址是否为IPv6格式，如果为IPv6格式且格式正确，则返回0，否则返回非0值。
 * *
 ******************************************************************************/
int	is_ip(const char *ip)						// 定义一个函数is_ip，接收一个const char类型的指针作为参数
{
	return SUCCEED == is_ip4(ip) ? SUCCEED : is_ip6(ip);	// 调用is_ip4函数判断IP地址是否为IPv4格式，如果返回SUCCEED，则认为IP地址格式正确；否则，调用is_ip6函数判断IP地址是否为IPv6格式
}

/**
 * 函数is_ip的主要目的是判断一个字符串表示的IP地址的格式，返回0表示IP地址格式正确，非0表示格式错误。
 * 该函数接收一个const char类型的指针作为参数，表示待判断的IP地址字符串。
 * 函数内部首先调用is_ip4函数判断IP地址是否为IPv4格式，如果为IPv4格式且格式正确，则直接返回0。
 * 如果IP地址不是IPv4格式，则调用is_ip6函数判断IP地址是否为IPv6格式，如果为IPv6格式且格式正确，则返回0，否则返回非0值。
 */


/******************************************************************************
 *                                                                            *
 * Function: zbx_validate_hostname                                            *
 *                                                                            *
 * Purpose: check if string is a valid internet hostname                      *
 *                                                                            *
 * Parameters: hostname - [IN] hostname string to be checked                  *
 *                                                                            *
 * Return value: SUCCEED - could be a valid hostname,                         *
 *               FAIL - definitely not a valid hostname                       *
 * Comments:                                                                  *
 *     Validation is not strict. Restrictions not checked:                    *
 *         - individual label (component) length 1-63,                        *
 *         - hyphens ('-') allowed only as interior characters in labels,     *
 *         - underscores ('_') allowed in domain name, but not in hostname.   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是验证一个主机名（hostname）是否符合规范。函数 `zbx_validate_hostname` 接受一个字符串参数 `hostname`，然后对它进行逐个字符的检查。检查规则如下：
 *
 *1. 第一个字符必须是字母或数字。
 *2. 后续字符可以是字母、数字、减号（-）或下划线（_），但每个组件（除最后一个）后的字符只能是字母或数字。
 *3. 最后一个字符可以是点（.），但仅当它用于分隔组件时。
 *
 *如果所有字符都符合上述规则，函数返回成功（SUCCEED）；否则，返回失败（FAIL）。
 ******************************************************************************/
int	zbx_validate_hostname(const char *hostname)
{
	// 定义一个整型变量 component，用于表示当前处理的域名组件
	int		component;	/* periods ('.') are only allowed when they serve to delimit components */
	// 定义一个整型变量 len，用于存储当前处理的字符长度
	int		len = MAX_ZBX_DNSNAME_LEN;
	// 定义一个指向字符串的指针 p，用于遍历字符串
	const char	*p;

	// 检查第一个字符是否为字母或数字
	/* the first character must be an alphanumeric character */
	if (0 == isalnum(*hostname))
		return FAIL;

	// 检查 hostname 字符串，直到遇到第一个 'len' 字符，第一个字符已经成功检查
	for (p = hostname + 1, component = 1; '\0' != *p; p++)
	{
		// 如果剩余字符长度为 0，说明域名过长，返回失败
		if (0 == --len)				/* hostname too long */
			return FAIL;

		// 检查当前字符是否为允许的字符
		/* check for allowed characters */
		if (0 != isalnum(*p) || '-' == *p || '_' == *p)
			component = 1;
		// 如果当前字符为 '.'，且当前组件数为 1，则允许
		else if ('.' == *p && 1 == component)
			component = 0;
		// 否则，返回失败
		else
			return FAIL;
	}

	// 如果所有字符都符合要求，返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: ip_in_list                                                       *
 *                                                                            *
 * Purpose: check if ip matches range of ip addresses                         *
 *                                                                            *
 * Parameters: list - [IN] comma-separated list of ip ranges                  *
 *                         192.168.0.1-64,192.168.0.128,10.10.0.0/24,12fc::21 *
 *             ip   - [IN] ip address                                         *
 *                                                                            *
 * Return value: FAIL - out of range, SUCCEED - within the range              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个IP地址是否在一个IP地址列表中。函数`ip_in_list`接收两个参数，一个是IP地址列表（以逗号分隔），另一个是要判断的IP地址。函数首先解析IP地址列表和要判断的IP地址，然后遍历列表中的每个地址，判断要判断的IP地址是否在列表中。如果找到匹配的IP地址，函数返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个函数，判断一个IP地址是否在一个IP地址列表中
int ip_in_list(const char *list, const char *ip)
{
    // 定义一些变量，用于存储IP地址和列表中的地址
    const char *__function_name = "ip_in_list"; // 函数名
    int ipaddress[8]; // 存储IP地址的数组
    zbx_iprange_t iprange; // 存储IP地址范围的结构体
    char *address = NULL; // 用于存储列表中的地址
    size_t address_alloc = 0, address_offset; // 分配给address的字节数和地址偏移量
    const char *ptr; // 用于遍历列表中的地址
    int ret = FAIL; // 函数返回值

    // 记录日志，输入的列表和IP地址
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() list:'%s' ip:'%s'", __function_name, list, ip);

    // 解析IP地址，存储在iprange结构体中
    if (SUCCEED != iprange_parse(&iprange, ip))
        goto out; // 如果解析失败，跳转到out标签

    // 判断iprange的类型，如果不是IPv4，直接返回失败
#ifndef HAVE_IPV6
if (ZBX_IPRANGE_V6 == iprange.type)
    goto out;
#endif
	iprange_first(&iprange, ipaddress);
    // 遍历列表中的地址
    for (ptr = list; '\0' != *ptr; list = ptr + 1)
    {
        // 查找列表中的下一个地址分隔符（',')
        if (NULL == (ptr = strchr(list, ',')))
            ptr = list + strlen(list);

        // 复制列表中的地址到address，并分配内存
		address_offset = 0;
        zbx_strncpy_alloc(&address, &address_alloc, &address_offset, list, ptr - list);

        // 解析address，存储在iprange结构体中
        if (SUCCEED != iprange_parse(&iprange, address))
            continue; // 如果解析失败，继续循环

        // 判断address是否在iprange中，如果在，则返回成功
        #ifndef HAVE_IPV6
        if (ZBX_IPRANGE_V6 == iprange.type)
            continue;
        #endif
        if (SUCCEED == iprange_validate(&iprange, ipaddress))
        {
            ret = SUCCEED; // 找到匹配的IP地址，返回成功
            break; // 跳出循环
        }
    }

    // 释放address内存
    zbx_free(address);

out:
    // 记录日志，输出函数返回值
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回函数结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: int_in_list                                                      *
 *                                                                            *
 * Purpose: check if integer matches a list of integers                       *
 *                                                                            *
 * Parameters: list  - integers [i1-i2,i3,i4,i5-i6] (10-25,45,67-699)         *
 *             value - integer to check                                       *
 *                                                                            *
 * Return value: FAIL - out of period, SUCCEED - within the period            *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查一个整数value是否在一个以逗号分隔的整数列表中。如果找到匹配项，返回成功状态（SUCCEED），否则返回失败状态（FAIL）。日志用于记录函数的执行过程和结果。
 ******************************************************************************/
// 定义一个函数int_in_list，接收两个参数：一个字符指针list和一个整数value
int int_in_list(char *list, int value)
{
	// 定义一些变量，用于存储列表中的内容和工作状态
	const char *__function_name = "int_in_list"; // 函数名
	char *start = NULL, *end = NULL, c = '\0'; // 指向列表的指针，和一个通用字符变量c
	int i1, i2, ret = FAIL; // 定义一个整数变量ret，初始值为失败状态

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() list:'%s' value:%d", __function_name, list, value);

	// 遍历列表，直到遇到空字符'\0'
	for (start = list; '\0' != *start;)
	{
		// 如果找到列表中的逗号分隔符，将其后的内容赋值给end
		if (NULL != (end = strchr(start, ',')))
		{
			c = *end; // 将分隔符保存到c
			*end = '\0'; // 将分隔符替换为空字符，方便后续处理
		}

		// 判断列表中的内容是否符合预期格式，如果是，则继续处理
		if (2 == sscanf(start, "%d-%d", &i1, &i2))
		{
			// 如果value在i1和i2之间，表示找到匹配项，更新ret为成功状态
			if (i1 <= value && value <= i2)
			{
				ret = SUCCEED;
				break;
			}
		}
		else
		{
			// 如果value等于列表中的某个整数，表示找到匹配项，更新ret为成功状态
			if (value == atoi(start))
			{
				ret = SUCCEED;
				break;
			}
		}

		// 如果找到了分隔符，将其还原，并更新start指针
		if (NULL != end)
		{
			*end = c;
			start = end + 1;
		}
		else
			break; // 如果没有找到分隔符，说明已经遍历完整个列表，跳出循环
	}

	// 恢复分隔符，以便后续使用
	if (NULL != end)
		*end = c;

	// 记录日志，表示函数执行结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回ret，表示函数执行结果
	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是比较两个双精度浮点数a和b是否相近。误差范围由常量ZBX_DOUBLE_EPSILON控制，如果a和b的差值小于等于这个误差范围，则认为两者相近，返回SUCCEED；否则，认为两者不相近，返回FAIL。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_double_compare，接收两个double类型的参数a和b
int zbx_double_compare(double a, double b)
{
    // 定义一个常量ZBX_DOUBLE_EPSILON，表示双精度浮点数的误差范围
    // 这里的注释写法不太规范，建议改为：
    // const double ZBX_DOUBLE_EPSILON = 1e-10; // 定义一个常量ZBX_DOUBLE_EPSILON，表示双精度浮点数的误差范围

    // 计算a和b的差值，并取其绝对值
    double diff = fabs(a - b);

    // 判断差值是否小于等于误差范围ZBX_DOUBLE_EPSILON，如果是，则返回SUCCEED，表示两者相近
    // 否则，返回FAIL，表示两者不相近
    return diff <= ZBX_DOUBLE_EPSILON ? SUCCEED : FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: is_double_suffix                                                 *
 *                                                                            *
 * Purpose: check if the string is double                                     *
 *                                                                            *
 * Parameters: str   - string to check                                        *
 *             flags - extra options including:                               *
 *                       ZBX_FLAG_DOUBLE_SUFFIX - allow suffixes              *
 *                                                                            *
 * Return value:  SUCCEED - the string is double                              *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: the function automatically processes suffixes K, M, G, T and     *
 *           s, m, h, d, w                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断给定字符串是否具有双后缀。首先，检查字符串是否以负号开头，如果是，则跳过负号。然后，尝试解析字符串为数字，如果解析失败，则返回失败。接下来，检查字符串是否为空，并且当前字符是否为合法的后缀字符（根据zbx_number_parse的返回值和双后缀标志）。如果满足条件，则跳过该字符。最后，判断字符串结尾是否为'\\0'，如果是，则表示成功，否则表示失败。
 ******************************************************************************/
int	is_double_suffix(const char *str, unsigned char flags)
{
	/* 定义一个函数，用于判断给定字符串是否具有双后缀 */

	int	len;																										/* 定义一个整型变量 len，用于存储字符串的长度 */

	if ('-' == *str)	/* 检查字符串是否以负号开头，如果是，则跳过负号 */
		str++;

	if (FAIL == zbx_number_parse(str, &len))																	/* 如果zbx_number_parse函数返回失败，说明字符串无法解析为数字，返回失败 */
		return FAIL;

	if ('\0' != *(str += len) && 0 != (flags & ZBX_FLAG_DOUBLE_SUFFIX) && NULL != strchr(ZBX_UNIT_SYMBOLS, *str))	/* 如果字符串不为空，且启用了双后缀标志，且当前字符为合法的后缀字符，则跳过该字符 */
		str++;

	return '\0' == *str ? SUCCEED : FAIL;																	/* 如果字符串结尾为'\0'，则表示成功，否则表示失败 */
}


/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的字符串是否符合双精度浮点数的语法规则，并返回验证结果。如果符合规则，返回SUCCEED；否则，返回FAIL。
 ******************************************************************************/
static int	is_double_valid_syntax(const char *str)
{
	int	len;

	/* 验证字符串是否符合双精度浮点数的语法规则，规则如下：
	 * 1. 有效语法是一个可选的十进制数，其后可选跟一个十进制指数。
	 * 2. 开头和结尾的空格、NAN、INF以及十六进制表示法均不允许。
	 */

	if ('-' == *str || '+' == *str)		/* 检查开头符号 */
		str++;

	if (FAIL == zbx_number_parse(str, &len))
		return FAIL;		/* 解析字符串为数字，失败则返回FAIL */

	str += len;		/* 跳过已解析的数字部分 */

	if ('e' == *str || 'E' == *str)		/* 检查指数部分 */
	{
		str++;		/* 跳过字母e或E */

		if ('-' == *str || '+' == *str)		/* 检查指数符号 */
			str++;

		if (0 == isdigit(*str))		/* 检查指数 */
			return FAIL;

		while (0 != isdigit(*str))
			str++;		/* 跳过指数中的数字 */
	}

	return '\0' == *str ? SUCCEED : FAIL;	/* 检查字符串结尾是否为空，若为空则返回SUCCEED，否则返回FAIL */
}


/******************************************************************************
 *                                                                            *
 * Function: is_double                                                        *
 *                                                                            *
 * Purpose: validate and optionally convert a string to a number of type      *
 *         'double'                                                           *
 *                                                                            *
 * Parameters: str   - [IN] string to check                                   *
 *             value - [OUT] output buffer where to write the converted value *
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个C字符串是否可以转换为double类型，并将转换结果存储在可选的value指针指向的内存位置。如果字符串不符合double类型的语法规则或者转换结果超出有效范围，函数返回FAIL；否则，返回SUCCEED。在转换过程中，还对字符串进行了更严格的语法检查，以保证其符合double类型的规范。
 ******************************************************************************/
/* 定义一个函数，判断字符串是否可以转换为double类型，并可选地保存转换结果到value指针指向的内存位置 */
int	is_double(const char *str, double *value)
{
	/* 定义一个临时double变量tmp，用于存储转换后的结果 */
	double	tmp;
	/* 定义一个指针endptr，用于存储strtod()函数的结束位置 */
	char	*endptr;

	/* 检查字符串是否符合double类型的语法规则 */
	if (SUCCEED != is_double_valid_syntax(str))
		return FAIL;

	/* 清零errno，以便后续判断错误情况 */
	errno = 0;
	/* 使用strtod()函数将字符串转换为double类型，并将结果存储在tmp变量中，结束位置存储在endptr指针中 */
	tmp = strtod(str, &endptr);

	/* 检查转换后的结束位置是否为'\0'，以及tmp是否在有效范围内 */
	if ('\0' != *endptr || HUGE_VAL == tmp || -HUGE_VAL == tmp || EDOM == errno)
		return FAIL;

	/* 如果value不为空，将转换后的double值赋给value */
	if (NULL != value)
		*value = tmp;

	/* 返回转换成功标识SUCCEED */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: is_time_suffix                                                   *
 *                                                                            *
 * Purpose: check if the string is a non-negative integer with or without     *
 *          supported time suffix                                             *
 *                                                                            *
 * Parameters: str    - [IN] string to check                                  *
 *             value  - [OUT] a pointer to converted value (optional)         *
 *             length - [IN] number of characters to validate, pass           *
 *                      ZBX_LENGTH_UNLIMITED to validate full string          *
 *                                                                            *
 * Return value: SUCCEED - the string is valid and within reasonable limits   *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Author: Aleksandrs Saveljevs, Vladimir Levijev                             *
 *                                                                            *
 * Comments: the function automatically processes suffixes s, m, h, d, w      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个字符串是否符合时间后缀的要求，例如：\"123s\"、\"123m\"、\"123h\"等。如果符合要求，计算出时间值并存储在传入的指针变量value中。在计算过程中，还对字符串长度和时间单位进行了检查，以防止溢出和不符合规范的长度。最后，返回成功或失败的结果。
 ******************************************************************************/
int is_time_suffix(const char *str, int *value, int length)
{
	/* 定义一个常量，表示最小的可接受整数值，这里是INT_MAX的2^31-1 */
	const int max = 0x7fffffff;

	/* 获取字符串长度 */
	int len = length;

	/* 定义一个临时变量，用于存储数值 */
	int value_tmp = 0;

	/* 定义一个变量，用于存储当前字符的十进制值 */
	int c;

	/* 定义一个因子，用于后续时间单位的转换 */
	int factor = 1;

	/* 判断字符串是否为空，或者长度小于0，或者第一个字符不是数字，返回失败 */
	if ('\0' == *str || 0 >= len || 0 == isdigit(*str))
		return FAIL;

	/* 循环读取字符串中的数字，直到遇到非数字字符或到达字符串末尾 */
	while ('\0' != *str && 0 < len && 0 != isdigit(*str))
	{
		/* 将字符转换为十进制数值 */
		c = (int)(unsigned char)(*str - '0');

		/* 判断当前数值是否导致溢出（大于最大可接受值） */
		if ((max - c) / 10 < value_tmp)
			return FAIL;

		/* 更新数值 */
		value_tmp = value_tmp * 10 + c;

		/* 移动字符指针和长度计数器 */
		str++;
		len--;
	}

	/* 判断字符串是否还包含时间单位，如果有，进行转换 */
	if ('\0' != *str && 0 < len)
	{
		/* 切换到时间单位因子 */
		switch (*str)
		{
			case 's':
				break;
			case 'm':
				factor = SEC_PER_MIN;
				break;
			case 'h':
				factor = SEC_PER_HOUR;
				break;
			case 'd':
				factor = SEC_PER_DAY;
				break;
			case 'w':
				factor = SEC_PER_WEEK;
				break;
			default:
				return FAIL;
		}

		/* 移动字符指针和长度计数器 */
		str++;
		len--;
	}

	/* 判断字符串长度是否符合要求，否则返回失败 */
	if ((ZBX_LENGTH_UNLIMITED == length && '\0' != *str) || (ZBX_LENGTH_UNLIMITED != length && 0 != len))
		return FAIL;

	/* 判断是否溢出（大于最大可接受值） */
	if (max / factor < value_tmp)
		return FAIL;

	/* 如果传入的value不为空，则保存计算结果 */
	if (NULL != value)
		*value = value_tmp * factor;

	/* 返回成功 */
	return SUCCEED;
}


#ifdef _WINDOWS
/******************************************************************************
 * *
 *整个代码块的主要目的是检查一个宽字符串（wide_string）是否包含任何数字。如果找到数字，函数返回成功（SUCCEED）；如果没有找到数字，函数返回失败（FAIL）。在遍历过程中，如果遇到空字符（'\\0'），则结束遍历。
 ******************************************************************************/
// 定义一个函数 _wis_uint，接收一个指向宽字符串的指针 wide_string 作为参数
int _wis_uint(const wchar_t *wide_string)
{
	// 保存宽字符串的指针，方便后续遍历
	const wchar_t	*wide_char = wide_string;

	// 判断宽字符串是否为空，如果为空则返回失败
	if (L'\0' == *wide_char)
		return FAIL;

	// 使用一个 while 循环遍历宽字符串中的每个字符
	while (L'\0' != *wide_char)
	{
		// 判断当前字符是否为数字，如果是数字则跳过下一个字符，继续循环
		if (0 != iswdigit(*wide_char))
		{
			wide_char++;
			continue;
		}

		// 如果不满足数字条件，则返回失败
		return FAIL;
	}

	// 遍历完整个宽字符串，如果没有找到任何数字，则返回成功
	return SUCCEED;
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: is_int_prefix                                                    *
 *                                                                            *
 * Purpose: check if the beginning of string is a signed integer              *
 *                                                                            *
 * Parameters: str - string to check                                          *
 *                                                                            *
 * Return value:  SUCCEED - the beginning of string is a signed integer       *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示，这段代码的主要目的是检查一个字符串是否是一个整数的前缀。如果字符串是以数字开头的，返回成功（SUCCEED），否则返回失败（FAIL）。在函数内部，首先遍历字符串，去除左边的空格。然后检查接下来的字符是否为减号或加号，如果是，则跳过一个字符。最后检查接下来的字符是否为数字，如果不是数字，返回失败，如果是数字，返回成功。
 ******************************************************************************/
/*
 * 这个函数的主要目的是检查一个字符串是否是一个整数的前缀。
 * 输入参数：str - 需要检查的字符串。
 * 返回值：如果字符串是整数前缀，返回SUCCEED，否则返回FAIL。
 */
int is_int_prefix(const char *str)
{
	// 定义一个变量i，初始值为0，用于遍历字符串
	size_t	i = 0;

	// 遍历字符串，直到遇到第一个非空格字符
	while (' ' == str[i])	/* 去除左边的空格 */
		i++;

	// 如果遇到减号或加号，跳过一个字符
	if ('-' == str[i] || '+' == str[i])
		i++;

	// 检查接下来的字符是否为数字
	if (0 == isdigit(str[i]))
		// 不是数字，返回失败
		return FAIL;

	// 如果是数字，说明字符串是整数前缀，返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: is_uint_n_range                                                  *
 *                                                                            *
 * Purpose: check if the string is unsigned integer within the specified      *
 *          range and optionally store it into value parameter                *
 *                                                                            *
 * Parameters: str   - [IN] string to check                                   *
 *             n     - [IN] string length or ZBX_MAX_UINT64_LEN               *
 *             value - [OUT] a pointer to output buffer where the converted   *
 *                     value is to be written (optional, can be NULL)         *
 *             size  - [IN] size of the output buffer (optional)              *
 *             min   - [IN] the minimum acceptable value                      *
 *             max   - [IN] the maximum acceptable value                      *
 *                                                                            *
 * Return value:  SUCCEED - the string is unsigned integer                    *
 *                FAIL - the string is not a number or its value is outside   *
 *                       the specified range                                  *
 *                                                                            *
 * Author: Alexander Vladishev, Andris Zeila                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个字符串是否在指定的范围内，并将结果存储在指定的void指针指向的空间中。具体实现过程如下：
 *
 *1. 定义必要的变量，如result_uint64、max_uint64等。
 *2. 检查输入参数是否合法，如字符串、字符串长度、void指针、指针指向的空间大小等。
 *3. 遍历字符串，判断每个字符是否为数字。
 *4. 将数字字符转换为zbx_uint64_t类型，并判断当前值是否超过最大值。
 *5. 将数字字符串转换为zbx_uint64_t类型，即value_uint64。
 *6. 判断value_uint64是否在指定的范围内。
 *7. 如果指定了void指针，将value_uint64存储到指定的空间。
 *8. 返回成功或失败。
 ******************************************************************************/
// 定义一个函数，判断一个字符串是否在指定的范围内，并将结果存储在指定的void指针指向的空间中
// 参数：
//   str：字符串
//   n：字符串长度
//   value：void指针，指向存储结果的空间
//   size：void指针指向的空间大小
//   min：最小值
//   max：最大值
int is_uint_n_range(const char *str, size_t n, void *value, size_t size, zbx_uint64_t min, zbx_uint64_t max)
{
	// 定义一个zbx_uint64_t类型的变量，用于存储结果
	zbx_uint64_t value_uint64 = 0, c;
	// 定义一个zbx_uint64_t类型的变量，用于存储最大值
	const zbx_uint64_t max_uint64 = ~(zbx_uint64_t)__UINT64_C(0);

	// 检查输入参数是否合法
	if ('\0' == *str || 0 == n || sizeof(zbx_uint64_t) < size || (0 == size && NULL != value))
		return FAIL;

	// 遍历字符串，判断每个字符是否为数字
	while ('\0' != *str && 0 < n--)
	{
		// 判断当前字符是否为数字
		if (0 == isdigit(*str))
			return FAIL;	/* not a digit */

		// 将字符转换为zbx_uint64_t类型
		c = (zbx_uint64_t)(unsigned char)(*str - '0');

		// 判断当前值是否超过最大值
		if ((max_uint64 - c) / 10 < value_uint64)
			return FAIL;	/* maximum value exceeded */

		// 将当前值加到value_uint64中
		value_uint64 = value_uint64 * 10 + c;

		// 移动字符串指针
		str++;
	}

	// 判断value_uint64是否在指定的范围内
	if (min > value_uint64 || value_uint64 > max)
		return FAIL;

	// 如果指定了value，将value_uint64存储到void指针指向的空间
	if (NULL != value)
	{
		// 根据系统架构设置存储偏移量
		unsigned short value_offset = (unsigned short)((sizeof(zbx_uint64_t) - size) << 8);

		// 将value_uint64存储到void指针指向的空间
		memcpy(value, (unsigned char *)&value_uint64 + *((unsigned char *)&value_offset), size);
	}

	// 返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: is_hex_n_range                                                   *
 *                                                                            *
 * Purpose: check if the string is unsigned hexadecimal integer within the    *
 *          specified range and optionally store it into value parameter      *
 *                                                                            *
 * Parameters: str   - [IN] string to check                                   *
 *             n     - [IN] string length                                     *
 *             value - [OUT] a pointer to output buffer where the converted   *
 *                     value is to be written (optional, can be NULL)         *
 *             size  - [IN] size of the output buffer (optional)              *
 *             min   - [IN] the minimum acceptable value                      *
 *             max   - [IN] the maximum acceptable value                      *
 *                                                                            *
 * Return value:  SUCCEED - the string is unsigned integer                    *
 *                FAIL - the string is not a hexadecimal number or its value  *
 *                       is outside the specified range                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个十六进制字符串，并检查其是否在指定的范围内。如果解析成功，将把解析后的值存储到指定的缓冲区。在此过程中，会对输入进行详细的检查，确保输入的合法性。如果输入不合法，函数返回失败；如果解析成功，函数返回0。
 ******************************************************************************/
// 定义一个函数，判断给定的字符串是否是一个合法的十六进制数，并且在这个范围内
// 输入：字符串 str，字符串长度 n，值 value，值的大小 size，最小值 min，最大值 max
// 输出：若输入合法，返回 0，否则返回 -1
int is_hex_n_range(const char *str, size_t n, void *value, size_t size, zbx_uint64_t min, zbx_uint64_t max)
{
	// 定义一个zbx_uint64_t类型的变量 value_uint64，用于存储解析后的十六进制数值
	zbx_uint64_t		value_uint64 = 0, c;
	// 定义一个zbx_uint64_t类型的变量 max_uint64，用于存储最大值
	const zbx_uint64_t	max_uint64 = ~(zbx_uint64_t)__UINT64_C(0);
	// 定义一个整型变量 len，用于存储当前解析的位数
	int			len = 0;

	// 检查输入是否合法
	if ('\0' == *str || 0 == n || sizeof(zbx_uint64_t) < size || (0 == size && NULL != value))
		return FAIL;

	// 循环解析字符串
	while ('\0' != *str && 0 < n--)
	{
		// 判断当前字符是否为十六进制字符
		if ('0' <= *str && *str <= '9')
			c = *str - '0';
		else if ('a' <= *str && *str <= 'f')
			c = 10 + (*str - 'a');
		else if ('A' <= *str && *str <= 'F')
			c = 10 + (*str - 'A');
		else
			return FAIL;	/* not a hexadecimal digit */

		// 检查当前位数是否超过最大值的范围
		if (16 < ++len && (max_uint64 >> 4) < value_uint64)
			return FAIL;	/* maximum value exceeded */

		// 更新 value_uint64的值
		value_uint64 = (value_uint64 << 4) + c;

		// 移动字符串指针
		str++;
	}

	// 检查值是否在最小值和最大值范围内
	if (min > value_uint64 || value_uint64 > max)
		return FAIL;

	// 如果指定了输出值，则将值存储到指定的缓冲区
	if (NULL != value)
	{
		// 根据系统架构设置值存储的起始位置
		unsigned short	value_offset = (unsigned short)((sizeof(zbx_uint64_t) - size) << 8);

		// 将 value_uint64 的值复制到缓冲区
		memcpy(value, (unsigned char *)&value_uint64 + *((unsigned char *)&value_offset), size);
	}

	// 解析成功，返回 0
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: is_boolean                                                       *
 *                                                                            *
 * Purpose: check if the string is boolean                                    *
 *                                                                            *
 * Parameters: str - string to check                                          *
 *                                                                            *
 * Return value:  SUCCEED - the string is boolean                             *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断输入的字符串是否为布尔值（true或false），并将布尔值存储到指针指向的内存空间中。为实现这一目的，代码首先尝试将输入字符串转换为双精度浮点数，如果转换成功，则将布尔值存储到指针指向的内存空间中。如果转换失败，则将输入字符串转换为小写，并判断其在指定的字符串列表中是否存在，根据存在的情况更新布尔值。最后，返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，判断输入的字符串是否为布尔值，如果是，将布尔值存储到指针指向的内存空间中
int is_boolean(const char *str, zbx_uint64_t *value)
{
	// 定义一个临时变量，用于存储双精度浮点数
	double dbl_tmp;
	// 定义一个返回值，用于存储函数执行结果
	int res;

	// 判断输入字符串是否为双精度浮点数
	if (SUCCEED == (res = is_double(str, &dbl_tmp)))
	{
		// 如果输入字符串为双精度浮点数，则将布尔值存储到指针指向的内存空间中
		*value = (0 != dbl_tmp);
	}
	else
	{
		// 否则，将输入字符串转换为小写，并进行字符串处理
		char tmp[16];

		// 将输入字符串复制到临时数组中
		strscpy(tmp, str);
		// 将临时数组中的所有字符转换为小写
		zbx_strlower(tmp);

		// 判断临时数组是否在指定的字符串列表中，如果是，则更新布尔值
		if (SUCCEED == (res = str_in_list("true,t,yes,y,on,up,running,enabled,available,ok,master", tmp, ',')))
		{
			// 如果临时数组在指定的字符串列表中，则将布尔值设置为1
			*value = 1;
		}
		else if (SUCCEED == (res = str_in_list("false,f,no,n,off,down,unused,disabled,unavailable,err,slave",
				tmp, ',')))
		{
			// 如果临时数组不在指定的字符串列表中，则将布尔值设置为0
			*value = 0;
		}
	}

	// 返回函数执行结果
	return res;
}


/******************************************************************************
 *                                                                            *
 * Function: is_uoct                                                          *
 *                                                                            *
 * Purpose: check if the string is unsigned octal                             *
 *                                                                            *
 * Parameters: str - string to check                                          *
 *                                                                            *
 * Return value:  SUCCEED - the string is unsigned octal                      *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为is_uoct，接收一个const char类型的指针作为参数。
 * 函数的主要目的是判断输入的字符串是否是一个无符号八进制数。
 * 
 * 变量声明：
 * res：表示函数执行结果的整数变量，初始值为FAIL。
 * str：指向输入字符串的指针。
 * 
 * 函数执行流程：
 * 1. 遍历字符串，直到遇到第一个非空格字符。
 * 2. 遍历字符串中的每个字符，判断是否为无符号八进制数。
 * 3. 如果遇到非八进制字符，跳出循环。
 * 4. 如果遍历完整个字符串，都没有遇到非八进制字符，则认为输入的字符串是一个无符号八进制数。
 * 5. 检查字符串的结尾是否有空格，如果有，返回FAIL。
 * 6. 如果没有遇到异常情况，返回res（成功或失败）。
 */
int	is_uoct(const char *str)
{
	int	res = FAIL;										/* 初始化res为失败 */

	while (' ' == *str)									/*  trim left spaces */
		str++;											/* 跳过左边的空格 */

	for (; '\0' != *str; str++)
	{
		if (*str < '0' || *str > '7')					/* 判断字符是否为无符号八进制数 */
			break;

		res = SUCCEED;									/* 如果字符符合要求，更新res为成功 */
	}

	while (' ' == *str)									/* check right spaces */
		str++;											/* 跳过右边的空格 */

	if ('\0' != *str)									/* 如果有字符串结尾，返回失败 */
		return FAIL;

	return res;											/* 如果没有遇到异常情况，返回res（成功或失败） */
}


/******************************************************************************
 *                                                                            *
 * Function: is_uhex                                                          *
 *                                                                            *
 * Purpose: check if the string is unsigned hexadecimal representation of     *
 *          data in the form "0-9, a-f or A-F"                                *
 *                                                                            *
 * Parameters: str - string to check                                          *
 *                                                                            *
 * Return value:  SUCCEED - the string is unsigned hexadecimal                *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个字符串是否为十六进制字符串。函数is_uhex接收一个字符指针作为参数，返回一个整型值，表示判断结果。如果字符串是十六进制字符串，返回SUCCEED（成功）；否则返回FAIL（失败）。在函数内部，首先去除字符串两边的空格，然后遍历字符串，判断每个字符是否为十六进制字符。如果遇到非十六进制字符，跳出循环。最后再次去除字符串右边的空格，如果字符串结尾不是空字符，返回FAIL；否则返回判断结果。
 ******************************************************************************/
int	is_uhex(const char *str)	// 定义一个函数is_uhex，接收一个字符指针作为参数
{
	int	res = FAIL;		// 定义一个整型变量res，初始值为FAIL（失败）

	while (' ' == *str)	/* 去除左边的空格 */
		str++;

	for (; '\0' != *str; str++)	/* 遍历字符串 */
	{
		if (0 == isxdigit(*str))	/* 如果当前字符不是十六进制字符，跳出循环 */
			break;

		res = SUCCEED;	/* 如果当前字符是十六进制字符，更新res为SUCCEED（成功） */
	}

	while (' ' == *str)	/* 去除右边的空格 */
		str++;

	if ('\0' != *str)	/* 如果字符串结尾不是空字符，返回FAIL（失败） */
		return FAIL;

	return res;		/* 返回res的值，即判断结果，如果是十六进制字符串，返回SUCCEED（成功），否则返回FAIL（失败） */
}


/******************************************************************************
 *                                                                            *
 * Function: is_hex_string                                                    *
 *                                                                            *
 * Purpose: check if the string is a hexadecimal representation of data in    *
 *          the form "F4 CE 46 01 0C 44 8B F4\nA0 2C 29 74 5D 3F 13 49\n"     *
 *                                                                            *
 * Parameters: str - string to check                                          *
 *                                                                            *
 * Return value:  SUCCEED - the string is formatted like the example above    *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示。这个函数的主要目的是判断一个字符串是否为十六进制字符串。如果字符串满足以下条件：
 *
 *1. 不是空字符串
 *2. 每个字符都是十六进制字符（0-9，a-f，A-F）
 *3. 每两个十六进制字符之间没有非空格和非换行符的字符
 *
 *那么函数返回成功（SUCCEED），否则返回失败（FAIL）。
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为：is_hex_string
 * 它的作用是判断一个字符串是否为十六进制字符串
 * 输入参数：const char *str，即一个字符串指针
 * 返回值：int，成功（SUCCEED）或失败（FAIL）
 */

int	is_hex_string(const char *str)
{
	// 判断字符串是否为空，如果是空字符串，则返回失败（FAIL）
	if ('\0' == *str)
		return FAIL;

	// 遍历字符串，直到遇到空字符 '\0'
	while ('\0' != *str)
	{
		// 检查当前字符是否为十六进制字符，如果不是，则返回失败（FAIL）
		if (0 == isxdigit(*str))
			return FAIL;

		// 检查下一个字符是否为十六进制字符，如果不是，则返回失败（FAIL）
		if (0 == isxdigit(*(str + 1)))
			return FAIL;

		// 检查第三个字符是否为空字符 '\0'，如果是，则结束循环
		if ('\0' == *(str + 2))
			break;

		// 检查第三个字符是否为空格或换行符，如果不是，则返回失败（FAIL）
		if (' ' != *(str + 2) && '\n' != *(str + 2))

			return FAIL;

		// 移动字符指针，跳过第三个字符及其后面的字符
		str += 3;
	}

	// 如果没有遇到错误，返回成功（SUCCEED）
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: get_nearestindex                                                 *
 *                                                                            *
 * Purpose: get nearest index position of sorted elements in array            *
 *                                                                            *
 * Parameters: p   - pointer to array of elements                             *
 *             sz  - element size                                             *
 *             num - number of elements                                       *
 *             id  - index to look for                                        *
 *                                                                            *
 * Return value: index at which it would be possible to insert the element so *
 *               that the array is still sorted                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个get_nearestindex函数，用于在给定的数组中查找离给定id最近的元素的索引。该函数通过二分查找算法，不断缩小搜索范围，直到找到匹配的元素或搜索范围为空。最后返回找到的匹配元素的索引或最后一个搜索范围的起始索引加1。
 ******************************************************************************/
// 定义一个函数int get_nearestindex，接收4个参数：
// 1. const void *p，指向一个包含zbx_uint64_t类型元素的数组的指针；
// 2. size_t sz，数组中每个元素的size；
// 3. int num，数组中元素的个数；
// 4. zbx_uint64_t id，需要查找的元素的id。
// 函数的作用是在给定的数组中查找离给定id最近的元素的索引。

int	get_nearestindex(const void *p, size_t sz, int num, zbx_uint64_t id)
{
	// 定义两个整型变量first_index和last_index，分别表示数组的起始索引和结束索引；
	// 定义一个zbx_uint64_t类型的变量element_id，用于存储当前索引对应的元素的id；
	int		first_index, last_index, index;
	zbx_uint64_t	element_id;

	if (0 == num)
		// 如果数组为空，直接返回0，表示没有找到匹配的元素；
		return 0;

	first_index = 0;
	last_index = num - 1;

	while (1)
	{
		// 计算当前索引（first_index和last_index的平均值）；
		index = first_index + (last_index - first_index) / 2;

		// 如果当前索引对应的元素id等于给定id，则直接返回当前索引；
		if (id == (element_id = *(const zbx_uint64_t *)((const char *)p + index * sz)))
			return index;

		if (last_index == first_index)
		{
			// 如果last_index等于first_index，说明已经找到了唯一的匹配元素；
			// 如果当前元素id小于给定id，则返回索引加1；

			if (element_id < id)
				index++;
			return index;
		}

		// 如果当前元素id小于给定id，说明匹配的元素在当前索引的右侧，更新first_index为当前索引加1；
		if (element_id < id)
			first_index = index + 1;
		// 如果当前元素id大于等于给定id，说明匹配的元素在当前索引的左侧，更新last_index为当前索引；
		else
			last_index = index;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: uint64_array_add                                                 *
 *                                                                            *
 * Purpose: add uint64 value to dynamic array                                 *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个动态数组的添加功能。该函数接收一个指向 zbx_uint64_t 类型数组的指针、分配内存的指针、存储元素数量的指针、要添加的值以及分配步长。在数组不满的情况下，查找要添加的值在数组中的最近索引，然后将值插入到数组中。如果数组已满，则重新分配内存并移动数组元素，最后将值插入到数组中。
 ******************************************************************************/
// 定义一个函数 uint64_array_add，接收五个参数：
// 参数一：指向 zbx_uint64_t 类型数组的指针
// 参数二：分配内存的指针
// 参数三：存储元素数量的指针
// 参数四：要添加的值
// 参数五：分配步长

int uint64_array_add(zbx_uint64_t **values, int *alloc, int *num, zbx_uint64_t value, int alloc_step)
{
	// 定义一个整型变量 index，用于表示要查找的值在数组中的索引
	int	index;
	// 调用 get_nearestindex 函数，查找 value 在数组中的最近索引
	// 返回值存储在 index 变量中
	index = get_nearestindex(*values, sizeof(zbx_uint64_t), *num, value);

	// 判断 index 是否小于 num 且数组中的值是否等于 value
	// 如果满足条件，直接返回 index
	if (index < (*num) && (*values)[index] == value)
		return index;

	// 判断 alloc 是否等于 num，如果等于，说明数组已满
	// 分配步长为 0 时，提示错误并退出程序
	if (*alloc == *num)
	{
		if (0 == alloc_step)
		{
			zbx_error("Unable to reallocate buffer");
			assert(0); //  assertion 断言，分配步长不能为 0
		}

		// 增加分配内存大小
		*alloc += alloc_step;

		// 重新分配内存，并将原数组内容复制到新数组中
		*values = (zbx_uint64_t *)zbx_realloc(*values, *alloc * sizeof(zbx_uint64_t));
	}

	// 移动数组元素，将索引后面的元素向前移动
	// 移动的元素数量为数组长度减去索引
	memmove(&(*values)[index + 1], &(*values)[index], sizeof(zbx_uint64_t) * (*num - index));

	// 将 value 存储在索引位置
	(*values)[index] = value;

	// 元素数量加 1
	(*num)++;

	// 返回索引
	return index;
}


/******************************************************************************
 *                                                                            *
 * Function: uint64_array_exists                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查一个给定的值是否在一个uint64_t类型的数组中存在。如果存在，则返回成功（SUCCEED），否则返回失败（FAIL）。为实现这个目的，代码首先定义了一个整型变量index，用于存储数组中value的最近索引。然后调用get_nearestindex函数查找value在数组中的最近索引，并根据查找结果返回相应的状态码。
 ******************************************************************************/
// 定义一个函数 uint64_array_exists，接收三个参数：
// 参数一：指向zbx_uint64_t类型数组的指针
// 参数二：数组的长度
// 参数三：需要查找的值
int uint64_array_exists(const zbx_uint64_t *values, int num, zbx_uint64_t value)
{
	// 定义一个整型变量 index，用于存储查找结果
	int index;

	// 调用 get_nearestindex 函数，查找 value 在数组中的最近索引
	// 参数一：指向数组的指针
	// 参数二：数组元素的大小（这里是zbx_uint64_t类型，大小为8字节）
	// 参数三：数组的长度
	// 参数四：需要查找的值
	index = get_nearestindex(values, sizeof(zbx_uint64_t), num, value);
	
	// 判断查找结果是否小于数组长度（即找到了对应的值）
	// 如果是，且查找到的值等于传入的 value，则返回成功（SUCCEED）
	if (index < num && values[index] == value)
		return SUCCEED;

	// 如果没有找到对应的值，返回失败（FAIL）
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: uint64_array_remove                                              *
 *                                                                            *
 * Purpose: remove uint64 values from array                                   *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是：从一个uint64_t类型的数组中删除指定数量的元素。这些元素的位置由rm_values数组指定。
 *
 *整个代码块的功能如下：
 *1. 遍历要删除的值的数量。
 *2. 对于每个要删除的值，计算在数组中的位置。
 *3. 如果位置等于数组长度或者要删除的值与数组中的值不相等，则继续循环。
 *4. 移动数组元素，将后面的元素向前移动一个位置。
 *5. 减去一个元素数量。
 ******************************************************************************/
void uint64_array_remove(zbx_uint64_t *values, int *num, const zbx_uint64_t *rm_values, int rm_num)
{
	// 定义变量
	int	rindex, index;

	// 遍历要删除的值的数量
	for (rindex = 0; rindex < rm_num; rindex++)
	{
		// 计算在数组中要删除的值的位置
		index = get_nearestindex(values, sizeof(zbx_uint64_t), *num, rm_values[rindex]);
		// 如果位置等于数组长度或者要删除的值与数组中的值不相等，则继续循环
		if (index == *num || values[index] != rm_values[rindex])
			continue;

		// 移动数组元素，将后面的元素向前移动一个位置
		memmove(&values[index], &values[index + 1], sizeof(zbx_uint64_t) * ((*num) - index - 1));
		// 减去一个元素数量
		(*num)--;
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将字符串转换为相应的字节单位或时间单位。函数`convert_unit`接收一个字符作为输入，使用switch语句根据字符的不同取值，返回相应的字节单位或时间单位。例如，如果输入为'K'，则返回1024字节；如果输入为'M'，则返回1024*1024字节；如果输入为's'，则返回1秒；如果输入为'm'，则返回60秒，以此类推。如果输入的字符不在指定的范围内，则默认返回1。
 ******************************************************************************/
// 定义一个函数，用于将字符转换为相应的字节单位或时间单位
zbx_uint64_t	suffix2factor(char c)
{


    // 使用switch语句根据输入的字符c，返回相应的字节单位或时间单位
    switch (c)
    {
        case 'K':
            // 输入为'K'，返回ZBX_KIBIBYTE，即1024字节
            return ZBX_KIBIBYTE;
        case 'M':
            // 输入为'M'，返回ZBX_MEBIBYTE，即1024*1024字节
            return ZBX_MEBIBYTE;
        case 'G':
            // 输入为'G'，返回ZBX_GIBIBYTE，即1024*1024*1024字节
            return ZBX_GIBIBYTE;
        case 'T':
            // 输入为'T'，返回ZBX_TEBIBYTE，即1024*1024*1024*1024字节
            return ZBX_TEBIBYTE;
        case 's':
            // 输入为's'，返回1，表示秒
            return 1;
        case 'm':
            // 输入为'm'，返回SEC_PER_MIN，即60秒
            return SEC_PER_MIN;
        case 'h':
            // 输入为'h'，返回SEC_PER_HOUR，即3600秒
            return SEC_PER_HOUR;
        case 'd':
            // 输入为'd'，返回SEC_PER_DAY，即24*3600秒
            return SEC_PER_DAY;
        case 'w':
            // 输入为'w'，返回SEC_PER_WEEK，即7*24*3600秒
            return SEC_PER_WEEK;
        default:
            // 输入为其他字符，返回1，表示默认单位为1
            return 1;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: str2uint64                                                       *
 *                                                                            *
 * Purpose: convert string to 64bit unsigned integer                          *
 *                                                                            *
 * Parameters: str   - string to convert                                      *
 *             value - a pointer to converted value                           *
 *                                                                            *
 * Return value:  SUCCEED - the string is unsigned integer                    *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: the function automatically processes suffixes K, M, G, T         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个字符串转换为zbx_uint64_t类型的数值，根据可选的后缀调整转换因子。具体步骤如下：
 *
 *1. 获取字符串str的长度，并设置一个指向字符串末尾的指针p；
 *2. 检查字符串的最后一个字符是否为可选后缀中的一个，如果是，则根据后缀调整转换因子factor；
 *3. 减去后缀长度，以便后续处理；
 *4. 调用is_uint64_n函数判断是否可以成功将字符串转换为zbx_uint64_t类型；
 *5. 如果转换成功，将转换因子乘以结果；
 *6. 返回转换结果。
 ******************************************************************************/
// 定义一个函数int str2uint64，接收三个参数：
// 1. const char *str，字符串，用来表示要转换的数值；
// 2. const char *suffixes，字符串，用来表示可选的后缀，用于确定转换因子；
// 3. zbx_uint64_t *value，指向一个zbx_uint64_t类型的指针，用于存储转换后的结果。
// 函数的主要目的是将字符串str转换为zbx_uint64_t类型的数值，根据suffixes指定的后缀来调整转换因子。

int	str2uint64(const char *str, const char *suffixes, zbx_uint64_t *value)
{
	// 定义一个长度为64的字符串缓冲区，用于存储待转换的字符串
	size_t		sz;
	const char	*p;
	int		ret;
	zbx_uint64_t	factor = 1;

	// 获取str的长度
	sz = strlen(str);
	// 指向字符串末尾的指针
	p = str + sz - 1;

	// 检查当前字符串的最后一个字符是否为可选后缀中的一个
	if (NULL != strchr(suffixes, *p))
	{
		// 根据后缀调整转换因子
		factor = suffix2factor(*p);

		// 减去后缀长度，以便后续处理
		sz--;
	}

	// 判断是否可以成功将字符串转换为zbx_uint64_t类型
	if (SUCCEED == (ret = is_uint64_n(str, sz, value)))
	{
		// 将转换因子乘以结果
		*value *= factor;
	}

	// 返回转换结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: str2double                                                       *
 *                                                                            *
 * Purpose: convert string to double                                          *
 *                                                                            *
 * Parameters: str - string to convert                                        *
 *                                                                            *
 * Return value: converted double value                                       *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: the function automatically processes suffixes K, M, G, T and     *
 *           s, m, h, d, w                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个字符串表示的数值转换为双精度浮点数。函数 str2double 接收一个字符指针（字符串），首先计算字符串的长度（减1），然后根据字符串的结尾字符调用 suffix2factor 函数计算倍数因子，最后将字符串表示的数值转换为双精度浮点数并返回。
 ******************************************************************************/
/*
 * 定义一个函数 str2double，接收一个字符指针（字符串），将其转换为double类型。
 * 函数主要目的是将字符串表示的数值转换为双精度浮点数。
 * 
 * 参数：
 *   str：输入的字符串，表示需要转换的数值。
 *
 * 返回值：
 *   返回转换后的双精度浮点数。
 */
double str2double(const char *str)
{
	/* 定义一个变量 sz，用于存储字符串的长度 */
	size_t sz;

	/* 计算字符串的长度，减1是为了排除字符串结尾的空字符 '\0' */
	sz = strlen(str) - 1;

	/* 调用 suffix2factor 函数，根据字符串的结尾字符计算倍数因子 */
	double factor = suffix2factor(str[sz]);

	/* 将字符串表示的数值转换为双精度浮点数，并返回 */
	return atof(str) * factor;
}


/******************************************************************************
 *                                                                            *
 * Function: is_hostname_char                                                 *
 *                                                                            *
 * Return value:  SUCCEED - the char is allowed in the host name              *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: in host name allowed characters: '0-9a-zA-Z. _-'                 *
 *           !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示。这个函数的主要目的是判断一个字符是否为合法的主机名字符，根据不同的条件返回SUCCEED或FAIL。
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为is_hostname_char。
 * 该函数的作用是判断一个字符是否为合法的主机名字符。
 * 输入参数：
 *   unsigned char c：一个unsigned char类型的字符。
 * 返回值：
 *   int类型，返回值为SUCCEED或FAIL。若字符为合法的主机名字符，返回SUCCEED；否则返回FAIL。
 * 函数主要分为以下三个判断条件：
 * 1. 如果字符是字母或数字（isalnum()函数判断），则返回SUCCEED。
 * 2. 如果字符是点('.')、空格(' ')、下划线('_')或减号('-')，则返回SUCCEED。
 * 3. 如果不满足以上两个条件，则返回FAIL。
 */
int	is_hostname_char(unsigned char c)
{
	// 判断字符是否为字母或数字（isalnum()函数判断）
	if (0 != isalnum(c))
	{
		// 如果字符是字母或数字，返回SUCCEED
		return SUCCEED;
	}

	// 判断字符是否为点('.')、空格(' ')、下划线('_')或减号('-')
	if (c == '.' || c == ' ' || c == '_' || c == '-')
	{
		// 如果字符为点('.）、空格(' ')、下划线('_')或减号('-'), 返回SUCCEED
		return SUCCEED;
	}

	// 如果不满足以上两个条件，返回FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: is_key_char                                                      *
 *                                                                            *
 * Return value:  SUCCEED - the char is allowed in the item key               *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: in key allowed characters: '0-9a-zA-Z._-'                        *
 *           !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示。这个函数的作用是判断给定的字符是否为关键字字符，如果是，返回SUCCEED，否则返回FAIL。关键字字符包括数字、字母、点（.）、下划线（_）和减号（-）。
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为is_key_char，接收一个无符号字符类型的参数c。
 * 函数的主要目的是判断给定的字符c是否为关键字字符。
 * 关键字字符包括：数字、字母、点（.）、下划线（_）和减号（-）。
 * 如果给定的字符是关键字字符，函数返回SUCCEED，否则返回FAIL。
 */

int	is_key_char(unsigned char c)
{
	// 首先，使用isalnum()函数判断给定的字符c是否为数字或字母。
	// 如果返回值为0，说明c是数字或字母，继续执行后续判断。
	if (0 != isalnum(c))
	{
		// 如果c是小写字母，将其转换为大写字母
		if (islower(c))
		{
			c = toupper(c);
		}

		// 判断c是否为点（.）、下划线（_）或减号（-）。
		if (c == '.' || c == '_' || c == '-')
		{
			// 如果c为关键字字符，返回SUCCEED
			return SUCCEED;
		}
	}

	// 如果c不是关键字字符，返回FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: is_function_char                                                 *
 *                                                                            *
 * Return value:  SUCCEED - the char is allowed in the trigger function       *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: in trigger function allowed characters: 'a-z'                    *
 *           !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为：is_function_char
 * 它的参数是一个无符号字符类型（unsigned char），表示一个字符
 * 函数的主要目的是判断这个字符是否为英文字母（小写）
 * 
 * 以下是代码的逐行注释：
 * 
 * int 表示这是一个整数类型的函数返回值
 * is_function_char 表示函数的名称为 is_function_char
 * (unsigned char c) 表示函数的参数是一个无符号字符类型
 * 
 * 如果（0 != islower(c)）：
 * 这里使用 islower() 函数来判断字符 c 是否为小写字母
 * 如果 c 是小写字母，那么函数返回 SUCCEED（成功）；否则，继续执行后续代码
 * 
 * 返回 SUCCEED：
 * 如果字符 c 是小写字母，那么函数返回 SUCCEED（成功）
 * 
 * 返回 FAIL：
 * 如果字符 c 不是小写字母，那么函数返回 FAIL（失败）
 */
int	is_function_char(unsigned char c)
{
	if (0 != islower(c))
		return SUCCEED;

	return FAIL;
}




/******************************************************************************
 *                                                                            *
 * Function: is_macro_char                                                    *
 *                                                                            *
 * Return value:  SUCCEED - the char is allowed in the macro name             *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: allowed characters in macro names: '0-9A-Z._'                    *
 *           !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断一个输入的字符是否为字母、数字、下划线或点，如果满足这些条件，返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个函数，用于判断一个字符是否为字母、数字、下划线或点，返回成功或失败
int is_macro_char(unsigned char c)
{
    // 判断字符是否为字母，如果是，返回成功
    if (0 != isupper(c))
    {
        return SUCCEED;
    }

    // 判断字符是否为点或下划线，如果是，返回成功
    if ('.' == c || '_' == c)
    {
        return SUCCEED;
    }

    // 判断字符是否为数字，如果是，返回成功
    if (0 != isdigit(c))
    {
        return SUCCEED;
    }

    // 如果以上条件都不满足，返回失败
    return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: is_discovery_macro                                               *
 *                                                                            *
 * Purpose: checks if the name is a valid discovery macro                     *
 *                                                                            *
 * Return value:  SUCCEED - the name is a valid discovery macro               *
 *                FAIL - otherwise                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示。这个函数的主要目的是检查传入的字符串是否是一个合法的宏定义。如果是一个合法的宏定义，函数返回成功（SUCCEED），否则返回失败（FAIL）。
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为is_discovery_macro，接收一个const char类型的指针作为参数。
 * 函数的主要目的是检查传入的字符串是否是一个宏定义。
 * 
 * 以下是代码的逐行注释：
 */

// 定义一个整型函数，返回值为整型
int	is_discovery_macro(const char *name) // 接收一个const char类型的指针作为参数
{
	if ('{' != *name++ || '#' != *name++) // 检查字符串开头是否为花括号或井号
		return FAIL; // 如果不是，返回失败

	do // 开始一个循环
	{
		if (SUCCEED != is_macro_char(*name++)) // 检查当前字符是否为宏字符
			return FAIL; // 如果不是，返回失败

	} while ('}' != *name); // 直到遇到右花括号为止

	if ('\0' != name[1]) // 检查第二个字符是否为空字符
		return FAIL; // 如果不是，返回失败

	return SUCCEED; // 如果是，返回成功
}


/******************************************************************************
 *                                                                            *
 * Function: is_time_function                                                 *
 *                                                                            *
 * Return value:  SUCCEED - given function is time-based                      *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 ******************************************************************************/
// 定义一个C语言函数，判断给定的字符串是否包含在指定的字符串列表中
// 函数原型：int is_time_function(const char *func)
int	is_time_function(const char *func)
{
    // 使用str_in_list函数判断给定的func字符串是否在"nodata,date,dayofmonth,dayofweek,time,now"这个列表中
    // 这里使用了逗号作为分隔符，表示列表中的每个元素
    return str_in_list("nodata,date,dayofmonth,dayofweek,time,now", func, ',');
}

// str_in_list函数的功能是判断给定的字符串是否在指定的字符串列表中，如果存在于列表中，则返回1，否则返回0
// 函数原型：int str_in_list(const char *list, const char *str, char delimiter)



/******************************************************************************
 *                                                                            *
 * Function: is_snmp_type                                                     *
 *                                                                            *
 * Return value:  SUCCEED  - the given type is one of regular SNMP types      *
 *                FAIL - otherwise                                            *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断给定的类型值是否为SNMP类型（SNMPv1、SNMPv2c或SNMPv3）。函数`is_snmp_type`接收一个无符号字符类型的参数`type`，返回值为整型。如果给定的类型值等于SNMPv1、SNMPv2c或SNMPv3的类型值，函数返回`SUCCEED`，表示成功；否则返回`FAIL`，表示失败。
 ******************************************************************************/
// 定义一个函数，用于判断给定的类型值是否为SNMP类型（SNMPv1、SNMPv2c或SNMPv3）
int is_snmp_type(unsigned char type)
{
    // 定义三个常量，分别表示SNMPv1、SNMPv2c和SNMPv3的类型值
    static const int ITEM_TYPE_SNMPv1 = 0;
    static const int ITEM_TYPE_SNMPv2c = 1;
    static const int ITEM_TYPE_SNMPv3 = 2;

    // 判断给定的类型值是否为SNMP类型，即判断是否等于SNMPv1、SNMPv2c或SNMPv3的类型值
    return ITEM_TYPE_SNMPv1 == type || ITEM_TYPE_SNMPv2c == type || ITEM_TYPE_SNMPv3 == type ? SUCCEED : FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: make_hostname                                                    *
 *                                                                            *
 * Purpose: replace all not-allowed hostname characters in the string         *
 *                                                                            *
 * Parameters: host - the target C-style string                               *
 *                                                                            *
 * Author: Dmitry Borovikov                                                   *
 *                                                                            *
 * Comments: the string must be null-terminated, otherwise not secure!        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将一个字符串转换为合法的主机名。函数`make_hostname`接收一个字符指针作为参数，遍历该字符串中的每一个字符，并判断是否为合法的主机名字符。如果不是，将其替换为下划线。这样，最后输出的结果就是一个合法的主机名。
 ******************************************************************************/
void	make_hostname(char *host) // 定义一个函数，接收一个字符指针作为参数，该函数的主要目的是将传入的字符串转换为合法的主机名
{
	char	*c; // 定义一个字符指针变量c，用于遍历传入的字符串

	assert(host); // 检查传入的字符串不为空，如果为空则抛出异常

	for (c = host; '\0' != *c; ++c) // 遍历字符串中的每一个字符
	{
		if (FAIL == is_hostname_char(*c)) // 判断当前字符是否为合法的主机名字符
			*c = '_'; // 如果不是合法的主机名字符，则将其替换为下划线
	}
}


/******************************************************************************
 *                                                                            *
 * Function: get_interface_type_by_item_type                                  *
 *                                                                            *
 * Purpose:                                                                   *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: Interface type                                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的type参数，判断其对应的接口类型，并将结果存储在无符号字符型变量中返回。接口类型包括：代理类型（ZABBIX）、SNMP类型（SNMPv1、SNMPv2c、SNMPv3、SNMPTRAP）、IPMI类型、JMX类型、简单类型、外部类型、SSH类型、TELNET类型、HTTPAGENT类型等。如果type不属于以上任何一种类型，则返回未知类型的接口。
 ******************************************************************************/
// 定义一个无符号字符型变量，用于存储接口类型
unsigned char	get_interface_type_by_item_type(unsigned char type) // 定义一个函数，接收一个无符号字符型参数type
{
	switch (type) // 使用switch语句根据type的值进行分支处理
	{
		case ITEM_TYPE_ZABBIX: // 当type等于ZABBIX类型的物品时
			return INTERFACE_TYPE_AGENT; // 返回代理类型的接口
		case ITEM_TYPE_SNMPv1: // 当type等于SNMPv1类型的物品时
		case ITEM_TYPE_SNMPv2c: // 当type等于SNMPv2c类型的物品时
		case ITEM_TYPE_SNMPv3: // 当type等于SNMPv3类型的物品时
		case ITEM_TYPE_SNMPTRAP: // 当type等于SNMPTRAP类型的物品时
			return INTERFACE_TYPE_SNMP; // 返回SNMP类型的接口
		case ITEM_TYPE_IPMI: // 当type等于IPMI类型的物品时
			return INTERFACE_TYPE_IPMI; // 返回IPMI类型的接口
		case ITEM_TYPE_JMX: // 当type等于JMX类型的物品时
			return INTERFACE_TYPE_JMX; // 返回JMX类型的接口
		case ITEM_TYPE_SIMPLE: // 当type等于简单类型的物品时
		case ITEM_TYPE_EXTERNAL: // 当type等于外部类型的物品时
		case ITEM_TYPE_SSH: // 当type等于SSH类型的物品时
		case ITEM_TYPE_TELNET: // 当type等于TELNET类型的物品时
		case ITEM_TYPE_HTTPAGENT: // 当type等于HTTPAGENT类型的物品时
			return INTERFACE_TYPE_ANY; // 返回任意类型的接口
		default: // 当type不属于以上任何一种类型时
			return INTERFACE_TYPE_UNKNOWN; // 返回未知类型的接口
	}
}


/******************************************************************************
 *                                                                            *
 * Function: calculate_sleeptime                                              *
 *                                                                            *
 * Purpose: calculate sleep time for Zabbix processes                         *
 *                                                                            *
 * Parameters: nextcheck     - [IN] next check or -1 (FAIL) if nothing to do  *
 *             max_sleeptime - [IN] maximum sleep time, in seconds            *
 *                                                                            *
 * Return value: sleep time, in seconds                                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个名为nextcheck的整数与当前时间（time(NULL)）之差，然后根据这个差值和给定的最大睡眠时间max_sleeptime来确定实际的睡眠时间。如果nextcheck为失败，则直接返回最大睡眠时间。最后将计算得到的睡眠时间返回。
 ******************************************************************************/
// 定义一个C语言函数，用于计算睡眠时间
int calculate_sleeptime(int nextcheck, int max_sleeptime)
{
	// 定义一个整型变量sleeptime，用于存储计算后的睡眠时间
	int	sleeptime;

	// 判断nextcheck是否为FAIL（失败）
	if (FAIL == nextcheck)
		// 如果nextcheck为失败，直接返回最大睡眠时间max_sleeptime
		return max_sleeptime;

	// 计算睡眠时间：nextcheck减去当前时间（time(NULL)）
	sleeptime = nextcheck - (int)time(NULL);

	// 判断计算得到的睡眠时间是否小于0
	if (sleeptime < 0)
		// 如果睡眠时间小于0，返回0
		return 0;

	// 判断计算得到的睡眠时间是否大于最大睡眠时间max_sleeptime
	if (sleeptime > max_sleeptime)
		// 如果睡眠时间大于最大睡眠时间，返回最大睡眠时间
		return max_sleeptime;

	// 如果sleepTime在0和最大睡眠时间之间，返回计算得到的睡眠时间
	return sleeptime;
}


/******************************************************************************
 *                                                                            *
 * Function: parse_serveractive_element                                       *
 *                                                                            *
 * Purpose: parse a ServerActive element like "IP<:port>" or "[IPv6]<:port>"  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个字符串，提取出对应的IP地址和端口，并将提取到的信息存储到指定的指针变量中。在这个过程中，首先判断字符串是否以'['开头，如果是，则尝试解析IPv6地址；否则，尝试解析IPv4地址。在解析地址的过程中，检查地址是否合法，如果不合法，则跳转到fail标签处，表示解析失败。如果地址解析成功，将地址复制到指定的内存空间，并返回操作成功。
 ******************************************************************************/
int	parse_serveractive_element(char *str, char **host, unsigned short *port, unsigned short port_default)
{
	// 定义一个全局变量，用于标记是否支持IPv6
	#ifdef HAVE_IPV6
	char	*r1 = NULL;	// 定义一个指针r1，用于处理IPv6地址
	#endif
	char	*r2 = NULL;	// 定义一个指针r2，用于处理IPv4地址
	int	res = FAIL;	// 定义一个变量res，初始值为FAIL，表示操作失败

	*port = port_default;	// 将指针*port指向的值设置为port_default，即默认端口

	#ifdef HAVE_IPV6
	if ('[' == *str)	// 如果字符串以'['开头，表示可能是IPv6地址
	{
		str++;		// 跳过'['

		if (NULL == (r1 = strchr(str, ']')))	// 查找字符串中是否包含']'，如果没有，表示地址不合法
			goto fail;	// 跳转到fail标签处，表示地址解析失败

		if (':' != r1[1] && '\0' != r1[1])	// 检查']'后面的字符是否为':'，如果不是，表示地址不合法
			goto fail;

		if (':' == r1[1] && SUCCEED != is_ushort(r1 + 2, port))	// 如果地址后面跟着一个数字，尝试将其解析为 unsigned short 类型，如果不成功，表示地址不合法
			goto fail;

		*r1 = '\0';	// 将r1指向的字符串结束符改为'\0'

		if (SUCCEED != is_ip6(str))	// 检查地址是否为有效的IPv6地址
			goto fail;

		*host = zbx_strdup(*host, str);	// 如果地址解析成功，将地址复制到*host指向的内存空间
	}
	else if (SUCCEED == is_ip6(str))	// 如果字符串是以'['开头的IPv6地址
	{
		*host = zbx_strdup(*host, str);	// 直接将地址复制到*host指向的内存空间
	}
	else
	{
#endif
		if (NULL != (r2 = strchr(str, ':')))	// 如果字符串中包含':'，表示可能是IPv4地址
		{
			if (SUCCEED != is_ushort(r2 + 1, port))	// 尝试将地址后面的数字解析为 unsigned short 类型，如果不成功，表示地址不合法
				goto fail;

			*r2 = '\0';	// 将r2指向的字符串结束符改为'\0'
		}

		*host = zbx_strdup(NULL, str);	// 如果地址解析成功，将地址复制到新的内存空间
#ifdef HAVE_IPV6
}
#endif


	res = SUCCEED;

fail:
	#ifdef HAVE_IPV6
	if (NULL != r1)
		*r1 = ']';	// 如果r1指向的地址解析失败，将其恢复为原始状态
	#endif
	if (NULL != r2)
		*r2 = ':';	// 如果r2指向的地址解析失败，将其恢复为原始状态

	return res;	// 返回操作结果
}

/******************************************************************************
 * c
 *void\tzbx_alarm_flag_set(void)\t\t\t// 定义一个名为zbx_alarm_flag_set的函数，不返回任何值
 *{
 *\t// 定义一个名为zbx_timed_out的变量，并将其初始化为1
 *\tzbx_timed_out = 1;
 *}
 *```
 ******************************************************************************/
// 这是一个C语言代码块，定义了一个名为zbx_alarm_flag_set的函数。
// 函数类型为void，表示该函数不返回任何值。
void zbx_alarm_flag_set(void)
{
	// 定义一个名为zbx_timed_out的变量，并将其初始化为1。
	zbx_timed_out = 1;
}



/******************************************************************************
 * *
 *这块代码的主要目的是清除zbx_timed_out变量，将其值设置为0。这个函数可能在一个定时器中断处理程序中使用，当定时器超时时，调用这个函数清除计时器超时的标志。
 ******************************************************************************/
// 这是一个C语言代码块，定义了一个名为zbx_alarm_flag_clear的函数，其作用是清除zbx_timed_out变量。
void	zbx_alarm_flag_clear(void)
{
	// 定义一个整型变量zbx_timed_out，并将其初始化为0。
	zbx_timed_out = 0;
}



#if !defined(_WINDOWS)
/******************************************************************************
 * *
 *这块代码的主要目的是设置一个闹钟，闹钟的触发时间为参数seconds表示的秒数。函数首先清除zbx_alarm_flag标志位，然后调用alarm函数设置闹钟。如果设置成功，返回1，否则返回0。
 ******************************************************************************/
unsigned int zbx_alarm_on(unsigned int seconds) // 定义一个名为zbx_alarm_on的函数，参数为一个无符号整数seconds
{
	zbx_alarm_flag_clear(); // 先清除zbx_alarm_flag标志位

	return alarm(seconds); // 调用alarm函数，设置闹钟，参数为seconds，返回值为设置成功与否的布尔值
}

/******************************************************************************
 * *
 *这块代码的主要目的是关闭警报器并清除警报标志。函数zbx_alarm_off接收无参数，返回一个无符号整数。在函数内部，首先定义一个无符号整数变量ret，用于存储函数返回值。接着调用alarm函数，参数为0，用于关闭警报器。然后调用zbx_alarm_flag_clear函数，用于清除警报标志。最后返回变量ret的值。
 ******************************************************************************/
unsigned int	zbx_alarm_off(void)		// 定义一个名为zbx_alarm_off的函数，参数为void，返回值为无符号整数
{
	unsigned int	ret;			// 定义一个无符号整数变量ret，用于存储函数返回值

	ret = alarm(0);			// 调用alarm函数，参数为0，用于关闭警报器
	zbx_alarm_flag_clear();		// 调用zbx_alarm_flag_clear函数，用于清除警报标志
	return ret;			// 返回变量ret的值
}

#endif
/******************************************************************************
 * *
 *这块代码的主要目的是判断zbx_timed_out变量是否为0（即是否超时），并根据判断结果返回成功或失败的标志。如果zbx_timed_out为0（超时），则返回FAIL（失败）；否则返回SUCCEED（成功）。
 ******************************************************************************/
// 定义一个名为 zbx_alarm_timed_out 的函数，该函数接受 void 类型的参数
int zbx_alarm_timed_out(void)
{
    // 定义一个返回值，初始值为 FAIL（失败）
    int ret = FAIL;

    // 判断 zbx_timed_out 变量是否为 0（即是否超时）
    if (0 == zbx_timed_out)
    {
        // 如果超时，返回 FAIL（失败）
        ret = FAIL;
    }
    else
    {
        // 如果不超时，返回 SUCCEED（成功）
        ret = SUCCEED;
    }

    // 函数执行完毕，返回 ret 变量，即判断zbx_timed_out 变量的值来返回成功或失败
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_create_token                                                 *
 *                                                                            *
 * Purpose: creates semi-unique token based on the seed and current timestamp *
 *                                                                            *
 * Parameters:  seed - [IN] the seed                                          *
 *                                                                            *
 * Return value: Hexadecimal token string, must be freed by caller            *
 *                                                                            *
 * Comments: if you change token creation algorithm do not forget to adjust   *
 *           ZBX_DATA_SESSION_TOKEN_SIZE definition                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是生成一个基于给定seed的md5字符串。它首先定义了一些必要的变量和常量，然后分配内存空间存储生成的token。接下来，它获取当前时间，并将其存储在ts结构体中。然后，它初始化md5计算状态，并将seed和ts的值作为输入数据进行md5计算。计算完成后，它遍历md5结果的字节数组，将其转换为16进制字符串。最后，它在字符串末尾添加'\\0'，使其成为一个字符串，并返回生成的token。
 ******************************************************************************/
// 定义一个函数，用于生成一个基于seed的md5字符串
char *zbx_create_token(zbx_uint64_t seed)
{
	// 定义一个常量字符串，用于表示md5值的16进制编码
	const char *hex = "0123456789abcdef";
	// 定义一个时间结构体，用于存储当前时间
	zbx_timespec_t	ts;
	// 定义一个md5计算状态结构体
	md5_state_t	state;
	// 定义一个md5计算结果的字节数组
	md5_byte_t	hash[MD5_DIGEST_SIZE];
	// 定义一个循环变量
	int		i;
	// 定义一个字符指针，用于存储生成的token
	char		*token, *ptr;

	// 为token分配内存空间，保证其长度至少为ZBX_DATA_SESSION_TOKEN_SIZE+1
	ptr = token = (char *)zbx_malloc(NULL, ZBX_DATA_SESSION_TOKEN_SIZE + 1);

	// 获取当前时间，并将其存储在ts结构体中
	zbx_timespec(&ts);

	// 初始化md5计算状态
	zbx_md5_init(&state);
	// 将seed的值作为md5计算的输入数据
	zbx_md5_append(&state, (const md5_byte_t *)&seed, (int)sizeof(seed));
	// 将当前时间的值作为md5计算的输入数据
	zbx_md5_append(&state, (const md5_byte_t *)&ts, (int)sizeof(ts));
	// 计算md5值
	zbx_md5_finish(&state, hash);

	// 遍历md5计算结果的字节数组，将其转换为16进制字符串
	for (i = 0; i < MD5_DIGEST_SIZE; i++)
	{
		*ptr++ = hex[hash[i] >> 4];
		*ptr++ = hex[hash[i] & 15];
	}

	// 在字符串末尾添加'\0'，使其成为一个字符串
	*ptr = '\0';

	// 返回生成的token字符串
	return token;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_update_env                                                   *
 *                                                                            *
 * Purpose: throttling of update "/etc/resolv.conf" and "stdio" to the new    *
 *          log file after rotation                                           *
 *                                                                            *
 * Parameters: time_now - [IN] the time for compare in seconds                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：检查当前时间与上一次更新时间之差，如果大于1.0秒，则执行更新操作，包括处理日志旋转和更新解析器配置。其中，日志旋转和解析器配置的具体处理方式取决于系统是否包含对应的头文件。
 ******************************************************************************/
void zbx_update_env(double time_now) // 定义一个名为zbx_update_env的函数，接收一个double类型的参数time_now
{
	static double time_update = 0; // 定义一个静态变量time_update，用于存储上一次更新时间

	/* 处理/etc/resolv.conf更新和日志旋转频率低于每秒一次 */
	if (1.0 < time_now - time_update) // 如果当前时间与上一次更新时间之差大于1.0秒
	{
		time_update = time_now; // 更新time_update的值为当前时间
		zbx_handle_log(); // 调用zbx_handle_log函数处理日志
#if !defined(_WINDOWS) && defined(HAVE_RESOLV_H) // 如果当前系统不是Windows，且系统包含resolv.h头文件
		zbx_update_resolver_conf(); // 调用zbx_update_resolver_conf函数更新解析器配置
#endif
	}
}

