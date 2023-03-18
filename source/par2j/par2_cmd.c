// par2_cmd.c
// Copyright : 2023-03-18 Yutaka Sawada
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

#include "common2.h"
#include "par2.h"
#include "ini.h"
#include "json.h"
#include "lib_opencl.h"
#include "version.h"


static void print_help(void)
{
	printf(
"Usage\n"
"t(rial)  [options] <par file> [input files]\n"
"c(reate) [options] <par file> [input files]\n"
"  available: f,fu,fo,fa,fe,ss,sn,sr,sm,rr,rn,rp,rs,rd,rf,ri,\n"
"\t     lr,lp,ls,lc,m,vs,vd,c,d,in,up,uo\n"
"v(erify) [options] <par file> [external files]\n"
"r(epair) [options] <par file> [external files]\n"
"  available: f,fu,fo,lc,m,vl,vs,vd,d,uo,w,b,br,bi\n"
"l(ist)   [uo,h   ] <par file>\n"
"\nOption\n"
" /f    : Use file-list instead of filename\n"
" /fu   : Use file-list which is encoded with UTF-8\n"
" /fo   : Search file only for wildcard\n"
" /fa\"*\": Adding file at search with wildcard\n"
" /fe\"*\": Excluding file at search with wildcard\n"
" /ss<n>: Slice size\n"
" /sn<n>: Number of source blocks\n"
" /sr<n>: Rate of source block count and size\n"
" /sm<n>: Slice size becomes a multiple of this value\n"
" /rr<n>: Rate of redundancy (%%)\n"
" /rn<n>: Number of recovery blocks\n"
" /rp<n>: Number of possible recovered files\n"
" /rs<n>: Starting recovery block number\n"
" /rd<n>: How to distribute recovery blocks to recovery files\n"
" /rf<n>: Number of recovery files\n"
" /ri   : Use file index to name recovery files\n"
" /lr<n>: Limit number of recovery blocks in a recovery file\n"
" /lp<n>: Limit repetition of packets in a recovery file\n"
" /ls<n>: Limit size of splited files\n"
" /lc<n>: Limit CPU feature\n"
" /m<n> : Memory usage\n"
" /vl<n>: Verification level\n"
" /vs<n>: Skip verification by recent result\n"
" /vd\"*\": Set directory of recent result\n"
" /c\"*\" : Set comment\n"
" /d\"*\" : Set directory of input files\n"
" /in   : Do not create index file\n"
" /up   : Write Unicode Filename packet for non-ASCII filename\n"
" /uo   : Console output is encoded with UTF-8\n"
" /w    : Write information on JSON file\n"
" /p    : Purge recovery files when input files are complete\n"
" /b    : Backup existing files at repair\n"
" /br   : Send existing files into recycle bin at repair\n"
" /bi   : Replace files even when repair was failed\n"
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

	printf("CPU thread\t: %d / %d\n", cpu_num & 0xFFFF, cpu_num >> 24);
	cpu_num &= 0xFFFF;	// 利用するコア数だけにしておく
	printf("CPU cache limit : %d KB, %d KB\n", (cpu_cache & 0x7FFF8000) >> 10, (cpu_cache & 0x00007FFF) << 7);
#ifndef _WIN64	// 32-bit 版は MMX, SSE2, SSSE3 のどれかを表示する
	printf("CPU extra\t:");
	if (cpu_flag & 1){
		if (cpu_flag & 256){
			printf(" SSSE3(old)");
		} else {
			printf(" SSSE3");
		}
	} else if (cpu_flag & 128){
		printf(" SSE2");
	} else {
		printf(" MMX");
	}
#else	// 64-bit 版は SSE2, SSSE3 を表示する
	printf("CPU extra\t: x64");
	if (cpu_flag & 1){
		if (cpu_flag & 256){
			printf(" SSSE3(old)");
		} else {
			printf(" SSSE3");
		}
	} else if (cpu_flag & 128){
		printf(" SSE2");
	}
#endif
	if (cpu_flag & 8)
		printf(" CLMUL");
	if (cpu_flag & 16)
		printf(" AVX2");
	printf("\nMemory usage\t: ");
	if (memory_use & 7){
		printf("%d/8", memory_use & 7);
	} else {
		printf("Auto");
	}
	statex.dwLength = sizeof(statex);
	if (GlobalMemoryStatusEx(&statex))
		printf(" (%I64d MB available)", statex.ullAvailPhys >> 20);
	if ((memory_use & ~7) == 0){	// HDD と SSD を自動判別する
		check_seek_penalty(base_dir);
		//printf("\n check_seek_penalty = %d\n", check_seek_penalty(base_dir));
	}
	if (memory_use & 16){		// SSD
		if (memory_use & 32){
			printf(", Fast SSD");
		} else {
			printf(", SSD");
		}
	} else if (memory_use & 8){	// HDD
		printf(", HDD");
	}
	printf("\n\n");
}

// 格納ファイル・リストを読み込んでバッファーに書き込む
static int read_list(
	wchar_t *list_path,		// リストのパス
	unsigned int code_page)	// CP_OEMCP か CP_UTF8
{
	char buf[MAX_LEN * 3];
	wchar_t file_name[MAX_LEN], file_path[MAX_LEN];
	int len;
	__int64 file_size;
	FILE *fp;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

	list_len = 0;
	list_max = ALLOC_LEN;
	list_buf = (wchar_t *)malloc(list_max * 2);
	if (list_buf == NULL){
		printf("malloc, %d\n", list_max * 2);
		return 1;
	}

	// 読み込むファイルを開く
	fp = _wfopen(list_path, L"rb");
	if (fp == NULL){
		printf_cp("cannot open file-list, %s\n", list_path);
		return 1;
	}

	// 一行ずつ読み込む
	wcscpy(file_path, base_dir);
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

		// 読み込んだ内容をユニコードに変換する、末尾のディレクトリ記号の分を空けておく
		if (!MultiByteToWideChar(code_page, 0, buf, -1, file_name, MAX_LEN - 1)){
			fclose(fp);
			printf("MultiByteToWideChar, %s\n", buf);
			return 1;
		}

		// ファイルが基準ディレクトリ以下に存在することを確認する
		len = copy_path_prefix(file_path, MAX_LEN - ADD_LEN, file_name, base_dir);	// 絶対パスにしてから比較する
		if (len == 0){
			fclose(fp);
			printf_cp("filename is invalid, %s\n", file_name);
			return 1;
		}
		if ((len <= base_len) || (_wcsnicmp(base_dir, file_path, base_len) != 0)){	// 基準ディレクトリ外なら
			fclose(fp);
			printf_cp("out of base-directory, %s\n", file_path);
			return 1;
		}
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			fclose(fp);
			printf_cp("input file is not found, %s\n", file_path);
			return 1;
		}

		// ファイル名が重複しないようにする
		wcscpy(file_name, file_path + base_len);
		if (search_file_path(list_buf, list_len, file_name))
			continue;

		if (AttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){	// フォルダなら
			len = (int)wcslen(file_name);
			if (file_name[len - 1] != '\\'){
				file_name[len] = '\\';	// フォルダの末尾は必ず「\」にする
				file_name[len + 1] = 0;
			}
			file_size = 0;
			split_size = 0;
		} else {	// ファイルなら
			file_size = ((__int64)AttrData.nFileSizeHigh << 32) | (__int64)AttrData.nFileSizeLow;
			total_file_size += file_size;
			if (file_size > 0)
				entity_num++;
			// サブ・ディレクトリまたはフォルダを含む場合はファイル分割を無効にする
			if ((split_size != 0) && (wcschr(file_name, '\\') != NULL))
				split_size = 0;
		}

		// リストにコピーする
		if (add_file_path(file_name)){
			fclose(fp);
			printf("add_file_path\n");
			return 1;
		}
		file_num++;
	}
	fclose(fp);
/*
fp = fopen("list_buf.txt", "wb");
len = 0;
while (len < list_len){
	fwprintf(fp, L"%s\n", list_buf + len);
	len += (wcslen(list_buf + len) + 1);
}
fclose(fp);
*/
	return 0;
}

