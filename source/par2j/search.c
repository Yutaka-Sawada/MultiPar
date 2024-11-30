// search.c
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
#include "search.h"

#define MIN_PACKET_SIZE	68

// ファイル情報のファイル名の文字コードを変換しなおす
static int convert_filename(unsigned int new_CP, unsigned int old_CP, file_ctx_r *files)
{
	char ascii_buf[MAX_LEN * 3];
	wchar_t uni_buf[MAX_LEN];
	int i, j, len;

	// 全てのファイル名を変換可能かを先に確かめる
	for (i = 0; i < file_num; i++){
		if (files[i].name <= 0)
			continue;	// ファイル名が取得できなかったファイルはとばす
		// old_CP で変換されたユニコードから元のデータに戻す
		if (utf16_to_cp(list_buf + files[i].name, ascii_buf, old_CP))
			return 1;
		// new_CP で再度変換しなおす
		if (cp_to_utf16(ascii_buf, uni_buf, new_CP))
			return 1;
	}

	// 先に確認済みなので、変換エラーは発生しないはず
	for (i = 0; i < file_num; i++){
		if (files[i].name <= 0)
			continue;	// ファイル名が取得できなかったファイルはとばす
		// old_CP で変換されたユニコードから元のデータに戻す
		utf16_to_cp(list_buf + files[i].name, ascii_buf, old_CP);
		// new_CP で再度変換しなおす
		cp_to_utf16(ascii_buf, uni_buf, new_CP);
		// 変換前のファイル名をリストから取り除く
		len = (int)wcslen(list_buf + files[i].name);
		list_len = remove_file_path(list_buf, list_len, files[i].name);
		for (j = 0; j < file_num; j++){
			if (files[j].name > files[i].name)
				files[j].name -= (len + 1);	// リスト上で後ろのやつをずらしていく
		}
		// 変換しなおしたファイル名を追加する
		files[i].name = list_len;
		if (add_file_path(uni_buf))
			return 2;
	}
	return 0;
}

// UTF-8 で記録しない PAR2 client かどうか
static int non_utf8_client(char *creator)
{
	int version;

	if (strncmp(creator, "QuickPar ", 9) == 0){
		// QuickPar 0.9
		version = creator[9] - 48;
		version *= 10;
		version += creator[10] - 48;
		if (version <= 9)
			return 1;
	} else if (strncmp(creator, "Created by phpar2 version ", 26) == 0){
		// Created by phpar2 version 1.3.
		version = creator[26] - 48;
		version *= 10;
		version += creator[28] - 48;
		if (version <= 13)
			return 1;
	}
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// リカバリ・ファイルを検索してファイル・リストに追加する
int search_recovery_files(void)
{
	wchar_t recovery_base[MAX_LEN], search_path[MAX_LEN], file_ext[EXT_LEN];
	wchar_t *tmp_p;
	int len, dir_len, ext_len, l_max;
	HANDLE hFind;
	WIN32_FIND_DATA FindData;

	get_base_dir(recovery_file, recovery_base);
	dir_len = (int)wcslen(recovery_base);

	// リカバリ・ファイルの名前の基
	get_base_filename(recovery_file, search_path, file_ext);
	if (switch_v & 2){	// 追加検査なら同じディレクトリ内の全ファイル
		wcscpy(search_path, recovery_base);
		wcscat(search_path, L"*");
		if (file_ext[0] != 0)	// 拡張子があったなら「*.拡張子」で検索する
			wcscat(search_path, file_ext);
	} else {
		if (file_ext[0] != 0){	// 拡張子があったなら「.*拡張子」で検索する
			file_ext[0] = '*';
			wcscat(search_path, L".");
			wcscat(search_path, file_ext);
			file_ext[0] = '.';
		} else {
			wcscat(search_path, L"*");
		}
	}
	ext_len = (int)wcslen(file_ext);

	l_max = ALLOC_LEN;
	recv_len = 0;
	recv_buf = (wchar_t *)malloc(l_max * 2);
	if (recv_buf == NULL){
		printf("malloc, %d\n", l_max * 2);
		return 1;
	}

	// リカバリ・ファイルを検索する
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind == INVALID_HANDLE_VALUE){
		print_win32_err();
		printf_cp("cannot find recovery file, %s\n", search_path);
		return 1;
	}
	do {
		if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0){
			len = (int)wcslen(FindData.cFileName);
			if (dir_len + len >= MAX_LEN)
				continue;	// 長すぎるファイル名は無視する
			if (file_ext[0] != 0){	// 拡張子が同じファイルだけ検査する
				if ((len <= ext_len) || (_wcsicmp(FindData.cFileName + (len - ext_len), file_ext) != 0))
					continue;
			}
			if (recv_len + dir_len + len >= l_max){	// 領域が足りなくなるなら拡張する
				l_max += ALLOC_LEN;
				tmp_p = (wchar_t *)realloc(recv_buf, l_max * 2);
				if (tmp_p == NULL){
					FindClose(hFind);
					printf("realloc, %d\n", l_max * 2);
					return 1;
				} else {
					recv_buf = tmp_p;
				}
			}

			// リストにコピーする
			wcscpy(recv_buf + recv_len, recovery_base);
			recv_len += dir_len;
			wcscpy(recv_buf + recv_len, FindData.cFileName);
			recv_len += len + 1;
			recovery_num++;
		}
	} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
	FindClose(hFind);
	// リカバリ・ファイルの拡張子が .par2 以外なら再検索する
	if ((file_ext[0] != 0) && (_wcsicmp(file_ext, L".par2") != 0)){
		get_base_filename(recovery_file, search_path, file_ext);
		wcscat(search_path, L".");
		wcscat(search_path, L"*");
		wcscat(search_path, L".par2");	// 「.*.par2」で検索する
		hFind = FindFirstFile(search_path, &FindData);
		if (hFind != INVALID_HANDLE_VALUE){
			do {
				if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0){
					len = (int)wcslen(FindData.cFileName);
					if (dir_len + len >= MAX_LEN)
						continue;	// 長すぎるファイル名は無視する
					if (recv_len + dir_len + len >= l_max){	// 領域が足りなくなるなら拡張する
						l_max += ALLOC_LEN;
						tmp_p = (wchar_t *)realloc(recv_buf, l_max * 2);
						if (tmp_p == NULL){
							FindClose(hFind);
							printf("realloc, %d\n", l_max * 2);
							return 1;
						} else {
							recv_buf = tmp_p;
						}
					}

					// リストにコピーする
					wcscpy(recv_buf + recv_len, recovery_base);
					recv_len += dir_len;
					wcscpy(recv_buf + recv_len, FindData.cFileName);
					recv_len += len + 1;
					recovery_num++;
				}
			} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
			FindClose(hFind);
		}
	}
	if (recovery_num > 1)
		sort_list(recv_buf, recv_len);	// リスト内でファイル名順に並び替える (外部ファイルは並び替えない)

	// 指定された検査対象にリカバリ・ファイルが含まれるかもしれない
	if (list2_buf){
		int list2_off = 0;
		if (file_ext[0] != 0){	// リカバリ・ファイルの拡張子
			ext_len = (int)wcslen(file_ext);
		} else {
			ext_len = MAX_LEN;	// 拡張子が省略されてる場合は比較しない
		}
		while (list2_off < list2_len){
			len = (int)wcslen(list2_buf + list2_off);
			// ファイル名が重複しないようにする
			if (search_file_path(recv_buf, recv_len, list2_buf + list2_off)){
				list2_off += len + 1;
				continue;
			}
			// 拡張子が「.par2」あるいはリカバリ・ファイルと同じ外部ファイルだけ検査する
			if (((len > 5) && (_wcsicmp(list2_buf + (list2_off + len - 5), L".par2") == 0)) ||
				((len > ext_len) && (_wcsicmp(list2_buf + (list2_off + len - ext_len), file_ext) == 0))){
				if (recv_len + len >= l_max){	// 領域が足りなくなるなら拡張する
					l_max += ALLOC_LEN;
					tmp_p = (wchar_t *)realloc(recv_buf, l_max * 2);
					if (tmp_p == NULL){
						printf("realloc, %d\n", l_max * 2);
						return 1;
					} else {
						recv_buf = tmp_p;
					}
				}
				// リストにコピーする
				wcscpy(recv_buf + recv_len, list2_buf + list2_off);
				recv_len += len + 1;
				recovery_num++;
			}
			list2_off += len + 1;	// 次のファイルへ
		}
	}

	// 探査時に除外するリストの領域を確保しておく
	recv2_len = 0;
	recv2_buf = (wchar_t *)malloc(recv_len * 2);
	if (recv2_buf == NULL){
		printf("malloc, %d\n", recv_len * 2);
		return 1;
	}
