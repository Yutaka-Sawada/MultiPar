// crc.c
// Copyright : 2024-11-30 Yutaka Sawada
// License : GPL

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601	// Windows 7 or later
#endif

#include <stdio.h>

#include <windows.h>
#include <nmmintrin.h>	// MMX ~ SSE4.2 命令セットを使用する場合インクルード
#include <wmmintrin.h>	// AES, CLMUL 命令セットを使用する場合インクルード

#include "crc.h"

extern unsigned int cpu_flag;	// declared in common2.h

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// CRC-32 計算用

#define CRC32_POLY	0xEDB88320	// CRC-32-IEEE 802.3 (little endian)
unsigned int crc_table[256];
unsigned int reverse_table[256];	// CRC-32 逆算用のテーブル

// CRC 計算用のテーブルを作る
void init_crc_table(void)
{
	unsigned int i, j, r;

	for (i = 0; i < 256; i++){	// CRC-32
		r = i;
		for (j = 0; j < 8; j++)
			r = (r >> 1) ^ (CRC32_POLY & ~((r & 1) - 1));
		crc_table[i] = r;
	}

	// crc の 最上位 1バイトからテーブルの位置がわかる
	// まずは逆算用のテーブルを作る、テーブルを 8ビットずらして最下位に番号を入れておく
	for (i = 0; i < 256; i++)
		reverse_table[(crc_table[i] >> 24)] = (crc_table[i] << 8) | i;
}

// CRC-32 を更新する
unsigned int crc_update_std(unsigned int crc, unsigned char *buf, unsigned int len)
{
	// 4バイト境界までは 1バイトずつ計算する
	while ((len > 0) && (((ULONG_PTR)buf) & 3)){
		crc = crc_table[(crc & 0xFF) ^ (*buf++)] ^ (crc >> 8);
		len--;
	}

	// 4バイトごとに計算する
	while (len >= 4){
		crc ^= *((unsigned int *)buf);
		crc = crc_table[crc & 0xFF] ^ (crc >> 8);
		crc = crc_table[crc & 0xFF] ^ (crc >> 8);
		crc = crc_table[crc & 0xFF] ^ (crc >> 8);
		crc = crc_table[crc & 0xFF] ^ (crc >> 8);
		len -= 4;
		buf += 4;
	}

	// 余りは 1バイトずつ計算する
	while (len--)
		crc = crc_table[(crc & 0xFF) ^ (*buf++)] ^ (crc >> 8);

	return crc;
}

// 内容が全て 0 のデータの CRC-32 を更新する
/*
unsigned int crc_update_zero(unsigned int crc, unsigned int len)
{
	while (len--)
		crc = crc_table[crc & 0xFF] ^ (crc >> 8);

	return crc;
}
*/

