// reedsolomon.c
// Copyright : 2023-10-21 Yutaka Sawada
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
#include "rs_encode.h"
#include "rs_decode.h"
#include "reedsolomon.h"


// GPU を使う最小データサイズ (MB 単位)
// GPU の起動には時間がかかるので、データが小さすぎると逆に遅くなる
#define GPU_DATA_LIMIT 200

// GPU を使う最小ブロックサイズとブロック数
// CPU と GPU で処理を割り振る為には、ある程度のブロック数を必要とする
#define GPU_BLOCK_SIZE_LIMIT 65536
#define GPU_SOURCE_COUNT_LIMIT 192
#define GPU_PARITY_COUNT_LIMIT 8

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// chunk がキャッシュに収まるようにすれば速くなる！ (Cache Blocking という最適化手法)
int try_cache_blocking(int unit_size)
{
	int limit_size, chunk_count, chunk_size, cache_line_diff;

	// CPUキャッシュをどのくらいまで使うか
	limit_size = cpu_flag & 0x7FFF0000;	// 最低でも 64KB になる
	if (limit_size == 0)	// キャッシュ・サイズを取得できなかった場合は最適化しない
		return unit_size;

	// キャッシュにうまく収まるように chunk のサイズを決める
	cache_line_diff = 64 - sse_unit;	// cache line size とデータ境界の差
	if (cache_line_diff < 0)
		cache_line_diff = 0;
	chunk_count = 1;
	chunk_size = unit_size;	// unit_size は sse_unit の倍数になってる
	while (chunk_size + cache_line_diff > limit_size){	// 制限サイズより大きいなら
		// 分割数を増やして chunk のサイズを試算してみる
		chunk_count++;
		chunk_size = (unit_size + chunk_count - 1) / chunk_count;
		chunk_size = (chunk_size + (sse_unit - 1)) & ~(sse_unit - 1);	// sse_unit の倍数にする
	}

	return chunk_size;
}

// 空きメモリー量からファイル・アクセスのバッファー・サイズを計算する
// io_size = unit_size - HASH_SIZE になることに注意 (alloc_unit >= HASH_SIZE)
unsigned int get_io_size(
	unsigned int buf_num,	// 何ブロック分の領域を確保するのか
	unsigned int *part_num,	// 部分的なエンコード用の作業領域
	size_t trial_alloc,		// 確保できるか確認するのか
	int alloc_unit)			// メモリー単位の境界 (sse_unit か MEM_UNIT)
{
	unsigned int unit_size, io_size, part_max, part_min;
	size_t mem_size, io_size64;

	if (part_num == NULL){	// 指定が無ければ調節しない
		part_max = 0;
		part_min = 0;
	} else {
		part_max = *part_num;	// 初期値には最大値をセットする
		part_min = source_num >> PART_MIN_RATE;
		part_min = (part_min / cpu_num) * cpu_num;	// cpu_num の倍数にする（切り下げ）
		if ((int)part_min < cpu_num * 2)
			part_min = cpu_num * 2;	// ダブル・バッファリングするなら cpu_num の倍以上にすること
		if (part_min > part_max)
			part_min = part_max;
#ifdef TIMER
		printf("get_io_size: part_min = %d, part_max = %d\n", part_min, part_max);
#endif
	}
	// alloc_unit の倍数にする
	unit_size = (block_size + HASH_SIZE + (alloc_unit - 1)) & ~(alloc_unit - 1);

	if (trial_alloc){
		__int64 possible_size;
		possible_size = (__int64)unit_size * (buf_num + part_max);
#ifndef _WIN64	// 32-bit 版なら
		if (possible_size > MAX_MEM_SIZE)	// 確保する最大サイズを 2GB までにする
			possible_size = MAX_MEM_SIZE;
		if (check_OS64() == 0){	// 32-bit OS 上なら更に制限する
			if (possible_size > MAX_MEM_SIZE32)
				possible_size = MAX_MEM_SIZE32;
		}
#endif
		trial_alloc = (size_t)possible_size;
		trial_alloc = (trial_alloc + 0xFFFF) & ~0xFFFF;	// 64KB の倍数にしておく
	}
	mem_size = get_mem_size(trial_alloc);
	io_size64 = mem_size / (buf_num + part_max) - HASH_SIZE;	// 何個分必要か

	// ブロック・サイズより大きい、またはブロック・サイズ自体が小さい場合は
	if ((io_size64 >= (size_t)block_size) || (block_size <= 1024)){
		io_size = unit_size - HASH_SIZE;	// ブロック・サイズ - HASH_SIZE

	} else {	// ブロック・サイズを等分割する
		unsigned int num, num2;
		io_size = (unsigned int)io_size64;
		num = (block_size + io_size - 1) / io_size;	// ブロックを何分割するか
		if (part_min < part_max){	// 保持する量に幅があるなら
			io_size64 = mem_size / (buf_num + part_min) - HASH_SIZE;	// 確保するサイズを最低限にした場合
			if (io_size64 >= (size_t)block_size){
				num2 = 1;
			} else {
				io_size = (unsigned int)io_size64;
				num2 = (block_size + io_size - 1) / io_size;
			}
		} else {
			num2 = num;
		}
		if (num > num2){	// 確保量を減らしたほうがブロックの分割数が減るなら
			io_size = (block_size + num2 - 1) / num2;
			if (io_size < 1024)
				io_size = 1024;
			num = (unsigned int)(mem_size / (io_size + HASH_SIZE)) - buf_num;
			if (num < part_max){	// 分割して計算するなら
				num2 = (parity_num + num - 1) / num;	// 分割回数
				num = (parity_num + num2 - 1) / num2;
				num = ((num + cpu_num - 1) / cpu_num) * cpu_num;	// cpu_num の倍数にする（切り上げ）
				if (num < part_min)
					num = part_min;
			}
			if (num > part_max)
				num = part_max;
			*part_num = num;
		} else {
			io_size = (block_size + num - 1) / num;
			if (io_size < 1024)
				io_size = 1024;	// 断片化する場合でもブロック数が多いと 32768 KB は使う
		}
		io_size = ((io_size + HASH_SIZE + (alloc_unit - 1)) & ~(alloc_unit - 1)) - HASH_SIZE;	// alloc_unit の倍数 - HASH_SIZE
	}

	return io_size;
}

