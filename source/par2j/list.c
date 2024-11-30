// list.c
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

#include <process.h>
#include <stdio.h>

#include <windows.h>

#include "common2.h"
#include "md5_crc.h"
#include "verify.h"
#include "ini.h"
#include "json.h"
#include "list.h"

//#define TIMER // 実験用

#ifdef TIMER
#include <time.h>
static double time_sec, time_speed;
#endif

// recovery set のファイルのハッシュ値を調べる (空のファイルは除く)
// 0x00 = ファイルが存在して完全である
// 0x01 = ファイルが存在しない
// 0x02 = ファイルが破損してる
// 0x10 = ファイルに追加されてる
static int check_file_hash(
	char *ascii_buf,		// ファイル名が入ってる
	wchar_t *file_path,		// 作業用
	int num,				// file_ctx におけるファイル番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, bad_flag = 0;
	unsigned int meta_data[7];
	__int64 file_size;	// 存在するファイルのサイズは本来のサイズとは異なることもある
	HANDLE hFile;

	// ファイルが存在するか
	wcscpy(file_path, base_dir);
	wcscpy(file_path + base_len, list_buf + files[num].name);
/*
// ディスク・キャッシュを使わない場合の実験用
i = file_hash_direct(num, file_path, list_buf + files[num].name, files, s_blk);
printf("file_hash_direct = %d\n", i);
if (i == -2)
	return 2;	// 検査中にキャンセルされた
*/
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED, NULL);
	if (hFile == INVALID_HANDLE_VALUE){	// ファイルが存在しない
		bad_flag = 1;
	} else {	// ファイルの状態が記録されてるか調べる
		i = check_ini_state(num, meta_data, hFile);
		if (i == -1){	// エラー
			CloseHandle(hFile);
			bad_flag = 1;	// 属性を取得できないファイルは消失と判定する
		}
	}
	if (bad_flag == 0){	// ファイルが存在するなら状態を確認する
		memcpy(&file_size, meta_data, 8);
		if (file_size >= files[num].size){	// 本来のサイズまでのハッシュ値が一致するか確かめる
			if (i == -2){	// 検査結果の記録が無ければ
				i = file_hash_check(num, list_buf + files[num].name, hFile, 0, files, s_blk);
				if (i == -2)
					return 2;	// 検査中にキャンセルされた
				// MD5-16k が一致した時だけ検査結果を記録する
				if (i != -1)
					write_ini_state(num, meta_data, i);
			}
			if (i != -3){	// ファイルのハッシュ値が異なる
				bad_flag = 2;	// IO エラーでも破損扱いにして処理を続行する
				if (i > 0)		// 先頭に完全なブロックが見つかってるなら
					bad_flag |= i << 8;	// 上位 24-bit に完全なブロックの個数を記録する
				// MD5-16k だけ異なるのは検出ブロック 0個として扱う
			} else if (file_size > files[num].size){	// 本来のサイズまでのハッシュ値は同じ
				bad_flag = 16;	// 末尾にゴミが付いてないか調べる
			}
		} else {	// file_size < files[num].size
			bad_flag = 2;
		}
		CloseHandle(hFile);
	}

	files[num].state = bad_flag;
	if (bad_flag == 1){	// 消失
		printf("            - Missing  : \"%s\"\n", ascii_buf);
	} else {	// 完全、破損または追加 (追加は特殊な破損状態)
		int b_last;
		if (bad_flag & 2){	// 破損なら上位 24-bit に完全なブロックの個数が記録されてる
			bad_flag >>= 8;
			b_last = files[num].b_off + bad_flag;	// 途中まで有効と見なす
			for (i = files[num].b_off; i < b_last; i++)
				s_blk[i].exist = 2;
			first_num += bad_flag;	// ブロック数を集計する
			print_progress_file(-1, first_num, NULL);	// 重複を除外した合計ブロック数を表示する
			printf("%13I64d %8d : \"%s\"\n", file_size, bad_flag, ascii_buf);
		} else {	// 完全か追加
			b_last = files[num].b_off + files[num].b_num;
			for (i = files[num].b_off; i < b_last; i++)
				s_blk[i].exist = 1;	// 全ブロックが有効と見なす
			first_num += files[num].b_num;	// ブロック数を集計する
			print_progress_file(-1, first_num, NULL);	// 重複を除外した合計ブロック数を表示する
			if (bad_flag == 0){	// サイズが同じでハッシュ値が一致したら完全
				printf("            = Complete : \"%s\"\n", ascii_buf);
			} else {			// 本来のサイズまでハッシュ値が一致したら追加
				printf("%13I64d Appended : \"%s\"\n", file_size, ascii_buf);
			}
		}
	}

	return 0;
}

