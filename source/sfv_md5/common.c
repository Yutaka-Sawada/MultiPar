// common.c
// Copyright : 2025-11-22 Yutaka Sawada
// License : The MIT license

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601	// Windows 7 or later
#endif

#include <conio.h>
#include <stdio.h>

#include <windows.h>
#include <shlobj.h>

#include "common.h"


// グローバル変数
wchar_t checksum_file[MAX_LEN];	// チェックサム・ファイルのパス
wchar_t base_dir[MAX_LEN];		// ソース・ファイルの基準ディレクトリ
wchar_t ini_path[MAX_LEN];		// 検査結果ファイルのパス

int base_len;		// ソース・ファイルの基準ディレクトリの長さ
int file_num;		// ソース・ファイルの数

// 可変長サイズの領域にテキストを保存する
wchar_t *text_buf;	// チェックサム・ファイルのテキスト内容
int text_len;		// テキストの文字数
int text_max;		// テキストの最大文字数

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

unsigned int cp_output;	// Console Output Code Page

// 指定された Code Page から UTF-16 に変換する
int cp_to_utf16(char *in, wchar_t *out, int max_size, unsigned int cp)
{
	int err = 0;
	unsigned int dwFlags = MB_ERR_INVALID_CHARS;	// 不適切な文字列を警告する

	if (cp == CP_UTF8)
		dwFlags = 0;

	if (MultiByteToWideChar(cp, dwFlags, in, -1, out, max_size) == 0){
		err = GetLastError();
		if ((dwFlags != 0) && (err == ERROR_INVALID_FLAGS)){
			//printf("MultiByteToWideChar, ERROR_INVALID_FLAGS\n");
			if (MultiByteToWideChar(cp, 0, in, -1, out, max_size) != 0)
				return 0;
			err = GetLastError();
		}
		wcscpy(out, L"cannot encode");
		return err;
	}
	return 0;
}

// Windown OS の UTF-16 から指定された Code Page に変換する
int utf16_to_cp(wchar_t *in, char *out, int max_size, unsigned int cp)
{
	unsigned int dwFlags = WC_NO_BEST_FIT_CHARS;	// 似た文字への自動変換を行わない

	if (cp == CP_UTF8)
		dwFlags = 0;

	if (WideCharToMultiByte(cp, dwFlags, in, -1, out, max_size, NULL, NULL) == 0){
		if ((dwFlags != 0) && (GetLastError() == ERROR_INVALID_FLAGS)){
			//printf("WideCharToMultiByte, ERROR_INVALID_FLAGS\n");
			if (WideCharToMultiByte(cp, 0, in, -1, out, max_size, NULL, NULL) != 0)
				return 0;
		}
		strcpy(out, "cannot encode");
		return 1;
	}
	return 0;
}

// 文字列が UTF-8 かどうかを判定する (0 = maybe UTF-8)
int check_utf8(unsigned char *text)
{
	unsigned char c1;
	int tail_len = 0;	// UTF8-tail が何バイト続くか

	while (*text != 0){
		c1 = *text;
		// 禁止 0xC0～0xC1，0xF5～0xFF
		if ((c1 == 0xC0) || (c1 == 0xC1) || (c1 >= 0xF5))
			return 1;	// 禁止文字
		if (tail_len == 0){	// 第1バイトなら
			// 1バイト文字
			if (c1 <= 0x7F){
				tail_len = 0;
			// 2バイト文字の第1バイト
			} else if ((c1 >= 0xC2) && (c1 <= 0xDF)){
				tail_len = 1;
			// 3バイト文字の第1バイト
			} else if ((c1 >= 0xE0) && (c1 <= 0xEF)){
				tail_len = 2;
			// 4バイト文字の第1バイト
			} else if (c1 >= 0xF0){
				tail_len = 3;
			} else {
				return 2;	//  第1バイトとして不適当な文字
			}
		} else {	// 第2バイト以後なら
			if ((c1 >= 0x80) && (c1 <= 0xBF)){
				tail_len--;
			} else {
				return 2;	// 第2バイト以後として不適当な文字
			}
		}
		text++;	// 次の文字へ
	}
	return 0;
}

