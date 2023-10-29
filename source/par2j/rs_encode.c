// rs_encode.c
// Copyright : 2023-10-29 Yutaka Sawada
// License : GPL

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600	// Windows Vista or later
#endif

#include <malloc.h>
#include <process.h>
#include <stdio.h>

#include <windows.h>

#include "common2.h"
#include "crc.h"
#include "create.h"
#include "gf16.h"
#include "phmd5.h"
#include "lib_opencl.h"
#include "reedsolomon.h"
#include "rs_encode.h"


#ifdef TIMER
static unsigned int time_start, time_read = 0, time_write = 0, time_calc = 0;
static unsigned int read_count, skip_count;
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// マルチスレッドCPU用のサブ・スレッド

typedef struct {	// RS threading control struct
	unsigned short *mat;	// 行列
	unsigned char * volatile buf;
	volatile unsigned int size;		// バイト数
	volatile unsigned int len;
	volatile int count;
	volatile int off;
	volatile int now;
	HANDLE run;
	HANDLE end;
} RS_TH;

// chunk ごとに計算するためのスレッド
static DWORD WINAPI thread_encode2(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf, *work_buf;
	unsigned short *constant, factor, factor2;
	int i, j, max_num, chunk_num;
	int part_off, part_num, part_now;
	int src_off, src_num;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	constant = th->mat;
	p_buf = th->buf;
	unit_size = th->size;
	chunk_size = th->len;
	part_num = th->count;
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	chunk_num = (unit_size + chunk_size - 1) / chunk_size;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		src_off = th->off;	// ソース・ブロック番号

		if (th->size == 0){	// ソース・ブロック読み込み中
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < part_num){	// j = ++th_now
				if (src_off == 0)	// 最初のブロックを計算する際に
					memset(p_buf + (size_t)unit_size * j, 0, unit_size);	// ブロックを 0で埋める
				factor = galois_power(constant[src_off], first_num + j);	// factor は定数行列の乗数になる
				galois_align_multiply(s_buf, p_buf + (size_t)unit_size * j, unit_size, factor);
#ifdef TIMER
loop_count2a++;
#endif
			}

#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// パリティ・ブロックを部分的に保持する場合
			// スレッドごとに作成するパリティ・ブロックの chunk を変える
			src_num = th->len;
			part_now = th->size;
			part_off = th->count;
			len = chunk_size;
			max_num = chunk_num * part_now;
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / part_now;	// chunk の番号
				j = j % part_now;	// parity の番号
				off *= chunk_size;	// chunk の位置
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				work_buf = p_buf + (size_t)unit_size * j + off;
				if (src_off == 0)	// 最初のブロックを計算する際に
					memset(work_buf, 0, len);	// パリティ・ブロックを 0で埋める

				// ソース・ブロックごとにパリティを追加していく
				if (galois_align_multiply2 != NULL){	// ２ブロックずつ計算する場合 (SSSE3 か AVX2)
					i = 0;
					if (src_num & 1){	// 奇数なら最初の一個を計算して、残りを偶数に変える
						factor = galois_power(constant[src_off + i], first_num + part_off + j);	// factor は定数行列の乗数になる
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor);
						i++;
					}
					for (; i < src_num; i += 2){
						factor = galois_power(constant[src_off + i], first_num + part_off + j);	// 二つ連続で計算する
						factor2 = galois_power(constant[src_off + i + 1], first_num + part_off + j);
						galois_align_multiply2(s_buf + (size_t)unit_size * i + off, s_buf + (size_t)unit_size * (i + 1) + off,
									work_buf, len, factor, factor2);
					}

				} else {	// 一つずつ計算する場合
					for (i = 0; i < src_num; i++){
						factor = galois_power(constant[src_off + i], first_num + part_off + j);	// factor は定数行列の乗数になる
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor);
					}
				}
#ifdef TIMER
loop_count2b += src_num;
#endif
			}
