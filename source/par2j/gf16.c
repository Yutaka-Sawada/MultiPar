// The basic of 16-bit Galois Field arithmetic is based on Galois.c by James S. Plank.
// Modified by Yutaka Sawada to support MMX, SSE2, and SSSE3.

/* Galois.c
 * James S. Plank

Jerasure - A C/C++ Library for a Variety of Reed-Solomon and RAID-6 Erasure Coding Techniques

Revision 1.2A
May 24, 2011

James S. Plank
Department of Electrical Engineering and Computer Science
University of Tennessee
Knoxville, TN 37996
plank@cs.utk.edu

Copyright (c) 2011, James S. Plank
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

 - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

 - Neither the name of the University of Tennessee nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

 */

#define _WIN32_WINNT 0x0601	// Windows 7 or later

#include <stdlib.h>
#include <stdio.h>

#include <windows.h>
#include <intrin.h>	// 組み込み関数(intrinsic)を使用する場合インクルード

#include "gf16.h"
#include "gf_jit.h"	// ParPar の JIT コード用

extern unsigned int cpu_flag;	// declared in common2.h

#ifndef _WIN64	// 32-bit 版なら
#pragma warning(disable:4731)		// inhibit VC's "ebp modified" warning
#pragma warning(disable:4799)		// inhibit VC's "missing emms" warning
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// CPU によって使う関数を変更する際の仮宣言

//#define NO_SIMD	// SIMD を使わない場合

int sse_unit;

void galois_align16_multiply(unsigned char *r1, unsigned char *r2, unsigned int len, int factor);
void galois_align32_multiply(unsigned char *r1, unsigned char *r2, unsigned int len, int factor);
void galois_align32avx_multiply(unsigned char *r1, unsigned char *r2, unsigned int len, int factor);
void galois_align256_multiply(unsigned char *r1, unsigned char *r2, unsigned int len, int factor);

void galois_align32_multiply2(unsigned char *src1, unsigned char *src2, unsigned char *dst, unsigned int len, int factor1, int factor2);
void galois_align32avx_multiply2(unsigned char *src1, unsigned char *src2, unsigned char *dst, unsigned int len, int factor1, int factor2);

void galois_altmap_none(unsigned char *data, unsigned int bsize);

// AVX2 と SSSE3 の ALTMAP は 32バイト単位で行う
void galois_altmap32_change(unsigned char *data, unsigned int bsize);
void galois_altmap32_return(unsigned char *data, unsigned int bsize);
void checksum16_altmap32(unsigned char *data, unsigned char *hash, int byte_size);
void checksum16_return32(unsigned char *data, unsigned char *hash, int byte_size);

// JIT(SSE2) は 256バイト単位で計算する
void galois_altmap256_change(unsigned char *data, unsigned int bsize);
void galois_altmap256_return(unsigned char *data, unsigned int bsize);
void checksum16_altmap256(unsigned char *data, unsigned char *hash, int byte_size);
void checksum16_return256(unsigned char *data, unsigned char *hash, int byte_size);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define NW   65536
#define NWM1 65535
#define PRIM_POLY 0x1100B

// なぜかテーブルは 2バイト整数を使った方が速い
static unsigned short *galois_log_table = NULL;
static unsigned short *galois_exp_table;

int galois_create_table(void)
{
	unsigned int j, b;

	if (galois_log_table != NULL) return 0;
	galois_log_table = _aligned_malloc(sizeof(unsigned short) * NW * 2, 64);
	if (galois_log_table == NULL) return -1;
	galois_exp_table = galois_log_table + NW;	// 要素数は 65536個

	b = 1;
	for (j = 0; j < NWM1; j++){
		galois_log_table[b] = (unsigned short)j;
		galois_exp_table[j] = (unsigned short)b;
		b = b << 1;
		if (b & NW) b ^= PRIM_POLY;
	}
	galois_exp_table[NWM1] = galois_exp_table[0];	// copy for reduction (? mod NWM1)

	// CPU によって使う関数を変更する
	sse_unit = 16;	// 16, 32, 64, 128 のどれでもいい (32のSSSE3は少し速い、GPUが識別するのに注意)
	galois_align_multiply = galois_align16_multiply;
	galois_align_multiply2 = NULL;
	galois_altmap_change = galois_altmap_none;
	galois_altmap_return = galois_altmap_none;
	checksum16_altmap = checksum16;
	checksum16_return = checksum16;
#ifndef NO_SIMD
	if (cpu_flag & 256){	// AVX2, SSSE3, JIT(SSE2) の並び替えを使わない場合
		// 将来的には AVX-512 などの命令に対応してもいい
		//printf("\nWithout ALTMAP\n");
		//sse_unit = 32;
	} else if (cpu_flag & 16){	// AVX2 対応なら
		//printf("\nUse AVX2 & ALTMAP\n");
		sse_unit = 32;	// 32, 64, 128 のどれでもいい
		galois_align_multiply = galois_align32avx_multiply;
		galois_align_multiply2 = galois_align32avx_multiply2;
		galois_altmap_change = galois_altmap32_change;
		galois_altmap_return = galois_altmap32_return;
		checksum16_altmap = checksum16_altmap32;
		checksum16_return = checksum16_return32;
	} else if (cpu_flag & 1){	// SSSE3 対応なら
		//printf("\nUse SSSE3 & ALTMAP\n");
		sse_unit = 32;	// 32, 64, 128 のどれでもいい
		galois_align_multiply = galois_align32_multiply;
		galois_align_multiply2 = galois_align32_multiply2;
		galois_altmap_change = galois_altmap32_change;
		galois_altmap_return = galois_altmap32_return;
		checksum16_altmap = checksum16_altmap32;
		checksum16_return = checksum16_return32;
	} else {	// SSSE3 が利用できない場合
		if ((cpu_flag & 128) && (jit_alloc() == 0)){	// JIT(SSE2) を使う
			//printf("\nUse JIT(SSE2) & ALTMAP\n");
			sse_unit = 256;
			galois_align_multiply = galois_align256_multiply;
			galois_align_multiply2 = NULL;
			galois_altmap_change = galois_altmap256_change;
			galois_altmap_return = galois_altmap256_return;
			checksum16_altmap = checksum16_altmap256;
			checksum16_return = checksum16_return256;
		}
	}
#endif

	return 0;
}

unsigned short galois_multiply(int x, int y)
{
	int sum;

	if ((x == 0) || (y == 0)) return 0;

	sum = galois_log_table[x] + galois_log_table[y];	// result is from 2 to NWM1 * 2
	//if (sum >= NWM1) sum -= NWM1;
	sum = (sum >> 16) + (sum & NWM1);	// result is from 0 to NWM1
	return galois_exp_table[sum];
}

// multiply when "y" is a fixed value
unsigned short galois_multiply_fix(int x, int log_y)
{
	int sum;

	if (x == 0) return 0;

	sum = galois_log_table[x] + log_y;	// result is from 2 to NWM1 * 2
	sum = (sum >> 16) + (sum & NWM1);	// result is from 0 to NWM1
	return galois_exp_table[sum];
}

unsigned short galois_divide(int x, int y)
{
	int sum;

	if (y == 0) return NWM1;	// 除算エラー
	if (x == 0) return 0;

	sum = galois_log_table[x] - galois_log_table[y];
	if (sum < 0) sum += NWM1;
	return galois_exp_table[sum];
}

// ガロア体上での乗数計算、x の y 乗
unsigned short galois_power(int x, int y)
{
	unsigned int sum;

	if (x == 0) return 0;	// 0**y = 0
	if (y == 0) return 1;	// x**0 = 1
	if (y == 1) return (unsigned short)x;	// x**1 = x

	sum = (unsigned int)(galois_log_table[x]) * (unsigned int)y;	// result is from 1 to NWM1 * NWM1
	//sum = sum % NWM1;
	sum = (sum >> 16) + (sum & NWM1);	// result is from 1 to NWM1 * 2
	sum = (sum >> 16) + (sum & NWM1);	// result is from 0 to NWM1
	return galois_exp_table[sum];
}

// ガロア体上での逆数、1 / x
unsigned short galois_reciprocal(int x)
{
	if (x == 0) return NWM1;	// 除算エラー
	return galois_exp_table[NWM1 - (int)(galois_log_table[x])];
}

