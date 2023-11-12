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

#include "zbxalgo.h"

#define DB_INFINITY	(1e12 - 1e-4)

#define ZBX_MATH_EPSILON	(1e-6)

#define ZBX_IS_NAN(x)	((x) != (x))

#define ZBX_VALID_MATRIX(m)		(0 < (m)->rows && 0 < (m)->columns && NULL != (m)->elements)
#define ZBX_MATRIX_EL(m, row, col)	((m)->elements[(row) * (m)->columns + (col)])
#define ZBX_MATRIX_ROW(m, row)		((m)->elements + (row) * (m)->columns)

typedef struct
{
	int	rows;
	int	columns;
	double	*elements;
}
zbx_matrix_t;

/******************************************************************************
 * *
 *这块代码的主要目的是：分配一个zbx矩阵结构体的内存空间，并初始化其属性。
 *
 *代码解释：
 *
 *1. 定义一个名为`zbx_matrix_struct_alloc`的静态函数，该函数接受一个指向`zbx_matrix_t`类型的指针作为参数。
 *2. 使用`zbx_malloc`函数为指针`pm`指向的内存地址分配大小为`sizeof(zbx_matrix_t)`的内存空间。
 *3. 将分配得到的内存地址赋值给指针`pm`。
 *4. 初始化分配得到的矩阵结构体的属性：
 *   - 矩阵的行数（`rows`）设为0；
 *   - 矩阵的列数（`columns`）设为0；
 *   - 矩阵的元素指针（`elements`）设为NULL。
 *
 *通过这段代码，我们可以得到一个初始化的zbx矩阵结构体，后续可以对其进行赋值和操作。
 ******************************************************************************/
// 定义一个静态函数，用于分配一个zbx矩阵结构体的内存空间
static void zbx_matrix_struct_alloc(zbx_matrix_t **pm)
{
    // 给指针pm指向的内存地址分配大小为sizeof(zbx_matrix_t)的内存空间
    *pm = (zbx_matrix_t *)zbx_malloc(*pm, sizeof(zbx_matrix_t));

    // 初始化分配得到的矩阵结构体的属性
    (*pm)->rows = 0;
    (*pm)->columns = 0;
    (*pm)->elements = NULL;
}


/******************************************************************************
 * *
 *这块代码的主要目的是分配一个二维矩阵的空间，并根据用户输入的行数和列数设置矩阵的尺寸。如果行数或列数为0，或者内存分配失败，函数会打印错误信息并返回分配失败的状态码。如果内存分配成功，函数返回分配成功的状态码。
 ******************************************************************************/
// 定义一个函数zbx_matrix_alloc，用于分配一个二维矩阵的空间
static int	zbx_matrix_alloc(zbx_matrix_t *m, int rows, int columns)
{
	// 检查输入的行数和列数是否为非负数
	if (0 >= rows || 0 >= columns)
		// 如果行数或列数为0，跳转到error标签处
		goto error;

	// 设置矩阵的行数和列数
	m->rows = rows;
	m->columns = columns;

	// 为矩阵分配内存空间，存储double类型的元素
	m->elements = (double *)zbx_malloc(m->elements, sizeof(double) * rows * columns);

	// 如果内存分配成功，返回SUCCEED
	return SUCCEED;

error:
	// 如果行数或列数为0，表示分配内存失败，打印错误信息
	THIS_SHOULD_NEVER_HAPPEN;
	// 返回分配失败的状态码FAIL
	return FAIL;
}


/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx矩阵结构体及其元素的内存。当调用这个函数时，它会首先检查传入的zbx矩阵指针m是否为空，如果不为空，则依次释放m指向的矩阵元素内存和矩阵结构体内存。这样可以确保在使用完矩阵后，正确地释放相关内存，避免内存泄漏。
 ******************************************************************************/
