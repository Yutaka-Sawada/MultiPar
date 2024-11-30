// repair.c
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
#include "crc.h"
#include "md5_crc.h"
#include "ini.h"
#include "json.h"
#include "repair.h"


// ファイル・リストを表示する
void print_file_list(
	char *ascii_buf,	// 作業用
	file_ctx_r *files)
{
	int i;

	printf("\nInput File list      :\n");
	printf("         Size  Slice :  Filename\n");
	fflush(stdout);
	for (i = 0; i < file_num; i++){
		if (files[i].name < 0){	// File Description packet が欠落してる
			printf("            ?      ? : Unknown\n");
		} else {
			if (files[i].name == 0){	// ファイル名だけ不明
				ascii_buf[0] = 0;
			} else {
				utf16_to_cp(list_buf + files[i].name, ascii_buf, cp_output);
			}
			// ファイルごとのブロック数と開始番号
			if ((i < entity_num) && (files[i].size > 0)){
				printf("%13I64d %6d : \"%s\"\n", files[i].size, files[i].b_num, ascii_buf);
			} else {	// 空のファイルやフォルダ、または non recovery set
				printf("%13I64d      0 : \"%s\"\n", files[i].size, ascii_buf);
			}
		}
	}
	printf("\nInput File total size\t: %I64d\n", total_file_size);
	printf("Input File Slice count\t: %d\n", source_num);
}

// ディレクトリ記号までの長さを返す (存在しない場合は -1)
static int wcschr_dir(wchar_t *s)
{
	int i = 0;

	while (s[i] != 0){
		if (s[i] == '\\')
			return i;
		i++;
	}

	return -1;
}

// ファイル名を比較する
static int name_cmp(const void *elem1, const void *elem2)
{
	wchar_t *name1, *name2;
	int rv = 2, len1, len2;

	name1 = list_buf + ((file_ctx_r *)elem1)->name;
	name2 = list_buf + ((file_ctx_r *)elem2)->name;

	while (name1[0] + name2[0] != 0){
		len1 = wcschr_dir(name1);
		len2 = wcschr_dir(name2);

		// フォルダを先にする
		if (len1 >= 0){
			if (len2 < 0)	// file1 だけサブ・ディレクトリがある
				return -1;
		} else if (len2 >= 0){	// file2 だけサブ・ディレクトリがある
			return 1;
		}

		// ユーザーの言語設定によって順序を決める
		//rv = CompareString(LOCALE_USER_DEFAULT, 0, name1, len1, name2, len2);
		rv = CompareStringEx(LOCALE_NAME_USER_DEFAULT, 0x00000008, name1, len1, name2, len2, NULL, NULL, 0);
		if ((rv == 2) && (len1 != -1) && (len2 != -1)){
			name1 += len1 + 1;
			name2 += len2 + 1;
		} else {
			break;
		}
	}

	return rv - 2;
//	return wcscmp(name1, name2);	// QuickPar はこちらの順序
}