// 文字列が UTF-16 かどうかを判定して変換する
int utf16_to_utf16(unsigned char *in, int len, wchar_t *out)
{
	unsigned char *text;
	int i;
	int little_endian = 0, space_mark = 0, return_mark = 0;

	if (len % 2)
		return 1;	// サイズは 2の倍数のはず

	// BOM を探す
	if ((in[0] == 0xFF) && (in[1] == 0xFE)){
		text = in + 2;
		little_endian = 1;	// UTF-16LE
	} else if ((in[0] == 0xFE) && (in[1] == 0xFF)){
		text = in + 2;
	} else {
		text = in;
	}

	// 必ず存在するはずのスペースと改行文字が一致するか調べる
	if (little_endian == 1){
		for (i = 0; i < len; i += 2){
			if ((text[i] == ' ') && (text[i + 1] == 0))
				space_mark++;
			if (((text[i] == '\n') || (text[i] == '\r'))&& (text[i + 1] == 0))
				return_mark++;
		}
	} else {
		for (i = 0; i < len; i += 2){
			if ((text[i + 1] == 0) && (text[i] == ' '))
				space_mark++;
			if ((text[i + 1] == 0) && ((text[i] == '\n') || (text[i] == '\r')))
				return_mark++;
		}
	}
	if ((space_mark == 0) && (return_mark == 0))
		return 1;

	// 変換する
	if (little_endian == 1){
		// Little Endian ならそのままコピーする
		memcpy(out, text, len);
	} else {
		// Big Endian なら上位と下位を入れ替える
		for (i = 0; i < len; i += 2){
			out[i / 2] = ((unsigned short)(text[i]) << 8) || ((unsigned short)(text[i + 1]));
		}
	}

	return 0;
}

// ファイル・パスから、先頭にある "\\?\" を省いて、指定された Code Page に変換する
int path_to_cp(wchar_t *path, char *out, unsigned int cp)
{
	unsigned int dwFlags = WC_NO_BEST_FIT_CHARS;	// 似た文字への自動変換を行わない

	if (cp == CP_UTF8)
		dwFlags = 0;

	if (wcsncmp(path, L"\\\\?\\", 4) == 0){	// "\\?\" を省く
		path += 4;
		if (wcsncmp(path, L"UNC\\", 4) == 0){	// "\\?\UNC" を省いて "\" を追加する
			path += 3;
			*out = '\\';
			out += 1;
		}
	}

	if (WideCharToMultiByte(cp, dwFlags, path, -1, out, MAX_LEN * 3, NULL, NULL) == 0){
		if ((dwFlags != 0) && (GetLastError() == ERROR_INVALID_FLAGS)){
			//printf("WideCharToMultiByte, ERROR_INVALID_FLAGS\n");
			if (WideCharToMultiByte(cp, 0, path, -1, out, MAX_LEN * 3, NULL, NULL) != 0)
				return 0;
		}
		strcpy(out, "cannot encode");
		return 1;
	}
	return 0;
}

