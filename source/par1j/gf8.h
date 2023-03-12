// This is a sub-set of Galois.h , and changed for 8-bit only

/* Galois.h
 * James S. Plank

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
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

James S. Plank
Department of Computer Science
University of Tennessee
Knoxville, TN 37996
plank@cs.utk.edu

 */

#ifndef _GF8_H_
#define _GF8_H_

#ifdef __cplusplus
extern "C" {
#endif


extern int galois_create_log_tables(void);	/* Returns 0 on success, -1 on failure */
extern int galois_create_mult_tables(void);
extern int galois_multtable_multiply(int x, int y);
extern int galois_multtable_divide(int x, int y);

extern int galois_power(int x, int y);	// 乗数計算用に追加
extern void galois_free_tables(void);	// 解放用に追加

void galois_region_xor(
	unsigned char *r1,	// Region 1
	unsigned char *r2,	// Sum region (r2 = r1 ^ r2)
	int nbytes);		// Number of bytes in region

void galois_region_multiply(
	unsigned char *r1,	// Region to multiply
	unsigned char *r2,	// products go here.
	int nbytes,			// Number of bytes in region
	int multby);		// Number to multiply by

#define HASH_SIZE 4
#define HASH_RANGE 128
void checksum4(unsigned char *data, unsigned char *hash, int byte_size);


#ifdef __cplusplus
}
#endif

#endif
