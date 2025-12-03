// main.c
// Copyright : 2025-11-23 Yutaka Sawada
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

#include <stdio.h>

#include <windows.h>
#include <imagehlp.h>

#include "crc.h"
#include "common.h"
#include "create.h"
#include "verify.h"
#include "ini.h"
#include "version.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void print_help(void)
{
	printf(
"Usage\n"
"c(reate) [fo,t, d,u] <checksum file> [input files]\n"
"v(erify) [vs,vd,d,u] <checksum file>\n"
"\nOption\n"
" /fo   : Search file only for wildcard\n"
" /t<n> : Save time stamp\n"
" /vs<n>: Skip verification by recent result\n"
" /vd\"*\": Set directory of recent result\n"
" /d\"*\" : Set directory of input files\n"
" /u    : Console output is encoded with UTF-8\n"
	);
}

// CRC-32 チェックサムを使って自分自身の破損を検出する
int test_checksum(wchar_t *file_path)	// 作業用
{
	unsigned int rv, crc, chk, chk2;
	unsigned char *pAddr;
	HANDLE hFile, hMap;

	init_crc_table();	// CRC 計算用のテーブルを作成する

	// 実行ファイルのパスを取得する
	rv = GetModuleFileName(NULL, file_path, MAX_LEN);
	if ((rv == 0) || (rv >= MAX_LEN))
		return 1;

	// 実行ファイルの PE checksum と CRC-32 を検証する
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		return 1;
	}
	rv = GetFileSize(hFile, &chk2);
	if (rv == INVALID_FILE_SIZE)
		return 1;
	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, chk2, rv, NULL);
	if (hMap == NULL){
		CloseHandle(hFile);
		return 1;
	}
	pAddr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, rv);
	if (pAddr == NULL){
		CloseHandle(hMap);
		CloseHandle(hFile);
		return 1;
	}
	if (CheckSumMappedFile(pAddr, rv, &chk2, &chk) == NULL){	// PE checksum
		UnmapViewOfFile(pAddr);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return 1;
	}
	crc = crc_update(0xFFFFFFFF, pAddr, rv) ^ 0xFFFFFFFF;
	UnmapViewOfFile(pAddr);
	CloseHandle(hMap);
	CloseHandle(hFile);

	if (chk != chk2)
		return 2;
	if (crc != 0x00000000)
		return 3;
	return 0;
}

