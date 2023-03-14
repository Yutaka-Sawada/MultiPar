// par1_cmd.c
// Copyright : 2023-03-14 Yutaka Sawada
// License : GPL

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600	// Windows Vista or later
#endif

#include <stdio.h>

#include <windows.h>

#include "common1.h"
#include "par1.h"
#include "ini.h"
#include "version.h"


void print_help(void)
{
	printf(
"Usage\n"
"c(reate) [f,fu,r,n,p,m,c,d,in,u] <par file> [input files]\n"
"v(erify) [  vs,vd,d,i,u,b,br] <par file>\n"
"r(epair) [m,vs,vd,d,i,u,b,br] <par file>\n"
"l(ist)   [u,h] <par file>\n"
"\nOption\n"
" /f    : Use file-list instead of files\n"
" /fu   : Use file-list which is encoded with UTF-8\n"
" /r<n> : Rate of redundancy (%%)\n"
" /n<n> : Number of parity volumes\n"
" /p<n> : First parity volume number\n"
" /m<n> : Memory usage\n"
" /vs<n>: Skip verification by recent result\n"
" /vd\"*\": Set directory of recent result\n"
" /c\"*\" : Set comment\n"
" /d\"*\" : Set directory of input files\n"
" /i    : Recreate index file\n"
" /in   : Do not create index file\n"
" /u    : Console output is encoded with UTF-8\n"
" /b    : Backup existing files at repair\n"
" /br   : Send existing files into recycle bin at repair\n"
" /h    : List hash value of input files\n"
	);
}

// 動作環境の表示
static void print_environment(void)
{
	MEMORYSTATUSEX statex;

	// 「\\?\」は常に付加されてるので表示する際には無視する
	printf_cp("Base Directory\t: \"%s\"\n", base_dir);
	printf_cp("Recovery File\t: \"%s\"\n", recovery_file);

	printf("Memory usage\t: ");
	if (memory_use == 0){
		printf("Auto");
	} else {
		printf("%d/8", memory_use);
	}
	statex.dwLength = sizeof(statex);
	if (GlobalMemoryStatusEx(&statex))
		printf(" (%I64d MB available)", statex.ullAvailPhys >> 20);
	printf("\n\n");
}