// ファイルを検索してファイル・リストに追加する
static int search_files(
	wchar_t *search_path,	// 検索するファイルのフル・パス、* ? も可
	int dir_len,			// ディレクトリ部分の長さ
	unsigned int filter,	//  2, FILE_ATTRIBUTE_HIDDEN    = 隠しファイルを無視する
							//  4, FILE_ATTRIBUTE_SYSTEM    = システムファイルを無視する
							// 16, FILE_ATTRIBUTE_DIRECTORY = ディレクトリを無視する
	int single_file)		// -1 = *や?で検索指定、0～ = 単独指定
{
	int len, dir_len2, old_num;
	unsigned int attrib_filter;
	__int64 file_size;
	HANDLE hFind;
	WIN32_FIND_DATA FindData;

	// 隠しファイルを見つけるかどうか
	attrib_filter = filter & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
	if (attrib_filter == 0)
		attrib_filter = INVALID_FILE_ATTRIBUTES;

	if (list_buf == NULL){
		list_len = 0;
		list_max = ALLOC_LEN;
		list_buf = (wchar_t *)malloc(list_max * 2);
		if (list_buf == NULL){
			printf("malloc, %d\n", list_max * 2);
			return 1;
		}
	}

	// 末尾に「\」を付けてフォルダを指定してるなら内部を検索しない
	len = (int)wcslen(search_path);
	if (search_path[len - 1] == '\\'){
		unsigned int rv;
		rv = GetFileAttributes(search_path);
		if ((rv != INVALID_FILE_ATTRIBUTES) && (rv & FILE_ATTRIBUTE_DIRECTORY)){	// そのフォルダが存在するなら
			if (!search_file_path(list_buf, list_len, search_path + base_len)){	// フォルダ名が重複しないようにする
				// そのフォルダを追加する
				if (add_file_path(search_path + base_len)){
					printf("add_file_path\n");
					return 1;
				}
				file_num++;
			}
		}
		return 0;
	}

	// 検索する
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind == INVALID_HANDLE_VALUE)	// 見つからなかったら
		return 0;
	do {
		if ((wcscmp(FindData.cFileName, L".") == 0) || (wcscmp(FindData.cFileName, L"..") == 0))
			continue;	// 自分や親のパスは無視する
		if ((single_file < 0) && ((FindData.dwFileAttributes & attrib_filter) == attrib_filter))
			continue;	// 検索中は隠し属性が付いてるファイルを無視する

		len = (int)wcslen(FindData.cFileName);	// 見つけたファイル名の文字数
		if (dir_len + len >= MAX_LEN - ADD_LEN - 2){	// 末尾に「\*」を付けて再検索するので
			FindClose(hFind);
			printf("filename is too long\n");
			return 1;
		}
		// 現在のディレクトリ部分に見つかったファイル名を連結する
		wcscpy(search_path + dir_len, FindData.cFileName);

		// フォルダなら
		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			if ((filter & FILE_ATTRIBUTE_DIRECTORY) == 0){
/*
				if (list2_buf){	// 途中のフォルダ名も比較して除外するなら
					int off = 0;
					while (off < list2_len){
						if (list2_buf[off++] == '-'){	// deny
							if (PathMatchWild(search_path + base_len, list2_buf + off) != 0){
								printf_cp("deny : \"%s\"", search_path + base_len);
								printf_cp(" : \"%s\"\n", list2_buf + off);
								off = -1;
								break;
							}
						}
						off += wcslen(list2_buf + off) + 1;
					}
					if (off < 0)
						continue;	// 除外対象なら無視する
				}
*/

				// フォルダの末尾は「\」にする
				wcscat(search_path, L"\\");
				if (!search_file_path(list_buf, list_len, search_path + base_len)){	// フォルダ名が重複しないようにする
					split_size = 0;
					old_num = file_num;	// そのフォルダの中にファイルが何個あったのかを調べる

					// そのフォルダの中身を更に検索する
					dir_len2 = (int)wcslen(search_path);
					search_path[dir_len2    ] = '*';	// 末尾に「*」を追加する
					search_path[dir_len2 + 1] = 0;
					if (search_files(search_path, dir_len2, filter, single_file)){
						FindClose(hFind);
						printf("cannot search inner folder\n");
						return 1;
					}
					if (old_num == file_num){	// 空のフォルダも含める
						search_path[dir_len2] = 0;	// 検索用に追加した「*」を取り除く

						if (list2_buf){	// 除外するフォルダ名が指定されてるなら
							if (exclude_path(search_path + base_len) != 0)
								continue;	// 除外対象なら無視する
						}

						// リストにコピーする
						if (add_file_path(search_path + base_len)){
							FindClose(hFind);
							printf("add_file_path\n");
							return 1;
						}
						file_num++;
					}
				}
			}
		} else {	// ファイルなら
			if ((single_file < 0) && (list2_buf)){	// 除外するファイル名が指定されてるなら
				if (exclude_path(search_path + base_len) != 0)
					continue;	// 除外対象なら無視する
			}

			if (!search_file_path(list_buf, list_len, search_path + base_len)){	// ファイル名が重複しないようにする
				// リストにコピーする
				if (add_file_path(search_path + base_len)){
					FindClose(hFind);
					printf("add_file_path\n");
					return 1;
				}
				file_num++;
				file_size = ((__int64)FindData.nFileSizeHigh << 32) | (__int64)FindData.nFileSizeLow;
				total_file_size += file_size;
				if (file_size > 0)
					entity_num++;
				// サブ・ディレクトリまたはフォルダを含む場合はファイル分割を無効にする
				if ((split_size != 0) && (wcschr(search_path + base_len, '\\') != NULL))
					split_size = 0;
			}
		}
	} while (FindNextFile(hFind, &FindData)); // 次のファイルを検索する
	FindClose(hFind);

	return 0;
}

// 外部ファイルのリストを読み込んでバッファーに書き込む
static int read_external_list(
	wchar_t *list_path,		// リストのパス
	unsigned int code_page)	// CP_OEMCP か CP_UTF8
{
	char buf[MAX_LEN * 3];
	wchar_t file_name[MAX_LEN], file_path[MAX_LEN], *tmp_p;
	int len, rv;
	FILE *fp;

	// 読み込むファイルを開く
	fp = _wfopen(list_path, L"rb");
	if (fp == NULL){
		printf_cp("cannot open file-list, %s\n", list_path);
		return 1;
	}

	// 一行ずつ読み込む
	wcscpy(file_path, base_dir);
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

		// 読み込んだ内容をユニコードに変換する、末尾のディレクトリ記号の分を空けておく
		if (!MultiByteToWideChar(code_page, 0, buf, -1, file_name, MAX_LEN - 1)){
			fclose(fp);
			printf("MultiByteToWideChar, %s\n", buf);
			return 1;
		}

		// ファイルが存在することを確認する
		len = copy_path_prefix(file_path, MAX_LEN - ADD_LEN, file_name, NULL);
		if (len == 0){
			fclose(fp);
			printf_cp("filename is invalid, %s\n", file_name);
			return 1;
		}
		//printf_cp("external file, %s\n", file_path);
		rv = GetFileAttributes(file_path);
		if ((rv == INVALID_FILE_ATTRIBUTES) || (rv & FILE_ATTRIBUTE_DIRECTORY)){
			fclose(fp);
			printf_cp("external file is not found, %s\n", file_path);
			return 1;
		}
		if (!search_file_path(list2_buf, list2_len, file_path)){	// ファイル名が重複しないようにする
			if (list2_len + len >= list2_max){ // 領域が足りなくなるなら拡張する
				list2_max += ALLOC_LEN;
				tmp_p = (wchar_t *)realloc(list2_buf, list2_max * 2);
				if (tmp_p == NULL){
					fclose(fp);
					printf("realloc, %d\n", list2_max);
					return 1;
				} else {
					list2_buf = tmp_p;
				}
			}
			// そのファイルを追加する
			//printf_cp("add external file, %s\n", file_path);
			wcscpy(list2_buf + list2_len, file_path);
			list2_len += len + 1;
		}
	}
	fclose(fp);

	return 0;
}