// ブロックのチェックサムが欠落してるファイルを検査する
// 0x80 = ファイルが存在して完全である (全てのブロックが完全とみなす)
// 0x81 = ファイルが存在しない
// 0x82 = ファイルが破損してる
// 0x90 = ファイルに追加されてる (全てのブロックが完全とみなす)
int check_file_hash_incomplete(
	char *ascii_buf,		// ファイル名が入ってる
	wchar_t *file_path,		// 作業用
	int num,				// file_ctx におけるファイル番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, bad_flag = 0;
	unsigned int meta_data[7];
	__int64 file_size;	// 存在するファイルのサイズは本来のサイズとは異なることもある
	HANDLE hFile;

	// ファイルが存在するか
	wcscpy(file_path, base_dir);
	wcscpy(file_path + base_len, list_buf + files[num].name);
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED, NULL);
	if (hFile == INVALID_HANDLE_VALUE){	// ファイルが存在しない
		bad_flag = 1;
	} else {	// ファイルの状態が記録されてるか調べる
		i = check_ini_state(num, meta_data, hFile);
		if (i == -1){	// エラー
			CloseHandle(hFile);
			bad_flag = 1;	// 属性を取得できないファイルは消失と判定する
		}
	}
	if (bad_flag == 0){	// ファイルが存在するなら状態を確認する
		memcpy(&file_size, meta_data, 8);
		if (file_size >= files[num].size){	// 本来のサイズまでのハッシュ値が一致するか確かめる
			if (i == -2){	// 検査結果の記録が無ければ
				i = file_hash_check(num, list_buf + files[num].name, hFile, INT_MAX, files, NULL);
				if (i == -2)
					return 2;	// 検査中にキャンセルされた
				// MD5-16k が一致した時だけ検査結果を記録する
				if (i != -1)
					write_ini_state(num, meta_data, i);
			}
			if (i != -3){	// ファイルのハッシュ値が異なる
				bad_flag = 2;	// IO エラーでも破損扱いにして処理を続行する
			} else if (file_size > files[num].size){	// 本来のサイズまでのハッシュ値は同じ
				bad_flag = 16;	// 末尾にゴミが付いてないか調べる
			}
		} else {	// file_size < files[num].size
			bad_flag = 2;
		}
		CloseHandle(hFile);
	}

	files[num].state = bad_flag | 0x80;	// チェックサムが存在しない印
	if (bad_flag == 1){	// 消失
		printf("            - Missing  : \"%s\"\n", ascii_buf);
	} else {	// 完全、破損または追加 (追加は特殊な破損状態)
		if (bad_flag & 2){	// ブロック単位の検査ができないので全て破損として扱う
			printf("%13I64d Damaged  : \"%s\"\n", file_size, ascii_buf);
		} else {	// 完全か追加
			// ブロック単位の検査はできないがとりあえず有効として扱う
			int b_last;
			b_last = files[num].b_off + files[num].b_num;
			for (i = files[num].b_off; i < b_last; i++)
				s_blk[i].exist = 1;	// 全ブロックが有効と見なす
			first_num += files[num].b_num;	// ブロック数を集計する
			print_progress_file(-1, first_num, NULL);	// 重複を除外した合計ブロック数を表示する
			if (bad_flag == 0){
				printf("            = Complete : \"%s\"\n", ascii_buf);
			} else {
				printf("%13I64d Appended : \"%s\"\n", file_size, ascii_buf);
			}
		}
	}

	return 0;
}

// 空ファイルやフォルダが存在するか調べる
// non-recovery set のファイルが存在して破損してないかも調べる
//  0 = ファイルが存在して完全である
//  1 = ファイルが存在しない
//  2 = ファイルが破損してる
// 16 = ファイルに追加されてる
// 64 = フォルダが存在する
// 65 = フォルダが存在しない
static int check_file_exist(
	char *ascii_buf,		// ファイル名が入ってる
	wchar_t *file_path,		// 作業用
	int num,				// file_ctx におけるファイル番号
	file_ctx_r *files)		// 各ソース・ファイルの情報
{
	int i, bad_flag = 0;
	unsigned int meta_data[7];
	__int64 file_size;
	HANDLE hFile;

	wcscpy(file_path, base_dir);
	wcscpy(file_path + base_len, list_buf + files[num].name);
	i = (int)wcslen(file_path);
	if ((i > 1) && (file_path[i - 1] == '\\')){	// フォルダなら
		bad_flag = 64;
		i = GetFileAttributes(file_path);
		if ((i == INVALID_FILE_ATTRIBUTES) || ((i & FILE_ATTRIBUTE_DIRECTORY) == 0)){
			bad_flag = 65;	// フォルダが存在しない
			printf("            - Missing  : \"%s\"\n", ascii_buf);
		} else {	// フォルダが存在する
			printf("            0 Complete : \"%s\"\n", ascii_buf);
		}
		files[num].state = bad_flag;
		return 0;
	}

	// ファイルが存在するか
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED, NULL);
	if (hFile == INVALID_HANDLE_VALUE){	// ファイルが存在しない
		bad_flag = 1;
	} else {
		// ファイルのサイズを調べる
		i = check_ini_state(num, meta_data, hFile);
		if (i == -1){	// エラー
			CloseHandle(hFile);
			bad_flag = 1;	// 属性を取得できないファイルは消失と判定する
		}
	}
	if (bad_flag == 0){	// ファイルが存在するなら状態を確認する
		memcpy(&file_size, meta_data, 8);
		if (files[num].size == 0){	// サイズが 0 だと破損しようがない
			if (file_size > 0)	// 末尾にゴミが付いてる
				bad_flag = 16;
		} else if (file_size >= files[num].size){	// 本来のサイズまでのハッシュ値が一致するか確かめる
			if (i == -2){	// 検査結果の記録が無ければ
				i = file_hash_check(num, list_buf + files[num].name, hFile, INT_MAX, files, NULL);
				if (i == -2)
					return 2;	// 検査中にキャンセルされた
				// MD5-16k が一致した時だけ検査結果を記録する
				if (i != -1)
					write_ini_state(num, meta_data, i);
			}
			if (i != -3){	// ファイルのハッシュ値が異なる
				bad_flag = 2;	// IO エラーでも破損扱いにして処理を続行する
			} else if (file_size > files[num].size){	// 本来のサイズまでのハッシュ値は同じ
				bad_flag = 16;	// 末尾にゴミが付いてないか調べる
			}
		} else {	// file_size < files[num].size
			bad_flag = 2;
		}
		CloseHandle(hFile);
	}

	files[num].state = bad_flag;
	switch (bad_flag){
	case 0:		// 完全なファイル
		printf("            = Complete : \"%s\"\n", ascii_buf);
		break;
	case 1:		// 消失したファイル
		printf("            - Missing  : \"%s\"\n", ascii_buf);
		break;
	case 2:		// 破損したファイル
		printf("%13I64d Damaged  : \"%s\"\n", file_size, ascii_buf);
		break;
	case 16:	// 追加されたファイル
		printf("%13I64d Appended : \"%s\"\n", file_size, ascii_buf);
		break;
	}

	return 0;
}

