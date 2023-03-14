#ifndef _COMMON_H_
#define _COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WIN64	// 32-bit 版なら
#define MAX_CPU			8			// 32-bit 版は少なくしておく
#define MAX_MEM_SIZE	0x7F000000	// 確保するメモリー領域の最大値 2032MB
#define MAX_MEM_SIZE32	0x50000000	// 32-bit OS で確保するメモリー領域の最大値 1280MB
#else
#define MAX_CPU			16			// 最大 CPU/Core 個数 (スレッド本数)
#endif

#define MAX_LEN			1024		// ファイル名の最大文字数 (末尾のNULL文字も含む)
#define ADD_LEN			8			// 作業中にファイル名に追加する文字数
#define EXT_LEN			16			// 拡張子として認識する最大文字数
#define COMMENT_LEN		128			// コメントの最大文字数
#define ALLOC_LEN		16384		// 可変長文字列を何文字ごとに確保するか
#define IO_SIZE			131072		// 16384 以上にすること
#define STACK_SIZE		131072		// 65536 以上にすること
#define MAX_SOURCE_NUM	32768		// ソース・ブロック数の最大値
#define MAX_PARITY_NUM	65535		// パリティ・ブロック数の最大値
#define MAX_BLOCK_SIZE	0x7FFFFFFC	// 対応するブロック・サイズの最大値 2 GB
#define SEARCH_SIZE		1048576		// リカバリ・ファイルの検査単位
#define UPDATE_TIME		1024		// 更新間隔 ms


// グローバル変数
extern wchar_t recovery_file[MAX_LEN];	// リカバリ・ファイルのパス
extern wchar_t base_dir[MAX_LEN];		// ソース・ファイルの基準ディレクトリ
extern wchar_t ini_path[MAX_LEN];		// 検査結果ファイルのパス

extern int base_len;		// ソース・ファイルの基準ディレクトリの長さ
extern int recovery_limit;	// 作成時はリカバリ・ファイルのサイズ制限
extern int first_num;		// 作成時は最初のパリティ・ブロック番号、検査時は初めて見つけた数

extern int file_num;		// ソース・ファイルの数
extern int entity_num;		// 実体のあるファイルの数 (recovery set に含まれるファイル数)
extern int recovery_num;	// リカバリ・ファイルの数
extern int source_num;		// ソース・ブロックの数
extern int parity_num;		// パリティ・ブロックの数

extern unsigned int block_size;	// ブロック・サイズ
extern unsigned int split_size;	// 分割サイズ
extern __int64 total_file_size;	// 合計ファイル・サイズ

extern int switch_v;	// 検査レベル
extern int switch_b;	// バックアップを作るか

// 可変長サイズの領域にファイル名を記録する
extern wchar_t *list_buf;	// ソース・ファイルのファイル名のリスト
extern int list_len;		// ファイル・リストの文字数
extern int list_max;		// ファイル・リストの最大文字数

extern wchar_t *recv_buf;	// リカバリ・ファイルのリスト
extern int recv_len;		// ファイル・リストの文字数
extern wchar_t *recv2_buf;	// 有効なパケットを含むリカバリ・ファイルのリスト
extern int recv2_len;		// ファイル・リストの文字数

extern wchar_t *list2_buf;	// 指定された検査対象のファイル名のリスト
extern int list2_len;
extern int list2_max;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 作成時

// ソース・ファイルの情報
typedef struct {	// 44バイト
	unsigned char id[16];	// File ID
	unsigned char hash[16];	// ファイルの先頭 16KB のハッシュ値
	__int64 size;			// ファイル・サイズ (存在しない場合は -1)
	int name;				// ファイル名の開始位置
} file_ctx_c;

// ソース・ブロックの情報
typedef struct {	// 12バイト
	int file;				// 属するソース・ファイルの番号
	unsigned int size;		// ソース・ファイルからの読み込みサイズ (ブロック・サイズ以下)
	unsigned int crc;		// ソース・ブロックの CRC-32
} source_ctx_c;

// パリティ・ブロックの情報
typedef struct {	// 12バイト
	__int64 off;			// リカバリ・ファイル内での書き込み開始位置
	int file;				// 属するリカバリ・ファイルの番号
} parity_ctx_c;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 検査・修復時