// UTF-16 のファイル・パスを画面出力用の Code Page を使って表示する
void printf_cp(unsigned char *format, wchar_t *path)
{
	unsigned char buf[MAX_LEN * 3];
	path_to_cp(path, buf, cp_output);
	printf(format, buf);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイルの offset バイト目から size バイトのデータを buf に読み込む
int file_read_data(
	HANDLE hFileRead,
	__int64 offset,
	unsigned char *buf,
	unsigned int size)
{
	unsigned int rv;

	// ファイルの位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileRead, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// size バイトを読み込む
	if (!ReadFile(hFileRead, buf, size, &rv, NULL)){
		print_win32_err();
		return 1;
	}
	if (size != rv)
		return 1;	// 指定サイズを読み込めなかったらエラーになる

	return 0;
}

// ファイルの offset バイト目に size バイトのデータを buf から書き込む
int file_write_data(
	HANDLE hFileWrite,
	__int64 offset,
	unsigned char *buf,
	unsigned int size)
{
	unsigned int rv;

	// ファイルの位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileWrite, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// size バイトを書き込む
	if (!WriteFile(hFileWrite, buf, size, &rv, NULL)){
		print_win32_err();
		return 1;
	}

	return 0;
}

// ファイルの offset バイト目に size バイトの指定値を書き込む
int file_fill_data(
	HANDLE hFileWrite,
	__int64 offset,
	unsigned char value,
	unsigned int size)
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;

	// ファイルの位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileWrite, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// 指定された値で埋める
	memset(buf, value, IO_SIZE);
	while (size){
		len = IO_SIZE;
		if (size < IO_SIZE)
			len = size;
		size -= len;
		if (!WriteFile(hFileWrite, buf, len, &rv, NULL)){
			print_win32_err();
			return 1;
		}
	}

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイル・パスがファイル・リスト上に既に存在するか調べる
int search_file_path(
	wchar_t *list,			// ファイル・リスト
	int total_len,			// ファイル・リストの文字数
	wchar_t *search_file)	// 検索するファイルのパス
{
	int off = 0;

	while (off < total_len){
		if (_wcsicmp(list + off, search_file) == 0)
			return 1;
		// 次のファイル名の位置へ
		//off += (wcslen(list + off) + 1);
		while (list[off] != 0)
			off++;
		off++;
	}

	return 0;
}

// ファイル・リストの内容を並び替える
void sort_list(
	wchar_t *list,	// ファイル・リスト
	int total_len)	// ファイル・リストの文字数
{
	wchar_t *work_buf;
	int off1 = 0, off2, off3, work_off = 0;

	// 作業バッファーを確保する
	work_buf = (wchar_t *)calloc(total_len, 2);
	if (work_buf == NULL)
		return;	// 並べ替え失敗

	while (off1 < total_len){
		if (list[off1] == 0){
			break;
		} else if (list[off1] == '*'){	// 既にコピー済なら
			//off1 += (wcslen(list + off1) + 1);
			while (list[off1] != 0)
				off1++;
			off1++;
			continue;
		}
		off3 = off1;

		//off2 = off3 + wcslen(list + off3) + 1;	// 次の項目
		off2 = off3;
		while (list[off2] != 0)
			off2++;
		off2++;
		while (off2 < total_len){
//printf("%S (%d), %S (%d)\n", list + off3, off3, list + off2, off2);
			if (list[off2] != '*'){	// まだなら項目を比較する
				if (wcscmp(list + off3, list + off2) > 0)
					off3 = off2;
			}
			//off2 += (wcslen(list + off2) + 1);	// 次の項目
			while (list[off2] != 0)
				off2++;
			off2++;
		}

		// 順番にコピーしていく
//printf("get %S (%d)\n", list + off3, off3);
		wcscpy(work_buf + work_off, list + off3);
		work_off += (wcslen(work_buf + work_off) + 1);
		list[off3] = '*';	// コピーした印
		if (off3 == off1){
			//off1 += (wcslen(list + off1) + 1);
			while (list[off1] != 0)
				off1++;
			off1++;
		}
	}

	// 作業バッファーから戻す
	memcpy(list, work_buf, total_len * 2);
	free(work_buf);
}

// テキストに新しい文字列を追加する
int add_text(wchar_t *new_text)	// 追加するテキスト
{
	wchar_t *tmp_p;
	int len;

	len = wcslen(new_text);

	if (text_len + len >= text_max){	// 領域が足りなくなるなら拡張する
		text_max += ALLOC_LEN;
		tmp_p = (wchar_t *)realloc(text_buf, text_max * 2);
		if (tmp_p == NULL){
			return 1;
		} else {
			text_buf = tmp_p;
		}
	}

	wcscpy(text_buf + text_len, new_text);
	text_len += len;

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイル・パスからファイル名の位置を戻す
wchar_t * offset_file_name(wchar_t *file_path)
{
	int i;

	for (i = wcslen(file_path) - 2; i >= 0; i--){
		if ((file_path[i] == '\\') || (file_path[i] == '/'))
			break;
	}
	i++;
	return file_path + i;
}

// ファイル・パスからファイル名だけ取り出す
void get_file_name(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *file_name)		// ファイル名
{
	int i, len, find = -1;

	len = wcslen(file_path);
	for (i = len - 2; i >= 0; i--){
		if ((file_path[i] == '\\') || (file_path[i] == '/')){
			find = i;
			break;
		}
	}
	find++;
	for (i = find; i < len + 1; i++){
		file_name[i - find] = file_path[i];
	}
}

// ファイル・パスからディレクトリだけ取り出す、末尾は「\」か「/」
void get_base_dir(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *base_path)		// ディレクトリ
{
	int i, len, find = 0;

	len = wcslen(file_path);
	if (len <= 1){
		base_path[0] = 0;
		return;
	}

	// 最初から末尾が「\」か「/」なら
	if ((file_path[len - 1] == '\\') || (file_path[len - 1] == '/'))
		len--;	// それを取り除いてひとつ親のディレクトリを返す
	for (i = len - 1; i >= 0; i--){
		if (find == 0){
			if ((file_path[i] == '\\') || (file_path[i] == '/')){
				find = 1;
				base_path[i] = file_path[i];
				base_path[i + 1] = 0;
			}
		} else {
			base_path[i] = file_path[i];
		}
	}
	if (find == 0)
		base_path[0] = 0;
}

// ディレクトリ記号の「\」を「/」に置換する
void unix_directory(wchar_t *path)
{
	wchar_t *tmp_p;

	tmp_p = wcschr(path, '\\');
	while (tmp_p != NULL){
		*tmp_p = '/';
		tmp_p = wcschr(tmp_p, '\\');
	}
}

// 絶対パスかどうかを判定する
int is_full_path(wchar_t *path)
{
	if ((path[0] == 0) || (path[1] == 0))
		return 0;	// 2文字以下だと短すぎ

	// 通常のドライブ指定「c:\」の形式
	if ((path[1] == ':') && ((path[2] == '\\') || (path[2] == '/')))
		return 1;

	//「\\?\」が付くのは絶対パスだけ
	// ネットワークの UNC パスなら「\\<server>\<share>」
	if (((path[0] == '\\') || (path[0] == '/')) && ((path[1] == '\\') || (path[1] == '/')))
		return 1;

	return 0;
}

// デバイス名かどうかを判定する
static int check_device_name(wchar_t *name, int len)
{
	if (len >= 3){
		if ((name[3] == 0) || (name[3] == '.') || (name[3] == '\\')){
			if (_wcsnicmp(name, L"CON", 3) == 0)
				return 1;
			if (_wcsnicmp(name, L"PRN", 3) == 0)
				return 1;
			if (_wcsnicmp(name, L"AUX", 3) == 0)
				return 1;
			if (_wcsnicmp(name, L"NUL", 3) == 0)
				return 1;
		}
		if (len >= 4){
			if ((name[4] == 0) || (name[4] == '.') || (name[4] == '\\')){
				if (_wcsnicmp(name, L"COM", 3) == 0){
					if ((name[3] >= 0x31) && (name[3] <= 0x39))
						return 1;
				}
				if (_wcsnicmp(name, L"LPT", 3) == 0){
					if ((name[3] >= 0x31) && (name[3] <= 0x39))
						return 1;
				}
			}
		}
	}

	return 0;
}

// ファイル名が有効か確かめて、問題があれば浄化する
// ディレクトリ記号を「\」に統一する
// 戻り値 0=変更無し, +1=ディレクトリ記号, +2=浄化した, +4=文字数が変わった, +8=警告, 16=エラー
int sanitize_filename(wchar_t *name)
{
	int i, j, rv = 0, len = 0, off;

	// 制御文字 1～31 (改行やタブなど) を削除する
	while (len < MAX_LEN){
		if (name[len] == 0){
			break;
		} else if (name[len] < 32){
			i = len;
			do {
				name[i] = name[i + 1];
				i++;
			} while (name[i] != 0);
			rv |= 4;
		} else {
			len++;
		}
	}
	if (len == 0){
		name[0] = 0;
		return 16;
	}

	// WinOS ではファイル名に「\/:*?"<>|」の文字は使えないので置換する
	for (i = 0; i < len; i++){
		if ((name[i] == '*') || (name[i] == '?') || (name[i] == '"')
				|| (name[i] == '<') || (name[i] == '>') || (name[i] == '|')){
			name[i] = '_';	// 「*,?,",<,>,|」 ->「_」
			rv |= 2;
		} else if (name[i] == ':'){	// NTFS の「alternate data stream」の区切りに「:」が使われる
			for (j = i + 1; j < len; j++){
				// 「:」の後にディレクトリ記号や再度同じのが出現するのは駄目
				if ((name[j] == '\\') || (name[j] == '/') || (name[j] == ':')){
					j = -1;
					break;
				}
			}
			if ((i == 0) || (i == len - 1) || (j < 0)){
				name[i] = '_';	// 「:」 ->「_」
				rv |= 2;
			}
		} else if (name[i] == '/'){	// ディレクトリ記号を変換する
			name[i] = '\\';	// 「/」 -> 「\」
			rv |= 1;
		}
	}

	// 先頭の「 」、末尾の「.」「 」を警告する
	if ((name[0] == ' ') || (name[len - 1] == '.') || (name[len - 1] == ' '))
		rv |= 8;
	// 「\」の前後に「 」があれば警告する
	i = len - 1;
	while (i > 0){
		if ((name[i] == '\\') && ((name[i - 1] == ' ') || (name[i + 1] == ' ')))
			rv |= 8;
		i--;
	}

	// 「\」の前に「.」があれば削除する (ディレクトリ移動を防ぐ)
	// ただし、SFV/MD5 ではファイルを書き換えないので、親ディレクトリへの移動は許可する。
	off = 0;
	while ((name[off] == '.') && (name[off + 1] == '.') && (name[off + 2] == '\\'))
		off += 3;
	i = len - 1;
	while (i > off){	// 先に許可した所まで遡る。
		if ((name[i] == '\\') && (name[i - 1] == '.')){
			for (j = i - 1; j < len; j++)
				name[j] = name[j + 1];
			len--;
			rv |= 4;
		}
		i--;
	}

	// 連続した「\」を一文字にする
	i = 0;
	while (i < len){
		if ((name[i] == '\\') && (name[i + 1] == '\\')){
			for (j = i + 1; j < len; j++)
				name[j] = name[j + 1];
			len--;
			rv |= 4;
		} else {
			i++;
		}
	}

	// 先頭の「\」を削除する
	if (name[0] == '\\'){
		for (i = 0; i < len; i++)
			name[i] = name[i + 1];
		len--;
		rv |= 4;
	}

	// デバイス名を警告する (サブ・ディレクトリも含める)
	i = len;
	while (i > 0){
		i--;
		if ((i == 0) || ((i > 0) && (name[i - 1] == '\\'))){
			if (check_device_name(name + i, len) != 0)
				rv |= 8;
		}
	}

	if (len == 0){	// 最終的にファイル名が無くなればエラー
		name[0] = 0;
		return 16;
	}
	return rv;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define PREFIX_LEN	4	// 「\\?\」の長さ

// 相対パスを絶対パスに変換し、パスの先頭に "\\?\" を追加する
// 戻り値 : 0=エラー, 5～=新しいパスの長さ
int copy_path_prefix(
	wchar_t *new_path,		// 新しいパス
	int max_len,			// 新しいパスの最大長さ (末尾の null文字も含む)
	wchar_t *src_path,		// 元のパス (相対パスでもよい)
	wchar_t *dir_path)		// 相対パスの場合に基準となるディレクトリ (NULL ならカレント・ディレクトリ)
{
	wchar_t tmp_path[MAX_LEN], *src_name, *new_name;
	int len;

	// 不正なファイル名を拒否する (検索記号は許可する)
	if (src_path[0] == 0)
		return 0;
	if (wcspbrk(src_path, L"\"<>|"))	// WinOS ではファイル名に「\/:*?"<>|」の文字は使えない
		return 0;

	// 不適切な名前のファイルは、変換時に浄化されることがある
	src_name = offset_file_name(src_path);

	// 相対パスに対して基準となるディレクトリが指定されてるなら
	if ((dir_path != NULL) && (is_full_path(src_path) == 0)){
		// そのまま基準ディレクトリを連結して絶対パスにする
		len = wcslen(dir_path);
		if (len + (int)wcslen(src_path) >= max_len)
			return 0;
		wcscpy(tmp_path, dir_path);	// dir_path の末尾は「\」にしておくこと
		wcscpy(tmp_path + len, src_path);
		if (GetFullPathName(tmp_path, max_len, new_path, &new_name) == 0)
			return 0;
	} else {	// カレント・ディレクトリを使う
		// 相対パスから絶対パスに変換する (ディレクトリ記号も「\」に統一される)
		if (GetFullPathName(src_path, max_len, new_path, &new_name) == 0)
			return 0;
	}

	// 元のファイル名と比較して、浄化を取り消す
	if (new_name == NULL)
		new_name = offset_file_name(new_path);
	if (_wcsicmp(src_name, new_name) != 0){
		len = (int)wcslen(new_name);
		if ((_wcsnicmp(src_name, new_name, len) == 0) && ((src_name[len] == ' ') || (src_name[len] == '.'))){
			len = (int)wcslen(src_name);
			if ((src_name[len - 1] == ' ') || (src_name[len - 1] == '.')){
				// 末尾の「 」や「.」が取り除かれてるなら元に戻す
				wcscpy(new_name, src_name);
			}
		}
	}

	// 先頭に "\\?\" が存在しないなら、追加する
	if ((wcsncmp(new_path, L"\\\\?\\", PREFIX_LEN) != 0)){
		len = wcslen(new_path);
		if ((new_path[0] == '\\') && (new_path[1] == '\\')){	// UNC パスなら
			if (len + PREFIX_LEN + 2 >= max_len)
				return 0;	// バッファー・サイズが足りなくて追加できない
			memmove(new_path + (PREFIX_LEN + 3), new_path + 1, len * 2);
			new_path[PREFIX_LEN    ] = 'U';
			new_path[PREFIX_LEN + 1] = 'N';
			new_path[PREFIX_LEN + 2] = 'C';
		} else {	// 通常のドライブ記号で始まるパスなら
			if (len + PREFIX_LEN >= max_len)
				return 0;	// バッファー・サイズが足りなくて追加できない
			memmove(new_path + PREFIX_LEN, new_path, (len + 1) * 2);
		}
		memcpy(new_path, L"\\\\?\\", PREFIX_LEN * 2);
	}

	// 8.3形式の短いファイル名を長いファイル名に変換する
	if (GetFileAttributes(new_path) == INVALID_FILE_ATTRIBUTES){
		// 指定されたファイルが存在しない場合 (作成するPARファイルや「*?」で検索するソース・ファイル)
		int name_len;
		// 区切り文字を探す
		name_len = wcslen(new_path);
		for (len = name_len - 1; len >= 0; len--){
			if (new_path[len] == '\\')
				break;
		}
		len++;
		wcscpy(tmp_path, new_path + len);	// 存在しないファイル名を記録しておく
		name_len -= len;
		new_path[len] = 0;	// ファイル名を消す
		len = GetLongPathName(new_path, new_path, max_len);
		if (len == 0){	// 変換エラー
			//printf("GetLongPathName (not exist) : err = %d\n", GetLastError());
			//print_win32_err();
			return 0;
		} else {	// 退避させておいたファイル名を追加する
			len += name_len;
			if (len < max_len)
				wcscat(new_path, tmp_path);
		}
	} else {
		len = GetLongPathName(new_path, new_path, max_len);
		if (len == 0){	// 変換エラー
			//printf("GetLongPathName (exist) : err = %d\n", GetLastError());
			return 0;
		}
	}
	if (len >= max_len)
		return 0;

	return len;
}

// ファイル・パスから、先頭にある "\\?\" を省いた長さを戻す
int len_without_prefix(wchar_t *file_path)
{
	// 最初から "\\?\" が無ければ
	if (wcsncmp(file_path, L"\\\\?\\", PREFIX_LEN) != 0)
		return wcslen(file_path);	// そのままの長さ

	// "\\?\UNC\<server>\<share>" ならネットワーク・パス
	if (wcsncmp(file_path + PREFIX_LEN, L"UNC\\", 4) == 0)
		return wcslen(file_path + PREFIX_LEN + 2);	// "\\?\UNC\" -> "\\" なので6文字減らす

	// "\\?\<drive:>\<path>" なら
	return wcslen(file_path + PREFIX_LEN);	// "\\?\" を省くので4文字減らす
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ユニコードの16進数文字列から数値を読み取る
unsigned int get_val32h(wchar_t *s){
	int i;
	unsigned int v, ret = 0;

	for (i = 0; i < 8; i++){
		v = s[i];
		if ((v >= 0x30) && (v <= 0x39)){		// 0 to 9
			ret = (ret * 16) + (v - 0x30);
		} else if ((v >= 0x61) && (v <= 0x66)){	// a to f
			ret = (ret * 16) + (v + 10 - 0x61);
		} else if ((v >= 0x41) && (v <= 0x46)){	// A to F
			ret = (ret * 16) + (v + 10 - 0x41);
		} else {	// 数字以外の文字に出会ったら終わる
			break;
		}
	}
	return ret;
}

// 16進数の文字が何個続いてるか
unsigned int base16_len(wchar_t *s)
{
	unsigned int v, len;

	len = 0;
	while (len <= 32){
		v = s[len];
		if ((v >= 0x30) && (v <= 0x39)){		// 0 to 9
			len++;
		} else if ((v >= 0x61) && (v <= 0x66)){	// a to f
			len++;
		} else if ((v >= 0x41) && (v <= 0x46)){	// A to F
			len++;
		} else {	// 数字以外の文字に出会ったら終わる
			break;
		}
	}
	return len;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define MAX_NAME_LEN	69	// 経過表示のタイトルの最大文字数 (末尾の null 文字を含む)

int prog_last;	// 前回と同じ進捗状況は出力しないので記録しておく

// ファイル・パスを短縮されたファイル名だけにしてコピーする
// ASCII 文字は 1文字, それ以外は 2文字として数えることに注意！
static void copy_filename(wchar_t *out, wchar_t *in)
{
	int i, len, name_off, ext_off, ext_len;

	// ファイル名部分の文字数を数える
	name_off = 0;
	ext_off = 0;
	len = 0;
	for (i = 0; in[i] != 0; i++){
		if ((in[i] == '\\') && (in[i + 1] != 0)){
			len = 0;
			name_off = i + 1;
		} else {
			if (in[i] < 0x80){
				len += 1;
				if ((in[i] == '.') && (in[i + 1] != 0))
					ext_off = i;
			} else {
				len += 2;
				ext_off = 0;	// 拡張子は ASCII文字だけにする
			}
		}
	}
	if (len < MAX_NAME_LEN){
		wcscpy(out, in + name_off);
		return;
	}

	// 拡張子の長さを調べる
	ext_len = 0;
	if (ext_off > name_off){
		while (in[ext_off + ext_len] != 0)
			ext_len++;
		if (ext_len >= EXT_LEN)
			ext_len = 0;
	}

	// ファイル名が長すぎる場合は、末尾を省いて拡張子を追加する
	len = 0;
	for (i = 0; in[name_off + i] != 0; i++){
		if (in[name_off + i] < 0x80){
			if (len >= MAX_NAME_LEN - 2 - ext_len)
				break;
			len += 1;
		} else {
			if (len >= MAX_NAME_LEN - 3 - ext_len)
				break;
			len += 2;
		}
		out[i] = in[name_off + i];
	}
	out[i++] = '~';
	while (ext_len > 0){
		out[i++] = in[ext_off++];
		ext_len--;
	}
	out[i] = 0;
}

// 経過のパーセントを表示する
// 普段は 0 を返す、キャンセル時は 0以外
int print_progress(int prog_now)	// 表示する % 値
{
	if (prog_now < 0)	// 範囲外なら
		return 0;

	if (_kbhit()){	// キー入力があるか
		int ch = _getch();
		if ((ch == 'c') || (ch == 'C')){	// Cancel
			printf("\nCancel\n");
			return 2;
		}

		// 一時停止と再開の処理
		if ((ch == 'p') || (ch == 'P')){	// Pause
			printf(" Pause\r");	// パーセントを上書きする
			do {
				ch = _getch();	// 再度入力があるまで待つ、CPU 占有率 0%
				if ((ch == 'c') || (ch == 'C')){	// 停止中でもキャンセルは受け付ける
					printf("\nCancel\n");
					return 2;
				}
			} while ((ch != 'r') && (ch != 'R'));	// Resume
			prog_last = -1;	// Pause の文字を上書きする
		}
	}

	// 前回と同じ進捗状況は出力しない
	if (prog_now == prog_last)
		return 0;
	printf("%3d.%d%%\r", prog_now / 10, prog_now % 10);
	prog_last = prog_now;
	fflush(stdout);

	return 0;
}

// 経過のパーセントとテキストを表示する
void print_progress_text(int prog_now, char *text)
{
	if (prog_now < 0)	// 範囲外なら
		return;
	printf("%3d.%d%% : %s\r", prog_now / 10, prog_now % 10, text);
	prog_last = prog_now;
	fflush(stdout);
}

// 経過のパーセントやファイル名を表示する
int print_progress_file(int prog_now, wchar_t *file_name)
{
	if (prog_now < 0)	// 範囲外なら
		return 0;

	if (_kbhit()){	// キー入力があるか
		int ch = _getch();
		if ((ch == 'c') || (ch == 'C')){	// Cancel
			if (prog_last >= 0)
				printf("\n");
			printf("Cancel\n");
			return 2;
		}

		// 一時停止と再開の処理
		if ((ch == 'p') || (ch == 'P')){	// Pause
			printf(" Pause\r");	// パーセントを上書きする
			do {
				ch = _getch();	// 再度入力があるまで待つ、CPU 占有率 0%
				if ((ch == 'c') || (ch == 'C')){	// 停止中でもキャンセルは受け付ける
					printf("\nCancel\n");
					return 2;
				}
			} while ((ch != 'r') && (ch != 'R'));	// Resume
			if (prog_now == prog_last)
				prog_last = prog_now + 1;	// Pause の文字を上書きする
		}
	}

	if (prog_last < 0){	// 初めて経過を表示する時だけファイル名を表示する
		char text[MAX_NAME_LEN * 3];
		wchar_t short_name[MAX_NAME_LEN];

		copy_filename(short_name, file_name);
		utf16_to_cp(short_name, text, sizeof(text), cp_output);
		printf("%3d.%d%% : \"%s\"\r", prog_now / 10, prog_now % 10, text);
	} else if (prog_now == prog_last){	// 前回と同じ進捗状況は出力しない
		return 0;
	} else {
		printf("%3d.%d%%\r", prog_now / 10, prog_now % 10);
	}
	prog_last = prog_now;
	fflush(stdout);

	return 0;
}

void print_progress_done(void)	// 終了と改行を表示する
{
	if (prog_last >= 0){	// そもそも経過表示がなかった場合は表示しない
		if (prog_last != 1000){
			printf("100.0%%\n");
		} else {
			printf("\n");
		}
		fflush(stdout);
		prog_last = -1;	// 進捗状況をリセットする
	}
}

// キャンセルと一時停止を行う
int cancel_progress(void)
{
	if (_kbhit()){	// キー入力があるか
		int ch = _getch();
		if ((ch == 'c') || (ch == 'C')){	// Cancel
			printf("Cancel\n");
			return 2;
		}

		// 一時停止と再開の処理
		if ((ch == 'p') || (ch == 'P')){	// Pause
			printf(" Pause\r");
			do {
				ch = _getch();	// 再度入力があるまで待つ、CPU 占有率 0%
				if ((ch == 'c') || (ch == 'C')){	// 停止中でもキャンセルは受け付ける
					printf("Cancel\n");
					return 2;
				}
			} while ((ch != 'r') && (ch != 'R'));	// Resume
		}
	}

	return 0;
}

// Win32 API のエラー・メッセージを表示する
void print_win32_err(void)
{
	unsigned int en;
	LPVOID lpMsgBuf = NULL;

	en = GetLastError();
	if (cp_output == CP_UTF8){
		if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
				NULL, en, 0, (LPWSTR) &lpMsgBuf, 0, NULL) > 0){
			char buf[MAX_LEN * 3];
			// エンコードを UTF-16 から UTF-8 に変換する
			utf16_to_cp(lpMsgBuf, buf, sizeof(buf), CP_UTF8);
			printf("0x%X, %s\n", en, buf);
		}
	} else {
		if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
				NULL, en, 0, (LPSTR) &lpMsgBuf, 0, NULL) > 0){
			printf("0x%X, %s\n", en, (char *)lpMsgBuf);
		}
	}
	if (lpMsgBuf != NULL)
		LocalFree(lpMsgBuf);
}

// エクスプローラーで隠しファイルを表示する設定になってるか調べる
unsigned int get_show_hidden(void)
{
	unsigned int rv;
	SHELLSTATE ssf;

	// Explorer の設定を調べる
	SHGetSetSettings(&ssf, SSF_SHOWALLOBJECTS | SSF_SHOWSUPERHIDDEN, FALSE);
	// 隠しファイルを表示するかどうか
	if (ssf.fShowAllObjects){	// 表示する設定なら
		// 保護されたオペレーティングシステムファイルを表示するかどうか
		if (ssf.fShowSuperHidden){	// 表示する設定なら
			rv = 0;
		} else {	// 隠し属性とシステム属性の両方で判定する
			rv = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
		}
	} else {	// 隠しファイルを表示しない場合は、隠し属性だけで判定する
		rv = FILE_ATTRIBUTE_HIDDEN;
	}

	return rv;
}

