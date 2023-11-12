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

#include "zbxalgo.h"

typedef unsigned char uchar;

/*
 * Bob Jenkins hash function (see http://burtleburtle.net/bob/hash/evahash.html)
 */
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`zbx_hash_lookup2`的函数，该函数接收三个参数：一个指向数据的指针`data`，数据长度`len`，以及一个初始化种子`seed`。函数计算数据内容的哈希值，并将结果返回。
 *
 *代码中首先将`data`指针转换为`uchar`类型，然后定义了一个名为`mix`的混合函数，用于混合三个哈希值。接下来，使用循环遍历数据，逐字节处理数据，并调用`mix`函数混合计算哈希值。最后，返回计算得到的哈希值。
 ******************************************************************************/
zbx_hash_t	zbx_hash_lookup2(const void *data, size_t len, zbx_hash_t seed)
{
	// 将传入的data指针转换为uchar类型
	const uchar	*p = (const uchar *)data;

	// 定义一个混合函数mix，用于混合三个hash值
	zbx_hash_t	a, b, c;

// 定义一个混合函数mix，用于混合三个hash值
#define	mix(a, b, c)						\
{								\
	a = a - b;	a = a - c;	a = a ^ (c >> 13);	\
	b = b - c;	b = b - a;	b = b ^ (a << 8);	\
	c = c - a;	c = c - b;	c = c ^ (b >> 13);	\
	a = a - b;	a = a - c;	a = a ^ (c >> 12);	\
	b = b - c;	b = b - a;	b = b ^ (a << 16);	\
	c = c - a;	c = c - b;	c = c ^ (b >> 5);	\
	a = a - b;	a = a - c;	a = a ^ (c >> 3);	\
	b = b - c;	b = b - a;	b = b ^ (a << 10);	\
	c = c - a;	c = c - b;	c = c ^ (b >> 15);	\
}


	a = b = 0x9e3779b9u;
	c = seed;
    
    /******************************************************************************
     * *
     ******************************************************************************/
    // 这是一个C语言代码块，主要目的是对输入的数据进行哈希计算。
    // 输入的数据是一个字节串，长度为len。
    // 代码块首先定义了三个变量a、b、c，用于存储哈希计算的中间结果。
    while (len >= 12) // 当输入数据长度大于等于12字节时，执行以下操作：
    {
        // 计算a、b、c三个变量的值，分别为：
            a = a + (p[0] + ((zbx_hash_t)p[1] << 8) + ((zbx_hash_t)p[2]  << 16) + ((zbx_hash_t)p[3]  << 24));
            b = b + (p[4] + ((zbx_hash_t)p[5] << 8) + ((zbx_hash_t)p[6]  << 16) + ((zbx_hash_t)p[7]  << 24));
            c = c + (p[8] + ((zbx_hash_t)p[9] << 8) + ((zbx_hash_t)p[10] << 16) + ((zbx_hash_t)p[11] << 24));

        mix(a, b, c); // 对a、b、c三个变量进行混合运算，具体运算未知。

        // 移动输入数据指针p，使其指向下一个12字节的起始位置。
        p += 12;
        // 更新输入数据长度，减去已处理的12字节。
        len -= 12;
    }

    c = c + (zbx_hash_t)len; // 添加len到变量c中。

    switch (len)
    {
        // 根据输入数据长度，对变量c进行不同的运算。
        case 11:	c = c + ((zbx_hash_t)p[10] << 24); // 当len为11时，执行此操作。
                ZBX_FALLTHROUGH; // 标签ZBX_FALLTHROUGH表示下面的代码块不进行注释。
        case 10:	c = c + ((zbx_hash_t)p[9] << 16); // 当len为10时，执行此操作。
                ZBX_FALLTHROUGH;
        case 9:		c = c + ((zbx_hash_t)p[8] << 8); // 当len为9时，执行此操作。
                ZBX_FALLTHROUGH;
        case 8:		b = b + ((zbx_hash_t)p[7] << 24); // 当len为8时，执行此操作。
                ZBX_FALLTHROUGH;
        case 7:		b = b + ((zbx_hash_t)p[6] << 16); // 当len为7时，执行此操作。
                ZBX_FALLTHROUGH;
        case 6:		b = b + ((zbx_hash_t)p[5] << 8); // 当len为6时，执行此操作。
                ZBX_FALLTHROUGH;
        case 5:		b = b + p[4]; // 当len为5时，执行此操作。
                ZBX_FALLTHROUGH;
        case 4:		a = a + ((zbx_hash_t)p[3] << 24); // 当len为4时，执行此操作。
                ZBX_FALLTHROUGH;
        case 3:		a = a + ((zbx_hash_t)p[2] << 16); // 当len为3时，执行此操作。
                ZBX_FALLTHROUGH;
        case 2:		a = a + ((zbx_hash_t)p[1] << 8); // 当len为2时，执行此操作。
                ZBX_FALLTHROUGH;
        case 1:		a = a + p[0]; // 当len为1时，执行此操作。
    }

    mix(a, b, c); // 对a、b、c三个变量进行混合运算，具体运算未知。

    return c; // 返回计算得到的哈希值c。
}