/*{
FILE *fp;
fp = fopen("par_list.txt", "wb");
fwrite(recv_buf, 2, recv_len, fp);
fclose(fp);
}*/

	return 0;
}

// Magic sequence と一致するか (match = 0, fail = 1)
static int match_magic(unsigned char *buf)
{
	if ((*buf++) != 'P')
		return 1;
	if ((*buf++) != 'A')
		return 1;
	if ((*buf++) != 'R')
		return 1;
	if ((*buf++) != '2')
		return 1;
	if ((*buf++) != 0)
		return 1;
	if ((*buf++) != 'P')
		return 1;
	if ((*buf++) != 'K')
		return 1;
	if ((*buf++) != 'T')
		return 1;

	return 0;
}

// Main packet を末尾から遡って探す
int search_main_packet(
	unsigned char *buf,		// 作業バッファー、File ID が戻る
	unsigned char *set_id)	// Recovery Set ID が戻る
{
	unsigned char hash[16];
	int num, recv_off, len, off;
	unsigned int rv, packet_size, time_last;
	__int64 file_size, file_off, end_size;
	HANDLE hFile;

	// リカバリ・ファイルの一覧を表示する
	printf("PAR File list :\n");
	printf("         Size :  Filename\n");
	total_file_size = 0;
	recv_off = 0;
	while (recv_off < recv_len){
		// ファイルを開くことができるか、サイズを取得できるかを確かめる
		hFile = CreateFile(recv_buf + recv_off, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE){
			recv_len = remove_file_path(recv_buf, recv_len, recv_off);	// リストから取り除く
			recovery_num--;
			continue;
		}
		// ファイルのサイズを取得する
		if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
			CloseHandle(hFile);
			recv_len = remove_file_path(recv_buf, recv_len, recv_off);	// リストから取り除く
			recovery_num--;
			continue;
		}
		CloseHandle(hFile);
		total_file_size += file_size;
		// リカバリ・ファイルの名前
		if (compare_directory(recovery_file, recv_buf + recv_off) == 0){	// 同じ場所なら
			utf16_to_cp(offset_file_name(recv_buf + recv_off), buf, cp_output);	// ファイル名だけにする
		} else {
			path_to_cp(recv_buf + recv_off, buf, cp_output);	// パスも表示する
		}
		printf("%13I64d : \"%s\"\n", file_size, buf);
		while (recv_buf[recv_off] != 0)	// 次のファイルの位置にずらす
			recv_off++;
		recv_off++;
	}
	printf("\nPAR File total size\t: %I64d\n", total_file_size);
	printf("PAR File possible count\t: %d\n\n", recovery_num);

	recv_off = 0;	// 先頭に戻しておく
	prog_last = -1;
	end_size = 0;
	for (num = 0; num < recovery_num; num++){
		if (cancel_progress() != 0)	// キャンセル処理
			return 2;

		// リカバリ・ファイルを開く
		if (num == 0){	// まずは指定されたリカバリ・ファイルから探す
			hFile = CreateFile(recovery_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		} else {
			if (_wcsicmp(recv_buf + recv_off, recovery_file) == 0){	// 最初に検査したリカバリ・ファイルはとばす
				while (recv_buf[recv_off] != 0)
					recv_off++;
				recv_off++;
			}
			hFile = CreateFile(recv_buf + recv_off, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			while (recv_buf[recv_off] != 0)
				recv_off++;
			recv_off++;
		}
		if (hFile == INVALID_HANDLE_VALUE)
			continue;	// エラーが発生したら次のリカバリ・ファイルを調べる

		// ファイルのサイズを取得する
		if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
			CloseHandle(hFile);
			continue;	// エラーが発生したら次のリカバリ・ファイルを調べる
		}
		if (file_size < MIN_PACKET_SIZE){
			CloseHandle(hFile);
			continue;	// 小さすぎるファイルは検査しない
		}
		// 読み取りサイズは最大で SEARCH_SIZE
		len = SEARCH_SIZE;
		if (file_size < SEARCH_SIZE)
			len = (unsigned int)file_size;
		//printf("num = %d, file_size = %I64d\n", num, file_size);
		time_last = GetTickCount();

		// 末尾から遡って読み込んでいく
		file_off = file_size;
		while (len > 0){
			file_off -= len;	// 読み取り開始位置になる
			// SEARCH_SIZE を読み込んでバッファーの先頭に入れる
			if (file_read_data(hFile, file_off, buf, len)){
				len = 0;	// 読み込み時にエラーが発生したファイルはそれ以上検査しない
				break;
			}

			// Main packet を探す
			off = 0;
			while (off < len){
				if (match_magic(buf + off) == 0){
					memcpy(&packet_size, buf + (off + 12), 4);	// パケット・サイズの上位4バイトが0以外だとだめ
					if (packet_size != 0){
						off += 8;
						continue;
					}
					memcpy(&packet_size, buf + (off + 8), 4);	// パケット・サイズを確かめる
					if ((packet_size & 3) || (packet_size < MIN_PACKET_SIZE) || (off + packet_size > SEARCH_SIZE * 3)){
						off += 8;
						continue;
					}
					// パケット全体がバッファー内にあれば破損してないかを調べる
					data_md5(buf + (off + 32), (packet_size - 32), hash);
					if (memcmp(buf + (off + 16), hash, 16) == 0){	// 完全なパケット発見
						if (memcmp(buf + (off + 48), "PAR 2.0\0Main\0\0\0\0", 16) == 0){
							// 念のためデータの整合性を検査する
							rv = 0;
							memcpy(&block_size, buf + (off + 68), 4);	// Slice size の上位 4バイトを確かめる
							if (block_size != 0)	// ブロック・サイズが 4GB以上だとエラー
								rv++;
							memcpy(&block_size, buf + (off + 64), 4);	// Slice size
							if (block_size > MAX_BLOCK_SIZE)	// 対応するブロック・サイズは 1GB まで
								rv++;
							if ((block_size & 3) != 0)	// ブロック・サイズは 4 の倍数のはず
								rv++;
							memcpy(&entity_num, buf + (off + 72), 4);	// Number of files in the recovery set.
							file_num = (packet_size - 76) / 16;
							if (file_num <= 0)
								rv++;
							if (entity_num > file_num)
								rv++;
							if (rv == 0){
								memcpy(set_id, buf + (off + 32), 16);	// Recovery Set ID
								memmove(buf, buf + (off + 76), packet_size - 76);	// File ID をバッファーの先頭に移す
								CloseHandle(hFile);
								// Recovery Set の情報を表示する
								printf("Recovery Set ID\t\t: ");
								print_hash(set_id);
								printf("\n");
								printf("Input File Slice size\t: %u\n", block_size);
								printf("Input File total count\t: %d\n", file_num);
								printf("Recovery Set file count : %d\n", entity_num);
								return 0;
							}
						}
						off += packet_size;	// パケットの終端まで一気に飛ぶ
					} else {
						off += 8;
					}
				} else {
					off++;
				}
			}

			// 簡易検査なら検査領域を制限する（ファイルの後半分だけ）
			if (((switch_v & 1) != 0) && (file_off < file_size / 2)){	// 最低でも後半分は検査する
				len = 0;
				break;
			}

			// バッファーの前半 SEARCH_SIZE * 2 を次の読み込みサイズ分だけ後ろにずらす
			len = SEARCH_SIZE;
			if (file_off < SEARCH_SIZE)
				len = (unsigned int)file_off;
			if (len > 0)
				memmove(buf + len, buf, SEARCH_SIZE * 2);

			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)(((end_size + file_size - file_off) * 1000) / total_file_size))){
					CloseHandle(hFile);
					return 2;
				}
				time_last = GetTickCount();
			}
		}
		CloseHandle(hFile);
		end_size += file_size;
	}
	if ((prog_last >= 0) && (prog_last != 1000))
		printf("100.0%%\r");

	return 1;
}

