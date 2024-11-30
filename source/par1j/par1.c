// par1.c
// Copyright : 2024-11-30 Yutaka Sawada
// License : GPL

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601	// Windows 7 or later
#endif

#include <stdio.h>

#include <windows.h>
#include <imagehlp.h>

#include "common1.h"
#include "phmd5.h"
#include "md5_1.h"
#include "ini.h"
#include "par1.h"
#include "version.h"

/*
Fast Galois Field Arithmetic Library in C/C++
Copright (C) 2007 James S. Plank
*/
#include "gf8.h"

/*
// 行列の表示用関数
void galois_print_matrix(int *matrix, int rows, int cols)
{
	int i, j;
	printf("\n");
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			printf("%4d", matrix[i*cols+j]);
		}
		printf("\n");
	}
}
*/

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*
gflib の行列作成用関数や行列の逆変換用の関数を元にして、
計算のやり方を PAR 1.0 用に修正する。

par-v1.1.tar.gz に含まれる rs.doc
Dummies guide to Reed-Solomon coding. を参考にする

解説ではパリティ計算用の行列の要素、掛ける値は
F(i,j) = i^(j-1) ・・・ i の (j-1) 乗
ソース・ブロックは i = 1～N で、
パリティ・ブロックは j = 1～M になってる。

実際に計算する関数では
ソース・ブロックが i = 0 ～ cols - 1 で、
パリティ・ブロックが j = 0 ～ rows - 1 なので、

理論のF(i,j) = F(i+1, j+1) = (i+1) ^ (j+1-1) = (i+1) ^ j
・・・ (i+1) の j 乗
*/

// PAR 1.0 のパリティ作成用の行列
int * make_encode_matrix(
	int rows,	// 横行、パリティのブロック数
	int cols,	// 縦列、ソースのブロック数
	int first)	// 最初のパリティ・ブロック番号
{
	int *mat, i, j;

	mat = malloc(sizeof(int) * rows * cols);
	if (mat == NULL){
		printf("malloc, %d\n", sizeof(int) * rows * cols);
		return NULL;
	}

	for (j = 0; j < rows; j++){
		for (i = 0; i < cols; i++)
			mat[cols * j + i] = galois_power(i + 1, first + j);
	}

	return mat;
}

/*
失われたソース・ブロックの数 = 利用するパリティ・ブロックの数を縦行とし、
ソース・ブロックの数を横列とする長方形の行列を作る。

例えば、ソース・ブロックを A, B, C, D, E 、
そのパリティ・ブロックを X, Y, Z とする。
その場合の関係式は次のようになる。

1: 1*A xor 1*B xor 1*C xor  1*D xor  1*E = X
2: 1*A xor 2*B xor 3*C xor  4*D xor  5*E = Y
3: 1*A xor 4*B xor 5*C xor 16*D xor 17*E = Z

これは PARファイルを作成する際の行列と同じ。
   1   1   1   1   1
   1   2   3   4   5
   1   4   5  16  17

ここで、B, C, D の三ブロックが失われて、それぞれを X, Y, Z で補うなら、
その状態は次のような関係式になる。

1: 1*A xor 1*B xor 1*C xor  1*D xor  1*E = 0*A xor 1*X xor 0*Y xor 0*Z xor 0*E
2: 1*A xor 2*B xor 3*C xor  4*D xor  5*E = 0*A xor 0*X xor 1*Y xor 0*Z xor 0*E
3: 1*A xor 4*B xor 5*C xor 16*D xor 17*E = 0*A xor 0*X xor 0*Y xor 1*Z xor 0*E

左：パリティ・ブロックの係数
   1   1   1   1   1
   1   2   3   4   5
   1   4   5  16  17

右：そのパリティ・ブロックがどのソース・ブロックの位置にくるか
   0   1   0   0   0
   0   0   1   0   0
   0   0   0   1   0

ここでまず、存在するソース・ブロックの列を 0にする。

1: 0*A xor 1*B xor 1*C xor  1*D xor 0*E = 1*A xor 1*X xor 0*Y xor 0*Z xor  1*E
2: 0*A xor 2*B xor 3*C xor  4*D xor 0*E = 1*A xor 0*X xor 1*Y xor 0*Z xor  5*E
3: 0*A xor 4*B xor 5*C xor 16*D xor 0*E = 1*A xor 0*X xor 0*Y xor 1*Z xor 17*E

左：ソース・ブロックが存在した所が 0になる
   0   1   1   1   0
   0   2   3   4   0
   0   4   5  16   0

右：逆にこちらではソース・ブロックが存在した所が反転する
   1   1   0   0   1
   1   0   1   0   5
   1   0   0   1  17

Gaussian Elimination (ガウスの消去法) で元の左の行列を右のに変換する

各行で 0でない値を探して、それが 1になるように、何倍かする。
まずは 1行目：

1: 0*A xor 1*B xor 1*C xor  1*D xor 0*E = 1*A xor 1*X xor 0*Y xor 0*Z xor  1*E
1 行目の 2列目は 1なのでそのまま。

他の行の同じ列に 0でない値があれば、それを 0にするために、
行を何倍かしたものを XOR する

2: 0*A xor 2*B xor 3*C xor  4*D xor 0*E = 1*A xor 0*X xor 1*Y xor 0*Z xor  5*E
2 行目の 2列目は 2なので、1行目の各値を 2倍したものを XOR する。

3: 0*A xor 4*B xor 5*C xor 16*D xor 0*E = 1*A xor 0*X xor 0*Y xor 1*Z xor 17*E
3 行目の 2列目は 4なので、1行目の各値を 4倍したものを XOR する。

1: 0*A xor 1*B xor 1*C xor  1*D xor 0*E = 1*A xor 1*X xor 0*Y xor 0*Z xor  1*E
2: 0*A xor 0*B xor 1*C xor  6*D xor 0*E = 3*A xor 2*X xor 1*Y xor 0*Z xor  7*E
3: 0*A xor 0*B xor 1*C xor 20*D xor 0*E = 5*A xor 4*X xor 0*Y xor 1*Z xor 21*E

次は 2行目：

2: 0*A xor 0*B xor 1*C xor  6*D xor 0*E = 3*A xor 2*X xor 1*Y xor 0*Z xor  7*E
2 行目の 3列目は 1なのでそのまま。

1: 0*A xor 1*B xor 1*C xor  1*D xor 0*E = 1*A xor 1*X xor 0*Y xor 0*Z xor  1*E
1 行目の 3列目は 1なので、1行目の各値をそのまま XOR する。

3: 0*A xor 0*B xor 1*C xor 20*D xor 0*E = 5*A xor 4*X xor 0*Y xor 1*Z xor 21*E
3 行目の 3列目は 1なので、1行目の各値をそのまま XOR する。

1: 0*A xor 1*B xor 0*C xor  7*D xor 0*E = 2*A xor 3*X xor 1*Y xor 0*Z xor  6*E
2: 0*A xor 0*B xor 1*C xor  6*D xor 0*E = 3*A xor 2*X xor 1*Y xor 0*Z xor  7*E
3: 0*A xor 0*B xor 0*C xor 18*D xor 0*E = 6*A xor 6*X xor 1*Y xor 1*Z xor 18*E

次は 3行目：

3: 0*A xor 0*B xor 0*C xor 18*D xor 0*E = 6*A xor 6*X xor 1*Y xor 1*Z xor 18*E
3 行目の 4列目は 18なので、18で割る

1: 0*A xor 1*B xor 0*C xor  7*D xor 0*E = 2*A xor 3*X xor 1*Y xor 0*Z xor  6*E
1 行目の 4列目は 7なので、1行目の各値を 7倍したものを XOR する。

2: 0*A xor 0*B xor 1*C xor  6*D xor 0*E = 3*A xor 2*X xor 1*Y xor 0*Z xor  7*E
2 行目の 4列目は 6なので、1行目の各値を 6倍したものを XOR する。

1: 0*A xor 1*B xor 0*C xor 0*D xor 0*E =   3*A xor   2*X xor 123*Y xor 122*Z xor 1*E
2: 0*A xor 0*B xor 1*C xor 0*D xor 0*E = 184*A xor 185*X xor 187*Y xor 186*Z xor 1*E
3: 0*A xor 0*B xor 0*C xor 1*D xor 0*E = 186*A xor 186*X xor 192*Y xor 192*Z xor 1*E

左：元の右の行列と同じになってる
   0   1   0   0   0
   0   0   1   0   0
   0   0   0   1   0

右：これが失われたソース・ブロックを復元するための行列となる
   3   2 123 122   1
 184 185 187 186   1
 186 186 192 192   1
*/

