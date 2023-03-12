#ifndef _PAR2_H_
#define _PAR2_H_

#ifdef __cplusplus
extern "C" {
#endif


// パリティを作成する
int par2_create(
	wchar_t *uni_buf,	// 作業用、入力されたコメントが入ってる
	int packet_limit,	// リカバリ・ファイルのパケット繰り返しの制限数
	int block_distri,	// パリティ・ブロックの分配方法
	int switch_p);		// インデックス・ファイルを作らない, ユニコードのファイル名も記録する

// リカバリ・ファイルの構成を試算する
int par2_trial(
	wchar_t *uni_buf,	// 作業用、入力されたコメントが入ってる
	int packet_limit,	// リカバリ・ファイルのパケット繰り返しの制限数
	int block_distri,	// パリティ・ブロックの分配方法
	int switch_p);		// インデックス・ファイルを作らない, ユニコードのファイル名も記録する

// ソース・ファイルの破損や欠損を調べる
int par2_verify(wchar_t *uni_buf);	// 作業用

// ソース・ファイルの破損や欠損を修復する
int par2_repair(wchar_t *uni_buf);	// 作業用

// ソース・ファイルの一覧を表示する
int par2_list(
	wchar_t *uni_buf,	// 作業用
	int switch_h);		// ハッシュ値も表示する

// CRC-32 チェックサムを使って自分自身の破損を検出する
int par2_checksum(wchar_t *uni_buf);	// 作業用


#ifdef __cplusplus
}
#endif

#endif