// ソース・ファイル情報を確認して集計する
int set_file_data(
	char *ascii_buf,	// 作業用
	file_ctx_r *files)
{
	int i, bad_flag;

	// ソース・ファイルの基本情報
	bad_flag = 0;
	total_file_size = 0;
	source_num = 0;
	for (i = 0; i < file_num; i++){
		if (files[i].name < 0){	// File Description packet が欠落してる
			bad_flag |= 1;
		} else {
			if (files[i].name == 0){	// ファイル名だけ不明
				bad_flag |= 2;
			} else {
				if (base_len + wcslen(list_buf + files[i].name) >= MAX_LEN - ADD_LEN)
					bad_flag |= 4;	// ファイル・パスが長過ぎる
			}
			// ファイルごとのブロック数と開始番号
			if ((i < entity_num) && (files[i].size > 0)){
				files[i].b_off = source_num;
				files[i].b_num = (int)((files[i].size + (__int64)block_size - 1) / (__int64)block_size);
				source_num += files[i].b_num;
				files[i].state = 0x80;	// チェックサムが必要だがまだ読み取ってない
			} else {	// 空のファイルやフォルダ、または non recovery set
				files[i].b_off = 0;
				files[i].b_num = 0;
				files[i].state = 0;	// チェックサムは必要ない
			}
			total_file_size += files[i].size;
		}
	}
	if (bad_flag){
		print_file_list(ascii_buf, files);	// 終了前にファイル・リストを表示する
		if (bad_flag & 1){	// File Description packet が不足してると検査を継続できない
			printf("\nFile Description packet is missing\n");
		} else if (bad_flag & 2){
			printf("\nfilename is unknown\n");
		} else if (bad_flag & 4){
			printf("\nfilename is too long\n");
		}
		return 1;
	}

	// PAR2 仕様ではソース・ブロックの最大値は 32768 個だが、
	// 規格外のリカバリ・データがあるかもしれない (YencPowerPost A&A v11b のバグ)
	if (source_num > MAX_SOURCE_NUM){
		parity_num = 0;	// 規格外ならリカバリ・ブロックを無効にする
	} else {
		// リカバリ・ブロック自体はいくらでも作れるが、異なるスライスは 65535個まで
		parity_num = MAX_PARITY_NUM;
	}

	// If you want to see original order (sorted by File ID), comment out below lines.
	// ファイルを並び替えたら、ソース・ブロックのファイル番号もそれに応じて変えること
	// recovery set のファイルをファイル名の順に並び替える
	if (entity_num > 1)
		qsort(files, entity_num, sizeof(file_ctx_r), name_cmp);
	// non-recovery set のファイルをファイル名の順に並び替える
	if (file_num - entity_num > 1)
		qsort(&(files[entity_num]), file_num - entity_num, sizeof(file_ctx_r), name_cmp);

	print_file_list(ascii_buf, files);	// 並び替えられたファイル・リストを表示する

	return 0;
}