// 格納ファイル・リストを読み込んでバッファーに書き込む
static wchar_t * read_list(
	wchar_t *list_path,		// リストのパス
	int *file_num,			// データ・ファイルの数
	int *list_len,			// ファイル・リストの文字数
	int *block_num,			// ソース・ブロックの数
	__int64 *block_size,	// ブロック・サイズ (最大ファイル・サイズ)
	int switch_f)			// 1=CP_OEMCP, 2=CP_UTF8
{
	char buf[MAX_LEN * 3];
	wchar_t *list_buf, file_name[MAX_LEN], file_path[MAX_LEN], *tmp_p;
	int len, base_len, l_max, l_off = 0;
	__int64 file_size;
	FILE *fp;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

	base_len = wcslen(base_dir);
	if (switch_f == 2){
		switch_f = CP_UTF8;
	} else {
		switch_f = CP_OEMCP;
	}
	l_max = ALLOC_LEN;
	list_buf = malloc(l_max);
	if (list_buf == NULL){
		printf("malloc, %d\n", l_max);
		return NULL;
	}

	// 読み込むファイルを開く
	fp = _wfopen(list_path, L"rb");
	if (fp == NULL){
		utf16_to_cp(list_path, buf);
		free(list_buf);
		printf("cannot open file-list, %s\n", buf);
		return NULL;
	}

	// 一行ずつ読み込む
	while (fgets(buf, MAX_LEN * 3, fp)){
		if (ferror(fp))
			break;
		buf[MAX_LEN * 3 - 1] = 0;
		// 末尾に改行があれば削除する
		for (len = 0; len < MAX_LEN * 3; len++){
			if (buf[len] == 0)
				break;
			if ((buf[len] == '\n') || (buf[len] == '\r')){
				buf[len] = 0;
				break;
			}
		}
		if (buf[0] == 0)
			continue;	// 改行だけなら次の行へ

		// 読み込んだ内容をユニコードに変換する
		if (!MultiByteToWideChar(switch_f, 0, buf, -1, file_name, MAX_LEN)){
			fclose(fp);
			free(list_buf);
			printf("MultiByteToWideChar, %s\n", buf);
			return NULL;
		}

		// ファイルが基準ディレクトリ以下に存在することを確認する
		len = copy_path_prefix(file_path, MAX_LEN - ADD_LEN, file_name, base_dir);	// 絶対パスにしてから比較する
		if (len == 0){
			fclose(fp);
			free(list_buf);
			printf_cp("filename is invalid, %s\n", file_name);
			return NULL;
		}
		if ((len <= base_len) || (_wcsnicmp(base_dir, file_path, base_len) != 0)){	// 基準ディレクトリ外なら
			fclose(fp);
			free(list_buf);
			printf("out of base-directory, %s\n", buf);
			return NULL;
		}
		wcscpy(file_name, file_path + base_len);
		if (wcschr(file_name, '\\') != NULL){	// サブ・ディレクトリは拒否する
			fclose(fp);
			free(list_buf);
			printf_cp("sub-directory is not supported, %s\n", file_name);
			return NULL;
		}

		// ファイルが存在するか確かめる
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			fclose(fp);
			free(list_buf);
			printf("input file is not found, %s\n", buf);
			return NULL;
		}
		if (AttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			fclose(fp);
			free(list_buf);
			printf("folder is not supported, %s\n", buf);
			return NULL;
		}
		file_size = ((unsigned __int64)AttrData.nFileSizeHigh << 32) | (unsigned __int64)AttrData.nFileSizeLow;
		if (*block_size < file_size)
			*block_size = file_size;

		// ファイル名が重複しないようにする
		if (search_file_path(list_buf, l_off, file_name))
			continue;

		// バッファー容量が足りなければ再確保する
		len = wcslen(file_name);
		if ((l_off + len) * 2 >= l_max){
			l_max += ALLOC_LEN;
			tmp_p = (wchar_t *)realloc(list_buf, l_max);
			if (tmp_p == NULL){
				free(list_buf);
				printf("realloc, %d\n", l_max);
				return NULL;
			} else {
				list_buf = tmp_p;
			}
		}

		// リストにコピーする
		wcscpy(list_buf + l_off, file_name);
		l_off += (len + 1);
		*file_num += 1;
		if (file_size > 0)
			*block_num += 1; // 空のファイルはブロックの計算に含めない
	}
	*list_len = l_off;
	fclose(fp);
/*
fp = fopen("list_buf.txt", "wb");
len = 0;
while (len < l_off){
	fwprintf(fp, L"%s\n", list_buf + len);
	len += (wcslen(list_buf + len) + 1);
}
fclose(fp);
*/
	return list_buf;
}

