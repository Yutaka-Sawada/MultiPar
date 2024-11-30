// common.c
// Copyright : 2024-11-30 Yutaka Sawada
// License : GPL

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
#include <shlwapi.h>

#include "common1.h"


// グローバル変数
wchar_t recovery_file[MAX_LEN];	// リカバリ・ファイルのパス
wchar_t base_dir[MAX_LEN];		// ソース・ファイルの基準ディレクトリ
wchar_t ini_path[MAX_LEN];		// 検査結果ファイルのパス

unsigned int cp_output;

/*
// Console Input Code Page から Windown OS の precomposed UTF-16 に変換する
int cp_to_utf16(char *in, wchar_t *out)
{
	if (!MultiByteToWideChar(cp_input, 0, in, -1, out, MAX_LEN)){
		wcscpy(out, L"cannot encode");
		return 1;
	}
	return 0;
}
*/

// Windown OS の precomposed UTF-16 から Console Output Code Page に変換する
int utf16_to_cp(wchar_t *in, char *out)
{
	if (cp_output == CP_UTF8){
		if (WideCharToMultiByte(CP_UTF8, 0, in, -1, out, MAX_LEN * 3, NULL, NULL) == 0){
			strcpy(out, "cannot encode");
			return 1;
		}
		return 0;
	}

	// 似た文字への自動変換を行わない
	if (!WideCharToMultiByte(cp_output, WC_NO_BEST_FIT_CHARS, in, -1, out, MAX_LEN * 2, NULL, NULL)){
		strcpy(out, "cannot encode");
		return 1;
	}
	return 0;
}

// ファイル・パスから、先頭にある "\\?\" を省いて、Console Output Code Page に変換する
int path_to_cp(wchar_t *path, char *out)
{
	unsigned int dwFlags = WC_NO_BEST_FIT_CHARS;	// 似た文字への自動変換を行わない

	if (cp_output == CP_UTF8)
		dwFlags = 0;

	if (wcsncmp(path, L"\\\\?\\", 4) == 0){	// "\\?\" を省く
		path += 4;
		if (wcsncmp(path, L"UNC\\", 4) == 0){	// "\\?\UNC" を省いて "\" を追加する
			path += 3;
			*out = '\\';
			out += 1;
		}
	}

	if (WideCharToMultiByte(cp_output, dwFlags, path, -1, out, MAX_LEN * 3, NULL, NULL) == 0){
		strcpy(out, "cannot encode");
		return 1;
	}
	return 0;
}