/*
 * modified FNV hash function (see http://www.isthe.com/chongo/tech/comp/fnv/)
 */
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个数据块的哈希值。它接收三个参数：一个指向数据块的指针 `data`、数据块的长度 `len` 和一个初始哈希值 `seed`。函数首先初始化一个名为 `hash` 的变量，然后使用 while 循环遍历数据块，计算每个元素与初始哈希值的异或结果，并乘以一个常数 16777619u。在循环结束后，对哈希值进行一系列位运算以增强随机性，最后返回计算得到的哈希值。
 ******************************************************************************/
// 定义一个名为 zbx_hash_modfnv 的函数，该函数用于计算数据块的哈希值
zbx_hash_t zbx_hash_modfnv(const void *data, size_t len, zbx_hash_t seed)
{
	// 定义一个指向数据块的指针 p，将其类型转换为 const uchar*，以便后续操作
	const uchar	*p = (const uchar *)data;

	// 定义一个名为 hash 的 zbx_hash_t 类型变量，用于存储计算结果
	zbx_hash_t	hash;

	// 初始化 hash 值为 2166136261u 减去 seed
	hash = 2166136261u ^ seed;

	// 使用 while 循环遍历数据块，直到 len 减为 0
	while (len-- >= 1)
	{
		// 使用 xor 运算符计算 hash 与数据块中当前元素的异或结果，然后乘以 16777619u
		hash = (hash ^ *(p++)) * 16777619u;
	}

	// 对 hash 值进行一系列位运算，以增强哈希值的随机性
	hash += hash << 13;
	hash ^= hash >> 7;
	hash += hash << 3;
	hash ^= hash >> 17;
	hash += hash << 5;

	// 返回计算得到的哈希值
	return hash;
}


/*
 * Murmur (see http://sites.google.com/site/murmurhash/)
 */
/******************************************************************************
 * *
 *整个代码块的主要目的是计算输入数据（最大长度为4字节）的指纹（hash值）。该函数接收三个参数：输入数据的首地址、输入数据的长度以及一个初始值（seed）。在计算过程中，首先根据输入数据长度和初始值计算出一个基础的 hash 值，然后根据输入数据的长度分别处理剩余的3、2、1个字节数据。最后，对 hash 值进行多次异或和乘法操作，得到最终的 hash 值并返回。这个 hash 值可以用于数据加密、解密、校验等场景。
 ******************************************************************************/