// 何ブロックまとめてファイルから読み込むかを空きメモリー量から計算する
int read_block_num(
	int keep_num,			// 保持するパリティ・ブロック数
	size_t trial_alloc,		// 確保できるか確認するのか
	int alloc_unit)			// メモリー単位の境界 (sse_unit か MEM_UNIT)
{
	int buf_num, read_min;
	unsigned int unit_size;
	size_t mem_size;

	read_min = keep_num >> READ_MIN_RATE;
	if (read_min < READ_MIN_NUM)
		read_min = READ_MIN_NUM;
	if (read_min > source_num)
		read_min = source_num;
	unit_size = (block_size + HASH_SIZE + (alloc_unit - 1)) & ~(alloc_unit - 1);

	if (trial_alloc){
		__int64 possible_size;
		possible_size = (__int64)unit_size * (source_num + keep_num);
#ifndef _WIN64	// 32-bit 版なら
		if (possible_size > MAX_MEM_SIZE)	// 確保する最大サイズを 2GB までにする
			possible_size = MAX_MEM_SIZE;
		if (check_OS64() == 0){	// 32-bit OS 上なら更に制限する
			if (possible_size > MAX_MEM_SIZE32)
				possible_size = MAX_MEM_SIZE32;
		}
#endif
		trial_alloc = (size_t)possible_size;
		trial_alloc = (trial_alloc + 0xFFFF) & ~0xFFFF;	// 64KB の倍数にしておく
	}
	mem_size = get_mem_size(trial_alloc) / unit_size;	// 何個分確保できるか

	if (mem_size >= (size_t)(source_num + keep_num)){	// 最大個数より多い
		buf_num = source_num;
	} else if ((int)mem_size < read_min + keep_num){	// 少なすぎる
		buf_num = 0;	// メモリー不足の印
	} else {	// ソース・ブロック個数を等分割する
		int split_num;
		buf_num = (int)mem_size - keep_num;
		split_num = (source_num + buf_num - 1) / buf_num;	// 何回に別けて読み込むか
		buf_num = (source_num + split_num - 1) / split_num;
	}

	return buf_num;
}

// 1st encode, decode を何スレッドで実行するか決める
int calc_thread_num1(int max_num)
{
	int i, num;

	// 読み込み中はスレッド数を減らす（シングル・スレッドの時は 0にする）
	num = 0;
	i = 1;
	while (i * 2 <= cpu_num){	// 1=0, 2~3=1, 4~7=2, 8~15=3, 16~31=4, 32=5
		num++;
		i *= 2;
	}
	if (num > max_num)
		num = max_num;

	return num;
}