// ソース・ファイルの情報
typedef struct {	// 72バイト
	unsigned char id[16];	// File ID
	unsigned char hash[32];	// ファイルのハッシュ値、と先頭 16KB のハッシュ値
	__int64 size;			// ファイル・サイズ
	int b_off;				// ソース・ブロックの開始番号
	int b_num;				// ソース・ブロックの個数
	int name;				// ファイル名の開始位置 (存在しない場合は -1 or 0)
	int name2;				// 名前が異なる場合のファイル名の開始位置
	unsigned int state;		// ファイルの状態
	// 128=そのファイルのチェックサムが存在しない
	// ファイルの状態 0=完全, 1=消失, 2=破損, 6=破損で上書き, 16=追加, 32=消失して別名&移動, 40=破損して別名&移動
	// フォルダの状態 64=存在, 65=消失, 96=別名&移動
} file_ctx_r;

// ソース・ブロックの情報
typedef struct {	// 36バイト
	int file;				// 属するソース・ファイルの番号
	unsigned int size;		// ソース・ファイルからの読み込みサイズ (ブロック・サイズ以下)
	unsigned int crc;		// ソース・ブロックの CRC-32
	unsigned char hash[20];	// ソース・ブロックの MD5 と CRC-32
	int exist;
	// 0=存在しない, 1=完全なファイル内に存在する, 2=破損ファイル内に存在する、またはエラー訂正済み
	// 3=内容は全て 0, 4=同じブロックが存在する, 5=CRCで内容を復元できる
	// 検査中にそのファイル内で見つかった場合は +0x1000 する (検査成功後に消す)
} source_ctx_r;

// パリティ・ブロックの情報
typedef struct {	// 16バイト
	__int64 off;			// リカバリ・ファイル内での読み込み開始位置
	int file;				// 属するリカバリ・ファイルの番号
	int exist;				// 0=存在しない, 1=存在する, 0x100=存在するけど利用せず
} parity_ctx_r;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern unsigned int cp_output;	// Console Output Code Page

// 指定された Code Page から UTF-16 に変換する
int cp_to_utf16(char *in, wchar_t *out, unsigned int cp);

// Windown OS の UTF-16 から指定された Code Page に変換する
int utf16_to_cp(wchar_t *in, char *out, unsigned int cp);

// Windows OS の UTF-8 から UTF-16 に変換する
void utf8_to_utf16(char *in, wchar_t *out);

// Windown OS の UTF-16 から UTF-8 に変換する
void utf16_to_utf8(wchar_t *in, char *out);

// 文字列が UTF-8 かどうかを判定する (0 = maybe UTF-8)
int check_utf8(unsigned char *text);

// ファイル・パスから、先頭にある "\\?\" を省いて、指定された Code Page に変換する
int path_to_cp(wchar_t *path, char *out, unsigned int cp);

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

// ファイルの指定バイト目から size バイトのデータを別のファイルに書き込む
int file_copy_data(
	HANDLE hFileRead,
	__int64 offset_read,
	HANDLE hFileWrite,
	__int64 offset_write,
	unsigned int size);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// スライス断片用の構造体
typedef struct {
	int id;			// ブロック番号
	int front_size;	// 前半の占有サイズ
	int rear_size;	// 後半の占有サイズ
} flake_ctx;

// スライス検査用の構造体
typedef struct {
	unsigned char *buf;		// ブロック・サイズ *3 の作業領域
	// ブロック比較用
	int *order;				// CRC-32 の順序を格納するバッファー
	int block_count;		// スライス検出で比較するブロックの数
	int index_shift;		// インデックス・サーチ用のシフト量
	int short_count;		// スライス検出で比較する半端なブロックの数
	unsigned int min_size;	// 半端なブロックの最小サイズ
	// 作業ファイル用
	int num;
	HANDLE hFile_tmp;
	int flake_count;		// スライス断片を何個記録してるか
	unsigned char *flk_buf;	// スライス断片の記録領域
	// IO のマルチスレッド用
	volatile unsigned int size;
	HANDLE volatile hFile;
	HANDLE h;
	HANDLE run;
	HANDLE end;
} slice_ctx;

// 修復中のテンポラリ・ファイルの名前を作る
void get_temp_name(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *temp_path);	// テンポラリ・ファイルのパス

// 作業用のゼロで埋められたテンポラリ・ファイルを作成する
int create_temp_file(
	wchar_t *file_path,		// ファイルのパス
	__int64 file_size);		// ファイルのサイズ

// 作業用のソース・ファイルを開く
HANDLE handle_temp_file(
	wchar_t *file_name,		// ソース・ファイル名
	wchar_t *file_path);	// 作業用、基準ディレクトリが入ってる

// 上書き用のソース・ファイルを開く
HANDLE handle_write_file(
	wchar_t *file_name,		// ソース・ファイル名
	wchar_t *file_path,		// 作業用、基準ディレクトリが入ってる
	__int64 file_size);		// 本来のファイルサイズ

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