// 定义一个函数，用于释放zbx矩阵结构体内存
static void zbx_matrix_free(zbx_matrix_t *m)
{
    // 判断指针m是否为空，如果不为空，则进行以下操作
    if (NULL != m)
    {
        // 释放m指向的zbx矩阵的元素内存
        zbx_free(m->elements);

        // 释放m指向的zbx矩阵结构体内存
        zbx_free(m);
    }
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个矩阵复制功能，从一个矩阵复制数据到另一个矩阵。具体步骤如下：
 *
 *1. 检查源矩阵是否有效，如果不有效，跳转到错误处理。
 *2. 为目标矩阵分配内存，分配的内存大小根据源矩阵的行数和列数计算。
 *3. 将源矩阵的元素复制到目标矩阵。
 *4. 如果复制成功，返回成功；如果出现错误，返回失败。
 ******************************************************************************/
// 定义一个静态函数zbx_matrix_copy，用于复制一个矩阵到另一个矩阵
static int	zbx_matrix_copy(zbx_matrix_t *dest, zbx_matrix_t *src)
{
	// 检查源矩阵是否有效
	if (!ZBX_VALID_MATRIX(src)) {
		// 如果矩阵无效，跳转到错误处理
		goto error;
	}

	// 为目标矩阵分配内存，参数分别为行数和列数
	if (SUCCEED != zbx_matrix_alloc(dest，src->rows，src->columns)) {
		// 如果内存分配失败，返回失败
		return FAIL;
	}

	// 将源矩阵的元素复制到目标矩阵
	memcpy(dest->elements, src->elements, sizeof(double) * src->rows * src->columns);

	// 复制成功，返回成功
	return SUCCEED;

error:
	// 这里不应该发生，表示错误处理
	THIS_SHOULD_NEVER_HAPPEN;
	// 返回失败
	return FAIL;
}


/******************************************************************************
 * *
 *这段代码的主要目的是创建一个 n 阶单位矩阵，并将它存储在 zbx_matrix_t 结构体指针 m 所指向的内存空间中。具体步骤如下：
 *
 *1. 检查分配矩阵空间是否成功，如果不成功，返回 FAIL。
 *2. 使用两个嵌套循环遍历矩阵的所有元素。
 *3. 根据循环变量 i 和 j 的值，设置矩阵元素 ZBX_MATRIX_EL(m, i, j) 的值：如果 i 等于 j，则值为 1.0，否则为 0.0。
 *4. 如果一切顺利，返回 SUCCEED，表示创建单位矩阵成功。
 ******************************************************************************/
// 定义一个名为 zbx_identity_matrix 的静态函数，接收两个参数：一个指向 zbx_matrix_t 结构体的指针 m，以及一个整数 n
static int	zbx_identity_matrix(zbx_matrix_t *m, int n)
{
	// 定义两个循环变量 i 和 j，分别用于遍历矩阵的行和列
	int	i, j;

	// 检查分配矩阵的空间是否成功，如果不成功，返回 FAIL
	if (SUCCEED != zbx_matrix_alloc(m, n, n))
		return FAIL;

	// 使用两个嵌套循环，遍历矩阵的所有元素
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
		{
			// 设置矩阵元素 ZBX_MATRIX_EL(m, i, j) 的值为：如果 i 等于 j，则值为 1.0，否则为 0.0
			ZBX_MATRIX_EL(m, i, j) = (i == j ? 1.0 : 0.0);
		}

	// 函数执行成功，返回 SUCCEED
	return SUCCEED;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个矩阵转置函数zbx_transpose_matrix，输入两个矩阵指针m和r，将矩阵m的元素按照行列交换的方式复制到矩阵r中。如果矩阵m不有效，或者矩阵r的内存分配失败，函数返回FAIL。如果所有操作顺利完成，返回SUCCEED。
 ******************************************************************************/
static int	zbx_transpose_matrix(zbx_matrix_t *m, zbx_matrix_t *r)
{
	int	i, j;					// 定义两个整型变量i和j，用于循环计数

	if (!ZBX_VALID_MATRIX(m))		// 检查输入的矩阵m是否有效，如果不有效，跳转到error标签处
		goto error;

	if (SUCCEED != zbx_matrix_alloc(r, m->columns, m->rows))	// 分配矩阵r的内存空间，如果分配失败，返回FAIL
		return FAIL;

	for (i = 0; i < r->rows; i++)		// 遍历矩阵r的行
		for (j = 0; j < r->columns; j++)	// 遍历矩阵r的列
			ZBX_MATRIX_EL(r, i, j) = ZBX_MATRIX_EL(m, j, i);	// 将矩阵m中的元素按照行列交换的方式复制到矩阵r中

	return SUCCEED;					// 如果以上操作顺利完成，返回SUCCEED

error:
	THIS_SHOULD_NEVER_HAPPEN;			// 这里是一个错误标签，表示不应该发生这种情况
	return FAIL;
}


/******************************************************************************
 * *
 *这块代码的主要目的是交换矩阵的行。函数`zbx_matrix_swap_rows`接收一个矩阵指针`m`以及两个表示要交换的行号的整数`r1`和`r2`。通过遍历矩阵中的每个元素，并将行`r1`的元素值与行`r2`的对应元素值交换，最后输出交换后的矩阵。
 ******************************************************************************/
// 定义一个函数，用于交换矩阵的行
static void zbx_matrix_swap_rows(zbx_matrix_t *m, int r1, int r2)
{
    // 定义一个临时变量，用于存储行交换过程中的数据
    double tmp;
    // 定义一个循环变量，用于遍历矩阵的每个元素
    int i;

    // 遍历矩阵中的每个元素
    for (i = 0; i < m->columns; i++)
    {
        // 分别获取行r1和行r2对应的元素值
        tmp = ZBX_MATRIX_EL(m, r1, i);
        // 将行r1的元素值与行r2的对应元素值交换
        ZBX_MATRIX_EL(m, r1, i) = ZBX_MATRIX_EL(m, r2, i);
        // 将行r2的对应元素值与临时变量tmp的值交换
        ZBX_MATRIX_EL(m, r2, i) = tmp;
    }
}


/******************************************************************************
 * *
 *这块代码的主要目的是实现一个矩阵的行除以一个分母的操作。具体来说，这个函数接收一个矩阵指针 `m`、行号 `row` 和分母 `denominator` 作为输入参数，将矩阵中指定行的每个元素除以分母，并将结果存储回矩阵中。
 *
 *代码中使用了循环遍历矩阵的每一列，通过 `ZBX_MATRIX_EL()` 函数获取当前行的当前列的元素，然后将元素值除以分母，并将计算后的值存储回矩阵中。
 ******************************************************************************/
/**
 * @file zbx_matrix.c
 * @brief 本文件实现了矩阵相关的操作函数
 */

/* 定义矩阵除以行的函数 */
static void	zbx_matrix_divide_row_by(zbx_matrix_t *m, int row, double denominator)
{
	/* 定义一个整数变量 i，用于循环 */
	int	i;

	/* 遍历矩阵的每一列 */
	for (i = 0; i < m->columns; i++)
	{
		/* 获取矩阵中当前行的当前列的元素 */
		double temp = ZBX_MATRIX_EL(m, row, i);

		/* 将当前元素的值除以分母 */
		temp /= denominator;

		/* 将计算后的值存储回矩阵中 */
		ZBX_MATRIX_EL(m, row, i) = temp;
	}
}


/******************************************************************************
 * *
 *整个代码块的主要目的是对矩阵中指定行的元素进行加法操作，将源行中的每个元素乘以一个因子后加到目标行对应的元素上。具体实现如下：
 *
 *1. 定义一个静态函数zbx_matrix_add_rows_with_factor，接收四个参数：矩阵对象m（类型为zbx_matrix_t）、目标行索引dest、源行索引src和因子factor。
 *2. 定义一个整型变量i，用于循环遍历矩阵的列。
 *3. 使用for循环遍历矩阵的列，从0开始，直到遍历完所有列。
 *4. 获取目标行和源行在第i列的元素值，分别为ZBX_MATRIX_EL(m, dest, i)和ZBX_MATRIX_EL(m, src, i)。
 *5. 将目标行的第i列元素值加上源行第i列元素值乘以因子factor，即ZBX_MATRIX_EL(m, dest, i) += ZBX_MATRIX_EL(m, src, i) * factor。
 *
 *这样，就实现了对矩阵中指定行的元素进行加法操作的功能。
 ******************************************************************************/
// 定义一个静态函数zbx_matrix_add_rows_with_factor，接收四个参数：
// zbx_matrix_t类型的指针m，表示矩阵对象；
// 整型变量dest，表示目标行索引；
// 整型变量src，表示源行索引；
// 双精度浮点型变量factor，表示乘以的因子。
static void zbx_matrix_add_rows_with_factor(zbx_matrix_t *m, int dest, int src, double factor)
{
	// 定义一个整型变量i，用于循环遍历矩阵的列。
	int i;

	// 使用for循环，从0开始遍历矩阵的列，直到遍历完所有列。
	for (i = 0; i < m->columns; i++)
		// 获取目标行和源行在第i列的元素值，分别为ZBX_MATRIX_EL(m, dest, i)和ZBX_MATRIX_EL(m, src, i)。
		// 将目标行的第i列元素值加上源行第i列元素值乘以因子factor。
		ZBX_MATRIX_EL(m, dest, i) += ZBX_MATRIX_EL(m, src, i) * factor;
}


/******************************************************************************
 * *
 *这段代码的主要目的是计算一个给定矩阵的逆矩阵。代码采用高斯消元法求解逆矩阵，并在过程中使用部分 pivoting 策略。对于不同行数的矩阵，代码有不同的处理方法。当矩阵行数为一或两行时，直接计算逆矩阵；当矩阵行数大于两行时，采用高斯消元法求解逆矩阵。在计算过程中，如果发现矩阵奇异（主元为0），则输出相应的日志信息并返回失败。计算完成后，释放分配的内存。
 ******************************************************************************/
/* static int zbx_inverse_matrix(zbx_matrix_t *m, zbx_matrix_t *r) */
static int	zbx_inverse_matrix(zbx_matrix_t *m, zbx_matrix_t *r)
{
	/* 声明变量 */
	zbx_matrix_t	*l = NULL;
	double		pivot, factor, det;
	int		i, j, k, n, res;

	/* 检查矩阵是否有效且行数等于列数 */
	if (!ZBX_VALID_MATRIX(m) || m->rows != m->columns)
		goto error;

	/* 获取矩阵的行数 */
	n = m->rows;

	/* 当矩阵只有一行时，直接计算逆矩阵 */
	if (1 == n)
	{
		if (SUCCEED != zbx_matrix_alloc(r, 1, 1))
			return FAIL;

		/* 如果矩阵主元为0，说明矩阵奇异 */
		if (0.0 == ZBX_MATRIX_EL(m, 0, 0))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "matrix is singular");
			res = FAIL;
			goto out;
		}

		/* 计算逆矩阵的元素 */
		ZBX_MATRIX_EL(r, 0, 0) = 1.0 / ZBX_MATRIX_EL(m, 0, 0);
		return SUCCEED;
	}

	/* 当矩阵有两行时，直接计算逆矩阵 */
	if (2 == n)
	{
		if (SUCCEED != zbx_matrix_alloc(r, 2, 2))
			return FAIL;

		/* 计算行列式 */
		det = ZBX_MATRIX_EL(m, 0, 0) * ZBX_MATRIX_EL(m, 1, 1) -
			ZBX_MATRIX_EL(m, 0, 1) * ZBX_MATRIX_EL(m, 1, 0));

		/* 如果行列式为0，说明矩阵奇异 */
		if (0.0 == det)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "matrix is singular");
			res = FAIL;
			goto out;
		}

		/* 计算逆矩阵的元素 */
		ZBX_MATRIX_EL(r, 0, 0) = ZBX_MATRIX_EL(m, 1, 1) / det;
		ZBX_MATRIX_EL(r, 0, 1) = -ZBX_MATRIX_EL(m, 0, 1) / det;
		ZBX_MATRIX_EL(r, 1, 0) = -ZBX_MATRIX_EL(m, 1, 0) / det;
		ZBX_MATRIX_EL(r, 1, 1) = ZBX_MATRIX_EL(m, 0, 0) / det;
		return SUCCEED;
	}

	/* 当矩阵行数大于2时，采用高斯消元法求逆矩阵 */
	if (SUCCEED != zbx_identity_matrix(r, n))
		return FAIL;

	/* 分配内存存储矩阵 */
	zbx_matrix_struct_alloc(&l);

	/* 判断分配内存是否成功 */
	if (SUCCEED != (res = zbx_matrix_copy(l, m)))
		goto out;

	/* 高斯消元法，采用部分 pivoting 策略 */
	for (i = 0; i < n; i++)
	{
		k = i;
		pivot = ZBX_MATRIX_EL(l, i, i);

		/* 寻找最大元素作为主元 */
		for (j = i; j < n; j++)
		{
			if (fabs(ZBX_MATRIX_EL(l, j, i)) > fabs(pivot))
			{
				k = j;
				pivot = ZBX_MATRIX_EL(l, j, i);
			}
		}

		/* 如果主元为0，说明矩阵奇异 */
		if (0.0 == pivot)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "matrix is singular");
			res = FAIL;
			goto out;
		}

		/* 交换行 */
		if (k != i)
		{
			zbx_matrix_swap_rows(l, i, k);
			zbx_matrix_swap_rows(r, i, k);
		}

		/* 高斯消元 */
		for (j = i + 1; j < n; j++)
		{
			if (0.0 != (factor = -ZBX_MATRIX_EL(l, j, i) / ZBX_MATRIX_EL(l, i, i)))
			{
				zbx_matrix_add_rows_with_factor(l, j, i, factor);
				zbx_matrix_add_rows_with_factor(r, j, i, factor);
			}
		}
	}

	/* 高斯消元结束后的矩阵 */
	for (i = n - 1; i > 0; i--)
	{
		for (j = 0; j < i; j++)
		{
			if (0.0 != (factor = -ZBX_MATRIX_EL(l, j, i) / ZBX_MATRIX_EL(l, i, i)))
			{
				zbx_matrix_add_rows_with_factor(l, j, i, factor);
				zbx_matrix_add_rows_with_factor(r, j, i, factor);
			}
		}
	}

	/* 计算逆矩阵 */
	for (i = 0; i < n; i++)
		zbx_matrix_divide_row_by(r, i, ZBX_MATRIX_EL(l, i, i));

	res = SUCCEED;
out:
	/* 释放内存 */
	zbx_matrix_free(l);
	return res;
error:
	THIS_SHOULD_NEVER_HAPPEN;
	return FAIL;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现两个矩阵的乘法运算，并将结果存储在第三个矩阵中。矩阵乘法运算的过程中，首先检查输入的左矩阵和右矩阵是否有效，以及左矩阵的列数是否等于右矩阵的行数。如果条件满足，为结果矩阵分配内存空间，然后遍历结果矩阵的行和列，计算每个元素值。最后，将计算得到的元素值存储到结果矩阵中，并返回运算成功与否的标志。如果发生错误，进行相应的处理并返回失败标志。
 ******************************************************************************/