// 定义一个名为 zbx_hash_murmur2 的函数，该函数用于计算输入数据的数据指纹（hash值）
zbx_hash_t zbx_hash_murmur2(const void *data, size_t len, zbx_hash_t seed)
{
    // 定义一个指针 p，指向输入数据的首地址
    const uchar *p = (const uchar *)data;

    // 定义一个 hash 变量，用于存储计算结果
    zbx_hash_t hash;

    // 定义两个常量 m 和 r，用于计算 hash 值
    const zbx_hash_t m = 0x5bd1e995u;
    const zbx_hash_t r = 24;

    // 计算 hash 值的初始值，使用 seed 值异或 len 值
    hash = seed ^ (zbx_hash_t)len;

    // 当输入数据长度大于等于 4 时，执行以下循环
    while (len >= 4)
    {
        // 定义一个变量 k，用于存储当前处理的 4 个字节的数据
        zbx_hash_t k;

        // 将 p 指向的 4 个字节数据转换为无符号整数 k
        k = p[0];
        k |= p[1] << 8;
        k |= p[2] << 16;
        k |= p[3] << 24;

        // 计算 k 与 m 的乘积，再异或 k 右移 r 位的值
        k *= m;
        k ^= k >> r;
        k *= m;

        // 将计算得到的 k 值与 hash 值异或
        hash *= m;
        hash ^= k;

        // 移动指针 p，减小输入数据长度
        p += 4;
        len -= 4;
    }

    // 根据输入数据长度，分别处理剩余的 3、2、1 个字节数据
	switch (len)
	{
			// 当输入数据长度为 3 时，计算 hash 值异或 p[2] 右移 16 位的值
		case 3:	hash ^= p[2] << 16;
			ZBX_FALLTHROUGH;
			// 当输入数据长度为 2 时，计算 hash 值异或 p[1] 右移 8 位的值
		case 2: hash ^= p[1] << 8;
			ZBX_FALLTHROUGH;
			// 当输入数据长度为 1 时，计算 hash 值异或 p[0]
		case 1: hash ^= p[0];
			hash *= m;
	}

    // 计算 hash 值异或 hash 右移 13 位的值
    hash ^= hash >> 13;

    // 计算 hash 值乘以 m 的值
    hash *= m;

    // 计算 hash 值异或 hash 右移 15 位的值
    hash ^= hash >> 15;

    // 返回计算得到的 hash 值
    return hash;
}


/*
 * sdbm (see http://www.cse.yorku.ca/~oz/hash.html)
 */
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个给定数据块的哈希值。该函数接收一个指向数据的指针、数据长度和一个初始化种子，然后使用位运算和switch语句逐字节处理数据，最后返回计算得到的哈希值。在代码中，使用了两种算法实现哈希计算，一种是简单的循环，另一种是Duff's device算法，用于提高哈希计算的性能。
 ******************************************************************************/
zbx_hash_t zbx_hash_sdbm(const void *data, size_t len, zbx_hash_t seed)
{
    // 定义一个名为 zbx_hash_sdbm 的函数，接收三个参数：一个指向数据的指针 data，数据长度 len，以及一个初始化种子 seed

	const uchar	*p = (const uchar *)data; // 将数据指针 p 指向 data 所指的uchar类型数据

	zbx_hash_t	hash = seed; // 初始化哈希值为 seed

#if	1

	while (len-- >= 1) // 当数据长度 len 大于等于1时，进行循环
	{
		/* hash = *(p++) + hash * 65599; */

		hash = *(p++) + (hash << 6) + (hash << 16) - hash; // 更新哈希值，这里使用了位运算
	}

#else	/* Duff's device */

#define	HASH_STEP	len--;							\
			hash = *(p++) + (hash << 6) + (hash << 16) - hash

	switch (len & 7) // 根据数据长度 len 调整哈希计算步长
	{
			do
			{
				HASH_STEP;
		case 7:		HASH_STEP;
		case 6:		HASH_STEP;
		case 5:		HASH_STEP;
		case 4:		HASH_STEP;
		case 3:		HASH_STEP;
		case 2:		HASH_STEP;
		case 1:		HASH_STEP;
		case 0:		;
			}
			while (len >= 8);
	}

#endif

	return hash; // 返回计算得到的哈希值
}


/*
 * djb2 (see http://www.cse.yorku.ca/~oz/hash.html)
 */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_hash_djb2 的函数，该函数用于计算给定数据（以 void * 类型指针传递）的 DJB2 哈希值。计算过程中，哈希值初始化为种子值（zbx_hash_t 类型）的异或结果，然后在循环中对每个数据字节进行处理，最后返回计算得到的哈希值。
 ******************************************************************************/
