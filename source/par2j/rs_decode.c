// rs_decode.c
// Copyright : 2023-10-27 Yutaka Sawada
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
#include "gf16.h"
#include "phmd5.h"
#include "lib_opencl.h"
#include "reedsolomon.h"
#include "rs_decode.h"


#ifdef TIMER
static unsigned int time_start, time_read = 0, time_write = 0, time_calc = 0;
static unsigned int read_count, write_count = 0, skip_count;
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 非同期 IO

typedef struct {	// RS threading control struct
	unsigned short * volatile mat;	// 行列
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
static DWORD WINAPI thread_decode2(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf, *work_buf;
	unsigned short *factor, *factor2;
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
		factor = th->mat;
		src_off = th->off;	// ソース・ブロック番号

		if (th->size == 0){	// ソース・ブロック読み込み中
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < part_num){	// j = ++th_now
				if (src_off == 0)	// 最初のブロックを計算する際に
					memset(p_buf + (size_t)unit_size * j, 0, unit_size);	// ブロックを 0で埋める
				galois_align_multiply(s_buf, p_buf + (size_t)unit_size * j, unit_size, factor[source_num * j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// 消失ブロックを部分的に保持する場合
			// スレッドごとに復元する消失ブロックの chunk を変える
			src_num = th->len;
			part_now = th->size;
			part_off = th->count;
			len = chunk_size;
			max_num = chunk_num * part_now;
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / part_now;	// chunk の番号
				j = j % part_now;	// lost block の番号
				off *= chunk_size;	// chunk の位置
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				work_buf = p_buf + (size_t)unit_size * j + off;
				if (src_off == 0)	// 最初のブロックを計算する際に
					memset(work_buf, 0, len);	// パリティ・ブロックを 0で埋める
				factor2 = factor + source_num * (part_off + j);

				// ソース・ブロックごとにパリティを追加していく
				if (galois_align_multiply2 != NULL){	// ２ブロックずつ計算する場合 (SSSE3 か AVX2)
					i = 0;
					if (src_num & 1){	// 奇数なら最初の一個を計算して、残りを偶数に変える
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor2[i]);
						i++;
					}
					for (; i < src_num; i += 2){
						galois_align_multiply2(s_buf + (size_t)unit_size * i + off, s_buf + (size_t)unit_size * (i + 1) + off,
									work_buf, len, factor2[i], factor2[i + 1]);
					}

				} else {	// 一つずつ計算する場合
					for (i = 0; i < src_num; i++)
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor2[i]);
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
	printf(" 1st decode %d.%03d sec, %d loop, %d MB/s\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a, i);
if (time_encode2b > 0){
	i = (int)((__int64)loop_count2b * unit_size * 125 / ((__int64)time_encode2b * 131072));
} else {
	i = 0;
}
printf(" 2nd decode %d.%03d sec, %d loop, %d MB/s\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b, i);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

static DWORD WINAPI thread_decode3(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf, *work_buf;
	unsigned short *factor, *factor2;
	int i, j, block_lost, max_num, chunk_num;
	int src_off, src_num;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	p_buf = th->buf;
	unit_size = th->size;
	chunk_size = th->len;
	block_lost = th->count;
	hRun = th->run;
	hEnd = th->end;
	//_mm_sfence();
	SetEvent(hEnd);	// 設定完了を通知する

	chunk_num = (unit_size + chunk_size - 1) / chunk_size;
	max_num = chunk_num * block_lost;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		factor = th->mat;

		if (th->size == 0){	// ソース・ブロック読み込み中
			// パリティ・ブロックごとに掛け算して追加していく
			src_off = th->off;	// ソース・ブロック番号
			while ((j = InterlockedIncrement(&(th->now))) < block_lost){	// j = ++th_now
				if (src_off == 0)	// 最初のブロックを計算する際に
					memset(p_buf + (size_t)unit_size * j, 0, unit_size);	// ブロックを 0で埋める
				galois_align_multiply(s_buf, p_buf + (size_t)unit_size * j, unit_size, factor[source_num * j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// 全ての消失ブロックを保持する場合
			// スレッドごとに復元する消失ブロックの chunk を変える
			src_num = th->size;
			len = chunk_size;
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / block_lost;	// chunk の番号
				j = j % block_lost;		// lost block の番号
				off *= chunk_size;		// chunk の位置
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				work_buf = p_buf + (size_t)unit_size * j + off;
				factor2 = factor + source_num * j;

				// ソース・ブロックごとにパリティを追加していく
				if (galois_align_multiply2 != NULL){	// ２ブロックずつ計算する場合 (SSSE3 か AVX2)
					i = 0;
					if (src_num & 1){	// 奇数なら最初の一個を計算して、残りを偶数に変える
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor2[i]);
						i++;
					}
					for (; i < src_num; i += 2){
						galois_align_multiply2(s_buf + (size_t)unit_size * i + off, s_buf + (size_t)unit_size * (i + 1) + off,
									work_buf, len, factor2[i], factor2[i + 1]);
					}

				} else {	// 一つずつ計算する場合
					for (i = 0; i < src_num; i++)
						galois_align_multiply(s_buf + (size_t)unit_size * i + off, work_buf, len, factor2[i]);
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
	printf(" 1st decode %d.%03d sec, %d loop, %d MB/s\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a, i);
if (time_encode2b > 0){
	i = (int)((__int64)loop_count2b * unit_size * 125 / ((__int64)time_encode2b * 131072));
} else {
	i = 0;
}
printf(" 2nd decode %d.%03d sec, %d loop, %d MB/s\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b, i);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

// GPU 対応のサブ・スレッド (最後のスレッドなので、1st decode では呼ばれない)
static DWORD WINAPI thread_decode_gpu(LPVOID lpParameter)
{
	unsigned char *s_buf, *g_buf;
	unsigned short *factor;
	int i, j, block_lost;
	int src_num;
	unsigned int unit_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int time_start2, time_encode2 = 0, loop_count2 = 0;
#endif

	th = (RS_TH *)lpParameter;
	g_buf = th->buf;
	unit_size = th->size;
	block_lost = th->count;
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
		factor = th->mat;
		src_num = th->size;

		// 最初にソース・ブロックをVRAMへ転送する
		i = gpu_copy_blocks(s_buf, unit_size, src_num);
		if (i != 0){
			th->len = i;
			InterlockedExchange(&(th->now), INT_MAX / 3);	// サブ・スレッドの計算を中断する
		}

		// スレッドごとに復元する消失ブロックを変える
		while ((j = InterlockedIncrement(&(th->now))) < block_lost){	// j = ++th_now
			// 倍率は逆行列から部分的にコピーする
			i = gpu_multiply_blocks(src_num, factor + source_num * j, g_buf + (size_t)unit_size * j, unit_size);
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
printf(" 2nd decode %d.%03d sec, %d loop, %d MB/s\n", time_encode2 / 1000, time_encode2 % 1000, loop_count2, i);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int decode_method1(	// ソース・ブロックが一個だけの場合
	wchar_t *file_path,
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk)	// パリティ・ブロックの情報
{
	unsigned char *buf = NULL, *work_buf, *hash;
	int err = 0, id;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_num = 0, prog_base;
	__int64 file_off;
	HANDLE hFile = NULL;

	// 作業バッファーを確保する
	len = 0;
	io_size = get_io_size(2, &len, 1, sse_unit);
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
	prog_base = (block_size + io_size - 1) / io_size;	// 断片の個数
#ifdef TIMER
	printf("\n read one block, and keep one recovering block\n");
	printf("buffer size = %d MB, io_size = %d, split = %d\n", len >> 20, io_size, (block_size + io_size - 1) / io_size);
	id = try_cache_blocking(unit_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF0000, id, (unit_size + id - 1) / id);
#endif

	// 書き込み先のファイルを開く
	wcscpy(file_path, base_dir);
	id = s_blk[0].file;	// ファイル番号
	if (files[id].state & 4){	// 破損ファイルを上書きして復元する場合
		// 上書き用のソース・ファイルを開く
		hFile = handle_write_file(list_buf + files[id].name, file_path, files[id].size);
	} else {
		// 作業用のテンポラリ・ファイルを開く
		hFile = handle_temp_file(list_buf + files[id].name, file_path);
	}
	if (hFile == INVALID_HANDLE_VALUE){
		hFile = NULL;
		err = 1;
		goto error_end;
	}

	// 何番のパリティ・ブロックを使うか
	for (id = 0; id < parity_num; id++){
		if (p_blk[id].exist == 1)
			break;
	}
	//printf("parity_num = %d, id = %d\n", parity_num, id);

	// バッファー・サイズごとにソース・ブロックを復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	block_off = 0;
	while (block_off < block_size){
#ifdef TIMER
time_start = GetTickCount();
#endif
		// パリティ・ブロックを読み込む
		len = block_size - block_off;
		if (len > io_size)
			len = io_size;
		file_off = p_blk[id].off + (__int64)block_off;
		if (file_read_data(rcv_hFile[p_blk[id].file], file_off, buf, len)){
			printf("file_read_data, recovery slice %d\n", id);
			err = 1;
			goto error_end;
		}
		if (len < io_size)
			memset(buf + len, 0, io_size - len);
		// パリティ・ブロックのチェックサムを計算する
		checksum16_altmap(buf, buf + io_size, io_size);
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 失われたソース・ブロックを復元する
		memset(work_buf, 0, unit_size);
		// factor で割ると元に戻る
		galois_align_multiply(buf, work_buf, unit_size, galois_divide(1, galois_power(2, id)));
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
		// 復元されたソース・ブロックのチェックサムを検証する
		checksum16_return(work_buf, hash, io_size);
		if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
			printf("checksum mismatch, recovered input slice %d\n", 0);
			err = 1;
			goto error_end;
		}
		// ファイルにソース・ブロックを書き込む
		len = s_blk[0].size - block_off;
		if (len > io_size)
			len = io_size;
		if (file_write_data(hFile, (__int64)block_off, work_buf, len)){
			printf("file_write_data, input slice %d\n", 0);
			err = 1;
			goto error_end;
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif

		block_off += io_size;
	}
	print_progress_done();	// 末尾ブロックの断片化によっては 100% で完了するとは限らない

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
printf("decode %d.%03d sec\n", time_calc / 1000, time_calc % 1000);
#endif

error_end:
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int decode_method2(	// ソース・データを全て読み込む場合
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,		// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, last_file, chunk_num;
	int part_off, part_num, part_now, recv_now;
	int cpu_num1, src_off, src_num, src_max;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_read, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか

	// 作業バッファーを確保する
	part_num = block_lost;	// 最大値を初期値にする
	//part_num = (block_lost + 1) / 2;	// 確保量の実験用
	//part_num = (block_lost + 2) / 3;	// 確保量の実験用
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
	p_buf = buf + (size_t)unit_size * source_num;	// 復元したブロックを記録する領域
	hash = p_buf + (size_t)unit_size * part_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_read = (block_lost + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base *= (__int64)(source_num + prog_write) * block_lost + prog_read * source_num;	// 全体の断片の個数
	len = try_cache_blocking(unit_size);
	//len = ((len + 2) / 3 + (sse_unit - 1)) & ~(sse_unit - 1);	// 1/3の実験用
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num1(part_num);	// 読み込み中はスレッド数を減らす
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (cpu_num == 1))
		src_max = 0x8000;	// 不明または少な過ぎる場合は、制限しない
#ifdef TIMER
	printf("\n read all blocks, and keep some recovering blocks\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF0000, len, chunk_num);
	printf("unit_size = %d, part_num = %d, cpu_num1 = %d, src_max = %d\n", unit_size, part_num, cpu_num1, src_max);
#endif

	// マルチ・スレッドの準備をする
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
		hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_decode2, (LPVOID)th, 0, NULL);
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

	// ブロック断片を読み込んで、消失ブロック断片を復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->size = 0;	// 1st decode
		src_off = -1;	// まだ計算して無い印

#ifdef TIMER
read_count = 0;
skip_count = 0;
time_start = GetTickCount();
#endif
		last_file = -1;
		recv_now = 0;	// 何番目の代替ブロックか
		for (i = 0; i < source_num; i++){
			switch(s_blk[i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				len = block_size - block_off;
				if (len > io_size)
					len = io_size;
				file_off = p_blk[id[recv_now]].off + (__int64)block_off;
				if (file_read_data(rcv_hFile[p_blk[id[recv_now]].file], file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, recovery slice %d\n", id[recv_now]);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				recv_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				len = 0;
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if (files[last_file].state & 4){	// 上書き中の破損ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					} else if (files[last_file].state & 3){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if (files[last_file].state & 32){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				if (s_blk[i].size > block_off){
					len = s_blk[i].size - block_off;
					if (len > io_size)
						len = io_size;
					file_off = (i - files[last_file].b_off) * (__int64)block_size + (__int64)block_off;
					if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
						printf("file_read_data, input slice %d\n", i);
						err = 1;
						goto error_end;
					}
					if (len < io_size)
						memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
					// ソース・ブロックのチェックサムを計算する
					checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				} else {
					len = 0;
					memset(buf + (size_t)unit_size * i, 0, unit_size);
				}
			}

			if (len > 0){
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
							while ((s_blk[src_off].exist != 0) &&
									((s_blk[src_off].size <= block_off) || (s_blk[src_off].exist == 3))){
								prog_num += part_num;
								src_off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						th->buf = buf + (size_t)unit_size * src_off;
						th->mat = mat + src_off;
						th->off = src_off;
						th->now = -1;	// 初期値 - 1
						//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
						for (j = 0; j < cpu_num1; j++){
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
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
		if (hFile){	// 最後の読み込みファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off > 0){	// 計算不要なソース・ブロックはとばす
			while ((s_blk[src_off].exist != 0) &&
					((s_blk[src_off].size <= block_off) || (s_blk[src_off].exist == 3))){
				prog_num += part_num;
				src_off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		}
		// 1st decode しなかった場合（src_off = 0）は、2nd decode で消失ブロックをゼロ埋めする
#ifdef TIMER
		j = (src_off * 1000) / source_num;
		printf("partial decode = %d / %d (%d.%d%%), read = %d, skip = %d\n", src_off, source_num, j / 10, j % 10, read_count, skip_count);
#endif
		recv_now = -1;	// 消失ブロックの本来のソース番号
		last_file = -1;

		// part_now ごとに処理する
		part_off = 0;
		part_now = part_num;
		while (part_off < block_lost){
			if (part_off + part_now > block_lost)
				part_now = block_lost - part_off;

			// スレッドごとに消失ブロックを計算する
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

				th->mat = mat + src_off;
				th->buf = buf + (size_t)unit_size * src_off;
				th->off = src_off;
				th->len = src_num;
				th->now = -1;	// 初期値 - 1
				//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
				for (j = 0; j < cpu_num; j++){
					ResetEvent(hEnd[j]);	// リセットしておく
					SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
				}

				// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待つ
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
			// 復元されたブロックを書き込む
			work_buf = p_buf;
			for (i = part_off; i < part_off + part_now; i++){
				for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
					if (s_blk[j].exist == 0){
						recv_now = j;
						break;
					}
				}
				//printf(" lost block[%d] = source block[%d]\n", i, recv_now);

				// 復元されたソース・ブロックのチェックサムを検証する
				checksum16_return(work_buf, hash, io_size);
				if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovered input slice %d\n", recv_now);
					err = 1;
					goto error_end;
				}
				if (s_blk[recv_now].size <= block_off){	// 書き込み不要
					work_buf += unit_size;
					prog_num += prog_write;
					continue;
				}
				// ファイルにソース・ブロックを書き込む
				if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[recv_now].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if (files[last_file].state & 4){	// 破損ファイルを上書きして復元する場合
						// 上書き用のソース・ファイルを開く
						hFile = handle_write_file(list_buf + files[last_file].name, file_path, files[last_file].size);
					} else {
						// 作業ファイルを開く
						hFile = handle_temp_file(list_buf + files[last_file].name, file_path);
					}
					if (hFile == INVALID_HANDLE_VALUE){
						hFile = NULL;
						err = 1;
						goto error_end;
					}
					//printf("file %d, open %S\n", last_file, file_path);
				}
				// ソース・ファイル内でのブロック断片の大きさと位置
				len = s_blk[recv_now].size - block_off;
				if (len > io_size)
					len = io_size;
				if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size + block_off, work_buf, len)){
					printf("file_write_data, input slice %d\n", recv_now);
					err = 1;
					goto error_end;
				}
#ifdef TIMER
write_count++;
#endif
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

			part_off += part_num;	// 次の消失ブロック位置にする
		}

		block_off += io_size;
		// 最後の書き込みファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
	}
	print_progress_done();

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
j = ((block_size + io_size - 1) / io_size) * block_lost;
printf("write  %d.%03d sec, count = %d/%d\n", time_write / 1000, time_write % 1000, write_count, j);
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
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

int decode_method3(	// 復元するブロックを全て保持できる場合
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,	// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, last_file, chunk_num;
	int source_off, read_num, recv_now, parity_now;
	int cpu_num1, src_off, src_num, src_max;
	unsigned int unit_size, len;
	unsigned int time_last, prog_read, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか
	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);	// チェックサムの分だけ増やす

	// 作業バッファーを確保する
	read_num = read_block_num(block_lost, 1, sse_unit);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
		//printf("cannot keep enough blocks, use another method\n");
		return -2;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 1) / 2 + 1;	// 2分割の実験用
	//read_num = (read_num + 2) / 3 + 1;	// 3分割の実験用
	file_off = (read_num + block_lost) * (size_t)unit_size + HASH_SIZE;
	buf = _aligned_malloc((size_t)file_off, sse_unit);
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	//memset(buf, 0xFF, (size_t)file_off);	// 後から 0 埋めしてるかの実験用
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	hash = p_buf + (size_t)unit_size * block_lost;
	prog_read = (block_lost + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base = (__int64)(source_num + prog_write) * block_lost + prog_read * source_num;	// ブロックの合計掛け算個数 + 読み書き回数
	len = try_cache_blocking(unit_size);
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num1(block_lost);	// 読み込み中はスレッド数を減らす
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (cpu_num == 1))
		src_max = 0x8000;	// 不明または少な過ぎる場合は、制限しない
#ifdef TIMER
	printf("\n read some blocks, and keep all recovering blocks\n");
	printf("buffer size = %I64d MB, read_num = %d, round = %d\n", file_off >> 20, read_num, (source_num + read_num - 1) / read_num);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF0000, len, chunk_num);
	printf("unit_size = %d, cpu_num1 = %d, src_max = %d\n", unit_size, cpu_num1, src_max);
#endif

	// マルチ・スレッドの準備をする
	th->buf = p_buf;
	th->size = unit_size;
	th->count = block_lost;
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
		hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_decode3, (LPVOID)th, 0, NULL);
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

	// 何回かに別けてブロックを読み込んで、消失ブロックを少しずつ復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	parity_now = 0;	// 何番目の代替ブロックか
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 0;	// 1st decode
		src_off = source_off - 1;	// まだ計算して無い印

#ifdef TIMER
read_count = 0;
time_start = GetTickCount();
#endif
		last_file = -1;
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			switch(s_blk[source_off + i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				if (file_read_data(rcv_hFile[p_blk[id[parity_now]].file], p_blk[id[parity_now]].off, buf + (size_t)unit_size * i, block_size)){
					printf("file_read_data, recovery slice %d\n", id[parity_now]);
					err = 1;
					goto error_end;
				}
				parity_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[source_off + i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if (files[last_file].state & 4){	// 上書き中の破損ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					} else if (files[last_file].state & 3){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if (files[last_file].state & 32){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む (追加訂正失敗時も)
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				len = s_blk[source_off + i].size;
				file_off = (source_off + i - files[last_file].b_off) * (__int64)block_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", source_off + i);
					err = 1;
					goto error_end;
				}
				if (len < block_size)
					memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
				// ソース・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
			}

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
					prog_num += block_lost;
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
					th->mat = mat + src_off;
					th->off = src_off;
					th->now = -1;	// 初期値 - 1
					//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
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
		if (hFile){	// 最後の読み込みファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off == 0)	// 1st decode しなかった場合（src_off = 0）は、消失ブロックをゼロ埋めする
			memset(p_buf, 0, (size_t)unit_size * block_lost);
#ifdef TIMER
		j = (src_off - source_off) * 1000 / read_num;
		printf("partial decode = %d / %d (%d.%d%%), source_off = %d, read = %d\n", src_off - source_off, read_num, j / 10, j % 10, source_off, read_count);
#endif
		recv_now = -1;	// 消失ブロックの本来のソース番号
		last_file = -1;

		// スレッドごとに消失ブロックを計算する
		src_num = src_max;	// 一度に処理するソース・ブロックの数を制限する
		while (src_off < source_off + read_num){
			// ソース・ブロックを何個ずつ処理するか
			if (src_off + src_num * 2 - 1 >= source_off + read_num)
				src_num = source_off + read_num - src_off;
			//printf("src_off = %d, src_num = %d\n", src_off, src_num);

			th->buf = buf + (size_t)unit_size * (src_off - source_off);
			th->mat = mat + src_off;
			th->off = src_off;	// ソース・ブロックの開始番号
			th->size = src_num;
			th->now = -1;	// 初期値 - 1
			//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
			for (j = 0; j < cpu_num; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME だけ待つ
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
			prog_num += src_num * block_lost;
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
	// 復元されたブロックを書き込む
	work_buf = p_buf;
	for (i = 0; i < block_lost; i++){
		for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
			if (s_blk[j].exist == 0){
				recv_now = j;
				break;
			}
		}
		//printf(" lost block[%d] = source block[%d]\n", i, recv_now);

		// 復元されたソース・ブロックのチェックサムを検証する
		checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
		if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
			printf("checksum mismatch, recovered input slice %d\n", recv_now);
			err = 1;
			goto error_end;
		}
		// ファイルにソース・ブロックを書き込む
		if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
			last_file = s_blk[recv_now].file;
			if (hFile){
				CloseHandle(hFile);	// 前のファイルを閉じる
				hFile = NULL;
			}
			if (files[last_file].state & 4){	// 破損ファイルを上書きして復元する場合
				// 上書き用のソース・ファイルを開く
				hFile = handle_write_file(list_buf + files[last_file].name, file_path, files[last_file].size);
			} else {
				// 作業ファイルを開く
				hFile = handle_temp_file(list_buf + files[last_file].name, file_path);
			}
			if (hFile == INVALID_HANDLE_VALUE){
				hFile = NULL;
				err = 1;
				goto error_end;
			}
			//printf("file %d, open %S\n", last_file, file_path);
		}
		if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size, work_buf, s_blk[recv_now].size)){
			printf("file_write_data, input slice %d\n", recv_now);
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
	// 最後の書き込みファイルを閉じる
	CloseHandle(hFile);
	hFile = NULL;
	print_progress_done();

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
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int decode_method4(	// 全てのブロックを断片的に保持する場合 (GPU対応)
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,		// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *g_buf, *work_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, last_file, chunk_num, recv_now;
	int cpu_num1, src_off, src_num, src_max;
	int cpu_num2, vram_max, cpu_end, gpu_end, th_act;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_read, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1], th2[1];

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか

	// 作業バッファーを確保する
	// part_num を使わず、全てのブロックを保持する所がdecode_method2と異なることに注意！
	// CPU計算スレッドと GPU計算スレッドで保存先を別けるので、消失ブロック分を２倍確保する
	io_size = get_io_size(source_num + block_lost * 2, NULL, 1, MEM_UNIT);
	//io_size = (((io_size + 1) / 2 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 2分割の実験用
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 3分割の実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	file_off = (source_num + block_lost * 2) * (size_t)unit_size + HASH_SIZE;
	buf = _aligned_malloc((size_t)file_off, MEM_UNIT);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * source_num;	// 復元したブロックを記録する領域
	g_buf = p_buf + (size_t)unit_size * block_lost;	// GPUスレッド用
	hash = g_buf + (size_t)unit_size * block_lost;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_read = (block_lost + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base *= (__int64)(source_num + prog_write) * block_lost + prog_read * source_num;	// 全体の断片の個数
	len = try_cache_blocking(unit_size);
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num2(block_lost, &cpu_num2);	// 使用するスレッド数を調節する
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (src_max > CACHE_MAX_NUM))
		src_max = CACHE_MAX_NUM;	// 不明または極端な場合は、規定値にする
	//cpu_num1 = 0;	// 2nd decode の実験用に 1st decode を停止する
#ifdef TIMER
	printf("\n read all blocks, and keep all recovering blocks (GPU)\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
	printf("cache: limit size = %d, chunk_size = %d, chunk_num = %d\n", cpu_flag & 0x7FFF0000, len, chunk_num);
	printf("unit_size = %d, cpu_num1 = %d, cpu_num2 = %d\n", unit_size, cpu_num1, cpu_num2);
#endif

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
	th->buf = p_buf;
	th2->buf = g_buf;
	th->size = unit_size;
	th2->size = unit_size;
	th->count = block_lost;
	th2->count = block_lost;
	th->len = len ;	// chunk size
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
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_decode_gpu, (LPVOID)th2, 0, NULL);
		} else {
			th->run = hRun[j];
			th->end = hEnd[j];
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_decode3, (LPVOID)th, 0, NULL);
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

	// ブロック断片を読み込んで、消失ブロック断片を復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->size = 0;	// 1st decode
		src_off = -1;	// まだ計算して無い印

#ifdef TIMER
read_count = 0;
skip_count = 0;
time_start = GetTickCount();
#endif
		last_file = -1;
		recv_now = 0;	// 何番目の代替ブロックか
		for (i = 0; i < source_num; i++){
			switch(s_blk[i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				len = block_size - block_off;
				if (len > io_size)
					len = io_size;
				file_off = p_blk[id[recv_now]].off + (__int64)block_off;
				if (file_read_data(rcv_hFile[p_blk[id[recv_now]].file], file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, recovery slice %d\n", id[recv_now]);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				recv_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				len = 0;
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if (files[last_file].state & 4){	// 上書き中の破損ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					} else if (files[last_file].state & 3){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if (files[last_file].state & 32){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				if (s_blk[i].size > block_off){
					len = s_blk[i].size - block_off;
					if (len > io_size)
						len = io_size;
					file_off = (i - files[last_file].b_off) * (__int64)block_size + (__int64)block_off;
					if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
						printf("file_read_data, input slice %d\n", i);
						err = 1;
						goto error_end;
					}
					if (len < io_size)
						memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
					// ソース・ブロックのチェックサムを計算する
					checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				} else {
					len = 0;
					memset(buf + (size_t)unit_size * i, 0, unit_size);
				}
			}

			if (len > 0){
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
						prog_num += block_lost;
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
							while ((s_blk[src_off].exist != 0) &&
									((s_blk[src_off].size <= block_off) || (s_blk[src_off].exist == 3))){
								prog_num += block_lost;
								src_off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						th->buf = buf + (size_t)unit_size * src_off;
						th->mat = mat + src_off;
						th->off = src_off;
						th->now = -1;	// 初期値 - 1
						//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
						for (j = 0; j < cpu_num1; j++){
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
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
		if (hFile){	// 最後の読み込みファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		memset(g_buf, 0, (size_t)unit_size * block_lost);	// 待機中に GPU用の領域をゼロ埋めしておく
		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off > 0){	// 計算不要なソース・ブロックはとばす
			while ((s_blk[src_off].exist != 0) &&
					((s_blk[src_off].size <= block_off) || (s_blk[src_off].exist == 3))){
				prog_num += block_lost;
				src_off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		} else {	// 1st decode しなかった場合（src_off = 0）は、消失ブロックをゼロ埋めする
			memset(p_buf, 0, (size_t)unit_size * block_lost);
		}
#ifdef TIMER
		j = (src_off * 1000) / source_num;
		printf("partial decode = %d / %d (%d.%d%%), read = %d, skip = %d\n", src_off, source_num, j / 10, j % 10, read_count, skip_count);
#endif

		recv_now = -1;	// 消失ブロックの本来のソース番号
		last_file = -1;
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
					prog_num += th->size * block_lost;
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
					prog_num += th2->size * block_lost;
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

			// どちらかのスレッドで消失ブロックを計算する
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
				th->mat = mat + src_off;
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
					if (gpu_end == 0){	// 最初に負担するブロック数は CPUスレッド 1個の半分にする
						src_num = (source_num - src_off) / (cpu_num2 * 2);
						if (src_num < src_max){
							src_num = src_max;
						} else if (src_num > src_max * 2){	// ただし、CPUスレッド担当量の 2倍までに制限する
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
				th2->mat = mat + src_off;
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
			prog_num += th2->size * block_lost;
		}
		if (th->size > 0)	// CPUスレッドの計算量を加算する
			prog_num += th->size * block_lost;

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 復元されたブロックを書き込む
		work_buf = p_buf;
		for (i = 0; i < block_lost; i++){
			for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
				if (s_blk[j].exist == 0){
					recv_now = j;
					break;
				}
			}
			//printf(" lost block[%d] = source block[%d]\n", i, recv_now);

			// CPUスレッドと GPUスレッドの計算結果を合わせる
			galois_align_xor(g_buf + (size_t)unit_size * i, work_buf, unit_size);
			// 復元されたソース・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovered input slice %d\n", recv_now);
				err = 1;
				goto error_end;
			}
			if (s_blk[recv_now].size <= block_off){	// 書き込み不要
				work_buf += unit_size;
				prog_num += prog_write;
				continue;
			}
			// ファイルにソース・ブロックを書き込む
			if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[recv_now].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				if (files[last_file].state & 4){	// 破損ファイルを上書きして復元する場合
					// 上書き用のソース・ファイルを開く
					hFile = handle_write_file(list_buf + files[last_file].name, file_path, files[last_file].size);
				} else {
					// 作業ファイルを開く
					hFile = handle_temp_file(list_buf + files[last_file].name, file_path);
				}
				if (hFile == INVALID_HANDLE_VALUE){
					hFile = NULL;
					err = 1;
					goto error_end;
				}
				//printf("file %d, open %S\n", last_file, file_path);
			}
			// ソース・ファイル内でのブロック断片の大きさと位置
			len = s_blk[recv_now].size - block_off;
			if (len > io_size)
				len = io_size;
			if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size + block_off, work_buf, len)){
				printf("file_write_data, input slice %d\n", recv_now);
				err = 1;
				goto error_end;
			}
#ifdef TIMER
write_count++;
#endif
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
		// 最後の書き込みファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
	}
	print_progress_done();

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
j = ((block_size + io_size - 1) / io_size) * block_lost;
printf("write  %d.%03d sec, count = %d/%d\n", time_write / 1000, time_write % 1000, write_count, j);
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
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	i = free_OpenCL();
	if (i != 0)
		printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
	return err;
}

int decode_method5(	// 復元するブロックだけ保持する場合 (GPU対応)
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,	// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *g_buf, *work_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, last_file, chunk_num, recv_now;
	int source_off, read_num, parity_now;
	int cpu_num1, src_off, src_num, src_max;
	int cpu_num2, vram_max, cpu_end, gpu_end, th_act;
	unsigned int unit_size, len;
	unsigned int time_last, prog_read, prog_write;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1], th2[1];

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか
	unit_size = (block_size + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);	// MEM_UNIT の倍数にする

	// 作業バッファーを確保する
	// CPU計算スレッドと GPU計算スレッドで保存先を別けるので、消失ブロック分を２倍確保する
	read_num = read_block_num(block_lost * 2, 1, MEM_UNIT);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
		//printf("cannot keep enough blocks, use another method\n");
		return -4;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 1) / 2 + 1;	// 2分割の実験用
	//read_num = (read_num + 2) / 3 + 1;	// 3分割の実験用
	file_off = (read_num + block_lost * 2) * (size_t)unit_size + HASH_SIZE;
	buf = _aligned_malloc((size_t)file_off, MEM_UNIT);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	g_buf = p_buf + (size_t)unit_size * block_lost;	// GPUスレッド用
	hash = g_buf + (size_t)unit_size * block_lost;
	prog_read = (block_lost + 31) / 32;	// 読み書きの経過をそれぞれ 3% ぐらいにする
	prog_write = (source_num + 31) / 32;
	prog_base = (__int64)(source_num + prog_write) * block_lost + prog_read * source_num;	// ブロックの合計掛け算個数 + 書き込み回数
	len = try_cache_blocking(unit_size);
	chunk_num = (unit_size + len - 1) / len;
	cpu_num1 = calc_thread_num2(block_lost, &cpu_num2);	// 使用するスレッド数を調節する
	src_max = cpu_cache & 0xFFFE;	// CPU cache 最適化のため、同時に処理するブロック数を制限する
	if ((src_max < CACHE_MIN_NUM) || (src_max > CACHE_MAX_NUM))
		src_max = CACHE_MAX_NUM;	// 不明または極端な場合は、規定値にする
	//cpu_num1 = 0;	// 2nd decode の実験用に 1st decode を停止する
#ifdef TIMER
	printf("\n read some blocks, and keep all recovering blocks (GPU)\n");
	printf("buffer size = %I64d MB, read_num = %d, round = %d\n", file_off >> 20, read_num, (source_num + read_num - 1) / read_num);
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

	// マルチ・スレッドの準備をする
	th->buf = p_buf;
	th2->buf = g_buf;
	th->size = unit_size;
	th2->size = unit_size;
	th->count = block_lost;
	th2->count = block_lost;
	th->len = len ;	// chunk size
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
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_decode_gpu, (LPVOID)th2, 0, NULL);
		} else {
			th->run = hRun[j];
			th->end = hEnd[j];
			hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_decode3, (LPVOID)th, 0, NULL);
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

	// 何回かに別けてブロックを読み込んで、消失ブロックを少しずつ復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	parity_now = 0;	// 何番目の代替ブロックか
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 0;	// 1st decode
		src_off = source_off - 1;	// まだ計算して無い印

#ifdef TIMER
read_count = 0;
time_start = GetTickCount();
#endif
		last_file = -1;
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			switch(s_blk[source_off + i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				if (file_read_data(rcv_hFile[p_blk[id[parity_now]].file], p_blk[id[parity_now]].off, buf + (size_t)unit_size * i, block_size)){
					printf("file_read_data, recovery slice %d\n", id[parity_now]);
					err = 1;
					goto error_end;
				}
				parity_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[source_off + i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if (files[last_file].state & 4){	// 上書き中の破損ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					} else if (files[last_file].state & 3){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if (files[last_file].state & 32){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む (追加訂正失敗時も)
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				len = s_blk[source_off + i].size;
				file_off = (source_off + i - files[last_file].b_off) * (__int64)block_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", source_off + i);
					err = 1;
					goto error_end;
				}
				if (len < block_size)
					memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
				// ソース・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
			}

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
					prog_num += block_lost;
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
					th->mat = mat + src_off;
					th->off = src_off;
					th->now = -1;	// 初期値 - 1
					//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
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
		if (hFile){	// 最後の読み込みファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		if (source_off == 0)
			memset(g_buf, 0, (size_t)unit_size * block_lost);	// 待機中に GPU用の領域をゼロ埋めしておく
		WaitForMultipleObjects(cpu_num1, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		src_off += 1;	// 計算を開始するソース・ブロックの番号
		if (src_off == 0)	// 1st decode しなかった場合（src_off = 0）は、消失ブロックをゼロ埋めする
			memset(p_buf, 0, (size_t)unit_size * block_lost);
#ifdef TIMER
		j = (src_off - source_off) * 1000 / read_num;
		printf("partial decode = %d / %d (%d.%d%%), source_off = %d, read = %d\n", src_off - source_off, read_num, j / 10, j % 10, source_off, read_count);
#endif

		recv_now = -1;	// 消失ブロックの本来のソース番号
		last_file = -1;
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
					prog_num += th->size * block_lost;
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
					prog_num += th2->size * block_lost;
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

			// どちらかのスレッドで消失ブロックを計算する
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
				th->mat = mat + (source_off + src_off);	// ソース・ブロックの番号にする
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
					if (gpu_end == 0){	// 最初に負担するブロック数は CPUスレッド 1個の半分にする
						src_num = (read_num - src_off) / (cpu_num2 * 2);
						if (src_num < src_max){
							src_num = src_max;
						} else if (src_num > src_max * 2){	// ただし、CPUスレッド担当量の 2倍までに制限する
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
				th2->mat = mat + (source_off + src_off);	// ソース・ブロックの番号にする
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
			prog_num += th2->size * block_lost;
		}
		if (th->size > 0)	// CPUスレッドの計算量を加算する
			prog_num += th->size * block_lost;

		source_off += read_num;
	}

#ifdef TIMER
time_start = GetTickCount();
#endif
	// 復元されたブロックを書き込む
	work_buf = p_buf;
	for (i = 0; i < block_lost; i++){
		for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
			if (s_blk[j].exist == 0){
				recv_now = j;
				break;
			}
		}
		//printf(" lost block[%d] = source block[%d]\n", i, recv_now);

		// CPUスレッドと GPUスレッドの計算結果を合わせる
		galois_align_xor(g_buf + (size_t)unit_size * i, work_buf, unit_size);
		// 復元されたソース・ブロックのチェックサムを検証する
		checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
		if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
			printf("checksum mismatch, recovered input slice %d\n", recv_now);
			err = 1;
			goto error_end;
		}
		// ファイルにソース・ブロックを書き込む
		if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
			last_file = s_blk[recv_now].file;
			if (hFile){
				CloseHandle(hFile);	// 前のファイルを閉じる
				hFile = NULL;
			}
			if (files[last_file].state & 4){	// 破損ファイルを上書きして復元する場合
				// 上書き用のソース・ファイルを開く
				hFile = handle_write_file(list_buf + files[last_file].name, file_path, files[last_file].size);
			} else {
				// 作業ファイルを開く
				hFile = handle_temp_file(list_buf + files[last_file].name, file_path);
			}
			if (hFile == INVALID_HANDLE_VALUE){
				hFile = NULL;
				err = 1;
				goto error_end;
			}
			//printf("file %d, open %S\n", last_file, file_path);
		}
		if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size, work_buf, s_blk[recv_now].size)){
			printf("file_write_data, input slice %d\n", recv_now);
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
	// 最後の書き込みファイルを閉じる
	CloseHandle(hFile);
	hFile = NULL;
	print_progress_done();

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
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	i = free_OpenCL();
	if (i != 0)
		printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
	return err;
}