void galois_free_table(void) // テーブルを解放するために追加
{
	if (galois_log_table != NULL){
		_aligned_free(galois_log_table);
		galois_log_table = NULL;
#ifndef _WIN64	// 32-bit 版ならインライン・アセンブラを使う
		if (((cpu_flag & 1) == 0) && ((cpu_flag & 128) == 0))	// SSSE3 を使わない場合、MMX の終了処理
			_mm_empty();
#endif
		// SSSE3 を使わない場合で、JIT(SSE2) を使った場合の終了処理
		if (((cpu_flag & 1) == 0) && ((cpu_flag & 128) != 0))
			jit_free();
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// MMX functions are based on code by Paul Houle (paulhoule.com) March 22, 2008

#ifndef _WIN64	// 32-bit 版ならインライン・アセンブラを使う

// Processes block of data a multiple of 8 bytes long using SIMD (mmx) opcodes.
// The amount of data to process (bsize) must be a non-zero multiple of 8.
// Paul's original code was modified to calculate each 8-bytes and removed last shift by Yutaka Sawada
static void DoBlock8(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned int *pMtab)
{
	__asm {
	push	ebp
	mov		ebx,bsize					;bytes to process (multiple of 8)
	mov		esi,input					;source
	mov		edi,output					;destination
	mov		ebp,pMtab					;combined multiplication table

	mov		eax,[esi]					;load 1st 8 source bytes
	movd	mm4,[esi+4]

	sub		ebx,8						;reduce last 8-bytes from loop
	add		esi,ebx						;point to end of input/output
	add		edi,ebx
	neg		ebx							;convert byte size to count-up

lp8:
	movzx	edx,al
	movzx	ecx,ah
	shr		eax,16
	movd	mm0,[ebp+edx*4]				;order is [_][_][_][0]
	movd	mm1,[ebp+400h+ecx*4]
	movzx	edx,al
	movzx	ecx,ah
	movd	eax,mm4
	movq	mm4,[esi+ebx+8]				;read-ahead next 8 source bytes
	movd	mm2,[ebp+edx*4]				;order is [_][_][_][1]
	movzx	edx,al
	movq	mm5,[edi+ebx]
	movd	mm3,[ebp+400h+ecx*4]
	movzx	ecx,ah
	shr		eax,16
	punpcklwd mm0,[ebp+edx*4]			;order is [_][_][2][0]
	movzx	edx,al
	punpcklwd mm1,[ebp+400h+ecx*4]
	movzx	ecx,ah
	punpcklwd mm2,[ebp+edx*4]			;order is [_][_][3][1]
	pxor	mm1,mm0
	punpcklwd mm3,[ebp+400h+ecx*4]
	pxor	mm3,mm2
	movd	eax,mm4						;prepare src bytes 3-0 for next loop
	punpcklwd mm1,mm3					;order is [3][2][1][0]
	psrlq	mm4,32						;align src bytes 7-4 for next loop
	pxor	mm1,mm5
	movq	[edi+ebx],mm1
	add		ebx,8
	jnz		lp8

	;no need to pre-read in last 8-bytes
	movzx	edx,al
	movzx	ecx,ah
	shr		eax,16
	movd	mm0,[ebp+edx*4]				;order is [_][_][_][0]
	movd	mm1,[ebp+400h+ecx*4]
	movzx	edx,al
	movzx	ecx,ah
	movd	eax,mm4
	movd	mm2,[ebp+edx*4]				;order is [_][_][_][1]
	movzx	edx,al
	movq	mm5,[edi+ebx]
	movd	mm3,[ebp+400h+ecx*4]
	movzx	ecx,ah
	shr		eax,16
	punpcklwd mm0,[ebp+edx*4]			;order is [_][_][2][0]
	movzx	edx,al
	punpcklwd mm1,[ebp+400h+ecx*4]
	movzx	ecx,ah
	punpcklwd mm2,[ebp+edx*4]			;order is [_][_][3][1]
	pxor	mm1,mm0
	punpcklwd mm3,[ebp+400h+ecx*4]
	pxor	mm3,mm2
	punpcklwd mm1,mm3					;order is [3][2][1][0]
	pxor	mm1,mm5
	movq	[edi+ebx],mm1

	pop		ebp
	}
}

#endif

// calculate multiplication tables instantly
static void create_two_table(unsigned int *mtab, int factor){
	int shift_table[8], i, j, sum;

	// factor * 2の乗数を計算する
	shift_table[0] = factor;	// factor * 1
	for (i = 1; i < 8; i++){
//		if (factor & 0x8000){
//			factor <<= 1;
//			factor ^= 0x1100B;
//		} else {
//			factor <<= 1;
//		}
		factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
		shift_table[i] = factor;	// factor * (2**i)
	}

	for (j = 0; j < 2; j++){
		for (i = 0; i < 256; i += 32){
/*
			sum = 0;
			if (i & 32)
				sum = shift_table[5];
			if (i & 64)
				sum ^= shift_table[6];
			if (i & 128)
				sum ^= shift_table[7];
*/
			sum  = shift_table[5] & ((i << 26) >> 31);
			sum ^= shift_table[6] & ((i << 25) >> 31);
			sum ^= shift_table[7] & ((i << 24) >> 31);

			mtab[i     ] = sum;
			mtab[i +  1] = sum ^ shift_table[0];
			mtab[i +  2] = sum ^ shift_table[1];
			mtab[i +  3] = sum ^ shift_table[1] ^ shift_table[0];
			mtab[i +  4] = sum ^ shift_table[2];
			mtab[i +  5] = sum ^ shift_table[2] ^ shift_table[0];
			mtab[i +  6] = sum ^ shift_table[2] ^ shift_table[1];
			mtab[i +  7] = sum ^ shift_table[2] ^ shift_table[1] ^ shift_table[0];
			mtab[i +  8] = sum ^ shift_table[3];
			mtab[i +  9] = sum ^ shift_table[3] ^ shift_table[0];
			mtab[i + 10] = sum ^ shift_table[3] ^ shift_table[1];
			mtab[i + 11] = sum ^ shift_table[3] ^ shift_table[1] ^ shift_table[0];
			mtab[i + 12] = sum ^ shift_table[3] ^ shift_table[2];
			mtab[i + 13] = sum ^ shift_table[3] ^ shift_table[2] ^ shift_table[0];
			mtab[i + 14] = sum ^ shift_table[3] ^ shift_table[2] ^ shift_table[1];
			mtab[i + 15] = sum ^ shift_table[3] ^ shift_table[2] ^ shift_table[1] ^ shift_table[0];
			sum ^= shift_table[4];
			mtab[i + 16] = sum;
			mtab[i + 17] = sum ^ shift_table[0];
			mtab[i + 18] = sum ^ shift_table[1];
			mtab[i + 19] = sum ^ shift_table[1] ^ shift_table[0];
			mtab[i + 20] = sum ^ shift_table[2];
			mtab[i + 21] = sum ^ shift_table[2] ^ shift_table[0];
			mtab[i + 22] = sum ^ shift_table[2] ^ shift_table[1];
			mtab[i + 23] = sum ^ shift_table[2] ^ shift_table[1] ^ shift_table[0];
			mtab[i + 24] = sum ^ shift_table[3];
			mtab[i + 25] = sum ^ shift_table[3] ^ shift_table[0];
			mtab[i + 26] = sum ^ shift_table[3] ^ shift_table[1];
			mtab[i + 27] = sum ^ shift_table[3] ^ shift_table[1] ^ shift_table[0];
			mtab[i + 28] = sum ^ shift_table[3] ^ shift_table[2];
			mtab[i + 29] = sum ^ shift_table[3] ^ shift_table[2] ^ shift_table[0];
			mtab[i + 30] = sum ^ shift_table[3] ^ shift_table[2] ^ shift_table[1];
			mtab[i + 31] = sum ^ shift_table[3] ^ shift_table[2] ^ shift_table[1] ^ shift_table[0];
		}

		for (i = 0; i < 8; i++){
			factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
			shift_table[i] = factor;	// factor * (2**i)
		}
		mtab += 256;
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// The method of using SSSE3 is based on Plank's papar;
// "Screaming Fast Galois Field Arithmetic Using Intel SIMD Instructions".

/*
static void create_eight_table_c(unsigned char *mtab, int factor)
{
	int shift_table[16], i, sum;

	// factor * 2の乗数を計算する
	shift_table[0] = factor;	// factor * 1
	for (i = 1; i < 16; i++){
//		if (factor & 0x8000){
//			factor <<= 1;
//			factor ^= 0x1100B;
//		} else {
//			factor <<= 1;
//		}
		factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
		shift_table[i] = factor;	// factor * (2**i)
	}

	for (i = 0; i < 16; i += 4){	// 4-bit ごとに計算する
		mtab[i * 8     ] = 0;
		mtab[i * 8 + 16] = 0;
		sum = shift_table[i];
		mtab[i * 8 +  1] = (unsigned char)sum;			// lower 8-bit
		mtab[i * 8 + 17] = (unsigned char)(sum >> 8);	// higher 8-bit
		sum = shift_table[i + 1];
		mtab[i * 8 +  2] = (unsigned char)sum;
		mtab[i * 8 + 18] = (unsigned char)(sum >> 8);
		sum = sum ^ shift_table[i];
		mtab[i * 8 +  3] = (unsigned char)sum;
		mtab[i * 8 + 19] = (unsigned char)(sum >> 8);
		sum = shift_table[i + 2];
		mtab[i * 8 +  4] = (unsigned char)sum;
		mtab[i * 8 + 20] = (unsigned char)(sum >> 8);
		sum = sum ^ shift_table[i];
		mtab[i * 8 +  5] = (unsigned char)sum;
		mtab[i * 8 + 21] = (unsigned char)(sum >> 8);
		sum = shift_table[i + 2] ^ shift_table[i + 1];
		mtab[i * 8 +  6] = (unsigned char)sum;
		mtab[i * 8 + 22] = (unsigned char)(sum >> 8);
		sum = sum ^ shift_table[i];
		mtab[i * 8 +  7] = (unsigned char)sum;
		mtab[i * 8 + 23] = (unsigned char)(sum >> 8);
		sum = shift_table[i + 3];
		mtab[i * 8 +  8] = (unsigned char)sum;
		mtab[i * 8 + 24] = (unsigned char)(sum >> 8);
		sum = sum ^ shift_table[i];
		mtab[i * 8 +  9] = (unsigned char)sum;
		mtab[i * 8 + 25] = (unsigned char)(sum >> 8);
		sum = shift_table[i + 3] ^ shift_table[i + 1];
		mtab[i * 8 + 10] = (unsigned char)sum;
		mtab[i * 8 + 26] = (unsigned char)(sum >> 8);
		sum = sum ^ shift_table[i];
		mtab[i * 8 + 11] = (unsigned char)sum;
		mtab[i * 8 + 27] = (unsigned char)(sum >> 8);
		sum = shift_table[i + 3] ^ shift_table[i + 2];
		mtab[i * 8 + 12] = (unsigned char)sum;
		mtab[i * 8 + 28] = (unsigned char)(sum >> 8);
		sum = sum ^ shift_table[i];
		mtab[i * 8 + 13] = (unsigned char)sum;
		mtab[i * 8 + 29] = (unsigned char)(sum >> 8);
		sum = shift_table[i + 3] ^ shift_table[i + 2] ^ shift_table[i + 1];
		mtab[i * 8 + 14] = (unsigned char)sum;
		mtab[i * 8 + 30] = (unsigned char)(sum >> 8);
		sum = sum ^ shift_table[i];
		mtab[i * 8 + 15] = (unsigned char)sum;
		mtab[i * 8 + 31] = (unsigned char)(sum >> 8);
	}
}
*/

#ifndef _WIN64	// 32-bit 版ならインライン・アセンブラを使う

// tables for split four combined multiplication
static void create_eight_table(unsigned char *mtab, int factor)
{
	__asm {	// This implementation requires SSE2
	mov		eax, factor
	mov		edx, mtab
	mov		ecx, -128

	; create mask for 8-bit
	pcmpeqw	xmm7, xmm7		; 0xFFFF *8
	psrlw	xmm7, 8			; 0x00FF *8

lp32:
	; factor * 1, *2, *4, *8
	movd	xmm0, eax		; [_][_][_][_][_][_][_][1]
	pxor	xmm1, xmm1

	movsx	ebx, ax
	sar		ebx, 31
	shl		eax, 1
	and		ebx, 0x1100B
	xor		eax, ebx
	pinsrw	xmm1, eax, 1	; [_][_][_][_][_][_][2][_]
	pxor	xmm2, xmm2

	movsx	ebx, ax
	sar		ebx, 31
	shl		eax, 1
	and		ebx, 0x1100B
	xor		eax, ebx
	pinsrw	xmm2, eax, 4	; [_][_][_][4][_][_][_][_]
	punpcklwd	xmm1, xmm1	; [_][_][_][_][2][2][_][_]

	movsx	ebx, ax
	sar		ebx, 31
	shl		eax, 1
	and		ebx, 0x1100B
	xor		eax, ebx
	movd	xmm3, eax		; [_][_][_][_][_][_][_][8]

	pshuflw	xmm0, xmm0, 17	; [_][_][_][_][1][_][1][_]
	punpcklwd	xmm3, xmm3	; [_][_][_][_][_][_][8][8]
	pxor	xmm0, xmm1		; [_][_][_][_][3][2][1][_]
	pshufhw	xmm2, xmm2, 0	; [4][4][4][4][_][_][_][_]
	punpcklqdq	xmm0, xmm0	; [3][2][1][_][3][2][1][_]
	pshufd	xmm3, xmm3, 0	; [8][8][8][8][8][8][8][8]
	pxor	xmm2, xmm0		; [7][6][5][4][3][2][1][_]
	pxor	xmm3, xmm2		; [15][14][13][12][11][10][9][8]

	movdqa	xmm0, xmm2
	movdqa	xmm1, xmm3
	pand	xmm0, xmm7
	pand	xmm1, xmm7
	packuswb	xmm0, xmm1	; lower 8-bit * 16
	psrlw	xmm2, 8
	psrlw	xmm3, 8
	packuswb	xmm2, xmm3	; higher 8-bit * 16

	movdqa	[edx+128+ecx], xmm0
	movdqa	[edx+144+ecx], xmm2

	; for next loop
	movsx	ebx, ax		; move with sign of word
	sar		ebx, 31
	shl		eax, 1
	and		ebx, 0x1100B
	xor		eax, ebx

	add		ecx, 32
	jnz		lp32
	}
}

// VC2008 は SSSE3 をインライン・アセンブラで使える
// Address (input) does not need be 16-byte aligned
static void gf16_ssse3_block16u(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__asm {
	mov		ecx, input		; source
	mov		edx, output		; destination
	mov		eax, bsize		; bytes to process (multiple of 16)
	mov		ebx, table		; multiplication table

	; create mask for 8 entries
	pcmpeqw	xmm7, xmm7		; 0xFFFF *8
	psrlw	xmm7, 12		; 0x000F *8

	add		ecx, eax		; point to end of input/output
	add		edx, eax
	neg		eax				; convert byte size to count-up

lp16:
	movdqu	xmm0, [ecx+eax]	; read source 16-bytes
	movdqa	xmm2, [edx+eax]

	movdqa	xmm3, [ebx]		; low table
	movdqa	xmm4, [ebx+16]	; high table
	movdqa	xmm1, xmm0		; copy source
	psrlw	xmm0, 4			; prepare next 4-bit
	pand	xmm1, xmm7		; src & 0x000F
	pshufb	xmm3, xmm1		; table look-up
	psllw	xmm1, 8			; shift 8-bit for higher table
	pshufb	xmm4, xmm1
		movdqa	xmm5, [ebx+32]	; low table
		movdqa	xmm6, [ebx+48]	; high table
	pxor	xmm3, xmm4		; combine high and low
	pxor	xmm2, xmm3

		movdqa	xmm1, xmm0		; copy source
		psrlw	xmm0, 4			; prepare next 4-bit
		pand	xmm1, xmm7		; (src >> 4) & 0x000F
		pshufb	xmm5, xmm1		; table look-up
		psllw	xmm1, 8			; shift 8-bit for higher table
		pshufb	xmm6, xmm1
	movdqa	xmm3, [ebx+64]	; low table
	movdqa	xmm4, [ebx+80]	; high table
		pxor	xmm5, xmm6		; combine high and low
		pxor	xmm2, xmm5

	movdqa	xmm1, xmm0		; copy source
	psrlw	xmm0, 4			; prepare next 4-bit
	pand	xmm1, xmm7		; (src >> 8) & 0x000F
	pshufb	xmm3, xmm1		; table look-up
	psllw	xmm1, 8			; shift 8-bit for higher table
	pshufb	xmm4, xmm1
		movdqa	xmm5, [ebx+96]	; low table
		movdqa	xmm6, [ebx+112]	; high table
	pxor	xmm3, xmm4		; combine high and low
	pxor	xmm2, xmm3

		pshufb	xmm5, xmm0		; table look-up
		psllw	xmm0, 8			; shift 8-bit for higher table
		pshufb	xmm6, xmm0
		pxor	xmm5, xmm6		; combine high and low
		pxor	xmm2, xmm5

	movdqa	[edx+eax], xmm2

	add		eax, 16
	jnz		lp16
	}
}

// その場で 32バイトごとに並び替えて、計算後に戻す方法（50% faster than 16-byte version)
// Address (input) does not need be 16-byte aligned
static void gf16_ssse3_block32u(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__asm {
	mov		ecx, input		; source
	mov		edx, output		; destination
	mov		eax, bsize		; bytes to process (multiple of 32)
	mov		ebx, table		; multiplication table

	; create mask for 16 entries
	pcmpeqw	xmm7, xmm7		; 0xFFFF *8
	pcmpeqw	xmm6, xmm6		; 0xFFFF *8
	psrlw	xmm7, 12		; 0x000F *8
	psrlw	xmm6, 8			; 0x00FF *8
	packuswb	xmm7, xmm7	; 0x0F *16

	add		ecx, eax		; point to end of input/output
	add		edx, eax
	neg		eax				; convert byte size to count-up

lp32:
	movdqu	xmm0, [ecx+eax   ]	; read source 32-bytes
	movdqu	xmm2, [ecx+eax+16]
	movdqa	xmm1, xmm0		; copy source
	movdqa	xmm3, xmm2
	pand	xmm0, xmm6		; erase higher byte
	pand	xmm2, xmm6
	psrlw		xmm1, 8		; move higher byte to lower
	psrlw		xmm3, 8
	packuswb	xmm0, xmm2	; select lower byte of each word
	packuswb	xmm1, xmm3	; select higher byte of each word

	movdqa	xmm4, [ebx]		; low table
	movdqa	xmm5, [ebx+16]	; high table
	movdqa	xmm3, xmm0		; copy source
	psrlw	xmm0, 4			; prepare next 4-bit
	pand	xmm3, xmm7		; src & 0x0F
	pand	xmm0, xmm7		; (src >> 4) & 0x0F
	pshufb	xmm4, xmm3		; table look-up
	pshufb	xmm5, xmm3

	movdqa	xmm2, [ebx+32]	; low table
	movdqa	xmm3, [ebx+48]	; high table
	pshufb	xmm2, xmm0		; table look-up
	pshufb	xmm3, xmm0
	pxor	xmm4, xmm2		; combine result
	pxor	xmm5, xmm3

	movdqa	xmm2, [ebx+64]	; low table
	movdqa	xmm3, [ebx+80]	; high table
	movdqa	xmm0, xmm1		; copy source
	psrlw	xmm1, 4			; prepare next 4-bit
	pand	xmm0, xmm7		; src & 0x0F
	pand	xmm1, xmm7		; (src >> 4) & 0x0F
	pshufb	xmm2, xmm0		; table look-up
	pshufb	xmm3, xmm0
	pxor	xmm4, xmm2		; combine result
	pxor	xmm5, xmm3

	movdqa	xmm2, [ebx+96]	; low table
	movdqa	xmm3, [ebx+112]	; high table
	pshufb	xmm2, xmm1		; table look-up
	pshufb	xmm3, xmm1
	pxor	xmm4, xmm2		; combine result
	pxor	xmm5, xmm3

	movdqa	xmm0, [edx+eax]	; read dest 32-bytes
	movdqa	xmm1, [edx+eax+16]
	movdqa	xmm3, xmm4		; copy result
	punpcklbw	xmm3, xmm5	; interleave lower and higher bytes
	punpckhbw	xmm4, xmm5
	pxor	xmm0, xmm3
	pxor	xmm1, xmm4
	movdqa	[edx+eax], xmm0	; write dest 32-bytes
	movdqa	[edx+eax+16], xmm1

	add		eax, 32
	jnz		lp32
	}
}

// 先に 32バイトごとに並び替えてあるデータを扱う方法
static void gf16_ssse3_block32_altmap(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__asm {
	mov		ecx, input		; source
	mov		edx, output		; destination
	mov		eax, bsize		; bytes to process (multiple of 32)
	mov		ebx, table		; multiplication table

	; create mask for 16 entries
	pcmpeqw	xmm7, xmm7		; 0xFFFF *8
	psrlw	xmm7, 12		; 0x000F *8
	packuswb	xmm7, xmm7	; 0x0F *16

	add		ecx, eax		; point to end of input/output
	add		edx, eax
	neg		eax				; convert byte size to count-up

lp32:
	movdqa	xmm0, [ecx+eax]	; read source 32-bytes
	movdqa	xmm1, [ecx+eax+16]

	movdqa	xmm4, [ebx]		; low table
	movdqa	xmm5, [ebx+16]	; high table
	movdqa	xmm3, xmm0		; copy source
	psrlw	xmm0, 4			; prepare next 4-bit
	pand	xmm3, xmm7		; src & 0x0F
	pand	xmm0, xmm7		; (src >> 4) & 0x0F
	pshufb	xmm4, xmm3		; table look-up
	pshufb	xmm5, xmm3

	movdqa	xmm2, [ebx+32]	; low table
	movdqa	xmm3, [ebx+48]	; high table
	pshufb	xmm2, xmm0		; table look-up
	pshufb	xmm3, xmm0
	pxor	xmm2, xmm4		; combine result
	pxor	xmm3, xmm5

	movdqa	xmm4, [ebx+64]	; low table
	movdqa	xmm5, [ebx+80]	; high table
	movdqa	xmm0, xmm1		; copy source
	psrlw	xmm0, 4			; prepare next 4-bit
	pand	xmm1, xmm7		; src & 0x0F
	pand	xmm0, xmm7		; (src >> 4) & 0x0F
	pshufb	xmm4, xmm1		; table look-up
	pshufb	xmm5, xmm1
	pxor	xmm4, xmm2		; combine result
	pxor	xmm5, xmm3

	movdqa	xmm2, [ebx+96]	; low table
	movdqa	xmm3, [ebx+112]	; high table
	pshufb	xmm2, xmm0		; table look-up
	pshufb	xmm3, xmm0

	movdqa	xmm0, [edx+eax]	; read dest 32-bytes
	movdqa	xmm1, [edx+eax+16]
	pxor	xmm4, xmm2	; combine result
	pxor	xmm5, xmm3
	pxor	xmm4, xmm0
	pxor	xmm5, xmm1
	movdqa	[edx+eax], xmm4	; write dest 32-bytes
	movdqa	[edx+eax+16], xmm5

	add		eax, 32
	jnz		lp32
	}
}

#else	// 64-bit 版ではインライン・アセンブラを使えない
// (__m128i *) で逐次ポインターをキャスト変換するよりも、
// 先に __m128i* で定義しておいた方が、連続した領域へのアクセス最適化がうまくいく？
// ほとんど変わらない気がする（むしろ遅い？）・・・コンパイラ次第なのかも

// tables for split four combined multiplication
static void create_eight_table(unsigned char *mtab, int factor)
{
	int count = 4;
	__m128i *tbl;
	__m128i xmm0, xmm1, xmm2, xmm3, mask;

	tbl = (__m128i *)mtab;

	// create mask for 8-bit
	mask = _mm_setzero_si128();
	mask = _mm_cmpeq_epi16(mask, mask);	// 0xFFFF *8
	mask = _mm_srli_epi16(mask, 8);		// 0x00FF *8

	while (1){
		xmm0 = _mm_cvtsi32_si128(factor);			// [_][_][_][_][_][_][_][1]
		xmm1 = _mm_setzero_si128();

		factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
		xmm1 = _mm_insert_epi16(xmm1, factor, 1);	// [_][_][_][_][_][_][2][_]
		xmm2 = _mm_setzero_si128();

		factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
		xmm2 = _mm_insert_epi16(xmm2, factor, 4);	// [_][_][_][4][_][_][_][_]
		xmm1 = _mm_unpacklo_epi16(xmm1, xmm1);		// [_][_][_][_][2][2][_][_]

		factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
		xmm3 = _mm_cvtsi32_si128(factor);			// [_][_][_][_][_][_][_][8]

		xmm0 = _mm_shufflelo_epi16(xmm0, _MM_SHUFFLE(0, 1, 0, 1));	// [_][_][_][_][1][_][1][_]
		xmm3 = _mm_unpacklo_epi16(xmm3, xmm3);						// [_][_][_][_][_][_][8][8]
		xmm0 = _mm_xor_si128(xmm0, xmm1);							// [_][_][_][_][3][2][1][_]
		xmm2 = _mm_shufflehi_epi16(xmm2, _MM_SHUFFLE(0, 0, 0, 0));	// [4][4][4][4][_][_][_][_]
		xmm0 = _mm_unpacklo_epi64(xmm0, xmm0);						// [3][2][1][_][3][2][1][_]
		xmm3 = _mm_shuffle_epi32(xmm3, _MM_SHUFFLE(0, 0, 0, 0));	// [8][8][8][8][8][8][8][8]
		xmm2 = _mm_xor_si128(xmm2, xmm0);							// [7][6][5][4][3][2][1][_]
		xmm3 = _mm_xor_si128(xmm3, xmm2);							// [15][14][13][12][11][10][9][8]

		xmm0 = _mm_load_si128(&xmm2);
		xmm1 = _mm_load_si128(&xmm3);
		xmm0 = _mm_and_si128(xmm0, mask);
		xmm1 = _mm_and_si128(xmm1, mask);
		xmm0 = _mm_packus_epi16(xmm0, xmm1);	// lower 8-bit * 16
		xmm2 = _mm_srli_epi16(xmm2, 8);
		xmm3 = _mm_srli_epi16(xmm3, 8);
		xmm2 = _mm_packus_epi16(xmm2, xmm3);	// higher 8-bit * 16

		_mm_store_si128(tbl    , xmm0);
		_mm_store_si128(tbl + 1, xmm2);

		count--;
		if (count == 0)
			break;

		factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
		tbl += 2;
	}
}

// 16バイトごとに計算する方法、_mm_shuffle_epi8 の利用効率が悪い。
// Address (input) does not need be 16-byte aligned
static void gf16_ssse3_block16u(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m128i *src, *dst, *tbl;
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

	src = (__m128i *)input;
	dst = (__m128i *)output;
	tbl = (__m128i *)table;

	// create mask for 8 entries
	xmm7 = _mm_setzero_si128();
	xmm7 = _mm_cmpeq_epi16(xmm7, xmm7);	// 0xFFFF *8
	xmm7 = _mm_srli_epi16(xmm7, 12);	// 0x000F *8

	while (bsize != 0){
		xmm0 = _mm_loadu_si128(src);	// read source 16-bytes
		xmm2 = _mm_load_si128(dst);

		xmm3 = _mm_load_si128(tbl);		// low table
		xmm4 = _mm_load_si128(tbl + 1);	// high table
		xmm1 = _mm_load_si128(&xmm0);			// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);			// prepare next 4-bit
		xmm1 = _mm_and_si128(xmm1, xmm7);		// src & 0x000F
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);	// table look-up
		xmm1 = _mm_slli_epi16(xmm1, 8);			// shift 8-bit for higher table
		xmm4 = _mm_shuffle_epi8(xmm4, xmm1);
			xmm5 = _mm_load_si128(tbl + 2);	// low table
			xmm6 = _mm_load_si128(tbl + 3);	// high table
		xmm3 = _mm_xor_si128(xmm3, xmm4);	// combine high and low
		xmm2 = _mm_xor_si128(xmm2, xmm3);

			xmm1 = _mm_load_si128(&xmm0);			// copy source
			xmm0 = _mm_srli_epi16(xmm0, 4);			// prepare next 4-bit
			xmm1 = _mm_and_si128(xmm1, xmm7);		// src & 0x000F
			xmm5 = _mm_shuffle_epi8(xmm5, xmm1);	// table look-up
			xmm1 = _mm_slli_epi16(xmm1, 8);			// shift 8-bit for higher table
			xmm6 = _mm_shuffle_epi8(xmm6, xmm1);
		xmm3 = _mm_load_si128(tbl + 4);	// low table
		xmm4 = _mm_load_si128(tbl + 5);	// high table
			xmm5 = _mm_xor_si128(xmm5, xmm6);	// combine high and low
			xmm2 = _mm_xor_si128(xmm2, xmm5);

		xmm1 = _mm_load_si128(&xmm0);			// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);			// prepare next 4-bit
		xmm1 = _mm_and_si128(xmm1, xmm7);		// src & 0x000F
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);	// table look-up
		xmm1 = _mm_slli_epi16(xmm1, 8);			// shift 8-bit for higher table
		xmm4 = _mm_shuffle_epi8(xmm4, xmm1);
			xmm5 = _mm_load_si128(tbl + 6);	// low table
			xmm6 = _mm_load_si128(tbl + 7);	// high table
		xmm3 = _mm_xor_si128(xmm3, xmm4);	// combine high and low
		xmm2 = _mm_xor_si128(xmm2, xmm3);

			xmm5 = _mm_shuffle_epi8(xmm5, xmm0);	// table look-up
			xmm0 = _mm_slli_epi16(xmm0, 8);			// shift 8-bit for higher table
			xmm6 = _mm_shuffle_epi8(xmm6, xmm0);
			xmm5 = _mm_xor_si128(xmm5, xmm6);	// combine high and low
			xmm2 = _mm_xor_si128(xmm2, xmm5);

		_mm_store_si128(dst, xmm2);

		src += 1;
		dst += 1;
		bsize -= 16;
	}
}

// その場で 32バイトごとに並び替えて、計算後に戻す方法（50% faster than 16-byte version)
// なぜか asm を使わない方が速い!? 32-bit と 64-bit の両方で使える
// xmm レジスタを 8個までしか使わない方が 32-bit 版で速いし安定する
// Address (input) does not need be 16-byte aligned
static void gf16_ssse3_block32u(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;
	__m128i tbl0, tbl1, tbl2, tbl3, tbl4, tbl5, tbl6, tbl7;

	// copy tables to local
	tbl0 = _mm_load_si128((__m128i *)table);
	tbl1 = _mm_load_si128((__m128i *)table + 1);
	tbl2 = _mm_load_si128((__m128i *)table + 2);
	tbl3 = _mm_load_si128((__m128i *)table + 3);
	tbl4 = _mm_load_si128((__m128i *)table + 4);
	tbl5 = _mm_load_si128((__m128i *)table + 5);
	tbl6 = _mm_load_si128((__m128i *)table + 6);
	tbl7 = _mm_load_si128((__m128i *)table + 7);

	// create mask for 16 entries
	xmm7 = _mm_setzero_si128();
	xmm7 = _mm_cmpeq_epi16(xmm7, xmm7);	// 0xFFFF *8
	xmm6 = _mm_srli_epi16(xmm7, 8);		// 0x00FF *8
	xmm7 = _mm_srli_epi16(xmm7, 12);	// 0x000F *8
	xmm7 = _mm_packus_epi16(xmm7, xmm7);	// 0x0F *16

	while (bsize != 0){
		xmm1 = _mm_loadu_si128((__m128i *)input);	// read source 32-bytes
		xmm3 = _mm_loadu_si128((__m128i *)input + 1);
		xmm0 = _mm_and_si128(xmm1, xmm6);	// erase higher byte
		xmm2 = _mm_and_si128(xmm3, xmm6);
		xmm1 = _mm_srli_epi16(xmm1, 8);		// move higher byte to lower
		xmm3 = _mm_srli_epi16(xmm3, 8);
		xmm0 = _mm_packus_epi16(xmm0, xmm2);	//  select lower byte of each word
		xmm1 = _mm_packus_epi16(xmm1, xmm3);	//  select higher byte of each word

		xmm4 = _mm_load_si128(&tbl0);	// load tables
		xmm5 = _mm_load_si128(&tbl1);
		xmm3 = _mm_and_si128(xmm0, xmm7);	// src & 0x0F
		xmm0 = _mm_and_si128(_mm_srli_epi16(xmm0, 4), xmm7);	// (src >> 4) & 0x0F
		xmm4 = _mm_shuffle_epi8(xmm4, xmm3);	// table look-up
		xmm5 = _mm_shuffle_epi8(xmm5, xmm3);

		xmm2 = _mm_load_si128(&tbl2);	// load tables
		xmm3 = _mm_load_si128(&tbl3);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm2 = _mm_load_si128(&tbl4);	// load tables
		xmm3 = _mm_load_si128(&tbl5);
		xmm0 = _mm_and_si128(xmm1, xmm7);	// src & 0x0F
		xmm1 = _mm_and_si128(_mm_srli_epi16(xmm1, 4), xmm7);	// (src >> 4) & 0x0F
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm2 = _mm_load_si128(&tbl6);	// load tables
		xmm3 = _mm_load_si128(&tbl7);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm1);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm0 = _mm_load_si128((__m128i *)output);	// read dest 32-bytes
		xmm1 = _mm_load_si128((__m128i *)output + 1);
		xmm3 = _mm_unpacklo_epi8(xmm4, xmm5);	// interleave lower and higher bytes
		xmm4 = _mm_unpackhi_epi8(xmm4, xmm5);
		xmm0 = _mm_xor_si128(xmm0, xmm3);
		xmm1 = _mm_xor_si128(xmm1, xmm4);
		_mm_store_si128((__m128i *)output, xmm0);	// write dest 32-bytes
		_mm_store_si128((__m128i *)output + 1, xmm1);

		input += 32;
		output += 32;
		bsize -= 32;
	}
}

// xmm レジスタにテーブルを読み込む方が 64-bit 版で微妙に速い
static void gf16_ssse3_block32_altmap(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm7;
	__m128i tbl0, tbl1, tbl2, tbl3, tbl4, tbl5, tbl6, tbl7;

	// copy tables to local
	tbl0 = _mm_load_si128((__m128i *)table);
	tbl1 = _mm_load_si128((__m128i *)table + 1);
	tbl2 = _mm_load_si128((__m128i *)table + 2);
	tbl3 = _mm_load_si128((__m128i *)table + 3);
	tbl4 = _mm_load_si128((__m128i *)table + 4);
	tbl5 = _mm_load_si128((__m128i *)table + 5);
	tbl6 = _mm_load_si128((__m128i *)table + 6);
	tbl7 = _mm_load_si128((__m128i *)table + 7);

	// create mask for 16 entries
	xmm7 = _mm_setzero_si128();
	xmm7 = _mm_cmpeq_epi16(xmm7, xmm7);	// 0xFFFF *8
	xmm7 = _mm_srli_epi16(xmm7, 12);	// 0x000F *8
	xmm7 = _mm_packus_epi16(xmm7, xmm7);	// 0x0F *16

	while (bsize != 0){
		xmm0 = _mm_load_si128((__m128i *)input);	// read source 32-bytes
		xmm1 = _mm_load_si128((__m128i *)input + 1);

		xmm3 = _mm_load_si128(&xmm0);	// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
		xmm3 = _mm_and_si128(xmm3, xmm7);	// src & 0x0F
		xmm0 = _mm_and_si128(xmm0, xmm7);	// (src >> 4) & 0x0F

		xmm4 = _mm_load_si128(&tbl0);	// load tables
		xmm5 = _mm_load_si128(&tbl1);
		xmm4 = _mm_shuffle_epi8(xmm4, xmm3);	// table look-up
		xmm5 = _mm_shuffle_epi8(xmm5, xmm3);

		xmm2 = _mm_load_si128(&tbl2);	// load tables
		xmm3 = _mm_load_si128(&tbl3);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm0 = _mm_load_si128(&xmm1);	// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
		xmm1 = _mm_and_si128(xmm1, xmm7);	// src & 0x0F
		xmm0 = _mm_and_si128(xmm0, xmm7);	// (src >> 4) & 0x0F

		xmm2 = _mm_load_si128(&tbl4);	// load tables
		xmm3 = _mm_load_si128(&tbl5);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm1);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm2 = _mm_load_si128(&tbl6);	// load tables
		xmm3 = _mm_load_si128(&tbl7);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);

		xmm0 = _mm_load_si128((__m128i *)output);	// read dest 32-bytes
		xmm1 = _mm_load_si128((__m128i *)output + 1);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);
		xmm4 = _mm_xor_si128(xmm4, xmm0);
		xmm5 = _mm_xor_si128(xmm5, xmm1);
		_mm_store_si128((__m128i *)output, xmm4);	// write dest 32-bytes
		_mm_store_si128((__m128i *)output + 1, xmm5);

		input += 32;
		output += 32;
		bsize -= 32;
	}
}

