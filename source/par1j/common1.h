#ifndef _COMMON_H_
#define _COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_LEN			1024	// ファイル名の最大文字数 (末尾のNULL文字も含む)
#define ADD_LEN			8		// 作業中にファイル名に追加する文字数
#define EXT_LEN			16		// 拡張子として認識する最大文字数
#define COMMENT_LEN		128		// コメントの最大文字数
#define ALLOC_LEN		4096	// 可変長領域を何バイトごとに確保するか
#define IO_SIZE			65536
#define UPDATE_TIME		1024	// 更新間隔 ms


// グローバル変数
extern wchar_t recovery_file[MAX_LEN];	// リカバリ・ファイルのパス
extern wchar_t base_dir[MAX_LEN];		// ソース・ファイルの基準ディレクトリ
extern wchar_t ini_path[MAX_LEN];		// 検査結果ファイルのパス

extern unsigned int cp_output;

// Windown OS の precomposed UTF-16 から Console Output Code Page に変換する
int utf16_to_cp(wchar_t *in, char *out);

// UTF-16 のファイル・パスを画面出力用の Code Page を使って表示する
void printf_cp(unsigned char *format, wchar_t *path);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイル・パスがファイル・リスト上に既に存在するか調べる
int search_file_path(
	wchar_t *list,			// ファイル・リスト
	int total_len,			// ファイル・リストの文字数
	wchar_t *search_file);	// 検索するファイルのパス

// ファイル・リストの内容を並び替える
void sort_list(
	wchar_t *list,	// ファイル・リスト
	int total_len);	// ファイル・リストの文字数

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイル・パスからファイル名の位置を戻す
wchar_t * offset_file_name(wchar_t *file_path);

// ファイル・パスからファイル名だけ取り出す
void get_file_name(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *file_name);	// ファイル名

// ファイル・パスからディレクトリだけ取り出す、末尾は「\」か「/」
void get_base_dir(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *base_path);	// ディレクトリ

// 絶対パスかどうかを判定する
int is_full_path(wchar_t *path);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 相対パスを絶対パスに変換し、パスの先頭に "\\?\" を追加する
// 戻り値 : 0=エラー, 5～=新しいパスの長さ
int copy_path_prefix(
	wchar_t *new_path,	// 新しいパス
	int max_len,		// 新しいパスの最大長さ (末尾の null文字も含む)
	wchar_t *src_path,	// 元のパス (相対パスでもよい)
	wchar_t *dir_path);	// 相対パスの場合に基準となるディレクトリ (NULL ならカレント・ディレクトリ)

// ファイル・パスから、先頭にある "\\?\" を省いた長さを戻す
int len_without_prefix(wchar_t *file_path);

// ファイル・パスから、先頭にある "\\?\" を省いてコピーする
int copy_without_prefix(
	wchar_t *dst_path,	// コピー先
	wchar_t *src_path);	// コピー元のパス

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイルを置き換える (ファイル名の修正、テンポラリー・ファイルからの書き戻しなど)
int replace_file(
	wchar_t *dest_path,	// 置き換える先のパス (移動先、修正されたファイル名)
	wchar_t *sorc_path,	// 置き換える元のパス (移動元、現在のファイル名)
	int switch_b);		// 既存のファイルをバックアップするかどうか

// ファイルを指定サイズに縮小する
int shorten_file(
	wchar_t *file_path,	// ファイル・パス
	__int64 new_size,
	int switch_b);		// 既存のファイルをバックアップするかどうか

// ファイル名が有効か確かめて、問題があれば浄化する
int sanitize_filename(wchar_t *name);

// 修復中のテンポラリ・ファイルの名前を作る
void get_temp_name(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *temp_path);	// テンポラリ・ファイルのパス

void print_hash(unsigned char hash[16]);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern unsigned int memory_use;	// メモリー使用量 0=auto, 1～7 -> 1/8 ～ 7/8

// 空きメモリー量と制限値から使用できるメモリー量を計算する
unsigned int get_mem_size(unsigned __int64 data_size);

extern int prog_last;	// 前回と同じ進捗状況は出力しないので記録しておく

// 経過のパーセント表示、キャンセルと一時停止ができる
// 普段は 0 を返す、キャンセル時は 0以外
int print_progress(int prog_now);	// 表示する % 値
void print_progress_text(int prog_now, char *text);
int print_progress_file(int prog_now, wchar_t *file_name);
void print_progress_done(void);

// キャンセルと一時停止を行う
int cancel_progress(void);

// Win32 API のエラー・メッセージを表示する
void print_win32_err(void);

// ファイルをゴミ箱に移す
int delete_file_recycle(wchar_t *file_path);


#ifdef __cplusplus
}
#endif

#endif
