
#define MAX_LEN			1024	// ファイル名の最大文字数 (末尾のNULL文字も含む)
#define ADD_LEN			32		// 作業中にファイル名に追加する文字数
#define EXT_LEN			16		// 拡張子として認識する最大文字数
#define COMMENT_LEN		128		// コメントの最大文字数
#define ALLOC_LEN		4096	// 可変長領域を何バイトごとに確保するか
#define IO_SIZE			65536	// 16384 以上にすること
#define UPDATE_TIME		1024	// 更新間隔 ms

// グローバル変数
extern wchar_t checksum_file[MAX_LEN];	// チェックサム・ファイルのパス
extern wchar_t base_dir[MAX_LEN];		// ソース・ファイルの基準ディレクトリ
extern wchar_t ini_path[MAX_LEN];		// 検査結果ファイルのパス

extern int base_len;		// ソース・ファイルの基準ディレクトリの長さ
extern int file_num;		// ソース・ファイルの数

// 可変長サイズの領域にテキストを保存する
extern wchar_t *text_buf;	// チェックサム・ファイルのテキスト内容
extern int text_len;		// テキストの文字数
extern int text_max;		// テキストの最大文字数

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern unsigned int cp_output;	// Console Output Code Page

// 指定された Code Page から UTF-16 に変換する
int cp_to_utf16(char *in, wchar_t *out, int max_size, unsigned int cp);

// Windown OS の UTF-16 から指定された Code Page に変換する
int utf16_to_cp(wchar_t *in, char *out, int max_size, unsigned int cp);

// 文字列が UTF-8 かどうかを判定する (0 = maybe UTF-8)
int check_utf8(unsigned char *text);

// 文字列が UTF-16 かどうかを判定して変換する
int utf16_to_utf16(unsigned char *in, int len, wchar_t *out);

// UTF-16 のファイル・パスを画面出力用の Code Page を使って表示する
void printf_cp(unsigned char *format, wchar_t *path);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイルの offset バイト目から size バイトのデータを buf に読み込む
int file_read_data(
	HANDLE hFileRead,
	__int64 offset,
	unsigned char *buf,
	unsigned int size);

// ファイルの offset バイト目に size バイトのデータを buf から書き込む
int file_write_data(
	HANDLE hFileWrite,
	__int64 offset,
	unsigned char *buf,
	unsigned int size);

// ファイルの offset バイト目に size バイトの指定値を書き込む
int file_fill_data(
	HANDLE hFileWrite,
	__int64 offset,
	unsigned char value,
	unsigned int size);

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

// テキストに新しい文字列を追加する
int add_text(wchar_t *new_text);	// 追加するテキスト

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

// ディレクトリ記号の「\」を「/」に置換する
void unix_directory(wchar_t *path);

// 絶対パスかどうかを判定する
int is_full_path(wchar_t *path);

// ファイル名が有効か確かめて、問題があれば浄化する
int sanitize_filename(wchar_t *name);

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

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ユニコードの16進数文字列から数値を読み取る
unsigned int get_val32h(wchar_t *s);

// 16進数の文字が何個続いてるか
unsigned int base16_len(wchar_t *s);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern int prog_last;	// 前回と同じ進捗状況は出力しないので記録しておく

// 経過のパーセント表示、キャンセルと一時停止ができる
// 普段は 0 を返す、キャンセル時は 0以外
int print_progress(int prog_now);
void print_progress_text(int prog_now, char *text);
int print_progress_file(int prog_now, wchar_t *file_name);
void print_progress_done(void);

// Win32 API のエラー・メッセージを表示する
void print_win32_err(void);

// エクスプローラーで隠しファイルを表示する設定になってるか調べる
unsigned int get_show_hidden(void);

