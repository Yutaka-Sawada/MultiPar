// This is a sub-set of Galois.c
// Modified by Yutaka Sawada for 8-bit only

/* Galois.c
 * James S. Plank
 * April, 2007

Galois.tar - Fast Galois Field Arithmetic Library in C/C++
Copright (C) 2007 James S. Plank

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License aint with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

James S. Plank
Department of Computer Science
University of Tennessee
Knoxville, TN 37996
plank@cs.utk.edu

*/

#include <stdlib.h>

#include "gf8.h"

#define NW   256
#define NWM1 255
#define PRIM_POLY 0x11D

static int *galois_log_table = NULL;
static int *galois_exp_table;

static int *galois_mult_table = NULL;
static int *galois_div_table;

int galois_create_log_tables(void)
{
	int j, b;

	if (galois_log_table != NULL) return 0;
	galois_log_table = _aligned_malloc(sizeof(int) * NW * 4, 64);
	if (galois_log_table == NULL) return -1;
	galois_exp_table = galois_log_table + NW + 1;	// 後で境界をあわせるため

	b = 1;
	for (j = 0; j < NWM1; j++){
		galois_log_table[b] = j;
		galois_exp_table[j] = b;
		b = b << 1;
		if (b & NW) b ^= PRIM_POLY;
	}
	galois_log_table[0] = -1;

	for (j = 0; j < NWM1; j++){
		galois_exp_table[j + NWM1    ] = galois_exp_table[j];
		galois_exp_table[j + NWM1 * 2] = galois_exp_table[j];
	}
	galois_exp_table += NWM1;
	return 0;
}

int galois_create_mult_tables(void)
{
	int j, x, y, logx;

	if (galois_mult_table != NULL) return 0;
	galois_mult_table = _aligned_malloc(sizeof(int) * NW * NW * 2, 64);
	if (galois_mult_table == NULL) return -1;
	galois_div_table = galois_mult_table + NW * NW;

	if (galois_log_table == NULL) {
		if (galois_create_log_tables() < 0) {
			_aligned_free(galois_mult_table);
			galois_mult_table = NULL;
			return -1;
		}
	}

	/* Set mult/div tables for x = 0 */
	j = 0;
	galois_mult_table[j] = 0;   /* y = 0 */
	galois_div_table[j] = -1;
	j++;
	for (y = 1; y < NW; y++) {   /* y > 0 */
		galois_mult_table[j] = 0;
		galois_div_table[j] = 0;
		j++;
	}

	for (x = 1; x < NW; x++){  /* x > 0 */
		galois_mult_table[j] = 0; /* y = 0 */
		galois_div_table[j] = -1;
		j++;
		logx = galois_log_table[x];
		for (y = 1; y < NW; y++){  /* y > 0 */
			galois_mult_table[j] = galois_exp_table[logx + galois_log_table[y]];
			galois_div_table[j]  = galois_exp_table[logx - galois_log_table[y]];
			j++;
		}
	}
	return 0;
}

int galois_multtable_multiply(int x, int y)
{
  return galois_mult_table[(x << 8) | y];
}

int galois_multtable_divide(int x, int y)
{
  return galois_div_table[(x << 8) | y];
}

// ガロア体上での乗数計算、x の y 乗
int galois_power(int x, int y)
{
	int sum_j;

	if (y == 0) return 1; // x^0 = 1
	sum_j = x;
	while (y > 1){
		sum_j = galois_multtable_multiply(sum_j, x);
		y--;
	}
	return sum_j;
}

void galois_free_tables(void) // テーブルを一括して解放するために追加
{
	if (galois_mult_table != NULL) {
		_aligned_free(galois_mult_table);
		galois_mult_table = NULL;
	}
	if (galois_log_table != NULL) {
		_aligned_free(galois_log_table);
		galois_log_table = NULL;
	}
}

void galois_region_xor(
	unsigned char *r1,	// Region 1
	unsigned char *r2,	// Sum region (r2 = r1 ^ r2)
	int nbytes)			// Number of bytes in region
{
	int i;

	for (i = 0; i < nbytes; i++)
		r2[i] ^= r1[i];
}

void galois_region_multiply(
	unsigned char *r1,	// Region to multiply
	unsigned char *r2,	// products go here.
	int nbytes,			// Number of bytes in region
	int multby)			// Number to multiply by
{
	int i, *table;

	table = galois_mult_table + (multby << 8);
	for (i = 0; i < nbytes; i++)
		r2[i] ^= table[ r1[i] ];
}

// チェックサムを計算する
void checksum4(unsigned char *data, unsigned char *hash, int byte_size)
{
	int i, count;
	unsigned int *data4, temp, prev, mask;

	data4 = (unsigned int *)data;
	count = byte_size / 4;
	prev = 0;

	while (count > 0){	// HASH_RANGE バイトごとに
		// 4バイトに XOR する
		temp = 0;
		for (i = 0; i < HASH_RANGE / 4; i++){
			temp ^= data4[i];
			count--;
			if (count == 0)
				break;
		}
		data4 += HASH_RANGE / 4;

		// 前回の値を 2倍して、今回の値を追加する
		mask = (prev & 0x80808080) >> 7;	// 0x01010101 or 0x00000000
		prev = (prev & 0x7F7F7F7F) << 1;	// 3倍する場合は「^=」にすればいい
		prev ^= mask * 0x1D;	// 0x1D1D1D1D or 0x00000000

		prev ^= temp;
	}
	*((unsigned int *)hash) = prev;
}