// 外部のファイルを検索してファイル・リストに追加する
static int search_external_files(
	wchar_t *search_path,	// 検索するファイルのフル・パス、* ? も可
	int dir_len,			// ディレクトリ部分の長さ
	int single_file)		// -1 = *や?で検索指定、0～ = 単独指定
{
	wchar_t *tmp_p;
	int len;
	HANDLE hFind;
	WIN32_FIND_DATA FindData;

	// 検索する
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind == INVALID_HANDLE_VALUE)	// 見つからなかったら
		return 0;
	do {
		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;	// フォルダは無視する
		if ((single_file < 0) && (FindData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
			continue;	// 検索中は隠し属性が付いてるファイルを無視する
		len = dir_len + (int)wcslen(FindData.cFileName);
		if (len >= MAX_LEN)
			continue;	// 長すぎるファイル名は無視する

		// 現在のディレクトリ部分に見つかったファイル名を連結する
		wcscpy(search_path + dir_len, FindData.cFileName);
		if (!search_file_path(list2_buf, list2_len, search_path)){	// ファイル名が重複しないようにする
			if (list2_len + len >= list2_max){ // 領域が足りなくなるなら拡張する
				list2_max += ALLOC_LEN;
				tmp_p = (wchar_t *)realloc(list2_buf, list2_max * 2);
				if (tmp_p == NULL){
					printf("realloc, %d\n", list2_max);
					return 1;
				} else {
					list2_buf = tmp_p;
				}
			}
			// そのファイルを追加する
			//printf_cp("add external file, %s\n", search_path);
			wcscpy(list2_buf + list2_len, search_path);
			list2_len += len + 1;
		}
	} while (FindNextFile(hFind, &FindData)); // 次のファイルを検索する
	FindClose(hFind);

	return 0;
}

// リカバリ・ファイルとソース・ファイルが同じ名前にならないようにする
static int check_recovery_match(int switch_p)	// インデックス・ファイルを作らない
{
	wchar_t recovery_base[MAX_LEN], file_ext[EXT_LEN], *tmp_p;
	int num, len, list_off = 0, ext_len, recovery_len;

	// 基準ディレクトリ外にリカバリ・ファイルが存在するなら問題なし
	if (_wcsnicmp(recovery_file, base_dir, base_len) != 0)
		return 0;

	// リカバリ・ファイルの拡張子には指定されたものを使う
	ext_len = 0;
	wcscpy(recovery_base, recovery_file);
	tmp_p = offset_file_name(recovery_base);
	tmp_p = wcsrchr(tmp_p, '.');
	if (tmp_p != NULL){
		if (wcslen(tmp_p) < EXT_LEN){
			wcscpy(file_ext, tmp_p);	// 拡張子を記録しておく
			ext_len = (int)wcslen(file_ext);
			*tmp_p = 0;	// 拡張子を取り除く
		}
	}
	recovery_len = (int)wcslen(recovery_base + base_len);

	// ファイルごとに比較する
	for (num = 0; num < file_num; num++){
		tmp_p = list_buf + list_off;
		while (list_buf[list_off] != 0)
			list_off++;
		list_off++;

		// ファイル名との前方一致を調べる
		if (_wcsnicmp(recovery_base + base_len, tmp_p, recovery_len) == 0){	// リカバリ・ファイルと基準が同じ
			len = (int)wcslen(tmp_p);
			// 拡張子も同じか調べる
			if ((ext_len != 0) && (len > ext_len)){
				if (_wcsnicmp(file_ext, tmp_p + (len - ext_len), ext_len) == 0){
					if (recovery_len + ext_len == len){
						// インデックス・ファイルと比較する
						if (switch_p == 0){
							printf_cp("filename is invalid, %s\n", tmp_p);
							return 1;
						}
					} else if (len - recovery_len - ext_len >= EXT_LEN){
						if (_wcsnicmp(tmp_p + recovery_len, L".vol", 4) == 0){
							printf_cp("filename is invalid, %s\n", tmp_p);
							return 1;
						}
					}
				}
			}
		}
	}
	return 0;
}

#define MAX_ADJUST 1024

// 最適なブロック・サイズを調べる
static int check_block_size(unsigned int adjust_size, int alloc_method, int limit_num)
{
	wchar_t file_path[MAX_LEN];
	int list_off = 0, i, target, loop_count;
	int lower_num, upper_num, new_num, sbc_rate;
	int source_max_num, source_min_num, source_num_best;
	unsigned int base_size, max_size, min_size, new_size, target_size;
	unsigned int block_max_size, block_min_size, block_size_best;
	__int64 *file_size, pad_size, pad_size_best;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

	base_size = 4;
	if (adjust_size != 0)
		base_size = adjust_size;

	// 各ファイルのサイズを取得する
	file_size = (__int64 *)malloc(sizeof(__int64) * file_num);
	if (file_size == NULL){
		printf("malloc, %zd\n", sizeof(__int64) * file_num);
		return 1;
	}
	wcscpy(file_path, base_dir);
	block_max_size = 0;	// ファイルが小さいとブロック・サイズの最大値も小さくなる
	for (i = 0; i < file_num; i++){
		wcscpy(file_path + base_len, list_buf + list_off);
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			printf_cp("GetFileAttributesEx, %s\n", list_buf + list_off);
			free(file_size);
			return 1;
		}
		if (AttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){	// フォルダなら
			file_size[i] = 0;
		} else {
			file_size[i] = ((__int64)AttrData.nFileSizeHigh << 32) | (unsigned __int64)AttrData.nFileSizeLow;
			if (file_size[i] >= MAX_BLOCK_SIZE){
				block_max_size = MAX_BLOCK_SIZE;
			} else if (file_size[i] > (__int64)block_max_size){
				block_max_size = (unsigned int)(file_size[i]);
			}
		}
		while (list_buf[list_off] != 0)
			list_off++;
		list_off++;
	}

	// ブロック・サイズの最大値 = ブロック数の最小値
	// 最大ファイルのサイズを単位の倍数にしたサイズのはず
	if (base_size == 4){	// 切り上げで単位の倍数にする
		block_max_size = (block_max_size + 3) & 0xFFFFFFFC;
	} else {
		block_max_size = ((block_max_size + base_size - 1) / base_size) * base_size;
		if (block_max_size > MAX_BLOCK_SIZE)
			block_max_size -= base_size;
	}
	source_min_num = 0;
	for (i = 0; i < file_num; i++){
		if (file_size[i] > 0)
			source_min_num += (int)((file_size[i] + (__int64)block_max_size - 1) / (__int64)block_max_size);
	}
	// ブロック・サイズの最小値 = ブロック数の最大値
	if ((source_min_num >= MAX_SOURCE_NUM) || (block_max_size <= base_size)){	// ファイルが多すぎ、または小さすぎ
		source_max_num = source_min_num;
		block_min_size = block_max_size;
	} else {
		pad_size = (total_file_size + MAX_SOURCE_NUM - 1) / MAX_SOURCE_NUM;	// 最も多く分割するには
		if (pad_size > (__int64)block_max_size - base_size)
			pad_size = (__int64)block_max_size - base_size;
		block_min_size = (unsigned int)pad_size;
		if (base_size == 4){	// 切り上げで単位の倍数にする
			block_min_size = (block_min_size + 3) & 0xFFFFFFFC;
		} else {
			block_min_size = ((block_min_size + base_size - 1) / base_size) * base_size;
		}
		source_max_num = 0;
		for (i = 0; i < file_num; i++){
			if (file_size[i] > 0)
				source_max_num += (int)((file_size[i] + (__int64)block_min_size - 1) / (__int64)block_min_size);
		}
	}
	if (source_max_num > MAX_SOURCE_NUM){	// 最大ブロック数を超えたら、最も多くなるサイズを探す
		i = source_max_num / source_min_num;	// 超えた時と何倍ぐらい違うか
		new_size = (block_min_size * i + block_max_size) / (1 + i);	// 超えたサイズと最大ブロック・サイズの間
		if (base_size == 4){	// 切り捨てで単位の倍数にする
			new_size &= 0xFFFFFFFC;
		} else {
			new_size -= new_size % base_size;
		}
		//printf("over = %d (%d blocks), rate = %d, new_size = %d\n", block_min_size, source_max_num, i, new_size);
		min_size = block_min_size;
		block_min_size = block_max_size;	// 最小ブロック・サイズの最大値にする
		source_max_num = source_min_num;	// そのブロック数
		while (block_min_size - min_size > base_size){
			new_num = 0;
			for (i = 0; i < file_num; i++){
				if (file_size[i] > 0)
					new_num += (int)((file_size[i] + (__int64)new_size - 1) / (__int64)new_size);
			}
			if (new_num > MAX_SOURCE_NUM){	// 指定したブロック数を越えたら、ブロック・サイズを大きくする
				min_size = new_size;	// 越えたサイズを最小値にする
			} else {	// ブロック・サイズを小さくする
				source_max_num = new_num;
				block_min_size = new_size;	// 越えなかったら最小ブロック・サイズの最大値にする
			}
			new_size = (min_size + block_min_size) / 2;	// 最小値と最大値の中間
			if (base_size == 4){	// 切り捨てで単位の倍数にする
				new_size &= 0xFFFFFFFC;
			} else {
				new_size -= new_size % base_size;
			}
			if (new_size <= min_size)
				new_size = min_size + base_size;
			//printf("over size = %d, ok = %d (%d blocks)\n", min_size, block_min_size, source_max_num);
		}
	}
	//printf("max = %d (%d blocks), min = %d (%d blocks)\n", block_max_size, source_min_num, block_min_size, source_max_num);

	// ブロック・サイズの初期値を決める
	max_size = block_max_size;
	min_size = block_min_size;
	lower_num = source_min_num;
	upper_num = source_max_num;
	if (alloc_method > 0){	// ブロック・サイズが指定された
		target_size = alloc_method;
		if (target_size >= block_max_size){	// ブロック・サイズの範囲内にする
			target_size = block_max_size;
			block_size = target_size;
		} else if (target_size <= block_min_size){
			target_size = block_min_size;
			block_size = target_size;
		} else {	// 単位の倍数で指定されたサイズに近い方にする
			block_size = ((target_size + (base_size / 2)) / base_size) * base_size;
			if (block_size > block_max_size)
				block_size -= base_size;
			if (block_size < block_min_size)
				block_size += base_size;
		}
		source_num = 0;
		for (i = 0; i < file_num; i++){
			if (file_size[i] > 0)	// ブロック数を計算する
				source_num += (int)((file_size[i] + block_size - 1) / (__int64)block_size);
		}
		if ((limit_num > 0) && (source_num > limit_num)){	// ブロック数の制限を越えたら
			alloc_method = 0;	// ブロック数を指定しなおす
			max_size = block_max_size;
			min_size = block_min_size;
			lower_num = source_min_num;
			upper_num = source_max_num;
		}

	} else if (alloc_method < 0){	// 割合(ブロック・サイズ * ? = ブロック数)が指定された
		target = -alloc_method;	// 識別用に負にしてたのを正に戻す
		// ブロック・サイズの初期値は単純に合計ファイル・サイズから計算する
		// SBC=root(F*rate/10000)=root(F*rate)/100
		pad_size = total_file_size * (__int64)target;	// rate/10000 だが root後なら /100 になる
		new_num = sqrt64(pad_size);
		new_num = (new_num + 50) / 100;	// 小数点以下は四捨五入する
		if (new_num > source_max_num){
			new_num = source_max_num;
		} else if (new_num < source_min_num){
			new_num = source_min_num;
		}
		if (new_num < 2)
			new_num = 2;	// 最低でも2ブロックにする
		pad_size = new_num;
		pad_size = (total_file_size + pad_size - 1) / pad_size;
		if (pad_size >= (__int64)block_max_size){
			new_size = block_max_size;
		} else {
			new_size = (unsigned int)pad_size;
			if (new_size <= block_min_size){
				new_size = block_min_size;
			} else {
				new_size = ((new_size + (base_size / 2)) / base_size) * base_size;	// 四捨五入して単位の倍数にする
			}
		}
		// 目標の割合を範囲内にする
		sbc_rate = ((10000 * source_min_num) + (block_max_size / 2)) / block_max_size;
		if (target <= sbc_rate){
			if (sbc_rate < 10){
				target = 10;	// 0.1% 未満にはしない
			} else {
				target = sbc_rate;
			}
		} else {
			sbc_rate = ((10000 * source_max_num) + (block_min_size / 2)) / block_min_size;
			if (target > sbc_rate)
				target = sbc_rate;
		}
		//printf("target rate = %d.%02d%%, size = %d, max = %d, min = %d\n", target /100, target %100, new_size, max_size, min_size);
		// 目標に近づける
		while (max_size - min_size > base_size){
			new_num = 0;
			for (i = 0; i < file_num; i++){
				if (file_size[i] > 0)
					new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
			}

			sbc_rate = ((10000 * new_num) + (new_size / 2)) / new_size;
			//printf(" new_num = %d, new_size = %d, rate = %d.%02d%%\n", new_num, new_size, sbc_rate /100, sbc_rate %100);
			if (sbc_rate > target){	// 指定した割合を越えたら、ブロック・サイズを大きくする
				upper_num = new_num;
				min_size = new_size;	// 越えたサイズを最小値にする
				new_size = (min_size + max_size) / 2;	// 最小値と最大値の中間
				if (base_size == 4){	// 切り上げで単位の倍数にする
					new_size = (new_size + 3) & 0xFFFFFFFC;
				} else {
					new_size = ((new_size + base_size - 1) / base_size) * base_size;
				}
				if (new_size >= max_size)
					new_size = max_size - base_size;
			} else {	// ブロック・サイズを小さくする
				lower_num = new_num;
				max_size = new_size;	// 越えなかったサイズを最大値にする
				new_size = (min_size + max_size) / 2;	// 最小値と最大値の中間
				if (base_size == 4){	// 切り捨てで単位の倍数にする
					new_size &= 0xFFFFFFFC;
				} else {
					new_size -= new_size % base_size;
				}
				if (new_size <= min_size)
					new_size = min_size + base_size;
			}
			//printf(" lower = %d, max = %d, upper = %d, min = %d\n", lower_num, max_size, upper_num, min_size);
			if (lower_num == upper_num)
				break;
		}
		if (lower_num == upper_num){	// 同じブロック数なら小さいブロック・サイズの方が効率がいい
			block_size = min_size;
			source_num = upper_num;
		} else {
			sbc_rate = ((10000 * lower_num) + (max_size / 2)) / max_size;
			i = ((10000 * upper_num) + (min_size / 2)) / min_size;
			// 割合が近い方にする
			if ((target - sbc_rate) < (i - target)){
				block_size = max_size;
				source_num = lower_num;
			} else {
				block_size = min_size;
				source_num = upper_num;
			}
		}
		if ((limit_num > 0) && (source_num > limit_num)){	// ブロック数の制限を越えたら
			if (lower_num > limit_num){
				alloc_method = 0;	// ブロック数を指定しなおす
				max_size = block_max_size;
				min_size = block_min_size;
				lower_num = source_min_num;
				upper_num = source_max_num;
			} else {	// 少ない方を使う
				block_size = max_size;
				source_num = lower_num;
			}
		}
	}
	if (alloc_method == 0){	// ブロック数が指定された、またはブロック数の制限を越えた
		target = limit_num;
		if (target > source_max_num){	// 目標ブロック数を範囲内にする
			target = source_max_num;
		} else if (target < source_min_num){
			target = source_min_num;
		}
		// ブロック・サイズの初期値は単純に合計ファイル・サイズを目標ブロック数で割る
		pad_size = target;
		pad_size = (total_file_size + pad_size - 1) / pad_size;
		if (pad_size >= (__int64)block_max_size){
			new_size = block_max_size;
		} else {
			new_size = (unsigned int)pad_size;
			if (new_size <= block_min_size){
				new_size = block_min_size;
			} else {
				new_size = ((new_size + (base_size / 2)) / base_size) * base_size;	// 四捨五入して単位の倍数にする
			}
		}
		//printf("target num = %d, size = %d, max = %d, min = %d\n", target, new_size, max_size, min_size);
		// 目標に近づける
		while (max_size - min_size > base_size){
			new_num = 0;
			for (i = 0; i < file_num; i++){
				if (file_size[i] > 0)
					new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
			}
			//printf(" new_num = %d, new_size = %d\n", new_num, new_size);

			if (new_num > target){	// 指定したブロック数を越えたら、ブロック・サイズを大きくする
				upper_num = new_num;
				min_size = new_size;	// 越えたサイズを最小値にする
				new_size = (min_size + max_size) / 2;	// 最小値と最大値の中間
				if (base_size == 4){	// 切り上げで単位の倍数にする
					new_size = (new_size + 3) & 0xFFFFFFFC;
				} else {
					new_size = ((new_size + base_size - 1) / base_size) * base_size;
				}
				if (new_size >= max_size)
					new_size = max_size - base_size;
			} else {	// ブロック・サイズを小さくする
				lower_num = new_num;
				max_size = new_size;	// 越えなかったサイズを最大値にする
				new_size = (min_size + max_size) / 2;	// 最小値と最大値の中間
				if (base_size == 4){	// 切り捨てで単位の倍数にする
					new_size &= 0xFFFFFFFC;
				} else {
					new_size -= new_size % base_size;
				}
				if (new_size <= min_size)
					new_size = min_size + base_size;
			}
			//printf(" lower = %d, max = %d, upper = %d, min = %d\n", lower_num, max_size, upper_num, min_size);
		}
		// ブロック数が近い方にする
		if ((target - lower_num) < (upper_num - target)){
			block_size = max_size;
			source_num = lower_num;
		} else {
			block_size = min_size;
			source_num = upper_num;
		}
	}

	// とりあえず最適値に設定しておく
	block_size_best = block_size;
	source_num_best = source_num;
	pad_size = ((__int64)block_size * (__int64)source_num) - total_file_size;
	pad_size_best = pad_size;
/*	sbc_rate = ((10000 * source_num) + (block_size / 2)) / block_size;
	i = (int)((1000 * total_file_size) / (total_file_size + pad_size));
	printf("start: size= %d, count= %d, sbc= %d.%02d%%, pad= %I64d, rate= %d.%d%%\n",
		block_size, source_num, sbc_rate /100, sbc_rate %100, pad_size, i /10, i %10);
*/
	if (pad_size > 0){	// 近辺で効率が良くなる所を探す
		if (alloc_method == 0){	// ブロック数が指定された
			// 指定された個数から -12.5% ~ +6.25% の範囲を調べる（1600 個指定なら 1400 ~ 1700）
			//printf("Search better count from %d to %d\n", target - target / 8, target + target / 16);
			// ブロック・サイズを大きくしてみる
			loop_count = 0;
			new_size = block_size + base_size;
			while ((loop_count < MAX_ADJUST * 2) && (new_size <= block_max_size)){
				loop_count++;
				new_num = 0;
				for (i = 0; i < file_num; i++){
					if (file_size[i] > 0)	// ブロック数を計算する
						new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
				}
				if (new_num < target - (target / 8))
					break;	// 目標から離れすぎるとだめ -12%
				pad_size = ((__int64)new_size * (__int64)new_num) - total_file_size;
				if (pad_size < pad_size_best){
					block_size_best = new_size;
					source_num_best = new_num;
					pad_size_best = pad_size;
					if (pad_size == 0)
						break;
				}
				//i = (int)((1000 * total_file_size) / (total_file_size + pad_size));
				//printf("+ : size = %d, count = %d, pad = %I64d, rate = %d.%d%%\n",
				//	new_size, new_num, pad_size, i / 10, i % 10);
				new_size += base_size;
			}
			//printf("+ : %d trials\n", loop_count);
			// ブロック・サイズを小さくしてみる
			loop_count = 0;
			new_size = block_size - base_size;
			while ((loop_count < MAX_ADJUST) && (new_size >= block_min_size)){
				loop_count++;
				new_num = 0;
				for (i = 0; i < file_num; i++){
					if (file_size[i] > 0)	// ブロック数を計算する
						new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
				}
				if (new_num > target + (target / 16))
					break;	// 目標から離れすぎるとだめ +6%
				pad_size = ((__int64)new_size * (__int64)new_num) - total_file_size;
				if (pad_size < pad_size_best){
					block_size_best = new_size;
					source_num_best = new_num;
					pad_size_best = pad_size;
					if (pad_size == 0)
						break;
				}
				//i = (int)((1000 * total_file_size) / (total_file_size + pad_size));
				//printf("- : size = %d, count = %d, pad = %I64d, rate = %d.%d%%\n",
				//	new_size, new_num, pad_size, i / 10, i % 10);
				new_size -= base_size;
			}
			//printf("- : %d trials\n", loop_count);

		} else if (alloc_method < 0){	// 割合(ブロック・サイズ * ? = ブロック数)が指定された
			// 指定された割合から -0.09% ~ +0.09% の範囲を調べる（1% 指定なら 0.91% ~ 1.09%）
			//printf("Search better rate from %d.%02d%% to %d.%02d%%\n", (target-9)/100, (target-9)%100, (target+9)/100, (target+9)%100);
			// ブロック・サイズを大きくしてみる
			loop_count = 0;
			new_size = block_size + base_size;
			while ((loop_count < MAX_ADJUST * 2) && (new_size <= block_max_size)){
				loop_count++;
				new_num = 0;
				for (i = 0; i < file_num; i++){
					if (file_size[i] > 0)	// ブロック数を計算する
						new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
				}
				sbc_rate = ((10000 * new_num) + (new_size / 2)) / new_size;	// 四捨五入する
				if (sbc_rate < target - 9)
					break;	// 目標から離れすぎるとだめ -0.09%
				pad_size = ((__int64)new_size * (__int64)new_num) - total_file_size;
				if (pad_size < pad_size_best){
					block_size_best = new_size;
					source_num_best = new_num;
					pad_size_best = pad_size;
					if (pad_size == 0)
						break;
				}
				//i = (int)((1000 * total_file_size) / (total_file_size + pad_size));
				//printf("+ : size= %d, count= %d, sbc= %d.%02d%%, pad= %I64d, rate= %d.%d%%\n",
				//	new_size, source_num, sbc_rate /100, sbc_rate %100, pad_size, i /10, i %10);
				new_size += base_size;
			}
			//printf("+ : %d trials\n", loop_count);
			// ブロック・サイズを小さくしてみる
			loop_count = 0;
			new_size = block_size - base_size;
			while ((loop_count < MAX_ADJUST) && (new_size >= block_min_size)){
				loop_count++;
				new_num = 0;
				for (i = 0; i < file_num; i++){
					if (file_size[i] > 0)	// ブロック数を計算する
						new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
				}
				if ((limit_num > 0) && (new_num > limit_num + (limit_num / 16)))
					break;	// 制限ブロック数を越えるとだめ +6%
				sbc_rate = ((10000 * new_num) + (new_size / 2)) / new_size;	// 四捨五入する
				if (sbc_rate > target + 9)
					break;	// 目標から離れすぎるとだめ +0.09%
				pad_size = ((__int64)new_size * (__int64)new_num) - total_file_size;
				if (pad_size < pad_size_best){
					block_size_best = new_size;
					source_num_best = new_num;
					pad_size_best = pad_size;
					if (pad_size == 0)
						break;
				}
				//i = (int)((1000 * total_file_size) / (total_file_size + pad_size));
				//printf("- : size= %d, count= %d, sbc= %d.%02d%%, pad= %I64d, rate= %d.%d%%\n",
				//	new_size, new_num, sbc_rate /100, sbc_rate %100, pad_size, i /10, i %10);
				new_size -= base_size;
			}
			//printf("- : %d trials\n", loop_count);

		} else if (adjust_size > 0){	// ブロック・サイズと変更単位の両方が指定された時だけ、自動調整する
			// 指定されたサイズから -25% ~ +50% の範囲を調べる（1024 KB 指定なら 768KB ~ 1536KB）
			//printf("Search better size from %d to %d\n", target_size - target_size / 4, target_size + target_size / 2);
			// ブロック・サイズを大きくしてみる
			loop_count = 0;
			new_size = block_size + base_size;
			while ((loop_count < MAX_ADJUST * 2) && (new_size <= block_max_size)){
				loop_count++;
				if (new_size > target_size + (target_size / 2))
					break;	// 目標から離れすぎるとだめ +50%
				new_num = 0;
				for (i = 0; i < file_num; i++){
					if (file_size[i] > 0)	// ブロック数を計算する
						new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
				}
				pad_size = ((__int64)new_size * (__int64)new_num) - total_file_size;
				if (pad_size < pad_size_best){
					block_size_best = new_size;
					source_num_best = new_num;
					pad_size_best = pad_size;
					if (pad_size == 0)
						break;
				}
				//i = (int)((1000 * total_file_size) / (total_file_size + pad_size));
				//printf("+ : size = %d, count = %d, pad = %I64d, rate = %d.%d%%\n",
				//	new_size, new_num, pad_size, i / 10, i % 10);
				new_size += base_size;
			}
			//printf("+ : %d trials\n", loop_count);
			// ブロック・サイズを小さくしてみる
			loop_count = 0;
			new_size = block_size - base_size;
			while ((loop_count < MAX_ADJUST) && (new_size >= block_min_size)){
				loop_count++;
				if (new_size < target_size - (target_size / 4))
					break;	// 目標から離れすぎるとだめ -25%
				new_num = 0;
				for (i = 0; i < file_num; i++){
					if (file_size[i] > 0)	// ブロック数を計算する
						new_num += (int)((file_size[i] + new_size - 1) / (__int64)new_size);
				}
				if ((limit_num > 0) && (new_num > limit_num + (limit_num / 16)))
					break;	// 制限ブロック数を越えるとだめ +6%
				pad_size = ((__int64)new_size * (__int64)new_num) - total_file_size;
				if (pad_size < pad_size_best){
					block_size_best = new_size;
					source_num_best = new_num;
					pad_size_best = pad_size;
					if (pad_size == 0)
						break;
				}
				//i = (int)((1000 * total_file_size) / (total_file_size + pad_size));
				//printf("- : size = %d, count = %d, pad = %I64d, rate = %d.%d%%\n",
				//	new_size, new_num, pad_size, i / 10, i % 10);
				new_size -= base_size;
			}
			//printf("- : %d trials\n", loop_count);
		}
	}

	// 最適値を使う
/*	sbc_rate = ((10000 * source_num_best) + (block_size_best / 2)) / block_size_best;
	i = (int)((1000 * total_file_size) / (total_file_size + pad_size_best));
	printf("best : size= %d, count= %d, sbc= %d.%02d%%, pad= %I64d, rate= %d.%d%%\n\n",
		block_size_best, source_num_best, sbc_rate /100, sbc_rate %100, pad_size_best, i /10, i %10);
*/
	block_size = block_size_best;
	source_num = source_num_best;
	if (recovery_limit < 0){	// ソース・ファイルの最大スライス数を計算する
		recovery_limit = -1;	// 初期化する
		for (i = 0; i < file_num; i++){
			if (file_size[i] > 0){	// スライス数を計算する
				upper_num = (int)((file_size[i] + block_size - 1) / (__int64)block_size);
				if (upper_num + recovery_limit > 0)
					recovery_limit = -upper_num;
			}
		}
	}

	free(file_size);
	return 0;
}

