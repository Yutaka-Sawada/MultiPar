#ifndef _INI_H_
#define _INI_H_

#ifdef __cplusplus
extern "C" {
#endif


#define INI_NAME_LEN	38		// 検査結果ファイルのファイル名の文字数

extern int recent_data;

// リカバリ・ファイルの新規作成時に、同じ Set ID の記録があれば消去しておく
void reset_ini_file(unsigned char *set_id);

// 検査するリカバリ・ファイルが同じであれば、再検査する必要は無い
int check_ini_file(unsigned char *set_id);
void close_ini_file(void);
void write_ini_file2(unsigned char *par_client, wchar_t *par_comment);
void write_ini_file(file_ctx_r *files);
int read_ini_file(wchar_t *uni_buf, file_ctx_r *files);

int check_ini_recovery(
	HANDLE hFile,			// リカバリ・ファイルのハンドル
	unsigned int meta[7]);	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号

void write_ini_recovery(int id, __int64 off);

void write_ini_recovery2(
	int packet_count,		// そのリカバリ・ファイル内に含まれるパケットの数
	int block_count,		// そのリカバリ・ファイル内に含まれるパリティ・ブロックの数
	int bad_flag,
	unsigned int meta[7]);	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号

int read_ini_recovery(
	int num,
	int *packet_count,		// そのリカバリ・ファイル内に含まれるパケットの数
	int *block_count,		// そのリカバリ・ファイル内に含まれるパリティ・ブロックの数
	int *bad_flag,
	parity_ctx_r *p_blk);	// 各パリティ・ブロックの情報

// Input File Slice Checksum を書き込む
void write_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// Input File Slice Checksum を読み込む
int read_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// Input File Slice Checksum が不完全でも読み書きする
void update_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

void write_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	int result);			// 検査結果 0～=何ブロック目まで一致, -3=完全に一致

int check_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	HANDLE hFile);			// そのファイルのハンドル

void write_ini_complete(
	int num,				// ファイル番号
	wchar_t *file_path);	// ソース・ファイルの絶対パス

int check_ini_verify(
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	int num1,				// チェックサムを比較したファイルの番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc);			// スライス検査用の情報

void write_ini_verify(int id, int flag, __int64 off);

void write_ini_verify2(
	int num1,				// チェックサムを比較したファイルの番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	int max);				// 記録したブロック数

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 作業用のテンポラリ・ファイルを開く
int open_temp_file(
	wchar_t *temp_path,		// 作業用、基準ディレクトリが入ってる
	int num,				// 見つけたスライスが属するファイル番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	slice_ctx *sc);


#ifdef __cplusplus
}
#endif

#endif
