// json.c
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

#include <stdio.h>

#include <windows.h>

#include "common2.h"

/*
{
"SelectedFile":"Full path of selected recovery file",
"BaseDirectory":"Full path of base directory of source files",
"RecoveryFile":[
"Name of recovery file1",
"Name of recovery file2",
"Name of recovery file3"
],
"SourceFile":[
"Name of source file1",
"Name of source file2",
"Name of source file3"
],
"FoundFile":[
"Name of found file1",
"Name of found file2"
],
"ExternalFile":[
"Name of external file1",
"Name of external file2"
],
"DamagedFile":[
"Name of damaged file1",
"Name of damaged file2"
],
"AppendedFile":[
"Name of appended file1",
"Name of appended file2"
],
"MissingFile":[
"Name of missing file1",
"Name of missing file2"
],
"MisnamedFile":{
"Correct name of misnamed file1":"Wrong name",
"Correct name of misnamed file2":"Wrong name"
}
}
*/
static FILE *fp_json = NULL;	// JSONファイル用

static wchar_t *list3_buf = NULL;	// 検出ファイルの名前リスト
static int list3_len, list3_max;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// JSONファイルを開く
void json_open(void)
{
	wchar_t path[MAX_LEN];
	int len;

	if (fp_json != NULL)
		return;	// 既に開いてる場合はそのまま

	wcscpy(path, ini_path);
	wcscat(path, offset_file_name(recovery_file));
	wcscat(path, L".json");
	//printf("json_path = %ls\n", path);

	// 既に存在する場合は上書きして作り直す
	fp_json = _wfopen(path, L"wt, ccs=UTF-8");
	if (fp_json == NULL)
		return;

	// 基礎情報を書き込む
	rewind(fp_json);	// Remove BOM of UTF-8
	copy_without_prefix(path, recovery_file);
	unix_directory(path);
	if (fwprintf(fp_json, L"{\n\"SelectedFile\":\"%s\"", path) < 0){
		fclose(fp_json);
		fp_json = NULL;
		return;
	}
	len = copy_without_prefix(path, base_dir);
	path[len - 1] = 0;	// 末尾の「\」を取り除く
	unix_directory(path);
	if (fwprintf(fp_json, L",\n\"BaseDirectory\":\"%s\"", path) < 0){
		fclose(fp_json);
		fp_json = NULL;
		return;
	}
}

// JSONファイルを閉じる
void json_close(void)
{
	if (fp_json == NULL)
		return;	// 既に閉じてる場合はそのまま

	fputws(L"\n}\n", fp_json);
	fclose(fp_json);
	fp_json = NULL;
}