// ソース・ファイルが完全かどうかを調べる
// 0=完了, 1=エラー, 2=キャンセル
int check_file_complete(
	char *ascii_buf,
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, rv;
#ifdef TIMER
clock_t time_start = clock();
#endif

	printf("\nVerifying Input File   :\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	count_last = 0;
	first_num = 0;	// 初めて見つけたソース・ブロックの数
	for (i = 0; i < entity_num; i++){
		if (cancel_progress() != 0)	// キャンセル処理
			return 2;
		utf16_to_cp(list_buf + files[i].name, ascii_buf, cp_output);

		if (files[i].size == 0){	// フォルダまたは空ファイルが存在するか調べる
			rv = check_file_exist(ascii_buf, file_path, i, files);
		} else {
			if (files[i].state & 0x80){	// チェックサムが欠落してても完全かどうかは判る
				rv = check_file_hash_incomplete(ascii_buf, file_path, i, files, s_blk);
			} else {	// ソース・ファイルの状態 (完全、消失、破損) を判定する
				rv = check_file_hash(ascii_buf, file_path, i, files, s_blk);
			}
		}
		if (rv != 0)
			return rv;
		fflush(stdout);
	}
	for (i = entity_num; i < file_num; i++){
		if (cancel_progress() != 0)	// キャンセル処理
			return 2;
		utf16_to_cp(list_buf + files[i].name, ascii_buf, cp_output);
		if (rv = check_file_exist(ascii_buf, file_path, i, files))
			return rv;
		fflush(stdout);
	}

#ifdef TIMER
time_start = clock() - time_start;
time_sec = (double)time_start / CLOCKS_PER_SEC;
if (time_sec > 0){
	time_speed = (double)total_file_size / (time_sec * 1048576);
} else {
	time_speed = 0;
}
printf("\n hash %.3f sec, %.0f MB/s\n", time_sec, time_speed);
#endif
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// SSD 上で複数ファイルを同時に検査する

// MAX_MULTI_READ の２倍ぐらいにする？
#define MAX_READ_NUM 12

int check_file_complete_multi(
	char *ascii_buf,
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int err = 0, i, rv;
	int num, bad_flag, multi_read, multi_num, id, id_prog;
	unsigned int time_last;
	__int64 file_size;	// 存在するファイルのサイズは本来のサイズとは異なることもある
	HANDLE hFile;
	HANDLE hSub[MAX_READ_NUM];
	FILE_CHECK_TH th[MAX_READ_NUM];
#ifdef TIMER
clock_t time_start = clock();
#endif

	memset(hSub, 0, sizeof(HANDLE) * MAX_READ_NUM);
	// Core数に応じてスレッド数を増やす
	if ((memory_use & 32) != 0){	// NVMe SSD
		multi_read = (cpu_num + 2) / 3 + 1;	// 3=2, 4~6=3, 7~9=4, 10~12=5, 13~=6
		if (multi_read > MAX_READ_NUM / 2)
			multi_read = MAX_READ_NUM / 2;
	} else {	// SATA SSD
		multi_read = 2;
	}
	if (multi_read > entity_num)
		multi_read = entity_num;
	multi_num = 0;
#ifdef TIMER
	printf("\n cpu_num = %d, entity_num = %d, multi_read = %d\n", cpu_num, entity_num, multi_read);
#endif
	id_prog = -1;
	for (i = 0; i < MAX_READ_NUM; i++)
		th[i].num = -1;	// データ未使用の印
	wcscpy(file_path, base_dir);

	printf("\nVerifying Input File   :\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	time_last = GetTickCount();
	count_last = 0;
	first_num = 0;	// 初めて見つけたソース・ブロックの数
	for (num = 0; num < entity_num; num++){
		if (cancel_progress() != 0){	// キャンセル処理
			err = 2;
			goto error_end;
		}

		// ソース・ファイルの状態 (完全、消失、破損) を判定する
		// チェックサムが欠落してても完全かどうかは判る
		if (files[num].size > 0){
			for (id = 0; id < MAX_READ_NUM; id++){	// 未使用のスレッドを探す
				if (th[id].num < 0)
					break;
			}
			if (id_prog < 0){
				id_prog = id;
				prog_last = -1;
			}
			th[id].num = num;
			hSub[id] = NULL;

			// 検査以外はシングル・スレッドで行う
			th[id].flag = 0;
			wcscpy(file_path + base_len, list_buf + files[num].name);
			hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED, NULL);
			if (hFile == INVALID_HANDLE_VALUE){	// ファイルが存在しない
				th[id].flag = 1;
			} else {	// ファイルの状態が記録されてるか調べる
				th[id].rv = check_ini_state(num, th[id].meta, hFile);
				if (th[id].rv == -1){	// エラー
					CloseHandle(hFile);
					th[id].flag = 1;	// 属性を取得できないファイルは消失と判定する
				}
			}
			if (th[id].flag == 0){	// ファイルが存在するなら状態を確認する
				memcpy(&file_size, th[id].meta, 8);
				if (file_size >= files[num].size){	// 本来のサイズまでのハッシュ値が一致するか確かめる
					if (th[id].rv == -2){	// 検査結果の記録が無ければ
						//rv = file_hash_check(num, list_buf + files[num].name, hFile, 0, files, s_blk);
						th[id].hFile = hFile;
						th[id].files = files;
						if (files[num].state & 0x80){
							th[id].flag |= 0x80;
							th[id].s_blk = NULL;
						} else {
							th[id].s_blk = s_blk;
						}
						th[id].rv = 0;
						//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
						hSub[id] = (HANDLE)_beginthreadex(NULL, STACK_SIZE, file_hash_background, (LPVOID)&(th[id]), 0, NULL);
						if (hSub[id] == NULL){
							print_win32_err();
							printf("error, sub-thread\n");
							CloseHandle(hFile);
							err = 1;
							goto error_end;
						}
						multi_num++;
					} else {
						CloseHandle(hFile);
					}
				} else {	// file_size < files[num].size
					CloseHandle(hFile);
					if (files[num].state & 0x80){
						th[id].flag = 2 | 0x80;
					} else {
						th[id].flag = 2;
					}
				}
			}
			//printf("num = %d, multi_num = %d, id = %d, id_prog = %d\n", num, multi_num, id, id_prog);
		}

		id = id_prog;
		//printf("check: num = %d, multi_num = %d, id = %d, id_prog = %d\n", num, multi_num, id, id_prog);
		while ((id >= 0) && (id < MAX_READ_NUM)){
			if (hSub[id]){	// 終了してるか調べる
				rv = WaitForSingleObject(hSub[id], 0);
				if (rv == WAIT_OBJECT_0){	// 終了してたら閉じる
					CloseHandle(hSub[id]);
					hSub[id] = NULL;
					multi_num--;
				}
			}
			if (hSub[id] == NULL){
				//printf("id = %d, th[id].rv = %d, th[id].flag = %d\n", id, th[id].rv, th[id].flag);
				bad_flag = th[id].flag & ~0x80;
				if (bad_flag != 1){	// 消失以外
					memcpy(&file_size, th[id].meta, 8);
					if (bad_flag != 2){	// 破損以外
						rv = th[id].rv;
						// MD5-16k が一致した時だけ検査結果を記録する
						if (rv != -1)
							write_ini_state(th[id].num, th[id].meta, rv);
						if (rv != -3){	// ファイルのハッシュ値が異なる
							bad_flag = 2;	// IO エラーでも破損扱いにして処理を続行する
							if (rv > 0)		// 先頭に完全なブロックが見つかってるなら
								bad_flag |= rv << 8;	// 上位 24-bit に完全なブロックの個数を記録する
							// MD5-16k だけ異なるのは検出ブロック 0個として扱う
						} else if (file_size > files[th[id].num].size){	// 本来のサイズまでのハッシュ値は同じ
							bad_flag = 16;	// 末尾にゴミが付いてないか調べる
						}
					}
				}

				utf16_to_cp(list_buf + files[th[id].num].name, ascii_buf, cp_output);
				if (th[id].flag & 0x80){
					files[th[id].num].state = bad_flag | 0x80;	// チェックサムが存在しない印
				} else {
					files[th[id].num].state = bad_flag;
				}
				if (bad_flag == 1){	// 消失
					printf("            - Missing  : \"%s\"\n", ascii_buf);
				} else {	// 完全、破損または追加 (追加は特殊な破損状態)
					int b_last;
					if (bad_flag & 2){	// 破損なら上位 24-bit に完全なブロックの個数が記録されてる
						if (th[id].flag & 0x80){	// チェックサムが存在しない場合
							printf("%13I64d Damaged  : \"%s\"\n", file_size, ascii_buf);
						} else {
							bad_flag >>= 8;
							b_last = files[th[id].num].b_off + bad_flag;	// 途中まで有効と見なす
							for (i = files[th[id].num].b_off; i < b_last; i++)
								s_blk[i].exist = 2;
							first_num += bad_flag;	// ブロック数を集計する
							print_progress_file(-1, first_num, NULL);	// 重複を除外した合計ブロック数を表示する
							printf("%13I64d %8d : \"%s\"\n", file_size, bad_flag, ascii_buf);
						}
					} else {	// 完全か追加
						b_last = files[th[id].num].b_off + files[th[id].num].b_num;
						for (i = files[th[id].num].b_off; i < b_last; i++)
							s_blk[i].exist = 1;	// 全ブロックが有効と見なす
						first_num += files[th[id].num].b_num;	// ブロック数を集計する
						print_progress_file(-1, first_num, NULL);	// 重複を除外した合計ブロック数を表示する
						if (bad_flag == 0){	// サイズが同じでハッシュ値が一致したら完全
							printf("            = Complete : \"%s\"\n", ascii_buf);
						} else {			// 本来のサイズまでハッシュ値が一致したら追加
							printf("%13I64d Appended : \"%s\"\n", file_size, ascii_buf);
						}
					}
				}
				fflush(stdout);
				time_last = GetTickCount();

				th[id].num = -1;	// 未使用に戻す
				if (id == id_prog)	// 経過表示中のスレッドが結果を表示したら
					id_prog = -1;
			}

			// 動作中のスレッドが終了してるか調べる
			for (id = 0; id < MAX_READ_NUM; id++){
				if (th[id].num >= 0){
					if (hSub[id]){
						rv = WaitForSingleObject(hSub[id], 0);
						if (rv == WAIT_OBJECT_0){	// 終了してたら閉じる
							CloseHandle(hSub[id]);
							hSub[id] = NULL;
							multi_num--;
						}
					}
					// 経過表示してない状態なら、他の終了したスレッドの結果も表示する
					if ((id_prog < 0) && (hSub[id] == NULL)){
						id_prog = id;
						//printf("next: num = %d, multi_num = %d, id = %d, id_prog = %d\n", num, multi_num, id, id_prog);
						break;
					}
				}
			}
			if (id == MAX_READ_NUM){	// 経過表示中、または、他のスレッドが動いて無いなら
				if (id_prog < 0){	// 経過表示するスレッドを指定する
					for (i = 0; i < MAX_READ_NUM; i++){
						if (th[i].num >= 0){
							id_prog = i;
							prog_last = -1;
							break;
						}
					}
					if (id_prog < 0)	// 動作中のスレッドがなければ待たない
						break;
				}
				// 最後のファイル、または、同時検査数や最大スレッド数に達してるなら、終わるのを待つ
				for (i = 0; i < MAX_READ_NUM; i++){
					if (th[i].num < 0)
						break;
				}
				if ((num + 1 == entity_num) || (multi_num == multi_read) || (i == MAX_READ_NUM)){
					id = id_prog;
					//printf("wait: num = %d, multi_num = %d, id = %d, id_prog = %d\n", num, multi_num, id, id_prog);
					if (hSub[id]){	// 終了してるか調べる
						rv = WaitForSingleObject(hSub[id], UPDATE_TIME / 2);
						if (rv == WAIT_OBJECT_0){	// 終了してたら閉じる
							CloseHandle(hSub[id]);
							hSub[id] = NULL;
							multi_num--;
						}
						// 経過表示
						if (GetTickCount() - time_last >= UPDATE_TIME){
							// 上位 12-bit にパーセント、下位 20-bit に検出ブロック数が記録されてる
							i = th[id].rv;
							rv = i >> 20;
							i = first_num + (i & 0x000FFFFF);
							if (print_progress_file(rv, i, list_buf + files[th[id].num].name)){
								err = 2;
								goto error_end;
							}
							time_last = GetTickCount();
						}
					}
				}
			}
		}
	}
	for (num = 0; num < entity_num; num++){
		if (files[num].size > 0)
			continue;
		if (cancel_progress() != 0){	// キャンセル処理
			err = 2;
			goto error_end;
		}
		// フォルダまたは空ファイルが存在するか調べる
		utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
		err = check_file_exist(ascii_buf, file_path, num, files);
		if (err)
			goto error_end;
		fflush(stdout);
	}
	for (num = entity_num; num < file_num; num++){
		if (cancel_progress() != 0){	// キャンセル処理
			err = 2;
			goto error_end;
		}
		utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
		err = check_file_exist(ascii_buf, file_path, num, files);
		if (err)
			goto error_end;
		fflush(stdout);
	}

#ifdef TIMER
time_start = clock() - time_start;
time_sec = (double)time_start / CLOCKS_PER_SEC;
if (time_sec > 0){
	time_speed = (double)total_file_size / (time_sec * 1048576);
} else {
	time_speed = 0;
}
printf("\n hash %.3f sec, %.0f MB/s\n", time_sec, time_speed);
#endif

error_end:
	if (err != 0){	// エラーが発生した場合
		for (i = 0; i < MAX_READ_NUM; i++)	// 終了指示をだす
			InterlockedExchange(&(th[i].flag), -1);
	}
	for (i = 0; i < MAX_READ_NUM; i++){
		if (hSub[i]){
			WaitForSingleObject(hSub[i], INFINITE);
			CloseHandle(hSub[i]);
		}
	}

	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 指定されたフォルダが空かどうかを調べる
static int is_folder_empty(wchar_t *folder_path)	// フォルダのパス
{
	int len;
	HANDLE hFind;
	WIN32_FIND_DATA FindData;

	len = (int)wcslen(folder_path);
	folder_path[len    ] = '*';
	folder_path[len + 1] = 0;
	hFind = FindFirstFile(folder_path, &FindData);
	folder_path[len    ] = 0;	// 元に戻す
	if (hFind != INVALID_HANDLE_VALUE){
		do {
			if ((wcscmp(FindData.cFileName, L".") != 0) && (wcscmp(FindData.cFileName, L"..") != 0)){
				len = 0;	// 空ではない
				break;
			}
		} while (FindNextFile(hFind, &FindData));
	}
	FindClose(hFind);

	return len;
}

// 指定フォルダ内の別名ファイルを探す
// 0=正常終了, 1=エラー, 2=キャンセル
static int recursive_search(
	char *ascii_buf,		// 作業用
	wchar_t *search_path,	// 検索するファイルの親ディレクトリ、見つけた別名ファイルの相対パスが戻る
	int dir_len,			// ディレクトリ部分の長さ
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	__int64 file_size;	// 存在するファイルのサイズは本来のサイズとは異なることもある
	int rv, i, num, b_last, source_len;
	unsigned int meta_data[7];
	HANDLE hFile, hFind;
	WIN32_FIND_DATA FindData;

	// ソース・ファイル名の領域までしか比較しない (別名ファイルが検出されるのは一回だけ)
	source_len = list_len - 1;

	// 指定されたディレクトリ直下のファイルと比較する
	wcscpy(search_path + dir_len, L"*");
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind != INVALID_HANDLE_VALUE){
		do {
			if (cancel_progress() != 0){	// キャンセル処理
				FindClose(hFind);
				return 2;
			}
			if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0){	// フォルダなら
				// 親ディレクトリは無視する
				if ((wcscmp(FindData.cFileName, L".") == 0) || (wcscmp(FindData.cFileName, L"..") == 0))
					continue;
				// 発見したフォルダ名が長すぎる場合は無視する
				if (dir_len + wcslen(FindData.cFileName) + 2 >= MAX_LEN)	// 末尾に「\*」が追加される
					continue;
				// 現在のディレクトリ部分に見つかったフォルダ名を連結する
				wcscpy(search_path + dir_len, FindData.cFileName);
				wcscat(search_path + dir_len, L"\\");
				// 他のソース・ファイルは無視する
				if (search_file_path(list_buf + 1, source_len, search_path + base_len))	// 基準ディレクトリからの相対パスで比較する
					continue;

				// フォルダ名が検出対象と一致するかどうか確かめる
				for (num = entity_num; num < file_num; num++){	// 空フォルダは non-recovery set にしか存在しない
					if (files[num].state == 65){	// 消失したフォルダなら
						if (_wcsicmp(search_path + dir_len, offset_file_name(list_buf + files[num].name)) == 0){
							// 他のファイルのサブ・ディレクトリは動かせないので、中身のあるフォルダは無視する
							if (is_folder_empty(search_path) == 0)
								continue;
							// 移動されたフォルダが見つかった
							files[num].name2 = list_len;	// 移動されたフォルダ名を記録する
							if (add_file_path(search_path + base_len)){
								printf("add_file_path\n");
								FindClose(hFind);
								return 1;
							}
							utf16_to_cp(search_path + base_len, ascii_buf, cp_output);	// 移動されたフォルダ名を表示する
							printf("            0 Found    : \"%s\"\n", ascii_buf);
							files[num].state = 64 | 32;	// 移動されたフォルダ
							utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);	// 本来のフォルダ名を表示する
							printf("            = Moved    : \"%s\"\n", ascii_buf);
							break;	// 見つかった場合は、それ以降のソース・ファイルと比較しない
						}
					}
				}
				continue;
			}

			// 発見したファイル名が長すぎる場合は無視する
			if (dir_len + wcslen(FindData.cFileName) >= MAX_LEN)
				continue;
			// 現在のディレクトリ部分に見つかったファイル名を連結する
			wcscpy(search_path + dir_len, FindData.cFileName);
			// 破損してないリカバリ・ファイルは無視する
			if (search_file_path(recv_buf, recv_len, search_path))	// フル・パスで比較する
				continue;
			// 他のソース・ファイルは無視する
			if (search_file_path(list_buf + 1, source_len, search_path + base_len))	// 基準ディレクトリからの相対パスで比較する
				continue;

			// ファイル・サイズが検出対象のサイズと一致するかどうか
			file_size = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
			for (num = 0; num < file_num; num++){
				if ((files[num].state & 0x03) && (files[num].size == file_size)){
					if (file_size > 0){
						//printf("find size %I64d, %S\n", file_size, search_path + base_len);
						// 別名ファイルのハッシュ値を調べる
						hFile = CreateFile(search_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED, NULL);
						if (hFile != INVALID_HANDLE_VALUE){	// ファイルを開けたなら内容を確認する
							prog_last = -1;	// 経過表示がまだの印
							rv = check_ini_state(num, meta_data, hFile);
							if (rv == -2){	// 検査結果の記録が無ければ
								if ((num >= entity_num) || ((files[num].state & 0x80) != 0)){
									// non-recovery set のファイルまたはチェックサムが欠落したソース・ファイル
									rv = file_hash_check(num, FindData.cFileName, hFile, INT_MAX, files, NULL);
								} else {	// 普通のソース・ファイル
									if (files[num].state & 0x02){	// 本来のファイルが破損ならスライスの重複カウントを避ける
										rv = files[num].state >> 8;
									} else {
										rv = 0;
									}
									rv = file_hash_check(num, FindData.cFileName, hFile, rv, files, s_blk);
								}
								if (rv == -2){
									CloseHandle(hFile);
									FindClose(hFind);
									return 2;	// エラーは無視して続行するが、キャンセルなら中断する
								}
								// MD5-16k が一致した時だけ検査結果を記録する
								if (rv != -1)
									write_ini_state(num, meta_data, rv);
							}
							CloseHandle(hFile);
							if (rv == -3){	// ファイルのハッシュ値が一致した
								b_last = files[num].b_off + files[num].b_num;
								for (i = files[num].b_off; i < b_last; i++)
									s_blk[i].exist = 1;	// 全ブロックが有効と見なす
								files[num].name2 = list_len;	// 別名・移動のファイル名を記録する
								if (add_file_path(search_path + base_len)){
									printf("add_file_path\n");
									FindClose(hFind);
									return 1;
								}
								if (files[num].state & 0x01){	// 本来のファイルが消失してるなら
									files[num].state ^= 0x21;	// 消失して別名・移動 0x01 -> 0x20
									first_num += files[num].b_num;	// ブロック数を集計する
								} else {
									files[num].state ^= 0x2A;	// 破損して別名・移動 0x02 -> 0x28 (スライス個数は維持する)
									first_num += files[num].b_num - (files[num].state >> 8);	// 新たなブロック数だけ追加する
								}
								print_progress_file(-1, first_num, NULL);	// 重複を除外した利用可能なソース・ブロック数を表示する
								utf16_to_cp(search_path + base_len, ascii_buf, cp_output);	// 別名・移動のファイル名を表示する
								printf("%13I64d Found    : \"%s\"\n", file_size, ascii_buf);
								utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);	// 本来のファイル名を表示する
								if (compare_directory(list_buf + files[num].name, search_path + base_len) == 0){	// 同じ場所なら別名
									printf("            = Misnamed : \"%s\"\n", ascii_buf);
								} else {	// 別の場所なら移動
									printf("            = Moved    : \"%s\"\n", ascii_buf);
								}
								break;	// 見つかった場合は、それ以降のソース・ファイルと比較しない
							} else {
								if (rv > 0){	// 別名の破損ファイル内にスライスを見つけたら、自動的に検査対象に追加する
									//printf("rv = %d, %S\n", rv, search_path + base_len);
									if (list2_buf == NULL){
										list2_len = 0;
										list2_max = ALLOC_LEN;
										list2_buf = (wchar_t *)malloc(list2_max * 2);
									}
									if (list2_buf){
										wchar_t *tmp_p;
										int len = (int)wcslen(search_path);
										if (!search_file_path(list2_buf, list2_len, search_path)){	// ファイル名が重複しないようにする
											if (list2_len + len >= list2_max){ // 領域が足りなくなるなら拡張する
												list2_max += ALLOC_LEN;
												tmp_p = (wchar_t *)realloc(list2_buf, list2_max * 2);
												if (tmp_p == NULL){
													printf("realloc, %d\n", list2_max);
													FindClose(hFind);
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
									}
									if (prog_last >= 0)	// 途中までのスライス数を表示してた場合
										print_progress_file(-1, first_num, NULL);	// 元の数に戻しておく
								}
								print_progress_done();	// 経過表示があれば 100% にしておく
							}
						}
					} else {	// サイズが 0 (空ファイル) の場合
						if (files[num].state & 64)
							continue;	// 比較対象がフォルダなら無視する (ファイル名の比較ではねられるから不要？)
						// ファイル名が同じかどうか確かめる
						if (_wcsicmp(FindData.cFileName, offset_file_name(list_buf + files[num].name)) == 0){
							// 移動されたファイルが見つかった
							files[num].name2 = list_len;	// 移動されたファイル名を記録する
							if (add_file_path(search_path + base_len)){
								printf("add_file_path\n");
								FindClose(hFind);
								return 1;
							}
							utf16_to_cp(search_path + base_len, ascii_buf, cp_output);	// 移動されたファイル名を表示する
							printf("            0 Found    : \"%s\"\n", ascii_buf);
							files[num].state = 0x20;	// 空ファイルは破損しないので、消失して移動のみ
							utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);	// 本来のファイル名を表示する
							printf("            = Moved    : \"%s\"\n", ascii_buf);
							break;	// 見つかった場合は、それ以降のソース・ファイルと比較しない
						}
					}
				}
			}
		} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
	}
	FindClose(hFind);

	// 指定されたディレクトリ以下のフォルダ内も再帰的に調べる
	wcscpy(search_path + dir_len, L"*");
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind != INVALID_HANDLE_VALUE){
		do {
			// フォルダ以外は無視する
			if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				continue;
			// 親ディレクトリは無視する
			if ((wcscmp(FindData.cFileName, L".") == 0) || (wcscmp(FindData.cFileName, L"..") == 0))
				continue;
			// 発見したフォルダ名が長すぎる場合は無視する
			if (dir_len + wcslen(FindData.cFileName) + 2 >= MAX_LEN)	// 末尾に「\*」が追加される
				continue;
			// そのフォルダの内部を新たな検索対象にする
			wcscpy(search_path + dir_len, FindData.cFileName);
			wcscat(search_path + dir_len, L"\\");	// ディレクトリ記号は「\」に統一してある
			//printf_cp("inner search... %s\n", search_path + base_len);
			rv = recursive_search(ascii_buf, search_path, (int)wcslen(search_path), files, s_blk);
			if (rv != 0){
				// エラー発生、またはキャンセルされた
				FindClose(hFind);
				return rv;
			}
		} while (FindNextFile(hFind, &FindData));	// 次のフォルダを検索する
	}
	FindClose(hFind);

	return 0;
}