// 復元用の行列を作る、十分な数のパリティ・ブロックが必要
int * make_decode_matrix(
	int parity_max,	// 本来のパリティ・ブロックの数
	int rows,		// 横行、行列の縦サイズ、失われたソース・ブロックの数 = 利用するパリティ・ブロック数
	int cols,		// 縦列、行列の横サイズ、本来のソース・ブロック数
	int *exist,		// どのブロックが存在するか
	int *id)		// 失われたソース・ブロックをどのパリティ・ブロックで代用したか
{
	int *mat_max, *mat_l, *mat_r;
	int factor, row_start, row_start2, col_find, i, j, k;

	// 失われたソース・ブロックをどのパリティ・ブロックで代用するか
	j = 0;
	for (i = 0; i < cols; i++){
		id[i] = i;
		if (exist[i] == 0){ // ソース・ブロックが存在しない所
			while (j < parity_max){
				if (exist[cols + j] != 0){ // その番号のパリティ・ブロックが存在すれば
					id[i] = cols + j;
					j++;
					break;
				} else {
					j++;
				}
			}
			if (id[i] < cols){
				printf("\nneed more parity volume\n");
				return NULL; // パリティ・ブロックの数が足りなければ
			}
		}
	}

	// 全てのパリティ・ブロックが存在すると仮定した行列を作る
	mat_max = make_encode_matrix(parity_max, cols, 0);
	if (mat_max == NULL)
		return NULL;

	// 存在して利用するパリティ・ブロックだけの行列を作る
	mat_l = malloc(sizeof(int) * rows * cols);
	if (mat_l == NULL) {
		free(mat_max);
		printf("malloc, %d\n", sizeof(int) * rows * cols);
		return NULL;
	}
	j = 0;
	for (i = 0; i < cols; i++){
		if (id[i] >= cols){
			for (k = 0; k < cols; k++)
				mat_l[cols * j + k] = mat_max[cols * (id[i] - cols) + k];
			j++;
		}
	}
	free(mat_max);
	//printf("\nLeft :\n");
	//galois_print_matrix(mat_l, rows, cols);

	// 失われたソース・ブロックと代用するパリティ・ブロックの位置を表す行列を作る
	mat_r = (int *)calloc(rows * cols, sizeof(int));
	if (mat_r == NULL){
		free(mat_l);
		printf("calloc, %d\n", sizeof(int) * rows * cols);
		return NULL;
	}
	j = 0;
	for (i = 0; i < cols; i++){
		if (id[i] >= cols){
			// j = 行番号
			// id[i] = その行を補ったパリティ・ブロックの番号
			// i = その行のソース・ブロック番号
			mat_r[cols * j + i] = 1;
			j++;
		}
	}
	//printf("\nRight :\n");
	//galois_print_matrix(mat_r, rows, cols);

	// mat_l の存在するソース・ブロックの列を 0 にするような値で XOR する
	for (i = 0; i < cols; i++){
		if (exist[i] != 0){ // ソース・ブロックが存在する列
			for (j = 0; j < rows; j++){ // 行ごとに
				factor = mat_l[cols * j + i]; // j 行の i 列の値
				mat_l[cols * j + i] = 0; // mat_l[cols * j + i] ^= factor; と同じ
				mat_r[cols * j + i] ^= factor;
			}
		}
	}
	//printf("\nErase source block :\n");
	//galois_print_matrix(mat_l, rows, cols);
	//printf("\n");
	//galois_print_matrix(mat_r, rows, cols);

	// Gaussian Elimination
	for (j = 0; j < rows; j++){
		row_start = cols * j; // その行の開始位置

		// mat_l の各行ごとに最初の 0でない値を探す
		for (col_find = 0; col_find < cols; col_find++){
			if (mat_l[row_start + col_find] != 0)
				break;
		}
		if (col_find == cols){ // 見つからなければ、その行列の逆行列を計算できない
			printf("\nmatrix is not invertible\n");
			k = 0;
			for (i = 0; i < cols; i++){
				if (id[i] >= cols){
					if (k == j){
						printf(" parity volume %u is useless\n", (1 + id[i] - cols));
						break;
					}
					k++;
				}
			}
			free(mat_l);
			free(mat_r);
			return NULL;
		}
		factor = mat_l[row_start + col_find]; // col_find 列に 0 ではない値を発見
		if (factor != 1){ // factor が 1でなければ、1にする為に factor で割る
			for (k = 0; k < cols; k++){
				mat_l[row_start + k] = galois_multtable_divide(mat_l[row_start + k], factor);
				mat_r[row_start + k] = galois_multtable_divide(mat_r[row_start + k], factor);
			}
		}

		// 別の行の同じ col_find 列が 0以外なら、その値を 0にするために、
		// j 行を何倍かしたものを XOR する
		for (i = 0; i < rows; i++){
			if (i == j)
				continue; // 同じ行はとばす
			row_start2 = cols * i; // その行の開始位置
			factor = mat_l[row_start2 + col_find]; // i 行の col_find 列の値
			if (factor != 0){ // 0でなければ
				// 先の計算により、j 行の col_find 列の値は必ず 1なので、この factor が倍率になる
				if (factor == 1){ // 倍率が 1なら、単純に XOR するだけ
					for (k = 0; k < cols; k++){
						mat_l[row_start2 + k] ^= mat_l[row_start + k];
						mat_r[row_start2 + k] ^= mat_r[row_start + k];
					}
				} else {
					for (k = 0; k < cols; k++){
						mat_l[row_start2 + k] ^= galois_multtable_multiply(mat_l[row_start + k], factor);
						mat_r[row_start2 + k] ^= galois_multtable_multiply(mat_r[row_start + k], factor);
					}
				}
			}
		}

		//printf("\nDivide & Eliminate :\n");
		//galois_print_matrix(mat_l, rows, cols);
		//printf("\n");
		//galois_print_matrix(mat_r, rows, cols);
	}

	free(mat_l);
	return mat_r;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define CHUNK_SIZE 65536

typedef struct {
	HANDLE hFile;	// ファイル・ハンドル
	__int64 size;	// ファイルの残りサイズ
} file_ctx;

// リード・ソロモン符号を使ってエンコードする
int rs_encode(
	int source_num,		// ソース・ブロックの数
	__int64 block_size,	// ブロック・サイズ (最大ファイル・サイズ)
	int parity_num,		// パリティ・ブロックの数
	int first_num,		// 最初のパリティ・ブロック番号
	file_ctx *files,
	HANDLE *par_hFile,
	PHMD5 *par_md5)
{
	unsigned char *buffer, *block = NULL;
	int err = 0, i, j;
	int *mat = NULL, factor;
	unsigned int io_size, unit_size, len, rv;
	unsigned int k, offset, length, chunk_count;
	unsigned int time_last, prog_num = 0, prog_base;
	__int64 block_left;
//unsigned int time1;

	// 利用できるメモリー量を調べる
	io_size = get_mem_size(block_size * (__int64)(source_num + parity_num));
	if (io_size > 1048576 * 2)
		io_size -= (parity_num * source_num) * 4 + 1048576; // 行列に必要なメモリーとスタック用に 1MB
	if (io_size < 1048576)
		io_size = 1048576;
	io_size /= source_num + 1;	// 何個分必要か

	// ブロック・サイズより大きい、またはブロック・サイズ自体が小さい場合は
	if (((__int64)io_size >= block_size) || (block_size <= 4096)){
		io_size = (unsigned int)block_size;	// ブロック・サイズと同じにする
	} else {	// ブロック・サイズを 2の乗数サイズの断片に分割する
		// 断片化する場合でもブロック数が多いと 256 * 4096 = 1MB は使う
		__int64 fragment_size = 4096;	// 最低サイズ
		while (fragment_size * 2 <= (__int64)io_size)
			fragment_size *= 2;
		io_size = (unsigned int)fragment_size;
	}
	io_size = (io_size + 3) & 0xFFFFFFFC;	// 4の倍数にする
	unit_size = io_size + HASH_SIZE;		// チェックサムの分だけ増やす
	prog_base = (unsigned int)((block_size + (__int64)io_size - 1) / (__int64)io_size) * parity_num;	// 全体の断片の個数
	//printf("io_size = %d, prog_base = %d\n", io_size, prog_base);

	block_left = block_size;
	if (galois_create_mult_tables() < 0){
		printf("galois_create_mult_tables\n");
		return 1;
	}

	// 作業バッファーを確保する
	block = malloc(unit_size * (source_num + 1));
	if (block == NULL){
		printf("malloc, %d\n", unit_size * (source_num + 1));
		err = 1;
		goto error_end;
	}
	buffer = block + unit_size;

	// パリティ計算用の行列演算の準備をする
	mat = make_encode_matrix(parity_num, source_num, first_num); // 変換行列
	if (mat == NULL){
		err = 1;
		goto error_end;
	}
	//galois_print_matrix(mat, parity_num, source_num);

	// chunk がキャッシュに収まるようにすれば速くなる！ (ストリップマイニングという最適化手法)
	// CPU L2キャッシュ・サイズが 256KB として、1/4 なら 64KB
	// chunk サイズが 2**16 なので分割数は 1～65536 の範囲になる
	chunk_count = (unit_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
	//printf("split count = %d, chunk size = %d\n", chunk_count, CHUNK_SIZE);

//time1 = GetTickCount();
	// バッファー・サイズごとにパリティ・ブロックを作成する
	time_last = GetTickCount();
	while (block_left > 0){
		// バッファーにソース・ファイルの内容を読み込む
		for (i = 0; i < source_num; i++){
			if (files[i].size > 0){
				if (files[i].size < io_size){
					len = (unsigned int)(files[i].size);
				} else {
					len = io_size;
				}
				files[i].size -= len;
				if (!ReadFile(files[i].hFile, buffer + (unit_size * i), len, &rv, NULL) || (len != rv)){
					print_win32_err();
					printf("ReadFile, data file %d\n", i);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buffer + (unit_size * i + len), 0, io_size - len);
				// ソース・ブロックのチェックサムを計算する
				checksum4(buffer + (unit_size * i), buffer + (unit_size * i + io_size), io_size);
			} else {
				// ソース・ブロックの値が全て 0 なら、チェックサムも 0 になる。
				memset(buffer + (unit_size * i), 0, unit_size);
			}
		}
		// バッファーに読み込んだサイズ
		if (block_left < io_size){
			len = (unsigned int)block_left;
		} else {
			len = io_size;
		}
		block_left -= len;

		// パリティ・ブロックごとに
		for (i = 0; i < parity_num; i++){
			/*	without strip mining
			memset(block, 0, unit_size);	// パリティ・ブロックを 0で埋める
			// ソース・ブロックごとにパリティを追加していく
			for (j = 0; j < source_num; j++){
				factor = mat[i * source_num + j];
				if (factor == 1){
					galois_region_xor(buffer + (unit_size * j), block, unit_size);
				} else if (factor != 0){
					galois_region_multiply(buffer + (unit_size * j), block, unit_size, factor);
				}
			}
			*/
			length = CHUNK_SIZE;
			for (k = 0; k < chunk_count; k++){	// chunk の番号
				offset = k * CHUNK_SIZE;
				if (offset + length >= unit_size){	// 最後の chunk なら
					length = unit_size - offset;
					k = chunk_count;
				}

				memset(block + offset, 0, length);	// パリティ・ブロックを 0で埋める
				// ソース・ブロックごとにパリティを追加していく
				for (j = 0; j < source_num; j++){
					factor = mat[i * source_num + j];
					if (factor == 1){
						galois_region_xor(buffer + (unit_size * j + offset), block + offset, length);
					} else if (factor != 0){
						galois_region_multiply(buffer + (unit_size * j + offset), block + offset, length, factor);
					}
				}
			}

			// 経過表示
			prog_num++;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((prog_num * 1000) / prog_base)){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			// パリティ・ブロックのチェックサムを検証する
			checksum4(block, (unsigned char *)&rv, io_size);
			if (memcmp(block + io_size, &rv, HASH_SIZE) != 0){
				printf("checksum mismatch, parity volume %d\n", i);
				err = 1;
				goto error_end;
			}
			Phmd5Process(&(par_md5[i]), block, len); // ハッシュ値を計算する
			// リカバリ・ファイルへ書き込む
			if (!WriteFile(par_hFile[i], block, len, &rv, NULL)){
				print_win32_err();
				printf("WriteFile, parity volume %d\n", first_num + 1 + i);
				err = 1;
				goto error_end;
			}
		}
	}
	print_progress_done();	// 改行して行の先頭に戻しておく
//time1 = GetTickCount() - time1;
//printf("encode %u.%03u sec\n", time1 / 1000, time1 % 1000);

error_end:
	if (block)
		free(block);
	if (mat)
		free(mat);
	// gflib のテーブルを解放する
	galois_free_tables();
	return err;
}

// リード・ソロモン符号を使ってデコードする
int rs_decode(
	int source_num,		// 本来のソース・ブロックの数
	int block_lost,		// 失われたソース・ブロックの数
	__int64 block_size,	// ブロック・サイズ (最大ファイル・サイズ)
	int parity_max,		// 本来のパリティ・ブロックの数
	int *exist,			// そのブロックが存在するか
	file_ctx *files)
{
	unsigned char *buffer, *block = NULL;
	int err = 0, i, j;
	int *mat = NULL, *id = NULL, factor;
	int block_recover;
	unsigned int io_size, unit_size, len, rv, len2;
	unsigned int k, offset, length, chunk_count;
	unsigned int time_last, prog_num = 0, prog_base;
	__int64 block_left;

	// 利用できるメモリー量を調べる
	io_size = get_mem_size(block_size * (__int64)(source_num + block_lost));
	io_size -= (block_lost * source_num * 8) + (parity_max * source_num * 4) + (source_num * 4);
	io_size -= 1048576; // 行列に必要なメモリーとスタック用に 1MB
	if (io_size < 1048576)
		io_size = 1048576;
	io_size /= source_num + 1;	// 何個分必要か

	// ブロック・サイズより大きい、またはブロック・サイズ自体が小さい場合は
	if (((__int64)io_size >= block_size) || (block_size <= 4096)){
		io_size = (unsigned int)block_size;	// ブロック・サイズと同じにする
	} else {	// ブロック・サイズを 2の乗数サイズの断片に分割する
		// 断片化する場合でもブロック数が多いと 256 * 4096 = 1MB は使う
		__int64 fragment_size = 4096;	// 最低サイズ
		while (fragment_size * 2 <= (__int64)io_size)
			fragment_size *= 2;
		io_size = (unsigned int)fragment_size;
	}
	io_size = (io_size + 3) & 0xFFFFFFFC;	// 4の倍数にする
	unit_size = io_size + HASH_SIZE;		// チェックサムの分だけ増やす
	prog_base = (unsigned int)((block_size + (__int64)io_size - 1) / (__int64)io_size) * block_lost;	// 全体の断片の個数
	//printf("io_size = %d\n", io_size);

	block_left = block_size;
	if (galois_create_mult_tables() < 0){
		printf("galois_create_mult_tables\n");
		return 1;
	}

	// 作業バッファーを確保する
	block = malloc(unit_size * (source_num + 1));
	if (block == NULL){
		printf("malloc, %d\n", unit_size * (source_num + 1));
		err = 1;
		goto error_end;
	}
	buffer = block + unit_size;

	// パリティ計算用の行列演算の準備をする
	id = malloc(sizeof(int) * source_num);
	if (id == NULL){
		printf("malloc, %d\n", sizeof(int) * source_num);
		err = 1;
		goto error_end;
	}

	mat = make_decode_matrix(parity_max, block_lost, source_num, exist, id);
	if (mat == NULL){
		err = 1;
		goto error_end;
	}
	//galois_print_matrix(mat, block_lost, source_num);
	//for (i = 0; i < source_num; i++)
	//	printf("id[%d] = %d\n", i, id[i]);

	// chunk サイズが 2**16 なので分割数は 1～65536 の範囲になる
	chunk_count = (unit_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

	// バッファー・サイズごとにソース・ブロックを復元する
	time_last = GetTickCount();
	while (block_left > 0){
		// バッファーにソース・ファイルとリカバリ・ファイルの内容を読み込む
		for (i = 0; i < source_num; i++){
			//printf("%d: id = %d, exist = %d, size = %I64u \n", i, id[i], exist[id[i]], files[id[i]].size);
			if (files[id[i]].size > 0){
				if (files[id[i]].size < io_size){
					len = (unsigned int)(files[id[i]].size);
				} else {
					len = io_size;
				}
				files[id[i]].size -= len;
				if (!ReadFile(files[id[i]].hFile, buffer + (unit_size * i), len, &rv, NULL)){
					print_win32_err();
					printf("ReadFile, data file %d\n", id[i]);
					err = 1;
					goto error_end;
				} else if (len != rv){
					printf("ReadFile, data file %d, %d, %d\n", id[i], len, rv);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buffer + (unit_size * i + len), 0, io_size - len);
				// ソース・ブロックのチェックサムを計算する
				checksum4(buffer + (unit_size * i), buffer + (unit_size * i + io_size), io_size);
			} else {
				// ソース・ブロックの値が全て 0 なら、チェックサムも 0 になる。
				memset(buffer + (unit_size * i), 0, unit_size);
			}
		}
		// バッファーに読み込んだサイズ
		if (block_left < io_size){
			len = (unsigned int)block_left;
		} else {
			len = io_size;
		}
		block_left -= len;

		// 失われたソース・ブロックごとに
		block_recover = 0;
		for (i = 0; i < source_num; i++){
			if (id[i] >= source_num){ // パリティ・ブロックで補った部分
				/*	without strip mining
				memset(block, 0, unit_size);	// ソース・ブロックを 0で埋める
				// 失われたソース・ブロックを復元していく
				for (j = 0; j < source_num; j++) {
					factor = mat[source_num * block_recover + j];
					if (factor == 1){
						galois_region_xor(buffer + (unit_size * j), block, unit_size);
					} else if (factor != 0){
						galois_region_multiply(buffer + (unit_size * j), block, unit_size, factor);
					}
				}
				*/
				length = CHUNK_SIZE;
				for (k = 0; k < chunk_count; k++){	// chunk の番号
					offset = k * CHUNK_SIZE;
					if (offset + length >= unit_size){	// 最後の chunk なら
						length = unit_size - offset;
						k = chunk_count;
					}

					memset(block + offset, 0, length);	// ソース・ブロックを 0で埋める
					// 失われたソース・ブロックを復元していく
					for (j = 0; j < source_num; j++) {
						factor = mat[source_num * block_recover + j];
						if (factor == 1){
							galois_region_xor(buffer + (unit_size * j + offset), block + offset, length);
						} else if (factor != 0){
							galois_region_multiply(buffer + (unit_size * j + offset), block + offset, length, factor);
						}
					}
				}

				// 経過表示
				prog_num++;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress((prog_num * 1000) / prog_base)){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}

				// 復元されたソース・ブロックのチェックサムを検証する
				checksum4(block, (unsigned char *)&rv, io_size);
				if (memcmp(block + io_size, &rv, HASH_SIZE) != 0){
					printf("checksum mismatch, recovered data file %d\n", i);
					err = 1;
					goto error_end;
				}
				block_recover++;
				// ソース・ファイルに書き込むサイズ
				if (files[i].size < len){
					len2 = (unsigned int)(files[i].size);
				} else {
					len2 = len;
				}
				files[i].size -= len2;
				// ソース・ファイルへ書き込む
				if (!WriteFile(files[i].hFile, block, len2, &rv, NULL)){
					print_win32_err();
					printf("WriteFile, data file %d\n", i);
					err = 1;
					goto error_end;
				}
			}
		}
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

error_end:
	if (block)
		free(block);
	if (id)
		free(id);
	if (mat)
		free(mat);
	// gflib のテーブルを解放する
	galois_free_tables();
	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// PAR 1.0 のヘッダーを作成する
int set_header(
	unsigned char *buf,
	file_ctx *files,	// ソース・ブロックのハンドルとサイズ
	int source_num,		// ソース・ブロックの数
	int file_num,		// ソース・ファイルの数
	wchar_t *list_buf,	// ソース・ファイルのリスト
	int list_len)		// ファイル・リストの文字数
{
	unsigned char hash[16], hash16[16], set_hash[16 * 256];
	unsigned char hash0[16] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
							0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
	wchar_t file_path[MAX_LEN], *file_name;
	int i, off, list_off, len, num = 0;
	__int64 file_size, total_file_size, prog_now;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

	memset(buf, 0, 96);
	// Identification String
	buf[0] = 'P';
	buf[1] = 'A';
	buf[2] = 'R';
	// Version Number
	i = 0x00010000; // PAR 1.0
	memcpy(buf + 8, &i, 4);
	// "program"-"version"-"subversion"-"subsubversion".
	buf[12] = PRODUCT_VERSION & 0xF;		// subsubversion
	buf[13] = (PRODUCT_VERSION >> 4) & 0xF;	// subversion
	buf[14] = PRODUCT_VERSION >> 8;			// version, ちなみに par-cmdline の最終バージョンは 0.9.0 ?
	buf[15] = 2;	// program, 1=Mirror, 2=PAR, 3=SmartPar, 0&4～=Unknown client
	// control hash の 16バイトは最後に書き込む
	// set hash の 16バイトは後で書き込む
	// volume number はパリティ・ブロックごとに変更する
	// number of files
	memcpy(buf + 56, &file_num, 4);
	// start offset of the file list
	buf[64] = 0x60;
	// file list size の 8バイトは後で書き込む
	// start offset of the data の 8バイトは後で書き込む
	// data size はパリティ・ブロックならブロック・サイズを書き込む

	// 合計ファイル・サイズを求める
	total_file_size = 0;
	prog_now = 0;
	list_off = 0;
	while (list_off < list_len){
		// ファイル名を取得する
		file_name = list_buf + list_off;	// サブ・ディレクトリを含まないのでそのまま使う
		list_off += wcslen(file_name) + 1;

		wcscpy(file_path, base_dir);
		wcscat(file_path, file_name);
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			printf_cp("cannot open input file, %s\n", file_name);
			return 0;
		}
		file_size = ((__int64)(AttrData.nFileSizeHigh) << 32) | (__int64)(AttrData.nFileSizeLow);
		total_file_size += file_size;
	}

	// ここから file list
	off = 0x0060;
	list_off = 0;
	while (list_off < list_len){
		// ファイル名を取得する
		file_name = list_buf + list_off;	// サブ・ディレクトリを含まないのでそのまま使う
		len = wcslen(file_name);
		list_off += (len + 1);
		len *= 2;	// 文字数からバイト数にする

		// entry size
		i = 0x38 + len;
		memcpy(buf + off, &i, 4);
		memset(buf + off + 4, 0, 4);
		off += 8;
		// status field
		wcscpy(file_path, base_dir);
		wcscat(file_path, file_name);
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			printf_cp("cannot open input file, %s\n", file_name);
			return 0;
		}
		file_size = ((__int64)(AttrData.nFileSizeHigh) << 32) | (__int64)(AttrData.nFileSizeLow);
		if (file_size > 0){
			i = 1;
		} else {
			i = 0; // 空のファイルはパリティ計算に含めない
		}
		memcpy(buf + off, &i, 4);
		memset(buf + off + 4, 0, 4);
		off += 8;
		// size
		memcpy(buf + off, &file_size, 8);
		off += 8;
		// MD5 hash
		if (file_size > 0){
			// パリティ・ブロック計算用に開く
			files[num].size = file_size;
			files[num].hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (files[num].hFile == INVALID_HANDLE_VALUE){
				print_win32_err();
				printf_cp("cannot open input file, %s\n", file_name);
				return 0;
			}
			// MD5 を計算する
			i = file_md5_total(files[num].hFile, file_size, hash, hash16, total_file_size, &prog_now);
			if (i != 0){
				if (i < 0)
					return i;
				printf_cp("file_md5_total, %s\n", file_name);
				return 0;
			}
		} else {
			memcpy(hash, hash0, 16);
		}
		memcpy(buf + off, hash, 16);
		off += 16;
		if (file_size > 0)
			memcpy(set_hash + (num * 16), hash, 16);
		// 16k MD5 hash
		if (file_size > 16384)
			memcpy(hash, hash16, 16);
		memcpy(buf + off, hash, 16);
		off += 16;
		// filename
		memcpy(buf + off, file_name, len);
		off += len;

		if (file_size > 0)
			num++;
	}

	// set hash をここで書き込む
	data_md5(set_hash, source_num * 16, hash);
	memcpy(buf + 32, hash, 16);
	// file list size をここで書き込む
	i = off - 96;
	memcpy(buf + 72, &i, 4);
	// start offset of the data をここで書き込む
	memcpy(buf + 80, &off, 4);

	// 最後にファイルのハッシュ値を計算する
	data_md5(buf + 32, off - 32, hash);
	memcpy(buf + 16, hash, 16);

	return off;
}

// インデックス・ファイルを作り直す
int recreate_par(
	wchar_t *file_path,		// PAR ファイルのパス
	unsigned char *pxx_buf,	// PXX ファイルのヘッダー
	unsigned char *buf)		// 作業バッファー
{
	unsigned char hash[16];
	int data_off, rv;
	HANDLE hFileWrite;

	// start offset of the data を読み取る
	memcpy(&data_off, pxx_buf + 80, 4);
	memcpy(buf, pxx_buf, data_off); // ヘッダー部分をコピーする

	// volume number を 0にする
	memset(buf + 48, 0, 8);
	// data size を 0にする
	memset(buf + 88, 0, 8);
	// control hash を計算しなおす
	data_md5(buf + 32, data_off - 32, hash);
	memcpy(buf + 16, hash, 16);

	// ファイルに書き込む
	hFileWrite = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFileWrite == INVALID_HANDLE_VALUE)
		return 1;
	if (!WriteFile(hFileWrite, buf, data_off, &rv, NULL)){
		CloseHandle(hFileWrite);
		return 1;
	}
	CloseHandle(hFileWrite);
	return 0;
}

// PAR 1.0 のヘッダーから値を読み取る
int get_header(
	wchar_t *file_name,
	HANDLE hFile,
	unsigned char *buf,
	int buf_size,
	unsigned char *set_hash,	// NULL でなければ set hash、同じ構成の PXX かどうかを識別するため
	int *volume_num,			// リカバリ・ファイルの番号
	int *file_num,				// ソース・ファイルの数
	int *list_off,				// ソース・ファイルのリストの開始位置
	int *list_size,				// ソース・ファイルのリストのサイズ
	int *data_off,				// ソースの開始位置
	__int64 *data_size)			// ソースのサイズ
{
	unsigned char hash[16];
	int i;
	unsigned int rv, meta_data[7];

	if (buf_size <= 96)
		return 1;

	// ファイルの先頭に戻す
	if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER){
		print_win32_err();
		return 1;
	}

	// ヘッダー部分を読み込む
	if (!ReadFile(hFile, buf, buf_size, &rv, NULL)){
		print_win32_err();
		printf("ReadFile, header\n");
		return 1;
	}

	// Identification String
	memcpy(&i, buf, 4);
	if (i != 5390672) // 'P' + 'A' * 256 + 'R' * 65536
		return 1;
	memcpy(&i, buf + 4, 4);
	if (i != 0)
		return 1;
	// Version Number
	memcpy(&i, buf + 8, 4);
	if ((i & 0xffff0000) != 0x00010000) // PAR v1.** にだけ対応する
		return 1;

	// 完全なリカバリ・ファイルを見つけた後だけ、検査結果を探す
	if (list_size == NULL){
		i = check_ini_state(0x10FF, meta_data, hFile);
		if (i == -2){	// 記録が無い、または状態が変化してる
			// control hash
			if (rv = file_md5_from32(file_name, hFile, hash))
				return rv;
			i = 0;
			if (memcmp(hash, buf + 16, 16) != 0)
				i = 1;
			// 今回の検査結果を書き込む
			write_ini_state(0x10FF, meta_data, i);
		}
		if (i != 0)
			return 1;

	} else {
		// control hash
		if (rv = file_md5_from32(file_name, hFile, hash))
			return rv;
		if (memcmp(hash, buf + 16, 16) != 0)
			return 1;
	}

	// set hash
	if (set_hash != NULL)
		memcpy(set_hash, buf + 32, 16);
	// volume number
	if (volume_num != NULL)
		memcpy(volume_num, buf + 48, 4);
	// number of files
	if (file_num != NULL)
		memcpy(file_num, buf + 56, 4);
	// start offset of the file list
	if (list_off != NULL)
		memcpy(list_off, buf + 64, 4);
	// file list size
	if (list_size != NULL)
		memcpy(list_size, buf + 72, 4);
	// start offset of the data
	if (data_off != NULL)
		memcpy(data_off, buf + 80, 4);
	// data size
	if (data_size != NULL)
		memcpy(data_size, buf + 88, 8);

	return 0;
}