/*
static void gf16_ssse3_block32_altmap(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m128i *src, *dst, *tbl;
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm7;

	src = (__m128i *)input;
	dst = (__m128i *)output;
	tbl = (__m128i *)table;

	// create mask for 16 entries
	xmm7 = _mm_setzero_si128();
	xmm7 = _mm_cmpeq_epi16(xmm7, xmm7);	// 0xFFFF *8
	xmm7 = _mm_srli_epi16(xmm7, 12);	// 0x000F *8
	xmm7 = _mm_packus_epi16(xmm7, xmm7);	// 0x0F *16

	while (bsize != 0){
		xmm0 = _mm_load_si128(src);	// read source 32-bytes
		xmm1 = _mm_load_si128(src + 1);

		xmm3 = _mm_load_si128(&xmm0);	// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
		xmm3 = _mm_and_si128(xmm3, xmm7);	// src & 0x0F
		xmm0 = _mm_and_si128(xmm0, xmm7);	// (src >> 4) & 0x0F

		xmm4 = _mm_load_si128(tbl);	// load tables
		xmm5 = _mm_load_si128(tbl + 1);
		xmm4 = _mm_shuffle_epi8(xmm4, xmm3);	// table look-up
		xmm5 = _mm_shuffle_epi8(xmm5, xmm3);

		xmm2 = _mm_load_si128(tbl + 2);	// load tables
		xmm3 = _mm_load_si128(tbl + 3);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm0 = _mm_load_si128(&xmm1);	// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
		xmm1 = _mm_and_si128(xmm1, xmm7);	// src & 0x0F
		xmm0 = _mm_and_si128(xmm0, xmm7);	// (src >> 4) & 0x0F

		xmm2 = _mm_load_si128(tbl + 4);	// load tables
		xmm3 = _mm_load_si128(tbl + 5);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm1);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm2 = _mm_load_si128(tbl + 6);	// load tables
		xmm3 = _mm_load_si128(tbl + 7);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);

		xmm0 = _mm_load_si128(dst);	// read dest 32-bytes
		xmm1 = _mm_load_si128(dst + 1);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);
		xmm4 = _mm_xor_si128(xmm4, xmm0);
		xmm5 = _mm_xor_si128(xmm5, xmm1);
		_mm_store_si128(dst, xmm4);	// write dest 32-bytes
		_mm_store_si128(dst + 1, xmm5);

		src += 2;
		dst += 2;
		bsize -= 32;
	}
}
*/

#endif

// 逆行列計算用に掛け算だけする（XORで追加しない）
static void gf16_ssse3_block16s(unsigned char *data, unsigned int bsize, unsigned char *table)
{
	__m128i dest, mask, xmm0, xmm1, xmm3, xmm4, xmm5, xmm6;
	__m128i tbl0, tbl1, tbl2, tbl3, tbl4, tbl5, tbl6, tbl7;

	// copy tables to local
	tbl0 = _mm_load_si128((__m128i *)table);
	tbl1 = _mm_load_si128((__m128i *)table + 1);
	tbl2 = _mm_load_si128((__m128i *)table + 2);
	tbl3 = _mm_load_si128((__m128i *)table + 3);
	tbl4 = _mm_load_si128((__m128i *)table + 4);
	tbl5 = _mm_load_si128((__m128i *)table + 5);
	tbl6 = _mm_load_si128((__m128i *)table + 6);
	tbl7 = _mm_load_si128((__m128i *)table + 7);

	// create mask for 8 entries
	mask = _mm_setzero_si128();
	mask = _mm_cmpeq_epi16(mask, mask);	// 0xFFFF *8
	mask = _mm_srli_epi16(mask, 12);	// 0x000F *8

	while (bsize != 0){
		xmm0 = _mm_load_si128((__m128i *)data);	// read source 16-bytes

		xmm3 = _mm_load_si128(&tbl0);	// low table
		xmm4 = _mm_load_si128(&tbl1);	// high table
		xmm1 = _mm_load_si128(&xmm0);			// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);			// prepare next 4-bit
		xmm1 = _mm_and_si128(xmm1, mask);		// src & 0x000F
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);	// table look-up
		xmm1 = _mm_slli_epi16(xmm1, 8);			// shift 8-bit for higher table
		xmm4 = _mm_shuffle_epi8(xmm4, xmm1);
			xmm5 = _mm_load_si128(&tbl2);	// low table
			xmm6 = _mm_load_si128(&tbl3);	// high table
		dest = _mm_xor_si128(xmm3, xmm4);	// combine high and low

			xmm1 = _mm_load_si128(&xmm0);			// copy source
			xmm0 = _mm_srli_epi16(xmm0, 4);			// prepare next 4-bit
			xmm1 = _mm_and_si128(xmm1, mask);		// src & 0x000F
			xmm5 = _mm_shuffle_epi8(xmm5, xmm1);	// table look-up
			xmm1 = _mm_slli_epi16(xmm1, 8);			// shift 8-bit for higher table
			xmm6 = _mm_shuffle_epi8(xmm6, xmm1);
		xmm3 = _mm_load_si128(&tbl4);	// low table
		xmm4 = _mm_load_si128(&tbl5);	// high table
			xmm5 = _mm_xor_si128(xmm5, xmm6);	// combine high and low
			dest = _mm_xor_si128(dest, xmm5);

		xmm1 = _mm_load_si128(&xmm0);			// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);			// prepare next 4-bit
		xmm1 = _mm_and_si128(xmm1, mask);		// src & 0x000F
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);	// table look-up
		xmm1 = _mm_slli_epi16(xmm1, 8);			// shift 8-bit for higher table
		xmm4 = _mm_shuffle_epi8(xmm4, xmm1);
			xmm5 = _mm_load_si128(&tbl6);	// low table
			xmm6 = _mm_load_si128(&tbl7);	// high table
		xmm3 = _mm_xor_si128(xmm3, xmm4);	// combine high and low
		dest = _mm_xor_si128(dest, xmm3);

			xmm5 = _mm_shuffle_epi8(xmm5, xmm0);	// table look-up
			xmm0 = _mm_slli_epi16(xmm0, 8);			// shift 8-bit for higher table
			xmm6 = _mm_shuffle_epi8(xmm6, xmm0);
			xmm5 = _mm_xor_si128(xmm5, xmm6);	// combine high and low
			dest = _mm_xor_si128(dest, xmm5);

		_mm_store_si128((__m128i *)data, dest);

		data += 16;
		bsize -= 16;
	}
}

// ２ブロック同時に計算することで、メモリーへのアクセス回数を減らす
// 128バイトのテーブルを２個用意しておくこと
// xmm レジスタの数が足りないので、テーブルを毎回ロードする
static void gf16_ssse3_block32_altmap2(unsigned char *input1, unsigned char *input2, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m128i *tbl;
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, mask;

	tbl = (__m128i *)table;

	// create mask for 16 entries
	mask = _mm_setzero_si128();
	mask = _mm_cmpeq_epi16(mask, mask);	// 0xFFFF *8
	mask = _mm_srli_epi16(mask, 12);	// 0x000F *8
	mask = _mm_packus_epi16(mask, mask);	// 0x0F *16

	while (bsize != 0){
		xmm0 = _mm_load_si128((__m128i *)input1);	// read source 32-bytes
		xmm1 = _mm_load_si128((__m128i *)input1 + 1);

		xmm6 = _mm_load_si128(&xmm0);	// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
		xmm6 = _mm_and_si128(xmm6, mask);	// src & 0x0F
		xmm0 = _mm_and_si128(xmm0, mask);	// (src >> 4) & 0x0F

		xmm4 = _mm_load_si128(tbl);	// load tables
		xmm5 = _mm_load_si128(tbl + 1);
		xmm4 = _mm_shuffle_epi8(xmm4, xmm6);	// table look-up
		xmm5 = _mm_shuffle_epi8(xmm5, xmm6);

		xmm2 = _mm_load_si128(tbl + 2);	// load tables
		xmm3 = _mm_load_si128(tbl + 3);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm0 = _mm_load_si128(&xmm1);	// copy source
		xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
		xmm1 = _mm_and_si128(xmm1, mask);	// src & 0x0F
		xmm0 = _mm_and_si128(xmm0, mask);	// (src >> 4) & 0x0F

		xmm2 = _mm_load_si128(tbl + 4);	// load tables
		xmm3 = _mm_load_si128(tbl + 5);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm1);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm1);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm2 = _mm_load_si128(tbl + 6);	// load tables
		xmm3 = _mm_load_si128(tbl + 7);
		xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
		xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
		xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
		xmm5 = _mm_xor_si128(xmm5, xmm3);

			xmm0 = _mm_load_si128((__m128i *)input2);	// read source 32-bytes
			xmm1 = _mm_load_si128((__m128i *)input2 + 1);

			xmm6 = _mm_load_si128(&xmm0);	// copy source
			xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
			xmm6 = _mm_and_si128(xmm6, mask);	// src & 0x0F
			xmm0 = _mm_and_si128(xmm0, mask);	// (src >> 4) & 0x0F

			xmm2 = _mm_load_si128(tbl + 8);	// load tables
			xmm3 = _mm_load_si128(tbl + 9);
			xmm2 = _mm_shuffle_epi8(xmm2, xmm6);	// table look-up
			xmm3 = _mm_shuffle_epi8(xmm3, xmm6);
			xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
			xmm5 = _mm_xor_si128(xmm5, xmm3);

			xmm2 = _mm_load_si128(tbl + 10);	// load tables
			xmm3 = _mm_load_si128(tbl + 11);
			xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
			xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
			xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
			xmm5 = _mm_xor_si128(xmm5, xmm3);

			xmm0 = _mm_load_si128(&xmm1);	// copy source
			xmm0 = _mm_srli_epi16(xmm0, 4);	// prepare next 4-bit
			xmm1 = _mm_and_si128(xmm1, mask);	// src & 0x0F
			xmm0 = _mm_and_si128(xmm0, mask);	// (src >> 4) & 0x0F

			xmm2 = _mm_load_si128(tbl + 12);	// load tables
			xmm3 = _mm_load_si128(tbl + 13);
			xmm2 = _mm_shuffle_epi8(xmm2, xmm1);	// table look-up
			xmm3 = _mm_shuffle_epi8(xmm3, xmm1);
			xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
			xmm5 = _mm_xor_si128(xmm5, xmm3);

			xmm2 = _mm_load_si128(tbl + 14);	// load tables
			xmm3 = _mm_load_si128(tbl + 15);
			xmm2 = _mm_shuffle_epi8(xmm2, xmm0);	// table look-up
			xmm3 = _mm_shuffle_epi8(xmm3, xmm0);
			xmm4 = _mm_xor_si128(xmm4, xmm2);	// combine result
			xmm5 = _mm_xor_si128(xmm5, xmm3);

		xmm0 = _mm_load_si128((__m128i *)output);	// read dest 32-bytes
		xmm1 = _mm_load_si128((__m128i *)output + 1);
		xmm0 = _mm_xor_si128(xmm0, xmm4);
		xmm1 = _mm_xor_si128(xmm1, xmm5);
		_mm_store_si128((__m128i *)output, xmm0);	// write dest 32-bytes
		_mm_store_si128((__m128i *)output + 1, xmm1);

		input1 += 32;
		input2 += 32;
		output += 32;
		bsize -= 32;
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// AVX2 命令を使うには Windows 7 以降じゃないといけない