// 1st & 2nd encode, decode を何スレッドで実行するか決める
int calc_thread_num2(int max_num, int *cpu_num2)
{
	int i, num1, num2;

	// 読み込み中はスレッド数を減らす（シングル・スレッドの時は 0にする）
	num1 = 0;
	i = 1;
	while (i * 2 <= cpu_num){	// 1=0, 2~3=1, 4~7=2, 8~15=3, 16~31=4, 32=5
		num1++;
		i *= 2;
	}
	if (num1 > max_num)
		num1 = max_num;

	// CPU と GPU で必ず２スレッド使う
	num2 = cpu_num;
	if (num2 < 2)
		num2 = 2;
	*cpu_num2 = num2;

	return num1;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 戸川 隼人 の「演習と応用FORTRAN77」の逆行列の計算方法を参考にして
// Gaussian Elimination を少し修正して行列の数を一つにしてみた

// 半分のメモリーで逆行列を計算する (利用するパリティ・ブロックの所だけ)
static int invert_matrix_st(unsigned short *mat,
	int rows,				// 横行の数、行列の縦サイズ、失われたソース・ブロックの数 = 利用するパリティ・ブロック数
	int cols,				// 縦列の数、行列の横サイズ、本来のソース・ブロック数
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, j, row_start, row_start2, pivot, factor;
	unsigned int time_last = GetTickCount();

	// Gaussian Elimination with 1 matrix
	pivot = 0;
	row_start = 0;	// その行の開始位置
	for (i = 0; i < rows; i++){
		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress((i * 1000) / rows))
				return 2;
			time_last = GetTickCount();
		}

		// その行 (パリティ・ブロック) がどのソース・ブロックの代用か
		while ((pivot < cols) && (s_blk[pivot].exist != 0))
			pivot++;

		// Divide the row by element i,pivot
		factor = mat[row_start + pivot];	// mat(j, pivot) は 0以外のはず
		//printf("\nparity[ %u ] -> source[ %u ], factor = %u\n", id[col_find], col_find, factor);
		if (factor > 1){	// factor が 1より大きいなら、1にする為に factor で割る
			mat[row_start + pivot] = 1;	// これが行列を一個で済ます手
			galois_region_divide(mat + row_start, cols, factor);
		} else if (factor == 0){	// factor = 0 だと、その行列の逆行列を計算できない
			return (0x00010000 | pivot);	// どのソース・ブロックで問題が発生したのかを返す
		}

		// 別の行の同じ pivot 列が 0以外なら、その値を 0にするために、
		// i 行を何倍かしたものを XOR する
		for (j = rows - 1; j >= 0; j--){
			if (j == i)
				continue;	// 同じ行はとばす
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + pivot];	// j 行の pivot 列の値
			mat[row_start2 + pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + row_start, mat + row_start2, cols, factor);
		}
		row_start += cols;	// 次の行にずらす
		pivot++;
	}

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// マルチ・プロセッサー対応
/*
typedef struct {	// RS threading control struct
	unsigned short *mat;	// 行列
	int cols;	// 横行の長さ
	volatile int start;	// 掛ける行の先頭位置
	volatile int pivot;	// 倍率となる値の位置
	volatile int skip;	// とばす行
	volatile int now;	// 消去する行
	HANDLE h;
	HANDLE run;
	HANDLE end;
} INV_TH;

// サブ・スレッド
static DWORD WINAPI thread_func(LPVOID lpParameter)
{
	unsigned short *mat;
	int j, cols, row_start2, factor;
	INV_TH *th;

	th = (INV_TH *)lpParameter;
	mat = th->mat;
	cols = th->cols;

	WaitForSingleObject(th->run, INFINITE);	// 計算開始の合図を待つ
	while (th->skip >= 0){
		while ((j = InterlockedDecrement(&(th->now))) >= 0){	// j = --th_now
			if (j == th->skip)
				continue;
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + th->pivot];	// j 行の pivot 列の値
			mat[row_start2 + th->pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + th->start, mat + row_start2, cols, factor);
		}
		//_mm_sfence();	// メモリーへの書き込みを完了する
		SetEvent(th->end);	// 計算終了を通知する
		WaitForSingleObject(th->run, INFINITE);	// 計算開始の合図を待つ
	}

	// 終了処理
	CloseHandle(th->run);
	CloseHandle(th->end);
	return 0;
}
*/
typedef struct {	// Maxtrix Inversion threading control struct
	unsigned short *mat;	// 行列
	int cols;	// 横行の長さ
	volatile int start;	// 掛ける行の先頭位置
	volatile int pivot;	// 倍率となる値の位置
	volatile int skip;	// とばす行
	volatile int now;	// 消去する行
	HANDLE run;
	HANDLE end;
} INV_TH;