// ソース・ファイルの検査結果を集計して、修復方法を判定する
int result_file_state(
	char *ascii_buf,
	int *result,
	int parity_now,
	int recovery_lost,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, num, find_num, b_last;
	int lost_num, need_repair, rejoin_num, repair_num, incomp_num;

	find_num = -1;	// 項目が未表示の印
	need_repair = 0;
	rejoin_num = 0;
	repair_num = 0;
	incomp_num = 0;
	for (num = 0; num < entity_num; num++){
		if (files[num].size == 0){	// フォルダまたは空ファイル
			if (files[num].state & 0x3F)	// 空ファイルとフォルダが存在する (0x40) 以外なら
				need_repair++;
		} else {	// ソース・ファイル
			if (files[num].state & 0x03){	// 消失(0x01)、破損(0x02) ならスライス検出結果を表示する
				if (find_num < 0){	// 項目表示がまだなら
					printf("\nCounting available slice:\n");
					printf(" Avail /  Slice :  Filename\n");	// 集計された検出スライス数
				}
				utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
				find_num = 0;
				if ((files[num].state & 0x80) == 0){	// チェックサムが存在するなら
					b_last = files[num].b_off + files[num].b_num;
					for (i = files[num].b_off; i < b_last; i++){
						if (s_blk[i].exist != 0)
							find_num++;
					}
				}
				printf("%6d / %6d : \"%s\"\n", find_num, files[num].b_num, ascii_buf);
				if (find_num == files[num].b_num){
					need_repair |= 0x20000000;	// スライスが揃ってるのでファイルを再構築できる
					rejoin_num++;
				} else {
					need_repair |= 0x10000000;	// スライスが足りない
					repair_num++;
				}
			} else if (files[num].state & 0x30){	// 追加(0x10)、別名・移動(0x20, 0x28)
				need_repair++;
			}
		}
	}
	for (num = entity_num; num < file_num; num++){
		if (files[num].state & 0x30){	// 追加(0x10)、別名・移動(0x20)、フォルダが移動(0x60)
			need_repair++;
		} else if (files[num].state & 0x03){	// 消失(0x01)、破損(0x02)、フォルダが消失(0x41)
			if (files[num].size == 0){	// サイズが 0ならすぐに復元できる
				need_repair++;
			} else {
				need_repair |= 0x40000000;	// non-recovery set のファイルが破損して修復できない
				incomp_num++;
			}
		}
	}
	json_file_state(files);	// JSONファイルに記録する

	// 利用可能なソース・ブロックの数
	printf("\nInput File Slice avail\t: %d\n", first_num);
	// 消失したソース・ブロックの数
	lost_num = source_num - first_num;
	printf("Input File Slice lost\t: %d\n\n", lost_num);
	if (need_repair == 0)
		printf("All Files Complete\n");
	if (recovery_lost > 0){	// 不完全なリカバリ・ファイルがあるなら
		i = 256;
		printf("%d PAR File(s) Incomplete\n", recovery_lost);
	} else {
		i = 0;
	}

	// 修復する必要があるかどうか
	if (need_repair != 0){
		i |= 4;
		if (need_repair & 0x0FFFFFFF){	// 簡易修復は可能
			printf("Ready to rename %d file(s)\n", need_repair & 0x0FFFFFFF);
			i |= 32;
		}
		if (need_repair & 0x20000000){	// 再構築までは可能
			printf("Ready to rejoin %d file(s)\n", rejoin_num);
			i |= 64;
		}
		if (need_repair & 0x10000000){	// ソース・ブロックの復元が必要
			if (lost_num > parity_now){
				printf("Need %d more slice(s) to repair %d file(s)\n", lost_num - parity_now, repair_num);
				i |= 8;
			} else if ((lost_num == parity_now) && (lost_num >= 2)){	// 逆行列の計算で失敗するかも
				printf("Try to repair %d file(s)\n", repair_num);
				i |= 128 | 8;
			} else {
				printf("Ready to repair %d file(s)\n", repair_num);
				i |= 128;
			}
		}
		if (need_repair & 0x40000000)	// non-recovery set のファイルは修復できない
			printf("Cannot repair %d file(s)\n", incomp_num);
	}
	fflush(stdout);

	*result = need_repair;
	return i;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 簡単な修復を行う、まだ修復の必要なファイルの数を戻す
int simple_repair(
	char *ascii_buf,
	int need_repair,
	file_ctx_r *files)		// 各ソース・ファイルの情報
{
	wchar_t file_path[MAX_LEN], old_path[MAX_LEN];
	int i, num, repaired_num;
	HANDLE hFile;

	if (need_repair){	// 簡単な修復だけでいいファイルの数
		printf("\nCorrecting file : %d\n", need_repair);
		printf(" Status   :  Filename\n");
		fflush(stdout);
		need_repair = 0x10000000;
	}
	repaired_num = 0;
	wcscpy(file_path, base_dir);
	wcscpy(old_path, base_dir);

	// recovery set のファイル
	for (num = 0; num < entity_num; num++){
		utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
		wcscpy(file_path + base_len, list_buf + files[num].name);
		if (files[num].size == 0){	// フォルダまたは空ファイルを作り直す
			switch (files[num].state){
			case 1:		// 存在しなくてもサイズが 0ならすぐに復元できる
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if ((hFile == INVALID_HANDLE_VALUE) && (GetLastError() == ERROR_PATH_NOT_FOUND)){	// Path not found (3)
					make_dir(file_path);	// 途中のフォルダが存在しないのなら作成する
					hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				}
				if (hFile != INVALID_HANDLE_VALUE){
					CloseHandle(hFile);
					files[num].state = 0;
					printf(" Restored : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {
					printf(" Failed   : \"%s\"\n", ascii_buf);
				}
				fflush(stdout);
				break;
			case 16:	// ファイルに内容がある場合は破損ではなく追加と見なす
				if (shorten_file(file_path, 0) == 0){
					files[num].state = 0;
					printf(" Restored : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {
					printf(" Failed   : \"%s\"\n", ascii_buf);
				}
				fflush(stdout);
				break;
			case 32:	// 本来の場所に戻す
			case 96:	// フォルダを本来の場所に戻す
				wcscpy(old_path + base_len, list_buf + files[num].name2);
				if (replace_file(file_path, old_path) == 0){
					files[num].state &= 0x40;	// 0 か 64 になる
					printf(" Restored : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {	// ファイル名の変更に失敗した
					printf(" Failed   : \"%s\"\n", ascii_buf);
				}
				fflush(stdout);
				break;
			case 65:	// フォルダが消失
				i = (int)wcslen(file_path);
				if (file_path[i - 1] == '\\')
					file_path[i - 1] = 0;
				if (CreateDirectory(file_path, NULL) == 0){
					i = GetLastError();
					if (i == ERROR_PATH_NOT_FOUND){	// Path not found (3)
						make_dir(file_path);	// 途中のフォルダが存在しないのなら作成する
						i = CreateDirectory(file_path, NULL);
					} else if (i == ERROR_ALREADY_EXISTS){	// Destination file is already exist (183)
						if ((GetFileAttributes(file_path) & FILE_ATTRIBUTE_DIRECTORY) == 0){
							// 同名のファイルが存在するならどかす
							move_away_file(file_path);
							i = CreateDirectory(file_path, NULL);
						}
					}
				}
				if (i != 0){
					files[num].state = 64;
					printf(" Restored : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {
					printf(" Failed   : \"%s\"\n", ascii_buf);
				}
				fflush(stdout);
				break;
			}
		} else {
			switch (files[num].state & 0x7F){	// チェックサムの有無に関係なく訂正できる
			case 1:		// 後で失われたブロックを復元する
			case 2:
			case 6:
				need_repair++;
				break;
			case 16:	// 末尾のゴミを取り除く
				if (shorten_file(file_path, files[num].size) == 0){
					write_ini_complete(num, file_path);
					files[num].state &= 0x80;
					printf(" Restored : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {
					printf(" Failed   : \"%s\"\n", ascii_buf);
				}
				fflush(stdout);
				break;
			case 32:	// ファイル名を訂正する
			case 40:
				wcscpy(old_path + base_len, list_buf + files[num].name2);
				if (replace_file(file_path, old_path) == 0){
					files[num].state &= 0x80;
					printf(" Restored : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {	// ファイル名の変更に失敗した
					printf(" Failed   : \"%s\"\n", ascii_buf);
				}
				fflush(stdout);
				break;
			}
		}
	}

	// non-recovery set のファイル
	for (num = entity_num; num < file_num; num++){
		utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
		wcscpy(file_path + base_len, list_buf + files[num].name);
		switch (files[num].state){
		case 1:		// 消失
		case 2:		// 破損
			if (files[num].size == 0){	// サイズが 0ならすぐに復元できる
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if ((hFile == INVALID_HANDLE_VALUE) && (GetLastError() == ERROR_PATH_NOT_FOUND)){	// Path not found (3)
					make_dir(file_path);	// 途中のフォルダが存在しないのなら作成する
					hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				}
				if (hFile != INVALID_HANDLE_VALUE){
					CloseHandle(hFile);
					files[num].state = 0;
					printf(" Restored : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {
					printf(" Failed   : \"%s\"\n", ascii_buf);
				}
				fflush(stdout);
			}
			break;
		case 16:	// 追加
			if (shorten_file(file_path, files[num].size) == 0){
				files[num].state = 0;
				printf(" Restored : \"%s\"\n", ascii_buf);
				repaired_num++;
			} else {
				printf(" Failed   : \"%s\"\n", ascii_buf);
			}
			fflush(stdout);
			break;
		case 32:	// 本来の場所に戻す、またはファイル名を訂正する
		case 40:
		case 96:	// フォルダを本来の場所に戻す
			wcscpy(old_path + base_len, list_buf + files[num].name2);
			if (replace_file(file_path, old_path) == 0){
				files[num].state &= 0x40;	// 0 か 64 になる
				printf(" Restored : \"%s\"\n", ascii_buf);
				repaired_num++;
			} else {	// ファイル名の変更に失敗した
				printf(" Failed   : \"%s\"\n", ascii_buf);
			}
			fflush(stdout);
			break;
		case 65:	// フォルダが消失
			i = (int)wcslen(file_path);
			if (file_path[i - 1] == '\\')
				file_path[i - 1] = 0;
			if (CreateDirectory(file_path, NULL) == 0){
				i = GetLastError();
				if (i == ERROR_PATH_NOT_FOUND){	// Path not found (3)
					make_dir(file_path);	// 途中のフォルダが存在しないのなら作成する
					i = CreateDirectory(file_path, NULL);
				} else if (i == ERROR_ALREADY_EXISTS){	// Destination file is already exist (183)
					if ((GetFileAttributes(file_path) & FILE_ATTRIBUTE_DIRECTORY) == 0){
						// 同名のファイルが存在するならどかす
						move_away_file(file_path);
						i = CreateDirectory(file_path, NULL);
					}
				}
			}
			if (i != 0){
				files[num].state = 64;
				printf(" Restored : \"%s\"\n", ascii_buf);
				repaired_num++;
			} else {
				printf(" Failed   : \"%s\"\n", ascii_buf);
			}
			fflush(stdout);
			break;
		}
	}

	if (need_repair & 0x10000000)	// 修復できたファイルの数
		printf("\nRestored file count\t: %d\n", repaired_num);

	return need_repair & 0x0FFFFFFF;
}

// 4バイトのソース・ブロックを逆算してソース・ファイルに書き込む
int restore_block4(
	wchar_t *file_path,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, j, num, b_last;
	unsigned int data;
	unsigned int time_last = 0;
	HANDLE hFile;

	print_progress_text(0, "Restoring slice");
	wcscpy(file_path, base_dir);
	for (num = 0; num < entity_num; num++){
		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress((num * 1000) / entity_num))
				return 2;
			time_last = GetTickCount();
		}

		if ((files[num].size > 0) && ((files[num].state & 0x80) == 0) &&
				((files[num].state & 3) != 0)){	// 不完全なファイルにチェックサムが存在するなら
			//printf("file %d, 0x%08x\n", num, files[num].state);
			if (files[num].state & 4){	// 破損ファイルを上書きして復元する場合
				// ソース・ファイルを作り直す（元のデータは全て消える）
				wcscpy(file_path + base_len, list_buf + files[num].name);
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
			} else {
				// 作業ファイルを開く
				hFile = handle_temp_file(list_buf + files[num].name, file_path);
			}
			if (hFile == INVALID_HANDLE_VALUE)
				return 1;

			// 逆算したソース・ブロックを書き込んでいく
			b_last = files[num].b_off + files[num].b_num;
			for (i = files[num].b_off; i < b_last; i++){
				data = crc_reverse_zero(s_blk[i].crc, 4);	// CRC-32 からブロック内容を逆算する
				if (!WriteFile(hFile, &data, s_blk[i].size, &j, NULL)){
					print_win32_err();
					CloseHandle(hFile);
					return 1;
				}
			}
			CloseHandle(hFile);
		}
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	return 0;
}

// 同じ内容のソース・ブロックを流用する、または内容がわかるブロックは逆算する
int restore_block(
	wchar_t *file_path,
	int reuse_num,			// 流用可能なソース・ブロックの数
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, num, src_blk, src_file;
	unsigned int data;
	unsigned int time_last = 0, prog_num = 0;
	__int64 file_off;
	HANDLE hFile, hFile_src;

	print_progress_text(0, "Restoring slice");
	wcscpy(file_path, base_dir);
	for (num = 0; num < entity_num; num++){
		if ((files[num].size > 0) && ((files[num].state & 0x80) == 0) &&
				((files[num].state & 3) != 0)){	// チェックサムと作業ファイルが存在するなら
			hFile = NULL;

			// 利用可能なソース・ブロックをコピーしていく
			i = files[num].b_off;
			for (file_off = 0; file_off < files[num].size; file_off += block_size){
				if ((s_blk[i].exist >= 3) && (s_blk[i].exist <= 5)){
					if (hFile == NULL){	// 書き込み先ファイルがまだ開かれてなければ
						if (files[num].state & 4){	// 破損ファイルを上書きして復元する場合
							// 上書き用のソース・ファイルを開く
							hFile = handle_write_file(list_buf + files[num].name, file_path, files[num].size);
						} else {
							// 作業ファイルを開く
							hFile = handle_temp_file(list_buf + files[num].name, file_path);
						}
						if (hFile == INVALID_HANDLE_VALUE)
							return 1;
					}

					switch (s_blk[i].exist){
					case 3:	// 内容が全て 0 のブロック
						// 0 で埋める
						if (file_fill_data(hFile, file_off, 0, s_blk[i].size)){
							CloseHandle(hFile);
							printf("file_fill_data, %d\n", i);
							return 1;
						}
						break;
					case 4:	// 同じファイル、または別のファイルに存在する同じブロック
						src_blk = s_blk[i].file;	// s_blk[i].file にはそのブロック番号が入ってる
						src_file = s_blk[src_blk].file;
/*						printf("copy block : off 0x%I64X block %d -> off 0x%I64X block %d\n",
							(__int64)(src_blk - files[src_file].b_off) * (__int64)block_size,
							src_blk, file_off, i);*/
						if (files[src_file].state & 7){	// 読み込み元が消失・破損ファイルなら
							if (files[src_file].state & 4){	// 上書き中の破損ファイルから読み込む
								wcscpy(file_path + base_len, list_buf + files[src_file].name);
							} else {	// 作り直した作業ファイルから読み込む
								get_temp_name(list_buf + files[src_file].name, file_path + base_len);
							}
							hFile_src = CreateFile(file_path, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
						} else {
							if (files[src_file].state & 0x20){	// 名前訂正失敗時には別名ファイルから読み込む
								wcscpy(file_path + base_len, list_buf + files[src_file].name2);
							} else {	// 完全なソース・ファイルから読み込む (追加訂正失敗時も)
								wcscpy(file_path + base_len, list_buf + files[src_file].name);
							}
							hFile_src = CreateFile(file_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
						}
						if (hFile_src == INVALID_HANDLE_VALUE){
							print_win32_err();
							CloseHandle(hFile);
							printf_cp("cannot open file, %s\n", file_path);
							return 1;
						}
						// コピーする
						if (file_copy_data(hFile_src, (__int64)(src_blk - files[src_file].b_off) * (__int64)block_size,
								hFile, file_off, s_blk[i].size)){
							print_win32_err();
							CloseHandle(hFile);
							printf("file_copy_data, %d\n", i);
							return 1;
						}
						CloseHandle(hFile_src);
						break;
					case 5:	// 内容を逆算することができるブロック
						data = crc_reverse_zero(s_blk[i].crc, block_size);	// CRC-32 からブロック内容を逆算する
						if (file_write_data(hFile, file_off, (unsigned char *)(&data), s_blk[i].size)){
							CloseHandle(hFile);
							printf("file_write_data, %d\n", i);
							return 1;
						}
						break;
					}
					s_blk[i].file = num;

					// 経過表示
					prog_num++;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress((prog_num * 1000) / reuse_num))
							return 2;
						time_last = GetTickCount();
					}
				}
				i++;
			}
			if (hFile)
				CloseHandle(hFile);
		}
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	return 0;
}

// 正しく修復できたか調べて結果表示する
int verify_repair(
	wchar_t *file_path,
	char *ascii_buf,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	wchar_t temp_path[MAX_LEN];
	int i, num, b_last, bad_flag, repaired_num;

	repaired_num = 0;
	wcscpy(file_path, base_dir);
	for (num = 0; num < entity_num; num++){
		if (files[num].size == 0)
			continue;	// 空ファイルは検証しない

		if (files[num].state & 4){	// 破損ファイルを上書きして修復したなら
			bad_flag = 0;
			// 再度開きなおす
			wcscpy(file_path + base_len, list_buf + files[num].name);
			if (files[num].state & 0x80){	// チェックサムが欠落したソース・ファイル
				i = file_hash_direct(num, file_path, list_buf + files[num].name, files, NULL);
			} else {
				i = file_hash_direct(num, file_path, list_buf + files[num].name, files, s_blk);
			}
			if (i == -2)
				return 2;	// 確認中にキャンセルされた
			if (i == -4){
				bad_flag = 1;	// Missing
			} else if (i != -3){
				bad_flag = 2;	// Failed
			}

			// 結果を表示する
			utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
			if (bad_flag){	// 失敗
				printf(" Failed   : \"%s\"\n", ascii_buf);
			} else {	// 修復成功
				write_ini_complete(num, file_path);
				files[num].state &= 0x80;
				b_last = files[num].b_off + files[num].b_num;
				for (i = files[num].b_off; i < b_last; i++){
					if (s_blk[i].exist == 0)
						first_num++;	// 復元したブロック数
					s_blk[i].exist = 1;
				}
				printf(" Repaired : \"%s\"\n", ascii_buf);
				repaired_num++;
			}
			fflush(stdout);

		} else if (files[num].state & 3){	// 新しく作り直したソース・ファイルなら
			bad_flag = 0;
			// 再度開きなおす
			wcscpy(file_path + base_len, list_buf + files[num].name);
			get_temp_name(file_path, temp_path);
			if (files[num].state & 0x80){	// チェックサムが欠落したソース・ファイル
				i = file_hash_direct(num, temp_path, list_buf + files[num].name, files, NULL);
			} else {
				i = file_hash_direct(num, temp_path, list_buf + files[num].name, files, s_blk);
			}
			if (i == -2)
				return 2;	// 確認中にキャンセルされた
			if (i == -4){
				bad_flag = 1;	// Missing
			} else if (i != -3){
				bad_flag = 2;	// Failed
			}

			// 結果を表示する
			utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
			if (bad_flag){	// 失敗
				if (((files[num].state & 0x80) == 0) &&
						(bad_flag == 2) && ((switch_b & 4) != 0)){	// 修復に失敗した場合でも、元のファイルを置き換える
					// 完全なブロックが含まれてるかどうか
					b_last = files[num].b_off + files[num].b_num;
					for (i = files[num].b_off; i < b_last; i++){
						if (s_blk[i].exist != 0){
							i = -1;
							break;
						}
					}
					if (i < 0){	// 消失ファイルを代替し、破損ファイルを置き換える
						if (replace_file(file_path, temp_path) == 0){
							files[num].state = 2;	// 破損ファイルにする
							printf(" Replaced : \"%s\"\n", ascii_buf);
							fflush(stdout);
							continue;
						}
					}
				}
				printf(" Failed   : \"%s\"\n", ascii_buf);
			} else {	// 修復成功
				if (replace_file(file_path, temp_path) == 0){	// 修復したファイルを戻す
					write_ini_complete(num, file_path);
					files[num].state &= 0x80;
					b_last = files[num].b_off + files[num].b_num;
					for (i = files[num].b_off; i < b_last; i++){
						if (s_blk[i].exist == 0)
							first_num++;	// 復元したブロック数
						s_blk[i].exist = 1;
					}
					printf(" Repaired : \"%s\"\n", ascii_buf);
					repaired_num++;
				} else {	// ファイルを戻せなかった場合は、別名扱いにして修復したファイルを残す
					files[num].state = (files[num].state & 0x80) | 0x20;
					printf(" Locked   : \"%s\"\n", ascii_buf);
				}
			}
			fflush(stdout);
		}
	}

	// 修復できたファイルの数と修復後のブロック数
	printf("\nRepaired file count\t: %d\n", repaired_num);
	printf("Input File Slice avail\t: %d\n", first_num);

	return 0;
}

// 作業用のソース・ファイルを削除する
void delete_work_file(
	wchar_t *file_path,
	file_ctx_r *files)		// 各ソース・ファイルの情報
{
	int num;

	wcscpy(file_path, base_dir);
	for (num = 0; num < entity_num; num++){
		if (files[num].size == 0)
			continue;

		//printf("files[%d].state = %d\n", num, files[num].state);
		if (((files[num].state & 3) != 0) && ((files[num].state & 4) == 0)){	// 作業ファイルが存在するなら
			// 作業ファイルを削除する
			get_temp_name(list_buf + files[num].name, file_path + base_len);
			//printf_cp("delete %s\n", file_path);
			if (DeleteFile(file_path) == 0){
				//printf("error = %d\n", GetLastError());
				// Anti-Virusソフトが書き込み直後のファイルを検査してロックすることがある
				if (GetLastError() == 32){	// ERROR_SHARING_VIOLATION
					Sleep(100);	// 少し待ってから再挑戦する
					DeleteFile(file_path);
				}
			}
		}
	}
}

// ブロック単位の復元ができなくても、再構築したファイルで置き換える
void replace_incomplete(
	wchar_t *file_path,
	char *ascii_buf,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	wchar_t temp_path[MAX_LEN];
	int i, num, b_last, first_time;

	first_time = 1;
	wcscpy(file_path, base_dir);
	for (num = 0; num < entity_num; num++){
		if ((files[num].size > 0) && ((files[num].state & 0x80) == 0) &&
				((files[num].state & 3) != 0)){	// チェックサムと作業ファイルが存在するなら
			// 完全なブロックが含まれてるかどうか
			b_last = files[num].b_off + files[num].b_num;
			for (i = files[num].b_off; i < b_last; i++){
				if (s_blk[i].exist != 0){
					i = -1;
					break;
				}
			}
			if (i >= 0)
				continue;	// 完全なブロックを全く見つけれなかった場合はだめ

			// 作業ファイルが存在すれば、消失ファイルを代替し、破損ファイルを置き換える
			wcscpy(file_path + base_len, list_buf + files[num].name);
			get_temp_name(file_path, temp_path);
			if (replace_file(file_path, temp_path) == 0){
				files[num].state = 2;	// 破損ファイルにする
				if (first_time){
					printf("\nPutting incomplete file :\n");
					printf(" Status   :  Filename\n");
					first_time = 0;
				}
				utf16_to_cp(list_buf + files[num].name, ascii_buf, cp_output);
				printf(" Replaced : \"%s\"\n", ascii_buf);
			}
		}
	}
}

// リカバリ・ファイルを削除する（Useless状態だったのは無視する）
int purge_recovery_file(void)
{
	int err, num, recv_off;

	//printf("recovery_num = %d\n", recovery_num);
	err = num = 0;
	recv_off = 0;
	while (recv_off < recv2_len){
		//printf_cp("delete %s\n", recv2_buf + recv_off);
		// 標準でゴミ箱に入れようとする、失敗したら普通に削除する
		if (delete_file_recycle(recv2_buf + recv_off) == 0){
			num++;
		} else {
			err++;
		}

		// 次のファイルの位置にずらす
		while (recv2_buf[recv_off] != 0)
			recv_off++;
		recv_off++;
	}
	printf("%d PAR File(s) Deleted\n", num);

	return err;
}

