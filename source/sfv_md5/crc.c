// crc.c
// Copyright : 2021-05-14 Yutaka Sawada
// License : The MIT license

#include "crc.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// CRC-32 計算用

// CRC-32
#define CRC_POLY	0xEDB88320	// (little endian)
unsigned int crc_table[256];

// CRC 計算用のテーブルを作る
void init_crc_table(void)
{
	unsigned int i, j, r;

	for (i = 0; i < 256; i++){	// CRC-32
		r = i;
		for (j = 0; j < 8; j++)
			r = (r >> 1) ^ (CRC_POLY & ~((r & 1) - 1));
		crc_table[i] = r;
	}
}

// CRC-32 を更新する
unsigned int crc_update(unsigned int crc, unsigned char *buf, unsigned int len)
{
/*
	while (len--)
		crc = crc_table[(crc & 0xFF) ^ (*buf++)] ^ (crc >> 8);
*/
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