zbx_hash_t	zbx_hash_djb2(const void *data, size_t len, zbx_hash_t seed)
{
	// 定义一个名为 zbx_hash_djb2 的函数，接收三个参数：
	// 1. 一个指向数据的指针 data
	// 2. 数据长度 len
	// 3. 一个种子值 seed

	const uchar	*p = (const uchar *)data; // 将数据指针转换为uchar类型

	zbx_hash_t	hash; // 定义一个名为 hash 的变量，用于存储计算结果

	hash = 5381u ^ seed; // 初始化 hash 值为种子值 seed 的异或结果，并赋值给 hash

	while (len-- >= 1) // 当数据长度 len 大于等于1时，执行以下操作：
	{
		/* hash = hash * 33 + *(p++); */

		hash = ((hash << 5) + hash) + *(p++); // 使用 DJB2 算法计算 hash 值，每次循环增加一个数据字节
	}

	return hash; // 返回计算得到的 hash 值
}


/* default hash functions */
/******************************************************************************
 * c
 *zbx_hash_t\tzbx_default_ptr_hash_func(const void *data)
 *{
 *    // 返回 ZBX_DEFAULT_PTR_HASH_ALGO 函数的结果，参数分别为 data、ZBX_PTR_SIZE 和 ZBX_DEFAULT_HASH_SEED
 *    return ZBX_DEFAULT_PTR_HASH_ALGO(data, ZBX_PTR_SIZE, ZBX_DEFAULT_HASH_SEED);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 zbx_default_ptr_hash_func 的函数，参数为 const void *data
zbx_hash_t zbx_default_ptr_hash_func(const void *data)
{
    // 返回 ZBX_DEFAULT_PTR_HASH_ALGO 函数的结果，参数分别为 data、ZBX_PTR_SIZE 和 ZBX_DEFAULT_HASH_SEED
    return ZBX_DEFAULT_PTR_HASH_ALGO(data, ZBX_PTR_SIZE, ZBX_DEFAULT_HASH_SEED);
}

//注释详细解释：
//1. 定义一个名为 zbx_default_ptr_hash_func 的函数，该函数接受一个 const void *data 类型的参数。这里的 const void *data 表示一个常量指针，它指向的数据类型未知，但我们可以确定它是一个指针。
//2. 在函数内部，调用 ZBX_DEFAULT_PTR_HASH_ALGO 函数，并将结果返回。这个 ZBX_DEFAULT_PTR_HASH_ALGO 函数的作用是根据给定的数据（data 参数）生成一个哈希值。
//3. 在调用 ZBX_DEFAULT_PTR_HASH_ALGO 函数时，传入三个参数：data、ZBX_PTR_SIZE 和 ZBX_DEFAULT_HASH_SEED。这些参数分别是：
//   - data：指向待哈希数据的指针。
//   - ZBX_PTR_SIZE：可能是指针数据类型的长度，用于计算哈希值时的大小调整。
//   - ZBX_DEFAULT_HASH_SEED：用于初始化哈希算法的种子值。
//4. 函数最后返回 ZBX_DEFAULT_PTR_HASH_ALGO 函数计算得到的哈希值。整个函数的主要目的是根据给定的数据生成一个哈希值，这个哈希值可以用于数据唯一性检查或其他相关操作。



/******************************************************************************
 * c
 *zbx_hash_t zbx_default_uint64_hash_func(const void *data)
 *{
 *\treturn ZBX_DEFAULT_UINT64_HASH_ALGO(data, sizeof(zbx_uint64_t), ZBX_DEFAULT_HASH_SEED);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 zbx_default_uint64_hash_func 的函数，返回类型为 zbx_hash_t
zbx_hash_t	zbx_default_uint64_hash_func(const void *data)// 接收一个 const void * 类型的参数 data，这个参数通常是指向一个内存地址
{
	return ZBX_DEFAULT_UINT64_HASH_ALGO(data, sizeof(zbx_uint64_t), ZBX_DEFAULT_HASH_SEED); // 调用 ZBX_DEFAULT_UINT64_HASH_ALGO 函数，并传入三个参数：
		// 1. data：指向待处理数据的内存地址
		// 2. sizeof(zbx_uint64_t)：表示 zbx_uint64_t 类型的数据占用内存大小
		// 3. ZBX_DEFAULT_HASH_SEED：用于初始化哈希算法的种子值
}
// 整个代码块的主要目的是定义一个名为 zbx_default_uint64_hash_func 的函数，该函数接收一个 const void * 类型的参数 data，并通过 ZBX_DEFAULT_UINT64_HASH_ALGO 函数计算数据的哈希值，最后返回一个 zbx_hash_t 类型的结果。这个哈希函数主要用于对数据进行加密和验证等操作。



/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 zbx_default_string_hash_func 的函数，该函数用于计算传入字符串的哈希值。函数接收一个 const void *类型的指针作为参数，将其转换为 const char * 类型，然后计算字符串长度，并调用 ZBX_DEFAULT_STRING_HASH_ALGO 函数计算哈希值。如果传入的指针为空，则直接返回一个默认的哈希值。
 ******************************************************************************/