// _mm256_permute2x128_si256 の control の意味は以下を参照
// http://www.felixcloutier.com/x86/VPERM2I128.html

// AVX2 を使って全体を２倍していくと、13% ぐらい速くなる
// でも、テーブル作成が少し速くなっても、全体的な速度はほとんど変わらない・・・
static void create_eight_table_avx2(unsigned char *mtab, int factor)
{
	int count;
	__m128i xmm0, xmm1, xmm2, xmm3, mask8;
	__m256i ymm0, ymm1, ymm2, ymm3, base, poly, mask16;

	// create mask for 8-bit
	mask8 = _mm_setzero_si128();
	mask8 = _mm_cmpeq_epi16(mask8, mask8);	// 0xFFFF *8
	mask8 = _mm_srli_epi16(mask8, 8);		// 0x00FF *8

	xmm0 = _mm_cvtsi32_si128(factor);			// [_][_][_][_][_][_][_][1]
	xmm1 = _mm_setzero_si128();
	factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
	xmm1 = _mm_insert_epi16(xmm1, factor, 1);	// [_][_][_][_][_][_][2][_]
	xmm2 = _mm_setzero_si128();
	factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
	xmm2 = _mm_insert_epi16(xmm2, factor, 4);	// [_][_][_][4][_][_][_][_]
	xmm1 = _mm_unpacklo_epi16(xmm1, xmm1);		// [_][_][_][_][2][2][_][_]
	factor = (factor << 1) ^ (((factor << 16) >> 31) & 0x1100B);
	xmm3 = _mm_cvtsi32_si128(factor);			// [_][_][_][_][_][_][_][8]

	xmm0 = _mm_shufflelo_epi16(xmm0, _MM_SHUFFLE(0, 1, 0, 1));	// [_][_][_][_][1][_][1][_]
	xmm3 = _mm_unpacklo_epi16(xmm3, xmm3);						// [_][_][_][_][_][_][8][8]
	xmm0 = _mm_xor_si128(xmm0, xmm1);							// [_][_][_][_][3][2][1][_]
	xmm2 = _mm_shufflehi_epi16(xmm2, _MM_SHUFFLE(0, 0, 0, 0));	// [4][4][4][4][_][_][_][_]
	xmm0 = _mm_unpacklo_epi64(xmm0, xmm0);						// [3][2][1][_][3][2][1][_]
	xmm3 = _mm_shuffle_epi32(xmm3, _MM_SHUFFLE(0, 0, 0, 0));	// [8][8][8][8][8][8][8][8]
	xmm2 = _mm_xor_si128(xmm2, xmm0);							// [7][6][5][4][3][2][1][_]
	xmm3 = _mm_xor_si128(xmm3, xmm2);							// [15][14][13][12][11][10][9][8]

	// 途中で AVX2 命令を使っても遅くならないっぽい
	poly = _mm256_set1_epi32(0x100B100B);	// PRIM_POLY = 0x1100B * 16
	mask16 = _mm256_cmpeq_epi16(poly, poly);
	mask16 = _mm256_srli_epi16(mask16, 8);	// 0x00FF *16
	base = _mm256_setzero_si256();
	base = _mm256_inserti128_si256(base, xmm2, 0);
	base = _mm256_inserti128_si256(base, xmm3, 1);

	// ymm レジスタに読み込んでる間にメモリーに書き込んだ方が速い
	xmm0 = _mm_and_si128(xmm2, mask8);
	xmm1 = _mm_and_si128(xmm3, mask8);
	xmm0 = _mm_packus_epi16(xmm0, xmm1);	// lower 8-bit * 16
	xmm2 = _mm_srli_epi16(xmm2, 8);
	xmm3 = _mm_srli_epi16(xmm3, 8);
	xmm2 = _mm_packus_epi16(xmm2, xmm3);	// higher 8-bit * 16
	_mm_store_si128((__m128i *)mtab    , xmm0);
	_mm_store_si128((__m128i *)mtab + 1, xmm2);

	for (count = 1; count < 4; count++){
		// 全体を２倍する
		ymm0 = _mm256_slli_epi16(base, 1);
		ymm1 = _mm256_srai_epi16(base, 15);
		ymm1 = _mm256_and_si256(ymm1, poly);
		base = _mm256_xor_si256(ymm1, ymm0);

		// 全体を２倍する
		ymm0 = _mm256_slli_epi16(base, 1);
		ymm1 = _mm256_srai_epi16(base, 15);
		ymm1 = _mm256_and_si256(ymm1, poly);
		base = _mm256_xor_si256(ymm1, ymm0);

		// 全体を２倍する
		ymm0 = _mm256_slli_epi16(base, 1);
		ymm1 = _mm256_srai_epi16(base, 15);
		ymm1 = _mm256_and_si256(ymm1, poly);
		base = _mm256_xor_si256(ymm1, ymm0);

		// 全体を２倍する
		ymm0 = _mm256_slli_epi16(base, 1);
		ymm1 = _mm256_srai_epi16(base, 15);
		ymm1 = _mm256_and_si256(ymm1, poly);
		base = _mm256_xor_si256(ymm1, ymm0);

		// 並び替えて保存する
		ymm0 = _mm256_and_si256(base, mask16);	// lower 8-bit * 16
		ymm1 = _mm256_srli_epi16(base, 8);		// higher 8-bit * 16
		ymm2 = _mm256_permute2x128_si256(ymm0, ymm1, 0x20);
		ymm3 = _mm256_permute2x128_si256(ymm0, ymm1, 0x31);
		ymm0 = _mm256_packus_epi16(ymm2, ymm3);
		_mm256_store_si256((__m256i *)mtab + count, ymm0);
	}

	// AVX-SSE 切り替えの回避
	_mm256_zeroupper();
}

// 逆行列計算用に掛け算だけする（XORで追加しない）
static void gf16_avx2_block32s(unsigned char *data, unsigned int bsize, unsigned char *table)
{
	__m256i tbl0, tbl1, tbl2, tbl3, tbl4, tbl5, tbl6, tbl7;
	__m256i mask, dest, src0, src1, tmp0, tmp1, tmp2, tmp3;

	// copy tables to local
	tmp0 = _mm256_load_si256((__m256i *)table);		// tbl0[low0][high0] <- 0x0f[lo][lo]
	tmp1 = _mm256_load_si256((__m256i *)table + 1);	// tbl1[low1][high1] <- 0xf0[lo][lo]
	tmp2 = _mm256_load_si256((__m256i *)table + 2);	// tbl2[low2][high2] <- 0x0f[hi][hi]
	tmp3 = _mm256_load_si256((__m256i *)table + 3);	// tbl3[low3][high3] <- 0xf0[hi][hi]

	// split to 8 tables
	tbl0 = _mm256_permute2x128_si256(tmp0, tmp0, 0x00);	// tbl0[low0][low0]
	tbl1 = _mm256_permute2x128_si256(tmp1, tmp1, 0x00);	// tbl1[low1][low1]
	tbl2 = _mm256_permute2x128_si256(tmp2, tmp2, 0x00);	// tbl2[low2][low2]
	tbl3 = _mm256_permute2x128_si256(tmp3, tmp3, 0x00);	// tbl3[low3][low3]
	tbl4 = _mm256_permute2x128_si256(tmp0, tmp0, 0x11);	// tbl0[high0][high0]
	tbl5 = _mm256_permute2x128_si256(tmp1, tmp1, 0x11);	// tbl1[high1][high1]
	tbl6 = _mm256_permute2x128_si256(tmp2, tmp2, 0x11);	// tbl2[high2][high2]
	tbl7 = _mm256_permute2x128_si256(tmp3, tmp3, 0x11);	// tbl3[high3][high3]

	// create mask for 16 entries
	mask = _mm256_cmpeq_epi16(tmp0, tmp0);	// 0xFFFF *16
	mask = _mm256_srli_epi16(mask, 12);		// 0x000F *16

	while (bsize != 0){
		src0 = _mm256_load_si256((__m256i *)data);	// read source 32-bytes

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl0, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl4, src1);
		src0 = _mm256_srli_epi16(src0, 4);		// prepare next 4-bit
		dest = _mm256_xor_si256(tmp0, tmp1);	// combine high and low

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl1, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl5, src1);
		dest = _mm256_xor_si256(dest, tmp0);	// combine high and low
		src0 = _mm256_srli_epi16(src0, 4);		// prepare next 4-bit
		dest = _mm256_xor_si256(dest, tmp1);

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl2, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl6, src1);
		dest = _mm256_xor_si256(dest, tmp0);	// combine high and low
		src0 = _mm256_srli_epi16(src0, 4);		// prepare next 4-bit
		dest = _mm256_xor_si256(dest, tmp1);

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl3, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl7, src1);
		dest = _mm256_xor_si256(dest, tmp0);	// combine high and low
		dest = _mm256_xor_si256(dest, tmp1);

		_mm256_store_si256((__m256i *)data, dest);	// write dest 32-bytes

		data += 32;
		bsize -= 32;
	}

	// AVX-SSE 切り替えの回避
	_mm256_zeroupper();
}

// 逆行列計算用に ALTMAP されてないソースにも対応しておく
// Address (input) does not need be 32-byte aligned
static void gf16_avx2_block32u(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m256i tbl0, tbl1, tbl2, tbl3, tbl4, tbl5, tbl6, tbl7;
	__m256i mask, dest, src0, src1, tmp0, tmp1, tmp2, tmp3;

	// copy tables to local
	tmp0 = _mm256_load_si256((__m256i *)table);		// tbl0[low0][high0] <- 0x0f[lo][lo]
	tmp1 = _mm256_load_si256((__m256i *)table + 1);	// tbl1[low1][high1] <- 0xf0[lo][lo]
	tmp2 = _mm256_load_si256((__m256i *)table + 2);	// tbl2[low2][high2] <- 0x0f[hi][hi]
	tmp3 = _mm256_load_si256((__m256i *)table + 3);	// tbl3[low3][high3] <- 0xf0[hi][hi]

	// split to 8 tables
	tbl0 = _mm256_permute2x128_si256(tmp0, tmp0, 0x00);	// tbl0[low0][low0]
	tbl1 = _mm256_permute2x128_si256(tmp1, tmp1, 0x00);	// tbl1[low1][low1]
	tbl2 = _mm256_permute2x128_si256(tmp2, tmp2, 0x00);	// tbl2[low2][low2]
	tbl3 = _mm256_permute2x128_si256(tmp3, tmp3, 0x00);	// tbl3[low3][low3]
	tbl4 = _mm256_permute2x128_si256(tmp0, tmp0, 0x11);	// tbl0[high0][high0]
	tbl5 = _mm256_permute2x128_si256(tmp1, tmp1, 0x11);	// tbl1[high1][high1]
	tbl6 = _mm256_permute2x128_si256(tmp2, tmp2, 0x11);	// tbl2[high2][high2]
	tbl7 = _mm256_permute2x128_si256(tmp3, tmp3, 0x11);	// tbl3[high3][high3]

	// create mask for 16 entries
	mask = _mm256_cmpeq_epi16(tmp0, tmp0);	// 0xFFFF *16
	mask = _mm256_srli_epi16(mask, 12);		// 0x000F *16

	while (bsize != 0){
		src0 = _mm256_loadu_si256((__m256i *)input);	// read source 32-bytes
		dest = _mm256_load_si256((__m256i *)output);	// read dest 32-bytes

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl0, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl4, src1);
		dest = _mm256_xor_si256(dest, tmp0);	// combine high and low
		src0 = _mm256_srli_epi16(src0, 4);		// prepare next 4-bit
		dest = _mm256_xor_si256(dest, tmp1);

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl1, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl5, src1);
		dest = _mm256_xor_si256(dest, tmp0);	// combine high and low
		src0 = _mm256_srli_epi16(src0, 4);		// prepare next 4-bit
		dest = _mm256_xor_si256(dest, tmp1);

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl2, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl6, src1);
		dest = _mm256_xor_si256(dest, tmp0);	// combine high and low
		src0 = _mm256_srli_epi16(src0, 4);		// prepare next 4-bit
		dest = _mm256_xor_si256(dest, tmp1);

		src1 = _mm256_and_si256(src0, mask);	// src & 0x0F
		tmp0 = _mm256_shuffle_epi8(tbl3, src1);	// table look-up
		src1 = _mm256_slli_epi16(src1, 8);		// shift 8-bit for higher table
		tmp1 = _mm256_shuffle_epi8(tbl7, src1);
		dest = _mm256_xor_si256(dest, tmp0);	// combine high and low
		dest = _mm256_xor_si256(dest, tmp1);

		_mm256_store_si256((__m256i *)output, dest);	// write dest 32-bytes

		input += 32;
		output += 32;
		bsize -= 32;
	}

	// AVX-SSE 切り替えの回避
	_mm256_zeroupper();
}

// テーブルを並び替えて使えば、ループ内の並び替え回数を一回に減らせる
static void gf16_avx2_block32(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m256i tbl0, tbl1, tbl2, tbl3, mask, dest, src0, src1, tmp0, tmp1, tmp2, tmp3;

	// copy tables to local
	tmp0 = _mm256_load_si256((__m256i *)table);		// tbl0[low0][high0] <- 0x0f[lo][lo]
	tmp1 = _mm256_load_si256((__m256i *)table + 1);	// tbl1[low1][high1] <- 0xf0[lo][lo]
	tmp2 = _mm256_load_si256((__m256i *)table + 2);	// tbl2[low2][high2] <- 0x0f[hi][hi]
	tmp3 = _mm256_load_si256((__m256i *)table + 3);	// tbl3[low3][high3] <- 0xf0[hi][hi]

	// re-arrange table order (permute より blend の方が速いらしい)
	tbl0 = _mm256_blend_epi32(tmp0, tmp2, 0xF0);		// tbl0[low0][high2] <- 0x0f[lo][hi]
	tbl1 = _mm256_blend_epi32(tmp1, tmp3, 0xF0);		// tbl1[low1][high3] <- 0xf0[lo][hi]
	tbl2 = _mm256_permute2x128_si256(tmp2, tmp0, 0x03);	// tbl2[high0][low2] <- 0x0f[lo][hi]
	tbl3 = _mm256_permute2x128_si256(tmp3, tmp1, 0x03);	// tbl3[high1][low3] <- 0xf0[lo][hi]

	// create mask for 32 entries
	mask = _mm256_cmpeq_epi16(tmp0, tmp0);	// 0xFFFF *16
	mask = _mm256_srli_epi16(mask, 12);		// 0x000F *16
	mask = _mm256_packus_epi16(mask, mask);	// 0x0F *32

	while (bsize != 0){
		src0 = _mm256_load_si256((__m256i *)input);	// read source 32-bytes
		src1 = _mm256_srli_epi16(src0, 4);	// prepare next 4-bit
		src0 = _mm256_and_si256(src0, mask);	// src & 0x0F
		src1 = _mm256_and_si256(src1, mask);	// (src >> 4) & 0x0F

		tmp0 = _mm256_shuffle_epi8(tbl0, src0);	// table look-up
		tmp1 = _mm256_shuffle_epi8(tbl1, src1);
		tmp2 = _mm256_shuffle_epi8(tbl2, src0);
		tmp3 = _mm256_shuffle_epi8(tbl3, src1);

		tmp0 = _mm256_xor_si256(tmp0, tmp1);	// combine result
		tmp2 = _mm256_xor_si256(tmp2, tmp3);
		tmp2 = _mm256_permute2x128_si256(tmp2, tmp2, 0x01);	// exchange low & high 128-bit

		dest = _mm256_load_si256((__m256i *)output);	// read dest 32-bytes
		tmp0 = _mm256_xor_si256(tmp0, tmp2);
		dest = _mm256_xor_si256(dest, tmp0);
		_mm256_store_si256((__m256i *)output, dest);	// write dest 32-bytes

		input += 32;
		output += 32;
		bsize -= 32;
	}

	// AVX-SSE 切り替えの回避
	_mm256_zeroupper();
}

/*
// テーブルを並び替えて使えば、ループ内の並び替え回数を減らせる
static void gf16_avx2_block32(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m256i tbl0, tbl1, tbl2, tbl3, mask, dest, src0, src1, src2, src3;

	// copy tables to local
	src0 = _mm256_load_si256((__m256i *)table);		// tbl0[low0][high0] <- 0x0f[lo][lo]
	src1 = _mm256_load_si256((__m256i *)table + 1);	// tbl1[low1][high1] <- 0xf0[lo][lo]
	src2 = _mm256_load_si256((__m256i *)table + 2);	// tbl2[low2][high2] <- 0x0f[hi][hi]
	src3 = _mm256_load_si256((__m256i *)table + 3);	// tbl3[low3][high3] <- 0xf0[hi][hi]

	// re-arrange table order
	tbl0 = _mm256_permute2x128_si256(src0, src2, 0x30);	// tblA[low0][high2] <- 0x0f[lo][hi]
	tbl1 = _mm256_permute2x128_si256(src1, src3, 0x30);	// tblB[low1][high3] <- 0xf0[lo][hi]
	tbl2 = _mm256_permute2x128_si256(src2, src0, 0x30);	// tblC[low2][high0] <- 0x0f[hi][lo]
	tbl3 = _mm256_permute2x128_si256(src3, src1, 0x30);	// tblD[low3][high1] <- 0xf0[hi][lo]

	// create mask for 32 entries
	mask = _mm256_set1_epi8(0x0F);	// 0x0F *32

	while (bsize != 0){
		src0 = _mm256_load_si256((__m256i *)input);		// read source 32-bytes
		dest = _mm256_load_si256((__m256i *)output);	// read dest 32-bytes

		src1 = _mm256_srli_epi16(src0, 4);	// prepare next 4-bit
		src0 = _mm256_and_si256(src0, mask);	// src & 0x0F
		src1 = _mm256_and_si256(src1, mask);	// (src >> 4) & 0x0F

		src2 = _mm256_permute2x128_si256(src0, src0, 0x01);	// exchange low & high 128-bit of "src & 0x0F"
		src3 = _mm256_permute2x128_si256(src1, src1, 0x01);	// exchange low & high 128-bit of "(src >> 4) & 0x0F"

		src0 = _mm256_shuffle_epi8(tbl0, src0);	// table look-up
		src1 = _mm256_shuffle_epi8(tbl1, src1);
		src2 = _mm256_shuffle_epi8(tbl2, src2);
		src3 = _mm256_shuffle_epi8(tbl3, src3);

		src0 = _mm256_xor_si256(src0, src1);	// combine result
		src2 = _mm256_xor_si256(src2, src3);
		dest = _mm256_xor_si256(dest, src0);
		dest = _mm256_xor_si256(dest, src2);
		_mm256_store_si256((__m256i *)output, dest);	// write dest 32-bytes

		input += 32;
		output += 32;
		bsize -= 32;
	}

	// AVX-SSE 切り替えの回避
	_mm256_zeroupper();
}
*/