// UTF-16 のファイル・パスを画面出力用の Code Page を使って表示する
void printf_cp(unsigned char *format, wchar_t *path)
{
	unsigned char buf[MAX_LEN * 3];
	path_to_cp(path, buf);
	printf(format, buf);
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
				if (wcscmp(list + off3, list + off2) > 0)	// 大文字と小文字を区別する
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

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define PREFIX_LEN	4	// 「\\?\」の長さ

// 相対パスを絶対パスに変換し、パスの先頭に "\\?\" を追加する
// 戻り値 : 0=エラー, 5～=新しいパスの長さ
int copy_path_prefix(
	wchar_t *new_path,	// 新しいパス
	int max_len,		// 新しいパスの最大長さ (末尾の null文字も含む)
	wchar_t *src_path,	// 元のパス (相対パスでもよい)
	wchar_t *dir_path)	// 相対パスの場合に基準となるディレクトリ (NULL ならカレント・ディレクトリ)
{
	wchar_t tmp_path[MAX_LEN], *src_name, *new_name;
	int len;

	// 不正なファイル名を拒否する (検索記号は許可する)
	if (src_path[0] == 0)
		return 0;
	if (wcspbrk(src_path, L"\"<>|"))	// WinOS ではファイル名に「"<>|」の文字は使えない
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

// ファイル・パスから、先頭にある "\\?\" を省いてコピーする
int copy_without_prefix(
	wchar_t *dst_path,	// コピー先 (コピー元と同じアドレスでもよい)
	wchar_t *src_path)	// コピー元のパス
{
	int i, len;

	len = wcslen(src_path);	// 元の文字数

	// 最初から "\\?\" が無ければ
	if (wcsncmp(src_path, L"\\\\?\\", PREFIX_LEN) != 0){
		for (i = 0; i <= len; i++)
			dst_path[i] = src_path[i];	// そのままコピーする
		return len;
	}

	// "\\?\UNC\<server>\<share>" ならネットワーク・パス
	if (wcsncmp(src_path + PREFIX_LEN, L"UNC\\", 4) == 0){
		len -= PREFIX_LEN + 2;	// "\\?\UNC\" -> "\\" なので6文字減らす
		for (i = 1; i <= len; i++)
			dst_path[i] = src_path[PREFIX_LEN + 2 + i];
		dst_path[0] = '\\';	// 先頭を "\\" に変えておく
		return len;
	}

	// "\\?\<drive:>\<path>" なら
	len -= PREFIX_LEN;	// "\\?\UNC\" -> "\\" なので6文字減らす
	for (i = 0; i <= len; i++)
		dst_path[i] = src_path[PREFIX_LEN + i];
	return len;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイルを置き換える (ファイル名の修正、テンポラリー・ファイルからの書き戻しなど)
int replace_file(
	wchar_t *dest_path,	// 置き換える先のパス (移動先、修正されたファイル名)
	wchar_t *sorc_path,	// 置き換える元のパス (移動元、現在のファイル名)
	int switch_b)		// 既存のファイルをバックアップするかどうか
{
	unsigned int rv, num;

	// 移動元のファイルが存在するかどうか
	num = GetFileAttributes(sorc_path);
	if (num == INVALID_FILE_ATTRIBUTES){
		return 1;	// 移動させるファイルが存在しない
	} else if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性なら、その属性を解除する
		if (SetFileAttributes(sorc_path, num ^ FILE_ATTRIBUTE_READONLY) == 0)
			return 2;	// 移動元の読み取り専用属性を解除できなかった
	}

	// 移動先のファイルが存在するかどうか
	rv = GetFileAttributes(dest_path);
	if (rv == INVALID_FILE_ATTRIBUTES){	// ファイルが存在しないなら、そのまま移動する
		if (MoveFile(sorc_path, dest_path) == 0)
			return 4;	// 移動失敗
		if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
			rv = GetFileAttributes(dest_path);
			SetFileAttributes(dest_path, rv | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
		}

	} else {	// 既にファイルが存在するなら
		wchar_t back_path[MAX_LEN];

		back_path[0] = 0;
		if (switch_b){	// 既存のファイルを削除せずに別名で残す
			for (num = 1; num < 100; num++){
				swprintf(back_path, _countof(back_path), L"%s.%d", dest_path, num);	// バックアップ・ファイルのパス
				if (GetFileAttributes(back_path) == INVALID_FILE_ATTRIBUTES)
					break;	// 存在しないなら OK
			}
			if (num >= 100)
				back_path[0] = 0;	// バックアップがいっぱいならそれ以上作らない
		}
		if (back_path[0] != 0){	// バックアップを作って置き換える
			num = ReplaceFile(dest_path, sorc_path, back_path, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
			if (num == 0){
				num = GetLastError();
				if ((num == 32) ||	// The process cannot access the file because it is being used by another process.
						(num == 1175) ||	// ERROR_UNABLE_TO_REMOVE_REPLACED
						(num == 1176)){		// ERROR_UNABLE_TO_MOVE_REPLACEMENT
					Sleep(100);
					num = ReplaceFile(dest_path, sorc_path, back_path, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
				} else if (num == 1177){	// ERROR_UNABLE_TO_MOVE_REPLACEMENT_2
					Sleep(100);
					num = MoveFile(sorc_path, dest_path);	// 既存ファイルはバックアップ済みなら、移動するだけ
				} else {
					num = 0;
				}
				if (num == 0)
					return 6;
			}
			if (switch_b & 2)	// バックアップをゴミ箱に入れる
				delete_file_recycle(back_path);
		} else {	// 既存のファイルを削除して置き換える
			if (rv & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性なら、一旦その属性を解除する
				if (SetFileAttributes(dest_path, rv ^ FILE_ATTRIBUTE_READONLY) == 0)
					return 7;	// 移動先の読み取り専用属性を解除できなかった = 削除もできない
			}
			num = ReplaceFile(dest_path, sorc_path, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
			if (num == 0){
				num = GetLastError();
				if ((num == 32) ||	// The process cannot access the file because it is being used by another process.
						(num == 1175)){	// ERROR_UNABLE_TO_REMOVE_REPLACED
					Sleep(100);
					num = ReplaceFile(dest_path, sorc_path, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
				} else if (num == 1176){	// ERROR_UNABLE_TO_MOVE_REPLACEMENT
					Sleep(100);
					num = MoveFile(sorc_path, dest_path);	// 既存ファイルは削除済みなら、移動するだけ
				} else {
					num = 0;
				}
				if (num == 0)
					return 8;
			}
			if (rv & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
				rv = GetFileAttributes(dest_path);
				SetFileAttributes(dest_path, rv | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
			}
		}
	}

	return 0;
}

// ファイルを指定サイズに縮小する
int shorten_file(
	wchar_t *file_path,		// ファイル・パス
	__int64 new_size,
	int switch_b)			// 既存のファイルをバックアップするかどうか
{
	wchar_t back_path[MAX_LEN];
	unsigned int rv, len, num;
	HANDLE hFileWrite;
	LARGE_INTEGER qwi;	// Quad Word Integer

	back_path[0] = 0;
	if (switch_b){	// 既存のファイルを削除せずに別名で残す
		for (num = 1; num < 100; num++){
			swprintf(back_path, _countof(back_path), L"%s.%d", file_path, num);	// バックアップ・ファイルのパス
			if (GetFileAttributes(back_path) == INVALID_FILE_ATTRIBUTES)
				break;	// 存在しないなら OK
		}
		if (num >= 100)
			back_path[0] = 0;	// バックアップがいっぱいならそれ以上作らない
	}

	if (back_path[0] != 0){	// バックアップを作って置き換える
		wchar_t temp_path[MAX_LEN];
		// テンポラリー・ファイルを作ってファイル内容をコピーする
		get_temp_name(file_path, temp_path);
		hFileWrite = CreateFile(temp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFileWrite == INVALID_HANDLE_VALUE){	// 作れなかったら
			back_path[0] = 0;
		} else if (new_size <= 0){
			CloseHandle(hFileWrite);
		} else {
			// ファイル・サイズを指定サイズにする
			qwi.QuadPart = new_size;
			if (!SetFilePointerEx(hFileWrite, qwi, NULL, FILE_BEGIN)){
				back_path[0] = 0;
			} else if (!SetEndOfFile(hFileWrite)){
				back_path[0] = 0;
			} else {
				// ポインターを先頭に戻す
				qwi.QuadPart = 0;
				if (!SetFilePointerEx(hFileWrite, qwi, NULL, FILE_BEGIN)){
					back_path[0] = 0;
				} else {
					__int64 left_size = new_size;
					HANDLE hFileRead;
					hFileRead = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFileRead == INVALID_HANDLE_VALUE){
						back_path[0] = 0;
					} else {	// 指定サイズまでコピーする
						unsigned char buf[IO_SIZE];
						while (left_size > 0){
							len = IO_SIZE;
							if (left_size < IO_SIZE)
								len = (unsigned int)left_size;
							left_size -= len;
							if (!ReadFile(hFileRead, buf, len, &rv, NULL) || (len != rv)){
								back_path[0] = 0;
								break;
							}
							if (!WriteFile(hFileWrite, buf, len, &rv, NULL)){
								back_path[0] = 0;
								break;
							}
						}
						CloseHandle(hFileRead);
					}
				}
			}
			CloseHandle(hFileWrite);
			if (back_path[0] == 0)	// 失敗したらテンポラリー・ファイルは削除する
				DeleteFile(temp_path);
		}

		if (back_path[0] != 0){	// バックアップを作って置き換える
			if (ReplaceFile(file_path, temp_path, back_path, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0) == 0){
				DeleteFile(temp_path);
				return 1;
			}
			if (switch_b & 2)	// バックアップをゴミ箱に入れる
				delete_file_recycle(back_path);
			return 0;
		}
	}

	num = GetFileAttributes(file_path);
	if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性なら、一旦その属性を解除する
		if (SetFileAttributes(file_path, num ^ FILE_ATTRIBUTE_READONLY) == 0)
			return 1;	// 読み取り専用属性を解除できなかった = 縮小もできない
	}

	// 直接開いて縮小する
	hFileWrite = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFileWrite == INVALID_HANDLE_VALUE)
		return 1;
	// ファイル・サイズを指定サイズにする
	qwi.QuadPart = new_size;
	if (!SetFilePointerEx(hFileWrite, qwi, NULL, FILE_BEGIN)){
		CloseHandle(hFileWrite);
		return 1;
	}
	if (!SetEndOfFile(hFileWrite)){
		CloseHandle(hFileWrite);
		return 1;
	}
	CloseHandle(hFileWrite);

	if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
		num = GetFileAttributes(file_path);
		SetFileAttributes(file_path, num | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
	}

	return 0;
}

// デバイス名かどうかを判定する
static int check_device_name(wchar_t *name, int len)
{
	if (len >= 3){
		if ((name[3] == 0) || (name[3] == '.')){
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
			if ((name[4] == 0) || (name[4] == '.')){
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
// 戻り値 0=変更無し, +2=浄化した, +4=文字数が変わった, +8=警告, 16=エラー
int sanitize_filename(wchar_t *name)
{
	int i, rv = 0, len = 0;

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
	if (len == 0)
		return 16;

	// WinOS ではファイル名に「\/:*?"<>|」の文字は使えないので置換する
	for (i = 0; i < len; i++){
		if ((name[i] == '\\') || (name[i] == '/') || (name[i] == ':')
				|| (name[i] == '*') || (name[i] == '?') || (name[i] == '"')
				|| (name[i] == '<') || (name[i] == '>') || (name[i] == '|')){
			name[i] = '_';	// 「\,/,:,*,?,",<,>,|」 ->「_」
			rv |= 2;
		}
	}

	// 先頭の「 」、末尾の「.」「 」を警告する
	if ((name[0] == ' ') || (name[len - 1] == '.') || (name[len - 1] == ' '))
		rv |= 8;

	// デバイス名を警告する
	if (check_device_name(name, len) != 0)
		rv |= 8;

	if (len == 0)	// 最終的にファイル名が無くなればエラー
		return 16;
	return rv;
}

// 修復中のテンポラリ・ファイルの名前を作る
// 末尾に _par.tmp を追加する (ADD_LEN 以下の文字数)
void get_temp_name(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *temp_path)		// テンポラリ・ファイルのパス
{
	wcscpy(temp_path, file_path);
	wcscat(temp_path, L"_par.tmp");	// 末尾に追加する
}

void print_hash(unsigned char hash[16])
{
	printf("%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
		hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7],
		hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

unsigned int memory_use;	// メモリー使用量 0=auto, 1～7 -> 1/8 ～ 7/8

// 空きメモリー量と制限値から使用できるメモリー量を計算する
unsigned int get_mem_size(unsigned __int64 data_size)
{
	unsigned int mem_size, *tmp_p;
	MEMORYSTATUSEX statex;

	statex.dwLength = sizeof(statex);
	if (GlobalMemoryStatusEx(&statex)){
		unsigned __int64 mem_size64, cache_size;
/*
		printf("MemoryLoad    = %d\n", statex.dwMemoryLoad);
		printf("TotalPhys     = %10I64d\n", statex.ullTotalPhys);
		printf("AvailPhys     = %10I64d\n", statex.ullAvailPhys);
		printf("TotalPageFile = %10I64d\n", statex.ullTotalPageFile);
		printf("AvailPageFile = %10I64d\n", statex.ullAvailPageFile);
		printf("TotalVirtual  = %10I64d\n", statex.ullTotalVirtual);
		printf("AvailVirtual  = %10I64d\n", statex.ullAvailVirtual);
*/
		cache_size = statex.ullAvailPhys >> 3;	// available size / 8
		if (data_size + cache_size < statex.ullAvailPhys){	// all file data can be cached in memory
			mem_size64 = statex.ullAvailPhys - data_size;	// using size = available size - data size
			if (memory_use){	// limited upto ?/8
				cache_size *= memory_use;
				if (mem_size64 > cache_size)
					mem_size64 = cache_size;
			}
		} else {
			if (memory_use){	// manual setting
				mem_size64 = cache_size * memory_use;	// using size = available size / 8 * memory_use
			} else {	// auto setting (mostly for 32-bit OS)
				// disk cache size is range from 100MB to 200MB.
				cache_size += statex.ullTotalPhys >> 4;	// cache size = available size / 8 + total size / 16
				if (cache_size > statex.ullAvailPhys >> 2)
					cache_size = statex.ullAvailPhys >> 2;	// cache size should be less than 1/4 of available size.
				mem_size64 = statex.ullAvailPhys - cache_size;	// using size = available size - cache size
			}
		}

		if (mem_size64 + 0x00200000 > statex.ullAvailVirtual)
			mem_size64 = statex.ullAvailVirtual - 0x00200000;	// keep 2MB for other task
		if (mem_size64 >= 0x7F000000){	// max is 2032MB
			mem_size = 0x7F000000;
		} else {
			mem_size = (unsigned int)mem_size64 & 0xFFFF0000;	// multiple of 64KB
		}

	} else {
		print_win32_err();
		mem_size = 0x20000000;	// メモリー量を認識できない場合は 512MB と仮定する
	}

	// try to allocate for test
	//printf("\nget_mem_size: try %d MB ... ", mem_size >> 20);
	tmp_p = malloc(mem_size);
	while ((tmp_p == NULL) && (mem_size > 0x08000000)){	// if over than 128MB
		mem_size -= 0x01000000;	// reduce 16MB
		tmp_p = malloc(mem_size);
	}
	if (tmp_p != NULL)
		free(tmp_p);
	//printf("%d MB ok\n", mem_size >> 20);

	return mem_size;
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

	if ((prog_last < 0) && (file_name != NULL)){	// 初めて経過を表示する時だけファイル名を表示する
		char text[MAX_NAME_LEN * 3];
		wchar_t short_name[MAX_NAME_LEN];

		copy_filename(short_name, file_name);
		utf16_to_cp(short_name, text);
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
			utf16_to_cp(lpMsgBuf, buf);
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

// ファイルをゴミ箱に移す
int delete_file_recycle(wchar_t *file_path)
{
	wchar_t temp_path[MAX_PATH + PREFIX_LEN];
	int rv;
	SHFILEOPSTRUCT FileOp;

	// "\\?\" が付いてると ShellAPI が動作しないので取り除く
	rv = len_without_prefix(file_path);
	if (rv >= MAX_PATH - 1){	// パスの文字数が MAX_PATH - 1 以下でないといけない
		// 短いパス名に変換してみる
		rv = GetShortPathName(file_path, temp_path, MAX_PATH + PREFIX_LEN);
		if ((rv == 0) || (rv >= MAX_PATH + PREFIX_LEN))
			return 1;	// パスが長すぎで短縮できない？
		// 更にプリフィックスを取り除く
		rv = copy_without_prefix(temp_path, temp_path);
	} else {
		rv = copy_without_prefix(temp_path, file_path);
	}
	temp_path[rv + 1] = 0;	// 末尾にもう一個 null 文字を追加する

	memset(&FileOp, 0, sizeof(FileOp));
	FileOp.wFunc = FO_DELETE;
	FileOp.pFrom = temp_path;
	FileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
	rv = SHFileOperation(&FileOp);
	if (FileOp.fAnyOperationsAborted == TRUE)
		rv = 1;	// 削除がキャンセルされた
	return rv;
}

// エクスプローラーで隠しファイルを表示する設定になってるか調べる
unsigned int get_show_hidden(void)
{
	unsigned int rv;
	SHELLSTATE ssf;

	rv = FILE_ATTRIBUTE_HIDDEN;
	// Explorer の設定を調べる
	SHGetSetSettings(&ssf, SSF_SHOWALLOBJECTS | SSF_SHOWSUPERHIDDEN, FALSE);
	// 隠しファイルを表示するかどうか
	if (ssf.fShowAllObjects){	// 表示する設定なら
		rv = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;

		// 保護されたオペレーティングシステムファイルを表示するかどうか
		if (ssf.fShowSuperHidden)	// 表示する設定なら
			rv = INVALID_FILE_ATTRIBUTES;
	}

	return rv;
}