// フォルダを無視して、指定フォルダ内の別名ファイルを探す（サブ・フォルダに移動したファイルは検出不能）
// 0=正常終了, 1=エラー, 2=キャンセル
static int direct_search(
	char *ascii_buf,		// 作業用
	wchar_t *search_path,	// 検索するファイルの親ディレクトリ、見つけた別名ファイルの相対パスが戻る
	int dir_len,			// ディレクトリ部分の長さ
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	__int64 file_size;	// 存在するファイルのサイズは本来のサイズとは異なることもある
	int rv, i, num, b_last, source_len;
	unsigned int meta_data[7];
	HANDLE hFile, hFind;
	WIN32_FIND_DATA FindData;

	// ソース・ファイル名の領域までしか比較しない (別名ファイルが検出されるのは一回だけ)
	source_len = list_len - 1;

	// 指定されたディレクトリ直下のファイルとだけ比較する
	wcscpy(search_path + dir_len, L"*");
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind != INVALID_HANDLE_VALUE){
		do {
			if (cancel_progress() != 0){	// キャンセル処理
				FindClose(hFind);
				return 2;
			}
			if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)	// フォルダなら無視する
				continue;

			// 発見したファイル名が長すぎる場合は無視する
			if (dir_len + wcslen(FindData.cFileName) >= MAX_LEN)
				continue;
			// 現在のディレクトリ部分に見つかったファイル名を連結する
			wcscpy(search_path + dir_len, FindData.cFileName);
			// 破損してないリカバリ・ファイルは無視する
			if (search_file_path(recv_buf, recv_len, search_path))	// フル・パスで比較する
				continue;
			// 他のソース・ファイルは無視する
			if (search_file_path(list_buf + 1, source_len, search_path + base_len))	// 基準ディレクトリからの相対パスで比較する
				continue;

			// ファイル・サイズが検出対象のサイズと一致するかどうか
			file_size = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
			for (num = 0; num < file_num; num++){
				if ((files[num].state & 0x03) && (files[num].size == file_size)){
					if (file_size > 0){	// サイズが 0 (空ファイル) でなければ
						//printf("find size %I64d, %S\n", file_size, search_path + base_len);
						// 別名ファイルのハッシュ値を調べる
						hFile = CreateFile(search_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED, NULL);
						if (hFile != INVALID_HANDLE_VALUE){	// ファイルを開けたなら内容を確認する
							prog_last = -1;	// 経過表示がまだの印
							rv = check_ini_state(num, meta_data, hFile);
							if (rv == -2){	// 検査結果の記録が無ければ
								if ((num >= entity_num) || ((files[num].state & 0x80) != 0)){
									// non-recovery set のファイルまたはチェックサムが欠落したソース・ファイル
									rv = file_hash_check(num, FindData.cFileName, hFile, INT_MAX, files, NULL);
								} else {	// 普通のソース・ファイル
									if (files[num].state & 0x02){	// 本来のファイルが破損ならスライスの重複カウントを避ける
										rv = files[num].state >> 8;
									} else {
										rv = 0;
									}
									rv = file_hash_check(num, FindData.cFileName, hFile, rv, files, s_blk);
								}
								if (rv == -2){
									CloseHandle(hFile);
									FindClose(hFind);
									return 2;	// エラーは無視して続行するが、キャンセルなら中断する
								}
								// MD5-16k が一致した時だけ検査結果を記録する
								if (rv != -1)
									write_ini_state(num, meta_data, rv);
							}
							CloseHandle(hFile);
							if (rv == -3){	// ファイルのハッシュ値が一致した
								b_last = files[num].b_off + files[num].b_num;
								for (i = files[num].b_off; i < b_last; i++)
									s_blk[i].exist = 1;	// 全ブロックが有効と見なす
								files[num].name2 = list_len;	// 別名・移動のファイル名を記録する
								if (add_file_path(search_path + base_len)){
									printf("add_file_path\n");
									FindClose(hFind);
									return 1;
								}
								if (files[num].state & 0x01){	// 本来のファイルが消失してるなら
									files[num].state ^= 0x21;	// 消失して別名・移動 0x01 -> 0x20
									first_num += files[num].b_num;	// ブロック数を集計する
								} else {
									files[num].state ^= 0x2A;	// 破損して別名・移動 0x02 -> 0x28 (スライス個数は維持する)
									first_num += files[num].b_num - (files[num].state >> 8);	// 新たなブロック数だけ追加する
								}
								print_progress_file(-1, first_num, NULL);	// 重複を除外した利用可能なソース・ブロック数を表示する
								utf16_to_cp(search_path + base_len, ascii_buf, cp_output);	// 別名・移動のファイル名を表示する
								printf("%13I64d Found    : \"%s\"\n", file_size, ascii_buf);
								utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);	// 本来のファイル名を表示する
								if (compare_directory(list_buf + files[num].name, search_path + base_len) == 0){	// 同じ場所なら別名
									printf("            = Misnamed : \"%s\"\n", ascii_buf);
								} else {	// 別の場所なら移動
									printf("            = Moved    : \"%s\"\n", ascii_buf);
								}
								break;	// 見つかった場合は、それ以降のソース・ファイルと比較しない
							} else {
								if (rv > 0){	// 別名の破損ファイル内にスライスを見つけたら、自動的に検査対象に追加する
									//printf("rv = %d, %S\n", rv, search_path + base_len);
									if (list2_buf == NULL){
										list2_len = 0;
										list2_max = ALLOC_LEN;
										list2_buf = (wchar_t *)malloc(list2_max * 2);
									}
									if (list2_buf){
										wchar_t *tmp_p;
										int len = (int)wcslen(search_path);
										if (!search_file_path(list2_buf, list2_len, search_path)){	// ファイル名が重複しないようにする
											if (list2_len + len >= list2_max){ // 領域が足りなくなるなら拡張する
												list2_max += ALLOC_LEN;
												tmp_p = (wchar_t *)realloc(list2_buf, list2_max * 2);
												if (tmp_p == NULL){
													printf("realloc, %d\n", list2_max);
													FindClose(hFind);
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
									}
									if (prog_last >= 0)	// 途中までのスライス数を表示してた場合
										print_progress_file(-1, first_num, NULL);	// 元の数に戻しておく
								}
								print_progress_done();	// 経過表示があれば 100% にしておく
							}
						}
					}
				}
			}
		} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
	}
	FindClose(hFind);

	return 0;
}