/*
// レジスタを大量に使って依存関係をなくせば並列処理できるかも？
static void gf16_avx2_block32(unsigned char *input, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m256i tbl0, tbl1, tbl2, tbl3, mask, dest, src0, src1, tmp0, tmp1, tmp2, tmp3;

	// copy tables to local
	tbl0 = _mm256_load_si256((__m256i *)table);
	tbl1 = _mm256_load_si256((__m256i *)table + 1);
	tbl2 = _mm256_load_si256((__m256i *)table + 2);
	tbl3 = _mm256_load_si256((__m256i *)table + 3);

	// create mask for 32 entries
	mask = _mm256_set1_epi8(0x0F);	// 0x0F *32

	while (bsize != 0){
		src0 = _mm256_load_si256((__m256i *)input);		// read source 32-bytes
		dest = _mm256_load_si256((__m256i *)output);	// read dest 32-bytes

		src1 = _mm256_srli_epi16(src0, 4);	// prepare next 4-bit
		src0 = _mm256_and_si256(src0, mask);	// src & 0x0F
		src1 = _mm256_and_si256(src1, mask);	// (src >> 4) & 0x0F

		tmp0 = _mm256_permute2x128_si256(src0, src0, 0x00);	// copy low 128-bit to high from "src & 0x0F"
		tmp1 = _mm256_permute2x128_si256(src1, src1, 0x00);	// copy low 128-bit to high from "(src >> 4) & 0x0F"
		tmp2 = _mm256_permute2x128_si256(src0, src0, 0x11);	// copy high 128-bit to low from "src & 0x0F"
		tmp3 = _mm256_permute2x128_si256(src1, src1, 0x11);	// copy high 128-bit to low from "(src >> 4) & 0x0F"

		tmp0 = _mm256_shuffle_epi8(tbl0, tmp0);	// table look-up
		tmp1 = _mm256_shuffle_epi8(tbl1, tmp1);
		tmp2 = _mm256_shuffle_epi8(tbl2, tmp2);
		tmp3 = _mm256_shuffle_epi8(tbl3, tmp3);

		tmp0 = _mm256_xor_si256(tmp0, tmp1);	// combine result
		tmp2 = _mm256_xor_si256(tmp2, tmp3);
		dest = _mm256_xor_si256(dest, tmp0);
		dest = _mm256_xor_si256(dest, tmp2);
		_mm256_store_si256((__m256i *)output, dest);	// write dest 32-bytes

		input += 32;
		output += 32;
		bsize -= 32;
	}

	// AVX-SSE 切り替えの回避
	_mm256_zeroupper();
}
*/

// ２ブロック同時に計算することで、メモリーへのアクセス回数を減らす
// 128バイトのテーブルを２個用意しておくこと
static void gf16_avx2_block32_2(unsigned char *input1, unsigned char *input2, unsigned char *output, unsigned int bsize, unsigned char *table)
{
	__m256i mask, src0, src1, tmp0, tmp1, tmp2, tmp3;
	__m256i tbl0, tbl1, tbl2, tbl3, tbl4, tbl5, tbl6, tbl7;

	// copy tables to local
	tmp0 = _mm256_load_si256((__m256i *)table);		// tbl0[low0][high0] <- 0x0f[lo][lo]
	tmp1 = _mm256_load_si256((__m256i *)table + 1);	// tbl1[low1][high1] <- 0xf0[lo][lo]
	tmp2 = _mm256_load_si256((__m256i *)table + 2);	// tbl2[low2][high2] <- 0x0f[hi][hi]
	tmp3 = _mm256_load_si256((__m256i *)table + 3);	// tbl3[low3][high3] <- 0xf0[hi][hi]

	// re-arrange table order (permute より blend の方が速いらしい)
	tbl0 = _mm256_blend_epi32(tmp0, tmp2, 0xF0);		// tbl0[low0][high2] <- 0x0f[lo][hi]
	tbl1 = _mm256_blend_epi32(tmp1, tmp3, 0xF0);		// tbl1[low1][high3] <- 0xf0[lo][hi]
	tbl2 = _mm256_permute2x128_si256(tmp2, tmp0, 0x03);	// tbl2[high0][low2] <- 0x0f[lo][hi]
	tbl3 = _mm256_permute2x128_si256(tmp3, tmp1, 0x03);	// tbl3[high1][low3] <- 0xf0[lo][hi]

	tmp0 = _mm256_load_si256((__m256i *)table + 4);
	tmp1 = _mm256_load_si256((__m256i *)table + 5);
	tmp2 = _mm256_load_si256((__m256i *)table + 6);
	tmp3 = _mm256_load_si256((__m256i *)table + 7);
	tbl4 = _mm256_blend_epi32(tmp0, tmp2, 0xF0);
	tbl5 = _mm256_blend_epi32(tmp1, tmp3, 0xF0);
	tbl6 = _mm256_permute2x128_si256(tmp2, tmp0, 0x03);
	tbl7 = _mm256_permute2x128_si256(tmp3, tmp1, 0x03);

	// create mask for 32 entries
	mask = _mm256_cmpeq_epi16(tmp0, tmp0);	// 0xFFFF *16
	mask = _mm256_srli_epi16(mask, 12);		// 0x000F *16
	mask = _mm256_packus_epi16(mask, mask);	// 0x0F *32

	while (bsize != 0){
		src0 = _mm256_load_si256((__m256i *)input1);	// read source 32-bytes
		src1 = _mm256_srli_epi16(src0, 4);	// prepare next 4-bit
		src0 = _mm256_and_si256(src0, mask);	// src & 0x0F
		src1 = _mm256_and_si256(src1, mask);	// (src >> 4) & 0x0F

		tmp0 = _mm256_shuffle_epi8(tbl0, src0);	// table look-up
		tmp1 = _mm256_shuffle_epi8(tbl1, src1);
		tmp2 = _mm256_shuffle_epi8(tbl2, src0);
		tmp3 = _mm256_shuffle_epi8(tbl3, src1);
		tmp0 = _mm256_xor_si256(tmp0, tmp1);	// combine result
		tmp2 = _mm256_xor_si256(tmp2, tmp3);

			src0 = _mm256_load_si256((__m256i *)input2);	// read source 32-bytes
			src1 = _mm256_srli_epi16(src0, 4);	// prepare next 4-bit
			src0 = _mm256_and_si256(src0, mask);	// src & 0x0F
			src1 = _mm256_and_si256(src1, mask);	// (src >> 4) & 0x0F

			tmp1 = _mm256_shuffle_epi8(tbl4, src0);	// table look-up
			tmp3 = _mm256_shuffle_epi8(tbl6, src0);
			tmp0 = _mm256_xor_si256(tmp0, tmp1);	// combine result
			tmp2 = _mm256_xor_si256(tmp2, tmp3);

			tmp1 = _mm256_shuffle_epi8(tbl5, src1);	// table look-up
			tmp3 = _mm256_shuffle_epi8(tbl7, src1);
			tmp0 = _mm256_xor_si256(tmp0, tmp1);	// combine result
			tmp2 = _mm256_xor_si256(tmp2, tmp3);

		src0 = _mm256_load_si256((__m256i *)output);	// read dest 32-bytes
		tmp2 = _mm256_permute2x128_si256(tmp2, tmp2, 0x01);	// exchange low & high 128-bit
		src0 = _mm256_xor_si256(src0, tmp0);
		src0 = _mm256_xor_si256(src0, tmp2);
		_mm256_store_si256((__m256i *)output, src0);	// write dest 32-bytes

		input1 += 32;
		input2 += 32;
		output += 32;
		bsize -= 32;
	}

	// AVX-SSE 切り替えの回避
	_mm256_zeroupper();
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// データを並び替えることで、メモリーアクセスを高速化する
void galois_altmap32_change(unsigned char *data, unsigned int bsize)
{
	__m128i xmm0, xmm1, xmm2, xmm3, mask;

	mask = _mm_setzero_si128();
	mask = _mm_cmpeq_epi16(mask, mask);	// 0xFFFF *8
	mask = _mm_srli_epi16(mask, 8);		// 0x00FF *8

	while (bsize != 0){
		xmm1 = _mm_load_si128((__m128i *)data);	// read 32-bytes
		xmm3 = _mm_load_si128((__m128i *)data + 1);

		xmm0 = _mm_and_si128(xmm1, mask);	// erase higher byte
		xmm2 = _mm_and_si128(xmm3, mask);
		xmm1 = _mm_srli_epi16(xmm1, 8);		// move higher byte to lower
		xmm3 = _mm_srli_epi16(xmm3, 8);
		xmm0 = _mm_packus_epi16(xmm0, xmm2);	//  select lower byte of each word
		xmm1 = _mm_packus_epi16(xmm1, xmm3);	//  select higher byte of each word

		_mm_store_si128((__m128i *)data, xmm0);	// write 32-bytes
		_mm_store_si128((__m128i *)data + 1, xmm1);

		data += 32;
		bsize -= 32;
	}
}

// データの並びを元に戻す
void galois_altmap32_return(unsigned char *data, unsigned int bsize)
{
	__m128i xmm0, xmm1, xmm2;

	while (bsize != 0){
		xmm1 = _mm_load_si128((__m128i *)data);	// read 32-bytes
		xmm2 = _mm_load_si128((__m128i *)data + 1);

		xmm0 = _mm_unpacklo_epi8(xmm1, xmm2);	// interleave lower and higher bytes
		xmm1 = _mm_unpackhi_epi8(xmm1, xmm2);

		_mm_store_si128((__m128i *)data, xmm0);	// write 32-bytes
		_mm_store_si128((__m128i *)data + 1, xmm1);

		data += 32;
		bsize -= 32;
	}
}

// 並び替えない場合
void galois_altmap_none(unsigned char *data, unsigned int bsize)
{
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*
from ParPar; "gf_w16_additions.c"
gf_w16_xor_lazy_sse_altmap_multiply_region
gf_w16_xor_lazy_sse_jit_altmap_multiply_region
*/

// 256バイトごとにビット単位の XOR で計算する方法
// input と output の領域は重ならないようにすること
static void gf16_sse2_block256(unsigned char *input, unsigned char *output, unsigned int bsize, int factor)
{
	unsigned int i, bit;
	unsigned int counts[16];
	uintptr_t deptable[16][16];
	__m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
	unsigned short tmp_depmask[16];

	// calculate dependent bits
	addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
	addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);

	// duplicate each bit in the polynomial 16 times
	polymask2 = _mm_set1_epi16(PRIM_POLY & 0xFFFF); // chop off top bit, although not really necessary
	polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 8, 1<< 9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15));
	polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 0, 1<< 1, 1<< 2, 1<< 3, 1<< 4, 1<< 5, 1<< 6, 1<< 7));
	polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
	polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);

	if (factor & (1<<15)){
		// XOR
		depmask1 = addvals1;
		depmask2 = addvals2;
	} else {
		depmask1 = _mm_setzero_si128();
		depmask2 = _mm_setzero_si128();
	}
	for (i = (1<<14); i; i >>= 1){
		// rotate
		__m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
		depmask1 = _mm_insert_epi16(
			_mm_srli_si128(depmask1, 2),
			_mm_extract_epi16(depmask2, 0),
			7
		);
		depmask2 = _mm_srli_si128(depmask2, 2);

		// XOR poly
		depmask1 = _mm_xor_si128(depmask1, _mm_andnot_si128(polymask1, last));
		depmask2 = _mm_xor_si128(depmask2, _mm_andnot_si128(polymask2, last));

		if (factor & i){
			// XOR
			depmask1 = _mm_xor_si128(depmask1, addvals1);
			depmask2 = _mm_xor_si128(depmask2, addvals2);
		}
	}

	// generate needed tables
	_mm_storeu_si128((__m128i*)(tmp_depmask), depmask1);
	_mm_storeu_si128((__m128i*)(tmp_depmask + 8), depmask2);
	for (bit = 0; bit < 16; bit++){
		unsigned int cnt = 0;
		for (i = 0; i < 16; i++){
			if (tmp_depmask[bit] & (1<<i)){
				deptable[bit][cnt++] = i<<4; // pre-multiply because x86 addressing can't do a x16; this saves a shift operation later
			}
		}
		counts[bit] = cnt;
	}

	while (bsize != 0){
		#define STEP(bit) { \
			uintptr_t* deps = deptable[bit]; \
			__m128i tmp = _mm_load_si128((__m128i *)output + bit); \
			switch (counts[bit]){ \
				case 16: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[15])); \
				case 15: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[14])); \
				case 14: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[13])); \
				case 13: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[12])); \
				case 12: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[11])); \
				case 11: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[10])); \
				case 10: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 9])); \
				case  9: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 8])); \
				case  8: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 7])); \
				case  7: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 6])); \
				case  6: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 5])); \
				case  5: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 4])); \
				case  4: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 3])); \
				case  3: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 2])); \
				case  2: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 1])); \
				case  1: tmp = _mm_xor_si128(tmp, *(__m128i *)(input + deps[ 0])); \
			} \
			_mm_store_si128((__m128i *)output + bit, tmp); \
		}
		STEP( 0)
		STEP( 1)
		STEP( 2)
		STEP( 3)
		STEP( 4)
		STEP( 5)
		STEP( 6)
		STEP( 7)
		STEP( 8)
		STEP( 9)
		STEP(10)
		STEP(11)
		STEP(12)
		STEP(13)
		STEP(14)
		STEP(15)
		#undef STEP
		input += 256;
		output += 256;
		bsize -= 256;
	}
}

// bsize が 0 にならないようにすること
static void gf16_sse2_block256_jit(unsigned char *input, unsigned char *output, unsigned int bsize, int factor)
{
	FAST_U32 i, bit;
	long inBit;
	__m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
	__m128i common_mask;
	unsigned short tmp_depmask[16], common_depmask[8];
	unsigned char * pos_startloop;
	unsigned char *jit_exec, *jit_ptr;
	int j, thread_id;

	// calculate dependent bits
	addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
	addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);

	// duplicate each bit in the polynomial 16 times
	polymask2 = _mm_set1_epi16(PRIM_POLY & 0xFFFF); // chop off top bit, although not really necessary
	polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 8, 1<< 9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15));
	polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 0, 1<< 1, 1<< 2, 1<< 3, 1<< 4, 1<< 5, 1<< 6, 1<< 7));
	polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
	polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);

	if (factor & (1<<15)){
		// XOR
		depmask1 = addvals1;
		depmask2 = addvals2;
	} else {
		depmask1 = _mm_setzero_si128();
		depmask2 = _mm_setzero_si128();
	}
	for (i = (1<<14); i; i >>= 1){
		// rotate
		__m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
		depmask1 = _mm_insert_epi16(
			_mm_srli_si128(depmask1, 2),
			_mm_extract_epi16(depmask2, 0),
			7
		);
		depmask2 = _mm_srli_si128(depmask2, 2);

		// XOR poly
		depmask1 = _mm_xor_si128(depmask1, _mm_andnot_si128(polymask1, last));
		depmask2 = _mm_xor_si128(depmask2, _mm_andnot_si128(polymask2, last));

		if (factor & i){
			// XOR
			depmask1 = _mm_xor_si128(depmask1, addvals1);
			depmask2 = _mm_xor_si128(depmask2, addvals2);
		}
	}

	// attempt to remove some redundant XOR ops with a simple heuristic
	// heuristic: we just find common XOR elements between bit pairs
	{
		__m128i tmp1, tmp2;
		// first, we need to re-arrange words so that we can perform bitwise AND on neighbouring pairs
		// unfortunately, PACKUSDW is SSE4.1 only, so emulate it with shuffles
		// 01234567 -> 02461357
		tmp1 = _mm_shuffle_epi32(
			_mm_shufflelo_epi16(
				_mm_shufflehi_epi16(depmask1, 0xD8), /* 0xD8 == 0b11011000 */
				0xD8
			),
			0xD8
		);
		tmp2 = _mm_shuffle_epi32(
			_mm_shufflelo_epi16(
				_mm_shufflehi_epi16(depmask2, 0xD8),
				0xD8
			),
			0xD8
		);
		common_mask = _mm_and_si128(
			// [02461357, 8ACE9BDF] -> [02468ACE, 13579BDF]
			_mm_unpacklo_epi64(tmp1, tmp2),
			_mm_unpackhi_epi64(tmp1, tmp2)
		);
		// we have the common elements between pairs, but it doesn't make sense to process a separate queue if there's only one common element (0 XORs), so eliminate those
		common_mask = _mm_andnot_si128(_mm_cmpeq_epi16(
		_mm_setzero_si128(),
		// "(v & (v-1)) == 0" is true if only zero/one bit is set in each word
		_mm_and_si128(common_mask, _mm_sub_epi16(common_mask, _mm_set1_epi16(1)))
		), common_mask);
		// we now have a common elements mask without 1-bit words, just simply merge stuff in
		depmask1 = _mm_xor_si128(depmask1, _mm_unpacklo_epi16(common_mask, common_mask));
		depmask2 = _mm_xor_si128(depmask2, _mm_unpackhi_epi16(common_mask, common_mask));
		_mm_storeu_si128((__m128i*)common_depmask, common_mask);
	}

	_mm_storeu_si128((__m128i*)(tmp_depmask), depmask1);
	_mm_storeu_si128((__m128i*)(tmp_depmask + 8), depmask2);

	// Multi-threading だとスレッドごとに実行領域を分離しないとアクセス違反エラーが発生する
	thread_id = GetCurrentThreadId();	// 自分のスレッド ID を取得する
	for (j = 0; j < MAX_CPU; j++){	// 対応するスレッド個数は MAX_CPU 個まで
		if (jit_id[j] == thread_id)
			break;
	}
	if (j == MAX_CPU){	// 初期状態では jit_code 内は全て 0 なので jit_id も 0 だけ
		for (j = 0; j < MAX_CPU; j++){
			if (InterlockedCompareExchange(jit_id + j, thread_id, 0) == 0)	// 0と置き換えたなら
				break;
		}
	}
	jit_exec = jit_code + 4096 * j;
	jit_ptr = jit_exec;