// 内容が全て 0 のデータの CRC-32 を逆算するための関数
unsigned int crc_reverse_zero(unsigned int crc, unsigned int len)
{
	// crc2 = 前の crc ^ 0xFFFFFFFF;
	// crc = table[crc2 & 0xff] ^ (crc2 >> 8);

	//crc ^= 0xFFFFFFFF; // 最終処理を取り消す
	while (len--)
		crc = reverse_table[(crc >> 24)] ^ (crc << 8);
	//crc ^= 0xFFFFFFFF; // 最終処理をし直す

	return crc;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// CRC-32 with PCLMULQDQ Instruction is based on below source code.

/*
 * Compute the CRC32 using a parallelized folding approach with the PCLMULQDQ 
 * instruction.
 *
 * A white paper describing this algorithm can be found at:
 * http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 * Authors:
 * 	Wajdi Feghali   <wajdi.k.feghali@intel.com>
 * 	Jim Guilford    <james.guilford@intel.com>
 * 	Vinodh Gopal    <vinodh.gopal@intel.com>
 * 	Erdinc Ozturk   <erdinc.ozturk@intel.com>
 * 	Jim Kukunas     <james.t.kukunas@linux.intel.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

// PCLMULQDQ を使って CRC-32 を更新する
unsigned int crc_update(unsigned int crc, unsigned char *buf, unsigned int len)
{
	__declspec( align(16) ) unsigned int buf128[4];
	unsigned int i;
	__m128i crc128, data128, temp128, two_k128;

	// special case; shorter than 19 bytes or miss-alignment
	if (((cpu_flag & 8) == 0) || (len < 19))
		return crc_update_std(crc, buf, len);
	// 4バイト境界までは 1バイトずつ計算する
	while (((ULONG_PTR)buf) & 3){
		crc = crc_table[(crc & 0xFF) ^ (*buf++)] ^ (crc >> 8);
		len--;
	}

	i = ((ULONG_PTR)buf) & 12;
	if (i != 0){	// read first 4, 8, or 12 bytes until memory alignment
		i = 16 - i;	// how many bytes to read
		len -= i;
		i /= 4;
		buf128[0] = 0;
		buf128[1] = 0;
		buf128[2] = 0;
		buf128[3] = 0;
		buf128[4 - i] ^= crc;	// set initial value
		while (i > 0){
			buf128[4 - i] ^= *((unsigned int *)buf);
			buf += 4;
			i--;
		}
	} else {	// read first 16 bytes
		buf128[0] = ((unsigned int *)buf)[0];
		buf128[1] = ((unsigned int *)buf)[1];
		buf128[2] = ((unsigned int *)buf)[2];
		buf128[3] = ((unsigned int *)buf)[3];
		buf128[0] ^= crc;	// set initial value
		len -= 16;
		buf += 16;
	}
	crc128 = _mm_load_si128((__m128i *)buf128);

	// set two constants; K1 = 0xccaa009e, K2 = 0x1751997d0
	two_k128 = _mm_set_epi32(0x00000001, 0x751997d0, 0x00000000, 0xccaa009e);

	// per 16 bytes
	while (len >= 16){
		data128 = _mm_load_si128((__m128i *)buf);

		temp128 = _mm_clmulepi64_si128(crc128, two_k128, 0x10);
		crc128  = _mm_clmulepi64_si128(crc128, two_k128, 0x01);
		data128 = _mm_xor_si128(data128, temp128);
		crc128  = _mm_xor_si128(crc128, data128);

		len -= 16;
		buf += 16;
	}

	// set two constants; K5 = 0xccaa009e, K6 = 0x163cd6124
	two_k128 = _mm_set_epi32(0x00000001, 0x63cd6124, 0x00000000, 0xccaa009e);

	// reduce from 128-bit to 96-bit by multiplication with K5
	data128 = _mm_clmulepi64_si128(crc128, two_k128, 0);
	temp128 = _mm_srli_si128(crc128, 8);
	data128 = _mm_xor_si128(data128, temp128);
	// reduce from 96-bit to 64-bit by multiplication with K6
	temp128 = _mm_slli_si128(data128, 4);
	crc128 = _mm_clmulepi64_si128(temp128, two_k128, 0x10);
	crc128 = _mm_xor_si128(crc128, data128);

	// set two constants; K7 = 0x1f7011640, K8 = 0x1db710640
	two_k128 = _mm_set_epi32(0x00000001, 0xdb710640, 0x00000001, 0xf7011640);

	// Barrett Reduction from 64-bit to 32-bit
	data128 = _mm_and_si128(crc128, _mm_set_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000));
	temp128 = _mm_clmulepi64_si128(data128, two_k128, 0);
	temp128 = _mm_xor_si128(temp128, data128);
	temp128 = _mm_and_si128(temp128, _mm_set_epi32(0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF));
	crc128 = _mm_clmulepi64_si128(temp128, two_k128, 0x10);
	crc128 = _mm_xor_si128(crc128, temp128);
	crc128 = _mm_xor_si128(crc128, data128);

	crc = _mm_extract_epi32(crc128, 2);

	// per 1 byte rest
	while (len--)
		crc = crc_table[(crc & 0xFF) ^ (*buf++)] ^ (crc >> 8);

	return crc;
}

// 内容が全て 0 のデータの CRC-32 を更新する
unsigned int crc_update_zero(unsigned int crc, unsigned int len)
{
	__m128i crc128, data128, temp128, two_k128;

	// special case; shorter than 16 bytes
	if (((cpu_flag & 8) == 0) || (len < 16)){
		while (len--)
			crc = crc_table[crc & 0xFF] ^ (crc >> 8);

		return crc;
	}

	// first 16 bytes
	len -= 16;
	crc128 = _mm_cvtsi32_si128(crc);	// set initial value

	// set two constants; K1 = 0xccaa009e, K2 = 0x1751997d0
	two_k128 = _mm_set_epi32(0x00000001, 0x751997d0, 0x00000000, 0xccaa009e);

	// per 16 bytes
	while (len >= 16){
		temp128 = _mm_clmulepi64_si128(crc128, two_k128, 0x10);
		crc128  = _mm_clmulepi64_si128(crc128, two_k128, 0x01);
		crc128  = _mm_xor_si128(crc128, temp128);

		len -= 16;
	}

	// set two constants; K5 = 0xccaa009e, K6 = 0x163cd6124
	two_k128 = _mm_set_epi32(0x00000001, 0x63cd6124, 0x00000000, 0xccaa009e);

	// reduce from 128-bit to 96-bit by multiplication with K5
	data128 = _mm_clmulepi64_si128(crc128, two_k128, 0);
	temp128 = _mm_srli_si128(crc128, 8);
	data128 = _mm_xor_si128(data128, temp128);
	// reduce from 96-bit to 64-bit by multiplication with K6
	temp128 = _mm_slli_si128(data128, 4);
	crc128 = _mm_clmulepi64_si128(temp128, two_k128, 0x10);
	crc128 = _mm_xor_si128(crc128, data128);

	// set two constants; K7 = 0x1f7011640, K8 = 0x1db710640
	two_k128 = _mm_set_epi32(0x00000001, 0xdb710640, 0x00000001, 0xf7011640);

	// Barrett Reduction from 64-bit to 32-bit
	data128 = _mm_and_si128(crc128, _mm_set_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000));
	temp128 = _mm_clmulepi64_si128(data128, two_k128, 0);
	temp128 = _mm_xor_si128(temp128, data128);
	temp128 = _mm_and_si128(temp128, _mm_set_epi32(0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF));
	crc128 = _mm_clmulepi64_si128(temp128, two_k128, 0x10);
	crc128 = _mm_xor_si128(crc128, temp128);
	crc128 = _mm_xor_si128(crc128, data128);

	crc = _mm_extract_epi32(crc128, 2);

	// per 1 byte rest
	while (len--)
		crc = crc_table[crc & 0xFF] ^ (crc >> 8);

	return crc;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// par2cmdline を参考にした関数
// window サイズの CRC を計算してある所に、1バイトずつ追加と削除をして、CRC を更新する

//  This file is part of par2cmdline (a PAR 2.0 compatible file verification and
//  repair tool). See http://parchive.sourceforge.net for details of PAR 2.0.
//
//  Copyright (c) 2003 Peter Brian Clements
//
//  par2cmdline is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.

unsigned int window_table[256];	// 詳細検査で CRC-32 をスライドさせる為のテーブル
unsigned int window_mask = 0;
// 先に window_mask を計算してソース・ブロックのチェックサムに XOR することで、
// 初期値と最終処理の 0xFFFFFFFF の影響を消す。
// そうすることで、スライド時に window_mask を使わないで済む。

// Slide the CRC along a buffer by one character (removing the old and adding the new).
// The new character is added using the main CCITT CRC32 table, and the old character
// is removed using the windowtable.
/*
unsigned int crc_slide_char(unsigned int crc, unsigned char chNew, unsigned char chOld){
	return crc_table[(crc & 0xFF) ^ chNew] ^ (crc >> 8) ^ window_table[chOld];
}
*/

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// Jonathan Camacho が改良した計算方法

// CRC( XOR(A,B) ) == XOR(CRC(A), CRC(B))
// bit twiddling to construct windowtable and windowmask
// window_table と window_mask の計算が 257倍? 速くなるらしい。

static void compute_result_table(unsigned int result, unsigned int result_array[32])
{
	int i;

	for (i = 0; i < 32; i++){
		result = ((result >> 1) & 0x7FFFFFFFL) ^ ((result & 1) ? CRC32_POLY : 0);
		result_array[i] = result;
	}
}

static void fast_compute_crc_table(unsigned int result_array[8], unsigned int table[256])
{
	int i, j, value;
	unsigned int new_crc;

	table[0] = 0; // g_CrcTable[0 & 0xff] ^ (0 >> 8) は常に 0
	for (i = 1; i < 256; i++){
		//Firstly, find the correct masks that we need.
		//result_array[0] is 128,
		//result_array[1] is 64
		//result_array[2] is 32
		//result_array[3] is 16
		//result_array[4] is 8
		//result_array[5] is 4
		//result_array[6] is 2
		//result_array[7] is 1.
		//The other values in result_array are not needed right now.
		//So basically, for all values of i 0..255 we need to xor
		//together the result_array values that it represents.
		new_crc = 0;
		value = i;
		for (j = 0; j < 8; j++){
			new_crc = new_crc ^ ( (value & 128) ? result_array[j] : 0) ;
			value = value << 1;
		}
		table[i] = new_crc;
//		printf("table[%d] = %08x\n", i, table[i]);
	}
}

/*
unsigned int onepass_window_gen(unsigned int window_size, unsigned int window_table[256])
{
	unsigned int result = 1;
	unsigned int i;
	unsigned int masked_result_array[32];
	unsigned int window_mask;

	if (window_size > 4){
		result = crc_update_zero(result, window_size - 4);
		compute_result_table(result, masked_result_array);
		window_mask = 0;
		for (i = 0; i < 32; i++){
			window_mask = window_mask ^ masked_result_array[i];
		}
		window_mask = window_mask ^ ~0;
		for (i = 0; i < 4; i++){
			result = crc_table[(result & 0xFF)] ^ (result >> 8);
		}
		compute_result_table(result, masked_result_array);
		fast_compute_crc_table(masked_result_array, window_table);

	} else { // 普通? に計算する
		window_table[0] = 0; // crc_table[0 & 0xff] ^ (0 >> 8) は常に 0
		for (i = 1; i < 256; i++){
			window_table[i] = crc_update_zero(crc_table[i], window_size); // 0が window サイズ個並んだデータの CRC
		}
		window_mask = crc_update_zero(0xFFFFFFFF, window_size);
		window_mask ^= 0xFFFFFFFF;
	}

//	printf("window_mask = %08x\n", window_mask);
//	for (i = 0; i < 256; i++)
//		printf("window_table[%d] = %08x\n", i, window_table[i]);
	return window_mask;
}
*/
void onepass_window_gen(unsigned int window_size)
{
	unsigned int result = 1;
	unsigned int i;
	unsigned int masked_result_array[32];

	if (window_size <= 4){
		window_mask = 0x2144DF1C;	// 4バイトの 0 に対する CRC-32
		return;	// ブロック・サイズが 4以下の時はスライド検査しない
	}

	result = crc_update_zero(result, window_size - 4);
	compute_result_table(result, masked_result_array);
	window_mask = 0;
	for (i = 0; i < 32; i++){
		window_mask = window_mask ^ masked_result_array[i];
	}
	window_mask = window_mask ^ 0xFFFFFFFF;
	for (i = 0; i < 4; i++){
		result = crc_table[(result & 0xFF)] ^ (result >> 8);
	}
	compute_result_table(result, masked_result_array);
	fast_compute_crc_table(masked_result_array, window_table);
}

// 初期値と最終処理の 0xFFFFFFFF を使ってない CRC のスライドには window_mask は必要ない
void onepass_window_gen_short(unsigned int short_size, unsigned int short_table[256])
{
	unsigned int result = 1;
	unsigned int masked_result_array[32];

	// short_size must be larger than 4-byte
	result = crc_update_zero(result, short_size);
	compute_result_table(result, masked_result_array);
	fast_compute_crc_table(masked_result_array, short_table);
}