// サブ・スレッド
static DWORD WINAPI thread_func(LPVOID lpParameter)
{
	unsigned short *mat;
	int j, cols, row_start2, factor;
	HANDLE hRun, hEnd;
	INV_TH *th;

	th = (INV_TH *)lpParameter;
	mat = th->mat;
	cols = th->cols;
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->skip >= 0){
		while ((j = InterlockedDecrement(&(th->now))) >= 0){	// j = --th_now
			if (j == th->skip)
				continue;
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + th->pivot];	// j 行の pivot 列の値
			mat[row_start2 + th->pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + th->start, mat + row_start2, cols, factor);
		}
		//_mm_sfence();	// メモリーへの書き込みを完了する
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

// マルチ・スレッドで逆行列を計算する (利用するパリティ・ブロックの所だけ)
/*
static int invert_matrix_mt(unsigned short *mat,
	int rows,				// 横行の数、行列の縦サイズ、失われたソース・ブロックの数 = 利用するパリティ・ブロック数
	int cols,				// 縦列の数、行列の横サイズ、本来のソース・ブロック数
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int j, row_start2, factor;
	unsigned int time_last = GetTickCount();
	INV_TH th[1];

	memset(th, 0, sizeof(INV_TH));

	// イベントを作成する
	th->run = CreateEvent(NULL, FALSE, FALSE, NULL);	// 両方とも Auto Reset にする
	if (th->run == NULL){
		print_win32_err();
		printf("error, inv-thread\n");
		return 1;
	}
	th->end = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (th->end == NULL){
		print_win32_err();
		CloseHandle(th->run);
		printf("error, inv-thread\n");
		return 1;
	}
	// サブ・スレッドを起動する
	th->mat = mat;
	th->cols = cols;
	//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
	th->h = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_func, (LPVOID)th, 0, NULL);
	if (th->h == NULL){
		print_win32_err();
		CloseHandle(th->run);
		CloseHandle(th->end);
		printf("error, inv-thread\n");
		return 1;
	}

	// Gaussian Elimination with 1 matrix
	th->pivot = 0;
	th->start = 0;	// その行の開始位置
	for (th->skip = 0; th->skip < rows; th->skip++){
		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress((th->skip * 1000) / rows)){
				th->skip = -1;	// 終了指示
				//_mm_sfence();
				SetEvent(th->run);
				WaitForSingleObject(th->h, INFINITE);
				CloseHandle(th->h);
				return 2;
			}
			time_last = GetTickCount();
		}

		// その行 (パリティ・ブロック) がどのソース・ブロックの代用か
		while ((th->pivot < cols) && (s_blk[th->pivot].exist != 0))
			th->pivot++;

		// Divide the row by element i,pivot
		factor = mat[th->start + th->pivot];
		if (factor > 1){
			mat[th->start + th->pivot] = 1;	// これが行列を一個で済ます手
			galois_region_divide(mat + th->start, cols, factor);
		} else if (factor == 0){	// factor = 0 だと、その行列の逆行列を計算できない
			th->skip = -1;	// 終了指示
			//_mm_sfence();
			SetEvent(th->run);
			WaitForSingleObject(th->h, INFINITE);
			CloseHandle(th->h);
			return (0x00010000 | th->pivot);	// どのソース・ブロックで問題が発生したのかを返す
		}

		// 別の行の同じ pivot 列が 0以外なら、その値を 0にするために、
		// i 行を何倍かしたものを XOR する
		th->now = rows;	// 初期値 + 1
		//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
		SetEvent(th->run);	// サブ・スレッドに計算を開始させる
		while ((j = InterlockedDecrement(&(th->now))) >= 0){	// j = --th_now
			if (j == th->skip)	// 同じ行はとばす
				continue;
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + th->pivot];	// j 行の pivot 列の値
			mat[row_start2 + th->pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + th->start, mat + row_start2, cols, factor);
		}

		WaitForSingleObject(th->end, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->start += cols;
		th->pivot++;
	}

	// サブ・スレッドを終了させる
	th->skip = -1;	// 終了指示
	//_mm_sfence();
	SetEvent(th->run);
	WaitForSingleObject(th->h, INFINITE);
	CloseHandle(th->h);
	return 0;
}
*/