#ifdef TIMER
time_encode2b += GetTickCount() - time_start2;
#endif
		}
		//_mm_sfence();	// メモリーへの書き込みを完了する
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
loop_count2b /= chunk_num;	// chunk数で割ってブロック数にする
printf("sub-thread : total loop = %d\n", loop_count2a + loop_count2b);
if (time_encode2a > 0){
	i = (int)((__int64)loop_count2a * unit_size * 125 / ((__int64)time_encode2a * 131072));
} else {
	i = 0;
}
if (loop_count2a > 0)
	printf(" 1st encode %d.%03d sec, %d loop, %d MB/s\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a, i);
if (time_encode2b > 0){
	i = (int)((__int64)loop_count2b * unit_size * 125 / ((__int64)time_encode2b * 131072));
} else {
	i = 0;
}
printf(" 2nd encode %d.%03d sec, %d loop, %d MB/s\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b, i);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

static DWORD WINAPI thread_encode3(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf, *work_buf;
	unsigned short *constant, factor, factor2;
	int i, j, max_num, chunk_num;
	int src_off, src_num;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	constant = th->mat;
	p_buf = th->buf;
	unit_size = th->size;
	chunk_size = th->len;
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	chunk_num = (unit_size + chunk_size - 1) / chunk_size;
	max_num = chunk_num * parity_num;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		src_off = th->off;	// ソース・ブロック番号

		if (th->size == 0){	// ソース・ブロック読み込み中
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < parity_num){	// j = ++th_now
				if (src_off == 0)	// 最初のブロックを計算する際に
					memset(p_buf + (size_t)unit_size * j, 0, unit_size);	// ブロックを 0で埋める
				factor = galois_power(constant[src_off], first_num + j);	// factor は定数行列の乗数になる
				galois_align_multiply(s_buf, p_buf + (size_t)unit_size * j, unit_size, factor);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// 全てのパリティ・ブロックを保持する場合
			// スレッドごとに作成するパリティ・ブロックの chunk を変える
			src_num = th->size;
			len = chunk_size;
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / parity_num;	// chunk の番号
				j = j % parity_num;		// parity の番号
				off *= chunk_size;		// chunk の位置
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				work_buf = p_buf + (size_t)unit_size * j + off;

				// ソース・ブロックごとにパリティを追加していく
				if (galois_align_multiply2 != NULL){	// ２ブロックずつ計算する場合 (SSSE3 か AVX2)
					i = 0;
					if (src_num & 1){	// 奇数なら最初の一個を計算して、残りを偶数に変える
						factor = galois_power(constant[src_off + i], first_num + j);	// factor は定数行列の乗数になる
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor);
						i++;
					}
					for (; i < src_num; i += 2){
						factor = galois_power(constant[src_off + i], first_num + j);	// 二つ連続で計算する
						factor2 = galois_power(constant[src_off + i + 1], first_num + j);
						galois_align_multiply2(s_buf + (size_t)unit_size * i + off, s_buf + (size_t)unit_size * (i + 1) + off,
									work_buf, len, factor, factor2);
					}

				} else {	// 一つずつ計算する場合
					for (i = 0; i < src_num; i++){
						factor = galois_power(constant[src_off + i], first_num + j);	// factor は定数行列の乗数になる
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor);
					}
				}
#ifdef TIMER
loop_count2b += src_num;
#endif
			}
#ifdef TIMER
time_encode2b += GetTickCount() - time_start2;
#endif
		}
		//_mm_sfence();	// メモリーへの書き込みを完了する
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
loop_count2b /= chunk_num;	// chunk数で割ってブロック数にする
printf("sub-thread : total loop = %d\n", loop_count2a + loop_count2b);
if (time_encode2a > 0){
	i = (int)((__int64)loop_count2a * unit_size * 125 / ((__int64)time_encode2a * 131072));
} else {
	i = 0;
}
if (loop_count2a > 0)
	printf(" 1st encode %d.%03d sec, %d loop, %d MB/s\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a, i);
if (time_encode2b > 0){
	i = (int)((__int64)loop_count2b * unit_size * 125 / ((__int64)time_encode2b * 131072));
} else {
	i = 0;
}
printf(" 2nd encode %d.%03d sec, %d loop, %d MB/s\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b, i);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

// GPU 対応のサブ・スレッド (最後のスレッドなので、1st encode では呼ばれない)
static DWORD WINAPI thread_encode_gpu(LPVOID lpParameter)
{
	unsigned char *s_buf, *g_buf;
	unsigned short *constant, *factor;
	int i, j;
	int src_off, src_num;
	unsigned int unit_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int time_start2, time_encode2 = 0, loop_count2 = 0;
#endif

	th = (RS_TH *)lpParameter;
	constant = th->mat;
	g_buf = th->buf;
	unit_size = th->size;
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	factor = constant + source_num;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		// GPUはソース・ブロック読み込み中に呼ばれない
		s_buf = th->buf;
		src_off = th->off;	// ソース・ブロック番号
		src_num = th->size;

		// 最初にソース・ブロックをVRAMへ転送する
		i = gpu_copy_blocks(s_buf, unit_size, src_num);
		if (i != 0){
			th->len = i;
			InterlockedExchange(&(th->now), INT_MAX / 3);	// サブ・スレッドの計算を中断する
		}

		// 一つの GPUスレッドが全てのパリティ・ブロックを処理する
		while ((j = InterlockedIncrement(&(th->now))) < parity_num){	// j = ++th_now
			// factor は定数行列の乗数になる
			for (i = 0; i < src_num; i++)
				factor[i] = galois_power(constant[src_off + i], first_num + j);

			// VRAM上のソース・ブロックごとにパリティを追加していく
			i = gpu_multiply_blocks(src_num, factor, g_buf + (size_t)unit_size * j, unit_size);
			if (i != 0){
				th->len = i;
				InterlockedExchange(&(th->now), INT_MAX / 3);	// サブ・スレッドの計算を中断する
				break;
			}
#ifdef TIMER
loop_count2 += src_num;
#endif
		}
#ifdef TIMER
time_encode2 += GetTickCount() - time_start2;
#endif
		// 最後にVRAMを解放する
		i = gpu_finish();
		if ((i != 0) && (th->len == 0))
			th->len = i;	// 初めてエラーが発生した時だけセットする

		//_mm_sfence();	// メモリーへの書き込みを完了する
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
printf("gpu-thread :\n");
if (time_encode2 > 0){
	i = (int)((__int64)loop_count2 * unit_size * 125 / ((__int64)time_encode2 * 131072));
} else {
	i = 0;
}
printf(" 2nd encode %d.%03d sec, %d loop, %d MB/s\n", time_encode2 / 1000, time_encode2 % 1000, loop_count2, i);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int encode_method1(	// ソース・ブロックが一個だけの場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk)		// パリティ・ブロックの情報
{
	unsigned char *buf = NULL, *work_buf, *hash;
	int err = 0, i, j;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	PHMD5 md_ctx, *md_ptr = NULL;

	// 作業バッファーを確保する
	io_size = get_io_size(2, NULL, 1, sse_unit);
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1)) - HASH_SIZE;	// 実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	len = 2 * unit_size + HASH_SIZE;
	buf = _aligned_malloc(len, sse_unit);
	if (buf == NULL){
		printf("malloc, %d\n", len);
		err = 1;
		goto error_end;
	}
	work_buf = buf + unit_size;
	hash = work_buf + unit_size;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_base *= parity_num;	// 全体の断片の個数
#ifdef TIMER
	printf("\n read one source block, and keep one parity block\n");
	printf("buffer size = %d MB, io_size = %d, split = %d\n", len >> 20, io_size, (block_size + io_size - 1) / io_size);
	j = try_cache_blocking(unit_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF0000, j, (unit_size + j - 1) / j);
#endif

	if (io_size < block_size){	// スライスが分割される場合だけ、途中までのハッシュ値を保持する
		len = sizeof(PHMD5) * parity_num;
		md_ptr = malloc(len);
		if (md_ptr == NULL){
			printf("malloc, %d\n", len);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++){
			Phmd5Begin(&(md_ptr[i]));
			j = first_num + i;	// 最初の番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			Phmd5Process(&(md_ptr[i]), header_buf + 32, 36);
		}
	}

	// ソース・ファイルを開く
	wcscpy(file_path, base_dir);
	wcscpy(file_path + base_len, list_buf + files[s_blk[0].file].name);
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		hFile = NULL;
		printf_cp("cannot open file, %s\n", list_buf + files[s_blk[0].file].name);
		err = 1;
		goto error_end;
	}

	// バッファー・サイズごとにパリティ・ブロックを作成する
	time_last = GetTickCount();
	s_blk[0].crc = 0xFFFFFFFF;
	block_off = 0;
	while (block_off < block_size){
#ifdef TIMER
time_start = GetTickCount();
#endif
		// ソース・ブロックを読み込む
		len = s_blk[0].size - block_off;
		if (len > io_size)
			len = io_size;
		if (file_read_data(hFile, (__int64)block_off, buf, len)){
			printf("file_read_data, input slice %d\n", i);
			err = 1;
			goto error_end;
		}
		if (len < io_size)
			memset(buf + len, 0, io_size - len);
		// ソース・ブロックのチェックサムを計算する
		s_blk[0].crc = crc_update(s_blk[0].crc, buf, len);	// without pad
		checksum16_altmap(buf, buf + io_size, io_size);
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		// リカバリ・ファイルに書き込むサイズ
		if (block_size - block_off < io_size){
			len = block_size - block_off;
		} else {
			len = io_size;
		}

		// パリティ・ブロックごとに
		for (i = 0; i < parity_num; i++){
#ifdef TIMER
time_start = GetTickCount();
#endif
			memset(work_buf, 0, unit_size);
			// factor は 2の乗数になる
			galois_align_multiply(buf, work_buf, unit_size, galois_power(2, first_num + i));
#ifdef TIMER
time_calc += GetTickCount() - time_start;
#endif

			// 経過表示
			prog_num++;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((prog_num * 1000) / prog_base)){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

#ifdef TIMER
time_start = GetTickCount();
#endif
			// パリティ・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
			// ハッシュ値を計算して、リカバリ・ファイルに書き込む
			if (io_size >= block_size){	// 1回で書き込みが終わるなら
				Phmd5Begin(&md_ctx);
				j = first_num + i;	// 最初の番号の分だけ足す
				memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
				Phmd5Process(&md_ctx, header_buf + 32, 36);
				Phmd5Process(&md_ctx, work_buf, len);
				Phmd5End(&md_ctx);
				memcpy(header_buf + 16, md_ctx.hash, 16);
				// ヘッダーを書き込む
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off - 68, header_buf, 68)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
			} else {
				Phmd5Process(&(md_ptr[i]), work_buf, len);
			}
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off, work_buf, len)){
				printf("file_write_data, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
		}

		block_off += io_size;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	if (s_blk[0].size < block_size){	// 残りを 0 でパディングする
		len = block_size - s_blk[0].size;
		memset(buf, 0, len);
		s_blk[0].crc = crc_update(s_blk[0].crc, buf, len);
	}
	s_blk[0].crc ^= 0xFFFFFFFF;
	if (((unsigned int *)(files[s_blk[0].file].hash))[0] != s_blk[0].crc){
		printf("checksum mismatch, input file %d\n", s_blk[0].file);
		err = 1;
		goto error_end;
	}

	if (io_size < block_size){	// 1回で書き込みが終わらなかったなら
		if (GetTickCount() - time_last >= UPDATE_TIME){	// キャンセルを受け付ける
			if (cancel_progress()){
				err = 2;
				goto error_end;
			}
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 最後に Recovery Slice packet のヘッダーを書き込む
		for (i = 0; i < parity_num; i++){
			Phmd5End(&(md_ptr[i]));
			memcpy(header_buf + 16, md_ptr[i].hash, 16);
			j = first_num + i;	// 最初のパリティ・ブロック番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			// リカバリ・ファイルに書き込む
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){	// ヘッダーのサイズ分だけずらす
				printf("file_write_data, packet header\n");
				err = 1;
				goto error_end;
			}
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
printf("encode %d.%03d sec\n", time_calc / 1000, time_calc % 1000);
#endif

error_end:
	if (md_ptr)
		free(md_ptr);
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// chunk ごとに計算するバージョン Cache Blocking for CPU's L3 cache

int encode_method2(	// ソース・データを全て読み込む場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant)	// 複数ブロック分の領域を確保しておく？
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	int err = 0, i, j, last_file, chunk_num;
	int part_off, part_num, part_now;
	int cpu_num1, src_off, src_num, src_max;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_read, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 md_ctx, *md_ptr = NULL;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);

	// 作業バッファーを確保する
	part_num = parity_num;	// 最大値を初期値にする
	//part_num = (parity_num + 1) / 2;	// 確保量の実験用
	//part_num = (parity_num + 2) / 3;	// 確保量の実験用
	io_size = get_io_size(source_num, &part_num, 1, sse_unit);
	//io_size = (((io_size + 1) / 2 + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1)) - HASH_SIZE;	// 2分割の実験用
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1)) - HASH_SIZE;	// 3分割の実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	file_off = (source_num + part_num) * (size_t)unit_size + HASH_SIZE;
	buf = _aligned_malloc((size_t)file_off, sse_unit);
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	//memset(buf, 0xFF, (size_t)file_off);	// 後から 0 埋めしてるかの実験用
	p_buf = buf + (size_t)unit_size * source_num;	// パリティ・ブロックを部分的に記録する領域
	hash = p_buf + (size_t)unit_size * part_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_read = (parity_num + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base *= (__int64)(source_num + prog_write) * parity_num + prog_read * source_num;	// 全体の断片の個数
	len = try_cache_blocking(unit_size);
	//len = ((len + 2) / 3 + (sse_unit - 1)) & ~(sse_unit - 1);	// 1/3の実験用
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num1(part_num);	// 読み込み中はスレッド数を減らす
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (cpu_num == 1))
		src_max = 0x8000;	// 不明または少な過ぎる場合は、制限しない
#ifdef TIMER
	printf("\n read all source blocks, and keep some parity blocks\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
	printf("cache: limit size = %d, chunk_size = %d, chunk_num = %d\n", cpu_flag & 0x7FFF0000, len, chunk_num);
	printf("unit_size = %d, part_num = %d, cpu_num1 = %d, src_max = %d\n", unit_size, part_num, cpu_num1, src_max);
#endif

	if (io_size < block_size){	// スライスが分割される場合だけ、途中までのハッシュ値を保持する
		block_off = sizeof(PHMD5) * parity_num;
		md_ptr = malloc(block_off);
		if (md_ptr == NULL){
			printf("malloc, %d\n", block_off);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++){
			Phmd5Begin(&(md_ptr[i]));
			j = first_num + i;	// 最初の番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			Phmd5Process(&(md_ptr[i]), header_buf + 32, 36);
		}
	}

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th->buf = p_buf;
	th->size = unit_size;
	th->count = part_num;
	th->len = len;	// キャッシュの最適化を試みる
	for (j = 0; j < cpu_num; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		th->run = hRun[j];
		th->end = hEnd[j];
		//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
		hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode2, (LPVOID)th, 0, NULL);
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// ソース・ブロック断片を読み込んで、パリティ・ブロック断片を作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->size = 0;	// 1st encode
		src_off = -1;	// まだ計算して無い印

		// ソース・ブロックを読み込む
#ifdef TIMER
read_count = 0;
skip_count = 0;
time_start = GetTickCount();
#endif
		last_file = -1;
		for (i = 0; i < source_num; i++){
			if (s_blk[i].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[i].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
				file_off = block_off;
			} else {	// 同じファイルならブロック・サイズ分ずらす
				file_off += block_size;
			}
			if (s_blk[i].size > block_off){	// バッファーにソース・ファイルの内容を読み込む
				len = s_blk[i].size - block_off;
				if (len > io_size)
					len = io_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", i);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				// ソース・ブロックのチェックサムを計算する
				if (block_off == 0)
					s_blk[i].crc = 0xFFFFFFFF;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf + (size_t)unit_size * i, len);	// without pad
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif

				if (src_off < 0){
					src_num = i + 1;	// 最後のブロックより前なら
				} else {
					src_num = i / (src_off + 1);	// だいたい何ブロック読むごとに計算が終わるか
					src_num += i + 1;	// 次のブロック番号を足す
				}
				if (src_num < source_num){	// 読み込みが終わる前に計算が終わりそうなら
					// サブ・スレッドの動作状況を調べる
					j = WaitForMultipleObjects(cpu_num1, hEnd, TRUE, 0);
					if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
						// 経過表示
						prog_num += part_num;
						if (GetTickCount() - time_last >= UPDATE_TIME){
							if (print_progress((int)((prog_num * 1000) / prog_base))){
								err = 2;
								goto error_end;
							}
							time_last = GetTickCount();
						}
						// 計算終了したブロックの次から計算を開始する
						src_off += 1;
						if (src_off > 0){	// バッファーに読み込んだ時だけ計算する
							while (s_blk[src_off].size <= block_off){
								prog_num += part_num;
								src_off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						th->buf = buf + (size_t)unit_size * src_off;
						th->off = src_off;
						th->now = -1;	// 初期値 - 1
						//_mm_sfence();
						for (j = 0; j < cpu_num1; j++){
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
					}
				}
			} else {
				memset(buf + (size_t)unit_size * i, 0, unit_size);
			}

			// 経過表示
			prog_num += prog_read;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}
		}
		// 最後のソース・ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off > 0){
			while (s_blk[src_off].size <= block_off){	// 計算不要なソース・ブロックはとばす
				prog_num += part_num;
				src_off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		}
		// 1st encode しなかった場合（src_off = 0）は、2nd encode で生成ブロックをゼロ埋めする
#ifdef TIMER
		j = (src_off * 1000) / source_num;
		printf("partial encode = %d / %d (%d.%d%%), read = %d, skip = %d\n", src_off, source_num, j / 10, j % 10, read_count, skip_count);
		// ここまでのパリティ・ブロックのチェックサムを検証する
/*		if (src_off > 0){
			for (j = 0; j < part_num; j++){
				checksum16_return(p_buf + (size_t)unit_size * j, hash, io_size);
				if (memcmp(p_buf + ((size_t)unit_size * j + io_size), hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovery slice %d after 1st encode\n", j);
					err = 1;
					goto error_end;
				}
				galois_altmap_change(p_buf + (size_t)unit_size * j, unit_size);
			}
		}*/
#endif

		// リカバリ・ファイルに書き込むサイズ
		if (block_size - block_off < io_size){
			len = block_size - block_off;
		} else {
			len = io_size;
		}

		// part_now ごとに処理する
		part_off = 0;
		part_now = part_num;
		while (part_off < parity_num){
			if (part_off + part_now > parity_num)
				part_now = parity_num - part_off;

			// スレッドごとにパリティ・ブロックを計算する
			th->count = part_off;
			th->size = part_now;
			if (part_off > 0)
				src_off = 0;	// 最初の計算以降は全てのソース・ブロックを対象にする
			src_num = src_max;	// 一度に処理するソース・ブロックの数を制限する
#ifdef TIMER
			printf("part_off = %d, part_now = %d, src_off = %d\n", part_off, part_now, src_off);
#endif
			while (src_off < source_num){
				// ソース・ブロックを何個ずつ処理するか
				if (src_off + src_num * 2 - 1 >= source_num)
					src_num = source_num - src_off;
				//printf("src_off = %d, src_num = %d\n", src_off, src_num);

				th->buf = buf + (size_t)unit_size * src_off;
				th->off = src_off;
				th->len = src_num;
				th->now = -1;	// 初期値 - 1
				//_mm_sfence();
				for (j = 0; j < cpu_num; j++){
					ResetEvent(hEnd[j]);	// リセットしておく
					SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
				}

				// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
				while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
					// th-now が最高値なので、計算が終わってるのは th-now + 1 - cpu_num 個となる
					j = th->now + 1 - cpu_num;
					if (j < 0)
						j = 0;
					j /= chunk_num;	// chunk数で割ってブロック数にする
					// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
					if (print_progress((int)(((prog_num + src_num * j) * 1000) / prog_base))){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}

				// 経過表示
				prog_num += src_num * part_now;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress((int)((prog_num * 1000) / prog_base))){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}

				src_off += src_num;
			}

#ifdef TIMER
time_start = GetTickCount();
#endif
			// パリティ・ブロックを書き込む
			work_buf = p_buf;
			for (i = part_off; i < part_off + part_now; i++){
				// パリティ・ブロックのチェックサムを検証する
				checksum16_return(work_buf, hash, io_size);
				if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				// ハッシュ値を計算して、リカバリ・ファイルに書き込む
				if (io_size >= block_size){	// 1回で書き込みが終わるなら
					Phmd5Begin(&md_ctx);
					j = first_num + i;	// 最初の番号の分だけ足す
					memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
					Phmd5Process(&md_ctx, header_buf + 32, 36);
					Phmd5Process(&md_ctx, work_buf, len);
					Phmd5End(&md_ctx);
					memcpy(header_buf + 16, md_ctx.hash, 16);
					// ヘッダーを書き込む
					if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off - 68, header_buf, 68)){
						printf("file_write_data, recovery slice %d\n", i);
						err = 1;
						goto error_end;
					}
				} else {
					Phmd5Process(&(md_ptr[i]), work_buf, len);
				}
				//printf("%d, buf = %p, size = %u, off = %I64d\n", i, work_buf, len, p_blk[i].off + block_off);
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off, work_buf, len)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				work_buf += unit_size;

				// 経過表示
				prog_num += prog_write;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress((int)((prog_num * 1000) / prog_base))){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}
			}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif

			part_off += part_num;	// 次のパリティ位置にする
		}

		block_off += io_size;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	memset(buf, 0, io_size);
	j = 0;
	while (j < source_num){
		last_file = s_blk[j].file;
		src_num = (int)((files[last_file].size + (__int64)block_size - 1) / block_size);
		i = j + src_num - 1;	// 末尾ブロックの番号
		if (s_blk[i].size < block_size){	// 残りを 0 でパディングする
			len = block_size - s_blk[i].size;
			while (len > io_size){
				len -= io_size;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf, io_size);
			}
			s_blk[i].crc = crc_update(s_blk[i].crc, buf, len);
		}
		memset(hash, 0, 16);
		for (i = 0; i < src_num; i++)	// XOR して 16バイトに減らす
			((unsigned int *)hash)[i & 3] ^= s_blk[j + i].crc ^ 0xFFFFFFFF;
		if (memcmp(files[last_file].hash, hash, 16) != 0){
			printf("checksum mismatch, input file %d\n", last_file);
			err = 1;
			goto error_end;
		}
		j += src_num;
	}

	//printf("io_size = %d, block_size = %d\n", io_size, block_size);
	if (io_size < block_size){	// 1回で書き込みが終わらなかったなら
		if (GetTickCount() - time_last >= UPDATE_TIME){	// キャンセルを受け付ける
			if (cancel_progress()){
				err = 2;
				goto error_end;
			}
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 最後に Recovery Slice packet のヘッダーを書き込む
		for (i = 0; i < parity_num; i++){
			Phmd5End(&(md_ptr[i]));
			memcpy(header_buf + 16, md_ptr[i].hash, 16);
			j = first_num + i;	// 最初のパリティ・ブロック番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			// リカバリ・ファイルに書き込む
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){	// ヘッダーのサイズ分だけずらす
				printf("file_write_data, packet header\n");
				err = 1;
				goto error_end;
			}
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
if (prog_num != prog_base)
	printf(" prog_num = %I64d, prog_base = %I64d\n", prog_num, prog_base);
#endif

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (md_ptr)
		free(md_ptr);
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

int encode_method3(	// パリティ・ブロックを全て保持して、一度に書き込む場合
	wchar_t *file_path,
	wchar_t *recovery_path,		// 作業用
	int packet_limit,			// リカバリ・ファイルのパケット繰り返しの制限
	int block_distri,			// パリティ・ブロックの分配方法 (3-bit目は番号の付け方)
	int packet_num,				// 共通パケットの数
	unsigned char *common_buf,	// 共通パケットのバッファー
	int common_size,			// 共通パケットのバッファー・サイズ
	unsigned char *footer_buf,	// 末尾パケットのバッファー
	int footer_size,			// 末尾パケットのバッファー・サイズ
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	unsigned short *constant)
{
	unsigned char *buf = NULL, *p_buf;
	int err = 0, i, j, last_file, chunk_num;
	int source_off, read_num, packet_off;
	int cpu_num1, src_off, src_num, src_max;
	unsigned int unit_size, len;
	unsigned int time_last, prog_read, prog_write;
	__int64 prog_num = 0, prog_base;
	size_t mem_size;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 file_md_ctx, blk_md_ctx;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);	// チェックサムの分だけ増やす

	// 作業バッファーを確保する
	read_num = read_block_num(parity_num, 1, sse_unit);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
#ifdef TIMER
		printf("cannot keep enough blocks, use another method\n");
#endif
		return -2;	// スライスを分割して処理しないと無理
	}
	print_progress_text(0, "Creating recovery slice");
	//read_num = (read_num + 1) / 2 + 1;	// 2分割の実験用
	//read_num = (read_num + 2) / 3 + 1;	// 3分割の実験用
	mem_size = (size_t)(read_num + parity_num) * unit_size;
	buf = _aligned_malloc(mem_size, sse_unit);
	if (buf == NULL){
		printf("malloc, %Id\n", mem_size);
		err = 1;
		goto error_end;
	}
	//memset(buf, 0xFF, mem_size);	// 後から 0 埋めしてるかの実験用
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	prog_read = (parity_num + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base = (__int64)(source_num + prog_write) * parity_num + prog_read * source_num;	// ブロックの合計掛け算個数 + 読み書き回数
	len = try_cache_blocking(unit_size);
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num1(parity_num);	// 読み込み中はスレッド数を減らす
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (cpu_num == 1))
		src_max = 0x8000;	// 不明または少な過ぎる場合は、制限しない
#ifdef TIMER
	printf("\n read some source blocks, and keep all parity blocks\n");
	printf("buffer size = %Id MB, read_num = %d, round = %d\n", mem_size >> 20, read_num, (source_num + read_num - 1) / read_num);
	printf("cache: limit size = %d, chunk_size = %d, chunk_num = %d\n", cpu_flag & 0x7FFF0000, len, chunk_num);
	printf("unit_size = %d, cpu_num1 = %d, src_max = %d\n", unit_size, cpu_num1, src_max);
#endif

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th->buf = p_buf;
	th->size = unit_size;
	th->len = len;	// キャッシュの最適化を試みる
	for (j = 0; j < cpu_num; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		th->run = hRun[j];
		th->end = hEnd[j];
		//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
		hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode3, (LPVOID)th, 0, NULL);
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// 何回かに別けてソース・ブロックを読み込んで、パリティ・ブロックを少しずつ作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	last_file = -1;
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 0;	// 1st encode
		src_off = source_off - 1;	// まだ計算して無い印

#ifdef TIMER
time_start = GetTickCount();
#endif
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			// ソース・ブロックを読み込む
			if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
					// チェックサム・パケットの MD5 を計算する
					memcpy(&packet_off, files[last_file].hash + 8, 4);
					memcpy(&len, files[last_file].hash + 12, 4);
					//printf("Checksum[%d], off = %d, size = %d\n", last_file, packet_off, len);
					Phmd5Begin(&blk_md_ctx);
					Phmd5Process(&blk_md_ctx, common_buf + packet_off + 32, 32 + len);
					Phmd5End(&blk_md_ctx);
					memcpy(common_buf + packet_off + 16, blk_md_ctx.hash, 16);
					// ファイルのハッシュ値の計算を終える
					Phmd5End(&file_md_ctx);
					memcpy(&packet_off, files[last_file].hash, 4);	// ハッシュ値の位置 = off + 64 + 16
					memcpy(&len, files[last_file].hash + 4, 4);
					//printf("File[%d], off = %d, size = %d\n", last_file, packet_off, len);
					// ファイルのハッシュ値を書き込んでから、パケットの MD5 を計算する
					memcpy(common_buf + packet_off + 64 + 16, file_md_ctx.hash, 16);
					Phmd5Begin(&file_md_ctx);
					Phmd5Process(&file_md_ctx, common_buf + packet_off + 32, 32 + len);
					Phmd5End(&file_md_ctx);
					memcpy(common_buf + packet_off + 16, file_md_ctx.hash, 16);
				}
				last_file = s_blk[source_off + i].file;
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				// 1-pass方式なら、断片化しないので FILE_FLAG_SEQUENTIAL_SCAN を付けた方がいいかも
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
				// ファイルのハッシュ値の計算を始める
				Phmd5Begin(&file_md_ctx);
				// チェックサムの位置 = off + 64 + 16
				memcpy(&packet_off, files[last_file].hash + 8, 4);
				packet_off += 64 + 16;
			}
			// バッファーにソース・ファイルの内容を読み込む
			len = s_blk[source_off + i].size;
			if (!ReadFile(hFile, buf + (size_t)unit_size * i, len, &j, NULL) || (len != j)){
				print_win32_err();
				err = 1;
				goto error_end;
			}
			if (len < block_size)
				memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
			// ファイルのハッシュ値を計算する
			Phmd5Process(&file_md_ctx, buf + (size_t)unit_size * i, len);
			// ソース・ブロックのチェックサムを計算する
			len = crc_update(0xFFFFFFFF, buf + (size_t)unit_size * i, block_size) ^ 0xFFFFFFFF;	// include pad
			Phmd5Begin(&blk_md_ctx);
			Phmd5Process(&blk_md_ctx, buf + (size_t)unit_size * i, block_size);
			Phmd5End(&blk_md_ctx);
			memcpy(common_buf + packet_off, blk_md_ctx.hash, 16);
			memcpy(common_buf + packet_off + 16, &len, 4);
			packet_off += 20;
			checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);

			if (src_off < 0){
				src_num = i + 1;	// 最後のブロックより前なら
			} else {
				src_num = i / (src_off + 1);	// だいたい何ブロック読むごとに計算が終わるか
				src_num += i + 1;	// 次のブロック番号を足す
			}
			if (src_num < read_num){	// 読み込みが終わる前に計算が終わりそうなら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects(cpu_num1, hEnd, TRUE, 0);
				if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
					// 経過表示
					prog_num += parity_num;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress((int)((prog_num * 1000) / prog_base))){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
					// 計算終了したブロックの次から計算を開始する
					src_off += 1;
					th->buf = buf + (size_t)unit_size * (src_off - source_off);
					th->off = src_off;
					th->now = -1;	// 初期値 - 1
					//_mm_sfence();
					for (j = 0; j < cpu_num1; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}

			// 経過表示
			prog_num += prog_read;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}
		}
		if (source_off + i == source_num){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
			// チェックサム・パケットの MD5 を計算する
			memcpy(&packet_off, files[last_file].hash + 8, 4);
			memcpy(&len, files[last_file].hash + 12, 4);
			//printf("Checksum[%d], off = %d, size = %d\n", last_file, packet_off, len);
			Phmd5Begin(&blk_md_ctx);
			Phmd5Process(&blk_md_ctx, common_buf + packet_off + 32, 32 + len);
			Phmd5End(&blk_md_ctx);
			memcpy(common_buf + packet_off + 16, blk_md_ctx.hash, 16);
			// ファイルのハッシュ値の計算を終える
			Phmd5End(&file_md_ctx);
			memcpy(&packet_off, files[last_file].hash, 4);	// ハッシュ値の位置 = off + 64 + 16
			memcpy(&len, files[last_file].hash + 4, 4);
			//printf("File[%d], off = %d, size = %d\n", last_file, packet_off, len);
			// ファイルのハッシュ値を書き込んでから、パケットの MD5 を計算する
			memcpy(common_buf + packet_off + 64 + 16, file_md_ctx.hash, 16);
			Phmd5Begin(&file_md_ctx);
			Phmd5Process(&file_md_ctx, common_buf + packet_off + 32, 32 + len);
			Phmd5End(&file_md_ctx);
			memcpy(common_buf + packet_off + 16, file_md_ctx.hash, 16);
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off == 0)	// 1st encode しなかった場合（src_off = 0）は、生成ブロックをゼロ埋めする
			memset(p_buf, 0, (size_t)unit_size * parity_num);