// 復元したいファイル数から、必要なパリティ・ブロック数を計算する
static int calc_required_parity(int possible_count)
{
	wchar_t file_path[MAX_LEN];
	int list_off = 0, i, block_need, extra_rate, id = 0, block_count = 0;
	int *file_block;
	__int64 file_size;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

	if (possible_count <= 0)
		return 0;
	extra_rate = possible_count % 100;
	possible_count /= 100;
	//printf("possible_count = %d, extra_rate = %d\n", possible_count, extra_rate);
	if (possible_count >= entity_num)
		return source_num;

	// 各ファイルのソース・ブロック数を取得する
	file_block = (int *)malloc(sizeof(int) * file_num);
	if (file_block == NULL){
		printf("malloc, %zd\n", sizeof(int) * file_num);
		return -1;
	}
	wcscpy(file_path, base_dir);
	for (i = 0; i < file_num; i++){
		wcscpy(file_path + base_len, list_buf + list_off);
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			printf_cp("GetFileAttributesEx, %s\n", list_buf + list_off);
			free(file_block);
			return -1;
		}
		if (AttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){	// フォルダなら
			file_block[i] = 0;
		} else {
			file_size = ((__int64)AttrData.nFileSizeHigh << 32) | (unsigned __int64)AttrData.nFileSizeLow;
			file_block[i] = (int)((file_size + block_size) / block_size);
			if (block_count < file_block[i]){
				block_count = file_block[i];
				id = i;
			}
		}
		//printf("%d: %d blocks\n", i, file_block[i]);
		while (list_buf[list_off] != 0)
			list_off++;
		list_off++;
	}
	//printf("max block = %d, file = %d\n", block_count, id);
	block_need = block_count;

	if (possible_count < 1){
		block_need = (block_need * extra_rate + 99) / 100;
	} else {
		while (possible_count > 1){
			// ブロック数が最大だったファイルを取り除く
			file_block[id] = 0;
			block_count = 0;

			// その次に大きなファイルを探す
			for (i = 0; i < file_num; i++){
				if (block_count < file_block[i]){
					block_count = file_block[i];
					id = i;
				}
			}
			if (block_count == 0)
				break;
			block_need += block_count;
			//printf("max block = %d, file = %d, sum = %d\n", block_count, id, block_need);
			possible_count--;
		}
		block_need = (block_need * (100 + extra_rate) + 99) / 100;
		if (block_need > source_num)
			block_need = source_num;
	}

	free(file_block);
	return block_need;
}