// 定义一个名为 zbx_default_string_hash_func 的函数，该函数接收一个 const void *类型的指针作为参数
zbx_hash_t zbx_default_string_hash_func(const void *data)
{
    // 定义一个常量字符串，用于计算字符串长度
    const char *default_string = "ZBX_DEFAULT_STRING";

    // 将传入的 void * 类型指针转换为 const char * 类型，以便后续操作
    const char *str = (const char *)data;

    // 判断传入的指针是否为空，如果为空，直接返回一个默认的哈希值
    if (str == NULL)
    {
        return ZBX_DEFAULT_HASH_SEED;
    }

    // 计算字符串长度
    size_t len = strlen(str);

    // 调用 ZBX_DEFAULT_STRING_HASH_ALGO 函数，传入计算好的字符串长度和哈希种子，以计算字符串的哈希值
    return ZBX_DEFAULT_STRING_HASH_ALGO(str, len, ZBX_DEFAULT_HASH_SEED);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 zbx_default_uint64_pair_hash_func 的函数，该函数接收一个 const void * 类型的参数，计算传入的数据结构（猜测为 zbx_uint64_pair_t）中两个 uint64 类型成员的哈希值，并将结果返回。在这个过程中，首先将传入的数据转换为 zbx_uint64_pair_t 类型的指针，然后分别计算两个 uint64 成员的哈希值，最后将两个哈希值合并后返回。
 ******************************************************************************/
zbx_hash_t	zbx_default_uint64_pair_hash_func(const void *data) // 定义一个名为 zbx_default_uint64_pair_hash_func 的函数，接收一个 const void * 类型的参数，返回一个 zbx_hash_t 类型的值。

{
	const zbx_uint64_pair_t	*pair = (const zbx_uint64_pair_t *)data; // 将传入的参数 data 转换为 const zbx_uint64_pair_t 类型的指针，并赋值给 pair 变量。

	zbx_hash_t		hash; // 定义一个名为 hash 的 zbx_hash_t 类型的变量，用于存储计算得到的哈希值。

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&pair->first); // 调用 ZBX_DEFAULT_UINT64_HASH_FUNC 函数，计算 pair 结构体中第一个成员（uint64 类型）的哈希值，并将结果赋值给 hash。
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&pair->second, sizeof(pair->second), hash); // 调用 ZBX_DEFAULT_UINT64_HASH_ALGO 函数，计算 pair 结构体中第二个成员（uint64 类型）的哈希值，并将结果与之前的 hash 值进行合并。

	return hash; // 返回计算得到的哈希值。
}


/* default comparison functions */
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个传入的整数（通过指针 d1 和 d2 传递），如果两个整数相等，则返回 0，表示比较结果为 True；如果不相等，也返回 0，表示比较结果为 False。整个代码块实现了一个简单的整数比较函数。
 ******************************************************************************/