#ifdef _WIN64
	_jit_push(&jit_ptr, BP);
	_jit_mov_r(&jit_ptr, BP, SP);
	// align pointer (avoid SP because stuff is encoded differently with it)
	_jit_mov_r(&jit_ptr, AX, SP);
	_jit_and_i(&jit_ptr, AX, 0xF);
	_jit_sub_r(&jit_ptr, BP, AX);

	// make Windows happy and save XMM6-15 registers
	// ideally should be done by this function, not JIT code, but MSVC has a convenient policy of no inline ASM
	for (i = 6; i < 16; i++)
		_jit_movaps_store(&jit_ptr, BP, -((int32_t)i-5)*16, (uint8_t)i);
#endif

	// adding 128 to the destination pointer allows the register offset to be coded in 1 byte
	// eg: 'movdqa xmm0, [rdx+0x90]' is 8 bytes, whilst 'movdqa xmm0, [rdx-0x60]' is 5 bytes
	_jit_mov_i(&jit_ptr, AX, (intptr_t)input + 128);
	_jit_mov_i(&jit_ptr, DX, (intptr_t)output + 128);
	_jit_mov_i(&jit_ptr, CX, (intptr_t)output + bsize + 128);

	_jit_align32(&jit_ptr);
	pos_startloop = jit_ptr;

	//_jit_movaps_load(reg, xreg, offs)
	// (we just save a conditional by hardcoding this)
	#define _LD_APS(xreg, mreg, offs) \
		*(int32_t*)(jit_ptr) = 0x40280F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
		jit_ptr += 4
	#define _ST_APS(mreg, offs, xreg) \
		*(int32_t*)(jit_ptr) = 0x40290F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
		jit_ptr += 4
	#define _LD_APS64(xreg, mreg, offs) \
		*(int64_t*)(jit_ptr) = 0x40280F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
		jit_ptr += 5
	#define _ST_APS64(mreg, offs, xreg) \
		*(int64_t*)(jit_ptr) = 0x40290F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
		jit_ptr += 5

#ifdef _WIN64
	#define _LD_DQA(xreg, mreg, offs) \
		*(int64_t*)(jit_ptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
		jit_ptr += 5
	#define _ST_DQA(mreg, offs, xreg) \
		*(int64_t*)(jit_ptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
		jit_ptr += 5
#else
	#define _LD_DQA(xreg, mreg, offs) \
		*(int32_t*)(jit_ptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24); \
		*(jit_ptr +4) = (uint8_t)((offs)&0xFF); \
		jit_ptr += 5
	#define _ST_DQA(mreg, offs, xreg) \
		*(int32_t*)(jit_ptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24); \
		*(jit_ptr +4) = (uint8_t)((offs)&0xFF); \
		jit_ptr += 5
#endif
	#define _LD_DQA64(xreg, mreg, offs) \
		*(int64_t*)(jit_ptr) = 0x406F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
		jit_ptr += 6
	#define _ST_DQA64(mreg, offs, xreg) \
		*(int64_t*)(jit_ptr) = 0x407F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
		jit_ptr += 6

	//_jit_xorps_m(reg, AX, offs<<4);
	#define _XORPS_M_(reg, offs, tr) \
		*(int32_t*)(jit_ptr) = (0x40570F + ((reg) << 19) + (((offs)&0xFF) <<28)) ^ (tr)
	#define _C_XORPS_M(reg, offs, c) \
		_XORPS_M_(reg, offs, 0); \
		jit_ptr += (c)<<2
	#define _XORPS_M64_(reg, offs, tr) \
		*(int64_t*)(jit_ptr) = (0x40570F44 + (((reg)-8) << 27) + ((int64_t)((offs)&0xFF) <<36)) ^ ((tr)<<8)
	#define _C_XORPS_M64(reg, offs, c) \
		_XORPS_M64_(reg, offs, 0); \
		jit_ptr += ((c)<<2)+(c)

	//_jit_pxor_m(1, AX, offs<<4);
#ifdef _WIN64
	#define _PXOR_M_(reg, offs, tr) \
		*(int64_t*)(jit_ptr) = (0x40EF0F66 + ((reg) << 27) + ((int64_t)((offs)&0xFF) << 36)) ^ (tr)
#else
	#define _PXOR_M_(reg, offs, tr) \
		*(int32_t*)(jit_ptr) = (0x40EF0F66 + ((reg) << 27)) ^ (tr); \
		*(jit_ptr +4) = (uint8_t)(((offs)&0xFF) << 4)
#endif
	#define _PXOR_M(reg, offs) \
		_PXOR_M_(reg, offs, 0); \
		jit_ptr += 5
	#define _C_PXOR_M(reg, offs, c) \
		_PXOR_M_(reg, offs, 0); \
		jit_ptr += ((c)<<2)+(c)
	#define _PXOR_M64_(reg, offs, tr) \
		*(int64_t*)(jit_ptr) = (0x40EF0F4466 + ((int64_t)((reg)-8) << 35) + ((int64_t)((offs)&0xFF) << 44)) ^ ((tr)<<8)
	#define _C_PXOR_M64(reg, offs, c) \
		_PXOR_M64_(reg, offs, 0); \
		jit_ptr += ((c)<<2)+((c)<<1)

	//_jit_xorps_r(r2, r1)
	#define _XORPS_R_(r2, r1, tr) \
		*(int32_t*)(jit_ptr) = (0xC0570F + ((r2) <<19) + ((r1) <<16)) ^ (tr)
	#define _XORPS_R(r2, r1) \
		_XORPS_R_(r2, r1, 0); \
		jit_ptr += 3
	#define _C_XORPS_R(r2, r1, c) \
		_XORPS_R_(r2, r1, 0); \
		jit_ptr += ((c)<<1)+(c)
	// r2 is always < 8, r1 here is >= 8
	#define _XORPS_R64_(r2, r1, tr) \
		*(int32_t*)(jit_ptr) = (0xC0570F41 + ((r2) <<27) + ((r1) <<24)) ^ ((tr)<<8)
	#define _C_XORPS_R64(r2, r1, c) \
		_XORPS_R64_(r2, r1, 0); \
		jit_ptr += (c)<<2

	//_jit_pxor_r(r2, r1)
	#define _PXOR_R_(r2, r1, tr) \
		*(int32_t*)(jit_ptr) = (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr)
	#define _PXOR_R(r2, r1) \
		_PXOR_R_(r2, r1, 0); \
		jit_ptr += 4
	#define _C_PXOR_R(r2, r1, c) \
		_PXOR_R_(r2, r1, 0); \
		jit_ptr += (c)<<2
	#define _PXOR_R64_(r2, r1, tr) \
		*(int64_t*)(jit_ptr) = (0xC0EF0F4166 + ((int64_t)(r2) <<35) + ((int64_t)(r1) <<32)) ^ (((int64_t)tr)<<8)
	#define _C_PXOR_R64(r2, r1, c) \
		_PXOR_R64_(r2, r1, 0); \
		jit_ptr += ((c)<<2)+(c)

	// optimised mix of xor/mov operations
	#define _MOV_OR_XOR_FP_M(reg, offs, flag, c) \
		_XORPS_M_(reg, offs, flag); \
		flag &= (c)-1; \
		jit_ptr += (c)<<2
	#define _MOV_OR_XOR_FP_M64(reg, offs, flag, c) \
		_XORPS_M64_(reg, offs, flag); \
		flag &= (c)-1; \
		jit_ptr += ((c)<<2)+(c)
	#define _MOV_OR_XOR_FP_INIT (0x570F ^ 0x280F)

	#define _MOV_OR_XOR_INT_M(reg, offs, flag, c) \
		_PXOR_M_(reg, offs, flag); \
		flag &= (c)-1; \
		jit_ptr += ((c)<<2)+(c)
	#define _MOV_OR_XOR_INT_M64(reg, offs, flag, c) \
		_PXOR_M64_(reg, offs, flag); \
		flag &= (c)-1; \
		jit_ptr += ((c)<<2)+((c)<<1)
	#define _MOV_OR_XOR_INT_INIT (0xEF0F00 ^ 0x6F0F00)

	#define _MOV_OR_XOR_R_FP(r2, r1, flag, c) \
		_XORPS_R_(r2, r1, flag); \
		flag &= (c)-1; \
		jit_ptr += ((c)<<1)+(c)
	#define _MOV_OR_XOR_R64_FP(r2, r1, flag, c) \
		_XORPS_R64_(r2, r1, flag); \
		flag &= (c)-1; \
		jit_ptr += (c)<<2

	#define _MOV_OR_XOR_R_INT(r2, r1, flag, c) \
		_PXOR_R_(r2, r1, flag); \
		flag &= (c)-1; \
		jit_ptr += (c)<<2
	#define _MOV_OR_XOR_R64_INT(r2, r1, flag, c) \
		_PXOR_R64_(r2, r1, flag); \
		flag &= (c)-1; \
		jit_ptr += ((c)<<2)+(c)

	// generate code
#ifdef _WIN64
	// preload upper 13 inputs into registers
	#define _XORS_FROM_MEMORY 3
	for (inBit = 3; inBit < 8; inBit++){
		_LD_APS(inBit, AX, (inBit-8)<<4);
	}
	for (; inBit<16; inBit++){
		_LD_APS64(inBit, AX, (inBit-8)<<4);
	}
#else
	// can only fit 5 in 32-bit mode :(
	#define _XORS_FROM_MEMORY 11
	for (inBit = 3; inBit < 8; inBit++){	// despite appearances, we're actually loading the top 5, not mid 5
		_LD_APS(inBit, AX, inBit<<4);
	}
#endif
	for (bit = 0; bit < 16; bit += 2){
		int destOffs = (int)((bit<<4)-128);
		FAST_U32 movC = _MOV_OR_XOR_INT_INIT;
		FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1], maskC = common_depmask[bit>>1];
		_LD_APS(0, DX, destOffs);
		_LD_DQA(1, DX, destOffs+16);

		for (inBit = -8; inBit < (_XORS_FROM_MEMORY-8); inBit++){
			_MOV_OR_XOR_INT_M(2, inBit, movC, maskC & 1);
			_C_XORPS_M(0, inBit, mask1 & 1);
			_C_PXOR_M(1, inBit, mask2 & 1);
			mask1 >>= 1;
			mask2 >>= 1;
			maskC >>= 1;
		}
		// at least 5 can come from registers
		for (inBit = 3; inBit < 8; inBit++){
			_MOV_OR_XOR_R_INT(2, inBit, (int32_t)movC, maskC & 1);
			_C_XORPS_R(0, inBit, mask1 & 1);
			_C_PXOR_R(1, inBit, mask2 & 1);
			mask1 >>= 1;
			mask2 >>= 1;
			maskC >>= 1;
		}
#ifdef _WIN64
		// more XORs can come from 64-bit registers
		for (inBit = 0; inBit < 8; inBit++){
			_MOV_OR_XOR_R64_INT(2, inBit, movC, maskC & 1);
			_C_XORPS_R64(0, inBit, mask1 & 1);
			_C_PXOR_R64(1, inBit, mask2 & 1);
			mask1 >>= 1;
			mask2 >>= 1;
			maskC >>= 1;
		}
#endif
		if (!movC){
			_XORPS_R(0, 2);
			_PXOR_R(1, 2);	// penalty?
		}
		_ST_APS(DX, destOffs, 0);
		_ST_DQA(DX, destOffs+16, 1);
	}
	#undef _XORS_FROM_MEMORY

	_jit_add_i(&jit_ptr, AX, 256);
	_jit_add_i(&jit_ptr, DX, 256);

	_jit_cmp_r(&jit_ptr, DX, CX);
	_jit_jcc(&jit_ptr, JL, pos_startloop);

#ifdef _WIN64
	for (i = 6; i < 16; i++)
		_jit_movaps_load(&jit_ptr, (uint8_t)i, BP, -((int32_t)i-5)*16);
	_jit_pop(&jit_ptr, BP);
#endif

	_jit_ret(&jit_ptr);

	// exec
	(*(void(*)(void))jit_exec)();

}

/*
XOR method is based on ParPar.
https://github.com/animetosho/ParPar/
*/