// 定义一个C函数，用于实现两个矩阵的乘法运算
static int	zbx_matrix_mult(zbx_matrix_t *left, zbx_matrix_t *right, zbx_matrix_t *result)
{
	// 定义一个双精度浮点数变量，用于存储矩阵元素
	double	element;
	// 定义三个整数变量，用于循环计数
	int	i, j, k;

	// 检查输入的左矩阵和右矩阵是否有效，以及左矩阵的列数是否等于右矩阵的行数
	if (!ZBX_VALID_MATRIX(left) || !ZBX_VALID_MATRIX(right) || left->columns != right->rows)
		// 如果条件不满足，跳转到error标签处
		goto error;

	// 为结果矩阵分配内存空间，存储左矩阵的行数和右矩阵的列数
	if (SUCCEED != zbx_matrix_alloc(result, left->rows, right->columns))
		// 如果内存分配失败，返回FAIL
		return FAIL;

	// 遍历结果矩阵的行和列
	for (i = 0; i < result->rows; i++)
	{
		for (j = 0; j < result->columns; j++)
		{
			// 初始化元素值为0
			element = 0;

			// 遍历左矩阵的列，计算结果矩阵的元素值
			for (k = 0; k < left->columns; k++)
				// 计算左矩阵和右矩阵对应元素的乘积，并累加到element中
				element += ZBX_MATRIX_EL(left, i, k) * ZBX_MATRIX_EL(right, k, j);

			// 将计算得到的元素值存储到结果矩阵中
			ZBX_MATRIX_EL(result, i, j) = element;
		}
	}

	// 矩阵乘法运算成功，返回SUCCEED
	return SUCCEED;

error:
	// 此处不应该发生，表示错误处理
	THIS_SHOULD_NEVER_HAPPEN;
	// 返回FAIL，表示矩阵乘法运算失败
	return FAIL;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算最小二乘法中的系数矩阵。为了减少操作次数和内存使用，改变了矩阵乘法的顺序。首先，将independent矩阵的行列式交换，然后计算independent和independent_transposed的乘积。接下来，对乘积矩阵进行求逆，并将结果与dependent矩阵相乘。最后，计算左乘积和右乘积的乘积，得到最终的系数矩阵。操作过程中，使用了一系列矩阵操作函数，如zbx_matrix_struct_alloc、zbx_transpose_matrix、zbx_matrix_mult和zbx_inverse_matrix。在操作完成后，释放分配的内存。
 ******************************************************************************/
/* 定义一个C语言函数zbx_least_squares，输入参数为三个矩阵：independent、dependent和coefficients。
 * 该函数的主要目的是计算最小二乘法中的系数矩阵。
 * 为了减少操作次数和内存使用，我们改变了矩阵乘法的顺序。
 */
static int	zbx_least_squares(zbx_matrix_t *independent, zbx_matrix_t *dependent, zbx_matrix_t *coefficients)
{
	/* 分配内存并初始化四个矩阵：independent_transposed、to_be_inverted、left_part和right_part */
	zbx_matrix_t	*independent_transposed = NULL, *to_be_inverted = NULL, *left_part = NULL, *right_part = NULL;
	int		res;

	zbx_matrix_struct_alloc(&independent_transposed);
	zbx_matrix_struct_alloc(&to_be_inverted);
	zbx_matrix_struct_alloc(&left_part);
	zbx_matrix_struct_alloc(&right_part);

	/* 交换independent矩阵的行列式，存储在independent_transposed矩阵中 */
	if (SUCCEED != (res = zbx_transpose_matrix(independent, independent_transposed)))
		goto out;

	/* 计算independent_transposed和independent的乘积，结果存储在to_be_inverted矩阵中 */
	if (SUCCEED != (res = zbx_matrix_mult(independent_transposed, independent, to_be_inverted)))
		goto out;

	/* 对to_be_inverted矩阵进行求逆，结果存储在left_part矩阵中 */
	if (SUCCEED != (res = zbx_inverse_matrix(to_be_inverted, left_part)))
		goto out;

	/* 计算independent_transposed和dependent的乘积，结果存储在right_part矩阵中 */
	if (SUCCEED != (res = zbx_matrix_mult(independent_transposed, dependent, right_part)))
		goto out;

	/* 计算left_part和right_part的乘积，结果存储在coefficients矩阵中 */
	if (SUCCEED != (res = zbx_matrix_mult(left_part, right_part, coefficients)))
		goto out;

out:
	/* 释放内存 */
	zbx_matrix_free(independent_transposed);
	zbx_matrix_free(to_be_inverted);
	zbx_matrix_free(left_part);
	zbx_matrix_free(right_part);
	
	/* 返回res，表示操作是否成功 */
	return res;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是根据输入的double类型数据x、数据数量n、拟合类型fit以及矩阵指针m，对数据进行处理并存储到矩阵m中。具体操作如下：
 *
 *1. 判断fit的值，如果是线性、多项式或对数拟合，分配内存空间给矩阵m，尺寸为n×1，并将数据存储到矩阵m的第一列。
 *2. 如果是指数或幂函数拟合，分配内存空间给矩阵m，尺寸为n×1，并对输入数据进行取对数操作，将结果存储到矩阵m的第一列。
 *3. 在整个过程中，如果遇到内存分配失败或输入数据包含负数或零值的情况，返回失败（FAIL）。
 *4. 执行完毕后，返回成功（SUCCEED）。
 ******************************************************************************/
// 定义一个静态函数zbx_fill_dependent，接收5个参数：
// 1. 指向double类型的指针x，用于存储输入数据
// 2. int类型的整数n，表示输入数据的数量
// 3. zbx_fit_t类型的变量fit，表示拟合类型
// 4. 指向zbx_matrix_t类型的指针m，用于存储计算结果
// 5. 返回值类型为int，表示操作结果
static int	zbx_fill_dependent(double *x, int n, zbx_fit_t fit, zbx_matrix_t *m)
{
	// 定义一个整数变量i，用于循环计数
	int	i;
	// 判断fit的值，如果是线性拟合（FIT_LINEAR），多项式拟合（FIT_POLYNOMIAL）或对数拟合（FIT_LOGARITHMIC）
	// 如果是这三种情况，继续执行后续代码
	if (FIT_LINEAR == fit || FIT_POLYNOMIAL == fit || FIT_LOGARITHMIC == fit)
	{
		// 分配内存空间给矩阵m，尺寸为n×1
		// 如果分配内存失败，返回失败（FAIL）
		if (SUCCEED != zbx_matrix_alloc(m, n, 1))
			return FAIL;

		// 遍历输入数据x，将数据存储到矩阵m的第一列
		for (i = 0; i < n; i++)
			ZBX_MATRIX_EL(m, i, 0) = x[i];
	}
	// 如果是指数拟合（FIT_EXPONENTIAL）或幂函数拟合（FIT_POWER），继续执行后续代码
	else if (FIT_EXPONENTIAL == fit || FIT_POWER == fit)
	{
		// 分配内存空间给矩阵m，尺寸为n×1
		// 如果分配内存失败，返回失败（FAIL）
		if (SUCCEED != zbx_matrix_alloc(m, n, 1))
			return FAIL;

		// 遍历输入数据x，对每个数据进行取对数操作，并将结果存储到矩阵m的第一列
		for (i = 0; i < n; i++)
		{
			// 如果x[i]小于等于0，提示数据包含负数或零值，并返回失败（FAIL）
			if (0.0 >= x[i])
			{
				zabbix_log(LOG_LEVEL_DEBUG, "data contains negative or zero values");
				return FAIL;
			}

			ZBX_MATRIX_EL(m, i, 0) = log(x[i]);
		}
	}

	// 执行完毕，返回成功（SUCCEED）
	return SUCCEED;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的拟合类型、自变量值数组t、拟合参数k，填充一个矩阵m。矩阵m的大小和元素根据不同的拟合类型和参数有所不同。在填充矩阵的过程中，首先判断拟合类型，然后为矩阵分配内存，最后根据拟合类型和参数填充矩阵的元素。
 ******************************************************************************/
// 定义一个静态函数zbx_fill_independent，用于根据不同的拟合类型填充矩阵m。
// 参数：
//    t：一个double类型的指针，指向一个包含n个元素的双精度浮点数数组，用于表示自变量值；
//    n：int类型，表示自变量值的数量；
//    fit：zbx_fit_t类型，表示拟合类型，可以是线性、指数、对数、多项式等；
//    k：int类型，表示多项式拟合时的阶数；
//    m：zbx_matrix_t类型的指针，指向一个矩阵，用于存储填充后的数据。
static int	zbx_fill_independent(double *t, int n, zbx_fit_t fit, int k, zbx_matrix_t *m)
{
	// 定义一个double类型的变量element，用于存储矩阵元素值；
	double	element;
	// 定义两个int类型的变量i和j，用于循环计数。
	int	i, j;
	// 判断拟合类型是否为线性或指数，如果是，执行以下操作：
	if (FIT_LINEAR == fit || FIT_EXPONENTIAL == fit)
	{
		// 为矩阵m分配内存，大小为nx2，如果分配失败，返回FAIL；
		if (SUCCEED != zbx_matrix_alloc(m, n, 2))
			return FAIL;

		// 遍历数组t，为矩阵m的每一行前两个元素赋值，第一个元素为1.0，第二个元素为t[i]；
		for (i = 0; i < n; i++)
		{
			ZBX_MATRIX_EL(m, i, 0) = 1.0;
			ZBX_MATRIX_EL(m, i, 1) = t[i];
		}
	}
	// 判断拟合类型是否为对数或幂，如果是，执行以下操作：
	else if (FIT_LOGARITHMIC == fit || FIT_POWER == fit)
	{
		// 为矩阵m分配内存，大小为nx2，如果分配失败，返回FAIL；
		if (SUCCEED != zbx_matrix_alloc(m, n, 2))
			return FAIL;

		// 遍历数组t，为矩阵m的每一行前两个元素赋值，第一个元素为1.0，第二个元素为log(t[i])；
		for (i = 0; i < n; i++)
		{
			ZBX_MATRIX_EL(m, i, 0) = 1.0;
			ZBX_MATRIX_EL(m, i, 1) = log(t[i]);
		}
	}
	// 判断拟合类型是否为多项式，如果是，执行以下操作：
	else if (FIT_POLYNOMIAL == fit)
	{
		// 如果k大于n-1，则将k设置为n-1；
		if (k > n - 1)
			k = n - 1;

		// 为矩阵m分配内存，大小为nx(k+1)，如果分配失败，返回FAIL；
		if (SUCCEED != zbx_matrix_alloc(m, n, k+1))
			return FAIL;

		// 遍历数组t，为矩阵m的每一行填充元素，根据多项式拟合的公式计算；
		for (i = 0; i < n; i++)
		{
			// 初始化一个元素值为1.0的变量element；
			element = 1.0;

			// 遍历k次，计算多项式拟合的系数；
			for (j = 0; j < k; j++)
			{
				// 将element的值赋给矩阵m的对应元素；
				ZBX_MATRIX_EL(m, i, j) = element;

				// 计算下一个元素值，即element乘以t[i]；
				element *= t[i];
			}

			// 将element的值赋给矩阵m的最后一个元素；
			ZBX_MATRIX_EL(m, i, k) = element;
		}
	}

	// 执行成功，返回SUCCEED。
	return SUCCEED;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_regression 的函数，该函数用于对给定的自变量 t 和因变量 x 进行线性回归分析。函数接收 5 个参数，分别是自变量和因变量的值、拟合函数的类型、拟合多项式的阶数以及用于存储拟合结果的系数矩阵。
 *
 *在函数内部，首先为两个矩阵 independent 和 dependent 分配内存空间。然后，依次调用 zbx_fill_independent 和 zbx_fill_dependent 函数，填充独立变量和依赖变量矩阵。接下来，调用 zbx_least_squares 函数，对独立变量和依赖变量进行最小二乘法拟合。如果以上任意一步操作失败，函数将通过 goto 语句跳转到 out 标签处，并释放已分配的内存。最后，返回整个函数的操作结果。
 ******************************************************************************/
// 定义一个名为 zbx_regression 的静态函数，该函数接收 5 个参数：
// 参数 1：一个 double 类型的指针，指向一个数组，该数组存储了自变量 t 的值；
// 参数 2：一个 double 类型的指针，指向一个数组，该数组存储了因变量 x 的值；
// 参数 3：一个 int 类型的值，表示自变量和因变量的数量；
// 参数 4：一个 zbx_fit_t 类型的值，表示拟合函数的类型；
// 参数 5：一个 int 类型的值，表示拟合多项式的阶数。
static int	zbx_regression(double *t, double *x, int n, zbx_fit_t fit, int k, zbx_matrix_t *coefficients)
{
	// 定义两个矩阵指针，分别为 independent 和 dependent，用于存储独立变量和依赖变量。
	zbx_matrix_t	*independent = NULL, *dependent = NULL;
	// 定义一个 int 类型的变量 res，用于存储分配矩阵结构的成功状态。
	int		res;

	// 为 independent 矩阵分配内存空间。
	zbx_matrix_struct_alloc(&independent);
	// 为 dependent 矩阵分配内存空间。
	zbx_matrix_struct_alloc(&dependent);

	// 调用 zbx_fill_independent 函数，填充 independent 矩阵，传入参数为 t、n、fit、k 和 independent。
	// 如果填充独立变量的操作失败，函数返回失败状态，并通过 goto 语句跳转到 out 标签处。
	if (SUCCEED != (res = zbx_fill_independent(t, n, fit, k, independent)))
		goto out;

	// 调用 zbx_fill_dependent 函数，填充依赖变量矩阵，传入参数为 x、n、fit 和 dependent。
	// 如果填充依赖变量的操作失败，函数返回失败状态，并通过 goto 语句跳转到 out 标签处。
	if (SUCCEED != (res = zbx_fill_dependent(x, n, fit, dependent)))
		goto out;

	// 调用 zbx_least_squares 函数，对独立变量和依赖变量进行最小二乘法拟合，传入参数为 independent、dependent 和 coefficients。
	// 如果最小二乘法拟合操作失败，函数返回失败状态，并通过 goto 语句跳转到 out 标签处。
	if (SUCCEED != (res = zbx_least_squares(independent, dependent, coefficients)))
		goto out;

out:
	// 释放 independent 和 dependent 矩阵的内存。
	zbx_matrix_free(independent);
	zbx_matrix_free(dependent);
	// 返回 res 变量，表示整个函数的操作结果。
	return res;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算一个多项式在给定点（由参数t表示）的值。函数接受两个参数，一个是double类型的t，表示多项式的一个点；另一个是zbx_matrix_t类型的指针，指向一个存储多项式系数矩阵的矩阵。
 *
 *在函数内部，首先定义了两个double类型的变量pow和res，分别用于存储幂次和结果。接着定义了一个整数类型的循环变量i，用于遍历多项式的系数矩阵。
 *
 *接下来，使用一个for循环遍历系数矩阵的每一行。在循环内部，将当前行首元素的幂次（由pow表示）乘以t，然后将结果累加到res变量中。
 *
 *最后，返回计算得到的结果res，即为多项式在给定点t的值。
 ******************************************************************************/
/* 定义一个函数，用于计算多项式在给定点的值 */
static double	zbx_polynomial_value(double t, zbx_matrix_t *coefficients)
{
	/* 定义两个变量，分别用于存储幂次和结果 */
	double	pow = 1.0, res = 0.0;
	/* 定义一个循环变量，用于遍历多项式的系数矩阵 */
	int	i;

	/* 遍历系数矩阵的行 */
	for (i = 0; i < coefficients->rows; i++, pow *= t)
		/* 累加当前行首元素的幂次乘积到结果中 */
		res += ZBX_MATRIX_EL(coefficients, i, 0) * pow;

	/* 返回计算得到的结果 */
	return res;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算一个多项式的反导数。函数接收两个参数，一个是double类型的t，表示自变量；另一个是zbx_matrix_t类型的指针coefficients，表示多项式的系数矩阵。通过循环遍历系数矩阵，计算多项式在t处的反导数，并返回结果。
 ******************************************************************************/
// 定义一个静态的double类型函数zbx_polynomial_antiderivative，接收两个参数：一个double类型的t，一个zbx_matrix_t类型的指针coefficients。
static double	zbx_polynomial_antiderivative(double t, zbx_matrix_t *coefficients)
{
	// 定义一个double类型的变量pow，用于存储t的幂次
	double	pow = t, res = 0.0;
	// 定义一个整型变量i，用于循环计数
	int	i;

	// 使用for循环，从0开始，直到coefficients->rows（即多项式的阶数）
	for (i = 0; i < coefficients->rows; i++, pow *= t)
		// 使用zbx_matrix_el函数获取系数矩阵中第i行第0列的元素（即多项式中的系数）
		res += ZBX_MATRIX_EL(coefficients, i, 0) * pow / (i + 1);

	// 返回计算得到的antiderivative结果
	return res;
}


/******************************************************************************
 * *
 *这段代码的主要目的是计算一个多项式的导数。它接收一个多项式矩阵（输入）和一个指向导数矩阵的指针（输出）。首先检查输入的多项式矩阵是否有效，如果无效则跳转到错误标签。然后为输出矩阵分配内存空间，遍历输入矩阵的每一行，计算导数并存入输出矩阵。最后，如果计算成功，返回 SUCCEED，否则返回 FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于计算多项式的导数
static int	zbx_derive_polynomial(zbx_matrix_t *polynomial, zbx_matrix_t *derivative)
{
	// 定义一个循环变量 i，用于遍历多项式的每一行
	int	i;

	// 检查输入的多项式矩阵是否有效
	if (!ZBX_VALID_MATRIX(polynomial))
		// 如果多项式矩阵无效，跳转到 error 标签处
		goto error;

	// 为输出矩阵（导数）分配内存空间
	if (SUCCEED != zbx_matrix_alloc(derivative, (polynomial->rows > 1 ? polynomial->rows - 1 : 1), 1))
		// 如果内存分配失败，返回 FAIL
		return FAIL;

	// 遍历多项式的每一行，计算导数
	for (i = 1; i < polynomial->rows; i++)
		// 计算当前行的第一个元素（系数）与 i 的乘积，存入导数矩阵
		ZBX_MATRIX_EL(derivative, i - 1, 0) = ZBX_MATRIX_EL(polynomial, i, 0) * i;

	// 如果当前行只有一个元素，即为一元一次多项式，导数为 0
	if (1 == i)
		ZBX_MATRIX_EL(derivative, 0, 0) = 0.0;

	// 计算成功，返回 SUCCEED
	return SUCCEED;

error:
	// 此标签处不应该发生，表示程序错误
	THIS_SHOULD_NEVER_HAPPEN;
	// 返回 FAIL，表示计算失败
	return FAIL;
}


/******************************************************************************
 * *
 *这段代码的主要目的是求解多项式的根。它定义了两个宏，分别表示复数的实部和虚部，以及一个复合函数用于计算两个复数的乘积。zbx_polynomial_roots函数接收两个矩阵作为输入参数，其中一个矩阵表示多项式的系数，另一个矩阵用于存储多项式的根。函数通过计算多项式的根矩阵，并将其存储在输入的roots矩阵中。如果求解成功，函数返回0；如果求解失败，返回-1。
 ******************************************************************************/
static int	zbx_polynomial_roots(zbx_matrix_t *coefficients, zbx_matrix_t *roots)
{
// 定义两个宏，分别表示复数的实部和虚部
#define Re(z)	(z)[0]
#define Im(z)	(z)[1]

// 定义一个复合函数，用于计算两个复数的乘积
#define ZBX_COMPLEX_MULT(z1, z2, tmp)			\
do							\
{							\
	Re(tmp) = Re(z1) * Re(z2) - Im(z1) * Im(z2);	\
	Im(tmp) = Re(z1) * Im(z2) + Im(z1) * Re(z2);	\
	Re(z1) = Re(tmp);				\
	Im(z1) = Im(tmp);				\
}							\

while(0)

#define ZBX_MAX_ITERATIONS	200

	zbx_matrix_t	*denominator_multiplicands = NULL, *updates = NULL;
	double		z[2], mult[2], denominator[2], zpower[2], polynomial[2], highest_degree_coefficient,
			lower_bound, upper_bound, radius, max_update, min_distance, residual, temp;
	int		i, j, degree, first_nonzero, res, iteration = 0, roots_ok = 0, root_init = 0;

	if (!ZBX_VALID_MATRIX(coefficients))
		goto error;

	degree = coefficients->rows - 1;
	highest_degree_coefficient = ZBX_MATRIX_EL(coefficients, degree, 0);

	while (0.0 == highest_degree_coefficient && 0 < degree)
		highest_degree_coefficient = ZBX_MATRIX_EL(coefficients, --degree, 0);

	if (0 == degree)
	{
		/* please check explicitly for an attempt to solve equation 0 == 0 */
		if (0.0 == highest_degree_coefficient)
			goto error;

		return SUCCEED;
	}

	if (1 == degree)
	{
		if (SUCCEED != zbx_matrix_alloc(roots, 1, 2))
			return FAIL;

		Re(ZBX_MATRIX_ROW(roots, 0)) = -ZBX_MATRIX_EL(coefficients, 0, 0) / ZBX_MATRIX_EL(coefficients, 1, 0);
		Im(ZBX_MATRIX_ROW(roots, 0)) = 0.0;

		return SUCCEED;
	}

	if (2 == degree)
	{
		if (SUCCEED != zbx_matrix_alloc(roots, 2, 2))
			return FAIL;

		if (0.0 < (temp = ZBX_MATRIX_EL(coefficients, 1, 0) * ZBX_MATRIX_EL(coefficients, 1, 0) -
				4 * ZBX_MATRIX_EL(coefficients, 2, 0) * ZBX_MATRIX_EL(coefficients, 0, 0)))
		{
			temp = (0 < ZBX_MATRIX_EL(coefficients, 1, 0) ?
					-ZBX_MATRIX_EL(coefficients, 1, 0) - sqrt(temp) :
					-ZBX_MATRIX_EL(coefficients, 1, 0) + sqrt(temp));
			Re(ZBX_MATRIX_ROW(roots, 0)) = 0.5 * temp / ZBX_MATRIX_EL(coefficients, 2, 0);
			Re(ZBX_MATRIX_ROW(roots, 1)) = 2.0 * ZBX_MATRIX_EL(coefficients, 0, 0) / temp;
			Im(ZBX_MATRIX_ROW(roots, 0)) = Im(ZBX_MATRIX_ROW(roots, 1)) = 0.0;
		}
		else
		{
			Re(ZBX_MATRIX_ROW(roots, 0)) = Re(ZBX_MATRIX_ROW(roots, 1)) =
					-0.5 * ZBX_MATRIX_EL(coefficients, 1, 0) / ZBX_MATRIX_EL(coefficients, 2, 0);
			Im(ZBX_MATRIX_ROW(roots, 0)) = -(Im(ZBX_MATRIX_ROW(roots, 1)) = 0.5 * sqrt(-temp)) /
					ZBX_MATRIX_EL(coefficients, 2, 0);
		}

		return SUCCEED;
	}

	zbx_matrix_struct_alloc(&denominator_multiplicands);
	zbx_matrix_struct_alloc(&updates);

	if (SUCCEED != zbx_matrix_alloc(roots, degree, 2) ||
			SUCCEED != zbx_matrix_alloc(denominator_multiplicands, degree, 2) ||
			SUCCEED != zbx_matrix_alloc(updates, degree, 2))
	{
		res = FAIL;
		goto out;
	}

	/* if n lower coefficients are zeros, zero is a root of multiplicity n */
	for (first_nonzero = 0; 0.0 == ZBX_MATRIX_EL(coefficients, first_nonzero, 0); first_nonzero++)
		Re(ZBX_MATRIX_ROW(roots, first_nonzero)) = Im(ZBX_MATRIX_ROW(roots, first_nonzero)) = 0.0;

	/* compute bounds for the roots */
	upper_bound = lower_bound = 1.0;

	for (i = first_nonzero; i < degree; i++)
	{
		if (upper_bound < fabs(ZBX_MATRIX_EL(coefficients, i, 0) / highest_degree_coefficient))
			upper_bound = fabs(ZBX_MATRIX_EL(coefficients, i, 0) / highest_degree_coefficient);

		if (lower_bound < fabs(ZBX_MATRIX_EL(coefficients, i + 1, 0) /
				ZBX_MATRIX_EL(coefficients, first_nonzero, 0)))
			lower_bound = fabs(ZBX_MATRIX_EL(coefficients, i + 1, 0) /
					ZBX_MATRIX_EL(coefficients, first_nonzero, 0));
	}

	radius = lower_bound = 1.0 / lower_bound;

	/* Weierstrass (Durand-Kerner) method */
	while (ZBX_MAX_ITERATIONS >= ++iteration && !roots_ok)
	{
		if (0 == root_init)
		{
			if (radius <= upper_bound)
			{
				for (i = 0; i < degree - first_nonzero; i++)
				{
					Re(ZBX_MATRIX_ROW(roots, i)) = radius * cos((2.0 * M_PI * (i + 0.25)) /
							(degree - first_nonzero));
					Im(ZBX_MATRIX_ROW(roots, i)) = radius * sin((2.0 * M_PI * (i + 0.25)) /
							(degree - first_nonzero));
				}

				radius *= 2.0;
			}
			else
				root_init = 1;
		}

		roots_ok = 1;
		max_update = 0.0;
		min_distance = HUGE_VAL;

		for (i = first_nonzero; i < degree; i++)
		{
			Re(z) = Re(ZBX_MATRIX_ROW(roots, i));
			Im(z) = Im(ZBX_MATRIX_ROW(roots, i));

			/* subtract from z every one of denominator_multiplicands and multiplicate them */
			Re(denominator) = highest_degree_coefficient;
			Im(denominator) = 0.0;

			for (j = first_nonzero; j < degree; j++)
			{
				if (j == i)
					continue;

				temp = (ZBX_MATRIX_EL(roots, i, 0) - ZBX_MATRIX_EL(roots, j, 0)) *
						(ZBX_MATRIX_EL(roots, i, 0) - ZBX_MATRIX_EL(roots, j, 0)) +
						(ZBX_MATRIX_EL(roots, i, 1) - ZBX_MATRIX_EL(roots, j, 1)) *
						(ZBX_MATRIX_EL(roots, i, 1) - ZBX_MATRIX_EL(roots, j, 1));
				if (temp < min_distance)
					min_distance = temp;

				Re(ZBX_MATRIX_ROW(denominator_multiplicands, j)) = Re(z) - Re(ZBX_MATRIX_ROW(roots, j));
				Im(ZBX_MATRIX_ROW(denominator_multiplicands, j)) = Im(z) - Im(ZBX_MATRIX_ROW(roots, j));
				ZBX_COMPLEX_MULT(denominator, ZBX_MATRIX_ROW(denominator_multiplicands, j), mult);
			}

			/* calculate complex value of polynomial for z */
			Re(zpower) = 1.0;
			Im(zpower) = 0.0;
			Re(polynomial) = ZBX_MATRIX_EL(coefficients, first_nonzero, 0);
			Im(polynomial) = 0.0;

			for (j = first_nonzero + 1; j <= degree; j++)
			{
				ZBX_COMPLEX_MULT(zpower, z, mult);
				Re(polynomial) += Re(zpower) * ZBX_MATRIX_EL(coefficients, j, 0);
				Im(polynomial) += Im(zpower) * ZBX_MATRIX_EL(coefficients, j, 0);
			}

			/* check how good root approximation is */
			residual = fabs(Re(polynomial)) + fabs(Im(polynomial));
			roots_ok = roots_ok && (ZBX_MATH_EPSILON > residual);

			/* divide polynomial value by denominator */
			if (0.0 != (temp = Re(denominator) * Re(denominator) + Im(denominator) * Im(denominator)))
			{
				Re(ZBX_MATRIX_ROW(updates, i)) = (Re(polynomial) * Re(denominator) +
						Im(polynomial) * Im(denominator)) / temp;
				Im(ZBX_MATRIX_ROW(updates, i)) = (Im(polynomial) * Re(denominator) -
						Re(polynomial) * Im(denominator)) / temp;
			}
			else	/* Denominator is zero iff two or more root approximations are equal. */
				/* Since root approximations are initially different their equality means that they */
				/* converged to a multiple root (hopefully) and no updates are required in this case. */
			{
				Re(ZBX_MATRIX_ROW(updates, i)) = Im(ZBX_MATRIX_ROW(updates, i)) = 0.0;
			}

			temp = ZBX_MATRIX_EL(updates, i, 0) * ZBX_MATRIX_EL(updates, i, 0) +
					ZBX_MATRIX_EL(updates, i, 1) * ZBX_MATRIX_EL(updates, i, 1);

			if (temp > max_update)
				max_update = temp;
		}

		if (max_update > radius * radius && 0 == root_init)
			continue;
		else
			root_init = 1;

		for (i = first_nonzero; i < degree; i++)
		{
			Re(ZBX_MATRIX_ROW(roots, i)) -= Re(ZBX_MATRIX_ROW(updates, i));
			Im(ZBX_MATRIX_ROW(roots, i)) -= Im(ZBX_MATRIX_ROW(updates, i));
		}
	}

	if (0 == roots_ok)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "polynomial root finding problem is ill-defined");
		res = FAIL;
	}
	else
		res = SUCCEED;

out:
	zbx_matrix_free(denominator_multiplicands);
	zbx_matrix_free(updates);
	return res;
error:
	THIS_SHOULD_NEVER_HAPPEN;
	return FAIL;

#undef ZBX_MAX_ITERATIONS

#undef Re
#undef Im
}

static int	zbx_polynomial_minmax(double now, double time, zbx_mode_t mode, zbx_matrix_t *coefficients,
		double *result)
{
	/* 分配一个指向zbx_matrix类型的指针，命名为derivative，用于存储导数矩阵 */
	zbx_matrix_t	*derivative = NULL;
	/* 分配一个指向zbx_matrix类型的指针，命名为derivative_roots，用于存储导数根矩阵 */
	zbx_matrix_t	*derivative_roots = NULL;
	/* 定义一个双精度浮点型变量min，用于存储最小值 */
	double		min;
	/* 定义一个双精度浮点型变量max，用于存储最大值 */
	double		max;
	/* 定义一个双精度浮点型变量tmp，用于临时存储值 */
	double		tmp;
	/* 定义一个整型变量i，用于循环计数 */
	int		i;
	/* 定义一个整型变量res，用于存储函数返回值 */
	int		res;

	/* 检查输入的系数矩阵是否有效 */
	if (!ZBX_VALID_MATRIX(coefficients))
		/* 如果系数矩阵无效，跳转到error标签处 */
		goto error;

	/* 分配内存并初始化导数矩阵 */
	zbx_matrix_struct_alloc(&derivative);
	/* 分配内存并初始化导数根矩阵 */
	zbx_matrix_struct_alloc(&derivative_roots);

	/* 调用zbx_derive_polynomial函数计算导数，并将结果存储在derivative矩阵中 */
	if (SUCCEED != (res = zbx_derive_polynomial(coefficients, derivative)))
		/* 如果计算导数失败，跳转到out标签处 */
		goto out;

	/* 调用zbx_polynomial_roots函数求解导数根，并将结果存储在derivative_roots矩阵中 */
	if (SUCCEED != (res = zbx_polynomial_roots(derivative, derivative_roots)))
		/* 如果求解导数根失败，跳转到out标签处 */
		goto out;

	/* 在now，now + time和导数根之间选择最小值和最大值（这些是潜在的局部极值） */
	/* 我们忽略根的虚部，这意味着更多的计算将会进行，但结果不会受到影响， */
	/* 并且我们不需要针对最小虚部设置边界，使其与零有所不同 */

	min = zbx_polynomial_value(now, coefficients);
	tmp = zbx_polynomial_value(now + time, coefficients);

	/* 如果tmp小于min，则更新min和max的值 */
	if (tmp < min)
	{
		max = min;
		min = tmp;
	}
	else
		max = tmp;

	/* 遍历导数根矩阵的每一行，查找更大的局部极值 */
	for (i = 0; i < derivative_roots->rows; i++)
	{
		tmp = ZBX_MATRIX_EL(derivative_roots, i, 0);

		/* 如果tmp小于now或大于now + time，则跳过此行 */
		if (tmp < now || tmp > now + time)
			continue;

		tmp = zbx_polynomial_value(tmp, coefficients);

		/* 如果tmp小于min，则更新min的值 */
		if (tmp < min)
			min = tmp;
		/* 如果tmp大于max，则更新max的值 */
		else if (tmp > max)
			max = tmp;
	}

	/* 根据模式选择最小值、最大值或差值作为结果 */
	if (MODE_MAX == mode)
		*result = max;
	else if (MODE_MIN == mode)
		*result = min;
	else if (MODE_DELTA == mode)
		*result = max - min;
	else
		/* 非法模式，不应发生这种情况 */
		THIS_SHOULD_NEVER_HAPPEN;

out:
	/* 释放derivative和derivative_roots矩阵 */
	zbx_matrix_free(derivative);
	zbx_matrix_free(derivative_roots);
	/* 返回计算结果 */
	return res;
error:
	/* 非法情况，不应发生 */
	THIS_SHOULD_NEVER_HAPPEN;
	/* 返回失败标志 */
	return FAIL;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是求解一个一元多项式在给定时间now下的最近根，并将结果存储在result变量中。为实现这一目的，代码首先检查输入的系数矩阵是否有效，然后分配两个矩阵（shifted_coefficients和roots）用于后续计算。接下来，代码复制系数矩阵到shifted_coefficients矩阵，并从移动后的系数矩阵中求解方程的根。在此基础上，代码遍历所有根，找到距离now最近的根，并更新result变量。最后，释放分配的内存，返回操作结果。如果发生错误，代码会给出详细的错误信息并返回失败。
 ******************************************************************************/
static int	zbx_polynomial_timeleft(double now, double threshold, zbx_matrix_t *coefficients, double *result)
{
	zbx_matrix_t	*shifted_coefficients = NULL, *roots = NULL; // 分配两个矩阵指针，分别为shifted_coefficients和roots
	double		tmp;
	int		i, res, no_root = 1; // 初始化no_root为1

	if (!ZBX_VALID_MATRIX(coefficients)) // 检查输入的系数矩阵是否有效
		goto error;

	zbx_matrix_struct_alloc(&shifted_coefficients); // 分配一个新的矩阵，用于存放移动后的系数
	zbx_matrix_struct_alloc(&roots); // 分配一个新的矩阵，用于存放方程的根

	if (SUCCEED != (res = zbx_matrix_copy(shifted_coefficients, coefficients))) // 复制系数矩阵到shifted_coefficients矩阵，若失败则退出
		goto out;

	ZBX_MATRIX_EL(shifted_coefficients, 0, 0) -= threshold; // 减去阈值

	if (SUCCEED != (res = zbx_polynomial_roots(shifted_coefficients, roots))) // 求解移动后系数矩阵的根，若失败则退出
		goto out;

	/* choose the closest root right from now or set result to -1 otherwise */
	/* if zbx_polynomial_value(tmp) is not close enough to zero it must be a complex root and must be skipped */

	for (i = 0; i < roots->rows; i++) // 遍历所有根
	{
		tmp = ZBX_MATRIX_EL(roots, i, 0); // 获取当前根

		if (no_root)
		{
			if (tmp > now && ZBX_MATH_EPSILON > fabs(zbx_polynomial_value(tmp, shifted_coefficients))) // 如果当前根大于now且与零的距离足够小，则更新result
			{
				no_root = 0;
				*result = tmp;
			}
		}
		else if (now < tmp && tmp < *result &&
				ZBX_MATH_EPSILON > fabs(zbx_polynomial_value(tmp, shifted_coefficients))) // 否则遍历其他根，如果当前根在now和*result之间且与零的距离足够小，则更新result
		{
			*result = tmp;
		}
	}

	if (no_root) // 如果no_root为真，表示没有找到合适的根
		*result = DB_INFINITY; // 结果为正无穷
	else
		*result -= now; // 结果减去now

out:
	zbx_matrix_free(shifted_coefficients); // 释放shifted_coefficients矩阵
	zbx_matrix_free(roots); // 释放roots矩阵
	return res; // 返回操作结果
error:
	THIS_SHOULD_NEVER_HAPPEN; // 表示不应该发生这种情况
	return FAIL; // 返回失败
}


/******************************************************************************
 * 以下是我为您注释好的代码块：
 *
 *
 *
 *整个代码块的主要目的是根据给定的参数（t，系数矩阵，拟合类型）计算拟合值，并将结果存储在`value`指向的内存位置。其中，拟合类型包括线性、多项式、指数、对数、幂次等。如果拟合类型不正确，函数将返回失败状态码。
 ******************************************************************************/
static int	zbx_calculate_value(double t, zbx_matrix_t *coefficients, zbx_fit_t fit, double *value)
{
    // 检查输入的系数矩阵是否有效
    if (!ZBX_VALID_MATRIX(coefficients))
        // 如果系数矩阵无效，跳转到错误处理
        goto error;

    // 判断拟合类型，如果是线性拟合
    if (FIT_LINEAR == fit)
        // 计算线性拟合的值
        *value = ZBX_MATRIX_EL(coefficients, 0, 0) + ZBX_MATRIX_EL(coefficients, 1, 0) * t;
    // 如果是多项式拟合
    else if (FIT_POLYNOMIAL == fit)
        // 计算多项式拟合的值
        *value = zbx_polynomial_value(t, coefficients);
    // 如果是指数拟合
    else if (FIT_EXPONENTIAL == fit)
        // 计算指数拟合的值
        *value = exp(ZBX_MATRIX_EL(coefficients, 0, 0) + ZBX_MATRIX_EL(coefficients, 1, 0) * t);
    // 如果是对数拟合
    else if (FIT_LOGARITHMIC == fit)
        // 计算对数拟合的值
        *value = ZBX_MATRIX_EL(coefficients, 0, 0) + ZBX_MATRIX_EL(coefficients, 1, 0) * log(t);
    // 如果是幂次拟合
    else if (FIT_POWER == fit)
        // 计算幂次拟合的值
        *value = exp(ZBX_MATRIX_EL(coefficients, 0, 0) + ZBX_MATRIX_EL(coefficients, 1, 0) * log(t));
    // 如果是其他类型的拟合，跳转到错误处理
    else
        goto error;

    // 函数执行成功，返回0
    return SUCCEED;

error:
    // 此代码块不应该被执行，表示拟合类型错误
    THIS_SHOULD_NEVER_HAPPEN;
    // 返回失败状态码
    return FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是根据输入的字符串参数 `fit_str` 设置 `fit` 结构体的值，同时输出 `k` 指针的值。根据 `fit_str` 的内容，分别对 `fit` 进行相应的设置，如线性拟合、多项式拟合、指数拟合、对数拟合和幂拟合。如果在判断过程中发现 `fit_str` 对应的拟合类型无效，则复制错误信息到 `error` 指针，并返回 `FAIL`。如果一切正常，返回 `SUCCEED`。
 ******************************************************************************/
// 定义一个函数，用于根据输入的字符串参数 fit_str 设置 fit 结构体的值，同时输出 k 指针的值
int zbx_fit_code(char *fit_str, zbx_fit_t *fit, unsigned *k, char **error)
{
	// 判断 fit_str 是否为空字符或等于 "linear"，如果是，直接设置 fit 为线性拟合，k 为 0
	if ('\0' == *fit_str || 0 == strcmp(fit_str, "linear"))
	{
		*fit = FIT_LINEAR;
		*k = 0;
	}
	// 判断 fit_str 是否以 "polynomial" 开头，如果是，设置 fit 为多项式拟合
	else if (0 == strncmp(fit_str, "polynomial", strlen("polynomial")))
	{
		*fit = FIT_POLYNOMIAL;

		// 检查 fit_str 后面的字符串是否为有效的多项式度数，如果是，继续执行后续操作
		if (SUCCEED != is_uint_range(fit_str + strlen("polynomial"), k, 1, 6))
		{
			// 如果多项式度数无效，复制错误信息到 error 指针，并返回 FAIL
			*error = zbx_strdup(*error, "polynomial degree is invalid");
			return FAIL;
		}
	}
	// 判断 fit_str 是否等于 "exponential"，如果是，设置 fit 为指数拟合，k 为 0
	else if (0 == strcmp(fit_str, "exponential"))
	{
		*fit = FIT_EXPONENTIAL;
		*k = 0;
	}
	// 判断 fit_str 是否等于 "logarithmic"，如果是，设置 fit 为对数拟合，k 为 0
	else if (0 == strcmp(fit_str, "logarithmic"))
	{
		*fit = FIT_LOGARITHMIC;
		*k = 0;
	}
	// 判断 fit_str 是否等于 "power"，如果是，设置 fit 为幂拟合，k 为 0
	else if (0 == strcmp(fit_str, "power"))
	{
		*fit = FIT_POWER;
		*k = 0;
	}
	// 如果 fit_str 都不是上述几种情况，说明参数无效，复制错误信息到 error 指针，并返回 FAIL
	else
	{
		*error = zbx_strdup(*error, "invalid 'fit' parameter");
		return FAIL;
	}

	// 如果一切正常，返回 SUCCEED
	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个字符串mode_str，根据字符串的值设置mode指针指向的zbx_mode_t类型的变量，如果解析失败，则返回错误信息。
 ******************************************************************************/
// 定义一个函数zbx_mode_code，接收三个参数：
// 1. 一个字符指针mode_str，用于存储模式字符串；
// 2. 一个zbx_mode_t类型的指针mode，用于存储解析后的模式；
// 3. 一个字符指针数组error，用于存储错误信息。
int zbx_mode_code(char *mode_str, zbx_mode_t *mode, char **error)
{
	// 判断mode_str是否为空字符或空字符串，如果是，直接将mode设置为MODE_VALUE模式。
	if ('\0' == *mode_str || 0 == strcmp(mode_str, "value"))
	{
		*mode = MODE_VALUE;
	}
	// 否则，判断mode_str是否为"max"（大写），如果是，将mode设置为MODE_MAX最大值模式。
	else if (0 == strcmp(mode_str, "max"))
	{
		*mode = MODE_MAX;
	}
	// 否则，判断mode_str是否为"min"（大写），如果是，将mode设置为MODE_MIN最小值模式。
	else if (0 == strcmp(mode_str, "min"))
	{
		*mode = MODE_MIN;
	}
	// 否则，判断mode_str是否为"delta"（大写），如果是，将mode设置为MODE_DELTA差值模式。
	else if (0 == strcmp(mode_str, "delta"))
	{
		*mode = MODE_DELTA;
	}
	// 否则，判断mode_str是否为"avg"（大写），如果是，将mode设置为MODE_AVG平均值模式。
	else if (0 == strcmp(mode_str, "avg"))
	{
		*mode = MODE_AVG;
	}
	// 否则，说明传入的mode_str参数无效，复制一份错误信息到error数组，并返回FAIL表示失败。
	else
	{
		*error = zbx_strdup(*error, "invalid 'mode' parameter");
		return FAIL;
	}

	// 如果解析成功，返回SUCCEED表示成功。
	return SUCCEED;
}


/******************************************************************************
 * 以下是我为您注释好的代码块：
 *
 *
 *
 *整个代码块的主要目的是根据给定的 fit 类型（线性、多项式、指数、对数或幂），计算拟合的表达式，并将结果打印到日志中。
 ******************************************************************************/
static void zbx_log_expression(double now, zbx_fit_t fit, int k, zbx_matrix_t *coeffs)
{
    /* x 是物品值，t 是从现在开始计算的时间（秒）*/

    // 判断 fit 的类型，如果是线性 Fit_LINEAR，则执行以下操作
    if (FIT_LINEAR == fit)
    {
		zabbix_log(LOG_LEVEL_DEBUG, "fitted expression is: x = (" ZBX_FS_DBL ") + (" ZBX_FS_DBL ") * (" ZBX_FS_DBL " + t)",
				ZBX_MATRIX_EL(coeffs, 0, 0), ZBX_MATRIX_EL(coeffs, 1, 0), now);
    }
    // 如果是多项式 Fit_POLYNOMIAL，则执行以下操作
    else if (FIT_POLYNOMIAL == fit)
    {
        char *polynomial = NULL;
        size_t alloc, offset;

        // 遍历 k，直到 k 减小到 0
        while (0 <= k)
        {
            zbx_snprintf_alloc(&polynomial, &alloc, &offset, "(" ZBX_FS_DBL ") * (" ZBX_FS_DBL " + t) ^ %d",
                    ZBX_MATRIX_EL(coeffs, k, 0), now, k);

            // 如果 k 不是 0，则在多项式中添加一个加号
            if (0 < k--)
                zbx_snprintf_alloc(&polynomial, &alloc, &offset, " + ");
        }

        // 输出拟合的表达式
		zabbix_log(LOG_LEVEL_DEBUG, "fitted expression is: x = %s", polynomial);
        // 释放 polynomial 内存
        zbx_free(polynomial);
    }
    // 如果是指数 Fit_EXPONENTIAL，则执行以下操作
    else if (FIT_EXPONENTIAL == fit)
    {
		zabbix_log(LOG_LEVEL_DEBUG, "fitted expression is: x = (" ZBX_FS_DBL ") * exp( (" ZBX_FS_DBL ") * (" ZBX_FS_DBL " + t) )",
				exp(ZBX_MATRIX_EL(coeffs, 0, 0)), ZBX_MATRIX_EL(coeffs, 1, 0), now);
    }
    // 如果是对数 Fit_LOGARITHMIC，则执行以下操作
    else if (FIT_LOGARITHMIC == fit)
    {
		zabbix_log(LOG_LEVEL_DEBUG, "fitted expression is: x = (" ZBX_FS_DBL ") + (" ZBX_FS_DBL ") * log(" ZBX_FS_DBL " + t)",
				ZBX_MATRIX_EL(coeffs, 0, 0), ZBX_MATRIX_EL(coeffs, 1, 0), now);
    }
    // 如果是幂 Fit_POWER，则执行以下操作
    else if (FIT_POWER == fit)
    {
		zabbix_log(LOG_LEVEL_DEBUG, "fitted expression is: x = (" ZBX_FS_DBL ") * (" ZBX_FS_DBL " + t) ^ (" ZBX_FS_DBL ")",
				exp(ZBX_MATRIX_EL(coeffs, 0, 0)), now, ZBX_MATRIX_EL(coeffs, 1, 0));
    }
    // 否则，不应该发生这种情况
    else
        THIS_SHOULD_NEVER_HAPPEN;
}

/******************************************************************************
 * *
 *这段代码的主要目的是提供一个名为`zbx_forecast`的函数，该函数根据输入的时间序列数据`t`、预测数据`x`、拟合类型`fit`、时间差`time`、模式`mode`等参数，计算出预测结果。函数支持多种拟合类型（如线性、指数、对数、幂函数等）和模式（如最大值、最小值、平均值、差值等）。计算结果后，函数返回预测值。
 ******************************************************************************/
double	zbx_forecast(double *t, double *x, int n, double now, double time, zbx_fit_t fit, unsigned k, zbx_mode_t mode)
{
	/* 声明变量 */
	zbx_matrix_t	*coefficients = NULL;
	double		left, right, result;
	int		res;

	/* 判断n是否为1，如果是，根据模式直接返回x[0]或执行其他操作 */
	if (1 == n)
	{
		if (MODE_VALUE == mode || MODE_MAX == mode || MODE_MIN == mode || MODE_AVG == mode)
			return x[0];

		if (MODE_DELTA == mode)
			return 0.0;

		THIS_SHOULD_NEVER_HAPPEN;
		return ZBX_MATH_ERROR;
	}

	/* 分配内存用于存储系数矩阵 */
	zbx_matrix_struct_alloc(&coefficients);

	/* 执行回归计算 */
	if (SUCCEED != (res = zbx_regression(t, x, n, fit, k, coefficients)))
		goto out;

	/* 对系数矩阵进行日志运算 */
	zbx_log_expression(now, fit, (int)k, coefficients);

	/* 根据模式进行不同操作 */
	if (MODE_VALUE == mode)
	{
		res = zbx_calculate_value(now + time, coefficients, fit, &result);
		goto out;
	}

	/* 时间差为0的情况下，根据模式返回最大、最小、平均或差值 */
	if (0.0 == time)
	{
		if (MODE_MAX == mode || MODE_MIN == mode || MODE_AVG == mode)
		{
			res = zbx_calculate_value(now + time, coefficients, fit, &result);
		}
		else if (MODE_DELTA == mode)
		{
			result = 0.0;
			res = SUCCEED;
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			res = FAIL;
		}

		goto out;
	}

	/* 根据拟合函数类型和模式计算结果 */
	if (FIT_LINEAR == fit || FIT_EXPONENTIAL == fit || FIT_LOGARITHMIC == fit || FIT_POWER == fit)
	{
		/* fit is monotone, therefore maximum and minimum are either at now or at now + time */
		if (SUCCEED != zbx_calculate_value(now, coefficients, fit, &left) ||
				SUCCEED != zbx_calculate_value(now + time, coefficients, fit, &right))
		{
			res = FAIL;
			goto out;
		}
		/* 计算结果，并根据模式返回最大、最小、平均或差值 */
		if (MODE_MAX == mode)
		{
			result = (left > right ? left : right);
		}
		else if (MODE_MIN == mode)
		{
			result = (left < right ? left : right);
		}
		else if (MODE_DELTA == mode)
		{
			result = (left > right ? left - right : right - left);
		}
		else if (MODE_AVG == mode)
		{
		if (FIT_LINEAR == fit)
		{
			result = 0.5 * (left + right);
		}
		else if (FIT_EXPONENTIAL == fit)
		{
			result = (right - left) / time / ZBX_MATRIX_EL(coefficients, 1, 0);
		}
		else if (FIT_LOGARITHMIC == fit)
		{
			result = right + ZBX_MATRIX_EL(coefficients, 1, 0) *
					(log(1.0 + time / now) * now / time - 1.0);
		}
		else if (FIT_POWER == fit)
		{
			if (-1.0 != ZBX_MATRIX_EL(coefficients, 1, 0))
				result = (right * (now + time) - left * now) / time /
						(ZBX_MATRIX_EL(coefficients, 1, 0) + 1.0);
			else
				result = exp(ZBX_MATRIX_EL(coefficients, 0, 0)) * log(1.0 + time / now) / time;
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			res = FAIL;
				goto out;
			}
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			res = FAIL;
			goto out;
		}

		res = SUCCEED;
	}
	else if (FIT_POLYNOMIAL == fit)
	{
		/* 计算最大、最小、差值或平均值，根据模式返回结果 */
		if (MODE_MAX == mode || MODE_MIN == mode || MODE_DELTA == mode)
		{
			res = zbx_polynomial_minmax(now, time, mode, coefficients, &result);
		}
		else if (MODE_AVG == mode)
		{
			result = (zbx_polynomial_antiderivative(now + time, coefficients) -
					zbx_polynomial_antiderivative(now, coefficients)) / time;
			res = SUCCEED;
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			res = FAIL;
		}
	}
	else
	{
		THIS_SHOULD_NEVER_HAPPEN;
		res = FAIL;
	}

out:
	/* 释放内存 */
	zbx_matrix_free(coefficients);

	/* 判断计算结果是否成功，并返回相应值 */
	if (SUCCEED != res)
	{
		result = ZBX_MATH_ERROR;
	}
	else if (ZBX_IS_NAN(result))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "numerical error");
		result = ZBX_MATH_ERROR;
	}
	else if (DB_INFINITY < result)
	{
		result = DB_INFINITY;
	}
	else if (-DB_INFINITY > result)
	{
		result = -DB_INFINITY;
	}

	return result;
}

/******************************************************************************
 * *
 *这块代码的主要目的是计算给定条件下，某个时间点的时间剩余量。函数接收多个参数，包括时间序列数据、待预测值、时间序列长度、当前时间、阈值、拟合类型和系数矩阵。通过zbx_regression函数进行回归计算，根据不同的拟合类型计算时间剩余量，并对其进行限制（大于0或小于等于DB_INFINITY），最后返回计算结果。在整个过程中，还对可能的错误情况进行处理，如数学错误和非法拟合类型。
 ******************************************************************************/
double	zbx_timeleft(double *t, double *x, int n, double now, double threshold, zbx_fit_t fit, unsigned k)
{
	/* 声明变量 */
	zbx_matrix_t	*coefficients = NULL;
	double		current, result;
	int		res;

	/* 如果输入的n为1，直接返回结果 */
	if (1 == n)
		return (x[0] == threshold ? 0.0 : DB_INFINITY);

	/* 分配内存用于存储系数矩阵 */
	zbx_matrix_struct_alloc(&coefficients);

	/* 调用zbx_regression函数进行回归计算，若失败，跳转out标签 */
	if (SUCCEED != (res = zbx_regression(t, x, n, fit, k, coefficients)))
		goto out;

	/* 对系数矩阵进行日志表达式计算 */
	zbx_log_expression(now, fit, (int)k, coefficients);

	/* 调用zbx_calculate_value计算当前时间下的值，若失败，跳转out标签 */
	if (SUCCEED != (res = zbx_calculate_value(now, coefficients, fit, &current)))
	{
		goto out;
	}
	else if (current == threshold)
	{
		result = 0.0;
		goto out;
	}

	/* 根据fit的值，计算结果 */
	if (FIT_LINEAR == fit)
	{
		result = (threshold - ZBX_MATRIX_EL(coefficients, 0, 0)) / ZBX_MATRIX_EL(coefficients, 1, 0) - now;
	}
	else if (FIT_POLYNOMIAL == fit)
	{
		res = zbx_polynomial_timeleft(now, threshold, coefficients, &result);
	}
	else if (FIT_EXPONENTIAL == fit)
	{
		result = (log(threshold) - ZBX_MATRIX_EL(coefficients, 0, 0)) / ZBX_MATRIX_EL(coefficients, 1, 0) - now;
	}
	else if (FIT_LOGARITHMIC == fit)
	{
		result = exp((threshold - ZBX_MATRIX_EL(coefficients, 0, 0)) / ZBX_MATRIX_EL(coefficients, 1, 0)) - now;
	}
	else if (FIT_POWER == fit)
	{
		result = exp((log(threshold) - ZBX_MATRIX_EL(coefficients, 0, 0)) / ZBX_MATRIX_EL(coefficients, 1, 0))
				- now;
	}
	else
	{
		THIS_SHOULD_NEVER_HAPPEN;
		res = FAIL;
	}

out:
	/* 如果res失败，返回数学错误值 */
	if (SUCCEED != res)
	{
		result = ZBX_MATH_ERROR;
	}
	/* 如果结果为NaN，记录日志并返回数学错误值 */
	else if (ZBX_IS_NAN(result))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "numerical error");
		result = ZBX_MATH_ERROR;
	}
	/* 如果结果不符合要求（大于0或小于等于DB_INFINITY），返回DB_INFINITY */
	else if (0.0 > result || DB_INFINITY < result)
	{
		result = DB_INFINITY;
	}

	/* 释放内存 */
	zbx_matrix_free(coefficients);

	/* 返回计算结果 */
	return result;
}

