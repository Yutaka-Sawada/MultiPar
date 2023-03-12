#ifndef _MD5_H
#define _MD5_H

#ifdef __cplusplus
extern "C" {
#endif


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// バイト配列の MD5 ハッシュ値を求める
void data_md5(
	unsigned char *data_in,	// ハッシュ値を求めるバイト配列
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash);	// ハッシュ値 16バイト

// ブロックの MD5 ハッシュ値を求める
void data_md5_block(
	unsigned char *data_in,	// ハッシュ値を求めるバイト配列
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash);	// ハッシュ値 16バイト

// ファイルの MD5-16k ハッシュ値を求める
int file_md5_16(
	wchar_t *file_path,		// ハッシュ値を求めるファイル
	unsigned char *hash);	// ファイルの先頭 16KB 分のハッシュ値 (16バイト)

// ファイルの指定部分の MD5 ハッシュ値を求める
int file_md5(
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	__int64 offset,
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash);	// ハッシュ値 (16バイト)

/*------------------  以下は MD5 と CRC-32 の組合せ関数  ------------------*/

// 内容が全て 0 のデータの MD5 ハッシュ値と CRC-32 を求める
void data_md5_crc32_zero(
	unsigned char *hash);	// ハッシュ値 (16 + 4バイト, MD5 + CRC-32)

// 2GB 未満のファイルの開始位置以降のハッシュ値を計算する
unsigned int file_crc_part(HANDLE hFile);

// ファイルの offset バイト目からブロック・サイズ分の MD5 と CRC-32 を求める
int file_md5_crc32_block(
	HANDLE hFileRead,			// MD5 と CRC を求めるファイルのハンドル
	__int64 offset,
	unsigned int avail_size,	// 入力バイト数
	unsigned char *hash);		// ハッシュ値 (16 + 4バイト, MD5 + CRC-32)

// ファイルのハッシュ値と各スライスのチェックサムを同時に計算する
int file_hash_crc(
	wchar_t *file_name,			// ハッシュ値を求めるファイル
	__int64 file_left,
	unsigned char *hash,		// ハッシュ値 (16バイト)
	unsigned char *sum,			// チェックサム (MD5 + CRC-32) の配列
	unsigned int *time_last,	// 前回に経過表示した時刻
	__int64 *prog_now);			// 経過表示での現在位置

typedef struct {
	wchar_t *file_name;
	__int64 file_size;
	unsigned char *hash;
	unsigned char *sum;
	unsigned int *crc;
	volatile int loop;
} FILE_HASH_TH;

DWORD WINAPI file_hash_crc2(LPVOID lpParameter);

// ファイルのハッシュ値が同じか調べる (全てのスライスのチェックサムも)
int file_hash_check(
	int num,				// file_ctx におけるファイル番号
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	int prog_min,			// 検出数がこの値を超えたら合計数を表示する (極大なら表示しない)
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報 (NULL なら比較しない)

typedef struct {
	HANDLE hFile;
	file_ctx_r *files;
	source_ctx_r *s_blk;
	int num;
	volatile int rv;
	volatile int flag;
	unsigned int meta[7];
} FILE_CHECK_TH;

DWORD WINAPI file_hash_background(LPVOID lpParameter);

// キャッシュ無しでファイルのハッシュ値が同じか調べる
int file_hash_direct(
	int num,				// file_ctx におけるファイル番号
	wchar_t *file_path,		// ハッシュ値を求めるファイル
	wchar_t *file_name,		// 表示するファイル名
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報 (NULL なら比較しない)

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// CRC-32 を使って 1バイト内のバースト・エラーを訂正する
// 全て 0 のデータの判定は他で行うこと
int correct_error(
	unsigned char *data_in,		// ハッシュ値を求めたバイト配列
	unsigned int data_len,		// 入力バイト数
	unsigned char *hash,		// ブロック・サイズ分のハッシュ値 (16バイト, MD5)
	unsigned int orig_crc,		// 入力バイト数分の CRC-32
	unsigned int *error_off,	// エラー開始位置
	unsigned char *error_mag);	// エラー内容、XORでエラー訂正を取り消せる

// CRC-32 を使って 1バイト内のバースト・エラーを訂正する
// CRC-32 は既に計算済み、エラー無しも比較済み
int correct_error2(
	unsigned char *data_in,		// ハッシュ値を求めたバイト配列
	unsigned int data_len,		// 入力バイト数
	unsigned int data_crc,		// 入力バイト数分の CRC-32
	unsigned char *hash,		// ブロック・サイズ分のハッシュ値 (16バイト, MD5)
	unsigned int crc,			// 入力バイト数分の CRC-32
	unsigned int *error_off,	// エラー開始位置
	unsigned char *error_mag);	// エラー内容、XORでエラー訂正を取り消せる


#ifdef __cplusplus
}
#endif

#endif