// ファイルを検索してファイル・リストに追加する
static wchar_t * search_files(
	wchar_t *list_buf,		// ファイル・リスト
	wchar_t *search_path,	// 検索するファイルのフル・パス
	int dir_len,			// ディレクトリ部分の長さ
	//int file_only,		// ファイルのみにするかどうか
	
	unsigned int filter,	//  2, FILE_ATTRIBUTE_HIDDEN    = 隠しファイルを無視する
							//  4, FILE_ATTRIBUTE_SYSTEM    = システムファイルを無視する
							// 16, FILE_ATTRIBUTE_DIRECTORY = ディレクトリを無視する

	int single_file,		// -1 = *や?で検索指定、0～ = 単独指定
	int *list_max,			// ファイル・リストの確保サイズ
	int *list_len,			// ファイル・リストの文字数
	__int64 *total_size)	// 合計ファイル・サイズ
{
	wchar_t *tmp_p;
	int len, l_max, l_off, dir_len2;
	unsigned int attrib_filter;
	HANDLE hFind;
	WIN32_FIND_DATA FindData;

	// 隠しファイルを見つけるかどうか
	attrib_filter = filter & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
	if (attrib_filter == 0)
		attrib_filter = INVALID_FILE_ATTRIBUTES;

	if (list_buf == NULL){
		l_off = 0;
		l_max = ALLOC_LEN;
		list_buf = (wchar_t *)malloc(l_max);
		if (list_buf == NULL){
			printf("malloc, %d\n", l_max);
			return NULL;
		}
	} else {
		l_max = *list_max;
		l_off = *list_len;
	}

	// 検索する
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind == INVALID_HANDLE_VALUE)
		return list_buf; // 見つからなかったらそのまま
	do {
		if ((single_file < 0) && ((FindData.dwFileAttributes & attrib_filter) == attrib_filter))
			continue;	// 検索中は隠し属性が付いてるファイルを無視する

		len = wcslen(FindData.cFileName);
		if (dir_len + len >= MAX_LEN - ADD_LEN - 2){	// 末尾に「\*」を付けて再検索するので
			FindClose(hFind);
			free(list_buf);
			printf("filename is too long\n");
			return NULL;
		}

		// 現在のディレクトリ部分に見つかったファイル名を連結する
		wcscpy(search_path + dir_len, FindData.cFileName);

		// フォルダなら
		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			if (((filter & FILE_ATTRIBUTE_DIRECTORY) == 0) && wcscmp(FindData.cFileName, L".") && wcscmp(FindData.cFileName, L"..")){
				// フォルダの末尾は「\」にする
				wcscat(search_path, L"\\");
				// そのフォルダの中身を更に検索する
				dir_len2 = wcslen(search_path);
				search_path[dir_len2    ] = '*';	// 末尾に「*」を追加する
				search_path[dir_len2 + 1] = 0;
				list_buf = search_files(list_buf, search_path, dir_len2, filter, single_file, &l_max, &l_off, total_size);
				if (list_buf == NULL){
					FindClose(hFind);
					printf("cannot search inner folder\n");
					return NULL;
				}
			}
		} else { // ファイルなら
			if (!search_file_path(list_buf, l_off, search_path + base_len)){	// ファイル名が重複しないようにする
				if ((l_off + dir_len- base_len + len) * 2 >= l_max){ // 領域が足りなくなるなら拡張する
					l_max += ALLOC_LEN;
					tmp_p = (wchar_t *)realloc(list_buf, l_max);
					if (tmp_p == NULL){
						FindClose(hFind);
						free(list_buf);
						printf("realloc, %d\n", l_max);
						return NULL;
					} else {
						list_buf = tmp_p;
					}
				}

				// リストにコピーする
				wcscpy(list_buf + l_off, search_path + base_len);
				l_off += dir_len - base_len + len + 1;
				file_num++;
				(*total_size) += ((__int64)FindData.nFileSizeHigh << 32) | (__int64)FindData.nFileSizeLow;
			}
		}
	} while (FindNextFile(hFind, &FindData)); // 次のファイルを検索する
	FindClose(hFind);

	*list_max = l_max;
	*list_len = l_off;
	return list_buf;
}

// ファイルの詳細情報を記録する
int save_detail(
	wchar_t *uni_buf,
	wchar_t *file_name)		// 検査対象のファイル名
{
	__int64 file_size;
	HANDLE hFile;
	FILETIME ftWrite;
	SYSTEMTIME stUTC, stLocal;

	// 調べるファイルを開く
	wcscpy(uni_buf, base_dir);
	wcscpy(uni_buf + base_len, file_name);
	hFile = CreateFile(uni_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		return 1;
	}
	// ファイル・サイズ
	if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
		print_win32_err();
		CloseHandle(hFile);
		return 1;
	}
	// 更新日時
	if (!GetFileTime(hFile, NULL, NULL, &ftWrite)){
		print_win32_err();
		CloseHandle(hFile);
		return 1;
	}
	FileTimeToSystemTime(&ftWrite, &stUTC);
    SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
	CloseHandle(hFile);

	// サイズと日時を書き込む
	wsprintf(uni_buf, L";%13I64d %02d:%02d.%02d %4d-%02d-%02d ", file_size,
			stLocal.wHour, stLocal.wMinute, stLocal.wSecond,
			stLocal.wYear, stLocal.wMonth, stLocal.wDay);
	add_text(uni_buf);

	// ファイル名を書き込む
	wcscpy(uni_buf, file_name);	// 変換前にコピーする
	unix_directory(uni_buf);
	add_text(uni_buf);
	add_text(L"\r\n");

	return 0;
}

