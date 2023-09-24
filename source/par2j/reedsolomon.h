#ifndef _REEDSOLOMON_H_
#define _REEDSOLOMON_H_

#ifdef __cplusplus
extern "C" {
#endif


//#define TIMER // 実験用

// Read all source & Keep some parity 方式
// 部分的なエンコードを行う最低ブロック数
#define PART_MIN_RATE	5	// ソース・ブロック数の 1/32 = 3.1%

// Read some source & Keep all parity 方式
// 一度に読み込む最少ブロック数
#define READ_MIN_RATE	1	// 保持するブロック数の 1/2 = 50%
#define READ_MIN_NUM	16

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// Cache Blocking を試みる
int try_cache_blocking(int unit_size);

// 空きメモリー量からファイル・アクセスのバッファー・サイズを計算する
unsigned int get_io_size(
	unsigned int buf_num,	// 何ブロック分の領域を確保するのか
	unsigned int *part_num,	// 部分的なエンコード用の作業領域
	size_t trial_alloc,		// 確保できるか確認するのか
	int alloc_unit);		// メモリー単位の境界 (sse_unit か MEM_UNIT)

// 何ブロックまとめてファイルから読み込むかを空きメモリー量から計算する
int read_block_num(
	int keep_num,			// 保持するパリティ・ブロック数
	size_t trial_alloc,		// 確保できるか確認するのか
	int alloc_unit);		// メモリー単位の境界 (sse_unit か MEM_UNIT)

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// リード・ソロモン符号を使ってエンコードする
int rs_encode(
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk);		// パリティ・ブロックの情報

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
	source_ctx_c *s_blk);		// ソース・ブロックの情報

// リード・ソロモン符号を使ってデコードする
int rs_decode(
	wchar_t *file_path,
	int block_lost,				// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_r *files,			// ソース・ファイルの情報
	source_ctx_r *s_blk,		// ソース・ブロックの情報
	parity_ctx_r *p_blk);		// パリティ・ブロックの情報


#ifdef __cplusplus
}
#endif

#endif