// ファイル・リストからブロック数を読み取る
int get_source_num(
	unsigned char *buf,
	__int64 *block_size)
{
	int i, off, file_num, entry_size, status, source_num = 0;
	__int64 file_size;

	// number of files
	memcpy(&file_num, buf + 56, 4);
	// start offset of the file list
	memcpy(&off, buf + 64, 4);

	for (i = 0; i < file_num; i++){
		// entry size
		memcpy(&entry_size, buf + off, 4);
		// status field
		memcpy(&status, buf + (off + 8), 4);
		if ((status & 1) == 1){
			source_num++;
			// file size
			memcpy(&file_size, buf + (off + 16), 8);
			if (file_size > *block_size)
				*block_size = file_size;
		}

		off += entry_size;
	}

	printf("%3d \r", source_num);
	return source_num;
}

// コメントを読み取る
static void read_comment(
	wchar_t *par_comment,
	unsigned char *buf,
	int len)
{
	if (par_comment[0] != 0)
		return;

	if (len >= COMMENT_LEN * 2)
		len = (COMMENT_LEN - 1) * 2;
	memcpy(par_comment, buf, len);
	len /= 2;	// 文字数にする
	par_comment[len] = 0;	// 末尾を null 文字にする
	for (len = 0; len < COMMENT_LEN; len++){	// 表示する前に sanitalize する
		if (par_comment[len] == 0){
			break;
		} else if ((par_comment[len] <= 31) || (par_comment[len] == 127)){	// 制御文字を消す
			par_comment[len] = ' ';
		}
	}
}