// bit 単位で XOR するには、256バイトごとに並び替える
void galois_altmap256_change(unsigned char *data, unsigned int bsize)
{
	unsigned short dtmp[128];
	int i, j;
	__m128i ta, tb, lmask, th, tl;

	lmask = _mm_set1_epi16(0xff);

	while (bsize != 0){
		for (j = 0; j < 8; j++){
			ta = _mm_load_si128((__m128i *)data);	// read 32-bytes
			tb = _mm_load_si128((__m128i *)data + 1);

			// split to high/low parts
			th = _mm_packus_epi16(_mm_srli_epi16(tb, 8), _mm_srli_epi16(ta, 8));
			tl = _mm_packus_epi16(_mm_and_si128(tb, lmask), _mm_and_si128(ta, lmask));

			// save to dest by extracting 16-bit masks
			dtmp[0 + j] = _mm_movemask_epi8(th);
			for (i = 1; i < 8; i++){
				th = _mm_slli_epi16(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
				dtmp[i*8 + j] = _mm_movemask_epi8(th);
			}
			dtmp[64 + j] = _mm_movemask_epi8(tl);
			for (i = 1; i < 8; i++){
				tl = _mm_slli_epi16(tl, 1);
				dtmp[64 + i*8 + j] = _mm_movemask_epi8(tl);
			}
			data += 32;
		}
		// we only really need to copy temp -> dest
		memcpy(data - 256, dtmp, 256);
		bsize -= 256;
	}
}

void galois_altmap256_return(unsigned char *data, unsigned int bsize)
{
	unsigned short dtmp[128];
	int i, j;
	__m128i ta, tb, lmask, th, tl;

	th = _mm_setzero_si128();	// shut up compiler warning
	tl = _mm_setzero_si128();
	lmask = _mm_set1_epi16(0xff);

	while (bsize != 0){
		for (j = 0; j < 8; j++){
			// load in pattern: [0011223344556677] [8899AABBCCDDEEFF]
			tl = _mm_insert_epi16(tl, *(int *)(data + 240), 0);
			th = _mm_insert_epi16(th, *(int *)(data + 112), 0);
			tl = _mm_insert_epi16(tl, *(int *)(data + 224), 1);
			th = _mm_insert_epi16(th, *(int *)(data + 96), 1);
			tl = _mm_insert_epi16(tl, *(int *)(data + 208), 2);
			th = _mm_insert_epi16(th, *(int *)(data + 80), 2);
			tl = _mm_insert_epi16(tl, *(int *)(data + 192), 3);
			th = _mm_insert_epi16(th, *(int *)(data + 64), 3);
			tl = _mm_insert_epi16(tl, *(int *)(data + 176), 4);
			th = _mm_insert_epi16(th, *(int *)(data + 48), 4);
			tl = _mm_insert_epi16(tl, *(int *)(data + 160), 5);
			th = _mm_insert_epi16(th, *(int *)(data + 32), 5);
			tl = _mm_insert_epi16(tl, *(int *)(data + 144), 6);
			th = _mm_insert_epi16(th, *(int *)(data + 16), 6);
			tl = _mm_insert_epi16(tl, *(int *)(data + 128), 7);
			th = _mm_insert_epi16(th, *(int *)data, 7);

			// swizzle to [0123456789ABCDEF] [0123456789ABCDEF]
			ta = _mm_packus_epi16(_mm_srli_epi16(tl, 8),_mm_srli_epi16(th, 8));
			tb = _mm_packus_epi16(_mm_and_si128(tl, lmask),_mm_and_si128(th, lmask));

			// extract top bits
			dtmp[j*16 + 7] = _mm_movemask_epi8(ta);
			dtmp[j*16 + 15] = _mm_movemask_epi8(tb);
			for (i = 1; i < 8; i++){
				ta = _mm_slli_epi16(ta, 1);
				tb = _mm_slli_epi16(tb, 1);
				dtmp[j*16 + 7-i] = _mm_movemask_epi8(ta);
				dtmp[j*16 + 15-i] = _mm_movemask_epi8(tb);
			}
			data += 2;
		}

		// we only really need to copy temp -> dest
		memcpy(data - 16, dtmp, 256);
		data += 240;
		bsize -= 256;
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 複数のテーブルを使って掛け算する
void galois_region_multiply(
	unsigned short *r1,	// Region to multiply
	unsigned short *r2,	// Products go here
	unsigned int count,	// Count of number in short
	int factor)			// Number to multiply by
{
	if (factor <= 1){
		if (factor == 0)
			return;

		// アドレスが 4の倍数で無い場合は 4バイト単位で計算する効率が落ちる
		if ((ULONG_PTR)r2 & 2){
			// そこで最初の 1個(2バイト)だけ普通に計算する
			*r2 ^= *r1;
			r1++;
			r2++;
			count--;
		}

		while (count >= 2){	// 2個(4バイト)ずつ計算する
			((unsigned int *)r2)[0] ^= ((unsigned int *)r1)[0];
			r1 += 2;
			r2 += 2;
			count -= 2;
		}
		if (count == 1)	// 最後に余ったやつだけ普通に計算する
			*r2 ^= *r1;
		return;
	}

	if (count >= 64){	// 64バイト以上なら掛け算用のテーブルを使った方が速い
#ifndef NO_SIMD
		if (cpu_flag & 16){	// AVX2 対応なら
			__declspec( align(32) ) unsigned char small_table[128];
			int s, d;

			create_eight_table_avx2(small_table, factor);

			// アドレスが 32の倍数で無い場合は 32バイト単位で計算する効率が落ちる
			while ((ULONG_PTR)r2 & 0x1E){
				// そこで最初の 1～15個(2～30バイト)だけ普通に計算する
				s = r1[0];
				d = r2[0];
				d ^= small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r2[0] = (unsigned short)d;
				r1++;
				r2++;
				count--;
			}

			// 16個ずつ計算するので 16の倍数にする
			gf16_avx2_block32u((unsigned char *)r1, (unsigned char *)r2,
					(count & 0xFFFFFFF0) << 1, small_table);
			r1 += count & 0xFFFFFFF0;
			r2 += count & 0xFFFFFFF0;
			count &= 15;

			// 残りは 1個ずつ計算する
			while (count != 0){
				s = r1[0];
				d = r2[0];
				d ^= small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r2[0] = (unsigned short)d;
				r1++;
				r2++;
				count--;
			}

		} else if (cpu_flag & 1){	// SSSE3 対応なら
			__declspec( align(16) ) unsigned char small_table[128];
			int s, d;

			create_eight_table(small_table, factor);

			// アドレスが 16の倍数で無い場合は 16バイト単位で計算する効率が落ちる
			while ((ULONG_PTR)r2 & 0xE){
				// そこで最初の 1～7個(2～14バイト)だけ普通に計算する
				s = r1[0];
				d = r2[0];
				d ^= small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r2[0] = (unsigned short)d;
				r1++;
				r2++;
				count--;
			}

			if (sse_unit == 16){
				// 8個ずつ計算するので 8の倍数にする
				gf16_ssse3_block16u((unsigned char *)r1, (unsigned char *)r2,
						(count & 0xFFFFFFF8) << 1, small_table);
				r1 += count & 0xFFFFFFF8;
				r2 += count & 0xFFFFFFF8;
				count &= 7;
			} else {
				// 16個ずつ計算するので 16の倍数にする
				gf16_ssse3_block32u((unsigned char *)r1, (unsigned char *)r2,
						(count & 0xFFFFFFF0) << 1, small_table);
				r1 += count & 0xFFFFFFF0;
				r2 += count & 0xFFFFFFF0;
				count &= 15;
			}

			// 残りは 1個ずつ計算する
			while (count != 0){
				s = r1[0];
				d = r2[0];
				d ^= small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r2[0] = (unsigned short)d;
				r1++;
				r2++;
				count--;
			}

		} else {	// Combined Multi Table support (2 tables of 256-entries)
#endif
			unsigned int mtab[256 * 2];

			create_two_table(mtab, factor);	// build combined multiplication tables

			// アドレスが 8の倍数で無い場合は 8バイト単位で計算する効率が落ちる
			while ((ULONG_PTR)r2 & 6){
				// そこで最初の 1～3個(2～6バイト)だけ普通に計算する
				r2[0] ^= mtab[((unsigned char *)r1)[0]] ^ mtab[256 + ((unsigned char *)r1)[1]];
				r1++;
				r2++;
				count--;
			}

#ifndef _WIN64	// 32-bit 版なら MMX を使う
#ifndef NO_SIMD
			// 4個(8バイト)ずつ計算するので 4の倍数にする
			DoBlock8((unsigned char *)r1, (unsigned char *)r2, (count & 0xFFFFFFFC) << 1, mtab);
			r1 += count & 0xFFFFFFFC;
			r2 += count & 0xFFFFFFFC;
			count &= 3;

#else	// MMX を使わないなら
			// バッファーを 32-bit整数として扱う
			while (count >= 2){	// 2個(4バイト)ずつ計算する
				// 先に計算しておいた 2個の参照テーブルを使う
				((unsigned int *)r2)[0] ^= mtab[((unsigned char *)r1)[0]] ^ mtab[256 + ((unsigned char *)r1)[1]] ^
										 ((mtab[((unsigned char *)r1)[2]] ^ mtab[256 + ((unsigned char *)r1)[3]]) << 16);
				r1 += 2;
				r2 += 2;
				count -= 2;
			}
#endif

#else	// 64-bit 版なら 64-bit 整数を使う
			// バッファーを 64-bit整数として扱う
			while (count >= 4){	// 4個(8バイト)ずつ計算する
				// 先に計算しておいた 2個の参照テーブルを使う
				((unsigned __int64 *)r2)[0] ^=
						  mtab[((unsigned char *)r1)[0]] ^ mtab[256 + ((unsigned char *)r1)[1]] ^
						((mtab[((unsigned char *)r1)[2]] ^ mtab[256 + ((unsigned char *)r1)[3]]) << 16) ^
						((unsigned __int64)(mtab[((unsigned char *)r1)[4]] ^ mtab[256 + ((unsigned char *)r1)[5]]) << 32) ^
						((unsigned __int64)(mtab[((unsigned char *)r1)[6]] ^ mtab[256 + ((unsigned char *)r1)[7]]) << 48);
				r1 += 4;
				r2 += 4;
				count -= 4;
			}
#endif

			// 残りは 1個ずつ計算する
			while (count != 0){
				r2[0] ^= mtab[((unsigned char *)r1)[0]] ^ mtab[256 + ((unsigned char *)r1)[1]];
				r1++;
				r2++;
				count--;
			}
#ifndef NO_SIMD
		}
#endif

	} else {	// 小さいデータは普通に計算する
		int log_y = galois_log_table[factor];

		while (count != 0){
			r2[0] ^= galois_multiply_fix(r1[0], log_y);
			r1++;
			r2++;
			count--;
		}
	}
}

// 行列の割り算用、微妙に速いけど大差無し
void galois_region_divide(
	unsigned short *r1,	// Region to divide. products go here
	unsigned int count,	// Count of number in short
	int factor)			// Number to divide by
{
	factor = galois_reciprocal(factor);	// factor = 1 / factor

	if (count >= 64){
// 行列サイズが小さいのでテーブル作成に時間がかかって、全く速くならない・・・
/*
#ifndef NO_SIMD
		if (cpu_flag & 16){	// AVX2 対応なら
			__declspec( align(32) ) unsigned char small_table[128];
			int s, d;

			create_eight_table_avx2(small_table, factor);

			// アドレスが 32の倍数で無い場合は 32バイト単位で計算する効率が落ちる
			while ((ULONG_PTR)r1 & 0x1E){
				// そこで最初の 1～15個(2～30バイト)だけ普通に計算する
				s = r1[0];
				d = small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r1[0] = (unsigned short)d;
				r1++;
				count--;
			}

			// 16個ずつ計算するので 16の倍数にする
			gf16_avx2_block32s((unsigned char *)r1, (count & 0xFFFFFFF0) << 1, small_table);
			r1 += count & 0xFFFFFFF0;
			count &= 15;

			// 残りは 1個ずつ計算する
			while (count != 0){
				s = r1[0];
				d = small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r1[0] = (unsigned short)d;
				r1++;
				count--;
			}

		} else if (cpu_flag & 1){	// SSSE3 対応なら
			__declspec( align(16) ) unsigned char small_table[128];
			int s, d;

			create_eight_table(small_table, factor);

			// アドレスが 16の倍数で無い場合は 16バイト単位で計算する効率が落ちる
			while ((ULONG_PTR)r1 & 0xE){
				// そこで最初の 1～7個(2～14バイト)だけ普通に計算する
				s = r1[0];
				d = small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r1[0] = (unsigned short)d;
				r1++;
				count--;
			}

			// 8個ずつ計算するので 8の倍数にする
			gf16_ssse3_block16s((unsigned char *)r1, (count & 0xFFFFFFF8) << 1, small_table);
			r1 += count & 0xFFFFFFF8;
			count &= 7;

			// 残りは 1個ずつ計算する
			while (count != 0){
				s = r1[0];
				d = small_table[s & 0xF] | ((int)(small_table[16 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[32 + (s & 0xF)] | ((int)(small_table[48 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[64 + (s & 0xF)] | ((int)(small_table[80 + (s & 0xF)]) << 8);
				s = s >> 4;
				d ^= small_table[96 + s] | ((int)(small_table[112 + s]) << 8);
				r1[0] = (unsigned short)d;
				r1++;
				count--;
			}

		} else {	// Combined Multi Table support (2 tables of 256-entries)
#endif
*/
			unsigned int mtab[256 * 2];

			create_two_table(mtab, factor);	// 掛け算用のテーブルをその場で構成する

			// アドレスが 4の倍数で無い場合は 4バイト単位で計算する効率が落ちる
			if (((ULONG_PTR)r1 & 2) != 0){
				// そこで最初の 1個(2バイト)だけ普通に計算する
				r1[0] = (unsigned short)(mtab[((unsigned char *)r1)[0]] ^ mtab[256 + ((unsigned char *)r1)[1]]);
				r1++;
				count--;
			}

			// バッファーを 32-bit整数として扱う
			while (count >= 2){	// 2個(4バイト)ずつ計算する
				// 先に計算しておいた 2個の参照テーブルを使う
				((unsigned int *)r1)[0] = mtab[((unsigned char *)r1)[0]] ^ mtab[256 + ((unsigned char *)r1)[1]] ^
										((mtab[((unsigned char *)r1)[2]] ^ mtab[256 + ((unsigned char *)r1)[3]]) << 16);
				r1 += 2;
				count -= 2;
			}
			// 奇数なら最後に 1個余る
			if (count == 1)
				r1[0] = (unsigned short)(mtab[((unsigned char *)r1)[0]] ^ mtab[256 + ((unsigned char *)r1)[1]]);
/*
#ifndef NO_SIMD
		}
#endif
*/

	} else {	// 小さいデータは普通に計算する
		int log_y = galois_log_table[factor];

		while (count != 0){
			r1[0] = galois_multiply_fix(r1[0], log_y);
			r1++;
			count--;
		}
	}
}

// 16バイト境界のバッファー専用のXOR
void galois_align_xor(
	unsigned char *r1,	// Region to multiply
	unsigned char *r2,	// Products go here
	unsigned int len)	// Byte length
{
#ifndef NO_SIMD
	__m128i xmm0, xmm1;	// 16バイトごとに XOR する

	while (len != 0){
		xmm0 = _mm_load_si128((__m128i *)r1);
		xmm1 = _mm_load_si128((__m128i *)r2);
		xmm1 = _mm_xor_si128(xmm1, xmm0);
		_mm_store_si128((__m128i *)r2, xmm1);
		r1 += 16;
		r2 += 16;
		len -= 16;
	}

#else	// SSE2 を使わないなら
	while (len != 0){	// 4バイトずつ計算する
		((unsigned int *)r2)[0] ^= ((unsigned int *)r1)[0];
		r1 += 4;
		r2 += 4;
		len -= 4;
	}
#endif
}

// 16バイト境界のバッファー専用の掛け算 (ALTMAP しない)
void galois_align16_multiply(
	unsigned char *r1,	// Region to multiply (must be aligned by 16)
	unsigned char *r2,	// Products go here
	unsigned int len,	// Byte length (must be multiple of 16)
	int factor)			// Number to multiply by
{
	if (factor <= 1){
		if (factor == 0)
			return;

#ifndef NO_SIMD
		{	__m128i xmm0, xmm1;	// 16バイトごとに XOR する
			while (len != 0){
				xmm0 = _mm_load_si128((__m128i *)r1);
				xmm1 = _mm_load_si128((__m128i *)r2);
				xmm1 = _mm_xor_si128(xmm1, xmm0);
				_mm_store_si128((__m128i *)r2, xmm1);
				r1 += 16;
				r2 += 16;
				len -= 16;
			}
		}
#else	// SSE2 を使わないなら
		while (len != 0){	// 4バイトずつ計算する
			((unsigned int *)r2)[0] ^= ((unsigned int *)r1)[0];
			r1 += 4;
			r2 += 4;
			len -= 4;
		}
#endif

	// 掛け算用のテーブルを常に作成する (32バイトだと少し遅くなる)
#ifndef NO_SIMD
/*
	// sse_unit が 32の倍数な時だけ
	} else if (cpu_flag & 16){	// AVX2 対応なら
		__declspec( align(32) ) unsigned char small_table[128];

		create_eight_table_avx2(small_table, factor);

		gf16_avx2_block32u(r1, r2, len, small_table);
*/

	} else if (cpu_flag & 1){	// SSSE3 対応なら
		__declspec( align(16) ) unsigned char small_table[128];

		create_eight_table(small_table, factor);

		gf16_ssse3_block16u(r1, r2, len, small_table);
		// sse_unit が 32の倍数ならこちらでもいい
		//gf16_ssse3_block32u(r1, r2, len, small_table);

#endif
	} else {	// Combined Multi Table support (2 tables of 256-entries)
		unsigned int mtab[256 * 2];

		create_two_table(mtab, factor);	// build combined multiplication tables

#ifndef _WIN64	// 32-bit 版なら MMX を使う
#ifndef NO_SIMD
		DoBlock8(r1, r2, len, mtab); // process large chunk 8-bytes a shot

#else	// MMX を使わないなら
		// バッファーを 32-bit整数として扱う
		while (len != 0){	// 4バイトずつ計算する
			((unsigned int *)r2)[0] ^= mtab[r1[0]] ^ mtab[256 + r1[1]] ^
									 ((mtab[r1[2]] ^ mtab[256 + r1[3]]) << 16);
			r1 += 4;
			r2 += 4;
			len -= 4;
		}
#endif

#else	// 64-bit 版なら 64-bit 整数を使う
		// バッファーを 64-bit整数として扱う
		while (len != 0){	// 8バイトずつ計算する
			((unsigned __int64 *)r2)[0] ^=
					  mtab[r1[0]] ^ mtab[256 + r1[1]] ^
					((mtab[r1[2]] ^ mtab[256 + r1[3]]) << 16) ^
					((unsigned __int64)(mtab[r1[4]] ^ mtab[256 + r1[5]]) << 32) ^
					((unsigned __int64)(mtab[r1[6]] ^ mtab[256 + r1[7]]) << 48);
			r1 += 8;
			r2 += 8;
			len -= 8;
		}
#endif
	}
}

// 32バイトごとに並び替えられたバッファー専用の掛け算 (SSSE3 & ALTMAP)
void galois_align32_multiply(
	unsigned char *r1,	// Region to multiply (must be aligned by 16)
	unsigned char *r2,	// Products go here
	unsigned int len,	// Byte length (must be multiple of 32)
	int factor)			// Number to multiply by
{
	if (factor <= 1){
		if (factor != 0){
			__m128i xmm0, xmm1;	// 16バイトごとに XOR する

			while (len != 0){
				xmm0 = _mm_load_si128((__m128i *)r1);
				xmm1 = _mm_load_si128((__m128i *)r2);
				xmm1 = _mm_xor_si128(xmm1, xmm0);
				_mm_store_si128((__m128i *)r2, xmm1);
				r1 += 16;
				r2 += 16;
				len -= 16;
			}
		}

	// 掛け算用のテーブルを常に作成する (32バイトだと少し遅くなる)
	} else {
		__declspec( align(16) ) unsigned char small_table[128];

		create_eight_table(small_table, factor);

		gf16_ssse3_block32_altmap(r1, r2, len, small_table);
	}
}

// 掛け算を２回行って、一度に更新する (SSSE3 & ALTMAP)
void galois_align32_multiply2(
	unsigned char *src1,	// Region to multiply (must be aligned by 16)
	unsigned char *src2,
	unsigned char *dst,		// Products go here
	unsigned int len,		// Byte length (must be multiple of 32)
	int factor1,			// Number to multiply by
	int factor2)
{
	if ((factor1 == 1) && (factor2 == 1)){	// 両方の factor が 1の場合
		__m128i xmm0, xmm1, xmm2;

		while (len != 0){
			xmm0 = _mm_load_si128((__m128i *)dst);
			xmm1 = _mm_load_si128((__m128i *)src1);
			xmm2 = _mm_load_si128((__m128i *)src2);
			xmm0 = _mm_xor_si128(xmm0, xmm1);
			xmm0 = _mm_xor_si128(xmm0, xmm2);
			_mm_store_si128((__m128i *)dst, xmm0);
			src1 += 16;
			src2 += 16;
			dst += 16;
			len -= 16;
		}

	// 掛け算用のテーブルを常に作成する (32バイトだと少し遅くなる)
	} else {
		__declspec( align(16) ) unsigned char small_table[256];

		create_eight_table(small_table, factor1);
		create_eight_table(small_table + 128, factor2);

		gf16_ssse3_block32_altmap2(src1, src2, dst, len, small_table);
	}
}

// 256バイトごとに並び替えられたバッファー専用の JIT(SSE2) を使った掛け算
void galois_align256_multiply(
	unsigned char *r1,	// Region to multiply (must be aligned by 16)
	unsigned char *r2,	// Products go here
	unsigned int len,	// Byte length (must be multiple of 32)
	int factor)			// Number to multiply by
{
	if (factor <= 1){
		if (factor != 0){
			__m128i xmm0, xmm1;	// 16バイトごとに XOR する

			while (len != 0){
				xmm0 = _mm_load_si128((__m128i *)r1);
				xmm1 = _mm_load_si128((__m128i *)r2);
				xmm1 = _mm_xor_si128(xmm1, xmm0);
				_mm_store_si128((__m128i *)r2, xmm1);
				r1 += 16;
				r2 += 16;
				len -= 16;
			}
		}

	// 常に JIT(SSE2) を使う
	} else {
		gf16_sse2_block256_jit(r1, r2, len, factor);
	}
}

// 32バイトごとに並び替えられたバッファー専用の掛け算 (AVX2 & ALTMAP)
void galois_align32avx_multiply(
	unsigned char *r1,	// Region to multiply (must be aligned by 32)
	unsigned char *r2,	// Products go here
	unsigned int len,	// Byte length (must be multiple of 32)
	int factor)			// Number to multiply by
{
	if (factor <= 1){
		if (factor != 0){
			__m256i ymm0, ymm1;	// 32バイトごとに XOR する

			while (len != 0){
				ymm0 = _mm256_load_si256((__m256i *)r1);
				ymm1 = _mm256_load_si256((__m256i *)r2);
				ymm1 = _mm256_xor_si256(ymm1, ymm0);
				_mm256_store_si256((__m256i *)r2, ymm1);
				r1 += 32;
				r2 += 32;
				len -= 32;
			}

			_mm256_zeroupper();	// AVX-SSE 切り替えの回避
		}

	// 掛け算用のテーブルを常に作成する (32バイトだと少し遅くなる)
	} else {
		__declspec( align(32) ) unsigned char small_table[128];

		create_eight_table_avx2(small_table, factor);

		gf16_avx2_block32(r1, r2, len, small_table);
	}
}

// 掛け算を２回行って、一度に更新する (AVX2 & ALTMAP)
void galois_align32avx_multiply2(
	unsigned char *src1,	// Region to multiply (must be aligned by 32)
	unsigned char *src2,
	unsigned char *dst,		// Products go here
	unsigned int len,		// Byte length (must be multiple of 32)
	int factor1,			// Number to multiply by
	int factor2)
{
	if ((factor1 == 1) && (factor2 == 1)){	// 両方の factor が 1の場合
		__m256i ymm0, ymm1, ymm2;
		while (len != 0){
			ymm0 = _mm256_load_si256((__m256i *)dst);
			ymm1 = _mm256_load_si256((__m256i *)src1);
			ymm2 = _mm256_load_si256((__m256i *)src2);
			ymm0 = _mm256_xor_si256(ymm0, ymm1);
			ymm0 = _mm256_xor_si256(ymm0, ymm2);
			_mm256_store_si256((__m256i *)dst, ymm0);
			src1 += 32;
			src2 += 32;
			dst += 32;
			len -= 32;
		}
		_mm256_zeroupper();	// AVX-SSE 切り替えの回避

	// 掛け算用のテーブルを常に作成する (32バイトだと少し遅くなる)
	} else {
		__declspec( align(32) ) unsigned char small_table[256];

		create_eight_table_avx2(small_table, factor1);
		create_eight_table_avx2(small_table + 128, factor2);

		gf16_avx2_block32_2(src1, src2, dst, len, small_table);
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// チェックサムを計算する

// buffer alignment must be 16, length must be multiple of 16
void checksum16(unsigned char *data, unsigned char *hash, int byte_size)
{
	int i, count;

#ifndef NO_SIMD	// SSE2 を使うなら
	__m128i temp16, prev16, data16, mask16, zero16, poly16;

	count = byte_size / 16;
	prev16 = _mm_setzero_si128();
	zero16 = _mm_setzero_si128();
	poly16 = _mm_set1_epi32(0x100B100B);	// PRIM_POLY = 0x1100B

	while (count > 0){	// HASH_RANGE バイトごとに
		// 16バイトごとに XOR する
		temp16 = _mm_setzero_si128();

		if (count < HASH_RANGE / 16){
			i = count;
			count = 0;
		} else {
			i = HASH_RANGE / 16;
			count -= HASH_RANGE / 16;
		}
		while (i > 0){
			data16 = _mm_load_si128((__m128i *)data);	// load 16-bytes
			temp16 = _mm_xor_si128(temp16, data16);
			data += 16;
			i--;
		}

		// 前回の値を 2倍して、今回の値を追加する
		//temp16 = _mm_xor_si128(temp16, prev16);	// 3倍する場合は、元の値も XOR すればいい
		mask16 = _mm_cmpgt_epi16(zero16, prev16);	// (0 > prev) ? 0xFFFF : 0x0000
		prev16 = _mm_slli_epi16(prev16, 1);			// prev *= 2
		mask16 = _mm_and_si128(mask16, poly16);		// 0x100B or 0x0000
		prev16 = _mm_xor_si128(prev16, mask16);

		prev16 = _mm_xor_si128(prev16, temp16);
	}
	_mm_store_si128((__m128i *)hash, prev16);

#else	// 4バイト整数で計算するなら
	unsigned int *data4, temp[4], prev[4], x, mask;

	data4 = (unsigned int *)data;
	count = byte_size / 4;
	for (i = 0; i < 4; i++)
		prev[i] = 0;

	while (count > 0){	// HASH_RANGE バイトごとに
		// 4バイトごとに 16バイトに XOR する
		for (i = 0; i < 4; i++)
			temp[i] = 0;
		for (i = 0; i < HASH_RANGE / 4; i++){
			temp[i & 3] ^= data4[i];
			count--;
			if (count == 0)
				break;
		}
		data4 += HASH_RANGE / 4;

		// 前回の値を 2倍して、今回の値を追加する
		for (i = 0; i < 4; i++){
			x = prev[i];
			mask = (x & 0x80008000) >> 15;	// 0x00010001 or 0x00000000
			x = (x & 0x7FFF7FFF) << 1;	// 3倍する場合は「^=」にすればいい
			x ^= mask * 0x100B;	// 0x100B100B or 0x00000000

			prev[i] = x ^ temp[i];
		}
	}
	for (i = 0; i < 4; i++)
		((unsigned int *)hash)[i] = prev[i];
#endif
}

// チェックサムを計算すると同時にデータを並び替える
// buffer alignment must be 16, length must be (multiple of 32) - 16
void checksum16_altmap32(unsigned char *data, unsigned char *hash, int byte_size)
{
	int i, count;
	__m128i temp16, prev16, mask16, zero16, poly16;
	__m128i dataA, dataB, dataC, dataD, maskB;

	count = byte_size / 16;
	prev16 = _mm_setzero_si128();
	zero16 = _mm_setzero_si128();
	poly16 = _mm_set1_epi32(0x100B100B);	// PRIM_POLY = 0x1100B
	maskB = _mm_set1_epi16(0x00FF);	// 0x00FF *8

	while (count > 0){	// HASH_RANGE バイトごとに
		// 16バイトごとに XOR する
		temp16 = _mm_setzero_si128();

		if (count < HASH_RANGE / 16){
			i = count;
			count = 0;
		} else {
			i = HASH_RANGE / 16;
			count -= HASH_RANGE / 16;
		}
		while (i >= 2){
			dataA = _mm_load_si128((__m128i *)data);	// read 32-bytes
			dataB = _mm_load_si128((__m128i *)data + 1);
			temp16 = _mm_xor_si128(temp16, dataA);
			temp16 = _mm_xor_si128(temp16, dataB);

			dataC = _mm_and_si128(dataA, maskB);	// erase higher byte
			dataD = _mm_and_si128(dataB, maskB);
			dataA = _mm_srli_epi16(dataA, 8);		// move higher byte to lower
			dataB = _mm_srli_epi16(dataB, 8);
			dataC = _mm_packus_epi16(dataC, dataD);	//  select lower byte of each word
			dataA = _mm_packus_epi16(dataA, dataB);	//  select higher byte of each word

			_mm_store_si128((__m128i *)data, dataC);	// write 32-bytes
			_mm_store_si128((__m128i *)data + 1, dataA);

			data += 32;
			i -= 2;
		}
		if (i > 0){
			dataA = _mm_load_si128((__m128i *)data);	// load 16-bytes
			temp16 = _mm_xor_si128(temp16, dataA);
		}

		// 前回の値を 2倍して、今回の値を追加する
		//temp16 = _mm_xor_si128(temp16, prev16);	// 3倍する場合は、元の値も XOR すればいい
		mask16 = _mm_cmpgt_epi16(zero16, prev16);	// (0 > prev) ? 0xFFFF : 0x0000
		prev16 = _mm_slli_epi16(prev16, 1);			// prev *= 2
		mask16 = _mm_and_si128(mask16, poly16);		// 0x100B or 0x0000
		prev16 = _mm_xor_si128(prev16, mask16);

		prev16 = _mm_xor_si128(prev16, temp16);
	}
	if (hash != data + 16)	// ハッシュ値の保存先が別なら
		_mm_store_si128((__m128i *)hash, prev16);

	// 最後にハッシュ値も並び替える
	dataC = _mm_and_si128(dataA, maskB);	// erase higher byte
	dataD = _mm_and_si128(prev16, maskB);
	dataA = _mm_srli_epi16(dataA, 8);		// move higher byte to lower
	dataB = _mm_srli_epi16(prev16, 8);
	dataC = _mm_packus_epi16(dataC, dataD);	//  select lower byte of each word
	dataA = _mm_packus_epi16(dataA, dataB);	//  select higher byte of each word

	_mm_store_si128((__m128i *)data, dataC);	// write 32-bytes
	_mm_store_si128((__m128i *)data + 1, dataA);
}

// データの並びを元に戻すと同時にチェックサムを計算する
// buffer alignment must be 32, length must be (multiple of 32) - 16
void checksum16_return32(unsigned char *data, unsigned char *hash, int byte_size)
{
	int i, count;
	__m128i temp16, prev16, mask16, zero16, poly16;
	__m128i dataA, dataB, dataC;

	count = byte_size / 16;
	prev16 = _mm_setzero_si128();
	zero16 = _mm_setzero_si128();
	poly16 = _mm_set1_epi32(0x100B100B);	// PRIM_POLY = 0x1100B

	while (count > 0){	// HASH_RANGE バイトごとに
		// 16バイトごとに XOR する
		temp16 = _mm_setzero_si128();

		if (count < HASH_RANGE / 16){
			i = count;
			count = 0;
		} else {
			i = HASH_RANGE / 16;
			count -= HASH_RANGE / 16;
		}
		while (i >= 2){
			dataA = _mm_load_si128((__m128i *)data);	// read 32-bytes
			dataB = _mm_load_si128((__m128i *)data + 1);

			dataC = _mm_unpacklo_epi8(dataA, dataB);	// interleave lower and higher bytes
			dataA = _mm_unpackhi_epi8(dataA, dataB);

			_mm_store_si128((__m128i *)data, dataC);	// write 32-bytes
			_mm_store_si128((__m128i *)data + 1, dataA);
			temp16 = _mm_xor_si128(temp16, dataC);
			temp16 = _mm_xor_si128(temp16, dataA);

			data += 32;
			i -= 2;
		}
		if (i > 0){
			dataA = _mm_load_si128((__m128i *)data);	// read 32-bytes
			dataB = _mm_load_si128((__m128i *)data + 1);

			dataC = _mm_unpacklo_epi8(dataA, dataB);	// interleave lower and higher bytes
			dataA = _mm_unpackhi_epi8(dataA, dataB);

			_mm_store_si128((__m128i *)data, dataC);	// write 32-bytes
			_mm_store_si128((__m128i *)data + 1, dataA);
			temp16 = _mm_xor_si128(temp16, dataC);
		}

		// 前回の値を 2倍して、今回の値を追加する
		//temp16 = _mm_xor_si128(temp16, prev16);	// 3倍する場合は、元の値も XOR すればいい
		mask16 = _mm_cmpgt_epi16(zero16, prev16);	// (0 > prev) ? 0xFFFF : 0x0000
		prev16 = _mm_slli_epi16(prev16, 1);			// prev *= 2
		mask16 = _mm_and_si128(mask16, poly16);		// 0x100B or 0x0000
		prev16 = _mm_xor_si128(prev16, mask16);

		prev16 = _mm_xor_si128(prev16, temp16);
	}

	if (hash != data + 16)	// ハッシュ値の保存先が別なら
		_mm_store_si128((__m128i *)hash, prev16);
}

// チェックサムを計算すると同時にデータを並び替える
// buffer alignment must be 256, length must be (multiple of 256) - 16
void checksum16_altmap256(unsigned char *data, unsigned char *hash, int byte_size)
{
	// 順番に処理する場合
//	checksum16(data, hash, byte_size);
//	galois_altmap256_change(data, byte_size + HASH_SIZE);

	unsigned short dtmp[128];
	int i, j;
	__m128i ta, tb, lmask, th, tl, temp16, prev16, poly16;

	lmask = _mm_set1_epi16(0xff);
	prev16 = _mm_setzero_si128();
	poly16 = _mm_set1_epi32(0x100B100B);	// PRIM_POLY = 0x1100B

	byte_size += HASH_SIZE;
	while (byte_size != 0){
		for (j = 0; j < 8; j++){
			ta = _mm_load_si128((__m128i *)data);	// read 32-bytes
			tb = _mm_load_si128((__m128i *)data + 1);

			// 128バイトを XOR して 16バイトにする
			if ((j & 3) == 0)
				temp16 = _mm_setzero_si128();
			temp16 = _mm_xor_si128(temp16, ta);
			if ((j != 7) || (byte_size != 256))	// 最後の 16バイトだけ含めない
				temp16 = _mm_xor_si128(temp16, tb);

			if ((j & 3) == 3){	// j = 3 or 7
				// 前回の値を 2倍して、今回の値を追加する
				//temp16 = _mm_xor_si128(temp16, prev16);	// 3倍する場合は、元の値も XOR すればいい
				tl = _mm_setzero_si128();
				th = _mm_cmpgt_epi16(tl, prev16);	// (0 > prev) ? 0xFFFF : 0x0000
				prev16 = _mm_slli_epi16(prev16, 1);	// prev *= 2
				th = _mm_and_si128(th, poly16);		// 0x100B or 0x0000
				prev16 = _mm_xor_si128(prev16, th);

				prev16 = _mm_xor_si128(prev16, temp16);
				if ((j == 7) && (byte_size == 256)){
					if (hash != data + 16)	// ハッシュ値の保存先が別なら
						_mm_store_si128((__m128i *)hash, prev16);
					_mm_store_si128(&tb, prev16);	// 最後の 16バイトにハッシュ値を置く
				}
			}

			// split to high/low parts
			th = _mm_packus_epi16(_mm_srli_epi16(tb, 8), _mm_srli_epi16(ta, 8));
			tl = _mm_packus_epi16(_mm_and_si128(tb, lmask), _mm_and_si128(ta, lmask));

			// save to dest by extracting 16-bit masks
			dtmp[0 + j] = _mm_movemask_epi8(th);
			for (i = 1; i < 8; i++){
				th = _mm_slli_epi16(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
				dtmp[i*8 + j] = _mm_movemask_epi8(th);
			}
			dtmp[64 + j] = _mm_movemask_epi8(tl);
			for (i = 1; i < 8; i++){
				tl = _mm_slli_epi16(tl, 1);
				dtmp[64 + i*8 + j] = _mm_movemask_epi8(tl);
			}
			data += 32;
		}
		// we only really need to copy temp -> dest
		memcpy(data - 256, dtmp, 256);
		byte_size -= 256;
	}
}

// データの並びを元に戻すと同時にチェックサムを計算する
// buffer alignment must be 256, length must be (multiple of 256) - 16
void checksum16_return256(unsigned char *data, unsigned char *hash, int byte_size)
{
	// 順番に処理する場合
//	galois_altmap256_return(data, byte_size + HASH_SIZE);
//	checksum16(data, hash, byte_size);

	__declspec( align(16) ) unsigned short dtmp[128];
	int i, j;
	__m128i ta, tb, lmask, th, tl, temp16, prev16, poly16;

	th = _mm_setzero_si128();	// shut up compiler warning
	tl = _mm_setzero_si128();
	lmask = _mm_set1_epi16(0xff);
	prev16 = _mm_setzero_si128();
	poly16 = _mm_set1_epi32(0x100B100B);	// PRIM_POLY = 0x1100B

	byte_size += HASH_SIZE;
	while (byte_size != 0){
		for (j = 0; j < 8; j++){
			// load in pattern: [0011223344556677] [8899AABBCCDDEEFF]
			tl = _mm_insert_epi16(tl, *(int *)(data + 240), 0);
			th = _mm_insert_epi16(th, *(int *)(data + 112), 0);
			tl = _mm_insert_epi16(tl, *(int *)(data + 224), 1);
			th = _mm_insert_epi16(th, *(int *)(data + 96), 1);
			tl = _mm_insert_epi16(tl, *(int *)(data + 208), 2);
			th = _mm_insert_epi16(th, *(int *)(data + 80), 2);
			tl = _mm_insert_epi16(tl, *(int *)(data + 192), 3);
			th = _mm_insert_epi16(th, *(int *)(data + 64), 3);
			tl = _mm_insert_epi16(tl, *(int *)(data + 176), 4);
			th = _mm_insert_epi16(th, *(int *)(data + 48), 4);
			tl = _mm_insert_epi16(tl, *(int *)(data + 160), 5);
			th = _mm_insert_epi16(th, *(int *)(data + 32), 5);
			tl = _mm_insert_epi16(tl, *(int *)(data + 144), 6);
			th = _mm_insert_epi16(th, *(int *)(data + 16), 6);
			tl = _mm_insert_epi16(tl, *(int *)(data + 128), 7);
			th = _mm_insert_epi16(th, *(int *)data, 7);

			// swizzle to [0123456789ABCDEF] [0123456789ABCDEF]
			ta = _mm_packus_epi16(_mm_srli_epi16(tl, 8),_mm_srli_epi16(th, 8));
			tb = _mm_packus_epi16(_mm_and_si128(tl, lmask),_mm_and_si128(th, lmask));

			// extract top bits
			dtmp[j*16 + 7] = _mm_movemask_epi8(ta);
			dtmp[j*16 + 15] = _mm_movemask_epi8(tb);
			for (i = 1; i < 8; i++){
				ta = _mm_slli_epi16(ta, 1);
				tb = _mm_slli_epi16(tb, 1);
				dtmp[j*16 + 7-i] = _mm_movemask_epi8(ta);
				dtmp[j*16 + 15-i] = _mm_movemask_epi8(tb);
			}
			data += 2;
		}

		// 128バイトを XOR して 16バイトにする
		temp16 = _mm_setzero_si128();
		for (j = 0; j < 8; j++){
			ta = _mm_load_si128((__m128i *)dtmp + j);
			temp16 = _mm_xor_si128(temp16, ta);
		}
		// 前回の値を 2倍して、今回の値を追加する
		//temp16 = _mm_xor_si128(temp16, prev16);	// 3倍する場合は、元の値も XOR すればいい
		tl = _mm_setzero_si128();
		th = _mm_cmpgt_epi16(tl, prev16);	// (0 > prev) ? 0xFFFF : 0x0000
		prev16 = _mm_slli_epi16(prev16, 1);	// prev *= 2
		th = _mm_and_si128(th, poly16);		// 0x100B or 0x0000
		prev16 = _mm_xor_si128(prev16, th);
		prev16 = _mm_xor_si128(prev16, temp16);

		if (byte_size != 256){
			// 次の 128バイトを XOR して 16バイトにする
			temp16 = _mm_setzero_si128();
			for (j = 8; j < 16; j++){
				ta = _mm_load_si128((__m128i *)dtmp + j);
				temp16 = _mm_xor_si128(temp16, ta);
			}
			// 前回の値を 2倍して、今回の値を追加する
			//temp16 = _mm_xor_si128(temp16, prev16);	// 3倍する場合は、元の値も XOR すればいい
			tl = _mm_setzero_si128();
			th = _mm_cmpgt_epi16(tl, prev16);	// (0 > prev) ? 0xFFFF : 0x0000
			prev16 = _mm_slli_epi16(prev16, 1);	// prev *= 2
			th = _mm_and_si128(th, poly16);		// 0x100B or 0x0000
			prev16 = _mm_xor_si128(prev16, th);
			prev16 = _mm_xor_si128(prev16, temp16);

		} else {	// 最後の 128バイトは特別
			// 次の 112バイトを XOR して 16バイトにする
			temp16 = _mm_setzero_si128();
			for (j = 8; j < 15; j++){
				ta = _mm_load_si128((__m128i *)dtmp + j);
				temp16 = _mm_xor_si128(temp16, ta);
			}
			// 前回の値を 2倍して、今回の値を追加する
			//temp16 = _mm_xor_si128(temp16, prev16);	// 3倍する場合は、元の値も XOR すればいい
			tl = _mm_setzero_si128();
			th = _mm_cmpgt_epi16(tl, prev16);	// (0 > prev) ? 0xFFFF : 0x0000
			prev16 = _mm_slli_epi16(prev16, 1);	// prev *= 2
			th = _mm_and_si128(th, poly16);		// 0x100B or 0x0000
			prev16 = _mm_xor_si128(prev16, th);
			prev16 = _mm_xor_si128(prev16, temp16);
		}

		// we only really need to copy temp -> dest
		memcpy(data - 16, dtmp, 256);
		data += 240;
		byte_size -= 256;
	}

	if (hash != data - 16)	// ハッシュ値の保存先が別なら
		_mm_store_si128((__m128i *)hash, prev16);
}

