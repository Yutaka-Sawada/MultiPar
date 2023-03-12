#ifndef _RS_DECODE_H_
#define _RS_DECODE_H_

#ifdef __cplusplus
extern "C" {
#endif


int decode_method1(	// ソース・ブロックが一個だけの場合
	wchar_t *file_path,
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk);	// パリティ・ブロックの情報

int decode_method2(	// ソース・データを全て読み込む場合
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,		// パリティ・ブロックの情報
	unsigned short *mat);

int decode_method3(	// 復元するブロックを全て保持できる場合
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,	// パリティ・ブロックの情報
	unsigned short *mat);

int decode_method4(	// 全てのブロックを断片的に保持する場合 (GPU対応)
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,		// パリティ・ブロックの情報
	unsigned short *mat);

int decode_method5(	// 復元するブロックだけ保持する場合 (GPU対応)
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,	// パリティ・ブロックの情報
	unsigned short *mat);


#ifdef __cplusplus
}
#endif

#endif
