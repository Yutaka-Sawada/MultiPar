// rs_encode.c
// Copyright : 2021-12-17 Yutaka Sawada
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
	unsigned short *constant, factor2;
	volatile unsigned short *factor1;
	int i, j, src_start, src_num, max_num, chunk_num;
	int part_start, part_num, cover_num;
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
	chunk_size = th->off;
	part_num = th->count;
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	factor1 = constant + source_num;
	chunk_num = (unit_size + chunk_size - 1) / chunk_size;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		src_start = th->off;	// ソース・ブロック番号
		len = chunk_size;

		if (th->size == 0){	// ソース・ブロック読み込み中
			// パリティ・ブロックごとに掛け算して追加していく
			max_num = chunk_num * part_num;
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / part_num;	// chunk の番号
				j = j % part_num;	// parity の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (src_start == 0)	// 最初のブロックを計算する際に
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(s_buf + off, p_buf + ((size_t)unit_size * j + off), len, factor1[j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// パリティ・ブロックを部分的に保持する場合
			// スレッドごとに作成するパリティ・ブロックの chunk を変える
			src_num = source_num - src_start;
			cover_num = th->size;
			part_start = th->count;
			max_num = chunk_num * cover_num;
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / cover_num;	// chunk の番号
				j = j % cover_num;		// parity の番号
				off *= chunk_size;		// chunk の位置
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				work_buf = p_buf + (size_t)unit_size * j + off;
				if (part_start != 0)
					memset(work_buf, 0, len);	// 最初の part_num 以降は 2nd encode だけなので 0で埋める

				// ソース・ブロックごとにパリティを追加していく
				for (i = 0; i < src_num; i++){
					factor2 = galois_power(constant[src_start + i], first_num + part_start + j);	// factor は定数行列の乗数になる
					galois_align_multiply(s_buf + ((size_t)unit_size * i + off), work_buf, len, factor2);
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
loop_count2a /= chunk_num;	// chunk数で割ってブロック数にする
loop_count2b /= chunk_num;
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
	unsigned short *constant, factor2;
	volatile unsigned short *factor1;
	int i, j, src_start, src_num, max_num, chunk_num;
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
	chunk_size = th->off;
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	factor1 = constant + source_num;
	chunk_num = (unit_size + chunk_size - 1) / chunk_size;
	max_num = chunk_num * parity_num;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		src_start = th->off;	// ソース・ブロック番号
		len = chunk_size;

		if (th->size == 0){	// ソース・ブロック読み込み中
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / parity_num;	// chunk の番号
				j = j % parity_num;	// parity の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (src_start == 0)	// 最初のブロックを計算する際に
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(s_buf + off, p_buf + ((size_t)unit_size * j + off), len, factor1[j]);
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
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / parity_num;	// chunk の番号
				j = j % parity_num;		// parity の番号
				off *= chunk_size;		// chunk の位置
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				work_buf = p_buf + (size_t)unit_size * j + off;

				// ソース・ブロックごとにパリティを追加していく
				for (i = 0; i < src_num; i++){
					factor2 = galois_power(constant[src_start + i], first_num + j);	// factor は定数行列の乗数になる
					galois_align_multiply(s_buf + ((size_t)unit_size * i + off), work_buf, len, factor2);
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
loop_count2a /= chunk_num;	// chunk数で割ってブロック数にする
loop_count2b /= chunk_num;
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

// ブロックごとに計算するためのスレッド
static DWORD WINAPI thread_encode_each(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf, *work_buf;
	unsigned short *constant, *factor2;
	volatile unsigned short *factor1;
	int i, j, th_id, src_start, src_num, max_num;
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
	th_id = th->now;	// スレッド番号
	chunk_size = th->off;
	factor2 = (unsigned short *)(p_buf + ((size_t)unit_size * parity_num + HASH_SIZE));
	factor2 += th->count * th_id;	// スレッドごとに保存場所を変える
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	factor1 = constant + source_num;
	max_num = ((unit_size + chunk_size - 1) / chunk_size) * parity_num;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		src_start = th->off;	// ソース・ブロック番号

		if (th->size == 0xFFFFFFFF){	// ソース・ブロック読み込み中
			len = chunk_size;
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / parity_num;	// chunk の番号
				j = j % parity_num;		// parity の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (src_start == 0)	// 最初のブロックを計算する際に
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(s_buf + off, p_buf + ((size_t)unit_size * j + off), len, factor1[j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {
			// スレッドごとに作成するパリティ・ブロックを変える
			src_num = th->count;
			while ((j = InterlockedIncrement(&(th->now))) < parity_num){	// j = ++th_now
				work_buf = p_buf + (size_t)unit_size * j;

				// factor は定数行列の乗数になる
				for (i = 0; i < src_num; i++)
					factor2[i] = galois_power(constant[src_start + i], first_num + j);

				// chunk に分割して計算する
				len = chunk_size;
				off = 0;
				while (off < unit_size){
					// ソース・ブロックごとにパリティを追加していく
					for (i = 0; i < src_num; i++)
						galois_align_multiply(s_buf + ((size_t)unit_size * i + off), work_buf, len, factor2[i]);

					work_buf += len;
					off += len;
					if (off + len > unit_size)
						len = unit_size - off;
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
loop_count2a /= (unit_size + chunk_size - 1) / chunk_size;	// chunk数で割ってブロック数にする
printf("sub-thread[%d] : total loop = %d\n", th_id, loop_count2a + loop_count2b);
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

// GPU 対応のサブ・スレッド (スレッド番号は最後になる)
static DWORD WINAPI thread_encode_gpu(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf;
	unsigned short *constant, *factor2;
	int i, j, th_id, src_start, src_num;
	unsigned int unit_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int time_start2, time_encode2 = 0, loop_count2 = 0;
#endif

	th = (RS_TH *)lpParameter;
	constant = th->mat;
	p_buf = th->buf;
	unit_size = th->size;
	th_id = th->now;	// スレッド番号
	factor2 = (unsigned short *)(p_buf + ((size_t)unit_size * parity_num + HASH_SIZE));
	factor2 += th->count * th_id;	// スレッドごとに保存場所を変える
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		// GPUはソース・ブロック読み込み中に呼ばれない
		s_buf = th->buf;
		src_start = th->off;	// ソース・ブロック番号
		src_num = th->count;

		// 最初にソース・ブロックをVRAMへ転送する
		i = gpu_copy_blocks(s_buf, unit_size, src_num);
		if (i != 0){
			th->size = i;
			InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
		}

		// スレッドごとに作成するパリティ・ブロックを変える
		while ((j = InterlockedIncrement(&(th->now))) < parity_num){	// j = ++th_now
			// factor は定数行列の乗数になる
			for (i = 0; i < src_num; i++)
				factor2[i] = galois_power(constant[src_start + i], first_num + j);

			i = gpu_multiply_blocks(src_num, factor2, p_buf + (size_t)unit_size * j, unit_size);
			if (i != 0){
				th->size = i;
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
		th->size = gpu_finish();

		//_mm_sfence();	// メモリーへの書き込みを完了する
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
printf("gpu-thread    : total loop = %d\n", loop_count2);
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
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_cache & 0x7FFF8000, j, (unit_size + j - 1) / j);
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
	unsigned short *factor1;
	int err = 0, i, j, last_file, part_start, part_num;
	int src_num, chunk_num, cover_num;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 md_ctx, *md_ptr = NULL;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	factor1 = constant + source_num;

	// 作業バッファーを確保する
	part_num = source_num >> PART_MAX_RATE;	// ソース・ブロック数に対する割合で最大量を決める
	//part_num = (parity_num + 1) / 2;	// 確保量の実験用
	//part_num = (parity_num + 2) / 3;	// 確保量の実験用
	if (part_num < parity_num){	// 分割して計算するなら
		i = (parity_num + part_num - 1) / part_num;	// 分割回数
		part_num = (parity_num + i - 1) / i;
		part_num = ((part_num + cpu_num - 1) / cpu_num) * cpu_num;	// cpu_num の倍数にする（切り上げ）
	}
	if (part_num > parity_num)
		part_num = parity_num;
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
	p_buf = buf + (size_t)unit_size * source_num;	// パリティ・ブロックを部分的に記録する領域
	hash = p_buf + (size_t)unit_size * part_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_write = source_num >> 5;	// 計算で 97%、書き込みで 3% ぐらい
	if (prog_write == 0)
		prog_write = 1;
	prog_base *= (__int64)(source_num + prog_write) * parity_num;	// 全体の断片の個数
	len = try_cache_blocking(unit_size);
	//len = ((len + 2) / 3 + (sse_unit - 1)) & ~(sse_unit - 1);	// 1/3の実験用
	chunk_num = (unit_size + len - 1) / len;
#ifdef TIMER
	printf("\n read all source blocks, and keep some parity blocks\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_cache & 0x7FFF8000, len, chunk_num);
	printf("prog_base = %I64d, unit_size = %d, part_num = %d\n", prog_base, unit_size, part_num);
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
	th->off = len;	// キャッシュの最適化を試みる
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
	// IO が延滞しないように、サブ・スレッド一つの優先度を下げる
	SetThreadPriority(hSub[0], THREAD_PRIORITY_BELOW_NORMAL);

	// ソース・ブロック断片を読み込んで、パリティ・ブロック断片を作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->size = 0;	// 1st encode
		th->off = -1;	// まだ計算して無い印

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

				if (i + 1 < source_num){	// 最後のブロック以外なら
					// サブ・スレッドの動作状況を調べる
					j = WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, 0);
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
						th->off += 1;
						if (th->off > 0){	// バッファーに読み込んだ時だけ計算する
							while (s_blk[th->off].size <= block_off){
								prog_num += part_num;
								th->off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						th->buf = buf + (size_t)unit_size * th->off;
						for (j = 0; j < part_num; j++)
							factor1[j] = galois_power(constant[th->off], first_num + j);	// factor は定数行列の乗数になる
						th->now = -1;	// 初期値 - 1
						//_mm_sfence();
						for (j = 0; j < (cpu_num + 1) / 2; j++){
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
					}
				}
			} else {
				memset(buf + (size_t)unit_size * i, 0, unit_size);
			}
		}
		// 最後のソース・ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off > 0){
			while (s_blk[th->off].size <= block_off){	// 計算不要なソース・ブロックはとばす
				prog_num += part_num;
				th->off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		} else {	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * part_num);
		}
#ifdef TIMER
		j = (th->off * 1000) / source_num;
		printf("partial encode = %d / %d (%d.%d%%), read = %d, skip = %d\n", th->off, source_num, j / 10, j % 10, read_count, skip_count);
		// ここまでのパリティ・ブロックのチェックサムを検証する
/*		if (th->off > 0){
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

		// cover_num ごとに処理する
		part_start = 0;
		cover_num = part_num;	// part_num は cpu_num の倍数にすること
		src_num = source_num - th->off;	// 一度に処理する量 (src_num > 0)
		th->buf = buf + (size_t)unit_size * (th->off);
		while (part_start < parity_num){
			if (part_start == part_num){	// part_num 分の計算が終わったら
				th->off = 0;	// 最初の計算以降は全てのソース・ブロックを対象にする
				src_num = source_num;	// source_num - th->off
				th->buf = buf;	// buf + (size_t)unit_size * (th->off);
			}
			if (part_start + cover_num > parity_num)
				cover_num = parity_num - part_start;
			//printf("part_start = %d, src_num = %d / %d, cover_num = %d\n", part_start, src_num, source_num, cover_num);

			// スレッドごとにパリティ・ブロックを計算する
			th->size = cover_num;
			th->count = part_start;
			th->now = -1;	// 初期値 - 1
			//_mm_sfence();
			for (j = 0; j < cpu_num; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
			while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
				// th-now が最高値なので、計算が終わってるのは th-now - cpu_num 個となる
				j = th->now - cpu_num;
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
			prog_num += src_num * cover_num;

#ifdef TIMER
time_start = GetTickCount();
#endif
			// パリティ・ブロックを書き込む
			work_buf = p_buf;
			for (i = part_start; i < part_start + cover_num; i++){
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

			part_start += part_num;	// 次のパリティ位置にする
		}

		block_off += io_size;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく
	//printf("prog_num = %I64d / %I64d\n", prog_num, prog_base);

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
	unsigned short *factor1;
	int err = 0, i, j, last_file, source_off, read_num, packet_off;
	int src_num, chunk_num;
	unsigned int unit_size, len;
	unsigned int time_last, prog_write;
	__int64 prog_num = 0, prog_base;
	size_t mem_size;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 file_md_ctx, blk_md_ctx;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	factor1 = constant + source_num;
	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);	// チェックサムの分だけ増やす

	// 作業バッファーを確保する
	read_num = read_block_num(parity_num, 0, 1, sse_unit);	// ソース・ブロックを何個読み込むか
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
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	prog_write = source_num >> 5;	// 計算で 97%、書き込みで 3% ぐらい
	if (prog_write == 0)
		prog_write = 1;
	prog_base = (__int64)(source_num + prog_write) * parity_num;	// ブロックの合計掛け算個数 + 書き込み回数
	len = try_cache_blocking(unit_size);
	chunk_num = (unit_size + len - 1) / len;
#ifdef TIMER
	printf("\n read some source blocks, and keep all parity blocks\n");
	printf("buffer size = %Id MB, read_num = %d, round = %d\n", mem_size >> 20, read_num, (source_num + read_num - 1) / read_num);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_cache & 0x7FFF8000, len, chunk_num);
	printf("prog_base = %I64d, unit_size = %d\n", prog_base, unit_size);
#endif

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th->buf = p_buf;
	th->size = unit_size;
	th->off = len;	// キャッシュの最適化を試みる
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
	// IO が延滞しないように、サブ・スレッド一つの優先度を下げる
	SetThreadPriority(hSub[0], THREAD_PRIORITY_BELOW_NORMAL);

	// 何回かに別けてソース・ブロックを読み込んで、パリティ・ブロックを少しずつ作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	last_file = -1;
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 0;	// 1st encode
		th->off = source_off - 1;	// まだ計算して無い印

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

			if (i + 1 < read_num){	// 最後のブロック以外なら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, 0);
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
					th->off += 1;
					th->buf = buf + (size_t)unit_size * (th->off - source_off);
					for (j = 0; j < parity_num; j++)
						factor1[j] = galois_power(constant[th->off], first_num + j);	// factor は定数行列の乗数になる
					th->now = -1;	// 初期値 - 1
					//_mm_sfence();
					for (j = 0; j < (cpu_num + 1) / 2; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
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

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off == 0)	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * parity_num);
#ifdef TIMER
		j = ((th->off - source_off) * 1000) / read_num;
		printf("partial encode = %d / %d (%d.%d%%), source_off = %d\n", th->off - source_off, read_num, j / 10, j % 10, source_off);
		// ここまでのパリティ・ブロックのチェックサムを検証する
/*		if (th->off - source_off > 0){
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
		src_num = read_num - (th->off - source_off);	// 一度に処理する量 (src_num > 0)
		th->buf = buf + (size_t)unit_size * (th->off - source_off);
		// th->off はソース・ブロックの番号
		th->size = src_num;
		th->now = -1;	// 初期値 - 1
		//_mm_sfence();
		for (j = 0; j < cpu_num; j++){
			ResetEvent(hEnd[j]);	// リセットしておく
			SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
		}

		// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
		while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
			// th-now が最高値なので、計算が終わってるのは th-now - cpu_num 個となる
			j = th->now - cpu_num;
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

		source_off += read_num;
	}
	//printf("\nprog_num = %I64d / %I64d\n", prog_num, prog_base);

#ifdef TIMER
time_start = GetTickCount();
#endif
	memcpy(common_buf + common_size, common_buf, common_size);	// 後の半分に前半のをコピーする
	// 最後にパリティ・ブロックのチェックサムを検証して、リカバリ・ファイルに書き込む
	err = create_recovery_file_1pass(file_path, recovery_path, packet_limit, block_distri,
			packet_num, common_buf, common_size, footer_buf, footer_size, rcv_hFile, p_buf, unit_size);
#ifdef TIMER
time_write = GetTickCount() - time_start;
#endif

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
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
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	unsigned short *factor1;
	int err = 0, i, j, last_file;
	int cpu_num1, cover_max, cover_from, cover_num;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 md_ctx, *md_ptr = NULL;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	factor1 = constant + source_num;
	cpu_num1 = cpu_num;	// 最後のスレッドを GPU 管理用にする
	if (cpu_num == 1)
		cpu_num1++;

	// 作業バッファーを確保する（GPU の作業領域として2個の余裕を見ておく）
	// part_num を使わず、全てのブロックを保持する所がencode_method2と異なることに注意！
	io_size = get_io_size(source_num + parity_num + 2, NULL, 1, MEM_UNIT);
	//io_size = (((io_size + 1) / 2 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 2分割の実験用
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 3分割の実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	file_off = (source_num + parity_num) * (size_t)unit_size + HASH_SIZE
			+ (source_num * sizeof(unsigned short) * cpu_num1);
	buf = _aligned_malloc((size_t)file_off, MEM_UNIT);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * source_num;	// パリティ・ブロックを記録する領域
	hash = p_buf + (size_t)unit_size * parity_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_write = source_num >> 5;	// 計算で 97%、書き込みで 3% ぐらい
	if (prog_write == 0)
		prog_write = 1;
	prog_base *= (__int64)(source_num + prog_write) * parity_num;	// 全体の断片の個数
#ifdef TIMER
	printf("\n read all source blocks, and keep all parity blocks (GPU)\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
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
	cover_max = source_num;
	len = 0;
	i = init_OpenCL(unit_size, &cover_max, &len);
	if (i != 0){
		if (i != 3)	// GPU が見つからなかった場合はエラー表示しない
			printf("init_OpenCL, %d, %d\n", i & 0xFF, i >> 8);
		i = free_OpenCL();
		if (i != 0)
			printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
		OpenCL_method = 0;	// GPU を使わない設定にする
		// GPU を使わずに計算を続行する場合は以下をコメントアウト
		err = -2;	// CPU だけの方式に切り替える
		goto error_end;
	}
	if (len == 0)	// GPUがキャッシュを使わない時だけ、CPU独自にキャッシュの最適化を試みる
		len = try_cache_blocking(unit_size);
#ifdef TIMER
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_cache & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %I64d, unit_size = %d, method = %d, cover_max = %d\n", prog_base, unit_size, OpenCL_method, cover_max);
#endif

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th->buf = p_buf;
	th->size = unit_size;
	th->count = source_num;
	th->off = len;	// chunk size
	for (j = 0; j < cpu_num1; j++){	// サブ・スレッドごとに
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
		th->now = j;	// スレッド番号
		//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
		if ((j == cpu_num1 - 1) && (OpenCL_method != 0)){	// 最後のスレッドを GPU 管理用にする
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode_gpu, (LPVOID)th, 0, NULL);
		} else {
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode_each, (LPVOID)th, 0, NULL);
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
	// IO が延滞しないように、サブ・スレッド一つの優先度を下げる
	SetThreadPriority(hSub[0], THREAD_PRIORITY_BELOW_NORMAL);

	// ソース・ブロック断片を読み込んで、パリティ・ブロック断片を作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->size = 0xFFFFFFFF;	// 1st encode
		th->off = -1;	// まだ計算して無い印

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

				if (i + 1 < source_num){	// 最後のブロック以外なら
					// サブ・スレッドの動作状況を調べる
					j = WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, 0);
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
						th->off += 1;
						if (th->off > 0){	// バッファーに読み込んだ時だけ計算する
							while (s_blk[th->off].size <= block_off){
								prog_num += parity_num;
								th->off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						th->buf = buf + (size_t)unit_size * th->off;
						for (j = 0; j < parity_num; j++)
							factor1[j] = galois_power(constant[th->off], first_num + j);	// factor は定数行列の乗数になる
						th->now = -1;	// 初期値 - 1
						//_mm_sfence();
						for (j = 0; j < (cpu_num + 1) / 2; j++){
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
					}
				}
			} else {
				memset(buf + (size_t)unit_size * i, 0, unit_size);
			}
		}
		// 最後のソース・ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->size = 0;	// 2nd encode
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off > 0){
			while (s_blk[th->off].size <= block_off){	// 計算不要なソース・ブロックはとばす
				prog_num += parity_num;
				th->off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		} else {	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * parity_num);
		}
#ifdef TIMER
		j = (th->off * 1000) / source_num;
		printf("partial encode = %d (%d.%d%%), read = %d, skip = %d\n", th->off, j / 10, j % 10, read_count, skip_count);
#endif

		// リカバリ・ファイルに書き込むサイズ
		if (block_size - block_off < io_size){
			len = block_size - block_off;
		} else {
			len = io_size;
		}

		// VRAM のサイズに応じて分割する
		cover_from = th->off;
		i = (source_num - cover_from + cover_max - 1) / cover_max;	// 何回に分けて処理するか
		cover_num = (source_num - cover_from + i - 1) / i;	// 一度に処理する量を平均化する
		//printf("cover range = %d, cover_num = %d\n", source_num - cover_from, cover_num);
		while (cover_from < source_num){
			// ソース・ブロックを何個ずつ処理するか
			if (cover_from + cover_num > source_num)
				cover_num = source_num - cover_from;
			//printf("cover_from = %d, cover_num = %d\n", cover_from, cover_num);

			// GPU と CPU がスレッドごとにパリティ・ブロックを計算する
			th->buf = buf + (size_t)unit_size * cover_from;
			th->off = cover_from;	// ソース・ブロックの番号にする
			th->count = cover_num;
			th->now = -1;	// 初期値 - 1
			//_mm_sfence();
			for (j = 0; j < cpu_num1; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
			while (WaitForMultipleObjects(cpu_num1, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
				// th-now が最高値なので、計算が終わってるのは th-now - cpu_num1 個となる
				j = th->now - cpu_num1;
				if (j < 0)
					j = 0;
				// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
				if (print_progress((int)(((prog_num + cover_num * j) * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}
			if (th->size != 0){	// エラー発生
				i = th->size;
				printf("error, gpu-thread, %d, %d\n", i & 0xFF, i >> 8);
				err = 1;
				goto error_end;
			}

			// 経過表示
			prog_num += cover_num * parity_num;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			cover_from += cover_num;
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// パリティ・ブロックを書き込む
		work_buf = p_buf;
		for (i = 0; i < parity_num; i++){
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
	//printf("prog_num = %I64d / %I64d\n", prog_num, prog_base);

	// ファイルごとにブロックの CRC-32 を検証する
	memset(buf, 0, io_size);
	j = 0;
	while (j < source_num){
		last_file = s_blk[j].file;
		cover_num = (int)((files[last_file].size + (__int64)block_size - 1) / block_size);
		i = j + cover_num - 1;	// 末尾ブロックの番号
		if (s_blk[i].size < block_size){	// 残りを 0 でパディングする
			len = block_size - s_blk[i].size;
			while (len > io_size){
				len -= io_size;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf, io_size);
			}
			s_blk[i].crc = crc_update(s_blk[i].crc, buf, len);
		}
		memset(hash, 0, 16);
		for (i = 0; i < cover_num; i++)	// XOR して 16バイトに減らす
			((unsigned int *)hash)[i & 3] ^= s_blk[j + i].crc ^ 0xFFFFFFFF;
		if (memcmp(files[last_file].hash, hash, 16) != 0){
			printf("checksum mismatch, input file %d\n", last_file);
			err = 1;
			goto error_end;
		}
		j += cover_num;
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
#endif
	info_OpenCL(buf, MEM_UNIT);	// デバイス情報を表示する

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num1; j++){
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
	unsigned char *buf = NULL, *p_buf;
	unsigned short *factor1;
	int err = 0, i, j, last_file, source_off, read_num, packet_off;
	int cpu_num1, cover_max, cover_from, cover_num;
	unsigned int unit_size, len;
	unsigned int time_last, prog_write;
	__int64 prog_num = 0, prog_base;
	size_t mem_size;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 file_md_ctx, blk_md_ctx;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	factor1 = constant + source_num;
	unit_size = (block_size + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);	// MEM_UNIT の倍数にする
	cpu_num1 = cpu_num;	// 最後のスレッドを GPU 管理用にする
	if (cpu_num == 1)
		cpu_num1++;

	// 作業バッファーを確保する（GPU の作業領域として2個の余裕を見ておく）
	read_num = read_block_num(parity_num, 2, 1, MEM_UNIT);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
#ifdef TIMER
		printf("cannot keep enough blocks, use another method\n");
#endif
		return -4;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 1) / 2 + 1;	// 2分割の実験用
	//read_num = (read_num + 2) / 3 + 1;	// 3分割の実験用
	mem_size = (size_t)(read_num + parity_num) * unit_size
			+ (read_num * sizeof(unsigned short) * cpu_num1);
	buf = _aligned_malloc(mem_size, MEM_UNIT);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %Id\n", mem_size);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	prog_write = source_num >> 5;	// 計算で 97%、書き込みで 3% ぐらい
	if (prog_write == 0)
		prog_write = 1;
	prog_base = (__int64)(source_num + prog_write) * parity_num;	// ブロックの合計掛け算個数 + 書き込み回数

	// OpenCL の初期化
	cover_max = read_num;	// 読み込める分だけにする
	len = 0;
	i = init_OpenCL(unit_size, &cover_max, &len);
	if (i != 0){
		if (i != 3)	// GPU が見つからなかった場合はエラー表示しない
			printf("init_OpenCL, %d, %d\n", i & 0xFF, i >> 8);
		i = free_OpenCL();
		if (i != 0)
			printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
		OpenCL_method = 0;	// GPU を使わない設定にする
		// GPU を使わずに計算を続行する場合は以下をコメントアウト
		err = -3;	// CPU だけの方式に切り替える
		goto error_end;
	}
	if (len == 0)	// GPUがキャッシュを使わない時だけ、CPU独自にキャッシュの最適化を試みる
		len = try_cache_blocking(unit_size);
	print_progress_text(0, "Creating recovery slice");
#ifdef TIMER
	printf("\n read some source blocks, and keep all parity blocks (GPU)\n");
	printf("buffer size = %Id MB, read_num = %d, round = %d\n", mem_size >> 20, read_num, (source_num + read_num - 1) / read_num);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_cache & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %I64d, unit_size = %d, method = %d, cover_max = %d\n", prog_base, unit_size, OpenCL_method, cover_max);
#endif

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th->buf = p_buf;
	th->size = unit_size;
	th->count = read_num;
	th->off = len;	// chunk size
	for (j = 0; j < cpu_num1; j++){	// サブ・スレッドごとに
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
		th->now = j;	// スレッド番号
		//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
		if ((j == cpu_num1 - 1) && (OpenCL_method != 0)){	// 最後のスレッドを GPU 管理用にする
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode_gpu, (LPVOID)th, 0, NULL);
		} else {
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_encode_each, (LPVOID)th, 0, NULL);
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
	// IO が延滞しないように、サブ・スレッド一つの優先度を下げる
	SetThreadPriority(hSub[0], THREAD_PRIORITY_BELOW_NORMAL);

	// 何回かに別けてソース・ブロックを読み込んで、パリティ・ブロックを少しずつ作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	last_file = -1;
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 0xFFFFFFFF;	// 1st encode
		th->off = source_off - 1;	// まだ計算して無い印

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

			if (i + 1 < read_num){	// 最後のブロック以外なら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, 0);
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
					th->off += 1;
					th->buf = buf + (size_t)unit_size * (th->off - source_off);
					for (j = 0; j < parity_num; j++)
						factor1[j] = galois_power(constant[th->off], first_num + j);	// factor は定数行列の乗数になる
					th->now = -1;	// 初期値 - 1
					//_mm_sfence();
					for (j = 0; j < (cpu_num + 1) / 2; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
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

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->size = 0;	// 2nd encode
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off == 0)	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * parity_num);
#ifdef TIMER
		j = (th->off - source_off) * 1000 / read_num;
		printf("partial encode = %d (%d.%d%%)\n", th->off - source_off, j / 10, j % 10);
#endif

		// VRAM のサイズに応じて分割する
		cover_from = th->off - source_off;
		i = (read_num - cover_from + cover_max - 1) / cover_max;	// 何回に分けて処理するか
		cover_num = (read_num - cover_from + i - 1) / i;	// 一度に処理する量を平均化する
		//printf("cover range = %d, cover_num = %d\n", read_num - cover_from, cover_num);
		while (cover_from < read_num){
			// ソース・ブロックを何個ずつ処理するか
			if (cover_from + cover_num > read_num)
				cover_num = read_num - cover_from;
			//printf("cover_from = %d, cover_num = %d\n", cover_from, cover_num);

			// GPU と CPU がスレッドごとにパリティ・ブロックを計算する
			th->buf = buf + (size_t)unit_size * cover_from;
			th->off = source_off + cover_from;	// ソース・ブロックの番号にする
			th->count = cover_num;
			th->now = -1;	// 初期値 - 1
			//_mm_sfence();
			for (j = 0; j < cpu_num1; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待ちながら、経過表示する
			while (WaitForMultipleObjects(cpu_num1, hEnd, TRUE, UPDATE_TIME) == WAIT_TIMEOUT){
				// th-now が最高値なので、計算が終わってるのは th-now - cpu_num1 個となる
				j = th->now - cpu_num1;
				if (j < 0)
					j = 0;
				// 経過表示（UPDATE_TIME 時間待った場合なので、必ず経過してるはず）
				if (print_progress((int)(((prog_num + cover_num * j) * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}
			if (th->size != 0){	// エラー発生
				i = th->size;
				printf("error, gpu-thread, %d, %d\n", i & 0xFF, i >> 8);
				err = 1;
				goto error_end;
			}

			// 経過表示
			prog_num += cover_num * parity_num;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			cover_from += cover_num;
		}

		source_off += read_num;
	}
	//printf("\nprog_num = %I64d / %I64d\n", prog_num, prog_base);

#ifdef TIMER
time_start = GetTickCount();
#endif
	memcpy(common_buf + common_size, common_buf, common_size);	// 後の半分に前半のをコピーする
	// 最後にパリティ・ブロックのチェックサムを検証して、リカバリ・ファイルに書き込む
	err = create_recovery_file_1pass(file_path, recovery_path, packet_limit, block_distri,
			packet_num, common_buf, common_size, footer_buf, footer_size, rcv_hFile, p_buf, unit_size);
#ifdef TIMER
time_write = GetTickCount() - time_start;
#endif

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
#endif
	info_OpenCL(buf, MEM_UNIT);	// デバイス情報を表示する

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num1; j++){
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