wmain(int argc, wchar_t *argv[])
{
	wchar_t uni_buf[MAX_LEN], *tmp_p;
	int i, j, k;
	unsigned int switch_set = 0;
/*
in= switch_set & 0x00000001
f = switch_set & 0x00000002
fu= switch_set & 0x00000004
up= switch_set & 0x00000008
h = switch_set & 0x00000010
w = switch_set & 0x00000020
lp= switch_set & 0x00000700
rd= switch_set & 0x00030000
ri= switch_set & 0x00040000
*/
	printf("Parchive 2.0 client version " FILE_VERSION " by Yutaka Sawada\n\n");
	if (argc < 3){
		printf("Self-Test: ");
		i = par2_checksum(uni_buf);
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
	base_dir[0] = 0;
	ini_path[0] = 0;
	uni_buf[0] = 0;
	file_num = 0;
	entity_num = 0;
	recovery_num = 0;
	source_num = 0;
	parity_num = 0;
	recovery_limit = 0;
	first_num = 0;
	switch_v = 0;
	switch_b = 0;
	block_size = 0;
	split_size = 0;
	total_file_size = 0;
	list_buf = NULL;
	recv_buf = NULL;
	recv2_buf = NULL;
	list2_buf = NULL;
	cp_output = GetConsoleOutputCP();
	check_cpu();	// CPU を検査する

	// コマンド
	switch (argv[1][0]){
	case 'c':	// create
	case 't':	// trial
	case 'v':	// verify
	case 'r':	// repair
	case 'l':	// list
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
			// 作成時のオプション
			if (wcscmp(tmp_p, L"ri") == 0){
				switch_set |= 0x40000;
			} else if (wcscmp(tmp_p, L"in") == 0){
				switch_set |= 0x01;
			} else if (wcscmp(tmp_p, L"up") == 0){
				switch_set |= 0x08;
			// 検査時のオプション
			} else if (wcscmp(tmp_p, L"h") == 0){
				switch_set |= 0x10;
			} else if (wcscmp(tmp_p, L"p") == 0){
				if ((argv[1][0] == 'v') || (argv[1][0] == 'r'))
					switch_b |= 0x10;
			} else if (wcscmp(tmp_p, L"w") == 0){
				switch_set |= 0x20;
			// 修復時のオプション
			} else if (wcscmp(tmp_p, L"b") == 0){
				if (argv[1][0] == 'r')
					switch_b |= 1;
			} else if (wcscmp(tmp_p, L"br") == 0){
				if (argv[1][0] == 'r')
					switch_b |= 2;
			} else if (wcscmp(tmp_p, L"bi") == 0){
				if (argv[1][0] == 'r')
					switch_b |= 4;
			// 共通のオプション
			} else if (wcscmp(tmp_p, L"f") == 0){
				switch_set |= 0x02;
			} else if (wcscmp(tmp_p, L"fu") == 0){
				switch_set |= 0x06;
			} else if (wcscmp(tmp_p, L"fo") == 0){
				switch_v |= 8;
			} else if (wcscmp(tmp_p, L"uo") == 0){
				cp_output = CP_UTF8;

			// 作成時のオプション (数値)
			} else if (wcsncmp(tmp_p, L"ss", 2) == 0){
				if ((argv[1][0] == 'c') || (argv[1][0] == 't')){
					block_size = 0;
					j = 2;
					while ((j < 2 + 10) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
						block_size = (block_size * 10) + (tmp_p[j] - '0');
						j++;
					}
					// この時点ではまだ単位がわからないので近似しない
					if (block_size > MAX_BLOCK_SIZE)
						block_size = MAX_BLOCK_SIZE;
					switch_b &= 0x003FFFFF;	// 割合の指定を取り消す
				}
			} else if (wcsncmp(tmp_p, L"sn", 2) == 0){
				source_num = 0;
				j = 2;
				while ((j < 2 + 6) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					source_num = (source_num * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (source_num > MAX_SOURCE_NUM)
					source_num = MAX_SOURCE_NUM;
			} else if (wcsncmp(tmp_p, L"sr", 2) == 0){
				if ((argv[1][0] == 'c') || (argv[1][0] == 't')){
					k = 0;
					j = 2;
					while ((j < 2 + 5) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
						k = (k * 10) + (tmp_p[j] - '0');
						j++;
					}
					if (k > 1023)
						k = 1023;
					switch_b = (switch_b & 0x003FFFFF) | (k << 22);
					block_size = 0;	// ブロック・サイズの指定を取り消す
				}
			} else if (wcsncmp(tmp_p, L"sm", 2) == 0){
				if ((argv[1][0] == 'c') || (argv[1][0] == 't')){
					k = 0;
					j = 2;
					while ((j < 2 + 8) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
						k = (k * 10) + (tmp_p[j] - '0');
						j++;
					}
					if (k > 4194300)
						k = 4194300;
					k &= 0x003FFFFC;	// 4の倍数にする
					switch_b = (switch_b & 0xFFC00000) | k;
				}
			} else if (wcsncmp(tmp_p, L"lp", 2) == 0){
				j = 4;	// lp は lp4 と同じにする
				if ((tmp_p[2] >= '0') && (tmp_p[2] <= '7'))
					j = tmp_p[2] - '0';
				if ((switch_set & 0x0700) == 0)
					switch_set |= (j << 8);
			} else if (wcsncmp(tmp_p, L"rn", 2) == 0){
				parity_num = 0;
				j = 2;
				while ((j < 2 + 6) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					parity_num = (parity_num * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (parity_num > MAX_PARITY_NUM)
					parity_num = MAX_PARITY_NUM;
			} else if (wcsncmp(tmp_p, L"rr", 2) == 0){
				parity_num = 0;
				j = 2;
				while ((j < 2 + 5) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					parity_num = (parity_num * 10) + (tmp_p[j] - '0');
					j++;
				}
				parity_num *= 100;
				if ((tmp_p[j] == '.') || (tmp_p[j] == ',')){	// 小数点があるなら、小数点以下第二位まで読み取る
					j++;
					if ((tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
						parity_num += (tmp_p[j] - '0') * 10;
						j++;
						if ((tmp_p[j] >= '0') && (tmp_p[j] <= '9'))
							parity_num += tmp_p[j] - '0';
					}
				}
				if (parity_num > 100000)
					parity_num = 100000;
				parity_num *= -1;
			} else if (wcsncmp(tmp_p, L"rp", 2) == 0){
				parity_num = 0;
				j = 2;
				while ((j < 2 + 4) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					parity_num = (parity_num * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (parity_num > 999)
					parity_num = 999;
				parity_num *= 100;
				if ((tmp_p[j] == '.') || (tmp_p[j] == ',')){	// 小数点があるなら、小数点以下第二位まで読み取る
					j++;
					if ((tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
						parity_num += (tmp_p[j] - '0') * 10;
						j++;
						if ((tmp_p[j] >= '0') && (tmp_p[j] <= '9'))
							parity_num += tmp_p[j] - '0';
					}
				}
				parity_num = parity_num * -1 - 200000;
			} else if (wcsncmp(tmp_p, L"rd", 2) == 0){
				j = 0;	// rd は rd0 と同じにする
				if ((tmp_p[2] >= '0') && (tmp_p[2] <= '3'))	// 0～3 の範囲
					j = tmp_p[2] - '0';
				if ((switch_set & 0x30000) == 0)
					switch_set |= (j << 16);
			} else if (wcsncmp(tmp_p, L"rf", 2) == 0){
				recovery_num = 0;
				j = 2;
				while ((j < 2 + 6) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					recovery_num = (recovery_num * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (recovery_num > MAX_PARITY_NUM)
					recovery_num = MAX_PARITY_NUM;
			} else if (wcsncmp(tmp_p, L"rs", 2) == 0){
				first_num = 0;
				j = 2;
				while ((j < 2 + 6) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					first_num = (first_num * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (first_num > MAX_PARITY_NUM - 1)
					first_num = MAX_PARITY_NUM - 1;
			} else if (wcsncmp(tmp_p, L"lr", 2) == 0){
				recovery_limit = 0;
				j = 2;
				while ((j < 2 + 6) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					recovery_limit = (recovery_limit * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (recovery_limit <= 0)	// -lr0 ならソース・ファイルの最大ブロック数と同じにする
					recovery_limit = -1;
				if (recovery_limit > MAX_PARITY_NUM)
					recovery_limit = MAX_PARITY_NUM;
			} else if (wcsncmp(tmp_p, L"ls", 2) == 0){
				__int64 num8 = 0;
				j = 2;
				while ((j < 2 + 10) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					num8 = (num8 * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (num8 >= 0xFFFFFFFC){
					split_size = 0xFFFFFFFC;	// 分割サイズは 4GBまで
				} else {
					split_size = (unsigned int)num8;
				}
			// 検査時のオプション (数値)
			} else if (wcsncmp(tmp_p, L"vl", 2) == 0){
				j = 0;	// vl は vl0 と同じにする
				if ((tmp_p[2] >= '0') && (tmp_p[2] <= '7'))	// 0～7 の範囲
					j = tmp_p[2] - '0';
				if ((switch_v & 7) == 0)
					switch_v |= j;
			// 共通のオプション (数値)
			} else if (wcsncmp(tmp_p, L"lc", 2) == 0){
				k = 0;
				j = 2;
				while ((j < 2 + 5) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					k = (k * 10) + (tmp_p[j] - '0');
					j++;
				}
				if (k & 32){	// GPU を使う
					OpenCL_method = 1;	// Faster GPU
				} else if (k & 64){
					OpenCL_method = -1;	// Slower GPU
				}
				if (k & 16)	// SSSE3 を使わない
					cpu_flag &= 0xFFFFFFFE;
				if (k & 128)	// CLMUL を使わない、SSSE3 の古いエンコーダーを使う
					cpu_flag = (cpu_flag & 0xFFFFFFF7) | 0x100;
				if (k & 256)	// JIT(SSE2) を使わない
					cpu_flag &= 0xFFFFFF7F;
				if (k & 512)	// AVX2 を使わない
					cpu_flag &= 0xFFFFFFEF;
				if (k & 15){	// 使用するコア数を変更する
					k &= 15;	// 1～15 の範囲
					// printf("\n lc# = %d , logical = %d, physical = %d \n", k, cpu_num >> 24, (cpu_num & 0x00FF0000) >> 16);
					if (k == 12){	// 物理コア数の 1/4 にする
						k = ((cpu_num & 0x00FF0000) >> 16) / 4;
					} else if (k == 13){	// 物理コア数の半分にする
						k = ((cpu_num & 0x00FF0000) >> 16) / 2;
					} else if (k == 14){	// 物理コア数の 3/4 にする
						k = (((cpu_num & 0x00FF0000) >> 16) * 3) / 4;
					} else if (k == 15){	// 物理コア数にする
						k = (cpu_num & 0x00FF0000) >> 16;
						if (k >= 6)
							k--;	// 物理コア数が 6以上なら、1個減らす
					} else if (k > (cpu_num >> 24)){
						k = cpu_num >> 24;	// 論理コア数を超えないようにする
					}
					if (k > MAX_CPU){
						k = MAX_CPU;
					} else if (k < 1){
						k = 1;
					}
					cpu_num = (cpu_num & 0xFFFF0000) | k;	// 指定されたコア数を下位に配置する
				}
			} else if (wcsncmp(tmp_p, L"m", 1) == 0){
				memory_use = 0;
				j = 1;	// メモリー使用量だけでなく、モード切替用としても使う、２桁まで
				while ((j < 1 + 2) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					memory_use = (memory_use * 10) + (tmp_p[j] - '0');
					j++;
				}
			} else if (wcsncmp(tmp_p, L"vs", 2) == 0){
				recent_data = 0;
				j = 2;
				while ((j < 2 + 2) && (tmp_p[j] >= '0') && (tmp_p[j] <= '9')){
					recent_data = (recent_data * 10) + (tmp_p[j] - '0');
					j++;
				}
				if ((recent_data == 8) || (recent_data > 15))
					recent_data = 0;

			// 作成時のオプション (文字列)
			} else if (wcsncmp(tmp_p, L"c", 1) == 0){
				tmp_p++;
				if (wcslen(tmp_p) >= COMMENT_LEN){
					printf("comment is too long\n");
					return 1;
				}
				wcscpy(uni_buf, tmp_p);
			} else if ((wcsncmp(tmp_p, L"fa", 2) == 0) || (wcsncmp(tmp_p, L"fe", 2) == 0)){
				tmp_p += 2;
				if ((argv[1][0] == 'c') || (argv[1][0] == 't')){
					j = (int)wcslen(tmp_p);
					if ((j == 0) || (list2_len + j + 1 > ALLOC_LEN - MAX_LEN))
						continue;
					if (list2_buf == NULL){
						list2_max = 0;	// allow list の項目数
						list2_len = 0;
						list2_buf = (wchar_t *)malloc(ALLOC_LEN * 2);
					}
					if (list2_buf != NULL){
						if (tmp_p[-1] == 'a'){
							list2_max++;
							list2_buf[list2_len++] = '+';
						} else {
							list2_buf[list2_len++] = '-';
						}
						j = copy_wild(list2_buf + list2_len, tmp_p);
						list2_len += j;
					}
				}
			// 共通のオプション (文字列)
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

		} else {	// オプションでなければループから抜ける
			break;
		}
	}
	if (i < argc){
		tmp_p = argv[i];
		i++;
		// オプション以外ならリカバリ・ファイル
		j = copy_path_prefix(recovery_file, MAX_LEN - 20, tmp_p, NULL);	// 最大で「.vol32768+32768.par2」が追加される
		if (j == 0){
			printf("PAR filename is invalid\n");
			return 1;
		}
		if ((argv[1][0] == 'c') || (argv[1][0] == 't')){	// 作成なら
			// 指定されたファイル名が適切か調べる
			if (sanitize_filename(offset_file_name(recovery_file), NULL, 0) != 0){
				printf("PAR filename is invalid\n");
				return 1;
			}
			// 拡張子が「.par2」以外なら標準の拡張子を追加する
			j = (int)wcslen(recovery_file);	// 浄化でファイル名が短縮されるかもしれない
			if (_wcsicmp(recovery_file + (j - 5), L".par2"))
				wcscpy(recovery_file + j, L".par2");
		} else {	// 検査や修復なら
			j = GetFileAttributes(recovery_file);
			if ((j == INVALID_FILE_ATTRIBUTES) || (j & FILE_ATTRIBUTE_DIRECTORY)){
				wchar_t search_path[MAX_LEN], file_ext[EXT_LEN];
				int name_len, dir_len;
				HANDLE hFind;
				WIN32_FIND_DATA FindData;
				// リカバリ・ファイルの名前の基
				get_base_filename(recovery_file, search_path, file_ext);
				if (file_ext[0] == 0)
					wcscpy(file_ext, L".par2");	// 拡張子が省略されてるなら標準の拡張子を追加してみる
				// 先にボリューム番号を含まないファイル名を試す
				name_len = (int)wcslen(search_path);
				dir_len = (int)(offset_file_name(recovery_file) - recovery_file);
				wcscpy(search_path + name_len, file_ext);
				hFind = FindFirstFile(search_path, &FindData);
				if (hFind != INVALID_HANDLE_VALUE){
					file_ext[0] = 0;
					FindClose(hFind);
				}
				if (file_ext[0] != 0){	// ボリューム番号を含むファイル名を試す
					wcscpy(search_path + name_len, L".vol*");
					wcscat(search_path, file_ext);	// 「～.par2」を「～.vol*.par2」にする
					hFind = FindFirstFile(search_path, &FindData);
					if (hFind != INVALID_HANDLE_VALUE){
						do {
							name_len = (int)wcslen(FindData.cFileName);
							if (dir_len + name_len < MAX_LEN){	// ファイル名が長すぎない
								file_ext[0] = 0;
								break;	// 見つけたファイル名で問題なし
							}
						} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
						FindClose(hFind);
					}
				}
				if (file_ext[0] != 0){	// その他のファイル名を試す
					wcscpy(search_path + name_len, L"*");
					wcscat(search_path, file_ext);	// 「～.par2」を「～*.par2」にする
					hFind = FindFirstFile(search_path, &FindData);
					if (hFind != INVALID_HANDLE_VALUE){
						file_ext[0] = '.';	// 拡張子を元に戻しておく
						do {
							//printf("file name = %S\n", FindData.cFileName);
							name_len = (int)wcslen(FindData.cFileName);
							if (dir_len + name_len < MAX_LEN){	// ファイル名が長すぎない
								file_ext[0] = 0;
								break;	// 見つけたファイル名で問題なし
							}
						} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
						FindClose(hFind);
					}
				}
				if (file_ext[0] != 0){
					printf("valid file is not found\n");
					return 1;
				}
				wcscpy(recovery_file + dir_len, FindData.cFileName);
			}
		}
	}

/*
// デバッグ用の比較だけなら
if (list2_buf){
	list2_buf[list2_len] = 0;
	wcscpy(base_dir, list2_buf + 1);
} else {
	base_dir[0] = 0;
}
i = PathMatchWild(uni_buf, base_dir);
printf("text = \"%S\"\n", uni_buf);
printf("wild = \"%S\"\n", base_dir);
printf("result = %d\n", i);
return 0;
*/

	if (recovery_file[0] == 0){	// リカバリ・ファイルが指定されてないなら
		printf("PAR file is not specified\n");
		return 1;
	}

	// input file の位置が指定されて無くて、
	// 最初のソース・ファイルが絶対パスで指定されてるなら、それを使う
	if ((argv[1][0] == 'c') || (argv[1][0] == 't')){
		if ((base_dir[0] == 0) && ((switch_set & 0x02) == 0) && (i < argc)){
			tmp_p = argv[i];
			if (is_full_path(tmp_p) != 0){	// 絶対パスなら
				wchar_t file_path[MAX_LEN];
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
	base_len = (int)wcslen(base_dir);

	// 検査結果ファイルの位置が指定されてないなら
	if (((recent_data != 0) || ((switch_set & 0x20) != 0)) && (ini_path[0] == 0)){
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

	// 環境の表示
	print_environment();

	switch (argv[1][0]){
	case 'c':
	case 't':
		// リカバリ・ファイル作成ならソース・ファイルのリストがいる
		if (i >= argc){	// もうファイル指定が無いなら
			wchar_t search_path[MAX_LEN];
			int dir_len;
			// リカバリ・ファイルの拡張子を取り除いたものがソース・ファイルと見なす
			wcscpy(search_path, recovery_file);
			tmp_p = offset_file_name(search_path);
			tmp_p = wcsrchr(tmp_p, '.');
			if (tmp_p != NULL)
				*tmp_p = 0;	// 拡張子を取り除く
			if (_wcsnicmp(base_dir, search_path, base_len) != 0){	// 基準ディレクトリ外のファイルは拒否する
				printf_cp("out of base-directory, %s\n", search_path);
				return 1;
			}
			// そのファイルが存在するか確かめる
			dir_len = (int)wcslen(search_path) - 2;	// ファイル名末尾の「\」を無視して、ディレクトリ部分の長さを求める
			while (search_path[dir_len] != '\\')
				dir_len--;
			dir_len++;
			if (search_files(search_path, dir_len, FILE_ATTRIBUTE_DIRECTORY, 0)){	// ファイルだけを探す
				free(list_buf);
				return 1;
			}
			if (file_num != 1){	// ファイルが見つかったか確かめる
				free(list_buf);
				printf_cp("input file is not found, %s\n", search_path + base_len);
				return 1;
			}
		} else if (switch_set & 0x06){	// ファイル・リストの読み込み
			j = (switch_set & 0x04) ? CP_UTF8 : CP_OEMCP;
			if (read_list(argv[i], j)){
				free(list_buf);
				return 1;
			}
		} else {	// 入力ファイルの指定
			wchar_t search_path[MAX_LEN];
			int dir_len;
			unsigned int filter;
			// 隠しファイルやフォルダーを無視するかどうか
			filter = get_show_hidden();
			if (switch_v & 8)
				filter |= FILE_ATTRIBUTE_DIRECTORY;

/*
			if (list2_buf){
				// デバッグ用のリスト表示
				printf("filtering list :\n", list2_buf);
				j = 0;
				while (j < list2_len){
					printf(" \"%S\"\n", list2_buf + j);
					j += wcslen(list2_buf + j) + 1;
				}
				printf("length : %d, allow number = %d\n\n", list2_len, list2_max);
			}
*/

			for (; i < argc; i++){
				// ファイルが基準ディレクトリ以下に存在することを確認する
				tmp_p = argv[i];
				j = copy_path_prefix(search_path, MAX_LEN - ADD_LEN - 2, tmp_p, base_dir);	// 絶対パスにしてから比較する
				if (j == 0){
					free(list_buf);
					printf_cp("filename is invalid, %s\n", tmp_p);
					return 1;
				}
				if ((j <= base_len) || (_wcsnicmp(base_dir, search_path, base_len) != 0)){	// 基準ディレクトリ外なら
					free(list_buf);
					printf_cp("out of base-directory, %s\n", tmp_p);
					return 1;
				}
				//printf("%d = \"%S\"\n", i, argv[i]);
				//printf_cp("search = \"%s\"\n", search_path);
				// 「*」や「?」で検索しない場合、ファイルが見つからなければエラーにする
				j = -1;
				if (wcspbrk(search_path + base_len, L"*?") == NULL)
					j = file_num;
				// ファイルを検索する
				dir_len = (int)wcslen(search_path) - 2;	// ファイル名末尾の「\」を無視して、ディレクトリ部分の長さを求める
				while (search_path[dir_len] != '\\')
					dir_len--;
				dir_len++;
				if (search_files(search_path, dir_len, filter, j)){
					free(list_buf);
					return 1;
				}
				// ファイルが見つかったか確かめる (すでに登録済みならいい)
				if ((j != -1) && (j == file_num) &&
						(search_file_path(list_buf, list_len, search_path + base_len) == 0)){
					free(list_buf);
					printf_cp("input file is not found, %s\n", search_path + base_len);
					return 1;
				}
			}
		}
		if (list2_buf){	// 除外ファイル・リストを消去する
			free(list2_buf);
			list2_buf = NULL;
		}
/*{
FILE *fp;
fp = fopen("list_buf.txt", "wb");
fwrite(list_buf, 2, list_len, fp);
fclose(fp);
}*/
		if (file_num < 1){
			free(list_buf);
			printf("input file is not found\n");
			return 1;
		}
		if (file_num > MAX_SOURCE_NUM){
			free(list_buf);
			printf("too many input files %d\n", file_num);
			return 1;
		}
		if (parity_num == 0)	// リカバリ・ファイルを作らない場合は、必ずインデックス・ファイルを作る
			switch_set &= ~0x01;
		if (check_recovery_match(switch_set & 0x01)){
			free(list_buf);
			return 1;
		}

		if (total_file_size == 0){	// ファイル・サイズが全て 0 だとブロックも無いはず
			block_size = 0;
			source_num = 0;
			parity_num = 0;
		} else {	// ブロック数を計算する
			// 最適なブロック・サイズを調べる
			i = 0;
			if (block_size != 0){	// ブロック・サイズを指定
				i = block_size;
			} else {	// ブロック・サイズとブロック数の割合を指定
				i = ((unsigned int)switch_b >> 22) * -10;	// 割合を 0.01% 刻みにする
			}
			j = 0;	// 標準ではソース・ブロック数を制限しない
			if (source_num != 0){	// ブロック数を指定
				j = source_num;
			} else if (i == 0){	// 全てを指定してなければ
				i = -100;	// 割合で 1% にする
				j = 3000;	// 3000ブロックまでにする
			}
			if (check_block_size(switch_b & 0x003FFFFC, i, j)){
				free(list_buf);
				return 1;
			}

			if (parity_num <= -200000){	// 復元できるファイル数でパリティ・ブロック数を決める
				j = calc_required_parity(parity_num * -1 - 200000);
				if (j < 0){
					free(list_buf);
					return 1;
				}
				parity_num = j;
			} else if (parity_num < 0){	// 冗長性(%)でパリティ・ブロック数を決める
				parity_num = -parity_num;	// % の 100倍になってることに注意
				i = (int)(((unsigned int)parity_num * (unsigned int)source_num) / 10000);
				j = (i * 10000) / source_num;
				//printf("num0 = %d, rate = %d, target = %d \n", i, j, parity_num);
				if (j != parity_num){
					j = ((i + 1) * 10000) / source_num;
					//printf("num1 = %d, rate = %d, target = %d \n", i + 1, j, parity_num);
					if (j == parity_num)
						i++;	// 1個増やして冗長性を一致させる
				}
				if (i > MAX_PARITY_NUM)
					i = MAX_PARITY_NUM;
				if (i == 0)
					i = 1;
				parity_num = i;
			}
		}
		// 分割サイズはブロック・サイズの倍数にする
		if (split_size >= 4){
			if (split_size <= block_size){
				split_size = block_size;
			} else {
				split_size -= split_size % block_size;
			}
		}
		// リカバリ・ファイルの数とその最大ブロック数を計算する
		if (parity_num > 0){
			if (recovery_limit == 0){	// 制限値が未設定ならパリティ・ブロック数にする
				recovery_limit = parity_num;
			} else if (recovery_limit < 0){	// ソース・ファイルの最大ブロック数を制限値にする
				recovery_limit = -recovery_limit;
				if (split_size >= 4){	// ソース・ファイルを分割する場合はその分割サイズまで
					j = split_size / block_size;	// 分割ファイル内のブロック数
					if (recovery_limit > j)
						recovery_limit = j;
				}
			}
			// パリティ・ブロックをリカバリ・ファイルに分配する方法
			i = (switch_set & 0x30000) >> 16;
			if (i == 0){	// 同じ量ずつ割り振る
				if (recovery_num == 0){	// 未設定なら、ソース・ファイル数と冗長性から計算する
					recovery_num = entity_num * parity_num / source_num;
					if (recovery_num < 1){
						recovery_num = 1;
					} else if (recovery_num > 10){
						recovery_num = 10;	// QuickPar と同じく 10個までにする
					}
				}
				if (recovery_num > parity_num)
					recovery_num = parity_num;
				// 制限数から最低ファイル数を計算する
				j = (parity_num + recovery_limit - 1) / recovery_limit;
				if (recovery_num < j)
					recovery_num = j;
			} else if (i == 1){	// 倍々で異なる量にする
				if (recovery_num == 0){	// 未設定なら、ソース・ファイル数と冗長性から計算する
					recovery_num = entity_num * parity_num / source_num;
					if (recovery_num < 3){
						if (parity_num >= 7){
							recovery_num = 3;	// 大中小で 3個にする。
						} else if (parity_num >= 3){
							recovery_num = 2;	// 大小で 2個にする。
						} else {
							recovery_num = 1;
						}
					} else if (recovery_num > 10){
						recovery_num = 10;	// QuickPar と同じく 10個までにする
					}
				}
				// 基準値が1の場合のファイル数 = 最大ファイル数
				i = 1;	// recovery_num
				j = 1;	// total count
				k = 1;	// count in the file
				while (j < parity_num){
					k *= 2;
					if (k > recovery_limit){
						i += (parity_num - j + recovery_limit - 1) / recovery_limit;
						break;
					}
					j += k;
					i++;
				}
				//printf("recovery_num = %d, max = %d (%d blocks)\n", recovery_num, i, k);
				if (recovery_num > i)
					recovery_num = i;
				i = (1 << recovery_num) - 1;	// 分割数
				k = (parity_num + i - 1) / i;	// 倍率
				//printf("recovery_num = %d, split = %d, base = %d\n", recovery_num, i, k);
				if (recovery_limit < parity_num){
					if (recovery_limit * recovery_num < parity_num){	// ファイル数が少なすぎるなら増やす
						recovery_num = (parity_num + recovery_limit - 1) / recovery_limit;
						//printf("min = %d, * %d = %d\n", recovery_num, recovery_limit, recovery_limit * recovery_num);
					}
					i = parity_num;
					j = recovery_num;
					if (j > 16){	// 限界を超えてる分は省く
						i -= recovery_limit * (j - 16);
						j = 16;
					}
					k = (1 << j) - 1;	// 分割数
					k = (i + k - 1) / k;	// 倍率
					//printf("rest = %d blocks on %d files, base = %d, last = %d, %d, limit = %d\n", i, j, k, k << (j - 2), i - ((1 << (j - 1)) - 1) * k, recovery_limit);
					while ((j >= 2) && (k < recovery_limit) && (((k << (j - 2)) > recovery_limit) || (i - ((1 << (j - 1)) - 1) * k > recovery_limit))){
						i -= recovery_limit;
						j--;
						k = (1 << j) - 1;	// 分割数
						k = (i + k - 1) / k;	// 倍率
						//printf("rest = %d blocks on %d files, base = %d, last = %d, %d\n", i, j, k, k << (j - 2), i - ((1 << (j - 1)) - 1) * k);
					}
				}
				recovery_limit = (recovery_limit & 0xFFFF) | (k << 16);
			} else {
				if (i == 2){	// 1,2,4,8,16 と2の乗数にする
					recovery_num = 0;
					i = 1;	// exp_num
					j = 0;	// total count
					k = 0;	// count in the file
					while (j < parity_num){
						k = i;
						if (k >= recovery_limit){	// サイズが制限されてるなら
							k = recovery_limit;
						} else {
							i *= 2;
						}
						j += k;
						recovery_num++;
					}
					recovery_limit = k;
				} else {	// 1,1,2,5,10,10,20,50 という decimal weights sizing scheme にする
					recovery_num = 0;
					i = 1;	// exp_num
					j = 0;	// total count
					k = 0;	// count in the file
					while (j < parity_num){
						k = i;
						if (k >= recovery_limit){	// サイズが制限されてるなら
							k = recovery_limit;
						} else {
							switch (recovery_num % 4){
							case 1:
							case 3:
								i = i * 2;
								break;
							case 2:
								i = (i / 2) * 5;
								break;
							}
						}
						j += k;
						recovery_num++;
					}
					recovery_limit = k;
				}
			}
		} else {
			recovery_num = 0;
		}
		printf("Input File count\t: %d\n", file_num);
		printf("Input File total size\t: %I64d\n", total_file_size);
		printf("Input File Slice size\t: %u\n", block_size);
		printf("Input File Slice count\t: %d\n", source_num);
		printf("Recovery Slice count\t: %d\n", parity_num);
		if (first_num > 0)
			printf("Recovery Slice start\t: %d\n", first_num);
		if (source_num != 0){
			i = (10000 * parity_num) / source_num;
		} else {
			i = 0;
		}
		printf("Redundancy rate\t\t: %d.%02d%%\n", i / 100, i % 100);
		printf("Recovery File count\t: %d\n", recovery_num);	// Index File の分は含まない
		if (parity_num > 0){
			i = (switch_set & 0x30000) >> 16;
			printf("Slice distribution\t: %d, ", i);
			switch (i){
			case 0:
				printf("uniform (until %d)\n", recovery_limit);
				break;
			case 1:
				printf("variable (base %d until %d)\n", (unsigned int)recovery_limit >> 16, recovery_limit & 0xFFFF);
				break;
			case 2:
				printf("power of two (until %d)\n", recovery_limit);
				break;
			case 3:
				printf("decimal weights (until %d)\n", recovery_limit);
				break;
			}
			k = (switch_set & 0x0700) >> 8;
			printf("Packet Repetition limit\t: %d\n", k);
		}
		if (split_size == 1){	// 書庫にリカバリ・レコードを追加できるか
			if ((file_num != 1) || (total_file_size < 54)){
				split_size = 0;	// zip's min = 100-byte, 7z's min = 74-byte
			} else {	// 拡張子を調べる
				j = (int)wcslen(list_buf);
				if ((_wcsicmp(list_buf + j - 4, L".zip") != 0) && (_wcsicmp(list_buf + j - 3, L".7z") != 0)){
					split_size = 0;
				} else {
					printf_cp("Append recovery record\t: \"%s\"\n", list_buf);
				}
			}
		} else if (split_size >= 4){	// 分割サイズは指定された時だけ表示する
			if (total_file_size == 0){
				split_size = 0;
			} else {
				printf("Split size\t\t: %u\n", split_size);
			}
		}
		if (parity_num + first_num > MAX_PARITY_NUM){	// ブロック数の制限
			free(list_buf);
			printf("too many recovery blocks %d\n", parity_num + first_num);
			return 1;
		}
		j = (switch_set & 0x01) | ((switch_set & 0x08) >> 2);
		if (argv[1][0] == 'c'){
			i = par2_create(uni_buf, (switch_set & 0x0700) >> 8, (switch_set & 0x70000) >> 16, j);
		} else {
			i = par2_trial(uni_buf, (switch_set & 0x0700) >> 8, (switch_set & 0x70000) >> 16, j);
		}
		break;
	case 'v':
	case 'r':
		// 検査・修復なら外部の検査対象ファイルのリストがあるかもしれない
		if (i < argc){	// 外部のファイルが指定されてるなら
			wchar_t search_path[MAX_LEN];
			int dir_len;
			list2_len = 0;
			list2_max = ALLOC_LEN;
			list2_buf = (wchar_t *)malloc(list2_max * 2);
			if (list2_buf == NULL){
				printf("malloc, %d\n", list2_max * 2);
				return 1;
			}

			if (switch_set & 0x06){	// ファイル・リストの読み込み
				j = (switch_set & 0x04) ? CP_UTF8 : CP_OEMCP;
				if (read_external_list(argv[i], j))
					list2_len = 0;
				i = argc;	// それ以上読み込まない
			}

			for (; i < argc; i++){
				j = copy_path_prefix(search_path, MAX_LEN, argv[i], NULL);
				if (j == 0){
					free(list2_buf);
					printf_cp("filename is invalid, %s\n", argv[i]);
					return 1;
				}
				//printf_cp("external file, %s\n", search_path);
				dir_len = (int)wcslen(search_path) - 2;	// ファイル名末尾の「\」を無視して、ディレクトリ部分の長さを求める
				while (search_path[dir_len] != '\\')
					dir_len--;
				dir_len++;
				// 「*」や「?」で検索しない場合、ファイルが見つからなければエラーにする
				j = -1;
				if (wcspbrk(search_path + dir_len, L"*?") == NULL)
					j = list2_len;
				if (search_external_files(search_path, dir_len, j)){
					free(list2_buf);
					return 1;
				}
				if ((j != -1) && (j == list2_len)){	// ファイルが見つかったか確かめる
					free(list2_buf);
					printf_cp("external file is not found, %s\n", search_path);
					return 1;
				}
			}
/*{
FILE *fp;
fp = fopen("list2_buf.txt", "wb");
fwrite(list2_buf, 2, list2_len, fp);
fclose(fp);
}*/
			if (list2_len == 0){	// 有効なファイルが見つからなかった場合
				free(list2_buf);
				list2_buf = NULL;
			}
		}
		if (switch_set & 0x20)
			json_open();	// 検査結果を JSONファイルに書き込む
		if (argv[1][0] == 'v'){
			i = par2_verify(uni_buf);
		} else {
			i = par2_repair(uni_buf);
		}
		json_close();
		if (list2_buf)
			free(list2_buf);
		break;
	case 'l':
		i = par2_list(uni_buf, (switch_set & 0x10) >> 4);
		break;
	}

	if (list_buf);
		free(list_buf);
	//printf("ExitCode: 0x%02X\n", i);
	return i;
}