// ファイルを検索してファイル・リストに追加する
static wchar_t * search_files(
	wchar_t *list_buf,		// ファイル・リスト
	wchar_t *search_path,	// 検索するファイルのフル・パス
	int dir_len,			// ディレクトリ部分の長さ
	int single_file,		// -1 = *や?で検索指定、0～ = 単独指定
	int *file_num,			// データ・ファイルの数
	int *list_max,			// ファイル・リストの確保サイズ
	int *list_len,			// ファイル・リストの文字数
	int *block_num,			// ソース・ブロックの数
	__int64 *block_size,	// ブロック・サイズ (最大ファイル・サイズ)
	unsigned int filter)	// 隠しファイルを見つけるかどうか
{
	wchar_t *tmp_p;
	int len, l_max, l_off;
	__int64 file_size;
	HANDLE hFind;
	WIN32_FIND_DATA FindData;

	if (list_buf == NULL){
		l_off = 0;
		l_max = ALLOC_LEN;
		list_buf = malloc(l_max);
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
		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;	// フォルダは無視する

		if ((single_file < 0) && ((FindData.dwFileAttributes & filter) == filter))
			continue;	// 検索中は隠し属性が付いてるファイルを無視する

		// フォルダは無視する、ファイル名が重複しないようにする
		if (!search_file_path(list_buf, l_off, FindData.cFileName)){
			//printf("FindData.cFileName =\n%S\n\n", FindData.cFileName);
			len = wcslen(FindData.cFileName);
			if (dir_len + len >= MAX_LEN - ADD_LEN){
				FindClose(hFind);
				free(list_buf);
				printf("filename is too long\n");
				return NULL;
			}
			if ((l_off + len) * 2 >= l_max){ // 領域が足りなくなるなら拡張する
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

			file_size = ((unsigned __int64)FindData.nFileSizeHigh << 32) | (unsigned __int64)FindData.nFileSizeLow;
			if (*block_size < file_size)
				*block_size = file_size;

			// リストにコピーする
			wcscpy(list_buf + l_off, FindData.cFileName);
			l_off += len + 1;
			*file_num += 1;
			if (file_size > 0)
				*block_num += 1; // 空のファイルはブロックの計算に含めない
		}
	} while (FindNextFile(hFind, &FindData)); // 次のファイルを検索する
	FindClose(hFind);

	*list_max = l_max;
	*list_len = l_off;
	return list_buf;
}

// リカバリ・ファイルとソース・ファイルが同じ名前にならないようにする
static int check_recovery_match(
	int file_num,
	wchar_t *list_buf,
	int list_len,
	int switch_p)	// インデックス・ファイルを作らない
{
	wchar_t *tmp_p;
	int i, num, len, list_off = 0, recovery_len, base_len;

	// 基準ディレクトリ外にリカバリ・ファイルが存在するなら問題なし
	base_len = wcslen(base_dir);
	if (_wcsnicmp(recovery_file, base_dir, base_len) != 0)
		return 0;

	// リカバリ・ファイル (*.PXX) の名前の基
	recovery_len = wcslen(recovery_file);
	recovery_len -= base_len + 2;	// 末尾の 2文字を除くファイル名の長さ

	// ファイルごとに比較する
	for (num = 0; num < file_num; num++){
		tmp_p = list_buf + list_off;
		while (list_buf[list_off] != 0)
			list_off++;
		list_off++;

		// ファイル名との前方一致を調べる
		if (_wcsnicmp(recovery_file + base_len, tmp_p, recovery_len) == 0){	// リカバリ・ファイルと基準が同じ
			len = wcslen(tmp_p);
			// 拡張子も同じか調べる
			if (len == recovery_len + 2){
				// インデックス・ファイルと比較する
				if (switch_p == 0){
					if (_wcsicmp(tmp_p + recovery_len, L"ar") == 0){
						printf_cp("filename is invalid, %s\n", tmp_p);
						return 1;
					}
				}
				// リカバリ・ファイル (*.PXX) と比較する
				for (i = 1; i <= 99; i++){
					if ((tmp_p[recovery_len    ] == '0' + (i / 10)) &&
						(tmp_p[recovery_len + 1] == '0' + (i % 10))){
						printf_cp("filename is invalid, %s\n", tmp_p);
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

wmain(int argc, wchar_t *argv[])
{
	wchar_t file_path[MAX_LEN], *list_buf;
	wchar_t par_comment[COMMENT_LEN], *tmp_p;
	int i, j;
	int switch_b = 0, switch_p = 0, switch_f = 0, switch_r = 0, switch_h = 0;
	int file_num = 0, source_num = 0, list_len = 0, parity_num = 0, first_vol = 1;
	__int64 block_size = 0;

	printf("Parchive 1.0 client version " FILE_VERSION " by Yutaka Sawada\n\n");
	if (argc < 3){
		printf("Self-Test: ");
		i = par1_checksum(file_path);
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
	recovery_file[0] = 0;
	par_comment[0] = 0;
	base_dir[0] = 0;
	ini_path[0] = 0;
	cp_output = GetConsoleOutputCP();
	memory_use = 0;

	// コマンド
	switch (argv[1][0]){
	case 'c': // create
	case 'v': // verify
	case 'r': // repair
	case 'l': // list
		break;
	default:
		print_help();
		return 0;
	}

	// オプションとリカバリ・ファイルの指定
	for (i = 2; i < argc; i++){
		tmp_p = argv[i];
		// オプション
		if (((tmp_p[0] == '/') || (tmp_p[0] == '-')) && (tmp_p[0] == argv[2][0])){
			tmp_p++;	// 先頭の識別文字をとばす
			// オプション
			if (wcscmp(tmp_p, L"b") == 0){
				switch_b = 1;
			} else if (wcscmp(tmp_p, L"br") == 0){
				switch_b = 2;
			} else if (wcscmp(tmp_p, L"f") == 0){
				switch_f = 1;
			} else if (wcscmp(tmp_p, L"fu") == 0){
				switch_f = 2;
			} else if (wcscmp(tmp_p, L"i") == 0){
				switch_p = 1;
			} else if (wcscmp(tmp_p, L"in") == 0){
				switch_p = 2;
			} else if (wcscmp(tmp_p, L"u") == 0){
				cp_output = CP_UTF8;
			} else if (wcscmp(tmp_p, L"h") == 0){
				switch_h = 1;

			// オプション (数値)
			} else if (wcsncmp(tmp_p, L"n", 1) == 0){
				j = 1;
				while ((j < 1 + 4) && (tmp_p[j] >= 48) && (tmp_p[j] <= 57)){
					parity_num = (parity_num * 10) + (tmp_p[j] - 48);
					j++;
				}
			} else if (wcsncmp(tmp_p, L"r", 1) == 0){
				j = 1;
				while ((j < 1 + 5) && (tmp_p[j] >= 48) && (tmp_p[j] <= 57)){
					switch_r = (switch_r * 10) + (tmp_p[j] - 48);
					j++;
				}
			} else if (wcsncmp(tmp_p, L"p", 1) == 0){
				j = 1;
				first_vol = 0;
				while ((j < 1 + 4) && (tmp_p[j] >= 48) && (tmp_p[j] <= 57)){
					first_vol = (first_vol * 10) + (tmp_p[j] - 48);
					j++;
				}
				if (first_vol < 1)
					first_vol = 1;
				if (first_vol > 99)
					first_vol = 99;
			} else if (wcsncmp(tmp_p, L"m", 1) == 0){
				if ((tmp_p[1] >= 48) && (tmp_p[1] <= 55))	// 0～7 の範囲
					memory_use = tmp_p[1] - 48;
			} else if (wcsncmp(tmp_p, L"vs", 2) == 0){
				recent_data = 0;
				j = 2;
				while ((j < 2 + 2) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					recent_data = (recent_data * 10) + (tmp_p[j] - '0');
					j++;
				}
				if ((recent_data == 8) || (recent_data > 15))
					recent_data = 0;

			// オプション (文字列)
			} else if (wcsncmp(tmp_p, L"c", 1) == 0){
				tmp_p++;
				if (wcslen(tmp_p) >= COMMENT_LEN){
					printf("comment is too long\n");
					return 1;
				}
				wcscpy(par_comment, tmp_p);
				//printf("comment = %S\n", tmp_p);
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
				printf_cp("invalid option, %s\n", tmp_p - 1);
				return 1;
			}
			continue;
		}
		// オプション以外ならリカバリ・ファイル
		j = copy_path_prefix(recovery_file, MAX_LEN - 4, tmp_p, NULL);	// 「.par」が追加されるかも
		if (j == 0){
			printf("PAR filename is invalid\n");
			return 1;
		}
		if (argv[1][0] == 'c'){ // 作成なら
			// 指定されたファイル名が適切か調べる
			if (sanitize_filename(offset_file_name(recovery_file)) != 0){
				printf("PAR filename is invalid\n");
				return 1;
			}
			// 拡張子が「.par」以外なら標準の拡張子を追加する
			j = wcslen(recovery_file);	// 浄化でファイル名が短縮されるかもしれない
			if (_wcsicmp(recovery_file + (j - 4), L".par"))
				wcscpy(recovery_file + j, L".par");
		} else { // 検査や修復なら
			if (_wcsicmp(recovery_file + (j - 4), L".par")){
				// 拡張子が「.par」以外でも「.p??」ならよい
				if ((recovery_file[j - 4] != '.') || 
						((recovery_file[j - 3] != 'p') && (recovery_file[j - 3] != 'P')) || 
						(recovery_file[j - 2] < 48) || (recovery_file[j - 2] > 57) || 
						(recovery_file[j - 1] < 48) || (recovery_file[j - 1] > 57) ){
				    wcscpy(recovery_file + j, L".par");
				}
			}
			j = GetFileAttributes(recovery_file);
			if ((j == INVALID_FILE_ATTRIBUTES) || (j & FILE_ATTRIBUTE_DIRECTORY)){
				wchar_t search_path[MAX_LEN];
				int name_len, dir_len, path_len, find_flag = 0;
				HANDLE hFind;
				WIN32_FIND_DATA FindData;
				path_len = wcslen(recovery_file);
				if (wcspbrk(offset_file_name(recovery_file), L"*?") != NULL){
					// 「*」や「?」で検索する場合は指定された拡張子を優先する
					hFind = FindFirstFile(recovery_file, &FindData);
					if (hFind != INVALID_HANDLE_VALUE){
						get_base_dir(recovery_file, search_path);
						dir_len = wcslen(search_path);
						do {
							//printf("file name = %S\n", FindData.cFileName);
							name_len = wcslen(FindData.cFileName);
							if ((dir_len + name_len < MAX_LEN) &&	// ファイル名が長すぎない
									(_wcsicmp(FindData.cFileName + (name_len - 4), recovery_file + (path_len - 4)) == 0)){
								find_flag = 1;
								break;	// 見つけたファイル名で問題なし
							}
						} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
						FindClose(hFind);
					}
				}
				if (find_flag == 0){	// リカバリ・ファイルの拡張子を「.p??」にして検索する
					wcscpy(search_path, recovery_file);
					search_path[path_len - 2] = '?';
					search_path[path_len - 1] = '?';
					hFind = FindFirstFile(search_path, &FindData);
					if (hFind != INVALID_HANDLE_VALUE){
						get_base_dir(recovery_file, search_path);
						dir_len = wcslen(search_path);
						do {
							//printf("file name = %S\n", FindData.cFileName);
							name_len = wcslen(FindData.cFileName);
							if ((dir_len + name_len < MAX_LEN) &&	// ファイル名が長すぎない
									 (FindData.cFileName[name_len - 4] == '.') &&
									((FindData.cFileName[name_len - 3] == 'p') || (FindData.cFileName[name_len - 3] == 'P')) &&
									 (FindData.cFileName[name_len - 2] >= '0') && (FindData.cFileName[name_len - 2] <= '9') &&
									 (FindData.cFileName[name_len - 1] >= '0') && (FindData.cFileName[name_len - 1] <= '9')){
								find_flag = 1;
								break;	// 見つけたファイル名で問題なし
							}
						} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
						FindClose(hFind);
					}
				}
				if (find_flag == 0){
					printf("valid file is not found\n");
					return 1;
				}
				wcscpy(recovery_file + dir_len, FindData.cFileName);
			}
		}
		break;
	}
	//printf("m = %d, f = %d, p = %d, n = %d\n", switch_b, switch_f, switch_p, parity_num);
	if (recovery_file[0] == 0){	// リカバリ・ファイルが指定されてないなら
		printf("PAR file is not specified\n");
		return 1;
	}

	// input file の位置が指定されて無くて、
	// 最初のソース・ファイル指定に絶対パスが含まれてるなら、それを使う
	if (argv[1][0] == 'c'){
		if ((base_dir[0] == 0) && (switch_f == 0) && (i + 1 < argc)){
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
		get_base_dir(recovery_file, base_dir);	// リカバリ・ファイルの位置にする

	// 環境の表示
	print_environment();

	switch (argv[1][0]){
	case 'c':
		// リカバリ・ファイル作成ならソース・ファイルのリストがいる
		i++;
		if (i >= argc){
			printf("input file is not specified\n");
			return 1;
		}
		if (switch_f){	// ファイル・リストの読み込み
			list_buf = read_list(argv[i], &file_num, &list_len, &source_num, &block_size, switch_f);
			if (list_buf == NULL)
				return 1;
		} else {	// 入力ファイルの指定
			int base_len, list_max;
			unsigned int filter = get_show_hidden();
			base_len = wcslen(base_dir);
			list_max = 0;
			list_buf = NULL;
			for (; i < argc; i++){
				// 絶対パスや、親パスへの移動を含むパスは危険なのでファイル名だけにする
				tmp_p = argv[i];
				j = copy_path_prefix(file_path, MAX_LEN - ADD_LEN, tmp_p, base_dir);	// 絶対パスにしてから比較する
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
				if (wcschr(file_path + base_len, '\\') != NULL){
					free(list_buf);
					printf_cp("sub-directory is not supported, %s\n", file_path + base_len);
					return 1;
				}
				//printf("%d = %S\nsearch = %S\n", i, argv[i], file_path);
				// 「*」や「?」で検索しない場合、ファイルが見つからなければエラーにする
				j = -1;
				if (wcspbrk(file_path + base_len, L"*?") == NULL)
					j = file_num;
				// ファイルを検索する
				list_buf = search_files(list_buf, file_path, base_len, j, &file_num, &list_max, &list_len, &source_num, &block_size, filter);
				if (list_buf == NULL)
					return 1;
				if ((j != -1) && (j == file_num)){	// ファイルが見つかったか確かめる
					free(list_buf);
					printf_cp("input file is not found, %s\n", file_path + base_len);
					return 1;
				}
			}
		}
		if (list_len == 0){
			if (list_buf)
				free(list_buf);
			printf("input file is not found\n");
			return 1;
		}
		if (check_recovery_match(file_num, list_buf, list_len, switch_p & 2)){
			free(list_buf);
			return 1;
		}

		// ファイル・リストの内容を並び替える (リストの末尾は null 1個にすること)
		sort_list(list_buf, list_len);
		//printf("file_num = %d, source_num = %d, block_size = %I64d\n", file_num, source_num, block_size);

		if ((parity_num == 0) && (switch_r > 0)){
			parity_num = (source_num * switch_r) / 100;
			if (parity_num == 0)
				parity_num = 1;
		}
		printf("Input File count: %d\n", file_num);
		printf("Data File count : %d\n", source_num);
		printf("Max file size\t: %I64d\n", block_size);
		printf("Parity Volume count\t: %d\n", parity_num);
		if (first_vol > 1)
			printf("Parity Volume start\t: %d\n", first_vol);
		if (source_num != 0){
			i = 100 * parity_num / source_num;
		} else {
			i = 0;
		}
		printf("Redundancy rate : %d%%\n", i);
		if (source_num + parity_num + first_vol - 1 > 256){
			free(list_buf);
			printf("too many total volumes %d\n", source_num + parity_num + first_vol - 1);
			return 1;
		}
		if (parity_num + first_vol - 1 > 99){
			free(list_buf);
			printf("too many parity volumes %d\n", parity_num + first_vol - 1);
			return 1;
		}
		i = par1_create(switch_p & 2, source_num, block_size, parity_num, first_vol,
				file_num, list_buf, list_len, par_comment);
		break;
	case 'v':
	case 'r':
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
		if (argv[1][0] == 'v'){
			i = par1_verify(switch_b, switch_p & 1, par_comment);
		} else {
			i = par1_repair(switch_b, switch_p & 1, par_comment);
		}
		break;
	case 'l':
		i = par1_list(switch_h, par_comment);
		break;
	}

	//printf("ExitCode: 0x%02X\n", i);
	return i;
}