static int invert_matrix_mt(unsigned short *mat,
	int rows,				// 横行の数、行列の縦サイズ、失われたソース・ブロックの数 = 利用するパリティ・ブロック数
	int cols,				// 縦列の数、行列の横サイズ、本来のソース・ブロック数
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int err = 0, j, row_start2, factor, sub_num;
	unsigned int time_last = GetTickCount();
	HANDLE hSub[MAX_CPU / 2], hRun[MAX_CPU / 2], hEnd[MAX_CPU / 2];
	INV_TH th[1];

	memset(hSub, 0, sizeof(HANDLE) * (MAX_CPU / 2));
	memset(th, 0, sizeof(INV_TH));

	// サブ・スレッドの数は平方根（切り上げ）にする
	sub_num = 1;
	j = 2;
	while (j < cpu_num){	// 1~2=1, 3~4=2, 5~8=3, 9~16=4, 17~32=5
		sub_num++;
		j *= 2;
	}
	if (sub_num > rows - 2)
		sub_num = rows - 2;	// 多過ぎても意味ないので制限する
#ifdef TIMER
	// 使うスレッド数は、メイン・スレッドの分も含めるので 1個増える
	printf("\nMaxtrix Inversion with %d threads\n", sub_num + 1);
#endif

	// サブ・スレッドを起動する
	th->mat = mat;
	th->cols = cols;
	for (j = 0; j < sub_num; j++){	// サブ・スレッドごとに
		// イベントを作成する
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// 両方とも Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, inv-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, inv-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		th->run = hRun[j];
		th->end = hEnd[j];
		//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
		hSub[j] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_func, (LPVOID)th, 0, NULL);
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, inv-thread\n");
			err = 1;
			goto error_end;
		}
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットする)
	}

	// Gaussian Elimination with 1 matrix
	th->pivot = 0;
	th->start = 0;	// その行の開始位置
	for (th->skip = 0; th->skip < rows; th->skip++){
		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress((th->skip * 1000) / rows)){
				err = 2;
				goto error_end;
			}
			time_last = GetTickCount();
		}

		// その行 (パリティ・ブロック) がどのソース・ブロックの代用か
		while ((th->pivot < cols) && (s_blk[th->pivot].exist != 0))
			th->pivot++;

		// Divide the row by element i,pivot
		factor = mat[th->start + th->pivot];
		if (factor > 1){
			mat[th->start + th->pivot] = 1;	// これが行列を一個で済ます手
			galois_region_divide(mat + th->start, cols, factor);
		} else if (factor == 0){	// factor = 0 だと、その行列の逆行列を計算できない
			err = (0x00010000 | th->pivot);	// どのソース・ブロックで問題が発生したのかを返す
			goto error_end;
		}

		// 別の行の同じ pivot 列が 0以外なら、その値を 0にするために、
		// i 行を何倍かしたものを XOR する
		th->now = rows;	// 初期値 + 1
		//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを再開する
		for (j = 0; j < sub_num; j++)
			SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
		while ((j = InterlockedDecrement(&(th->now))) >= 0){	// j = --th_now
			if (j == th->skip)	// 同じ行はとばす
				continue;
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + th->pivot];	// j 行の pivot 列の値
			mat[row_start2 + th->pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + th->start, mat + row_start2, cols, factor);
		}

		WaitForMultipleObjects(sub_num, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->start += cols;
		th->pivot++;
	}