// ソース・ファイルが不完全なら別名・移動ファイルを探す
// 0=完了, 1=エラー, 2=キャンセル
int search_misnamed_file(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	wchar_t find_path[MAX_LEN];
	int i, rv, num, b_last;

	// 順列検査でファイルが破損してれば上書修復する（作業ファイルを作らない）
	if (switch_v & 4){
		for (num = 0; num < entity_num; num++){
			if (files[num].state & 2)
				files[num].state |= 4;
		}
	}

	// 不完全なソース・ファイルが存在するか調べる
	i = 0;		// 消失 0x01, 0x81, フォルダの消失 0x41 も
	rv = 0;		// 破損 0x02, 0x82, 追加 0x10 も
	b_last = 0;	// 消失や破損なら別名ファイルを探す
	for (num = 0; num < file_num; num++){
		if (files[num].state & 0x01)
			i++;	// 消失したファイルの数
		if (files[num].state & 0x12)
			rv++;	// 破損・追加のファイルの数
		if (files[num].state & 0x03)	// 消失 0x01、破損 0x02 なら別名ファイルを探す
			b_last++;	// 探す必要があるファイルの数
	}
	printf("\nComplete file count\t: %d\n", file_num - i - rv);
	if (b_last == 0){	// 探さない場合はファイル数の集計だけ
		printf("Misnamed file count\t: %d\n", 0);
		printf("Damaged file count\t: %d\n", rv);
		printf("Missing file count\t: %d\n", i);
		return 0;	// 探さないで終わる
	}

	// 検査したファイルごとに結果を表示する
	printf("\nSearching misnamed file: %d\n", b_last);	// 探す対象のファイル数
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	wcscpy(find_path, base_dir);
	if (switch_v & 8){	// 基準ディレクトリ直下のファイルだけを検査する
		if (rv = direct_search(ascii_buf, find_path, base_len, files, s_blk))
			return rv;
	} else {	// 基準ディレクトリ以下を再帰的に検索する
		if (rv = recursive_search(ascii_buf, find_path, base_len, files, s_blk))
			return rv;
	}

	// 状態ごとにファイル数を集計する
	i = 0;		// 消失 0x01, 0x81, フォルダの消失 0x41 も
	rv = 0;		// 破損 0x02, 0x82, 追加 0x10 も
	b_last = 0;	// 消失して別名・移動 0x20, 0xA0, 破損して別名・移動 0x28, 0xA8, フォルダの移動 0x60
	for (num = 0; num < file_num; num++){
		if (files[num].state & 0x01)
			i++;	// 消失したファイルの数
		if (files[num].state & 0x12)
			rv++;	// 破損・追加のファイルの数
		if (files[num].state & 0x20)
			b_last++;	// 別名・移動の数
	}
	printf("\n");
	printf("Misnamed file count\t: %d\n", b_last);
	printf("Damaged file count\t: %d\n", rv);
	printf("Missing file count\t: %d\n", i);
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 破損・分割・類似名のファイルから使えるスライスを探す
// 0=完了, 1=エラー, 2=キャンセル
int search_file_slice(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int rv, num;
	slice_ctx sc[1];

	printf("\nInput File Slice found\t: %d\n", first_num);
	fflush(stdout);
	if (first_num > source_num)
		return 1;	// 検出数が多すぎるのはどこか間違ってる
	if ((first_num == source_num) || (block_size <= 4))
		return 0;	// 探さないで終わる

	// スライス検査の準備をする
	memset(sc, 0, sizeof(slice_ctx));
	rv = init_verification(files, s_blk, sc);
	if (rv < 0)	// 致命的なエラーまたはスライス検査不要
		return 0;
	if (rv == 1)	// スライド検査不可あるいは不要
		switch_v |= 1;	// スライド検査しない場合は簡易検査にする
	//printf("\n init_verification = %d, switch_v = %d\n", rv, switch_v);

	// 検査したファイルごとに結果を表示する
	printf("\nFinding available slice:\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);

	// 破損してるファイルと分割・類似名ファイルを検査する
	for (num = 0; num < entity_num; num++){
		// 破損 0x02、追加 0x10、破損して別名 0x28 ならソース・ファイルを検査する
		// 消失 0x01、破損 0x02 で、そのファイルのスライスが未検出なら、分割ファイルを探す
		// 消失で分割ファイルが見つからない場合だけ、文字化けファイルも探す
		if ((files[num].state & 0x1B) == 0)
			continue;

		if (rv = cancel_progress())	// キャンセル処理
			goto error_end;

		if (rv = check_file_slice(ascii_buf, file_path, num, files, s_blk, sc))
			goto error_end;
	}

	// 指定された外部ファイルを検査する
	if ((first_num < source_num) && (list2_buf != NULL) && ((switch_v & 4) == 0)){
		if (rv = check_external_file(ascii_buf, file_path, files, s_blk, sc))
			goto error_end;
	}

	// 簡易検査と順列検査以外なら、そのファイルのスライスが全て検出されていても、分割ファイルを探す
	num = 0;
	if (switch_v & (1 | 4))
		num = entity_num;
	for (; num < entity_num; num++){
		if (first_num >= source_num)
			break;

		if (rv = cancel_progress())	// キャンセル処理
			goto error_end;

		if (rv = search_file_split(ascii_buf, file_path, num, files, s_blk, sc))
			goto error_end;
	}

	// 基準ディレクトリ内のその他のファイルを検査する
	if ((first_num < source_num) && ((switch_v & 4) == 0)){
		if (rv = search_additional_file(ascii_buf, file_path, files, s_blk, sc))
			goto error_end;
	}

	// 見つかったソース・ブロックの数
	printf("\nInput File Slice found\t: %d\n", first_num);
	fflush(stdout);
	if (first_num > source_num){
		rv = 1;	// 検出数が多すぎるのはどこか間違ってる
	} else {
		rv = 0;
	}
	json_save_found();

error_end:
	if (sc->buf){
		if (sc->h){	// サブ・スレッドを終了させる
			sc->size = 0;	// 終了指示
			SetEvent(sc->run);
			WaitForSingleObject(sc->h, INFINITE);
			CloseHandle(sc->h);
		}
		free(sc->buf);
	}
	if (sc->order)
		free(sc->order);
	if (sc->hFile_tmp)
		CloseHandle(sc->hFile_tmp);
	if (sc->flk_buf)
		free(sc->flk_buf);

	return rv;
}