// 定义一个名为 zbx_default_int_compare_func 的函数，该函数用于比较两个整数
int	zbx_default_int_compare_func(const void *d1, const void *d2)
{
	// 将传入的参数 d1 和 d2 分别转换为指向 int 类型数据的指针 i1 和 i2
	const int	*i1 = (const int *)d1;
	const int	*i2 = (const int *)d2;

	// 判断 *i1 和 *i2 是否相等，如果相等，则不执行后续操作，返回 0
	ZBX_RETURN_IF_NOT_EQUAL(*i1, *i2);

	// 如果 *i1 和 *i2 不相等，返回 0，表示比较结果为 False
	return 0;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个 uint64 类型的数据，如果它们不相等，则返回 0，表示比较结果为 False。在函数内部，首先将传入的参数 d1 和 d2 分别转换为指向 uint64 类型数据的指针 i1 和 i2。然后判断 *i1 和 *i2 是否相等，如果相等，则不执行后续操作，返回 0。如果不相等，返回 0，表示比较结果为 False。
 ******************************************************************************/
// 定义一个名为 zbx_default_uint64_compare_func 的函数，该函数用于比较两个 uint64 类型的数据
int	zbx_default_uint64_compare_func(const void *d1, const void *d2)
{
	// 将传入的参数 d1 和 d2 分别转换为指向 uint64 类型数据的指针 i1 和 i2
	const zbx_uint64_t	*i1 = (const zbx_uint64_t *)d1;
	const zbx_uint64_t	*i2 = (const zbx_uint64_t *)d2;

	// 判断 *i1 和 *i2 是否相等，如果相等，则不执行后续操作，返回 0
	ZBX_RETURN_IF_NOT_EQUAL(*i1, *i2);

	// 如果 *i1 和 *i2 不相等，返回 0，表示比较结果为 False
	return 0;
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个函数`zbx_default_uint64_ptr_compare_func`，用于比较两个指向zbx_uint64_t类型数据的指针所指向的数据的大小。函数接收两个参数，均为指向zbx_uint64_t类型数据的指针。首先将这两个指针转换为zbx_uint64_t类型的数据，然后调用名为`zbx_default_uint64_compare_func`的函数进行比较。比较结果为：若第一个指针指向的数据大于第二个指针指向的数据，返回大于0的整数；若第一个指针指向的数据小于第二个指针指向的数据，返回小于0的整数；若两个指针指向的数据相等，返回0。
 ******************************************************************************/
// 定义一个C函数，名为zbx_default_uint64_ptr_compare_func，接收两个参数，均为指向zbx_uint64_t类型数据的指针
int zbx_default_uint64_ptr_compare_func(const void *d1, const void *d2)
{
	// 将第一个参数d1转换为指向zbx_uint64_t类型数据的指针，存储在p1变量中
	const zbx_uint64_t	*p1 = *(const zbx_uint64_t **)d1;
	// 将第二个参数d2转换为指向zbx_uint64_t类型数据的指针，存储在p2变量中
	const zbx_uint64_t	*p2 = *(const zbx_uint64_t **)d2;

	// 调用名为zbx_default_uint64_compare_func的函数，比较p1和p2指向的zbx_uint64_t数据的大小
	// 返回值：若p1大于p2，返回大于0的整数；若p1小于p2，返回小于0的整数；若p1等于p2，返回0
	return zbx_default_uint64_compare_func(p1, p2);
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个用于比较两个字符串（字符数组）的函数 zbx_default_str_compare_func。该函数接收两个 void 类型的指针作为参数，通过解引用指针得到两个 const char 类型的指针，然后使用 strcmp 函数比较这两个字符串的长度和字符顺序。返回值表示两个字符串的大小关系，若第一个字符串小于第二个字符串，返回负数；若第一个字符串大于第二个字符串，返回正数；若两个字符串相等，返回 0。
 ******************************************************************************/
// 定义一个名为 zbx_default_str_compare_func 的函数，参数为两个 void 类型的指针
int zbx_default_str_compare_func(const void *d1, const void *d2)
{
    // 将指针 d1 和 d2 解引用，得到两个 const char 类型的指针
    const char *p1 = *(const char **)d1;
    const char *p2 = *(const char **)d2;

    // 使用 strcmp 函数比较两个字符串（字符数组）p1 和 p2 的长度和字符顺序
    // 返回值：若 p1 < p2，返回负数；若 p1 > p2，返回正数；若 p1 = p2，返回 0
    return strcmp(p1, p2);
}

/******************************************************************************
 * *
 *这块代码的主要目的是比较两个 const void 类型的指针（d1 和 d2）是否相等。如果相等，则返回 0，表示比较成功；如果不相等，则返回 1，表示比较失败。在这个过程中，代码将指针转换为指针类型，以便进行比较。
 ******************************************************************************/
// 定义一个名为 zbx_default_ptr_compare_func 的 C 语言函数，该函数接受两个参数，都是 const void 类型的指针。
int	zbx_default_ptr_compare_func(const void *d1, const void *d2)
{
	// 将 d1 指向的内存地址转换为指针类型，存储在变量 p1 中
	const void	*p1 = *(const void **)d1;
	// 将 d2 指向的内存地址转换为指针类型，存储在变量 p2 中
	const void	*p2 = *(const void **)d2;

	// 判断 p1 和 p2 是否相等，如果不相等，则返回 1，表示比较失败
	ZBX_RETURN_IF_NOT_EQUAL(p1, p2);

	// 如果 p1 和 p2 相等，则返回 0，表示比较成功
	return 0;
}

/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_uint64_pair结构体是否相等。函数接收两个void类型的指针作为输入参数，分别为d1和d2。通过指针类型转换，将输入的void类型指针转换为zbx_uint64_pair_t类型的指针，方便后续操作。接下来，分别比较两个zbx_uint64_pair结构体中的first和second成员是否相等。如果两个结构体完全相等，则返回0；否则，返回一个错误码。
 ******************************************************************************/
// 定义一个C函数，名为zbx_default_uint64_pair_compare_func，接收两个参数，均为void类型的指针
int zbx_default_uint64_pair_compare_func(const void *d1, const void *d2)
{
	// 将指针d1和d2分别转换为指向zbx_uint64_pair_t类型的指针，方便后续操作
	const zbx_uint64_pair_t *p1 = (const zbx_uint64_pair_t *)d1;
	const zbx_uint64_pair_t *p2 = (const zbx_uint64_pair_t *)d2;

	// 判断两个指针指向的zbx_uint64_pair结构体中的first成员是否相等，若不相等，则返回错误码
	ZBX_RETURN_IF_NOT_EQUAL(p1->first, p2->first);

	// 判断两个指针指向的zbx_uint64_pair结构体中的second成员是否相等，若不相等，则返回错误码
	ZBX_RETURN_IF_NOT_EQUAL(p1->second, p2->second);

	// 如果上述判断条件均满足，则返回0，表示两个zbx_uint64_pair结构体完全相等
	return 0;
}


/* default memory management functions */
// 定义一个名为 zbx_default_mem_malloc_func 的函数，它是 void 类型的指针，有两个参数：old 和 size。
 *void *zbx_default_mem_malloc_func(void *old, size_t size)
 *{
 *    // 调用 zbx_malloc 函数，传入 old 和 size 作为参数，用于分配内存。
 *    // zbx_malloc 函数的返回值是一个 void 类型的指针，这里将其转换为 void * 类型。
 *    return zbx_malloc(old, size);
 *}
 *
 ******************************************************************************/
// 定义一个名为 zbx_default_mem_malloc_func 的函数，它是 void 类型的指针，有两个参数：old 和 size。
void *zbx_default_mem_malloc_func(void *old, size_t size)
{
    // 调用 zbx_malloc 函数，传入 old 和 size 作为参数，用于分配内存。
    // zbx_malloc 函数的返回值是一个 void 类型的指针，这里将其转换为 void * 类型。
    return zbx_malloc(old, size);
}

// 整个代码块的主要目的是：分配内存空间。这段代码定义了一个函数 zbx_default_mem_malloc_func，它接受一个旧指针 old 和一个大小 size 作为参数，然后调用 zbx_malloc 函数来分配内存。分配成功后，返回一个指向新分配内存的指针。
// 输出注释后的代码块：


/******************************************************************************
 * *
 *这块代码的主要目的是定义一个函数，用于重新分配内存。当需要调整内存大小时，可以使用这个函数来实现。在此示例中，函数接受一个旧内存地址和一个预期的大小，然后调用另一个名为 zbx_realloc 的函数来完成内存重新分配。如果重新分配成功，函数返回新的内存地址；如果失败，返回 NULL。
 ******************************************************************************/
// 定义一个名为 zbx_default_mem_realloc_func 的函数，该函数接受两个参数：一个旧内存地址（old）和一个预期的大小（size）。
void *zbx_default_mem_realloc_func(void *old, size_t size)
{
    // 调用 zbx_realloc 函数，该函数用于重新分配内存。
    // 参数1：旧内存地址（old）
    // 参数2：预期的大小（size）
    return zbx_realloc(old, size);
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个函数 zbx_default_mem_free_func，用于释放内存。当调用这个函数时，它会将传入的 void* 类型指针 ptr 指向的内存空间释放。这里的注释详细解释了每个步骤，包括函数的声明、参数以及函数内部的核心操作。
 ******************************************************************************/
// 定义一个名为 zbx_default_mem_free_func 的函数，该函数为 void 类型（无返回值），接收一个 void* 类型的参数 ptr。
void	zbx_default_mem_free_func(void *ptr)
{
	// 使用 zbx_free 函数释放内存，参数为指向要释放内存的指针 ptr。
	zbx_free(ptr);
}



/* numeric functions */
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个整数n是否为质数。通过判断n是否小于等于1、等于2或者能被2整除，以及使用循环检查n是否能被大于1的整数整除。如果经过以上判断，n仍然不能被任何大于1的整数整除，那么n就是质数，函数返回1；否则，返回0。
 ******************************************************************************/
// 定义一个函数is_prime，参数为一个整数n
int is_prime(int n)
{
	// 定义一个整数变量i，用于循环使用
	int i;

	// 如果n小于等于1，说明n不是质数，返回0
	if (n <= 1)
		return 0;

	// 如果n等于2，说明n是质数，返回1
	if (n == 2)
		return 1;

	// 如果n能被2整除，说明n不是质数，返回0
	if (n % 2 == 0)
		return 0;

	// 针对n大于2的情况，使用for循环从3开始，每次步进为2，检查n是否能被i整除
	for (i = 3; i * i <= n; i += 2)
		// 如果n能被i整除，说明n不是质数，返回0
		if (n % i == 0)
			return 0;

	// 经过以上循环，如果n不能被任何大于1的整数整除，说明n是质数，返回1
	return 1;
}

/******************************************************************************
 * *
 *这个代码块的主要目的是求解下一个质数。函数 next_prime 接收一个整数参数 n，并判断 n 是否为质数。如果 n 不是质数，则继续递增 n，直到找到下一个质数为止。找到质数后，返回该质数。
 ******************************************************************************/
// 定义一个函数 next_prime(int n)，用于求解下一个质数
int next_prime(int n)
{
    // 判断 n 是否为质数，若不是，则继续递增 n
    while (!is_prime(n))
        n++;

    // 返回下一个质数
    return n;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_isqrt32                                                      *
 *                                                                            *
 * Purpose: calculate integer part of square root of a 32 bit integer value   *
 *                                                                            *
 * Parameters: value     - [IN] the value to calculate square root for        *
 *                                                                            *
 * Return value: the integer part of square root                              *
 *                                                                            *
 * Comments: Uses basic digit by digit square root calculation algorithm with *
 *           binary base.                                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * ````
 *
 *这块代码的主要目的是计算一个无符号整数`value`的平方根，返回结果。这里采用牛顿迭代法，通过循环16次迭代，逐步逼近平方根值。最终返回计算得到的平方根值。
 ******************************************************************************/
unsigned int zbx_isqrt32(unsigned int value)
{
	// 定义变量，计算平方根
	unsigned int i, remainder = 0, result = 0, p;

	// 循环16次，利用牛顿迭代法计算平方根
	for (i = 0; i < 16; i++)
	{
		// 将结果左移一位，相当于除以2
		result <<= 1;

		// 计算余数，并将值右移30位
		remainder = (remainder << 2) + (value >> 30);

		// 将值左移2位，相当于除以4
		value <<= 2;

		// 计算下一次迭代的中间值
		p = (result << 1) | 1;

		// 如果中间值小于等于余数，说明这次迭代成功
		if (p <= remainder)
		{
			// 减去中间值
			remainder -= p;

			// 标志位加1，表示这次迭代成功
			result |= 1;
		}
	}

	// 返回计算得到的平方根值
	return result;
}

