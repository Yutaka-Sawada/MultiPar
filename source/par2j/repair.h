#ifndef _REPAIR_H_
#define _REPAIR_H_

#ifdef __cplusplus
extern "C" {
#endif


// ソース・ファイル情報を確認して集計する
int set_file_data(
	char *ascii_buf,	// 作業用
	file_ctx_r *files);

// ソース・ファイルの検査結果を集計して、修復方法を判定する
int result_file_state(
	char *ascii_buf,
	int *result,
	int parity_now,
	int recovery_lost,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 簡単な修復を行う
int simple_repair(
	char *ascii_buf,
	int need_repair,
	file_ctx_r *files);		// 各ソース・ファイルの情報

// 4バイトのソース・ブロックを逆算してソース・ファイルに書き込む
int restore_block4(
	wchar_t *file_path,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// ソース・ブロックを流用または逆算してソース・ファイルに書き込む
int restore_block(
	wchar_t *file_path,
	int reuse_num,			// 流用可能なソース・ブロックの数
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// 正しく修復できたか調べて結果表示する
int verify_repair(
	wchar_t *file_path,
	char *ascii_buf,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// 作業用のソース・ファイルを削除する
void delete_work_file(
	wchar_t *file_path,
	file_ctx_r *files);		// 各ソース・ファイルの情報

// ブロック単位の復元ができなくても、再構築したファイルで置き換える
void replace_incomplete(
	wchar_t *file_path,
	char *ascii_buf,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// リカバリ・ファイルを削除する（Useless状態だったのは無視する）
int purge_recovery_file(void);


#ifdef __cplusplus
}
#endif

#endif
