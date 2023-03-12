#ifndef _PAR1_H_
#define _PAR1_H_

#ifdef __cplusplus
extern "C" {
#endif


// リカバリ・ファイルを作成する
int par1_create(
	int switch_p,			// インデックス・ファイルを作らない
	int block_num,			// ソース・ブロックの数
	__int64 block_size,		// ブロック・サイズ (最大ファイル・サイズ)
	int parity_num,			// パリティ・ブロックの数
	int first_vol,			// 最初のリカバリ・ファイル番号
	int file_num,			// ソース・ファイルの数
	wchar_t *list_buf,		// ソース・ファイルのリスト
	int list_len,			// ファイル・リストの文字数
	wchar_t *par_comment);	// コメント

// ソース・ファイルの破損や欠損を調べる
int par1_verify(
	int switch_b,			// 既存のファイルを別名にしてどかす
	int switch_p,			// インデックス・ファイルを作り直す
	wchar_t *par_comment);	// コメント

// ソース・ファイルの破損や欠損を修復する
int par1_repair(
	int switch_b,			// 既存のファイルを別名にしてどかす
	int switch_p,			// インデックス・ファイルを作り直す
	wchar_t *par_comment);	// コメント

// ソース・ファイルの一覧を表示する
int par1_list(
	int switch_h,			// ハッシュ値も表示する
	wchar_t *par_comment);	// コメント

// CRC-32 チェックサムを使って自分自身の破損を検出する
int par1_checksum(wchar_t *uni_buf);	// 作業用


#ifdef __cplusplus
}
#endif

#endif