// リカバリ・ファイルを作成する
int par1_create(
	int switch_p,			// インデックス・ファイルを作らない
	int source_num,			// ソース・ブロックの数
	__int64 block_size,		// ブロック・サイズ (最大ファイル・サイズ)
	int parity_num,			// パリティ・ブロックの数
	int first_vol,			// 最初のリカバリ・ファイル番号
	int file_num,			// ソース・ファイルの数
	wchar_t *list_buf,		// ソース・ファイルのリスト
	int list_len,			// ファイル・リストの文字数
	wchar_t *par_comment)	// コメント
{
	unsigned char *header_buf = NULL;
	wchar_t recovery_base[MAX_LEN];
	int err = 0, i;
	int int4, buf_size;
	unsigned int rv;
	HANDLE *par_hFile = NULL;
	file_ctx *files = NULL;
	PHMD5 *par_md5 = NULL;

	buf_size = 0x0060 + (0x0038 * file_num) + (list_len * 2); // 最大サイズ分確保しておく
	header_buf = malloc(buf_size);
	if (header_buf == NULL){
		printf("malloc, %d\n", buf_size);
		err = 1;
		goto error_end;
	}

	files = malloc(sizeof(file_ctx) * source_num);
	if (files == NULL){
		printf("malloc, %d\n", sizeof(file_ctx) * source_num);
		err = 1;
		goto error_end;
	}
	for (i = 0; i < source_num; i++)
		files[i].hFile = NULL;

	int4 = sizeof(HANDLE) * parity_num;
	if (int4 == 0)
		int4 = sizeof(HANDLE);
	par_hFile = malloc(int4);
	if (par_hFile == NULL){
		printf("malloc, %d\n", int4);
		err = 1;
		goto error_end;
	}
	par_hFile[0] = NULL;
	for (i = 1; i < parity_num; i++)
		par_hFile[i] = NULL;

	if (parity_num > 0){
		par_md5 = malloc(sizeof(PHMD5) * parity_num);
		if (par_md5 == NULL){
			printf("malloc, %d\n", sizeof(PHMD5) * parity_num);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++)
			Phmd5Begin(&(par_md5[i]));
	}

	printf("\n");
	print_progress_text(0, "Computing file hash");

	// ヘッダーを作成する
	buf_size = set_header(header_buf, files, source_num, file_num, list_buf, list_len);
	if (buf_size <= 0){
		if (buf_size < 0){
			err = 2;
		} else {
			err = 1;
		}
		goto error_end;
	}

	if (switch_p == 0){	// インデックス・ファイルを作るなら
		// コメントがあるならヘッダーを修正する
		if (int4 = wcslen(par_comment) * 2)
			memcpy(header_buf + 88, &int4, 4);

		// リカバリ・ファイル (*.PAR) を書き込む
		par_hFile[0] = CreateFile(recovery_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (par_hFile[0] == INVALID_HANDLE_VALUE){
			print_win32_err();
			printf_cp("cannot create file, %s\n", recovery_file);
			par_hFile[0] = NULL;
			err = 1;
			goto error_end;
		}
		if (!WriteFile(par_hFile[0], header_buf, buf_size, &rv, NULL)){
			print_win32_err();
			printf("WriteFile, index file\n");
			err = 1;
			goto error_end;
		}
		// コメントがあるならコメントも書き込む
		if (int4 = wcslen(par_comment) * 2){
			if (!WriteFile(par_hFile[0], par_comment, int4, &int4, NULL)){
				print_win32_err();
				printf("WriteFile, index file\n");
				err = 1;
				goto error_end;
			}
			// MD5を計算しなおす
			if (err = file_md5_from32(NULL, par_hFile[0], header_buf + 16)){
				if (err == 1)
					printf("file_md5_from32\n");
				goto error_end;
			}
			if (SetFilePointer(par_hFile[0], 16, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER){
				print_win32_err();
				err = 1;
				goto error_end;
			}
			if (!WriteFile(par_hFile[0], header_buf + 16, 16, &rv, NULL)){
				print_win32_err();
				printf("WriteFile, index file\n");
				err = 1;
				goto error_end;
			}
		}
		CloseHandle(par_hFile[0]);
		par_hFile[0] = NULL;
	}
	print_progress_done();
	if (parity_num == 0) // パリティ・ブロックを作らない場合はここで終わる
		goto creation_end;

	// ソース・ファイルから読み込む準備をする
	for (i = 0; i < source_num; i++){
		if (SetFilePointer(files[i].hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER){
			print_win32_err();
			err = 1;
			goto error_end;
		}
	}

	// リカバリ・ファイル (*.PXX) の名前の基
	wcscpy(recovery_base, recovery_file);
	int4 = wcslen(recovery_base);
	recovery_base[int4 - 1] = 0;
	recovery_base[int4 - 2] = 0;
	print_progress_text(0, "Creating parity volume");

	// リカバリ・ファイル (*.PXX) に書き込む準備をする
	memcpy(header_buf + 88, &block_size, 8); // ブロック・サイズにする
	for (i = 0; i < parity_num; i++){
		int4 = first_vol + i;	// 作成する parity volume の番号にする
		swprintf(recovery_file, MAX_LEN, L"%s%02d", recovery_base, int4);
		par_hFile[i] = CreateFile(recovery_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (par_hFile[i] == INVALID_HANDLE_VALUE){
			print_win32_err();
			par_hFile[i] = NULL;
			printf_cp("cannot create file, %s\n", recovery_file);

			// これまでに作ったリカバリ・ファイルを削除する
			if (switch_p == 0){
				wcscpy(recovery_file, recovery_base);
				wcscat(recovery_file, L"ar");
				DeleteFile(recovery_file);
			}
			for (int4 = 0; int4 < i; int4++){
				CloseHandle(par_hFile[int4]);
				par_hFile[int4] = NULL;
				swprintf(recovery_file, MAX_LEN, L"%s%02d", recovery_base, first_vol + int4);
				DeleteFile(recovery_file); // 途中までのリカバリ・ファイルを削除する
			}

			err = 1;
			goto error_end;
		}

		// ヘッダーを書き込む
		memcpy(header_buf + 48, &int4, 2); // volume number にする
		if (!WriteFile(par_hFile[i], header_buf, buf_size, &rv, NULL)){
			print_win32_err();
			printf("WriteFile, parity volume %d\n", int4);
			err = 1;
			goto error_end;
		}
		// ヘッダー部分からのハッシュ値を計算しかけておく
		Phmd5Process(&(par_md5[i]), header_buf + 32, buf_size - 32);
	}

	// パリティ・ブロックを作成する
	if (err = rs_encode(source_num, block_size, parity_num, first_vol - 1, files, par_hFile, par_md5)){
		if (err == 2){ // キャンセルされたのなら、作業中のファイルを削除する
			if (switch_p == 0){
				wcscpy(recovery_file, recovery_base);
				wcscat(recovery_file, L"ar");
				DeleteFile(recovery_file);
			}
			for (i = 0; i < parity_num; i++){
				CloseHandle(par_hFile[i]);
				par_hFile[i] = NULL;
				swprintf(recovery_file, MAX_LEN, L"%s%02d", recovery_base, first_vol + i);
				DeleteFile(recovery_file); // 途中までのリカバリ・ファイルを削除する
			}
		}
		goto error_end;
	}

	// リカバリ・ファイル (*.PXX) の MD5を計算しなおす
	for (i = 0; i < parity_num; i++){
		Phmd5End(&(par_md5[i])); // ハッシュ値の計算終了
		memcpy(header_buf + 16, par_md5[i].hash, 16);
		if (SetFilePointer(par_hFile[i], 16, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER){
			print_win32_err();
			err = 1;
			goto error_end;
		}
		if (!WriteFile(par_hFile[i], header_buf + 16, 16, &rv, NULL)){
			print_win32_err();
			printf("WriteFile, parity volume %d\n", i);
			err = 1;
			goto error_end;
		}
	}

creation_end:
	printf("\nCreated successfully\n");
error_end:
	if (list_buf)
		free(list_buf);
	if (header_buf)
		free(header_buf);
	if (par_md5)
		free(par_md5);
	if (files){
		for (i = 0; i < source_num; i++){
			if (files[i].hFile != NULL)
				CloseHandle(files[i].hFile);
		}
		free(files);
	}
	if (par_hFile){
		for (i = 0; i < parity_num; i++){
			if (par_hFile[i] != NULL)
				CloseHandle(par_hFile[i]);
		}
		free(par_hFile);
	}
	return err;
}

// ソース・ファイルの破損や欠損を調べる
int par1_verify(
	int switch_b,			// 既存のファイルを別名にしてどかす
	int switch_p,			// インデックス・ファイルを作り直す
	wchar_t *par_comment)	// コメント
{
	char ascii_buf[MAX_LEN * 3], ascii_buf2[MAX_LEN * 3];
	unsigned char *header_buf = NULL, *header_buf2 = NULL, hash[16], set_hash[16];
	wchar_t file_name[MAX_LEN], file_path[MAX_LEN], find_path[MAX_LEN];
	wchar_t par_char[] = L"AR";
	int err = 0, i, j, bad_flag, find_flag, parity_flag, par_flag, exist[99];
	int len, dir_len, base_len, buf_size, entry_size, entry_size2, blk;
	int volume_num, volume_max, file_num, list_off, list_size, data_off;
	int buf_size2, volume_num2, file_num2, list_off2, data_off2;
	int parity_num, source_num, block_lost = 0, need_repair = 0, recovery_lost = 0;
	unsigned int rv, meta_data[7];
	__int64 file_size, file_size2, data_size, block_size = 0, total_file_size;
	HANDLE hFile = NULL, hFind;
	WIN32_FIND_DATA FindData;

	// リカバリ・ファイルの名前
	wcscpy(file_path, recovery_file);
	len = wcslen(file_path);
	file_path[len - 2] = 0; // 末尾の2文字を消去する
	if (file_path[len - 3] == 'p'){
		par_char[0] = 'a';
		par_char[1] = 'r';
	}
	// ディレクトリの長さ
	dir_len = len - 3;
	while (dir_len >= 0){
		if ((file_path[dir_len] == '\\') || (file_path[dir_len] == '/'))
			break;
		dir_len--;
	}
	dir_len++;
	base_len = wcslen(base_dir);

	// リカバリ・ファイルの一覧を表示する
	printf("PAR File list :\n");
	printf("         Size :  Filename\n");
	total_file_size = 0;
	parity_num = 0;
	volume_max = 0;
	for (i = 0; i <= 99; i++){
		if (i == 0){
			swprintf(find_path, MAX_LEN, L"%s%s", file_path, par_char);
			blk = 0;
		} else {
			swprintf(find_path, MAX_LEN, L"%s%02d", file_path, i);
			blk = 1;
		}
		// ファイルを開くことができるか、サイズを取得できるかを確かめる
		hFile = CreateFile(find_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE){
			continue;
		}
		// ファイルのサイズを取得する
		if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
			CloseHandle(hFile);
			continue;
		}
		CloseHandle(hFile);
		total_file_size += file_size;
		parity_num++;
		volume_max = i;
		// リカバリ・ファイルの名前
		utf16_to_cp(find_path + dir_len, ascii_buf);
		printf("%13I64d : \"%s\"\n", file_size, ascii_buf);
	}
	hFile = NULL;
	printf("\nPAR File total size\t: %I64d\n", total_file_size);
	printf("PAR File possible count\t: %d\n\n", parity_num);

	// 指定されたリカバリ・ファイルを開く
	hFile = CreateFile(recovery_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		printf("valid file is not found\n");
		err = 1;
		goto error_end;
	}
	// ファイルのサイズを取得する
	if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
		print_win32_err();
		err = 1;
		goto error_end;
	}

	// ヘッダー用のバッファーを確保する
	buf_size = 0x0060 + ((0x0038 + (260 * 2)) * 256) + (COMMENT_LEN * 2); // 最大サイズ
	if ((__int64)buf_size > file_size)
		buf_size = (unsigned int)file_size;
	header_buf = malloc(buf_size);
	if (header_buf == NULL){
		printf("malloc, %d\n", buf_size);
		err = 1;
		goto error_end;
	}

	// PAR や PXX ファイルとして正しいかを調べる
	par_flag = 0;
	par_comment[0] = 0;
	for (i = 0; i < 99; i++)
		exist[i] = -1;
	if (rv = get_header(NULL, hFile, header_buf, buf_size, set_hash,
			&volume_num, &file_num, &list_off, &list_size, &data_off, &data_size)){
		if (rv == 2){	// キャンセル
			err = 2;
			goto error_end;
		}
		volume_num = -1;
		par_flag |= 2;
	} else {
		// 各ブロックが存在するかを記録していく
		j = get_source_num(header_buf, &block_size);
		check_ini_file(set_hash, j);	// 前回の検査結果が存在するかどうか
		if (volume_num == 0){
			par_flag |= 1;
			rv = (int)data_size;
			if (buf_size < (int)data_size)
				rv = buf_size;
			if (rv > 1)
				read_comment(par_comment, header_buf + data_off, rv);
		}
	}
	CloseHandle(hFile);
	hFile = NULL;

	// 他のリカバリ・ファイルのヘッダー用のバッファーを確保する
	if (volume_num == -1){
		buf_size2 = 0x0060 + ((0x0038 + (260 * 2)) * 256) + (COMMENT_LEN * 2); // 最大サイズ
	} else {
		buf_size2 = 0x0060 + ((0x0038 + (260 * 2)) * file_num);
	}
	header_buf2 = malloc(buf_size2);
	if (header_buf2 == NULL){
		printf("malloc, %d\n", buf_size2);
		err = 1;
		goto error_end;
	}

	// 他のリカバリ・ファイルも調べる
	printf("Loading PAR File:\n");
	printf(" Block Status   :  Filename\n");
	fflush(stdout);
	parity_num = 0;
	for (i = 0; i <= 99; i++){
		if (err = cancel_progress())	// キャンセル処理
			goto error_end;
		if (i == 0){
			swprintf(find_path, MAX_LEN, L"%s%s", file_path, par_char);
			blk = 0;
		} else {
			swprintf(find_path, MAX_LEN, L"%s%02d", file_path, i);
			blk = 1;
		}
		utf16_to_cp(find_path + dir_len, ascii_buf);

		// 既にチェック済みなら
		if ((_wcsicmp(recovery_file + dir_len, find_path + dir_len) == 0) && (volume_num != -1)){
			if ((volume_num > 0) && (volume_num <= 99)){
				if (exist[volume_num - 1] == -1)	// 係数が重複してるのは使えない
					parity_num++;
				exist[volume_num - 1] = i;
				if (volume_max < volume_num)
					volume_max = volume_num;
			}
			if ((par_flag & 2) != 0){
				printf(" 0 / %d Damaged  : \"%s\"\n", blk, ascii_buf);
				recovery_lost++;
			} else if (i == volume_num){
				printf(" %d / %d Good     : \"%s\"\n", blk, blk, ascii_buf);
			} else {
				blk = (volume_num > 0) ? 1 : 0;
				printf(" %d / %d Misnamed : \"%s\"\n", blk, blk, ascii_buf);
				recovery_lost++;	// リカバリ・ファイルの破損は修復必要とは判定しない
			}
			continue;
		}

		// ファイルを開く
		hFile = CreateFile(find_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			continue;
		// PAR や PXX ファイルとして正しいかを調べる
		bad_flag = 0;
		if (volume_num == -1){
			if (rv = get_header(find_path + dir_len, hFile, header_buf, buf_size, set_hash,
					&volume_num, &file_num, &list_off, &list_size, &data_off, &data_size)){
				if (rv == 2){	// キャンセル
					err = 2;
					goto error_end;
				}
				volume_num = -1;
				bad_flag = 1;
			} else {
				j = get_source_num(header_buf, &block_size);
				check_ini_file(set_hash, j);	// 前回の検査結果が存在するかどうか
				if (volume_num == 0){
					par_flag |= 1;
					rv = (int)data_size;
					if (buf_size < (int)data_size)
						rv = buf_size;
					if (rv > 1)
						read_comment(par_comment, header_buf + data_off, (int)data_size);
				} else if (volume_num <= 99){
					parity_num++;
					exist[volume_num - 1] = i;
					if (volume_max < volume_num)
						volume_max = volume_num;
				}
				if (i != volume_num){	// ブロック番号と拡張子が異なるなら
					bad_flag = 3;
					blk = (volume_num > 0) ? 1 : 0;
				}
			}
		} else {
			if (rv = get_header(find_path + dir_len, hFile, header_buf2, buf_size2, hash,
					&volume_num2, &file_num2, &list_off2, NULL, &data_off2, &data_size)){
				if (rv == 2){	// キャンセル
					err = 2;
					goto error_end;
				}
				bad_flag = 1;
			} else { // 同じ構成の PXX かを確認する
				if (memcmp(hash, set_hash, 16) != 0)
					bad_flag = 2;
				if ((file_num2 != file_num) || (list_off2 != list_off) || (data_off2 != data_off))
					bad_flag = 2;
				if ((block_size) && (volume_num2 > 0)){
					if (data_size != block_size)
						bad_flag = 2;
				}
				if (!bad_flag){
					if (volume_num2 == 0){
						par_flag |= 1;
						rv = (int)data_size;
						if (buf_size2 < (int)data_size)
							rv = buf_size2;
						if (rv > 1)
							read_comment(par_comment, header_buf2 + data_off2, rv);
					} else if (volume_num2 <= 99){
						if (exist[volume_num2 - 1] == -1)	// 係数が重複してるのは使えない
							parity_num++;
						exist[volume_num2 - 1] = i;
						if (volume_max < volume_num2)
							volume_max = volume_num2;
					}
					if (i != volume_num2){	// ブロック番号と拡張子が異なるなら
						bad_flag = 3;
						blk = (volume_num2 > 0) ? 1 : 0;
					}
				}
			}
		}
		switch (bad_flag){
		case 0:
			printf(" %d / %d Good     : \"%s\"\n", blk, blk, ascii_buf);
			break;
		case 1:
			printf(" 0 / %d Damaged  : \"%s\"\n", blk, ascii_buf);
			recovery_lost++;
			break;
		case 2:
			printf(" %d / %d Useless  : \"%s\"\n", blk, blk, ascii_buf);
			recovery_lost++;
			break;
		case 3:
			printf(" %d / %d Misnamed : \"%s\"\n", blk, blk, ascii_buf);
			recovery_lost++;	// リカバリ・ファイルの破損は修復必要とは判定しない
			break;
		}
		CloseHandle(hFile);
		hFile = NULL;

		fflush(stdout);
	}

	if (volume_num == -1){ // Recovery Set が見つからなければここで終わる
		printf("valid file is not found\n");
		err = 1;
		goto error_end;
	}
	// セット・ハッシュとプログラムのバージョンを表示する
	printf("\nSet Hash: ");
	print_hash(set_hash);
	memcpy(&rv, header_buf + 12, 4);
	printf("\nCreator : ");
	switch (rv >> 24){
	case 1:	// Mirror
		printf("Mirror");
		break;
	case 2:	// PAR
		printf("PAR");
		break;
	case 3:	// SmartPar
		printf("SmartPar");
		break;
	default:
		printf("Unknown client %d", rv >> 24);
	}
	printf(" version %d.%d.%d\n", (rv >> 16) & 0xFF, (rv >> 8) & 0xFF, rv & 0xFF);
	if ((par_comment[0] != 0) && (!utf16_to_cp(par_comment, ascii_buf)))
		printf("Comment : %s\n", ascii_buf); // コメントをユニコードから戻せた場合だけ表示する

	if (((par_flag & 1) == 0) && (switch_p)){ // PAR ファイルを作り直す
		swprintf(find_path, MAX_LEN, L"%s%s", file_path, par_char);
		get_temp_name(find_path, file_name);
		if (recreate_par(file_name, header_buf, header_buf2) == 0){
			if (replace_file(find_path, file_name, switch_b) == 0){	// 破損したPARファイルが存在するならどかす
				utf16_to_cp(find_path + dir_len, ascii_buf);
				printf("\nRestored file :\n");
				printf("         Size :  Filename\n");
				printf("%13u : \"%s\"\n", data_off, ascii_buf);
			} else {
				DeleteFile(file_name);
			}
		}
	}
	free(header_buf2);
	header_buf2 = NULL;
	printf("\nParity Volume count\t: %d\n", volume_max);
	printf("Parity Volume found\t: %d\n", parity_num);
	fflush(stdout);

	// ファイル・リストを表示する
	printf("\nInput File list : %d\n", file_num);
	printf("         Size B :  Filename\n");
	total_file_size = 0;
	source_num = 0;
	list_off2 = list_off;
	for (i = 0; i < file_num; i++){
		// entry size
		memcpy(&entry_size, header_buf + list_off2, 4);
		// status field
		memcpy(&parity_flag, header_buf + (list_off2 + 8), 2);
		parity_flag &= 0x01; // bit 0 = file is saved in the parity volume set
		if (parity_flag){
			source_num++;
			blk = 1;
		} else {
			blk = 0;
		}
		// size
		memcpy(&data_size, header_buf + (list_off2 + 16), 8);
		total_file_size += data_size;
		// filename
		len = (entry_size - 56) / 2;
		if (len >= MAX_LEN)
			len = MAX_LEN - 1;
		memcpy(file_name, header_buf + (list_off2 + 56), len * 2);
		file_name[len] = 0;
		utf16_to_cp(file_name, ascii_buf);
		// ファイル名が有効かどうか確かめる
		j = sanitize_filename(file_name);
		if (j != 0){
			if (j == 16){
				printf("filename is not valied, %s\n", ascii_buf);
				err = 1;
				goto error_end;
			}
			utf16_to_cp(file_name, ascii_buf);
		}
		printf("%13I64u %d : \"%s\"\n", data_size, blk, ascii_buf);
		list_off2 += entry_size; // 次のエントリー位置にずらす
	}
	printf("\nData File count : %d\n", source_num);
	printf("Max file size\t: %I64d\n", block_size);
	printf("Total data size : %I64d\n", total_file_size);

	// ソース・ファイルが存在していて正しいかを調べる
	printf("\nVerifying Input File   :\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	for (i = 0; i < file_num; i++){
		if (err = cancel_progress())	// キャンセル処理
			goto error_end;
		find_flag = bad_flag = parity_flag = 0;

		// entry size
		memcpy(&entry_size, header_buf + list_off, 4);
		// status field
		memcpy(&parity_flag, header_buf + (list_off + 8), 2);
		parity_flag &= 0x01; // bit 0 = file is saved in the parity volume set
		// size
		memcpy(&data_size, header_buf + (list_off + 16), 8);
		file_size = 0;
		// filename
		len = (entry_size - 56) / 2;
		if (len >= MAX_LEN)
			len = MAX_LEN - 1;
		memcpy(file_name, header_buf + (list_off + 56), len * 2);
		file_name[len] = 0;
		utf16_to_cp(file_name, ascii_buf);
		// ファイル名が有効かどうか確かめる
		wcscpy(file_path, file_name);
		j = sanitize_filename(file_path);
		if (j != 0){
			wcscpy(file_name, file_path);
			utf16_to_cp(file_name, ascii_buf);
		}
		// ファイル名を基準ディレクトリに追加してパスにする
		if (base_len + wcslen(file_name) >= MAX_LEN - ADD_LEN){
			printf("filename is too long\n");
			err = 1;
			goto error_end;
		}
		wcscpy(file_path, base_dir);
		wcscpy(file_path + base_len, file_name);

		// ファイルが存在するか
		hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE){ // ファイルが存在するなら内容を確認する
			// 検査結果の記録があるかどうか
			bad_flag = check_ini_state(i, meta_data, hFile);
			memcpy(&file_size, meta_data, 8);
			if (bad_flag == -1){	// ファイル・サイズが不明なら消失扱いにする
				bad_flag = 1;
			} else if (bad_flag == -2){	// 検査結果が無かった場合
				bad_flag = 0;
				if (file_size >= data_size){ // 末尾にゴミが付いてないか調べる
					if (file_size > data_size)
						bad_flag = 2;
					if (data_size > 0){ // 16k MD5 hash が一致するか確かめる
						if (file_md5_16k(hFile, data_size, hash)){
							bad_flag = 1;
						} else if (memcmp(hash, header_buf + (list_off + 40), 16) != 0){
							bad_flag = 1;
						} else if (data_size > 16384){ // MD5 hash が一致するか確かめる
							if (rv = file_md5(file_name, hFile, data_size, hash)){
								if (rv == 2){
									err = 2;
									goto error_end;
								}
								bad_flag = 1;
							} else if (memcmp(hash, header_buf + (list_off + 24), 16) != 0){
								bad_flag = 1;
							}
							// 16k MD5 hash が一致した時だけ検査結果を記録する
							write_ini_state(i, meta_data, bad_flag);
						}
					}
				} else if (file_size < data_size){
					bad_flag = 1;
				}
			}
			CloseHandle(hFile);
			hFile = NULL;
			if (!bad_flag)
				find_flag = 3;
		}

		// ファイルが存在しないか、不完全だった場合は、サイズとハッシュ値で検索する
		if ((data_size) && ((hFile == INVALID_HANDLE_VALUE) || (bad_flag))){
			wcscpy(find_path, base_dir);
			wcscpy(find_path + base_len, L"*");
			hFind = FindFirstFile(find_path, &FindData);
			if (hFind != INVALID_HANDLE_VALUE){
				do {
					if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0){ // フォルダは無視する
						find_flag = 1;
						file_size2 = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
						if (file_size2 != data_size){ // ファイル・サイズが一致しなければ
							find_flag = 0;
						} else { // ファイル名がリスト上にあれば除外する
							memcpy(&list_off2, header_buf + 64, 4);
							for (j = 0; j < file_num; j++){
								// entry size
								memcpy(&entry_size2, header_buf + list_off2, 4);
								// filename
								len = (entry_size2 - 56) / 2;
								if (len >= MAX_LEN)
									len = MAX_LEN - 1;
								memcpy(file_name, header_buf + (list_off2 + 56), len * 2);
								file_name[len] = 0;
								sanitize_filename(file_name);
								if (_wcsicmp(file_name, FindData.cFileName) == 0){
									find_flag = 0;
									break;
								}
								list_off2 += entry_size2; // 次のエントリー位置にずらす
							}
						}
						// リスト上に無くてファイル・サイズが一致すれば
						if ((find_flag != 0) && (base_len + wcslen(FindData.cFileName) < MAX_LEN)){
							wcscpy(find_path + base_len, FindData.cFileName);
							hFile = CreateFile(find_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
							if (hFile != INVALID_HANDLE_VALUE){
								j = check_ini_state(i, meta_data, hFile);
								if (j == -2){	// 検査結果が無かった場合
									if (data_size > 0){ // 16k MD5 hash が一致するか確かめる
										if (file_md5_16k(hFile, data_size, hash)){
											find_flag = 0;
										} else if (memcmp(hash, header_buf + (list_off + 40), 16) != 0){
											find_flag = 0;
										} else if (data_size > 16384){ // MD5 hash が一致するか確かめる
											j = 0;
											if (rv = file_md5(FindData.cFileName, hFile, data_size, hash)){
												if (rv == 2){
													err = 2;
													goto error_end;
												}
												find_flag = 0;
												j = 1;
											} else if (memcmp(hash, header_buf + (list_off + 24), 16) != 0){
												find_flag = 0;
												j = 1;
											}
											// 16k MD5 hash が一致した時だけ検査結果を記録する
											write_ini_state(i, meta_data, j);
										}
									}
								} else if (j != 0){	// 完全以外なら
									find_flag = 0;
								}
								CloseHandle(hFile);
								hFile = NULL;
								if (find_flag){ // 異なる名前だが同じ内容のファイルを発見した
									utf16_to_cp(FindData.cFileName, ascii_buf2);
									break;
								}
							} else {
								find_flag = 0;
							}
						}
					}
				} while (FindNextFile(hFind, &FindData)); // 次のファイルを検索する
				FindClose(hFind);
			}
		}
		if (find_flag == 3){
			printf("            = Complete : \"%s\"\n", ascii_buf);
		} else if (find_flag == 1){
			need_repair++;
			if (bad_flag){
				printf("%13I64u Damaged~ : \"%s\"\n", file_size, ascii_buf);
			} else {
				printf("            - Missing~ : \"%s\"\n", ascii_buf);
			}
			printf("%13I64u Misnamed : \"%s\"\n", data_size, ascii_buf2);
		} else if (bad_flag == 2){
			need_repair++;
			printf("%13I64u Appended : \"%s\"\n", file_size, ascii_buf);
		} else if (bad_flag){
			if (parity_flag)
				block_lost++;
			if (data_size == 0)
				need_repair++;
			printf("%13I64u Damaged  : \"%s\"\n", file_size, ascii_buf);
		} else {
			if (parity_flag)
				block_lost++;
			if (data_size == 0)
				need_repair++;
			printf("            - Missing  : \"%s\"\n", ascii_buf);
		}

		fflush(stdout);
		list_off += entry_size; // 次のエントリー位置にずらす
	}
	printf("\nData File lost\t: %d\n\n", block_lost);
	if ((block_lost == 0) && (need_repair == 0))
		printf("All Files Complete\n");
	if (recovery_lost > 0){	// 不完全なリカバリ・ファイルがあるなら
		err = 256;
		printf("%d PAR File(s) Incomplete\n", recovery_lost);
	}

	// 修復する必要があるかどうか
	if ((block_lost > 0) || (need_repair > 0)){
		err |= 4;
		if (need_repair > 0){
			printf("Ready to rename %d file(s)\n", need_repair);
			err |= 32;
		}
		if (block_lost > 0){
			if (block_lost > parity_num){
				printf("Need %d more volume(s) to repair %d file(s)\n", block_lost - parity_num, block_lost);
				err |= 8;
			} else {
				printf("Ready to repair %d file(s)\n", block_lost);
				err |= 128;
			}
		}
	}

error_end:
	close_ini_file();
	if (hFile != NULL)
		CloseHandle(hFile);
	if (header_buf)
		free(header_buf);
	if (header_buf2)
		free(header_buf2);
	return err;
}

// ソース・ファイルの破損や欠損を修復する
int par1_repair(
	int switch_b,			// 既存のファイルを別名にしてどかす
	int switch_p,			// インデックス・ファイルを作り直す
	wchar_t *par_comment)	// コメント
{
	char ascii_buf[MAX_LEN * 3], ascii_buf2[MAX_LEN * 3];
	unsigned char *header_buf = NULL, *header_buf2 = NULL, hash[16], set_hash[16];
	wchar_t file_name[MAX_LEN * 2], file_path[MAX_LEN], find_path[MAX_LEN * 2];
	wchar_t par_char[] = L"AR";
	int err = 0, i, j, bad_flag, find_flag, parity_flag, par_flag, exist[256];
	int len, dir_len, base_len, buf_size, entry_size, entry_size2, blk;
	int volume_num, volume_max, file_num, list_off, list_size, data_off;
	int buf_size2, volume_num2, file_num2, list_off2, data_off2;
	int parity_num, source_num, block_lost = 0, need_repair = 0, recovery_lost = 0;
	int parity_max, block_max;
	unsigned int rv, meta_data[7];
	__int64 file_size, file_size2, data_size, block_size = 0, total_file_size;
	HANDLE hFile = NULL, hFind;
	WIN32_FIND_DATA FindData;
	file_ctx *files = NULL, *tmp_p;

	// リカバリ・ファイルの名前
	wcscpy(file_path, recovery_file);
	len = wcslen(file_path);
	file_path[len - 2] = 0; // 末尾の2文字を消去する
	if (file_path[len - 3] == 'p'){
		par_char[0] = 'a';
		par_char[1] = 'r';
	}
	// ディレクトリの長さ
	dir_len = len - 3;
	while (dir_len >= 0){
		if ((file_path[dir_len] == '\\') || (file_path[dir_len] == '/'))
			break;
		dir_len--;
	}
	dir_len++;
	base_len = wcslen(base_dir);

	// リカバリ・ファイルの一覧を表示する
	printf("PAR File list :\n");
	printf("         Size :  Filename\n");
	total_file_size = 0;
	parity_num = 0;
	volume_max = 0;
	for (i = 0; i <= 99; i++){
		if (i == 0){
			swprintf(find_path, MAX_LEN, L"%s%s", file_path, par_char);
			blk = 0;
		} else {
			swprintf(find_path, MAX_LEN, L"%s%02d", file_path, i);
			blk = 1;
		}
		// ファイルを開くことができるか、サイズを取得できるかを確かめる
		hFile = CreateFile(find_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE){
			continue;
		}
		// ファイルのサイズを取得する
		if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
			CloseHandle(hFile);
			continue;
		}
		CloseHandle(hFile);
		total_file_size += file_size;
		parity_num++;
		volume_max = i;
		// リカバリ・ファイルの名前
		utf16_to_cp(find_path + dir_len, ascii_buf);
		printf("%13I64d : \"%s\"\n", file_size, ascii_buf);
	}
	hFile = NULL;
	printf("\nPAR File total size\t: %I64d\n", total_file_size);
	printf("PAR File possible count\t: %d\n\n", parity_num);

	// 指定されたリカバリ・ファイルを開く
	hFile = CreateFile(recovery_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		printf("valid file is not found\n");
		err = 1;
		goto error_end;
	}
	// ファイルのサイズを取得する
	if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
		print_win32_err();
		err = 1;
		goto error_end;
	}

	// ヘッダー用のバッファーを確保する
	buf_size = 0x0060 + ((0x0038 + (260 * 2)) * 256) + (COMMENT_LEN * 2); // 最大サイズ
	if ((__int64)buf_size > file_size)
		buf_size = (unsigned int)file_size;
	header_buf = malloc(buf_size);
	if (header_buf == NULL){
		printf("malloc, %d\n", buf_size);
		err = 1;
		goto error_end;
	}

	// PAR や PXX ファイルとして正しいかを調べる
	parity_max = 0;
	par_flag = 0;
	par_comment[0] = 0;
	for (i = 0; i < 256; i++)
		exist[i] = -1;
	if (rv = get_header(NULL, hFile, header_buf, buf_size, set_hash,
			&volume_num, &file_num, &list_off, &list_size, &data_off, &data_size)){
		if (rv == 2){	// キャンセル
			err = 2;
			goto error_end;
		}
		volume_num = -1;
		par_flag |= 2;
	} else {
		// 各ブロックが存在するかを記録していく
		block_max = get_source_num(header_buf, &block_size);
		check_ini_file(set_hash, block_max);	// 前回の検査結果が存在するかどうか
		if (volume_num == 0){
			par_flag |= 1;
			rv = (int)data_size;
			if (buf_size < (int)data_size)
				rv = buf_size;
			if (rv > 1)
				read_comment(par_comment, header_buf + data_off, rv);
		}
	}
	CloseHandle(hFile);
	hFile = NULL;

	// 他のリカバリ・ファイルのヘッダー用のバッファーを確保する
	if (volume_num == -1){
		buf_size2 = 0x0060 + ((0x0038 + (260 * 2)) * 256) + (COMMENT_LEN * 2); // 最大サイズ
	} else {
		buf_size2 = 0x0060 + ((0x0038 + (260 * 2)) * file_num);
	}
	header_buf2 = malloc(buf_size2);
	if (header_buf2 == NULL){
		printf("malloc, %d\n", buf_size2);
		err = 1;
		goto error_end;
	}

	// 他のリカバリ・ファイルも調べる
	printf("Loading PAR File:\n");
	printf(" Block Status   :  Filename\n");
	fflush(stdout);
	parity_num = 0;
	for (i = 0; i <= 99; i++){
		if (err = cancel_progress())	// キャンセル処理
			goto error_end;
		if (i == 0){
			swprintf(find_path, MAX_LEN, L"%s%s", file_path, par_char);
			blk = 0;
		} else {
			swprintf(find_path, MAX_LEN, L"%s%02d", file_path, i);
			blk = 1;
		}
		utf16_to_cp(find_path + dir_len, ascii_buf);

		// 既にチェック済みなら
		if ((_wcsicmp(recovery_file + dir_len, find_path + dir_len) == 0) && (volume_num != -1)){
			if ((volume_num > 0) && (volume_num <= 99)){
				if (exist[block_max + volume_num - 1] == -1)	// 係数が重複してるのは使えない
					parity_num++;
				if (parity_max < volume_num)
					parity_max = volume_num;
				if (block_max + volume_num <= 256)
					exist[block_max + volume_num - 1] = i;
				if (volume_max < volume_num)
					volume_max = volume_num;
			}
			if ((par_flag & 2) != 0){
				printf(" 0 / %d Damaged  : \"%s\"\n", blk, ascii_buf);
				recovery_lost++;
			} else if (i == volume_num){
				printf(" %d / %d Good     : \"%s\"\n", blk, blk, ascii_buf);
			} else {
				blk = (volume_num > 0) ? 1 : 0;
				printf(" %d / %d Misnamed : \"%s\"\n", blk, blk, ascii_buf);
				recovery_lost++;	// リカバリ・ファイルの破損は修復必要とは判定しない
			}
			continue;
		}

		// ファイルを開く
		hFile = CreateFile(find_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			continue;
		// PAR や PXX ファイルとして正しいかを調べる
		bad_flag = 0;
		if (volume_num == -1){
			if (rv = get_header(find_path + dir_len, hFile, header_buf, buf_size, set_hash,
					&volume_num, &file_num, &list_off, &list_size, &data_off, &data_size)){
				if (rv == 2){	// キャンセル
					err = 2;
					goto error_end;
				}
				volume_num = -1;
				bad_flag = 1;
			} else {
				block_max = get_source_num(header_buf, &block_size);
				check_ini_file(set_hash, block_max);	// 前回の検査結果が存在するかどうか
				if (volume_num == 0){
					par_flag |= 1;
					rv = (int)data_size;
					if (buf_size < (int)data_size)
						rv = buf_size;
					if (rv > 1)
						read_comment(par_comment, header_buf + data_off, (int)data_size);
				} else if (volume_num <= 99){
					parity_num++;
					if (parity_max < volume_num)
						parity_max = volume_num;
					if (block_max + volume_num <= 256)
						exist[block_max + volume_num - 1] = i;
					if (volume_max < volume_num)
						volume_max = volume_num;
				}
				if (i != volume_num){ // ブロック番号と拡張子が異なるなら
					bad_flag = 3;
					blk = (volume_num > 0) ? 1 : 0;
				}
			}
		} else {
			if (rv = get_header(find_path + dir_len, hFile, header_buf2, buf_size2, hash,
					&volume_num2, &file_num2, &list_off2, NULL, &data_off2, &data_size)){
				if (rv == 2){	// キャンセル
					err = 2;
					goto error_end;
				}
				bad_flag = 1;
			} else { // 同じ構成の PXX かを確認する
				if (memcmp(hash, set_hash, 16) != 0)
					bad_flag = 2;
				if ((file_num2 != file_num) || (list_off2 != list_off) || (data_off2 != data_off))
					bad_flag = 2;
				if ((block_size) && (volume_num2 > 0)){
					if (data_size != block_size)
						bad_flag = 2;
				}
				if (!bad_flag){
					if (volume_num2 == 0){
						par_flag |= 1;
						rv = (int)data_size;
						if (buf_size2 < (int)data_size)
							rv = buf_size2;
						if (rv > 1)
							read_comment(par_comment, header_buf2 + data_off2, rv);
					} else if (volume_num2 <= 99){
						if (exist[block_max + volume_num2 - 1] == -1)	// 係数が重複してるのは使えない
							parity_num++;
						// 各ブロックが存在するかを記録していく
						if (parity_max < volume_num2)
							parity_max = volume_num2;
						if (block_max + volume_num2 <= 256)
							exist[block_max + volume_num2 - 1] = i;
						if (volume_max < volume_num2)
							volume_max = volume_num2;
					}
					if (i != volume_num2){ // ブロック番号と拡張子が異なるなら
						bad_flag = 3;
						blk = (volume_num2 > 0) ? 1 : 0;
					}
				}
			}
		}
		switch (bad_flag){
		case 0:
			printf(" %d / %d Good     : \"%s\"\n", blk, blk, ascii_buf);
			break;
		case 1:
			printf(" 0 / %d Damaged  : \"%s\"\n", blk, ascii_buf);
			recovery_lost++;
			break;
		case 2:
			printf(" %d / %d Useless  : \"%s\"\n", blk, blk, ascii_buf);
			recovery_lost++;
			break;
		case 3:
			printf(" %d / %d Misnamed : \"%s\"\n", blk, blk, ascii_buf);
			recovery_lost++;	// リカバリ・ファイルの破損は修復必要とは判定しない
			break;
		}
		CloseHandle(hFile);
		hFile = NULL;

		fflush(stdout);
	}

	if (volume_num == -1){ // Recovery Set が見つからなければここで終わる
		printf("valid file is not found\n");
		err = 1;
		goto error_end;
	}
	// セット・ハッシュとプログラムのバージョンを表示する
	printf("\nSet Hash: ");
	print_hash(set_hash);
	memcpy(&rv, header_buf + 12, 4);
	printf("\nCreator : ");
	switch (rv >> 24){
	case 1:	// Mirror
		printf("Mirror");
		break;
	case 2:	// PAR
		printf("PAR");
		break;
	case 3:	// SmartPar
		printf("SmartPar");
		break;
	default:
		printf("Unknown client %d", rv >> 24);
	}
	printf(" version %d.%d.%d\n", (rv >> 16) & 0xFF, (rv >> 8) & 0xFF, rv & 0xFF);
	if ((par_comment[0] != 0) && (!utf16_to_cp(par_comment, ascii_buf)))
		printf("Comment : %s\n", ascii_buf); // コメントをユニコードから戻せた場合だけ表示する

	if (((par_flag & 1) == 0) && (switch_p)){ // PAR ファイルを作り直す
		swprintf(find_path, MAX_LEN, L"%s%s", file_path, par_char);
		get_temp_name(find_path, file_name);
		if (recreate_par(file_name, header_buf, header_buf2) == 0){
			if (replace_file(find_path, file_name, switch_b) == 0){	// 破損したPARファイルが存在するならどかす
				utf16_to_cp(find_path + dir_len, ascii_buf);
				printf("\nRestored file :\n");
				printf("         Size :  Filename\n");
				printf("%13u : \"%s\"\n", data_off, ascii_buf);
			} else {
				DeleteFile(file_name);
			}
		}
	}
	free(header_buf2);
	header_buf2 = NULL;
	printf("\nParity Volume count\t: %d\n", volume_max);
	printf("Parity Volume found\t: %d\n", parity_num);
	fflush(stdout);

	// リカバリ・ファイルを開く
	len = sizeof(file_ctx) * (block_max + parity_max);
	files = malloc(len);
	if (files == NULL){
		printf("malloc, %d\n", len);
		err = 1;
		goto error_end;
	}
	for (i = 0; i < (block_max + parity_max); i++)
		files[i].hFile = NULL;
	for (i = block_max; i < (block_max + parity_max); i++){
		if (exist[i] != -1){ // リカバリ・ファイルが存在するなら
			if (exist[i] == 0){
				swprintf(find_path, MAX_LEN, L"%s%s", file_path, par_char);
			} else {
				swprintf(find_path, MAX_LEN, L"%s%02d", file_path, exist[i]);
			}
			files[i].hFile = CreateFile(find_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (files[i].hFile == INVALID_HANDLE_VALUE){
				exist[i] = 0;
				continue;
			}
			// ファイル・サイズを取得する
			if (!GetFileSizeEx(files[i].hFile, (PLARGE_INTEGER)&file_size2)){
				exist[i] = 0;
				continue;
			}
			files[i].size = file_size2;
			files[i].size -= data_off;
			//printf("%d: off = %d, size = %I64u \n", i, data_off, files[i].size);
			// 開始位置をリード・ソロモン符号のデータの所にしておく
			if (SetFilePointer(files[i].hFile, data_off, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER){
				exist[i] = 0;
				continue;
			}
			exist[i] = 1;
		} else {
			exist[i] = 0;
		}
	}
	for (i = 0; i < block_max; i++)
		exist[i] = 0;

	// ファイル・リストを表示する
	printf("\nInput File list : %d\n", file_num);
	printf("         Size B :  Filename\n");
	total_file_size = 0;
	source_num = 0;
	list_off2 = list_off;
	for (i = 0; i < file_num; i++){
		// entry size
		memcpy(&entry_size, header_buf + list_off2, 4);
		// status field
		memcpy(&parity_flag, header_buf + (list_off2 + 8), 2);
		parity_flag &= 0x01; // bit 0 = file is saved in the parity volume set
		if (parity_flag){
			source_num++;
			blk = 1;
		} else {
			blk = 0;
		}
		// size
		memcpy(&data_size, header_buf + (list_off2 + 16), 8);
		total_file_size += data_size;
		// filename
		len = (entry_size - 56) / 2;
		if (len >= MAX_LEN)
			len = MAX_LEN - 1;
		memcpy(file_name, header_buf + (list_off2 + 56), len * 2);
		file_name[len] = 0;
		utf16_to_cp(file_name, ascii_buf);
		// ファイル名が有効かどうか確かめる
		j = sanitize_filename(file_name);
		if (j != 0){
			if (j == 16){
				printf("filename is not valied, %s\n", ascii_buf);
				err = 1;
				goto error_end;
			}
			utf16_to_cp(file_name, ascii_buf);
		}
		printf("%13I64u %d : \"%s\"\n", data_size, blk, ascii_buf);
		list_off2 += entry_size; // 次のエントリー位置にずらす
	}
	printf("\nData File count : %d\n", source_num);
	printf("Max file size\t: %I64d\n", block_size);
	printf("Total data size : %I64d\n", total_file_size);

	// ソース・ファイルが存在していて正しいかを調べる
	printf("\nVerifying Input File   :\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	source_num = 0;
	for (i = 0; i < file_num; i++){
		if (err = cancel_progress())	// キャンセル処理
			goto error_end;
		find_flag = bad_flag = parity_flag = 0;

		// entry size
		memcpy(&entry_size, header_buf + list_off, 4);
		// status field
		memcpy(&parity_flag, header_buf + (list_off + 8), 2);
		parity_flag &= 0x0001; // bit 0 = file is saved in the parity volume set
		if (parity_flag)
			source_num++;
		// size
		memcpy(&data_size, header_buf + (list_off + 16), 8);
		file_size = 0;
		// filename
		len = (entry_size - 56) / 2;
		if (len >= MAX_LEN)
			len = MAX_LEN - 1;
		memcpy(file_name, header_buf + (list_off + 56), len * 2);
		file_name[len] = 0;
		utf16_to_cp(file_name, ascii_buf);
		// ファイル名が有効かどうか確かめる
		wcscpy(file_path, file_name);
		j = sanitize_filename(file_path);
		if (j != 0){
			wcscpy(file_name, file_path);
			utf16_to_cp(file_name, ascii_buf);
		}
		// ファイル名を基準ディレクトリに追加してパスにする
		if (base_len + wcslen(file_name) >= MAX_LEN - ADD_LEN){
			printf("filename is too long\n");
			err = 1;
			goto error_end;
		}
		wcscpy(file_path, base_dir);
		wcscpy(file_path + base_len, file_name);

		// ファイルが存在するか
		hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE){ // ファイルが存在するなら内容を確認する
			// 検査結果の記録があるかどうか
			bad_flag = check_ini_state(i, meta_data, hFile);
			memcpy(&file_size, meta_data, 8);
			if (bad_flag == -1){	// ファイル・サイズが不明なら消失扱いにする
				bad_flag = 1;
			} else if (bad_flag == -2){	// 検査結果が無かった場合
				bad_flag = 0;
				if (file_size >= data_size){ // 末尾にゴミが付いてないか調べる
					if (file_size > data_size)
						bad_flag = 2;
					if (data_size > 0){ // 16k MD5 hash が一致するか確かめる
						if (file_md5_16k(hFile, data_size, hash)){
							bad_flag = 1;
						} else if (memcmp(hash, header_buf + (list_off + 40), 16) != 0){
							bad_flag = 1;
						} else if (data_size > 16384){ // MD5 hash が一致するか確かめる
							if (rv = file_md5(file_name, hFile, data_size, hash)){
								if (rv == 2){
									err = 2;
									goto error_end;
								}
								bad_flag = 1;
							} else if (memcmp(hash, header_buf + (list_off + 24), 16) != 0){
								bad_flag = 1;
							}
							// 16k MD5 hash が一致した時だけ検査結果を記録する
							write_ini_state(i, meta_data, bad_flag);
						}
					}
				} else if (file_size < data_size){
					bad_flag = 1;
				}
			}
			CloseHandle(hFile);
			hFile = NULL;
			if (!bad_flag){
				find_flag = 3;
				if (parity_flag)
					exist[source_num - 1] = 1;
			}
		}

		// ファイルが存在しないか、不完全だった場合は、サイズとハッシュ値で検索する
		if ((data_size) && ((hFile == INVALID_HANDLE_VALUE) || (bad_flag))){
			wcscpy(find_path, base_dir);
			wcscpy(find_path + base_len, L"*");
			hFind = FindFirstFile(find_path, &FindData);
			if (hFind != INVALID_HANDLE_VALUE){
				do {
					if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0){ // フォルダは無視する
						find_flag = 1;
						file_size2 = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
						if (file_size2 != data_size){ // ファイル・サイズが一致しなければ
							find_flag = 0;
						} else { // ファイル名がリスト上にあれば除外する
							memcpy(&list_off2, header_buf + 64, 4);
							for (j = 0; j < file_num; j++){
								// entry size
								memcpy(&entry_size2, header_buf + list_off2, 4);
								// filename
								len = (entry_size2 - 56) / 2;
								if (len >= MAX_LEN)
									len = MAX_LEN - 1;
								memcpy(file_name, header_buf + (list_off2 + 56), len * 2);
								file_name[len] = 0;
								sanitize_filename(file_name);
								if (_wcsicmp(file_name, FindData.cFileName) == 0){
									find_flag = 0;
									break;
								}
								list_off2 += entry_size2; // 次のエントリー位置にずらす
							}
						}
						// リスト上に無くてファイル・サイズが一致すれば
						if ((find_flag != 0) && (base_len + wcslen(FindData.cFileName) < MAX_LEN)){
							wcscpy(find_path + base_len, FindData.cFileName);
							hFile = CreateFile(find_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
							if (hFile != INVALID_HANDLE_VALUE){
								j = check_ini_state(i, meta_data, hFile);
								if (j == -2){	// 検査結果が無かった場合
									if (data_size > 0){ // 16k MD5 hash が一致するか確かめる
										if (file_md5_16k(hFile, data_size, hash)){
											find_flag = 0;
										} else if (memcmp(hash, header_buf + (list_off + 40), 16) != 0){
											find_flag = 0;
										} else if (data_size > 16384){ // MD5 hash が一致するか確かめる
											j = 0;
											if (rv = file_md5(FindData.cFileName, hFile, data_size, hash)){
												if (rv == 2){
													err = 2;
													goto error_end;
												}
												find_flag = 0;
												j = 1;
											} else if (memcmp(hash, header_buf + (list_off + 24), 16) != 0){
												find_flag = 0;
												j = 1;
											}
											// 16k MD5 hash が一致した時だけ検査結果を記録する
											write_ini_state(i, meta_data, j);
										}
									}
								} else if (j != 0){	// 完全以外なら
									find_flag = 0;
								}
								CloseHandle(hFile);
								hFile = NULL;
								if (find_flag){ // 異なる名前だが同じ内容のファイルを発見した
									utf16_to_cp(FindData.cFileName, ascii_buf2);
									// ファイル名を修正する
									if (replace_file(file_path, find_path, switch_b) == 0){
										find_flag = 2;
//									} else {
//										printf("cannot rename to %s\n", ascii_buf);
//										err = 1;
//										goto error_end;
									}
									break;
								}
							} else {
								find_flag = 0;
							}
						}
					}
				} while (FindNextFile(hFind, &FindData)); // 次のファイルを検索する
				FindClose(hFind);
			}
		}
		if (find_flag == 3){
			printf("            = Complete : \"%s\"\n", ascii_buf);
		} else if (find_flag == 1){
			need_repair++;
			need_repair |= 0x40000000;	// 修復失敗の印
			if (bad_flag){
				printf("%13I64u Failed~  : \"%s\"\n", file_size, ascii_buf);
			} else {
				printf("            - Failed   : \"%s\"\n", ascii_buf);
			}
			printf("%13I64u Misnamed : \"%s\"\n", data_size, ascii_buf2);
		} else if (find_flag == 2){
			need_repair += 0x10001;	// 修復できた数を記録する
			if (parity_flag)
				exist[source_num - 1] = 1;
			printf("%13I64u Restored : \"%s\"\n", data_size, ascii_buf);
		} else if (bad_flag == 2){
			if (shorten_file(file_path, data_size, switch_b) != 0)	// ファイルを小さくする
				bad_flag = 1;
			need_repair++;
			if (bad_flag == 2){	// 修復 (縮小) できたら
				need_repair += 0x10000;	// 上位 16-bit に修復できた数を記録する
				if (parity_flag)
					exist[source_num - 1] = 1;
				printf("%13I64u Restored : \"%s\"\n", data_size, ascii_buf);
			} else {	// 修復できなかったなら
				if (parity_flag)
					block_lost++;
				printf("%13I64u Appended : \"%s\"\n", file_size, ascii_buf);
			}
		} else if (bad_flag){
			bad_flag = 1;
			if (data_size == 0){	// サイズが 0ならすぐに復元できる
				need_repair++;
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile != INVALID_HANDLE_VALUE){
					bad_flag = 0;
					need_repair += 0x10000;	// 上位 16-bit に修復できた数を記録する
				}
				CloseHandle(hFile);
				hFile = NULL;
			}
			if (bad_flag){
				if (parity_flag)
					block_lost++;
				printf("%13I64u Damaged  : \"%s\"\n", file_size, ascii_buf);
			} else {
				if (parity_flag)
					exist[source_num - 1] = 1;
				printf("%13I64u Restored : \"%s\"\n", data_size, ascii_buf);
			}
		} else {
			bad_flag = 1;
			if (data_size == 0){ // サイズが 0ならすぐに復元できる
				need_repair++;
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile != INVALID_HANDLE_VALUE){
					bad_flag = 0;
					need_repair += 0x10000;	// 上位 16-bit に修復できた数を記録する
				}
				CloseHandle(hFile);
				hFile = NULL;
			}
			if (bad_flag){
				if (parity_flag)
					block_lost++;
				printf("            - Missing  : \"%s\"\n", ascii_buf);
			} else {
				if (parity_flag)
					exist[source_num - 1] = 1;
				printf("%13I64u Restored : \"%s\"\n", data_size, ascii_buf);
			}
		}

		if (parity_flag){
			// ソース・ファイルを開く
			if (exist[source_num - 1]){ // ソース・ブロックが存在するなら
				files[source_num - 1].hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (files[source_num - 1].hFile == INVALID_HANDLE_VALUE){
					exist[source_num - 1] = 0;
				} else {
					if (!GetFileSizeEx(files[source_num - 1].hFile, (PLARGE_INTEGER)&file_size2)){
						exist[source_num - 1] = 0;
					} else {
						files[source_num - 1].size = file_size2;
					}
				}
			} else { // 破損してる、または存在しないなら
				get_temp_name(file_path, find_path); // 修復中のテンポラリ・ファイル
				files[source_num - 1].hFile = CreateFile(find_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (files[source_num - 1].hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					printf_cp("cannot create file, %s\n", find_path);
					err = 1;
					goto error_end;
				}
				files[source_num - 1].size = data_size; 
			}
			//printf("%d: exist = %d, size = %I64u \n", source_num - 1, exist[source_num - 1], files[source_num - 1].size);
		}

		fflush(stdout);
		list_off += entry_size; // 次のエントリー位置にずらす
	}
	printf("\nData File lost\t: %d\n\n", block_lost);

	// 修復に必要なパリティ・ブロックの数が少なくて済むなら最大値を調節する
	if (block_lost < parity_num){
		volume_num = 0;
		len = block_max + parity_max;
		for (i = block_max; i < len; i++){
			if (exist[i]){
				volume_num++;
				if (volume_num > block_lost){
					volume_num--;
					exist[i] = 0;
					CloseHandle(files[i].hFile);
					files[i].hFile = NULL;
				} else {
					parity_max = 1 + i - block_max;
				}
			}
		}
		len = sizeof(file_ctx) * (block_max + parity_max);
		tmp_p = (file_ctx *)realloc(files, len);
		if (tmp_p == NULL){
			printf("realloc, %d\n", len);
			err = 1;
			goto error_end;
		} else {
			files = tmp_p;
		}
	}

	if ((block_lost == 0) && (need_repair == 0))
		printf("All Files Complete\n");
	if (recovery_lost > 0){	// 不完全なリカバリ・ファイルがあるなら
		err = 256;
		printf("%d PAR File(s) Incomplete\n", recovery_lost);
	}

	// 修復する必要があるかどうか
	if (block_lost == 0){
		if (need_repair > 0){
			need_repair &= 0x3FFFFFFF;
			printf("Restored file count\t: %d\n\n", need_repair >> 16);
			need_repair = (need_repair & 0xFFFF) - (need_repair >> 16);
			if (need_repair == 0){	// 全て修復できたのなら
				printf("Repaired successfully\n");
				err |= 16;
			} else {
				printf("Failed to repair %d file(s)\n", need_repair);
				err |= 16 | 4;
			}
		}
		goto error_end;
	} else {
		if (need_repair > 0){
			printf("Restored file count\t: %d\n\n", (need_repair >> 16) & 0x3FFF);
			if (need_repair & 0x40000000){
				need_repair = (need_repair & 0xFFFF) - ((need_repair >> 16) & 0x3FFF);
				printf("Failed to repair %d file(s)\n", block_lost + need_repair);
				err |= 16 | 4;
				goto error_end;
			}
			need_repair &= 0x3FFFFFFF;
			need_repair = (need_repair & 0xFFFF) - (need_repair >> 16);
		}
		if (block_lost > parity_num){
			printf("Need %d more volume(s) to repair %d file(s)\n", block_lost - parity_num, block_lost);
			err |= 8 | 4;
			goto error_end;
		} else {
			printf("Ready to repair %d file(s)\n", block_lost);
		}
	}

	// 失われたブロックを復元する
	printf("\n");
	print_progress_text(0, "Recovering data");
	if (err = rs_decode(block_max, block_lost, block_size, parity_max, exist, files)){
		goto error_end;
	}

	// 正しく修復できたか確かめる
	printf("\nVerifying repair: %d\n", block_lost);
	printf(" Status   :  Filename\n");
	fflush(stdout);
	wcscpy(file_path, base_dir);
	find_flag = 0;
	source_num = 0;
	memcpy(&list_off, header_buf + 64, 4);
	for (i = 0; i < file_num; i++){
		bad_flag = 0;

		// entry size
		memcpy(&entry_size, header_buf + list_off, 4);
		// status field
		memcpy(&parity_flag, header_buf + (list_off + 8), 2);
		parity_flag &= 0x0001; // bit 0 = file is saved in the parity volume set
		// size
		memcpy(&data_size, header_buf + (list_off + 16), 8);
		// filename
		len = (entry_size - 56) / 2;
		if (len >= MAX_LEN)
			len = MAX_LEN - 1;
		memcpy(file_name, header_buf + (list_off + 56), len * 2);
		file_name[len] = 0;
		utf16_to_cp(file_name, ascii_buf);
		sanitize_filename(file_name);

		// ファイル名を基準ディレクトリに追加してパスにする
		wcscpy(file_path + base_len, file_name);

		//printf("%d : list_off = %d, exist = %d \n", i, list_off, exist[source_num]);
		// 修復したファイルのハッシュ値を計算する
		if (parity_flag){
			if (exist[source_num] == 0){
				if (file_md5(file_name, files[source_num].hFile, data_size, hash)){
					bad_flag = 1;
				} else {
					for (j = 0; j < 16; j++){
						if (hash[j] != header_buf[list_off + 24 + j]){
							bad_flag = 1;
							break;
						}
					}
				}
				CloseHandle(files[source_num].hFile);
				files[source_num].hFile = NULL;
				get_temp_name(file_path, find_path);
				if (bad_flag){ // 失敗
					DeleteFile(find_path); // テンポラリ・ファイルを削除する
					printf(" Failed   : \"%s\"\n", ascii_buf);
				} else { // 復元成功
					if (replace_file(file_path, find_path, switch_b) == 0){	// 修復したファイルを戻す
						block_lost--;
						find_flag++;
						printf(" Repaired : \"%s\"\n", ascii_buf);
					} else {	// ファイルを戻せなかった場合は、テンポラリ・ファイルを残した方がいいかも？
						// まあ PAR2 と違ってすぐに修復できるから、削除してもよさそう。
						DeleteFile(find_path); // テンポラリ・ファイルを削除する
						printf(" Locked   : \"%s\"\n", ascii_buf);
					}
				}
				fflush(stdout);
			}
			source_num++;
		}

		list_off += entry_size; // 次のエントリー位置にずらす
	}
	printf("\nRepaired file count\t: %d\n\n", find_flag);

	err = 16;
	if (block_lost + need_repair == 0){	// 全て修復できたなら
		printf("Repaired successfully\n");
	} else {
		printf("Failed to repair %d file(s)\n", block_lost + need_repair);
		err |= 4;
	}
	if (recovery_lost > 0)
		err |= 256;

error_end:
	close_ini_file();
	if (files){
		// エラーが発生したら、作業中のファイルを削除する
		source_num = 0;
		memcpy(&list_off, header_buf + 64, 4);
		for (i = 0; i < file_num; i++){
			// entry size
			memcpy(&entry_size, header_buf + list_off, 4);
			// status field
			memcpy(&parity_flag, header_buf + (list_off + 8), 2);
			parity_flag &= 0x0001; // bit 0 = file is saved in the parity volume set
			// filename
			len = (entry_size - 56) / 2;
			if (len >= MAX_LEN)
				len = MAX_LEN - 1;
			memcpy(file_name, header_buf + (list_off + 56), len * 2);
			file_name[len] = 0;
			sanitize_filename(file_name);

			// ファイル名を基準ディレクトリに追加してパスにする
			wcscpy(file_path + base_len, file_name);

			if (parity_flag){
				if ((exist[source_num] == 0) && (files[source_num].hFile)){
					CloseHandle(files[source_num].hFile);
					files[source_num].hFile = NULL;
					get_temp_name(file_path, find_path);
					DeleteFile(find_path); // テンポラリ・ファイルを削除する
				}
				source_num++;
			}

			list_off += entry_size; // 次のエントリー位置にずらす
		}
		for (i = 0; i < (source_num + parity_max); i++){
			if (files[i].hFile != NULL)
				CloseHandle(files[i].hFile);
		}
		free(files);
	}
	if (hFile != NULL)
		CloseHandle(hFile);
	if (header_buf)
		free(header_buf);
	if (header_buf2)
		free(header_buf2);
	return err;
}

// ソース・ファイルの一覧を表示する
int par1_list(
	int switch_h,				// ハッシュ値も表示する
	wchar_t *par_comment)		// コメント
{
	char ascii_buf[MAX_LEN * 3];
	unsigned char *header_buf = NULL, hash[16];
	wchar_t file_name[MAX_LEN];
	int err = 0, i, parity_flag;
	int len, buf_size, entry_size, blk;
	int volume_num, file_num, list_off, list_size, data_off;
	int source_num = 0;
	__int64 file_size, data_size, block_size = 0;
	HANDLE hFile = NULL;

	// 指定されたリカバリ・ファイルを開く
	hFile = CreateFile(recovery_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		printf("cannot open PAR file\n");
		err = 1;
		goto error_end;
	}

	// ファイルのサイズを取得する
	if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
		print_win32_err();
		err = 1;
		goto error_end;
	}

	// ヘッダー用のバッファーを確保する
	buf_size = 0x0060 + ((0x0038 + (260 * 2)) * 256) + (COMMENT_LEN * 2); // 最大サイズ
	if ((__int64)buf_size > file_size)
		buf_size = (unsigned int)file_size;
	header_buf = malloc(buf_size);
	if (header_buf == NULL){
		printf("malloc, %d\n", buf_size);
		err = 1;
		goto error_end;
	}

	// PAR や PXX ファイルとして正しいかを調べる
	if (err = get_header(NULL, hFile, header_buf, buf_size, hash,
			&volume_num, &file_num, &list_off, &list_size, &data_off, &data_size)){
		if (err != 2)	// キャンセル以外なら
			printf("valid file is not found\n");
		goto error_end;
	} else {
		if ((volume_num == 0) && (data_size > 0)){ // コメントがある
			len = (int)data_size;
			if (buf_size < (int)data_size)
				len = buf_size;
			if (len > 1)
				read_comment(par_comment, header_buf + data_off, len);
			if (!utf16_to_cp(par_comment, ascii_buf))
				printf("Comment : %s\n", ascii_buf); // コメントをユニコードから戻せた場合だけ表示する
		}
	}
	CloseHandle(hFile);
	hFile = NULL;

	printf("Input File list : %d\n", file_num);
	if (switch_h){
		printf("         Size B             MD5 Hash             :  Filename\n");
	} else {
		printf("         Size B :  Filename\n");
	}
	// ソース・ファイルの情報を表示する
	for (i = 0; i < file_num; i++){
		// entry size
		memcpy(&entry_size, header_buf + list_off, 4);
		// status field
		memcpy(&parity_flag, header_buf + (list_off + 8), 2);
		parity_flag &= 0x01; // bit 0 = file is saved in the parity volume set
		if (parity_flag){
			source_num++;
			blk = 1;
		} else {
			blk = 0;
		}
		// size
		memcpy(&file_size, header_buf + (list_off + 16), 8);
		if (file_size > block_size)
			block_size = file_size;
		// hash
		memcpy(hash, header_buf + (list_off + 40), 16);
		// filename
		len = (entry_size - 56) / 2;
		if (len >= MAX_LEN)
			len = MAX_LEN - 1;
		memcpy(file_name, header_buf + (list_off + 56), len * 2);
		file_name[len] = 0;
		utf16_to_cp(file_name, ascii_buf);

		// 項目番号、ソース・ブロックかどうか、ファイル・サイズ、MD5ハッシュ値、ファイル名
		if (switch_h){
			printf("%13I64u %d ", file_size, blk);
			print_hash(hash);
			printf(" : \"%s\"\n", ascii_buf);
		} else {
			printf("%13I64u %d : \"%s\"\n", file_size, blk, ascii_buf);
		}

		list_off += entry_size; // 次のエントリー位置にずらす
	}

	// 調べた結果を表示する
	printf("\nData File count : %d\n", source_num);
	printf("Max file size\t: %I64u\n", block_size);
	printf("\nListed successfully\n");
error_end:
	if (header_buf)
		free(header_buf);
	if (hFile != NULL)
		CloseHandle(hFile);
	return err;
}

// CRC-32 チェックサムを使って自分自身の破損を検出する
int par1_checksum(wchar_t *uni_buf)	// 作業用
{
	unsigned int crc, chk, chk2;
	unsigned int len, tmp, i;
	unsigned char *pAddr, *p;
	HANDLE hFile, hMap;

	// 実行ファイルのパスを取得する
	tmp = GetModuleFileName(NULL, uni_buf, MAX_LEN);
	if ((tmp == 0) || (tmp >= MAX_LEN))
		return 1;
	//printf("%S\n", uni_buf);

	// 実行ファイルの PE checksum と CRC-32 を検証する
	hFile = CreateFile(uni_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		return 1;
	}
	len = GetFileSize(hFile, &chk2);
	if (len == INVALID_FILE_SIZE)
		return 1;
	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, chk2, len, NULL);
	if (hMap == NULL){
		CloseHandle(hFile);
		return 1;
	}
	pAddr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, len);
	if (pAddr == NULL){
		CloseHandle(hMap);
		CloseHandle(hFile);
		return 1;
	}
	if (CheckSumMappedFile(pAddr, len, &chk2, &chk) == NULL){	// PE checksum
		UnmapViewOfFile(pAddr);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return 1;
	}
	crc = 0xFFFFFFFF;
	p = pAddr;
	while (len--){
		tmp = (*p++);
		for (i = 0; i < 8; i++){
			if ((tmp ^ crc) & 1){
				crc = (crc >> 1) ^ 0xEDB88320;
			} else {
				crc = crc >> 1;
			}
			tmp = tmp >> 1;
		}
	}
	crc ^= 0xFFFFFFFF;
	UnmapViewOfFile(pAddr);
	CloseHandle(hMap);
	CloseHandle(hFile);

	if (chk != chk2)
		return 2;
	if (crc != 0x11111111)
		return 3;
	return 0;
}