error_end:
	InterlockedExchange(&(th->skip), -1);		// 終了指示
	for (j = 0; j < sub_num; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*
gflib の行列作成用関数や行列の逆変換用の関数を元にして、
計算のやり方を PAR 2.0 用に修正する。

par-v1.1.tar.gz に含まれる rs.doc
Dummies guide to Reed-Solomon coding. を参考にする
*/

/*
5 * 5 なら
 1   1    1     1     1     constant の 0乗
 2   4   16   128   256  <- この行の値を constant とする
 4  16  256 16384  4107     constant の 2乗
 8  64 4096  8566  7099     constant の 3乗
16 256 4107 43963  7166     constant の 4乗

par2-specifications.pdf によると、constant は 2の乗数で、
その指数は (n%3 != 0 && n%5 != 0 && n%17 != 0 && n%257 != 0) になる。
*/

// PAR 2.0 のパリティ検査行列はエンコード中にその場で生成する
// constant と facter の 2個のベクトルで表現する
// パリティ・ブロックごとに facter *= constant で更新していく
static void make_encode_constant(
	unsigned short *constant)	// constant を収めた配列
{
	unsigned short temp;
	int n, i;

	// constant は 2の乗数で、係数が3,5,17,257の倍数になるものは除く
	// 定数 2, 4, 16, 128, 256, 2048, 8192, ...
	n = 0;
	temp = 1;
	for (i = 0; i < source_num; i++){
		while (n <= 65535){
			temp = galois_multiply_fix(temp, 1);	// galois_multiply(temp, 2);
			n++;
			if ((n % 3 != 0) && (n % 5 != 0) && (n % 17 != 0) && (n % 257 != 0))
				break;
		}
		constant[i] = temp;
	}
}

// 復元用の行列を作る、十分な数のパリティ・ブロックが必要
static int make_decode_matrix(
	unsigned short *mat,	// 復元用の行列
	int block_lost,			// 横行、行列の縦サイズ、失われたソース・ブロックの数 = 必要なパリティ・ブロック数
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	parity_ctx_r *p_blk)	// 各パリティ・ブロックの情報
{
	unsigned short *id;		// 失われたソース・ブロックをどのパリティ・ブロックで代用したか
	unsigned short constant;
	int i, j, k, n;

	// printf("\n parity_num = %d, rows = %d, cols = %d \n", parity_num, block_lost, source_num);
	// 失われたソース・ブロックをどのパリティ・ブロックで代用するか
	id = mat + (block_lost * source_num);
	j = 0;
	for (i = 0; (i < parity_num) && (j < block_lost); i++){
		if (p_blk[i].exist == 1)	// 利用不可の印が付いてるブロックは無視する
			id[j++] = (unsigned short)i;
	}
	if (j < block_lost){	// パリティ・ブロックの数が足りなければ
		printf("need more recovery slice\n");
		return 1;
	}

	// 存在して利用するパリティ・ブロックだけの行列を作る
	n = 0;
	constant = 1;
	for (i = 0; i < source_num; i++){	// 一列ずつ縦に値をセットしていく
		while (n <= 65535){
			constant = galois_multiply_fix(constant, 1);	// galois_multiply(constant, 2);
			n++;
			if ((n % 3 != 0) && (n % 5 != 0) && (n % 17 != 0) && (n % 257 != 0))
				break;
		}
//		printf("\n[%5d], 2 pow %5d = %5d", i, n, constant);

		k = 0;
		for (j = 0; j < source_num; j++){	// j 行の i 列
			if (s_blk[j].exist == 0){	// 該当部分はパリティ・ブロックで補うのなら
				mat[source_num * k + i] = galois_power(constant, id[k]);
				k++;
			}
		}
	}

	if ((cpu_num == 1) || (source_num < 10) || (block_lost < 4)){	// 小さすぎる行列はマルチ・スレッドにしない
		k = invert_matrix_st(mat, block_lost, source_num, s_blk);
	} else {
		k = invert_matrix_mt(mat, block_lost, source_num, s_blk);
	}
	return k;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// リード・ソロモン符号を使ってエンコードする
int rs_encode(
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk)		// パリティ・ブロックの情報
{
	unsigned short *constant = NULL;
	int err = 0;
	unsigned int len;
#ifdef TIMER
unsigned int time_total = GetTickCount();
#endif

	if (galois_create_table()){
		printf("galois_create_table\n");
		return 1;
	}

	if (source_num == 1){	// ソース・ブロックが一個だけなら
		err = encode_method1(file_path, header_buf, rcv_hFile, files, s_blk, p_blk);
		goto error_end;
	}

	// パリティ計算用の行列演算の準備をする
	len = sizeof(unsigned short) * source_num;
	if (OpenCL_method != 0)
		len *= 2;	// GPU の作業領域も確保しておく
	constant = malloc(len);
	if (constant == NULL){
		printf("malloc, %d\n", len);
		err = 1;
		goto error_end;
	}
#ifdef TIMER
	if (len & 0xFFFFF000){
		printf("\nmatrix size = %u KB\n", len >> 10);
	} else {
		printf("\nmatrix size = %u Bytes\n", len);
	}
#endif
	// パリティ検査行列の基になる定数
	make_encode_constant(constant);
//	for (len = 0; (int)len < source_num; len++)
//		printf("constant[%5d] = %5d\n", len, constant[len]);

#ifdef TIMER
	err = 0;	// IO method : 0=Auto, -2=Read all, -4=GPU read all
	if (err == 0){
#endif
	// HDD なら 1-pass & Read some 方式を使う
	// メモリー不足や SSD なら、Read all 方式でブロックを断片化させる
	if ((OpenCL_method != 0) && (block_size >= GPU_BLOCK_SIZE_LIMIT) &&
			(source_num >= GPU_SOURCE_COUNT_LIMIT) && (parity_num >= GPU_PARITY_COUNT_LIMIT) &&
			((source_num + parity_num) * (__int64)block_size > 1048576 * GPU_DATA_LIMIT)){
		// ブロック数が多いなら、ブロックごとにスレッドを割り当てる (GPU を使う)
		err = -4;	// 2-pass & GPU read all
	} else {
		err = -2;	// 2-pass & Read all
	}
#ifdef TIMER
	}
#endif

	// 最初は GPUを使い、無理なら次に移る
	if (err == -4)
		err = encode_method4(file_path, header_buf, rcv_hFile, files, s_blk, p_blk, constant);
	if (err == -2)	// ソース・データを全て読み込む場合
		err = encode_method2(file_path, header_buf, rcv_hFile, files, s_blk, p_blk, constant);
#ifdef TIMER
	if (err != 1){
		time_total = GetTickCount() - time_total;
		printf("total  %d.%03d sec\n", time_total / 1000, time_total % 1000);
	}
#endif

error_end:
	if (constant)
		free(constant);
	galois_free_table();	// Galois Field のテーブルを解放する
	return err;
}

// パリティ・ブロックをメモリー上に保持して、一度に読み書きする
int rs_encode_1pass(
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
	source_ctx_c *s_blk)		// ソース・ブロックの情報
{
	unsigned short *constant = NULL;
	int err = 0;
	unsigned int len;
#ifdef TIMER
unsigned int time_total = GetTickCount();
#endif

	if (galois_create_table()){
		printf("galois_create_table\n");
		return 1;
	}

	// パリティ計算用の行列演算の準備をする
	len = sizeof(unsigned short) * source_num;
	if (OpenCL_method != 0)
		len *= 2;	// GPU の作業領域も確保しておく
	constant = malloc(len);
	if (constant == NULL){
		printf("malloc, %d\n", len);
		err = 1;
		goto error_end;
	}
#ifdef TIMER
	if (len & 0xFFFFF000){
		printf("\nmatrix size = %u KB\n", len >> 10);
	} else {
		printf("\nmatrix size = %u Bytes\n", len);
	}
#endif
	// パリティ検査行列の基になる定数
	make_encode_constant(constant);
//	for (len = 0; (int)len < source_num; len++)
//		printf("constant[%5d] = %5d\n", len, constant[len]);

#ifdef TIMER
	err = 0;	// IO method : 0=Auto, -3=Read some, -5=GPU read some, -? = Goto 2pass
	if (err == 0){
#endif
	// メモリーが足りてる場合だけ 1-pass方式を使う
	if ((OpenCL_method != 0) && (block_size >= GPU_BLOCK_SIZE_LIMIT) &&
			(source_num >= GPU_SOURCE_COUNT_LIMIT) && (parity_num >= GPU_PARITY_COUNT_LIMIT) &&
			((source_num + parity_num) * (__int64)block_size > 1048576 * GPU_DATA_LIMIT)){
		err = -5;	// 1-pass & GPU read some
	} else {
		err = -3;	// 1-pass & Read some
	}
#ifdef TIMER
	}
#endif

	// 最初は GPUを使い、無理なら次に移る
	if (err == -5)
		err = encode_method5(file_path, recovery_path, packet_limit, block_distri, packet_num,
				common_buf, common_size, footer_buf, footer_size, rcv_hFile, files, s_blk, constant);
	if (err == -3)	// ソース・データをいくつか読み込む場合
		err = encode_method3(file_path, recovery_path, packet_limit, block_distri, packet_num,
				common_buf, common_size, footer_buf, footer_size, rcv_hFile, files, s_blk, constant);

#ifdef TIMER
	if (err < 0){
		printf("switching to 2-pass processing, %d\n", err);
	} else if (err != 1){
		time_total = GetTickCount() - time_total;
		printf("total  %d.%03d sec\n", time_total / 1000, time_total % 1000);
	}
#endif

error_end:
	if (constant)
		free(constant);
	galois_free_table();	// Galois Field のテーブルを解放する
	return err;
}

// リード・ソロモン符号を使ってデコードする
int rs_decode(
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk)	// パリティ・ブロックの情報
{
	unsigned short *mat = NULL, *id;
	int err = 0, i, j, k;
	unsigned int len;
#ifdef TIMER
unsigned int time_matrix = 0, time_total = GetTickCount();
#endif

	if (galois_create_table()){
		printf("galois_create_table\n");
		return 1;
	}

	if (source_num == 1){	// ソース・ブロックが一個だけなら
		err = decode_method1(file_path, rcv_hFile, files, s_blk, p_blk);
		goto error_end;
	}

	// 復元用の行列演算の準備をする
	len = sizeof(unsigned short) * block_lost * (source_num + 1);
	mat = malloc(len);
	if (mat == NULL){
		printf("malloc, %d\n", len);
		printf("matrix for recovery is too large\n");
		err = 1;
		goto error_end;
	}
#ifdef TIMER
	if (len & 0xFFF00000){
		printf("\nmatrix size = %u MB\n", len >> 20);
	} else if (len & 0x000FF000){
		printf("\nmatrix size = %u KB\n", len >> 10);
	} else {
		printf("\nmatrix size = %u Bytes\n", len);
	}
#endif
	// 何番目の消失ソース・ブロックがどのパリティで代替されるか
	id = mat + (block_lost * source_num);

#ifdef TIMER
time_matrix = GetTickCount();
#endif
	// 復元用の行列を計算する
	print_progress_text(0, "Computing matrix");
	err = make_decode_matrix(mat, block_lost, s_blk, p_blk);
	while (err >= 0x00010000){	// 逆行列を計算できなかった場合 ( Petr Matas の修正案を参考に実装)
		printf("\n");
		err ^= 0x00010000;	// エラーが起きた行 (ソース・ブロックの番号)
		printf("fail at input slice %d\n", err);
		k = 0;
		for (i = 0; i < err; i++){
			if (s_blk[i].exist == 0)
				k++;
		}
		// id[k] エラーが起きた行に対応するパリティ・ブロックの番号
		p_blk[id[k]].exist = 0x100;	// そのパリティ・ブロックを使わないようにする
		printf("disable recovery slice %d\n", id[k]);
		j = 0;
		for (i = 0; i < parity_num; i++){
			if (p_blk[i].exist == 1)
				j++;	// 利用可能なパリティ・ブロックの数
		}
		if (j >= block_lost){	// 使えるパリティ・ブロックの数が破損ブロックの数以上なら
			print_progress_text(0, "Computing matrix");
			err = make_decode_matrix(mat, block_lost, s_blk, p_blk);
		} else {	// 代替するパリティ・ブロックの数が足りなければ
			printf("fail at recovery slice");
			for (i = 0; i < parity_num; i++){
				if (p_blk[i].exist == 0x100)
					printf(" %d", i);
			}
			printf("\n");
			err = 1;
		}
	}
	if (err)	// それ以外のエラーなら
		goto error_end;
	print_progress_done();	// 改行して行の先頭に戻しておく
	//for (i = 0; i < block_lost; i++)
	//	printf("id[%d] = %d\n", i, id[i]);
#ifdef TIMER
time_matrix = GetTickCount() - time_matrix;
#endif

#ifdef TIMER
	err = 0;	// IO method : 0=Auto, -2=Read all, -3=Read some, -4=GPU all, -5=GPU some
	if (err == 0){
#endif
	if ((OpenCL_method != 0) && (block_size >= GPU_BLOCK_SIZE_LIMIT) &&
			(source_num >= GPU_SOURCE_COUNT_LIMIT) && (block_lost >= GPU_PARITY_COUNT_LIMIT) &&
			((source_num + block_lost) * (__int64)block_size > 1048576 * GPU_DATA_LIMIT)){
		// ブロック数が多いなら、ブロックごとにスレッドを割り当てる (GPU を使う)
		if (memory_use & 16){
			err = -4;	// SSD なら Read all 方式でブロックが断片化しても速い
		} else if (read_block_num(block_lost * 2, 0, MEM_UNIT) != 0){
			err = -5;	// HDD でメモリーが足りてるなら Read some 方式を使う
		} else {
			err = -4;	// メモリー不足なら Read all 方式でブロックを断片化させる
		}
	} else {
		// ソース・ブロックを全て断片的に読み込むか、いくつかを丸ごと読み込むかを決める
		if (memory_use & 16){
			err = -2;	// SSD なら Read all 方式でブロックが断片化しても速い
		} else if (read_block_num(block_lost, 0, sse_unit) != 0){
			err = -3;	// HDD でメモリーが足りてるなら Read some 方式を使う
		} else {
			err = -2;	// メモリー不足なら Read all 方式でブロックを断片化させる
		}
	}
#ifdef TIMER
	}
#endif

	// ファイル・アクセスの方式によって分岐する
	if (err == -5)
		err = decode_method5(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
	if (err == -4)
		err = decode_method4(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
	if (err == -3)	// ソース・データをいくつか読み込む場合
		err = decode_method3(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
	if (err == -2)	// ソース・データを全て読み込む場合
		err = decode_method2(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
#ifdef TIMER
	if (err != 1){
		time_total = GetTickCount() - time_total;
		printf("total  %d.%03d sec\n", time_total / 1000, time_total % 1000);
		printf("matrix %d.%03d sec\n", time_matrix / 1000, time_matrix % 1000);
	}
#endif

error_end:
	if (mat)
		free(mat);
	galois_free_table();	// Galois Field のテーブルを解放する
	return err;
}