// ファイル情報のパケットを探す
int search_file_packet(
	char *ascii_buf,
	unsigned char *buf,		// 作業バッファー
	wchar_t *par_comment,	// Unicode コメントを入れる
	unsigned char *set_id,	// Recovery Set ID を確かめる
	int flag_sanitize,		// 0以外 = ファイル名を浄化する
	file_ctx_r *files)		// 各ソース・ファイルの情報
{
	char par_client[COMMENT_LEN], ascii_comment[COMMENT_LEN];
	unsigned char hash[16];
	wchar_t uni_buf[MAX_LEN];
	int i, j, k, recv_off, find_num;
	int len, off, max, used_CP = CP_UTF8;
	unsigned int packet_size, time_last;
	__int64 file_size, file_off, file_next, end_size;
	HANDLE hFile;

	par_client[0] = 0;
	par_comment[0] = 0;
	ascii_comment[0] = 0;

	list_len = 1;	// 先頭に不明用の null 文字を置く
	list_max = ALLOC_LEN;
	list_buf = (wchar_t *)malloc(list_max * 2);
	if (list_buf == NULL){
		printf("malloc, %d\n", list_max * 2);
		return 1;
	}
	list_buf[0] = 0;

	find_num = 0;
	recv_off = 0;
	end_size = 0;
	while (recv_off < recv_len){
		if (cancel_progress() != 0)	// キャンセル処理
			return 2;

		//utf16_to_cp(recv_buf + recv_off, ascii_buf, cp_output);
		//printf("verifying %s\n", ascii_buf);
		// リカバリ・ファイルを開く
		hFile = CreateFile(recv_buf + recv_off, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		while (recv_buf[recv_off] != 0)	// 次のファイルの位置にずらす
			recv_off++;
		recv_off++;
		if (hFile == INVALID_HANDLE_VALUE)
			continue;	// エラーが発生したら次のリカバリ・ファイルを調べる
		// ファイルのサイズを取得する
		if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
			CloseHandle(hFile);
			continue;	// エラーが発生したら次のリカバリ・ファイルを調べる
		}
		if (file_size < MIN_PACKET_SIZE){
			CloseHandle(hFile);
			continue;	// 小さすぎるファイルは検査しない
		}
		file_off = 0;
		time_last = GetTickCount();

		// 最初はファイルの先頭 SEARCH_SIZE * 3 を読み込む
		off = 0;
		len = SEARCH_SIZE * 3;
		if (file_size < SEARCH_SIZE * 3)
			len = (unsigned int)file_size;
		// 最大 SEARCH_SIZE * 3 を読み込んでバッファーの先頭に入れる
		if (file_read_data(hFile, 0, buf, len)){
			CloseHandle(hFile);
			continue;	// エラーが発生したら次のリカバリ・ファイルを調べる
		}
		file_next = len;	// 次の読み込み開始位置
		max = len;
		if (len > SEARCH_SIZE)
			max = SEARCH_SIZE;

		// パケットを探す
		while (len > 0){
			while (off < max){
				if (match_magic(buf + off) == 0){
					memcpy(&packet_size, buf + (off + 12), 4);	// パケット・サイズの上位4バイトが0以外だとだめ
					if (packet_size != 0){
						off += 8;
						continue;
					}
					memcpy(&packet_size, buf + (off + 8), 4);	// パケット・サイズを確かめる
					if ((packet_size & 3) || (packet_size < MIN_PACKET_SIZE) || (file_off + (__int64)off + (__int64)packet_size > file_size)){
						off += 8;
						continue;
					}
					if (off + packet_size > SEARCH_SIZE * 3){	// 大きすぎてバッファー内に収まってないなら
						if (packet_size > 64 + 4 + block_size){	// Recovery Slice packet よりも大きいパケットは無いはず
							off += 8;
							continue;
						}
						if ((switch_v & 1) == 0){	// 簡易検査でなければ
							// パケットが破損してないかを調べる
							if (file_md5(hFile, file_off + off + 32, packet_size - 32, hash)){
								off += 8;	// 計算できなかったら
							} else {
								if (memcmp(buf + (off + 16), hash, 16) == 0){	// 完全なパケット発見
									// バッファー内のオフセットとパケット・サイズ分ずらす
									if (file_next < file_size)
										file_next = file_off + off + packet_size;
									off = max;	// 一気に末尾までいく
								} else {
									off += 8;	// 破損パケットなら
								}
							}
						} else {	// 簡易検査では巨大パケットが完全かは調べない
							// バッファー内のオフセットとパケット・サイズ分ずらす
							if (file_next < file_size)
								file_next = file_off + off + packet_size;
							off = max;	// 一気に末尾までいく
						}
						continue;
					}
					// パケットが破損してないかを調べる
					data_md5(buf + (off + 32), (packet_size - 32), hash);
					if (memcmp(buf + (off + 16), hash, 16) == 0){	// 完全なパケット発見
						if (memcmp(buf + (off + 32), set_id, 16) != 0){	// Recovery Set ID が同じパケットだけ
							off += packet_size;
							continue;
						}
/*
memset(ascii_buf, 0, 9);
memcpy(ascii_buf, buf + (off + 56), 8);
printf(" packet : %s, size = %u, file_off = %I64d, off = %d\n", ascii_buf, packet_size, file_off, off);
*/
						if ((memcmp(buf + (off + 48), "PAR 2.0\0FileDesc", 16) == 0) && (packet_size - 120 < MAX_LEN * 3)){
							// File Description packet
							memcpy(ascii_buf, buf + (off + 120), packet_size - 120);
							ascii_buf[packet_size - 120] = 0;	// 末尾を null 文字にしておく
							j = -1;	// File ID が重複してないか確認する
							for (i = 0; i < file_num; i++){
								if (memcmp(buf + (off + 64), files[i].id, 16) == 0){	// File ID でどのファイルか探す
									if (files[i].name < 0){	// 新しく発見したのなら
										break;
									} else {	// 同じ情報が既に在った場合
										j = i;
									}
								}
							}
							if (i < file_num){
								memcpy(files[i].hash, buf + (off + 80), 32);
								memcpy(&(files[i].size), buf + (off + 112), 8);
								if ((j >= 0) && (i != j)){	// File ID が同じファイルが他に存在する
									// ファイル内容が異なるのに同じ File ID になると区別できないので駄目！
									// 逆に、ファイル内容 (ハッシュ値とサイズ) が同じなら問題ない？
									if ((files[i].size != files[j].size) ||
											 (memcmp(files[i].hash, files[j].hash, 32) != 0)){
										printf("same File ID, %d and %d\n", j, i);
										return 1;
									}
								}
								// パケット内に記録されてるファイル名をユニコードに変換する
								if (used_CP != CP_UTF8){	// QuickPar などファイル名が UTF-8 以外なら
									// 変換できるか調べる
									j = cp_to_utf16(ascii_buf, uni_buf, used_CP);
									if (j == ERROR_NO_UNICODE_TRANSLATION){	// CP1252 では変換エラーは発生しない
										j = cp_to_utf16(ascii_buf, uni_buf, 1252);
										if (j == 0){
											k = convert_filename(1252, used_CP, files);	// これまでのファイル名を変換しなおす
											if (k != 0){
												printf("convert_filename\n");
												return 1;
											}
										}
										used_CP = 1252;	// これ以降のファイル名は Latin-1 CP1252 とみなす
									}
								} else if (check_utf8(ascii_buf) != 0){	// UTF-8 以外の文字をみつけたら
									used_CP = CP_ACP;	// これ以降のファイル名は全て UTF-8 以外とみなす
									// ACP で変換できるか調べる
									j = cp_to_utf16(ascii_buf, uni_buf, used_CP);
									if (j == ERROR_NO_UNICODE_TRANSLATION){	// CP1252 では変換エラーは発生しない
										j = cp_to_utf16(ascii_buf, uni_buf, 1252);
										used_CP = 1252;	// これ以降のファイル名は Latin-1 CP1252 とみなす
									}
									if (j == 0){
										k = convert_filename(used_CP, CP_UTF8, files);	// これまでのファイル名を変換しなおす
										if (k == 1){	// CP1252 では変換エラーは発生しない
											j = cp_to_utf16(ascii_buf, uni_buf, 1252);
											k = convert_filename(1252, CP_UTF8, files);
											used_CP = 1252;	// これ以降のファイル名は Latin-1 CP1252 とみなす
										}
										if (k != 0){
											printf("convert_filename\n");
											return 1;
										}
									}
								} else {
									utf8_to_utf16(ascii_buf, uni_buf);
									j = 0;
								}
								if (j == 0){
									files[i].name = list_len;
									if (add_file_path(uni_buf)){
										printf("add_file_path\n");
										return 1;
									}
								} else {
									files[i].name = 0;	// パケットは存在するけどファイル名の変換に失敗した
								}
								find_num++;	// 完全な File Description packet の数
							}
						} else if ((par_client[0] == 0) && (memcmp(buf + (off + 48), "PAR 2.0\0Creator\0", 16) == 0)){
							// Creater packet
							i = packet_size - 64;	// Creater のバイト数
							if (i >= COMMENT_LEN)
								i = COMMENT_LEN - 1;
							memcpy(par_client, buf + (off + 64), i);
							par_client[i] = 0;	// 末尾を null 文字にする
						} else if ((par_comment[0] == 0) && (ascii_comment[0] == 0) && (memcmp(buf + (off + 48), "PAR 2.0\0CommASCI", 16) == 0)){
							// ASCII Comment packet
							i = packet_size - 64;	// コメントのバイト数
							if (i >= COMMENT_LEN)
								i = COMMENT_LEN - 1;
							memcpy(ascii_comment, buf + (off + 64), i);
							ascii_comment[i] = 0;	// 末尾を null 文字にする
						} else if ((par_comment[0] == 0) && (memcmp(buf + (off + 48), "PAR 2.0\0CommUni\0", 16) == 0)){
							// Unicode Comment packet
							i = packet_size - 80;	// コメントのバイト数
							if (i >= COMMENT_LEN * 2)
								i = COMMENT_LEN * 2 - 2;
							memcpy(par_comment, buf + (off + 80), i);
							par_comment[i / 2] = 0;	// 末尾を null 文字にする
						} else if ((memcmp(buf + (off + 48), "PAR 2.0\0UniFileN", 16) == 0) && (packet_size - 80 < MAX_LEN * 2)){
							// Unicode Filename packet
							memset(uni_buf, 0, sizeof(uni_buf));
							memcpy(uni_buf, buf + (off + 80), packet_size - 80);
							for (i = 0; i < file_num; i++){
								if (memcmp(buf + (off + 64), files[i].id, 16) == 0)	// File ID が一致すれば
									break;
							}
							if (i < file_num){	// File Description packet を先に読み込んでる状態でしか認識しない
								if (files[i].name == 0){	// パケットは存在するけどファイル名の変換に失敗してる場合
									// 新しいファイル名
									files[i].name = list_len;
									if (add_file_path(uni_buf)){
										printf("add_file_path\n");
										return 1;
									}
								} else if ((files[i].name > 0) && (wcscmp(uni_buf, list_buf + files[i].name) != 0)){
									// パケットが存在してファイル名も取得してるけどユニコード版とは異なる場合
									k = (int)wcslen(list_buf + files[i].name);
									list_len = remove_file_path(list_buf, list_len, files[i].name);
									for (j = 0; j < file_num; j++){
										if (files[j].name > files[i].name)
											files[j].name -= (k + 1);	// リスト上で後ろのやつをずらしていく
									}
									// 新しいファイル名
									files[i].name = list_len;
									if (add_file_path(uni_buf)){
										printf("add_file_path\n");
										return 1;
									}
								}
							}
						}
						off += packet_size;	// パケットの終端まで一気に飛ぶ
					} else {
						off += 8;
					}
				} else {
					off++;
				}
			}
			// 全てのファイル情報を取得してしまえば抜ける
			if ((find_num >= file_num) && (par_client[0] != 0))
				break;

			if (file_next < file_size){	// ファイル・データがまだ残ってれば
				if (file_next > file_off + SEARCH_SIZE * 3){	// 次の読み込み位置が前回よりも SEARCH_SIZE * 3 以上離れてるなら
					file_off = file_next;
					// 読み取りサイズは最大で SEARCH_SIZE * 3
					off = 0;
					len = SEARCH_SIZE * 3;
					if (file_size < file_next + SEARCH_SIZE * 3)
						len = (unsigned int)(file_size - file_next);
					// 最大 SEARCH_SIZE * 3 を読み込んでバッファーの先頭に入れる
					if (file_read_data(hFile, file_next, buf, len)){
						off = len;		// 読み込み時にエラーが発生した部分は検査しない
						file_next = file_size;	// 次は読み込まず、このファイルの検査を終える
					}
					file_next += len;	// 次の読み込み開始位置
					max = len;
					if (len > SEARCH_SIZE)
						max = SEARCH_SIZE;
				} else {	// 次の SEARCH_SIZE を読み込む
					// バッファーの内容を前にずらす
					memcpy(buf, buf + SEARCH_SIZE, SEARCH_SIZE);
					memcpy(buf + SEARCH_SIZE, buf + SEARCH_SIZE * 2, SEARCH_SIZE);
					file_off += SEARCH_SIZE;
					// 読み取りサイズは最大で SEARCH_SIZE
					off -= max;
					len = SEARCH_SIZE;
					if (file_size < file_next + SEARCH_SIZE)
						len = (unsigned int)(file_size - file_next);
					// 最大 SEARCH_SIZE を読み込んでバッファーの末尾に入れる
					if (file_read_data(hFile, file_next, buf + SEARCH_SIZE * 2, len)){
						memset(buf + SEARCH_SIZE * 2, 0, len);	// 読み込み時にエラーが発生した部分は 0 にしておく
						file_next = file_size;	// 次は読み込まず、このファイルの検査を終える
					}
					file_next += len;	// 次の読み込み開始位置
					max = SEARCH_SIZE;
					len += SEARCH_SIZE * 2;	// 未処理データのサイズに残ってる SEARCH_SIZE * 2 を足す
				}
			} else {	// バッファー内の残りデータを処理する
				off -= max;
				len -= max;
				if (len > 0){
					if (len > SEARCH_SIZE){
						max = SEARCH_SIZE;
						memcpy(buf, buf + SEARCH_SIZE, SEARCH_SIZE);
						memcpy(buf + SEARCH_SIZE, buf + SEARCH_SIZE * 2, len - SEARCH_SIZE);
					} else {
						max = len;
						memcpy(buf, buf + SEARCH_SIZE, len);	// バッファーの内容を前にずらす
					}
					file_off += SEARCH_SIZE;
				}
			}

			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)(((end_size + file_off) * 1000) / total_file_size))){
					CloseHandle(hFile);
					return 2;
				}
				time_last = GetTickCount();
			}
		}
		CloseHandle(hFile);
		end_size += file_size;

		// 全てのファイル情報を取得してしまえば抜ける
		if ((find_num >= file_num) && (par_client[0] != 0))
			break;
	}

	// QuickPar と phpar2 はファイル名を UTF-8 で記録しないことがわかってる
	if ((used_CP == CP_UTF8) && (par_client[0] != 0) && (non_utf8_client(par_client) != 0)){
		k = convert_filename(CP_ACP, CP_UTF8, files);	// これまでのファイル名を変換しなおす
		if (k == 1){
			used_CP = 1252;	// Latin-1 CP1252 で変換を試みる
			k = convert_filename(1252, CP_UTF8, files);
		}
		if (k != 0){
			printf("convert_filename\n");
			return 1;
		}
	}

	// 作成したクライアントとコメントを表示する
	if (par_client[0] != 0){
		for (i = 0; i < COMMENT_LEN; i++){	// 表示する前に sanitalize する
			if (par_client[i] == 0){
				break;
			} else if ((par_client[i] <= 31) || (par_client[i] >= 127)){	// 制御文字を消す、非 ASCII 文字も
				par_client[i] = ' ';
			}
		}
		printf("Creator : %s\n", par_client);
	}
	if ((par_comment[0] == 0) && (ascii_comment[0] != 0)){	// ユニコードのコメントを優先する
		if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, ascii_comment, -1, par_comment, COMMENT_LEN))
			par_comment[0] = 0;
	}
	if (par_comment[0] != 0){
		for (i = 0; i < COMMENT_LEN; i++){	// 表示する前に sanitalize する
			if (par_comment[i] == 0){
				break;
			} else if ((par_comment[i] <= 31) || (par_comment[i] == 127)){	// 制御文字を消す
				par_comment[i] = ' ';
			}
		}
		if (!utf16_to_cp(par_comment, ascii_buf, cp_output))
			printf("Comment : %s\n", ascii_buf);
	}
	write_ini_file2(par_client, par_comment);

	// リスト内のファイル名を検査して、問題があれば浄化する
	// ついでにディレクトリ記号を「\」に統一する
	if (flag_sanitize != 0){
		max = 0;
		for (i = 0; i < file_num; i++){
			if (files[i].name <= 0)
				continue;	// ファイル名が取得できなかったファイルはとばす
			wcscpy(uni_buf, list_buf + files[i].name);
			off = sanitize_filename(uni_buf, files, i);
			if (off != 0){
				if ((off == 1) || (off == 9)){	// ディレクトリ記号を含んでる
					wcscpy(list_buf + files[i].name, uni_buf);
					off ^= 1;
				}
				if (off != 0){
					if (max == 0){
						max = 1;
						printf("\nWarning about filenames :\n");
					}
					utf16_to_cp(uni_buf, ascii_buf, cp_output);
					if (off == 16){
						max++;
						printf("file%d: \"%s\" is invalid\n", i, ascii_buf);
					} else if (off == 8){
						printf("file%d: \"%s\" is invalid\n", i, ascii_buf);
					} else {
						printf("file%d: \"%s\" was sanitized\n", i, ascii_buf);
						if (off & 4){	// 文字数が変わった
							k = (int)wcslen(list_buf + files[i].name);
							list_len = remove_file_path(list_buf, list_len, files[i].name);
							for (j = 0; j < file_num; j++){
								if (files[j].name > files[i].name)
									files[j].name -= (k + 1);	// リスト上で後ろのやつをずらしていく
							}
							// 新しいファイル名
							files[i].name = list_len;
							if (add_file_path(uni_buf)){
								printf("add_file_path\n");
								return 1;
							}
						} else {	// 文字数が同じなら元の場所にコピーする
							wcscpy(list_buf + files[i].name, uni_buf);
						}
					}
				}
			}
		}
		if (max > 1)
			return 1;
	}

	return 0;
}