#ifdef TIMER
		j = ((src_off - source_off) * 1000) / read_num;
		printf("partial encode = %d / %d (%d.%d%%), source_off = %d\n", src_off - source_off, read_num, j / 10, j % 10, source_off);
		// ここまでのパリティ・ブロックのチェックサムを検証する
/*		if (src_off - source_off > 0){
			__declspec( align(16) ) unsigned char hash[HASH_SIZE];
			for (j = 0; j < parity_num; j++){
				checksum16_return(p_buf + (size_t)unit_size * j, hash, unit_size - HASH_SIZE);
				if (memcmp(p_buf + ((size_t)unit_size * j + unit_size - HASH_SIZE), hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovery slice %d after 1st encode\n", j);
					err = 1;
					goto error_end;
				}
				galois_altmap_change(p_buf + (size_t)unit_size * j, unit_size);
			}
		}*/
#endif

		// スレッドごとにパリティ・ブロックを計算する
		src_num = src_max;	// 一度に処理するソース・ブロックの数を制限する
		while (src_off < source_off + read_num){
			// ソース・ブロックを何個ずつ処理するか
			if (src_off + src_num * 2 - 1 >= source_off + read_num)
				src_num = source_off + read_num - src_off;
			//printf("src_off = %d, src_num = %d\n", src_off, src_num);

			th->buf = buf + (size_t)unit_size * (src_off - source_off);
			th->off = src_off;	// ソース・ブロックの開始番号
			th->size = src_num;
			th->now = -1;	// 初期値 - 1
			//_mm_sfence();
			for (j = 0; j < cpu_num; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
			while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
				// th-now が最高値なので、計算が終わってるのは th-now + 1 - cpu_num 個となる
				j = th->now + 1 - cpu_num;
				if (j < 0)
					j = 0;
				j /= chunk_num;	// chunk数で割ってブロック数にする
				// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
				if (print_progress((int)(((prog_num + src_num * j) * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			// 経過表示
			prog_num += src_num * parity_num;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			src_off += src_num;
		}

		source_off += read_num;
	}

#ifdef TIMER
time_start = GetTickCount();
#endif
	memcpy(common_buf + common_size, common_buf, common_size);	// 後の半分に前半のをコピーする
	// 最後にパリティ・ブロックのチェックサムを検証して、リカバリ・ファイルに書き込む
	err = create_recovery_file_1pass(file_path, recovery_path, packet_limit, block_distri,
			packet_num, common_buf, common_size, footer_buf, footer_size, rcv_hFile, p_buf, NULL, unit_size);
#ifdef TIMER
time_write = GetTickCount() - time_start;
#endif

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
if (prog_num != prog_base - prog_write * parity_num)
	printf(" prog_num = %I64d != %I64d\n", prog_num, prog_base - prog_write * parity_num);
#endif

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int encode_method4(	// 全てのブロックを断片的に保持する場合 (GPU対応)
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant)	// 複数ブロック分の領域を確保しておく？
{
	unsigned char *buf = NULL, *p_buf, *g_buf, *work_buf, *hash;
	int err = 0, i, j, last_file, chunk_num;
	int cpu_num1, src_off, src_num, src_max;
	int cpu_num2, vram_max, cpu_end, gpu_end, th_act;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_read, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1], th2[1];
	PHMD5 md_ctx, *md_ptr = NULL;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);

	// 作業バッファーを確保する
	// part_num を使わず、全てのブロックを保持する所がencode_method2と異なることに注意！
	// CPU計算スレッドと GPU計算スレッドで保存先を別けるので、パリティ・ブロック分を２倍確保する
	io_size = get_io_size(source_num + parity_num * 2, NULL, 1, MEM_UNIT);
	//io_size = (((io_size + 1) / 2 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 2分割の実験用
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 3分割の実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	file_off = (source_num + parity_num * 2) * (size_t)unit_size + HASH_SIZE;
	buf = _aligned_malloc((size_t)file_off, MEM_UNIT);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * source_num;	// パリティ・ブロックを記録する領域
	g_buf = p_buf + (size_t)unit_size * parity_num;	// GPUスレッド用
	hash = g_buf + (size_t)unit_size * parity_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_read = (parity_num + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base *= (__int64)(source_num + prog_write) * parity_num + prog_read * source_num;	// 全体の断片の個数
	len = try_cache_blocking(unit_size);
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num2(parity_num, &cpu_num2);	// 使用するスレッド数を調節する
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (src_max > CACHE_MAX_NUM))
		src_max = CACHE_MAX_NUM;	// 不明または極端な場合は、規定値にする
	//cpu_num1 = 0;	// 2nd encode の実験用に 1st encode を停止する
#ifdef TIMER
	printf("\n read all source blocks, and keep all parity blocks (GPU)\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
	printf("cache: limit size = %d, chunk_size = %d, chunk_num = %d\n", cpu_flag & 0x7FFF0000, len, chunk_num);
	printf("unit_size = %d, cpu_num1 = %d, cpu_num2 = %d\n", unit_size, cpu_num1, cpu_num2);
#endif

	if (io_size < block_size){	// スライスが分割される場合だけ、途中までのハッシュ値を保持する
		block_off = sizeof(PHMD5) * parity_num;
		md_ptr = malloc(block_off);
		if (md_ptr == NULL){
			printf("malloc, %d\n", block_off);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++){
			Phmd5Begin(&(md_ptr[i]));
			j = first_num + i;	// 最初の番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			Phmd5Process(&(md_ptr[i]), header_buf + 32, 36);
		}
	}

	// OpenCL の初期化
	vram_max = source_num;
	i = init_OpenCL(unit_size, &vram_max);
	if (i != 0){
		if (i != 3)	// GPU が見つからなかった場合はエラー表示しない
			printf("init_OpenCL, %d, %d\n", i & 0xFF, i >> 8);
		i = free_OpenCL();
		if (i != 0)
			printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
		OpenCL_method = 0;	// GPU を使えなかった印
		err = -2;	// CPU だけの方式に切り替える
		goto error_end;
	}
#ifdef TIMER
	printf("OpenCL_method = %d, vram_max = %d\n", OpenCL_method, vram_max);
#endif

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th2->mat = constant;
	th->buf = p_buf;
	th2->buf = g_buf;
	th->size = unit_size;
	th2->size = unit_size;
	th->len = len;	// chunk size
	th2->len = 0;	// GPUのエラー通知用にする
	for (j = 0; j < cpu_num2; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		if (j == cpu_num2 - 1){	// 最後のスレッドを GPU 管理用にする
			th2->run = hRun[j];
			th2->end = hEnd[j];
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode_gpu, (LPVOID)th2, 0, NULL);
		} else {
			th->run = hRun[j];
			th->end = hEnd[j];
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode3, (LPVOID)th, 0, NULL);
		}
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// ソース・ブロック断片を読み込んで、パリティ・ブロック断片を作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->size = 0;	// 1st encode
		src_off = -1;	// まだ計算して無い印

		// ソース・ブロックを読み込む
#ifdef TIMER
read_count = 0;
skip_count = 0;
time_start = GetTickCount();
#endif
		last_file = -1;
		for (i = 0; i < source_num; i++){
			if (s_blk[i].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[i].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
				file_off = block_off;
			} else {	// 同じファイルならブロック・サイズ分ずらす
				file_off += block_size;
			}
			if (s_blk[i].size > block_off){	// バッファーにソース・ファイルの内容を読み込む
				len = s_blk[i].size - block_off;
				if (len > io_size)
					len = io_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", i);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				// ソース・ブロックのチェックサムを計算する
				if (block_off == 0)
					s_blk[i].crc = 0xFFFFFFFF;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf + (size_t)unit_size * i, len);	// without pad
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif

				if (src_off < 0){
					src_num = i + 1;	// 最後のブロックより前なら
				} else {
					src_num = i / (src_off + 1);	// だいたい何ブロック読むごとに計算が終わるか
					src_num += i + 1;	// 次のブロック番号を足す
				}
				if (src_num < source_num){	// 読み込みが終わる前に計算が終わりそうなら
					// サブ・スレッドの動作状況を調べる
					j = WaitForMultipleObjects(cpu_num1, hEnd, TRUE, 0);
					if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
						// 経過表示
						prog_num += parity_num;
						if (GetTickCount() - time_last >= UPDATE_TIME){
							if (print_progress((int)((prog_num * 1000) / prog_base))){
								err = 2;
								goto error_end;
							}
							time_last = GetTickCount();
						}
						// 計算終了したブロックの次から計算を開始する
						src_off += 1;
						if (src_off > 0){	// バッファーに読み込んだ時だけ計算する
							while (s_blk[src_off].size <= block_off){
								prog_num += parity_num;
								src_off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						th->buf = buf + (size_t)unit_size * src_off;
						th->off = src_off;
						th->now = -1;	// 初期値 - 1
						//_mm_sfence();
						for (j = 0; j < cpu_num1; j++){
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
					}
				}
			} else {
				memset(buf + (size_t)unit_size * i, 0, unit_size);
			}

			// 経過表示
			prog_num += prog_read;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}
		}
		// 最後のソース・ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		memset(g_buf, 0, (size_t)unit_size * parity_num);	// 待機中に GPU用の領域をゼロ埋めしておく
		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off > 0){
			while (s_blk[src_off].size <= block_off){	// 計算不要なソース・ブロックはとばす
				prog_num += parity_num;
				src_off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		} else {	// 1st encode しなかった場合（src_off = 0）は、生成ブロックをゼロ埋めする
			memset(p_buf, 0, (size_t)unit_size * parity_num);
		}
#ifdef TIMER
		j = (src_off * 1000) / source_num;
		printf("partial encode = %d / %d (%d.%d%%), read = %d, skip = %d\n", src_off, source_num, j / 10, j % 10, read_count, skip_count);
#endif

		// リカバリ・ファイルに書き込むサイズ
		if (block_size - block_off < io_size){
			len = block_size - block_off;
		} else {
			len = io_size;
		}

		th2->size = 0;	// 計算前の状態にしておく (th->size は既に 0 になってる)
		cpu_end = gpu_end = 0;
#ifdef TIMER
		printf("remain = %d, src_off = %d, src_max = %d\n", source_num - src_off, src_off, src_max);
#endif
		while (src_off < source_num){
			// GPUスレッドと CPUスレッドのどちらかが待機中になるまで待つ
			do {
				th_act = 0;
				// CPUスレッドの動作状況を調べる
				if (WaitForMultipleObjects(cpu_num2 - 1, hEnd, TRUE, 0) == WAIT_TIMEOUT){
					th_act |= 1;	// CPUスレッドが動作中
				} else if (th->size > 0){	// CPUスレッドの計算量を加算する
					prog_num += th->size * parity_num;
					th->size = 0;
				}
				// GPUスレッドの動作状況を調べる
				if (WaitForSingleObject(hEnd[cpu_num2 - 1], 0) == WAIT_TIMEOUT){
					th_act |= 2;	// GPUスレッドが動作中
				} else if (th2->size > 0){	// GPUスレッドの計算量を加算する
					if (th2->len != 0){	// エラー発生
						i = th2->len;
						printf("error, gpu-thread, %d, %d\n", i & 0xFF, i >> 8);
						err = 1;
						goto error_end;
					}
					prog_num += th2->size * parity_num;
					th2->size = 0;
				}
				if (th_act == 3){	// 両方が動作中なら
				//if (th_act == 1){	// CPUスレッドだけが動作中か調べる実験
				//if (th_act == 2){	// GPUスレッドだけが動作中か調べる実験
					// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
					while (WaitForMultipleObjects(cpu_num2, hEnd, FALSE, UPDATE_TIME) == WAIT_TIMEOUT){
						// th2-now が GPUスレッドの最高値なので、計算が終わってるのは th2-now 個となる
						i = th2->now;
						if (i < 0){
							i = 0;
						} else {
							i *= th2->size;
						}
						// th-now が CPUスレッドの最高値なので、計算が終わってるのは th-now + 2 - cpu_num2 個となる
						j = th->now + 2 - cpu_num2;
						if (j < 0){
							j = 0;
						} else {
							j /= chunk_num;	// chunk数で割ってブロック数にする
							j *= th->size;
						}
						// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
						if (print_progress((int)(((prog_num + i + j) * 1000) / prog_base))){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
				}
			} while (th_act == 3);
			//} while (th_act == 1);	// CPUスレッドの終了だけを待つ実験
			//} while (th_act == 2);	// GPUスレッドの終了だけを待つ実験

			// どちらかのスレッドでパリティ・ブロックを計算する
			if ((th_act & 1) == 0){	// CPUスレッドを優先的に開始する
				src_num = src_max;	// 一度に処理するソース・ブロックの数を制限する
				if (src_off + src_num * 2 - 1 >= source_num){
					src_num = source_num - src_off;
#ifdef TIMER
					printf("CPU last: src_off = %d, src_num = %d\n", src_off, src_num);
#endif
				}
				cpu_end += src_num;
				th->buf = buf + (size_t)unit_size * src_off;
				th->off = src_off;	// ソース・ブロックの番号にする
				th->size = src_num;
				th->now = -1;	// CPUスレッドの初期値 - 1
				//_mm_sfence();
				for (j = 0; j < cpu_num2 - 1; j++){
					ResetEvent(hEnd[j]);	// リセットしておく
					SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
				}
			} else {	// CPUスレッドが動作中なら、GPUスレッドを開始する
				src_num = (source_num - src_off) * gpu_end / (cpu_end + gpu_end);	// 残りブロック数に対する割合
				if (src_num < src_max){
					if (gpu_end == 0){	// 最初に負担するブロック数は CPUスレッドの 2倍まで
						src_num = (source_num - src_off) / (cpu_num2 + 2);
						if (src_num < src_max){
							src_num = src_max;
						} else if (src_num > src_max * 2){
							src_num = src_max * 2;
						}
					} else if (gpu_end * 2 < cpu_end){	// GPU が遅い場合は最低負担量も減らす
						if (gpu_end * 4 < cpu_end){
							if (src_num < src_max / 4)
								src_num = src_max / 4;
						} else if (src_num < src_max / 2){
							src_num = src_max / 2;
						}
					} else {
						src_num = src_max;	// 最低でも CPUスレッドと同じ量を担当する
					}
				}
				if (src_num > vram_max)
					src_num = vram_max;
				if (src_off + src_num >= source_num){
					src_num = source_num - src_off;
#ifdef TIMER
					printf("GPU last 1: src_off = %d, src_num = %d\n", src_off, src_num);
#endif
				} else if (src_off + src_num + src_max > source_num){
					src_num = source_num - src_off - src_max;
					// src_num が 0にならないように、src_num == src_max なら上の last1 にする
					if ((src_num < src_max) && (src_num + src_max <= vram_max) && (gpu_end * 2 > cpu_end)){
						src_num += src_max;	// GPU担当量が少なくて、余裕がある場合は、残りも全て任せる
#ifdef TIMER
						printf("GPU last +: src_off = %d, src_num = %d + %d\n", src_off, src_num - src_max, src_max);
					} else {
						printf("GPU last 2: src_off = %d, src_num = %d\n", src_off, src_num);
#endif
					}
#ifdef TIMER
				} else {
					printf("GPU: remain = %d, src_off = %d, src_num = %d\n", source_num - src_off, src_off, src_num);
#endif
				}
				gpu_end += src_num;
				th2->buf = buf + (size_t)unit_size * src_off;
				th2->off = src_off;	// ソース・ブロックの番号にする
				th2->size = src_num;
				th2->now = -1;	// GPUスレッドの初期値 - 1
				//_mm_sfence();
				ResetEvent(hEnd[cpu_num2 - 1]);	// リセットしておく
				SetEvent(hRun[cpu_num2 - 1]);	// サブ・スレッドに計算を開始させる
			}

			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (th2->size == 0){
					i = 0;
				} else {
					// th2-now がGPUスレッドの最高値なので、計算が終わってるのは th2-now 個となる
					i = th2->now;
					if (i < 0){
						i = 0;
					} else {
						i *= th2->size;
					}
				}
				if (th->size == 0){
					j = 0;
				} else {
					// th-now が CPUスレッドの最高値なので、計算が終わってるのは th-now + 2 - cpu_num2 個となる
					j = th->now + 2 - cpu_num2;
					if (j < 0){
						j = 0;
					} else {
						j /= chunk_num;	// chunk数で割ってブロック数にする
						j *= th->size;
					}
				}
				if (print_progress((int)(((prog_num + i + j) * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			src_off += src_num;
		}

		// 全スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
		while (WaitForMultipleObjects(cpu_num2, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
			if (th2->size == 0){
				i = 0;
			} else {
				// th2-now が GPUスレッドの最高値なので、計算が終わってるのは th2-now 個となる
				i = th2->now;
				if (i < 0){
					i = 0;
				} else {
					i *= th2->size;
				}
			}
			if (th->size == 0){
				j = 0;
			} else {
				// th-now が CPUスレッドの最高値なので、計算が終わってるのは th-now + 2 - cpu_num2 個となる
				j = th->now + 2 - cpu_num2;
				if (j < 0){
					j = 0;
				} else {
					j /= chunk_num;	// chunk数で割ってブロック数にする
					j *= th->size;
				}
			}
			// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
			if (print_progress((int)(((prog_num + i + j) * 1000) / prog_base))){
				err = 2;
				goto error_end;
			}
			time_last = GetTickCount();
		}
		if (th2->size > 0){	// GPUスレッドの計算量を加算する
			if (th2->len != 0){	// エラー発生
				i = th2->len;
				printf("error, gpu-thread, %d, %d\n", i & 0xFF, i >> 8);
				err = 1;
				goto error_end;
			}
			prog_num += th2->size * parity_num;
		}
		if (th->size > 0)	// CPUスレッドの計算量を加算する
			prog_num += th->size * parity_num;

#ifdef TIMER
time_start = GetTickCount();
#endif
		// パリティ・ブロックを書き込む
		work_buf = p_buf;
		for (i = 0; i < parity_num; i++){
			// CPUスレッドと GPUスレッドの計算結果を合わせる
			galois_align_xor(g_buf + (size_t)unit_size * i, work_buf, unit_size);
			// パリティ・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
			// ハッシュ値を計算して、リカバリ・ファイルに書き込む
			if (io_size >= block_size){	// 1回で書き込みが終わるなら
				Phmd5Begin(&md_ctx);
				j = first_num + i;	// 最初の番号の分だけ足す
				memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
				Phmd5Process(&md_ctx, header_buf + 32, 36);
				Phmd5Process(&md_ctx, work_buf, len);
				Phmd5End(&md_ctx);
				memcpy(header_buf + 16, md_ctx.hash, 16);
				// ヘッダーを書き込む
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off - 68, header_buf, 68)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
			} else {
				Phmd5Process(&(md_ptr[i]), work_buf, len);
			}
			//printf("%d, buf = %p, size = %u, off = %I64d\n", i, work_buf, len, p_blk[i].off + block_off);
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off, work_buf, len)){
				printf("file_write_data, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
			work_buf += unit_size;

			// 経過表示
			prog_num += prog_write;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif

		block_off += io_size;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	memset(buf, 0, io_size);
	j = 0;
	while (j < source_num){
		last_file = s_blk[j].file;
		src_num = (int)((files[last_file].size + (__int64)block_size - 1) / block_size);
		i = j + src_num - 1;	// 末尾ブロックの番号
		if (s_blk[i].size < block_size){	// 残りを 0 でパディングする
			len = block_size - s_blk[i].size;
			while (len > io_size){
				len -= io_size;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf, io_size);
			}
			s_blk[i].crc = crc_update(s_blk[i].crc, buf, len);
		}
		memset(hash, 0, 16);
		for (i = 0; i < src_num; i++)	// XOR して 16バイトに減らす
			((unsigned int *)hash)[i & 3] ^= s_blk[j + i].crc ^ 0xFFFFFFFF;
		if (memcmp(files[last_file].hash, hash, 16) != 0){
			printf("checksum mismatch, input file %d\n", last_file);
			err = 1;
			goto error_end;
		}
		j += src_num;
	}

	//printf("io_size = %d, block_size = %d\n", io_size, block_size);
	if (io_size < block_size){	// 1回で書き込みが終わらなかったなら
		if (GetTickCount() - time_last >= UPDATE_TIME){	// キャンセルを受け付ける
			if (cancel_progress()){
				err = 2;
				goto error_end;
			}
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 最後に Recovery Slice packet のヘッダーを書き込む
		for (i = 0; i < parity_num; i++){
			Phmd5End(&(md_ptr[i]));
			memcpy(header_buf + 16, md_ptr[i].hash, 16);
			j = first_num + i;	// 最初のパリティ・ブロック番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			// リカバリ・ファイルに書き込む
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){	// ヘッダーのサイズ分だけずらす
				printf("file_write_data, packet header\n");
				err = 1;
				goto error_end;
			}
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
if (prog_num != prog_base)
	printf(" prog_num = %I64d, prog_base = %I64d\n", prog_num, prog_base);
#endif
	info_OpenCL(buf, MEM_UNIT);	// デバイス情報を表示する

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	InterlockedExchange(&(th2->now), INT_MAX / 2);
	for (j = 0; j < cpu_num2; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (md_ptr)
		free(md_ptr);
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	i = free_OpenCL();
	if (i != 0)
		printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
	return err;
}

int encode_method5(	// ソース・ブロックの一部とパリティ・ブロックを保持する場合 (GPU対応)
	wchar_t *file_path,
	wchar_t *recovery_path,		// 作業用
	int packet_limit,			// リカバリ・ファイルのパケット繰り返しの制限
	int block_distri,			// パリティ・ブロックの分配方法 (3-bit目は番号の付け方)
	int packet_num,				// 共通パケットの数
	unsigned char *common_buf,	// 共通パケットのバッファー
	int common_size,			// 共通パケットのバッファー・サイズ
	unsigned char *footer_buf,	// 末尾パケットのバッファー
	int footer_size,			// 末尾パケットのバッファー・サイズ
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	unsigned short *constant)
{
	unsigned char *buf = NULL, *p_buf, *g_buf;
	int err = 0, i, j, last_file, chunk_num;
	int source_off, read_num, packet_off;
	int cpu_num1, src_off, src_num, src_max;
	int cpu_num2, vram_max, cpu_end, gpu_end, th_act;
	unsigned int unit_size, len;
	unsigned int time_last, prog_read, prog_write;
	__int64 prog_num = 0, prog_base;
	size_t mem_size;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1], th2[1];
	PHMD5 file_md_ctx, blk_md_ctx;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	unit_size = (block_size + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);	// MEM_UNIT の倍数にする

	// 作業バッファーを確保する
	// CPU計算スレッドと GPU計算スレッドで保存先を別けるので、パリティ・ブロック分を２倍確保する
	read_num = read_block_num(parity_num * 2, 1, MEM_UNIT);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
#ifdef TIMER
		printf("cannot keep enough blocks, use another method\n");
#endif
		return -4;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 1) / 2 + 1;	// 2分割の実験用
	//read_num = (read_num + 2) / 3 + 1;	// 3分割の実験用
	mem_size = (size_t)(read_num + parity_num * 2) * unit_size;
	buf = _aligned_malloc(mem_size, MEM_UNIT);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %Id\n", mem_size);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	g_buf = p_buf + (size_t)unit_size * parity_num;	// GPUスレッド用
	prog_read = (parity_num + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base = (__int64)(source_num + prog_write) * parity_num + prog_read * source_num;	// ブロックの合計掛け算個数 + 書き込み回数
	len = try_cache_blocking(unit_size);
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num2(parity_num, &cpu_num2);	// 使用するスレッド数を調節する
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (src_max > CACHE_MAX_NUM))
		src_max = CACHE_MAX_NUM;	// 不明または極端な場合は、規定値にする
	//cpu_num1 = 0;	// 2nd encode の実験用に 1st encode を停止する
#ifdef TIMER
	printf("\n read some source blocks, and keep all parity blocks (GPU)\n");
	printf("buffer size = %Id MB, read_num = %d, round = %d\n", mem_size >> 20, read_num, (source_num + read_num - 1) / read_num);
	printf("cache: limit size = %d, chunk_size = %d, chunk_num = %d\n", cpu_flag & 0x7FFF0000, len, chunk_num);
	printf("unit_size = %d, cpu_num1 = %d, cpu_num2 = %d\n", unit_size, cpu_num1, cpu_num2);
#endif

	// OpenCL の初期化
	vram_max = read_num;	// 読み込める分だけにする
	i = init_OpenCL(unit_size, &vram_max);
	if (i != 0){
		if (i != 3)	// GPU が見つからなかった場合はエラー表示しない
			printf("init_OpenCL, %d, %d\n", i & 0xFF, i >> 8);
		i = free_OpenCL();
		if (i != 0)
			printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
		OpenCL_method = 0;	// GPU を使えなかった印
		err = -3;	// CPU だけの方式に切り替える
		goto error_end;
	}
#ifdef TIMER
	printf("OpenCL_method = %d, vram_max = %d\n", OpenCL_method, vram_max);
#endif
	print_progress_text(0, "Creating recovery slice");

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th2->mat = constant;
	th->buf = p_buf;
	th2->buf = g_buf;
	th->size = unit_size;
	th2->size = unit_size;
	th->len = len;	// chunk size
	th2->len = 0;	// GPUのエラー通知用にする
	for (j = 0; j < cpu_num2; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		if (j == cpu_num2 - 1){	// 最後のスレッドを GPU 管理用にする
			th2->run = hRun[j];
			th2->end = hEnd[j];
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode_gpu, (LPVOID)th2, 0, NULL);
		} else {
			th->run = hRun[j];
			th->end = hEnd[j];
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode3, (LPVOID)th, 0, NULL);
		}
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// 何回かに別けてソース・ブロックを読み込んで、パリティ・ブロックを少しずつ作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	last_file = -1;
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 0;	// 1st encode
		src_off = source_off - 1;	// まだ計算して無い印

#ifdef TIMER
time_start = GetTickCount();
#endif
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			// ソース・ブロックを読み込む
			if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
					// チェックサム・パケットの MD5 を計算する
					memcpy(&packet_off, files[last_file].hash + 8, 4);
					memcpy(&len, files[last_file].hash + 12, 4);
					//printf("Checksum[%d], off = %d, size = %d\n", last_file, packet_off, len);
					Phmd5Begin(&blk_md_ctx);
					Phmd5Process(&blk_md_ctx, common_buf + packet_off + 32, 32 + len);
					Phmd5End(&blk_md_ctx);
					memcpy(common_buf + packet_off + 16, blk_md_ctx.hash, 16);
					// ファイルのハッシュ値の計算を終える
					Phmd5End(&file_md_ctx);
					memcpy(&packet_off, files[last_file].hash, 4);	// ハッシュ値の位置 = off + 64 + 16
					memcpy(&len, files[last_file].hash + 4, 4);
					//printf("File[%d], off = %d, size = %d\n", last_file, packet_off, len);
					// ファイルのハッシュ値を書き込んでから、パケットの MD5 を計算する
					memcpy(common_buf + packet_off + 64 + 16, file_md_ctx.hash, 16);
					Phmd5Begin(&file_md_ctx);
					Phmd5Process(&file_md_ctx, common_buf + packet_off + 32, 32 + len);
					Phmd5End(&file_md_ctx);
					memcpy(common_buf + packet_off + 16, file_md_ctx.hash, 16);
				}
				last_file = s_blk[source_off + i].file;
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
				// ファイルのハッシュ値の計算を始める
				Phmd5Begin(&file_md_ctx);
				// チェックサムの位置 = off + 64 + 16
				memcpy(&packet_off, files[last_file].hash + 8, 4);
				packet_off += 64 + 16;
			}
			// バッファーにソース・ファイルの内容を読み込む
			len = s_blk[source_off + i].size;
			if (!ReadFile(hFile, buf + (size_t)unit_size * i, len, &j, NULL) || (len != j)){
				print_win32_err();
				err = 1;
				goto error_end;
			}
			if (len < block_size)
				memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
			// ファイルのハッシュ値を計算する
			Phmd5Process(&file_md_ctx, buf + (size_t)unit_size * i, len);
			// ソース・ブロックのチェックサムを計算する
			len = crc_update(0xFFFFFFFF, buf + (size_t)unit_size * i, block_size) ^ 0xFFFFFFFF;	// include pad
			Phmd5Begin(&blk_md_ctx);
			Phmd5Process(&blk_md_ctx, buf + (size_t)unit_size * i, block_size);
			Phmd5End(&blk_md_ctx);
			memcpy(common_buf + packet_off, blk_md_ctx.hash, 16);
			memcpy(common_buf + packet_off + 16, &len, 4);
			packet_off += 20;
			checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);

			if (src_off < 0){
				src_num = i + 1;	// 最後のブロックより前なら
			} else {
				src_num = i / (src_off + 1);	// だいたい何ブロック読むごとに計算が終わるか
				src_num += i + 1;	// 次のブロック番号を足す
			}
			if (src_num < read_num){	// 読み込みが終わる前に計算が終わりそうなら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects(cpu_num1, hEnd, TRUE, 0);
				if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
					// 経過表示
					prog_num += parity_num;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress((int)((prog_num * 1000) / prog_base))){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
					// 計算終了したブロックの次から計算を開始する
					src_off += 1;
					th->buf = buf + (size_t)unit_size * (src_off - source_off);
					th->off = src_off;
					th->now = -1;	// 初期値 - 1
					//_mm_sfence();
					for (j = 0; j < cpu_num1; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}

			// 経過表示
			prog_num += prog_read;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}
		}
		if (source_off + i == source_num){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
			// チェックサム・パケットの MD5 を計算する
			memcpy(&packet_off, files[last_file].hash + 8, 4);
			memcpy(&len, files[last_file].hash + 12, 4);
			//printf("Checksum[%d], off = %d, size = %d\n", last_file, packet_off, len);
			Phmd5Begin(&blk_md_ctx);
			Phmd5Process(&blk_md_ctx, common_buf + packet_off + 32, 32 + len);
			Phmd5End(&blk_md_ctx);
			memcpy(common_buf + packet_off + 16, blk_md_ctx.hash, 16);
			// ファイルのハッシュ値の計算を終える
			Phmd5End(&file_md_ctx);
			memcpy(&packet_off, files[last_file].hash, 4);	// ハッシュ値の位置 = off + 64 + 16
			memcpy(&len, files[last_file].hash + 4, 4);
			//printf("File[%d], off = %d, size = %d\n", last_file, packet_off, len);
			// ファイルのハッシュ値を書き込んでから、パケットの MD5 を計算する
			memcpy(common_buf + packet_off + 64 + 16, file_md_ctx.hash, 16);
			Phmd5Begin(&file_md_ctx);
			Phmd5Process(&file_md_ctx, common_buf + packet_off + 32, 32 + len);
			Phmd5End(&file_md_ctx);
			memcpy(common_buf + packet_off + 16, file_md_ctx.hash, 16);
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		if (source_off == 0)
			memset(g_buf, 0, (size_t)unit_size * parity_num);	// 待機中に GPU用の領域をゼロ埋めしておく
		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off == 0)	// 1st encode しなかった場合（src_off = 0）は、生成ブロックをゼロ埋めする
			memset(p_buf, 0, (size_t)unit_size * parity_num);
#ifdef TIMER
		j = (src_off - source_off) * 1000 / read_num;
		printf("partial encode = %d / %d (%d.%d%%), source_off = %d\n", src_off - source_off, read_num, j / 10, j % 10, source_off);
#endif

		th2->size = 0;	// 計算前の状態にしておく (th->size は既に 0 になってる)
		cpu_end = gpu_end = 0;
		src_off -= source_off;	// バッファー内でのソース・ブロックの位置にする
#ifdef TIMER
		printf("remain = %d, src_off = %d, src_max = %d\n", read_num - src_off, src_off, src_max);
#endif
		while (src_off < read_num){
			// GPUスレッドと CPUスレッドのどちらかが待機中になるまで待つ
			do {
				th_act = 0;
				// CPUスレッドの動作状況を調べる
				if (WaitForMultipleObjects(cpu_num2 - 1, hEnd, TRUE, 0) == WAIT_TIMEOUT){
					th_act |= 1;	// CPUスレッドが動作中
				} else if (th->size > 0){	// CPUスレッドの計算量を加算する
					prog_num += th->size * parity_num;
					th->size = 0;
				}
				// GPUスレッドの動作状況を調べる
				if (WaitForSingleObject(hEnd[cpu_num2 - 1], 0) == WAIT_TIMEOUT){
					th_act |= 2;	// GPUスレッドが動作中
				} else if (th2->size > 0){	// GPUスレッドの計算量を加算する
					if (th2->len != 0){	// エラー発生
						i = th2->len;
						printf("error, gpu-thread, %d, %d\n", i & 0xFF, i >> 8);
						err = 1;
						goto error_end;
					}
					prog_num += th2->size * parity_num;
					th2->size = 0;
				}
				if (th_act == 3){	// 両方が動作中なら
					// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
					while (WaitForMultipleObjects(cpu_num2, hEnd, FALSE, UPDATE_TIME) == WAIT_TIMEOUT){
						// th2-now が GPUスレッドの最高値なので、計算が終わってるのは th2-now 個となる
						i = th2->now;
						if (i < 0){
							i = 0;
						} else {
							i *= th2->size;
						}
						// th-now が CPUスレッドの最高値なので、計算が終わってるのは th-now + 2 - cpu_num2 個となる
						j = th->now + 2 - cpu_num2;
						if (j < 0){
							j = 0;
						} else {
							j /= chunk_num;	// chunk数で割ってブロック数にする
							j *= th->size;
						}
						// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
						if (print_progress((int)(((prog_num + i + j) * 1000) / prog_base))){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
				}
			} while (th_act == 3);

			// どちらかのスレッドでパリティ・ブロックを計算する
			if ((th_act & 1) == 0){	// CPUスレッドを優先的に開始する
				src_num = src_max;	// 一度に処理するソース・ブロックの数を制限する
				if (src_off + src_num * 2 - 1 >= read_num){
					src_num = read_num - src_off;
#ifdef TIMER
					printf("CPU last: src_off = %d, src_num = %d\n", src_off, src_num);
#endif
				}
				cpu_end += src_num;
				th->buf = buf + (size_t)unit_size * src_off;
				th->off = source_off + src_off;	// ソース・ブロックの番号にする
				th->size = src_num;
				th->now = -1;	// CPUスレッドの初期値 - 1
				//_mm_sfence();
				for (j = 0; j < cpu_num2 - 1; j++){
					ResetEvent(hEnd[j]);	// リセットしておく
					SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
				}
			} else {	// CPUスレッドが動作中なら、GPUスレッドを開始する
				src_num = (read_num - src_off) * gpu_end / (cpu_end + gpu_end);	// 残りブロック数に対する割合
				if (src_num < src_max){
					if (gpu_end == 0){	// 最初に負担するブロック数は CPUスレッドの 2倍まで
						src_num = (read_num - src_off) / (cpu_num2 + 2);
						if (src_num < src_max){
							src_num = src_max;
						} else if (src_num > src_max * 2){
							src_num = src_max * 2;
						}
					} else if (gpu_end * 2 < cpu_end){	// GPU が遅い場合は最低負担量も減らす
						if (gpu_end * 4 < cpu_end){
							if (src_num < src_max / 4)
								src_num = src_max / 4;
						} else if (src_num < src_max / 2){
							src_num = src_max / 2;
						}
					} else {
						src_num = src_max;	// 最低でも CPUスレッドと同じ量を担当する
					}
				}
				if (src_num > vram_max)
					src_num = vram_max;
				if (src_off + src_num >= read_num){
					src_num = read_num - src_off;
#ifdef TIMER
					printf("GPU last 1: src_off = %d, src_num = %d\n", src_off, src_num);
#endif
				} else if (src_off + src_num + src_max > read_num){
					src_num = read_num - src_off - src_max;
					if ((src_num < src_max) && (src_num + src_max <= vram_max) && (gpu_end * 2 > cpu_end)){
						src_num += src_max;	// GPU担当量が少なくて、余裕がある場合は、残りも全て任せる
#ifdef TIMER
						printf("GPU last +: src_off = %d, src_num = %d + %d\n", src_off, src_num - src_max, src_max);
					} else {
						printf("GPU last 2: src_off = %d, src_num = %d\n", src_off, src_num);
#endif
					}
#ifdef TIMER
				} else {
					printf("GPU: remain = %d, src_off = %d, src_num = %d\n", read_num - src_off, src_off, src_num);
#endif
				}
				gpu_end += src_num;
				th2->buf = buf + (size_t)unit_size * src_off;
				th2->off = source_off + src_off;	// ソース・ブロックの番号にする
				th2->size = src_num;
				th2->now = -1;	// GPUスレッドの初期値 - 1
				//_mm_sfence();
				ResetEvent(hEnd[cpu_num2 - 1]);	// リセットしておく
				SetEvent(hRun[cpu_num2 - 1]);	// サブ・スレッドに計算を開始させる
			}

			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (th2->size == 0){
					i = 0;
				} else {
					// th2-now がGPUスレッドの最高値なので、計算が終わってるのは th2-now 個となる
					i = th2->now;
					if (i < 0){
						i = 0;
					} else {
						i *= th2->size;
					}
				}
				if (th->size == 0){
					j = 0;
				} else {
					// th-now が CPUスレッドの最高値なので、計算が終わってるのは th-now + 2 - cpu_num2 個となる
					j = th->now + 2 - cpu_num2;
					if (j < 0){
						j = 0;
					} else {
						j /= chunk_num;	// chunk数で割ってブロック数にする
						j *= th->size;
					}
				}
				if (print_progress((int)(((prog_num + i + j) * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			src_off += src_num;
		}

		// 全スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
		while (WaitForMultipleObjects(cpu_num2, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
			if (th2->size == 0){
				i = 0;
			} else {
				// th2-now が GPUスレッドの最高値なので、計算が終わってるのは th2-now 個となる
				i = th2->now;
				if (i < 0){
					i = 0;
				} else {
					i *= th2->size;
				}
			}
			if (th->size == 0){
				j = 0;
			} else {
				// th-now が CPUスレッドの最高値なので、計算が終わってるのは th-now + 2 - cpu_num2 個となる
				j = th->now + 2 - cpu_num2;
				if (j < 0){
					j = 0;
				} else {
					j /= chunk_num;	// chunk数で割ってブロック数にする
					j *= th->size;
				}
			}
			// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
			if (print_progress((int)(((prog_num + i + j) * 1000) / prog_base))){
				err = 2;
				goto error_end;
			}
			time_last = GetTickCount();
		}
		if (th2->size > 0){	// GPUスレッドの計算量を加算する
			if (th2->len != 0){	// エラー発生
				i = th2->len;
				printf("error, gpu-thread, %d, %d\n", i & 0xFF, i >> 8);
				err = 1;
				goto error_end;
			}
			prog_num += th2->size * parity_num;
		}
		if (th->size > 0)	// CPUスレッドの計算量を加算する
			prog_num += th->size * parity_num;

		source_off += read_num;
	}

#ifdef TIMER
time_start = GetTickCount();
#endif
	memcpy(common_buf + common_size, common_buf, common_size);	// 後の半分に前半のをコピーする
	// 最後にパリティ・ブロックのチェックサムを検証して、リカバリ・ファイルに書き込む
	err = create_recovery_file_1pass(file_path, recovery_path, packet_limit, block_distri,
			packet_num, common_buf, common_size, footer_buf, footer_size, rcv_hFile, p_buf, g_buf, unit_size);
#ifdef TIMER
time_write = GetTickCount() - time_start;
#endif

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
if (prog_num != prog_base - prog_write * parity_num)
	printf(" prog_num = %I64d != %I64d\n", prog_num, prog_base - prog_write * parity_num);
#endif
	info_OpenCL(buf, MEM_UNIT);	// デバイス情報を表示する

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	InterlockedExchange(&(th2->now), INT_MAX / 2);
	for (j = 0; j < cpu_num2; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	i = free_OpenCL();
	if (i != 0)
		printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
	return err;
}