// ソース・ファイルのリストに新しいファイル名を追加する
int add_file_path(wchar_t *filename);	// 追加するファイル名

// ファイル・リストから指定されたファイル・パスを取り除く
// 減らした後のファイル・リストの文字数を返す
int remove_file_path(
	wchar_t *list,	// ファイル・リスト
	int total_len,	// ファイル・リストの文字数
	int file_off);	// 取り除くファイル・パスの位置

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

// ファイルのディレクトリ位置が同じかどうかを調べる
int compare_directory(wchar_t *path1, wchar_t *path2);

// ワイルドカードを含む文字列をコピーする
int copy_wild(wchar_t *dst, wchar_t *src);

// ワイルドカード('*', '?')を使ってユニコードのパスを比較する
int PathMatchWild(
	wchar_t *text,	// 比較する文字列
	wchar_t *wild);	// ワイルドカード

// ファイルのパスを除外リストと比較する
int exclude_path(wchar_t *path);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 相対パスを絶対パスに変換し、パスの先頭に "\\?\" を追加する
// 戻り値 : 0=エラー, 5～=新しいパスの長さ
int copy_path_prefix(
	wchar_t *new_path,		// 新しいパス
	int max_len,			// 新しいパスの最大長さ (末尾の null文字も含む)
	wchar_t *src_path,		// 元のパス (相対パスでもよい)
	wchar_t *dir_path);		// 相対パスの場合に基準となるディレクトリ (NULL ならカレント・ディレクトリ)

// ファイル・パスから、先頭にある "\\?\" を省いた長さを戻す
int len_without_prefix(wchar_t *file_path);

// ファイル・パスから、先頭にある "\\?\" を省いてコピーする
int copy_without_prefix(
	wchar_t *dst_path,		// コピー先
	wchar_t *src_path);		// コピー元のパス

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 順番にサブ・ディレクトリを作成する
int make_dir(
	wchar_t *file_path);	// サブ・ディレクトリを含むファイル・パス

// ファイルを置き換える (ファイル名の修正、テンポラリー・ファイルからの書き戻しなど)
int replace_file(
	wchar_t *dest_path,		// 置き換える先のパス (移動先、修正されたファイル名)
	wchar_t *sorc_path);	// 置き換える元のパス (移動元、現在のファイル名)

// ファイルをどかす
void move_away_file(
	wchar_t *file_path);	// ファイル・パス

// ファイルを指定サイズに縮小する
int shorten_file(
	wchar_t *file_path,		// ファイル・パス
	__int64 new_size);

// ファイル名が有効かどうか調べる
int check_filename(
	wchar_t *name);		// 検査するファイル名

// ファイル名が有効か確かめて、問題があれば浄化する
int sanitize_filename(
	wchar_t *name,		// 検査するファイル名
	file_ctx_r *files,	// 各ソース・ファイルの情報
	int num);			// 比較から除外するファイル番号

// リカバリ・ファイルのパスから拡張子とボリューム番号を取り除く
void get_base_filename(
	wchar_t *file_path,		// リカバリ・ファイルのパス
	wchar_t *base_path,		// 基準ファイル名のパス
	wchar_t *file_ext);		// 拡張子 (存在するなら)

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ハッシュ値を表示する
void print_hash(unsigned char hash[16]);

// 64-bit 整数の平方根を求める
unsigned int sqrt64(__int64 num);

// 32-bit 整数の平方根を求める
int sqrt32(int num);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern int cpu_num;
extern unsigned int cpu_flag, cpu_cache;
extern unsigned int memory_use;	// メモリー使用量 0=auto, 1～7 -> 1/8 ～ 7/8

void check_cpu(void);
int check_OS64(void);

// 空きメモリー量と制限値から使用できるメモリー量を計算する
size_t get_mem_size(size_t trial_alloc);

// 記録装置の特性を調べる
int check_seek_penalty(wchar_t *dir_path);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern int prog_last;	// 前回と同じ進捗状況は出力しないので記録しておく
extern int count_last;

// 経過表示用
int print_progress(int prog_now);
void print_progress_text(int prog_now, char *text);
int print_progress_file(int prog_now, int count_now, wchar_t *file_name);
void print_progress_done(void);

// キャンセルと一時停止を行う
int cancel_progress(void);

// エラー発生時にキャンセルできるようにする
int error_progress(int error_now, int error_last);

// Win32 API のエラー・メッセージを表示する
void print_win32_err(void);

// ファイルをゴミ箱に移す
int delete_file_recycle(wchar_t *file_path);

// エクスプローラーで隠しファイルを表示する設定になってるか調べる
unsigned int get_show_hidden(void);


#ifdef __cplusplus
}
#endif

#endif
