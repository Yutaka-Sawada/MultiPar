#ifndef _MD5_H
#define _MD5_H

#ifdef __cplusplus
extern "C" {
#endif


// バイト配列の MD5 ハッシュ値を求める
void data_md5(
	unsigned char *data_in,	// ハッシュ値を求めるバイト配列
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash);	// ハッシュ値 16バイト

// ファイルの MD5 ハッシュ値を求める
int file_md5(
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned __int64 file_size,
	unsigned char *hash);	// ハッシュ値 (16バイト)

// ファイルの MD5 ハッシュ値を求めて、全体的な経過を表示する
int file_md5_total(
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned __int64 file_size,
	unsigned char *hash,	// ハッシュ値 (16バイト)
	unsigned char *hash16,	// 先頭 16KB 分のハッシュ値
	__int64 total_file_size,
	__int64 *prog_now);		// 経過表示での現在位置

// ファイルの先頭 16KB 分の MD5 ハッシュ値を求める
int file_md5_16k(
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned __int64 file_size,
	unsigned char *hash);	// ハッシュ値 (16バイト)

// ファイルの先頭 32バイト目からの MD5 ハッシュ値を求める
int file_md5_from32(
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	unsigned char *hash);	// ハッシュ値 (16バイト)


#ifdef __cplusplus
}
#endif

#endif // md5.h