// 修復用のパケットを探す
// 0~= 不完全なリカバリ・ファイルの数を返す、-2=Cansel
int search_recovery_packet(
	char *ascii_buf,
	unsigned char *buf,		// 作業バッファー
	wchar_t *file_path,
	unsigned char *set_id,	// Recovery Set ID を確かめる
	HANDLE *rcv_hFile,		// 各リカバリ・ファイルのハンドル (verify なら NULL)
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	parity_ctx_r *p_blk)	// 各パリティ・ブロックの情報
{
	unsigned char hash[16];
	int i, j, recv_off, num, packet_count, bad_flag, file_block;
	int find_num, find_new, recovery_lost;
	int len, off, max;
	unsigned int packet_size, time_last, meta_data[7];
	__int64 file_size, file_off, file_next;
	HANDLE hFile;

	printf("\nLoading PAR File       :\n");
	printf(" Packet Slice Status   :  Filename\n");
	fflush(stdout);
	count_last = 0;
	recovery_lost = 0;	// 不完全なリカバリ・ファイルの数
	first_num = 0;	// 初めて見つけたパリティ・ブロックの数
	num = 0;
	recv_off = 0;
	while (recv_off < recv_len){
		if (cancel_progress() != 0)	// キャンセル処理
			return -2;

		//utf16_to_cp(recv_buf + recv_off, ascii_buf, cp_output);
		//printf("verifying %s\n", ascii_buf);
		// リカバリ・ファイルを開く
		hFile = CreateFile(recv_buf + recv_off, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE){
			i = -1;
		} else {
			// 検査するリカバリ・ファイルが同じであれば、再検査する必要は無い
			i = check_ini_recovery(hFile, meta_data);
		}
		if (i < 0){	// エラーが発生したら次のリカバリ・ファイルを調べる
			while (recv_buf[recv_off] != 0)	// 次のファイルの位置にずらす
				recv_off++;
			recv_off++;
			continue;
		}
		if (i == 0){
			memcpy(&file_size, meta_data, 8);
			find_num = 0;
			find_new = 0;
			file_off = 0;
			prog_last = -1;
			time_last = GetTickCount();

			// 最初はファイルの先頭 SEARCH_SIZE * 3 を読み込む
			bad_flag = 0;
			off = 0;
			len = SEARCH_SIZE * 3;
			if (file_size < SEARCH_SIZE * 3)
				len = (unsigned int)file_size;
			if (file_size < MIN_PACKET_SIZE){	// 小さすぎるファイルは検査しない
				bad_flag |= 1;
				len = 0;
			}
			// 最大 SEARCH_SIZE * 3 を読み込んでバッファーの先頭に入れる
			if (file_read_data(hFile, 0, buf, len)){
				off = len;		// 読み込み時にエラーが発生した部分は検査しない
				bad_flag |= 128;
				file_next = file_size;	// 次は読み込まず、このファイルの検査を終える
			} else {
				file_next = len;	// 次の読み込み開始位置
			}
			max = len;
			if (len > SEARCH_SIZE)
				max = SEARCH_SIZE;
			//printf("file_size = %I64d, max = %d, off = %d, len = %d\n", file_size, max, off, len);

			// パケットを探す
			packet_count = 0;
			while (len > 0){
				while (off < max){
					if (match_magic(buf + off) == 0){
						memcpy(&packet_size, buf + (off + 12), 4);	// パケット・サイズの上位4バイトが0以外だとだめ
						if (packet_size != 0){
							bad_flag |= 1;
							off += 8;
							continue;
						}
						memcpy(&packet_size, buf + (off + 8), 4);	// パケット・サイズを確かめる
						if ((packet_size & 3) || (packet_size < MIN_PACKET_SIZE) || (file_off + (__int64)off + (__int64)packet_size > file_size)){
							bad_flag |= 1;
							off += 8;
							continue;
						}
						if (off + packet_size > SEARCH_SIZE * 3){	// 大きすぎてバッファー内に収まってないなら
							if (packet_size > 64 + 4 + block_size){	// Recovery Slice packet よりも大きいパケットは無いはず
								off += 8;
								continue;
							}
							// パケットが破損してないかを調べる
							if (file_md5(hFile, file_off + off + 32, packet_size - 32, hash)){
								off += 8;	// 計算できなかったら
							} else {
								if (memcmp(buf + (off + 16), hash, 16) == 0){	// 完全なパケット発見
									if (memcmp(buf + (off + 32), set_id, 16) == 0){	// Recovery Set ID が同じなら
										// 少なくともパケットの先頭 SEARCH_SIZE * 2 まではバッファー内に存在する
										// バッファーに収まらないパケットは Recovery Slice packet だけのはず
										if (memcmp(buf + (off + 48), "PAR 2.0\0RecvSlic", 16) == 0){
											// Recovery Slice packet
											memcpy(&j, buf + (off + 64), 4);	// パリティ・ブロックの番号
											j &= 0xFFFF;	// 番号は 16-bit (65536以上は 0～65535 の繰り返し)
											if (j == 65535)	// (0 と 65535 は同じ)
												j = 0;
											if (j < parity_num){
												if (p_blk[j].exist == 0){
													find_new++;	// リカバリ・ファイル内の新しいパリティ・ブロックの数
													first_num++;
													p_blk[j].exist = 1;
													p_blk[j].file = num;
													p_blk[j].off = file_off + off + 68;
												}
												write_ini_recovery(j, file_off + off + 68);
												find_num++;	// リカバリ・ファイル内のパリティ・ブロックの数
											}
										}
										packet_count++;
									} else {
										bad_flag |= 2;	// 別の Set ID が混じってる
									}
									// バッファー内のオフセットとパケット・サイズ分ずらす
									if (file_next < file_size)
										file_next = file_off + off + packet_size;
									off = max;	// 一気に末尾までいく
								} else {
									off += 8;	// 破損パケットなら
								}
							}
							continue;
						}
						// パケットが破損してないかを調べる
						data_md5(buf + (off + 32), (packet_size - 32), hash);
						if (memcmp(buf + (off + 16), hash, 16) == 0){	// 完全なパケット発見
							if (memcmp(buf + (off + 32), set_id, 16) != 0){	// Recovery Set ID が同じパケットだけ
								bad_flag |= 2;	// 別の Set ID が混じってる
								off += packet_size;
								continue;
							}
/*
memset(ascii_buf, 0, 9);
memcpy(ascii_buf, buf + (off + 56), 8);
printf(" packet : %s, size = %u, file_off = %I64d, off = %d\n", ascii_buf, packet_size, file_off, off);
*/
							if (memcmp(buf + (off + 48), "PAR 2.0\0IFSC\0\0\0\0", 16) == 0){
								// Input File Slice Checksum packet
								for (j = 0; j < entity_num; j++){
									if (memcmp(buf + (off + 64), files[j].id, 16) == 0){	// File ID が一致すれば
										if (files[j].state & 0x80)	// チェックサムを新しく発見したのなら
											break;
									}
								}
								file_block = files[j].b_num;	// ソース・ブロックの数
								if ((j < entity_num) && ((int)((packet_size - 80) / 20) == file_block)){
									files[j].state = 0;	// そのファイルのチェックサムを見つけた
									for (i = 0; i < file_block; i++){
										memcpy(s_blk[files[j].b_off + i].hash, buf + (off + 80 + (20 * i)), 20);
										memcpy(&(s_blk[files[j].b_off + i].crc), buf + (off + 80 + (20 * i) + 16), 4);
										s_blk[files[j].b_off + i].crc ^= window_mask;	// CRC の初期値と最終処理の 0xFFFFFFFF を取り除く
									}
								}
							} else if (memcmp(buf + (off + 48), "PAR 2.0\0RecvSlic", 16) == 0){
								// Recovery Slice packet
								memcpy(&j, buf + (off + 64), 4);	// パリティ・ブロックの番号
								j &= 0xFFFF;	// 番号は 16-bit (65536以上は 0～65535 の繰り返し)
								if (j == 65535)	// (0 と 65535 は同じ)
									j = 0;
								if ((packet_size == 64 + 4 + block_size) && (j < parity_num)){
									if (p_blk[j].exist == 0){
										find_new++;	// リカバリ・ファイル内の新しいパリティ・ブロックの数
										first_num++;
										p_blk[j].exist = 1;
										p_blk[j].file = num;
										p_blk[j].off = file_off + off + 68;
									}
									write_ini_recovery(j, file_off + off + 68);
									find_num++;	// リカバリ・ファイル内のパリティ・ブロックの数
								}
							}
							packet_count++;
							off += packet_size;	// パケットの終端まで一気に飛ぶ
						} else {
							bad_flag |= 1;
							off += 8;
						}
					} else {
						bad_flag |= 1;
						off++;
					}
				}

				if (file_next < file_size){	// ファイル・データがまだ残ってれば
					if (file_next > file_off + SEARCH_SIZE * 3){	// 次の読み込み位置が前回よりも SEARCH_SIZE * 3 以上離れてるなら
						file_off = file_next;
						// 読み取りサイズは最大で SEARCH_SIZE * 3
						off = 0;
						len = SEARCH_SIZE * 3;
						if (file_size < file_next + SEARCH_SIZE * 3)
							len = (unsigned int)(file_size - file_next);
						// 最大 SEARCH_SIZE * 3 を読み込んでバッファーの先頭に入れる
						if (file_read_data(hFile, file_next, buf, len)){
							off = len;		// 読み込み時にエラーが発生した部分は検査しない
							bad_flag |= 128;
							file_next = file_size;	// 次は読み込まず、このファイルの検査を終える
						}
						file_next += len;	// 次の読み込み開始位置
						max = len;
						if (len > SEARCH_SIZE)
							max = SEARCH_SIZE;
					} else {	// 次の SEARCH_SIZE を読み込む
						// バッファーの内容を前にずらす
						memcpy(buf, buf + SEARCH_SIZE, SEARCH_SIZE);
						memcpy(buf + SEARCH_SIZE, buf + SEARCH_SIZE * 2, SEARCH_SIZE);
						file_off += SEARCH_SIZE;
						// 読み取りサイズは最大で SEARCH_SIZE
						off -= max;
						len = SEARCH_SIZE;
						if (file_size < file_next + SEARCH_SIZE)
							len = (unsigned int)(file_size - file_next);
						// 最大 SEARCH_SIZE を読み込んでバッファーの末尾 SEARCH_SIZE に入れる
						if (file_read_data(hFile, file_next, buf + SEARCH_SIZE * 2, len)){
							memset(buf + SEARCH_SIZE * 2, 0, len);	// 読み込み時にエラーが発生した部分は 0 にしておく
							bad_flag |= 128;
							file_next = file_size;	// 次は読み込まず、このファイルの検査を終える
						}
						file_next += len;	// 次の読み込み開始位置
						max = SEARCH_SIZE;
						len += SEARCH_SIZE * 2;	// 未処理データのサイズに残ってる SEARCH_SIZE * 2 を足す
					}
				} else {	// バッファー内の残りデータを処理する
					//printf("max = %d, off = %d, len = %d\n", max, off, len);
					off -= max;
					len -= max;
					if (len > 0){
						if (len > SEARCH_SIZE){
							max = SEARCH_SIZE;
							memcpy(buf, buf + SEARCH_SIZE, SEARCH_SIZE);
							memcpy(buf + SEARCH_SIZE, buf + SEARCH_SIZE * 2, len - SEARCH_SIZE);
						} else {
							max = len;
							memcpy(buf, buf + SEARCH_SIZE, len);	// バッファーの内容を前にずらす
						}
						file_off += SEARCH_SIZE;
					}
				}

				// 経過表示
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress_file((int)((file_off * 1000) / file_size), first_num, recv_buf + recv_off)){
						CloseHandle(hFile);
						write_ini_recovery2(-1, 0, 0, meta_data);
						return -2;
					}
					time_last = GetTickCount();
				}
			}
			write_ini_recovery2(packet_count, find_num, bad_flag, meta_data);
		} else {	// 検査済みなら記録を読み込む
			find_new = read_ini_recovery(num, &packet_count, &find_num, &bad_flag, p_blk);
			if (find_new < 0){
				bad_flag = 1;
				packet_count = 0;
				find_num = 0;
			} else {
				first_num += find_new;	// 新たに発見したパリティ・ブロックの数を合計する
			}
		}
		print_progress_file(-1, first_num, NULL);	// 重複を除外した合計ブロック数を表示する
		if ((find_new > 0) && (rcv_hFile != NULL)){
			rcv_hFile[num] = hFile;	// 後から読み込めるように開いたままにする
		} else {
			CloseHandle(hFile);
		}

		// リカバリ・ファイルの名前
		if (compare_directory(recovery_file, recv_buf + recv_off) == 0){	// 同じ場所なら
			utf16_to_cp(offset_file_name(recv_buf + recv_off), ascii_buf, cp_output);	// ファイル名だけにする
		} else {
			path_to_cp(recv_buf + recv_off, ascii_buf, cp_output);	// パスも表示する
		}
		if (bad_flag == 0){
			// 良好 (パケット単位なので完全に元と同じかは判別できない)
			printf("%7d %5d Good     : \"%s\"\n", packet_count, find_num, ascii_buf);
			// 削除する時用に記録しておく
			len = (int)wcslen(recv_buf + recv_off);
			wcscpy(recv2_buf + recv2_len, recv_buf + recv_off);
			recv2_len += len + 1;
			// 次のファイルの位置にずらす
			recv_off += len + 1;
		} else if (packet_count == 0){
			// パケットが全く見つからなかった (リカバリ・ファイルではない)
			// 別の Set ID のリカバリ・ファイルも含む
			printf("      0     0 Useless  : \"%s\"\n", ascii_buf);
			recovery_lost++;
			if (bad_flag == 2){	// 別の Set ID のリカバリ・ファイルで良好なら
				// 次のファイルの位置にずらす
				while (recv_buf[recv_off] != 0)
					recv_off++;
				recv_off++;
			} else {	// リカバリ・ファイルでなければリストから取り除く (ファイル数はそのまま)
				recv_len = remove_file_path(recv_buf, recv_len, recv_off);
			}
		} else {
			printf("%7d %5d Damaged  : \"%s\"\n", packet_count, find_num, ascii_buf);
			recovery_lost++;
			// 削除する時用に記録しておく
			len = (int)wcslen(recv_buf + recv_off);
			wcscpy(recv2_buf + recv2_len, recv_buf + recv_off);
			recv2_len += len + 1;
			// 破損したリカバリ・ファイルはリストから取り除く (ファイル数はそのまま)
			recv_len = remove_file_path(recv_buf, recv_len, recv_off);
		}
		fflush(stdout);
		num++;	// 次のファイルへ
	}
/*{
FILE *fp;
fp = fopen("par_list.txt", "wb");
fwrite(recv_buf, 2, recv_len, fp);
fclose(fp);
fp = fopen("par_list2.txt", "wb");
fwrite(recv2_buf, 2, recv2_len, fp);
fclose(fp);
}*/
	json_file_list(files);	// JSONファイルに記録する

	return recovery_lost;
}