// チェックサム・ファイルを書き込む
static int write_checksum(wchar_t *uni_buf,
	wchar_t *list_buf, int list_len, __int64 total_size, int switch_t)
{
	char *ascii_buf;
	wchar_t *ads_p;
	int err = 0, rv, len, format;
	unsigned int time_last;
	__int64 prog_now = 0;
	HANDLE hFile;
	FILETIME ftWrite;

	len = wcslen(checksum_file);
	if (_wcsicmp(checksum_file + (len - 4), L".sfv") == 0){	// SFVファイル
		format = 1;
	} else if (_wcsicmp(checksum_file + (len - 4), L".md5") == 0){	// MD5ファイル
		format = 2;
	} else if ((_wcsicmp(checksum_file + (len - 4), L".ffp") == 0) || (_wcsicmp(checksum_file + (len - 7), L"ffp.txt") == 0)){
		format = 3; // FLAC Fingerprint ファイル
	} else {
		printf("unknown format\n");
		return 1;
	}

	// チェックサム・ファイルのテキストを UTF-16 の文字列で作成する
	text_len = 0;
	text_max = ALLOC_LEN;
	text_buf = (wchar_t *)malloc(text_max * 2);
	if (text_buf == NULL){
		printf("malloc, %d\n", text_max * 2);
		return 1;
	}

	// コメントを書き込む
	wsprintf(uni_buf, L"; Generated by SFV/MD5 checker v%hs", PRODUCT_VERSION);
	add_text(uni_buf);
	if (switch_t >= 1){	// 作成した日時を書き込む
		SYSTEMTIME stLocal;
		GetLocalTime(&stLocal);
		wsprintf(uni_buf, L" on %4d-%02d-%02d at %02d:%02d.%02d",
			stLocal.wYear, stLocal.wMonth, stLocal.wDay,
			stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
		add_text(uni_buf);
		if (switch_t >= 2){	// ファイルの詳細を書き込むなら
			add_text(L"\r\n;");
			if (file_num > 1){
				add_text(L"\r\n");
				add_text(L"; Size (Bytes)   Time      Date      Filename\n");
				add_text(L"; ------------ -------- ---------- ------------");
			}
		}
	}
	add_text(L"\r\n");

	if (switch_t >= 2){	// ファイルのサイズと更新日時を書き込む
		len = 0;
		while (len < list_len){
			if (save_detail(uni_buf, list_buf + len) != 0){
				printf("cannot save detail\n");
				free(text_buf);
				return 1;
			}
			len += wcslen(list_buf + len) + 1;
		}
		add_text(L";\r\n");
	}

	// 各ファイルのチェックサムを計算する
	printf("\n");
	print_progress_text(0, "Computing file hash");
	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する
	len = 0;
	while (len < list_len){
		if (format == 1){
			rv = create_sfv(uni_buf, list_buf + len, &time_last, &prog_now, total_size);
		} else if (format == 2){
			rv = create_md5(uni_buf, list_buf + len, &time_last, &prog_now, total_size);
		} else if (format == 3){
			rv = create_ffp(uni_buf, list_buf + len, &prog_now);
		} else {
			rv = 1;
		}
		if (rv != 0){
			free(text_buf);
			return rv;	// エラー、キャンセルなど
		}

		// 経過表示
		if (GetTickCount() / UPDATE_TIME != time_last){
			if (print_progress((int)((prog_now * 1000) / total_size))){
				free(text_buf);
				return 2;
			}
			time_last = GetTickCount() / UPDATE_TIME;
		}

		len += wcslen(list_buf + len) + 1;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// 既存ファイルの alternate data stream に書き込む場合は更新日時をそのままにする
	ads_p = wcschr(offset_file_name(checksum_file), ':');
	if (ads_p != NULL){
		*ads_p = 0;
		hFile = CreateFile(checksum_file, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
		*ads_p = ':';
		if (hFile == INVALID_HANDLE_VALUE){
			ads_p = NULL;
		} else {
			if (!GetFileTime(hFile, NULL, NULL, &ftWrite)){
				//print_win32_err();
				//printf("cannot read time stamp\n");
				ads_p = NULL;
			}
			CloseHandle(hFile);
		}
	}

	// UTF-16 から UTF-8 に変換する
	len = text_len * 3;
	ascii_buf = (char *)malloc(len);
	if (ascii_buf == NULL){
		printf("malloc, %d\n", len);
		free(text_buf);
		return 1;
	}
	if (utf16_to_cp(text_buf, ascii_buf, len, CP_UTF8) != 0){
		printf("cannot encode text\n");
		free(ascii_buf);
		free(text_buf);
		return 1;
	}
	len = strlen(ascii_buf);

	// チェックサム・ファイルを開いて、エンコードしたテキストを書き込む
	hFile = CreateFile(checksum_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		printf("cannot create checksum file\n");
		free(ascii_buf);
		free(text_buf);
		return 1;
	}
	if (!WriteFile(hFile, ascii_buf, len, &rv, NULL)){
		print_win32_err();
		printf("cannot write checksum file\n");
		CloseHandle(hFile);
		free(ascii_buf);
		free(text_buf);
		return 1;
	}
	// main stream の更新日時を元に戻す
	if (ads_p != NULL)
		SetFileTime(hFile, NULL, NULL, &ftWrite);

	CloseHandle(hFile);	// チェックサム・ファイルを閉じる
	free(ascii_buf);
	free(text_buf);

	printf("\nCreated successfully\n");
	return 0;
}

// チェックサム・ファイルを読み込む
static int read_checksum(char *ascii_buf, wchar_t *file_path)
{
	unsigned char *data_buf;
	unsigned int err = 0, rv, file_size;
	HANDLE hFile;

	// 読み込むファイルを開く
	hFile = CreateFile(checksum_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		printf("cannot open checksum file\n");
		return 1;
	}

	// 2MB を越えるチェックサム・ファイルには対応しない
	file_size = GetFileSize(hFile, NULL);
	if ((file_size == INVALID_FILE_SIZE) || (file_size > (2 << 21))){
		CloseHandle(hFile);
		printf("checksum file is too large\n");
		return 1;
	}
	printf("Checksum Size\t: %d\n\n", file_size);
	fflush(stdout);

	// ハッシュ値を記録するには最低でも 1(ファイル名)+1(スペース)+8(CRC-32) = 10バイト必要
	if (file_size < 10){
		CloseHandle(hFile);
		printf("Status  : Damaged\n");
		printf("valid file is not found\n");
		return 1;
	}

	// ファイル全体を読み込む
	data_buf = (unsigned char *)malloc(file_size + 2);
	if (data_buf == NULL){
		CloseHandle(hFile);
		printf("malloc, %d\n", file_size);
		return 1;
	}
	text_max = file_size + 1;	// 全て ASCII 文字と仮定して、末尾に null 文字を付けた時の文字数
	text_buf = (wchar_t *)malloc(text_max * 2);
	if (text_buf == NULL){
		free(data_buf);
		CloseHandle(hFile);
		printf("malloc, %d\n", text_max * 2);
		return 1;
	}
	if (!ReadFile(hFile, data_buf, file_size, &rv, NULL) || (file_size != rv)){
		free(data_buf);
		free(text_buf);
		CloseHandle(hFile);
		printf("ReadFile, %d\n", file_size);
		return 1;
	}
	CloseHandle(hFile);
	data_buf[file_size    ] = 0;	// 末尾に null 文字を付けておく
	data_buf[file_size + 1] = 0;

	// UTF-16 の文字列に変換する
	rv = utf16_to_utf16(data_buf, file_size + 2, text_buf);
	if (rv != 0){	// UTF-16 ではなかった
		if (check_utf8(data_buf) == 0){	// UTF-8 として変換する
			rv = 0;	// BOMが付いてるかどうか
			if ((data_buf[0] == 0xEF) && (data_buf[1] == 0xBB) && (data_buf[2] == 0xBF))
				rv = 3;
			rv = cp_to_utf16(data_buf + rv, text_buf, file_size + 1, CP_UTF8);
		} else {
			rv = cp_to_utf16(data_buf, text_buf, file_size + 1, CP_ACP);
			if (rv != 0)
				rv = cp_to_utf16(data_buf, text_buf, file_size + 1, 1252);	// Latin-1 CP1252 で変換を試みる
		}
	}
	free(data_buf);
	if (rv != 0){
		free(text_buf);
		printf("cannot decode text\n");
		return 1;
	}
	text_len = wcslen(text_buf);	// 文字数には末尾の null 文字を含まないことに注意

	// チェックサムを検証する
	rv = wcslen(checksum_file);
	if (_wcsicmp(checksum_file + (rv - 4), L".sfv") == 0){	// SFVファイル
		err = verify_sfv(ascii_buf, file_path);
	} else if (_wcsicmp(checksum_file + (rv - 4), L".md5") == 0){	// MD5ファイル
		err = verify_md5(ascii_buf, file_path);
	} else if ((_wcsicmp(checksum_file + (rv - 4), L".ffp") == 0) || (_wcsicmp(checksum_file + (rv - 7), L"ffp.txt") == 0)){
		err = verify_ffp(ascii_buf, file_path);	// FLAC Fingerprint ファイル
	} else {
		err = 1;
		printf("unknown format\n");
	}
	free(text_buf);
	close_ini_file();

	// 検査結果を表示する
	printf("\n");
	if (err == 0){
		printf("All Files Complete\n");
	} else {
		if (err & 16)
			printf("Checksum File Incomplete\n");
		if (err & 0xFFFFFF00){
			printf("%d Files Missing or Damaged\n", err >> 8);
			//err &= 0xFF;
		}
		err = ((err & 0x10) << 4) | (err & 0xEF);	// 16を 256 に変更する
	}

	return err;
}

// チェックサム・ファイルとソース・ファイルの名前に問題が無いか確かめる
static int check_filename(
	wchar_t *list_buf,
	int list_len)
{
	wchar_t *tmp_p;
	int num, list_off = 0;

	// SFV ファイル作成時に、ソース・ファイルの先頭が「;」だとコメントと区別できない
	if (_wcsicmp(checksum_file + (wcslen(checksum_file) - 4), L".sfv") == 0){	// SFVファイル
		for (num = 0; num < file_num; num++){
			tmp_p = list_buf + list_off;
			while (list_buf[list_off] != 0)
				list_off++;
			list_off++;

			// ファイル名の先頭を調べる
			if (tmp_p[0] == ';'){
				printf_cp("filename is invalid, %s\n", tmp_p);
				return 1;
			}
		}
	}

	// 基準ディレクトリ外にチェックサム・ファイルが存在するなら問題なし
	if (_wcsnicmp(checksum_file, base_dir, base_len) != 0)
		return 0;

	// チェックサム・ファイルとソース・ファイルが同じ名前にならないようにする
	for (num = 0; num < file_num; num++){
		tmp_p = list_buf + list_off;
		while (list_buf[list_off] != 0)
			list_off++;
		list_off++;

		// ファイル名との一致を調べる
		if (_wcsicmp(checksum_file + base_len, tmp_p) == 0){
			printf_cp("filename is invalid, %s\n", tmp_p);
			return 1;
		}
	}
	return 0;
}

wmain(int argc, wchar_t *argv[])
{
	char ascii_buf[MAX_LEN * 3];
	wchar_t file_path[MAX_LEN], *tmp_p;
	int i, j, switch_set = 0;
/*
t = switch_set & 0x00000003
fo= switch_set & 0x00000020
*/
	printf("SFV/MD5 checker version " FILE_VERSION " by Yutaka Sawada\n\n");
	if (argc < 3){
		printf("Self-Test: ");
		i = test_checksum(file_path);
		if (i == 0){
			printf("Success");
		} else if (i == 2){
			printf("PE checksum is different");
		} else if (i == 3){
			printf("CRC-32 is different");
		} else {
			printf("Error\0thedummytext");
		}
		printf("\n\n");
		print_help();
		return 0;
	}

	// 初期化
	checksum_file[0] = 0;
	base_dir[0] = 0;
	ini_path[0] = 0;
	file_num = 0;
	cp_output = GetConsoleOutputCP();

	// コマンド
	switch (argv[1][0]){
	case 'c': // create
	case 'v': // verify
		break;
	default:
		print_help();
		return 0;
	}

	// オプションとチェックサム・ファイルの指定
	for (i = 2; i < argc; i++){
		tmp_p = argv[i];
		// オプション
		if (((tmp_p[0] == '/') || (tmp_p[0] == '-')) && (tmp_p[0] == argv[2][0])){
			tmp_p++;	// 先頭の識別文字をとばす
			// オプション
			if (wcscmp(tmp_p, L"u") == 0){
				cp_output = CP_UTF8;
			} else if (wcscmp(tmp_p, L"fo") == 0){
				switch_set |= 0x20;

			// オプション (数値)
			} else if (wcsncmp(tmp_p, L"vs", 2) == 0){
				recent_data = 0;
				j = 2;
				while ((j < 2 + 2) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					recent_data = (recent_data * 10) + (tmp_p[j] - '0');
					j++;
				}
				if ((recent_data == 8) || (recent_data > 15))
					recent_data = 0;
			} else if (wcsncmp(tmp_p, L"t", 1) == 0){
				j = -1;
				if ((tmp_p[1] >= '0') && (tmp_p[1] <= '2'))	// 0～2 の範囲
					j = tmp_p[1] - '0';
				if (j != -1)
					switch_set |= j;

			// オプション (文字列)
			} else if (wcsncmp(tmp_p, L"vd", 2) == 0){
				tmp_p += 2;
				j = copy_path_prefix(ini_path, MAX_LEN - INI_NAME_LEN - 1, tmp_p, NULL);
				if (j == 0){
					printf("save-directory is invalid\n");
					return 1;
				}
				if (ini_path[j - 1] != '\\'){	// 末尾が「\」でなければ付けておく
					ini_path[j    ] = '\\';
					ini_path[j + 1] = 0;
				}
				//printf("Save Directory : %S\n", ini_path);
				j = GetFileAttributes(ini_path);
				if ((j == INVALID_FILE_ATTRIBUTES) || !(j & FILE_ATTRIBUTE_DIRECTORY)){
					printf("save-directory is invalid\n");
					return 1;
				}
			} else if (wcsncmp(tmp_p, L"d", 1) == 0){
				tmp_p++;
				j = copy_path_prefix(base_dir, MAX_LEN - 2, tmp_p, NULL);	// 末尾に追加される分の余裕を見ておく
				if (j == 0){
					printf("base-directory is invalid\n");
					return 1;
				}
				if (base_dir[j - 1] != '\\'){	// 末尾が「\」でなければ付けておく
					base_dir[j    ] = '\\';
					base_dir[j + 1] = 0;
				}
				j = GetFileAttributes(base_dir);
				if ((j == INVALID_FILE_ATTRIBUTES) || !(j & FILE_ATTRIBUTE_DIRECTORY)){
					printf("base-directory is invalid\n");
					return 1;
				}
			} else {	// 未対応のオプション
				utf16_to_cp(tmp_p - 1, ascii_buf, MAX_LEN * 3, cp_output);
				printf("invalid option, %s\n", ascii_buf);
				return 1;
			}
			continue;
		}
		// オプション以外ならリカバリ・ファイル
		j = copy_path_prefix(checksum_file, MAX_LEN, tmp_p, NULL);
		if (j == 0){
			printf("checksum filename is invalid\n");
			return 1;
		}
		// 拡張子は SFV か MD5 のみ
		if (_wcsicmp(checksum_file + (j - 4), L".sfv") == 0){
			init_crc_table();	// CRC 計算用のテーブルを作る
		} else if (_wcsicmp(checksum_file + (j - 4), L".md5") == 0){
			if ((argv[1][0] == 'v') && (recent_data != 0))
				init_crc_table();
		} else if ((_wcsicmp(checksum_file + (j - 4), L".ffp") == 0) || (_wcsicmp(checksum_file + (j - 7), L"ffp.txt") == 0)){
			if ((argv[1][0] == 'v') && (recent_data != 0))
				init_crc_table();
		} else {	// 拡張子が違うなら
			wcscpy(checksum_file + j, L".sfv");	// 標準で SFV 形式にする
			if (argv[1][0] == 'c'){	// 作成なら
				init_crc_table();
			} else {	// 検査なら
				if (GetFileAttributes(checksum_file) == INVALID_FILE_ATTRIBUTES){
					wcscpy(checksum_file + j, L".md5");	// SFV で駄目なら MD5 にする
					if (GetFileAttributes(checksum_file) == INVALID_FILE_ATTRIBUTES){
						printf("file format is unknown\n");
						return 1;
					} else if (recent_data != 0){
						init_crc_table();
					}
				} else {
					init_crc_table();
				}
			}
		}
		if (argv[1][0] == 'c'){	// 作成なら
			// 指定されたファイル名が適切か調べる
			if (sanitize_filename(offset_file_name(checksum_file)) != 0){
				printf("checksum filename is invalid\n");
				return 1;
			}
		} else {	// 検査なら
			j = GetFileAttributes(checksum_file);
			if ((j == INVALID_FILE_ATTRIBUTES) || (j & FILE_ATTRIBUTE_DIRECTORY)){
				wchar_t search_path[MAX_LEN];
				int name_len, dir_len, path_len, find_flag = 0;
				HANDLE hFind;
				WIN32_FIND_DATA FindData;
				if (wcspbrk(offset_file_name(checksum_file), L"*?") == NULL){
					// 「*」や「?」で検索しない場合、ファイルが見つからなければエラーにする
					printf("valid file is not found\n");
					return 1;
				}
				// 指定された拡張子で検索する
				path_len = wcslen(checksum_file);
				hFind = FindFirstFile(checksum_file, &FindData);
				if (hFind != INVALID_HANDLE_VALUE){
					get_base_dir(checksum_file, search_path);
					dir_len = wcslen(search_path);
					do {
						//printf("file name = %S\n", FindData.cFileName);
						name_len = wcslen(FindData.cFileName);
						if ((dir_len + name_len < MAX_LEN) &&	// ファイル名が長すぎない
								(_wcsicmp(FindData.cFileName + (name_len - 4), checksum_file + (path_len - 4)) == 0)){	// 拡張子が同じ
							find_flag = 1;
							break;	// 見つけたファイル名で問題なし
						}
					} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
					FindClose(hFind);
				}
				if (find_flag == 0){
					printf("valid file is not found\n");
					return 1;
				}
				wcscpy(checksum_file + dir_len, FindData.cFileName);
			}
		}
		break;
	}
	if (checksum_file[0] == 0){	// チェックサム・ファイルが指定されてないなら
		printf("checksum file is not specified\n");
		return 1;
	}

	// input file の位置が指定されて無くて、
	// 最初のソース・ファイル指定に絶対パスや相対パスが含まれてるなら、それを使う
	if (argv[1][0] == 'c'){
		if ((base_dir[0] == 0) && (i + 1 < argc)){
			tmp_p = argv[i + 1];
			if (is_full_path(tmp_p) != 0){	// 絶対パスなら
				if (copy_path_prefix(file_path, MAX_LEN - 2, tmp_p, NULL) == 0){
					printf("base-directory is invalid\n");
					return 1;
				}
				get_base_dir(file_path, base_dir);	// 最初のソース・ファイルの位置にする
				if (len_without_prefix(base_dir) == 0)	// サブ・ディレクトリが無ければ
					base_dir[0] = 0;
			}
		}
	}
	if (base_dir[0] == 0)	// input file の位置が指定されてないなら
		get_base_dir(checksum_file, base_dir);	// リカバリ・ファイルの位置にする
	base_len = wcslen(base_dir);

	// 動作環境の表示
	// 「\\?\」は常に付加されてるので表示する際には無視する
	printf_cp("Base Directory\t: \"%s\"\n", base_dir);
	printf_cp("Checksum File\t: \"%s\"\n", checksum_file);

	if (argv[1][0] == 'c'){
		wchar_t *list_buf;
		int dir_len, list_len, list_max;
		__int64 total_size = 0;	// 合計ファイル・サイズ
		unsigned int filter;
		// 隠しファイルやフォルダーを無視するかどうか
		filter = get_show_hidden();
		if (switch_set & 0x20)
			filter |= FILE_ATTRIBUTE_DIRECTORY;
		// チェックサム・ファイル作成ならソース・ファイルのリストがいる
		i++;
		if (i >= argc){
			printf("input file is not specified\n");
			return 1;
		}
		// 入力ファイルの指定
		file_num = 0;
		list_len = 0;
		list_max = 0;
		list_buf = NULL;
		for (; i < argc; i++){
			// ファイルが基準ディレクトリ以下に存在することを確認する
			tmp_p = argv[i];
			j = copy_path_prefix(file_path, MAX_LEN - 2, tmp_p, base_dir);	// 絶対パスにしてから比較する
			if (j == 0){
				free(list_buf);
				printf_cp("filename is invalid, %s\n", tmp_p);
				return 1;
			}
			if ((j <= base_len) || (_wcsnicmp(base_dir, file_path, base_len) != 0)){	// 基準ディレクトリ外なら
				free(list_buf);
				printf_cp("out of base-directory, %s\n", tmp_p);
				return 1;
			}
			//printf("%d = %S\n", i, argv[i]);
			//printf_cp("search = %s\n", file_path);
			// 「*」や「?」で検索しない場合、ファイルが見つからなければエラーにする
			j = -1;
			if (wcspbrk(file_path + base_len, L"*?") == NULL)
				j = file_num;
			// ファイルを検索する
			dir_len = wcslen(file_path) - 2;	// ファイル名末尾の「\」を無視して、ディレクトリ部分の長さを求める
			while (file_path[dir_len] != '\\')
				dir_len--;
			dir_len++;
			list_buf = search_files(list_buf, file_path, dir_len, filter, j, &list_max, &list_len, &total_size);
			if (list_buf == NULL)
				return 1;
			if ((j != -1) && (j == file_num)){	// ファイルが見つかったか確かめる
				free(list_buf);
				printf_cp("input file is not found, %s\n", file_path + base_len);
				return 1;
			}
		}
		if (list_len == 0){
			if (list_buf)
				free(list_buf);
			printf("input file is not found\n");
			return 1;
		}
		if (check_filename(list_buf, list_len)){
			free(list_buf);
			return 1;
		}
/*{
FILE *fp;
fp = fopen("list_buf.txt", "wb");
fwrite(list_buf, 2, list_len, fp);
fclose(fp);
}*/

		// ファイル・リストの内容を並び替える (リストの末尾は null 1個にすること)
		sort_list(list_buf, list_len);

		printf("\nInput File count\t: %d\n", file_num);
		printf("Input File total size\t: %I64d\n", total_size);
		i = write_checksum(file_path, list_buf, list_len, total_size, switch_set & 3);

	} else {
		// 検査結果ファイルの位置が指定されてないなら
		if ((recent_data != 0) && (ini_path[0] == 0)){
			// 実行ファイルのディレクトリにする
			j = GetModuleFileName(NULL, ini_path, MAX_LEN);
			if ((j == 0) || (j >= MAX_LEN)){
				printf("GetModuleFileName\n");
				return 1;
			}
			while (j > 0){
				if (ini_path[j - 1] == '\\'){
					ini_path[j] = 0;
					break;
				}
				j--;
			}
			if (j >= MAX_LEN - INI_NAME_LEN - 1){
				printf("save-directory is invalid\n");
				return 1;
			}
		}

		i = read_checksum(ascii_buf, file_path);
	}

	//printf("ExitCode: %d\n", i);
	return i;
}