// ファイル一覧
void json_file_list(file_ctx_r *files)
{
	wchar_t path[MAX_LEN];
	int rv, i;

	if (fp_json == NULL)
		return;	// 開いてない場合は何もしない

	// パケットを含む (Good or Damaged) リカバリ・ファイルのリスト
	// 全くパケットを含まない (Useless) ファイルは除く
	if (fputws(L",\n\"RecoveryFile\":[", fp_json) < 0){
		fclose(fp_json);
		fp_json = NULL;
		return;
	}
	i = 0;
	while (i < recv2_len){
		if (compare_directory(recovery_file, recv2_buf + i) == 0){
			// 指定されたリカバリ・ファイルと同じ場所ならファイル名のみ
			wcscpy(path, offset_file_name(recv2_buf + i));
		} else {
			// 異なる場所（External File）ならフルパス
			copy_without_prefix(path, recv2_buf + i);
			unix_directory(path);
		}
		if (i == 0){
			rv = fwprintf(fp_json, L"\n\"%s\"", path);
		} else {
			rv = fwprintf(fp_json, L",\n\"%s\"", path);
		}
		if (rv < 0){
			fclose(fp_json);
			fp_json = NULL;
			return;
		}

		while (recv2_buf[i] != 0)
			i++;
		i++;
	}
	if (fputws(L"\n]", fp_json) < 0){
		fclose(fp_json);
		fp_json = NULL;
		return;
	}

	// ソース・ファイルのリスト
	if (fputws(L",\n\"SourceFile\":[", fp_json) < 0){
		fclose(fp_json);
		fp_json = NULL;
		return;
	}
	for (i = 0; i < file_num; i++){
		wcscpy(path, list_buf + files[i].name);
		unix_directory(path);
		if (i == 0){
			rv = fwprintf(fp_json, L"\n\"%s\"", path);
		} else {
			rv = fwprintf(fp_json, L",\n\"%s\"", path);
		}
		if (rv < 0){
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	}
	if (fputws(L"\n]", fp_json) < 0){
		fclose(fp_json);
		fp_json = NULL;
		return;
	}
}

// ファイルの状態
void json_file_state(file_ctx_r *files)
{
	wchar_t path[MAX_LEN], path2[MAX_LEN];
	int rv, i, flag_item;

	if (fp_json == NULL)
		return;	// 開いてない場合は何もしない

	// 破損ファイル
	flag_item = 0;
	for (i = 0; i < file_num; i++){
		if (files[i].state & 2){
			wcscpy(path, list_buf + files[i].name);
			unix_directory(path);
			if (flag_item == 0){
				flag_item = 1;
				if (fputws(L",\n\"DamagedFile\":[", fp_json) < 0){
					fclose(fp_json);
					fp_json = NULL;
					return;
				}
				rv = fwprintf(fp_json, L"\n\"%s\"", path);
			} else {
				rv = fwprintf(fp_json, L",\n\"%s\"", path);
			}
			if (rv < 0){
				fclose(fp_json);
				fp_json = NULL;
				return;
			}
		}
	}
	if (flag_item != 0){
		if (fputws(L"\n]", fp_json) < 0){
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	}

	// 追加されたファイル
	flag_item = 0;
	for (i = 0; i < file_num; i++){
		if (files[i].state & 16){
			wcscpy(path, list_buf + files[i].name);
			unix_directory(path);
			if (flag_item == 0){
				flag_item = 1;
				if (fputws(L",\n\"AppendedFile\":[", fp_json) < 0){
					fclose(fp_json);
					fp_json = NULL;
					return;
				}
				rv = fwprintf(fp_json, L"\n\"%s\"", path);
			} else {
				rv = fwprintf(fp_json, L",\n\"%s\"", path);
			}
			if (rv < 0){
				fclose(fp_json);
				fp_json = NULL;
				return;
			}
		}
	}
	if (flag_item != 0){
		if (fputws(L"\n]", fp_json) < 0){
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	}

	// 消失ファイル
	flag_item = 0;
	for (i = 0; i < file_num; i++){
		if (files[i].state & 1){
			wcscpy(path, list_buf + files[i].name);
			unix_directory(path);
			if (flag_item == 0){
				flag_item = 1;
				if (fputws(L",\n\"MissingFile\":[", fp_json) < 0){
					fclose(fp_json);
					fp_json = NULL;
					return;
				}
				rv = fwprintf(fp_json, L"\n\"%s\"", path);
			} else {
				rv = fwprintf(fp_json, L",\n\"%s\"", path);
			}
			if (rv < 0){
				fclose(fp_json);
				fp_json = NULL;
				return;
			}
		}
	}
	if (flag_item != 0){
		if (fputws(L"\n]", fp_json) < 0){
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	}

	// 別名ファイルは、"本来の名前":"間違った名前" というセットにする
	flag_item = 0;
	for (i = 0; i < file_num; i++){
		if (files[i].state & 32){
			// ソースファイルの本来の名前
			wcscpy(path, list_buf + files[i].name);
			unix_directory(path);
			// 別名
			wcscpy(path2, list_buf + files[i].name2);
			unix_directory(path2);
			if (flag_item == 0){
				flag_item = 1;
				if (fputws(L",\n\"MisnamedFile\":{", fp_json) < 0){
					fclose(fp_json);
					fp_json = NULL;
					return;
				}
				rv = fwprintf(fp_json, L"\n\"%s\":\"%s\"", path, path2);
			} else {
				rv = fwprintf(fp_json, L",\n\"%s\":\"%s\"", path, path2);
			}
			if (rv < 0){
				fclose(fp_json);
				fp_json = NULL;
				return;
			}
		}
	}
	if (flag_item != 0){
		if (fputws(L"\n}", fp_json) < 0){
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	}
}

// 検出されたファイル名を保持する
void json_add_found(wchar_t *filename, int flag_external)
{
	wchar_t *tmp_p;
	int len;

	if (fp_json == NULL)
		return;	// 開いてない場合は何もしない

	len = (int)wcslen(filename) + 1;

	if (list3_buf == NULL){
		list3_max = ALLOC_LEN;
		list3_len = 0;
		list3_buf = malloc(list3_max * 2);
		if (list3_buf == NULL){
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	} else if (list3_len + len >= list3_max){	// 領域が足りなくなるなら拡張する
		list3_max += ALLOC_LEN;
		tmp_p = (wchar_t *)realloc(list3_buf, list3_max * 2);
		if (tmp_p == NULL){
			free(list3_buf);
			list3_buf = NULL;
			fclose(fp_json);
			fp_json = NULL;
			return;
		} else {
			list3_buf = tmp_p;
		}
	}

	if (flag_external != 0){
		list3_buf[list3_len] = 'e';
	} else {
		list3_buf[list3_len] = 'f';
	}
	wcscpy(list3_buf + list3_len + 1, filename);
	list3_len += len + 1;
}

// 検出されたファイル名を書き込む
void json_save_found(void)
{
	wchar_t path[MAX_LEN];
	int rv, i, flag_item;

	if (fp_json == NULL)
		return;	// 開いてない場合は何もしない
	if (list3_buf == NULL)
		return;	// 検出しなかった場合は何もしない

/*{
FILE *fp;
fp = fopen("list3.txt", "wb");
fwrite(list3_buf, 2, list3_len, fp);
fclose(fp);
}*/

	// ブロックを検出したファイル
	flag_item = 0;
	i = 0;
	while (i < list3_len){
		if (list3_buf[i] == 'f'){
			wcscpy(path, list3_buf + i + 1);
			unix_directory(path);
			if (flag_item == 0){
				flag_item = 1;
				if (fputws(L",\n\"FoundFile\":[", fp_json) < 0){
					free(list3_buf);
					list3_buf = NULL;
					fclose(fp_json);
					fp_json = NULL;
					return;
				}
				rv = fwprintf(fp_json, L"\n\"%s\"", path);
			} else {
				rv = fwprintf(fp_json, L",\n\"%s\"", path);
			}
			if (rv < 0){
				free(list3_buf);
				list3_buf = NULL;
				fclose(fp_json);
				fp_json = NULL;
				return;
			}
		}

		while (list3_buf[i] != 0)
			i++;
		i++;
	}
	if (flag_item != 0){
		if (fputws(L"\n]", fp_json) < 0){
			free(list3_buf);
			list3_buf = NULL;
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	}

	// ブロックを検出した外部ファイル
	flag_item = 0;
	i = 0;
	while (i < list3_len){
		if (list3_buf[i] == 'e'){
			copy_without_prefix(path, list3_buf + i + 1);
			unix_directory(path);
			if (flag_item == 0){
				flag_item = 1;
				if (fputws(L",\n\"ExternalFile\":[", fp_json) < 0){
					free(list3_buf);
					list3_buf = NULL;
					fclose(fp_json);
					fp_json = NULL;
					return;
				}
				rv = fwprintf(fp_json, L"\n\"%s\"", path);
			} else {
				rv = fwprintf(fp_json, L",\n\"%s\"", path);
			}
			if (rv < 0){
				free(list3_buf);
				list3_buf = NULL;
				fclose(fp_json);
				fp_json = NULL;
				return;
			}
		}

		while (list3_buf[i] != 0)
			i++;
		i++;
	}
	if (flag_item != 0){
		if (fputws(L"\n]", fp_json) < 0){
			free(list3_buf);
			list3_buf = NULL;
			fclose(fp_json);
			fp_json = NULL;
			return;
		}
	}

	// 最後にメモリーを開放する
	free(list3_buf);
	list3_buf = NULL;
}
