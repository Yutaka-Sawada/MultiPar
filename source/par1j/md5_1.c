// md5_1.c
// Copyright : 2021-05-14 Yutaka Sawada
// License : GPL

#include <windows.h>

#include "common1.h"
#include "phmd5.h"
#include "md5_1.h"


// バイト配列の MD5 ハッシュ値を求める
void data_md5(
	unsigned char *data_in,	// ハッシュ値を求めるバイト配列
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash)	// ハッシュ値 16バイト
{
	PHMD5 ctx;

	Phmd5Begin(&ctx);
	Phmd5Process(&ctx, data_in, data_len);
	Phmd5End(&ctx);
	memcpy(hash, ctx.hash, 16);
}

// ファイルの MD5 ハッシュ値を求める
int file_md5(
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned __int64 file_size,
	unsigned char *hash)	// ハッシュ値 (16バイト)
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;
	unsigned int time_last;
	unsigned __int64 file_left;
	PHMD5 hash_ctx;

	// ファイルの先頭に戻す
	file_left = 0;
	if (!SetFilePointerEx(hFileRead, *((PLARGE_INTEGER)&file_left), NULL, FILE_BEGIN))
		return 1;

	prog_last = -1;	// 検証中のファイル名を毎回表示する
	time_last = GetTickCount();
	file_left = file_size;
	Phmd5Begin(&hash_ctx); // 初期化

	while (file_left > 0){
		if (file_left < IO_SIZE){
			len = (unsigned int)file_left;
		} else {
			len = IO_SIZE;
		}
		if (!ReadFile(hFileRead, buf, len, &rv, NULL) || (len != rv))
			return 1;
		file_left -= len;

		// MD5 計算
		Phmd5Process(&hash_ctx, buf, len);

		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress_file((int)(((file_size - file_left) * 1000) / file_size), file_name))
				return 2;
			time_last = GetTickCount();
		}
	}
	Phmd5End(&hash_ctx); // 最終処理
	memcpy(hash, hash_ctx.hash, 16);

	return 0;
}

// ファイルの MD5 ハッシュ値を求めて、全体的な経過を表示する
int file_md5_total(
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned __int64 file_size,
	unsigned char *hash,	// ハッシュ値 (16バイト)
	unsigned char *hash16,	// 先頭 16KB 分のハッシュ値
	__int64 total_file_size,
	__int64 *prog_now)		// 経過表示での現在位置
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;
	unsigned int time_last;
	unsigned __int64 file_left;
	PHMD5 hash_ctx;

	// ファイルの先頭に戻す
	file_left = 0;
	if (!SetFilePointerEx(hFileRead, *((PLARGE_INTEGER)&file_left), NULL, FILE_BEGIN))
		return 1;

	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する
	file_left = file_size;
	Phmd5Begin(&hash_ctx); // 初期化

	while (file_left > 0){
		if (file_left < IO_SIZE){
			len = (unsigned int)file_left;
		} else {
			len = IO_SIZE;
		}
		if (!ReadFile(hFileRead, buf, len, &rv, NULL) || (len != rv))
			return 2;
		(*prog_now) += len;
		if (file_left == file_size){	// 最初だけ先頭 16KB 分のハッシュ値を計算する
			if (len > 16384){
				Phmd5Process(&hash_ctx, buf, 16384);
				Phmd5End(&hash_ctx); // 最終処理
				memcpy(hash16, hash_ctx.hash, 16);
				Phmd5Begin(&hash_ctx); // 初期化しなおす
			} else {	// ファイル・サイズが 16KB 以下なら
				Phmd5Process(&hash_ctx, buf, len);
				Phmd5End(&hash_ctx); // 最終処理
				memcpy(hash, hash_ctx.hash, 16);
				return 0;
			}
		}
		file_left -= len;

		// MD5 計算
		Phmd5Process(&hash_ctx, buf, len);

		// 経過表示
		if (GetTickCount() / UPDATE_TIME != time_last){
			if (print_progress((int)(((*prog_now) * 1000) / total_file_size)))
				return -2;
			time_last = GetTickCount() / UPDATE_TIME;
		}
	}
	Phmd5End(&hash_ctx); // 最終処理
	memcpy(hash, hash_ctx.hash, 16);

	return 0;
}

// ファイルの先頭 16KB 分の MD5 ハッシュ値を求める
int file_md5_16k(
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned __int64 file_size,
	unsigned char *hash)	// ハッシュ値 (16バイト)
{
	unsigned char buf[16384];
	unsigned int len;
	LARGE_INTEGER qwi;	// Quad Word Integer
	PHMD5 hash_ctx;

	// ファイルの先頭に戻す
	qwi.QuadPart = 0;
	if (!SetFilePointerEx(hFileRead, qwi, NULL, FILE_BEGIN))
		return 1;

	Phmd5Begin(&hash_ctx); // 初期化

	if (!ReadFile(hFileRead, buf, 16384, &len, NULL))
		return 2;
	if ((unsigned __int64)len > file_size)
		len = (unsigned int)file_size;

	// MD5 計算
	Phmd5Process(&hash_ctx, buf, len);
	Phmd5End(&hash_ctx); // 最終処理
	memcpy(hash, hash_ctx.hash, 16);

	return 0;
}

// ファイルの先頭 32バイト目からの MD5 ハッシュ値を求める
int file_md5_from32(
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned char *hash)	// ハッシュ値 (16バイト)
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;
	unsigned int time_last;
	unsigned __int64 file_size, file_left;
	LARGE_INTEGER qwi;	// Quad Word Integer
	PHMD5 hash_ctx;

	// ファイル・サイズを取得する
	if (!GetFileSizeEx(hFileRead, (PLARGE_INTEGER)&file_size))
		return 1;
	if (file_size <= 32)
		return 1;

	// ファイルの開始位置を 32バイト目にする
	qwi.QuadPart = 32;
	if (!SetFilePointerEx(hFileRead, qwi, NULL, FILE_BEGIN))
		return 1;

	prog_last = -1;	// 検証中のファイル名を毎回表示する
	time_last = GetTickCount();
	file_left = file_size - 32;
	Phmd5Begin(&hash_ctx); // 初期化

	while (file_left > 0){
		if (file_left < IO_SIZE){
			len = (unsigned int)file_left;
		} else {
			len = IO_SIZE;
		}
		if (!ReadFile(hFileRead, buf, len, &rv, NULL) || (len != rv))
			return 1;
		file_left -= len;

		// MD5 計算
		Phmd5Process(&hash_ctx, buf, len);

		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress_file((int)(((file_size - file_left) * 1000) / file_size), file_name))
				return 2;
			time_last = GetTickCount();
		}
	}
	Phmd5End(&hash_ctx); // 最終処理
	memcpy(hash, hash_ctx.hash, 16);

	return 0;
}
