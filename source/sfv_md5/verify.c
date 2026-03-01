// verify.c
// Copyright : 2026-02-28 Yutaka Sawada
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

#include "common.h"
#include "crc.h"
#include "verify.h"
#include "ini.h"
#include "phmd5.h"

//#define TIMER

#ifdef TIMER
#include <time.h>
static clock_t time_start, time_calc;
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ディレクトリ内の再起探索用
static int file_recursive_search(
	wchar_t *search_path,	// 検索するファイルの親ディレクトリ、見つけた別名ファイルの相対パスが戻る
	int dir_len,			// ディレクトリ部分の長さ
	int hash_type,			// 0 = SFV, 1 = MD5
	wchar_t *find_hash)		// 探したいハッシュ値
{
	unsigned char buf[IO_SIZE];
	wchar_t *self_name = NULL, tmp_buf[33];
	int off, len;
	unsigned int crc, time_last, meta_data[7];
	__int64 file_size, file_left;
	HANDLE hFile, hFind;
	WIN32_FIND_DATA FindData;
	PHMD5 ctx;

	//printf_cp("search_path = %s\n", search_path);
	prog_last = -1;
	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する

	// チェックサムファイル自身を除外する
	if (_wcsnicmp(search_path, checksum_file, dir_len) == 0)
		self_name = checksum_file + dir_len;	// チェックサムファイル自身を除外する

	// 指定されたディレクトリ直下のファイルと比較する
	wcscpy(search_path + dir_len, L"*");
	hFind = FindFirstFile(search_path, &FindData);
	if (hFind != INVALID_HANDLE_VALUE){
		do {
			if (cancel_progress() != 0){	// キャンセル処理
				FindClose(hFind);
				return -2;
			}
			// フォルダなら無視する
			if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				continue;
			// 発見したファイル名が長すぎる場合は無視する
			if (dir_len + wcslen(FindData.cFileName) >= MAX_LEN)
				continue;
			// 自分自身を無視する
			if ((self_name != NULL) && (_wcsicmp(self_name, FindData.cFileName) == 0))
				continue;
			// 現在のディレクトリ部分に見つかったファイル名を連結する
			wcscpy(search_path + dir_len, FindData.cFileName);
			// 既にチェック済みのファイルは無視する
			if (search_hash_name(search_path + base_len) >= 0)
				continue;
			//printf_cp("found = %s\n", search_path + base_len);

			// 読み込むファイルを開く
			hFile = CreateFile(search_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (hFile == INVALID_HANDLE_VALUE)
				continue;
			if (hash_type == 0){	// CRC-32 を比較する
				len = check_ini_state(-1, meta_data, 4, (unsigned char *)&crc, hFile);
				if (len == 0)
					wsprintf(tmp_buf, L"%08X", crc);
			} else {	// MD5 を比較する
				len = check_ini_state(-1, meta_data, 16, buf, hFile);
				if (len == 0)
					wsprintf(tmp_buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
						buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
						buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
			}
			file_size = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
			if (len == 0){	// 記録がある時
				wsprintf((wchar_t *)buf, L"%I64d", file_size);
				off = hash_len;
				if (add_hash(search_path + base_len, tmp_buf, (wchar_t *)buf) != 0){
					CloseHandle(hFile);
					FindClose(hFind);
					return -3;	// 記録できない時は終わる
				}
				if (wcscmp(tmp_buf, find_hash) == 0){
					CloseHandle(hFile);
					FindClose(hFind);
					return off;
				}
				continue;	// 記録があってもハッシュ値が一致しなければ、次のファイルを調べる
			} else if (len != -2){
				continue;	// 属性取得エラーなら検査せずに、次のファイルに移る
			}
			file_left = file_size;
			// ハッシュ値を計算する
			if (hash_type == 0){	// CRC-32 初期化
				crc = 0xFFFFFFFF;
			} else {	// MD5 初期化
				Phmd5Begin(&ctx);
			}
			while (file_left > 0){
				len = IO_SIZE;
				if (file_left < IO_SIZE)
					len = (int)file_left;
				if (!ReadFile(hFile, buf, len, &off, NULL) || (len != off))
					break;	// 読み取りエラーは file_left > 0 になる
				file_left -= len;
				if (hash_type == 0){	// CRC-32 を更新する
					crc = crc_update(crc, buf, len);
				} else {	// MD5 を更新する
					Phmd5Process(&ctx, buf, len);
				}

				// 経過表示
				if (GetTickCount() / UPDATE_TIME != time_last){
					if (print_progress_file((int)(((file_size - file_left) * 1000) / file_size), FindData.cFileName)){
						CloseHandle(hFile);
						FindClose(hFind);
						return -2;
					}
					time_last = GetTickCount() / UPDATE_TIME;
				}
			}
			CloseHandle(hFile);
			if (file_left > 0)
				continue;	// エラー等で中断した場合
			if (hash_type == 0){	// CRC-32 最終処理
				crc ^= 0xFFFFFFFF;
				write_ini_state(-1, meta_data, 4, &crc);	// ハッシュ値を記録する
				wsprintf(tmp_buf, L"%08X", crc);
			} else {	// MD5 最終処理
				Phmd5End(&ctx);
				write_ini_state(-1, meta_data, 16, ctx.hash);	// ハッシュ値を記録する
				wsprintf(tmp_buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
					ctx.hash[0], ctx.hash[1], ctx.hash[2], ctx.hash[3], ctx.hash[4], ctx.hash[5], ctx.hash[6], ctx.hash[7],
					ctx.hash[8], ctx.hash[9], ctx.hash[10], ctx.hash[11], ctx.hash[12], ctx.hash[13], ctx.hash[14], ctx.hash[15]);
			}

			// 検出ファイルのハッシュ値とサイズを記録しておく
			wsprintf((wchar_t *)buf, L"%I64d", file_size);
			off = hash_len;
			if (add_hash(search_path + base_len, tmp_buf, (wchar_t *)buf) != 0){
				FindClose(hFind);
				return -3;	// 記録できない時は終わる
			}
			if (wcscmp(tmp_buf, find_hash) == 0){
				FindClose(hFind);
				return off;
			}
		} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
	}
	FindClose(hFind);

	if (switch_v & 8)
		return -1;	// ファイルのみ検査ならサブ・フォルダを調べない

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
			off = file_recursive_search(search_path, (int)wcslen(search_path), hash_type, find_hash);
			if (off != -1){	// -3 = エラー, -2 = キャンセル, 0以上 = 見つけた
				FindClose(hFind);
				return off;
			}	// 見つからなかった場合は続行する
		} while (FindNextFile(hFind, &FindData));	// 次のフォルダを検索する
	}
	FindClose(hFind);

	return -1;
}

// 基準ディレクトリ内のファイルを全て検査する
// -1 = 見つからず, -2 =キャンセル, -3 = エラー
static int file_all_check(
	wchar_t *file_path,
	int search_from,	// hash_buf 内のこれ以降から探す
	int hash_type,		// 0 = SFV, 1 = MD5
	wchar_t *find_hash)	// 探したいハッシュ値
{
	int off;

	// 既に検査済みのファイルのハッシュ値と比較する
	off = search_hash_value(search_from, find_hash);
	if (off >= 0)
		return off;

	// ディレクトリ内のファイルを再起探索する
	wcscpy(file_path, base_dir);
	off = file_recursive_search(file_path, base_len, hash_type, find_hash);

	return off;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// CRC-32 を比較する
//  0 = ファイルが存在して完全である
//  1 = ファイルが存在しない
//  2 = ファイルが破損してる
static int file_crc32_check(
	int num,
	char *ascii_name,
	wchar_t *uni_name,
	wchar_t *file_path,
	unsigned int crc2)
{
	unsigned char buf[IO_SIZE];
	int len, off, bad_flag;
	unsigned int crc, time_last, meta_data[7];
	__int64 file_size = 0, file_left;
	HANDLE hFile;

	prog_last = -1;
	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する
	wcscpy(file_path, base_dir);
	// 先頭の「..\」を許可してるので、基準ディレクトリから上に遡る。
	len = base_len - 1;
	off = 0;
	while ((uni_name[off] == '.') && (uni_name[off + 1] == '.') && (uni_name[off + 2] == '\\')){
		off += 3;
		file_path[len] = 0;
		while (file_path[len] != '\\'){
			file_path[len] = 0;
			len--;
		}
	}
	wcscat(file_path, uni_name + off);

	// 読み込むファイルを開く
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		bad_flag = 1;	// 消失は記録しない
	} else {
		bad_flag = check_ini_state(num, meta_data, 4, (unsigned char *)&crc, hFile);
		memcpy(&file_size, meta_data, 8);
		if (bad_flag == 0){	// 記録がある時
			if (crc2 != crc){
				bad_flag = 2;	// ハッシュ値が異なる
				if (switch_v & 2){	// 破損ファイルのハッシュ値を記録しておく
					wsprintf((wchar_t *)buf, L"%08X", crc);
					wsprintf((wchar_t *)buf + 64, L"%I64d", file_size);
					if (add_hash(uni_name, (wchar_t *)buf, (wchar_t *)buf + 64) != 0){
						printf("file%d: cannot add hash\n", num);
						CloseHandle(hFile);
						return 1;
					}
				}
			} else {
				bad_flag = 0;	// 完全
			}
		} else if (bad_flag == -2){	// 記録が無い場合 (属性取得エラーは不明にする)
			file_left = file_size;
			crc = 0xFFFFFFFF;	// 初期化
			while (file_left > 0){
				len = IO_SIZE;
				if (file_left < IO_SIZE)
					len = (int)file_left;
				if (!ReadFile(hFile, buf, len, &bad_flag, NULL) || (len != bad_flag))
					break;	// 読み取りエラーは必ず破損になる
				file_left -= len;
				// CRC-32 を更新する
				crc = crc_update(crc, buf, len);

				// 経過表示
				if (GetTickCount() / UPDATE_TIME != time_last){
					if (print_progress_file((int)(((file_size - file_left) * 1000) / file_size), uni_name)){
						CloseHandle(hFile);
						return 2;
					}
					time_last = GetTickCount() / UPDATE_TIME;
				}
			}
			if (file_left > 0){
				bad_flag = 2;	// エラー等で中断した場合は破損として扱い、検査結果を記録しない
			} else {
				crc ^= 0xFFFFFFFF;	// 最終処理
				if (crc != crc2){
					bad_flag = 2;	// ハッシュ値が異なる
					if (switch_v & 2){	// 破損ファイルのハッシュ値を記録しておく
						wsprintf((wchar_t *)buf, L"%08X", crc);
						wsprintf((wchar_t *)buf + 64, L"%I64d", file_size);
						if (add_hash(uni_name, (wchar_t *)buf, (wchar_t *)buf + 64) != 0){
							printf("file%d: cannot add hash\n", num);
							CloseHandle(hFile);
							return 1;
						}
					}
				} else {
					bad_flag = 0;	// 完全
				}
				// 完全か破損なら、ハッシュ値を記録する
				write_ini_state(num, meta_data, 4, &crc);
			}
		}
		CloseHandle(hFile);
	}

	switch (bad_flag){
	case 0:
		printf("%13I64d Complete : \"%s\"\n", file_size, ascii_name);
		break;
	case 1:
		printf("            0 Missing  : \"%s\"\n", ascii_name);
		bad_flag = 4 + 8;
		break;
	case 2:
		printf("%13I64d Damaged  : \"%s\"\n", file_size, ascii_name);
		bad_flag = 4;
		break;
	default:	// IOエラー等
		printf("            ? Unknown  : \"%s\"\n", ascii_name);
		bad_flag = 4;
		break;
	}
	fflush(stdout);

	if (cancel_progress() != 0)	// キャンセル処理
		return 2;
	return bad_flag;
}

// SFV ファイル
int verify_sfv(
	char *ascii_buf,
	wchar_t *file_path)
{
	wchar_t *line_off, uni_buf[MAX_LEN], tmp_buf[33];
	int i, off, num, line_num, line_len, name_len;
	int c_num, d_num, m_num, f_num;
	unsigned int err = 0, rv, crc, comment;

	// ファイル数を調べる
	comment = 0;
	num = 0;
	line_num = 1;
	line_off = text_buf;
	while (*line_off != 0){	// 一行ずつ処理する
		line_len = 0;
		while (line_off[line_len] != 0){	// 改行までを一行とする
			if (line_off[line_len] == '\n')
				break;
			if (line_off[line_len] == '\r')
				break;
			line_len++;
		}
		// 行の内容が適正か調べる
		if (line_off[0] == ';'){	// コメント
			if (((comment & 1) == 0) && (line_len > 8) && (line_len < MAX_LEN)){
				// クリエイターの表示がまだなら
				if (wcsncmp(line_off, L"; Generated by ", 15) == 0){	// WIN-SFV32, SFV32nix
					wcsncpy(uni_buf, line_off + 15, line_len - 15);
					uni_buf[line_len - 15] = 0;
					comment |= 1;
				} else if (wcsncmp(line_off, L"; Using ", 8) == 0){
					wcsncpy(uni_buf, line_off + 8, line_len - 8);
					uni_buf[line_len - 8] = 0;
					comment |= 1;	// 同一行を二度表示しない
				}
				if (comment & 1){
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Creator : %s\n", ascii_buf);
					comment |= 4;
				}
			}
			if (((comment & 6) == 0) && (line_len < MAX_LEN)){
				// 最初のコメントの表示がまだなら
				i = 1;
				while (i < line_len){
					if (line_off[i] != ' ')
						break;
					i++;
				}
				if (i < line_len){
					wcsncpy(uni_buf, line_off + i, line_len - i);
					uni_buf[line_len - i] = 0;
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Comment : %s\n", ascii_buf);
				}
				comment |= 2;	// 「;」だけの行も認識する
			}
			comment &= ~4;	// bit 4 を消す
		} else if ((line_len > 9) && (line_off[line_len - 9] == ' ')	// CRC の前がスペース
					&& (base16_len(line_off + (line_len - 8)) == 8)){	// 16進数で8文字
			// ファイル名
			name_len = line_len - 9;
			if (base_len + name_len < MAX_LEN){
				while (line_off[name_len - 1] == ' ')
					name_len--;
				// ファイル名の前後が「"」で囲まれてる場合は取り除く
				if ((line_off[0] == '"') && (line_off[name_len - 1] == '"')){
					name_len -= 2;
					wcsncpy(uni_buf, line_off + 1, name_len);
				} else {
					wcsncpy(uni_buf, line_off, name_len);
				}
				uni_buf[name_len] = 0;
				rv = sanitize_filename(uni_buf);	// ファイル名を浄化する
				if (rv > 1){
					if ((comment & 8) == 0){
						comment |= 8;
						printf("\nWarning about filenames :\n");
					}
					if (rv == 16){
						printf("line%d: filename is invalid\n", line_num);
						num++;	// 浄化できないファイル名
					} else if ((rv & 6) == 0){
						utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
						printf("line%d: \"%s\" is invalid\n", line_num, ascii_buf);
					} else {
						utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
						printf("line%d: \"%s\" was sanitized\n", line_num, ascii_buf);
					}
				}
				if (rv != 16){
					file_num++;
					// CRC
					crc = get_val32h(line_off + (line_len - 8));
					wsprintf(tmp_buf, L"%08X", crc);	// 大文字に揃える
					if (add_hash(uni_buf, tmp_buf, L"?") != 0){
						printf("line%d: cannot add hash\n", line_num);
						return 1;
					}
				}
			} else {
				if ((comment & 8) == 0){
					comment |= 8;
					printf("\nWarning about filenames :\n");
				}
				printf("line%d: filename is invalid\n", line_num);
				num++;	// 長すぎるファイル名
			}
		} else if (line_len > 0){
			//printf("line %d is invalid\n", line_num);
			num++;	// 内容が認識できない行
		}
		// 次の行へ
		line_off += line_len;
		if (*line_off == '\n')
			line_off++;
		if (*line_off == '\r'){
			line_off++;
			if (*line_off == '\n')	// 「\r\n」を一つの改行として扱う
				line_off++;
		}
		line_num++;
	}
	if (comment & 8)
		printf("\n");
	// チェックサム・ファイルの状態
	if (num == 0){
		printf("Status  : Good\n");
	} else {
		printf("Status  : Damaged\n");
		err |= 16;	// 後で 256に変更する
	}
	if (file_num == 0){
		printf("valid file is not found\n");
		return 1;
	}

	printf("\nInput File list : %d\n", file_num);
	printf(" CRC-32  :  Filename\n");
	fflush(stdout);
	off = 0;
	name_len = hash_len;	// ソースファイルの状態まで
	while (off < name_len){
		// ファイル名
		utf16_to_cp(hash_buf + off, ascii_buf, MAX_LEN * 3, cp_output);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// ハッシュ値
		printf("%S : \"%s\"\n", hash_buf + off, ascii_buf);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// 状態
		off += 2;
	}

	printf("\nVerifying Input File   :\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	if (recent_data != 0){	// 前回の検査結果を使うなら
		PHMD5 ctx;
		// チェックサム・ファイル識別用にハッシュ値を計算する
		Phmd5Begin(&ctx);
		Phmd5Process(&ctx, (unsigned char *)text_buf, text_len * 2);
		Phmd5End(&ctx);
		// 前回の検査結果が存在するか
		check_ini_file(ctx.hash, text_len);
	}
	num = c_num = d_num = m_num = f_num = 0;
	off = 0;
	while (off < name_len){
		// ファイル名
		wcscpy(uni_buf, hash_buf + off);
		utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// ハッシュ値
		wcscpy(tmp_buf, hash_buf + off);
		crc = get_val32h(tmp_buf);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// 状態
		rv = file_crc32_check(num, ascii_buf, uni_buf, file_path, crc);
		if (rv & 3){
			err = rv;
			return err;
		}
		if (rv & 0xC){	// Missing or Damaged
			err |= rv;
			err += 0x100;
		}
		if (rv == 0){
			hash_buf[off] = 'c';	// complete
			c_num++;
		} else if (rv == 4 + 8){
			hash_buf[off] = 'm';	// missing
			m_num++;
		} else {
			hash_buf[off] = 'd';	// damaged
			d_num++;
		}
		off += 2;
		num++;
	}
	printf("\nComplete file count\t: %d\n", c_num);

	// 消失か破損なら他のファイルと比較する
	if (((switch_v & 2) != 0) && ((err & 0xC) != 0)){
		printf("\nSearching misnamed file:\n");
		printf("         Size Status   :  Filename\n");
		fflush(stdout);
		off = 0;
		while (off < name_len){
			// ファイル名
			wcscpy(uni_buf, hash_buf + off);
			while (hash_buf[off] != 0)
				off++;
			off++;
			// ハッシュ値
			wcscpy(tmp_buf, hash_buf + off);
			while (hash_buf[off] != 0)
				off++;
			off++;
			// 状態
			if ((hash_buf[off] == 'd') || (hash_buf[off] == 'm')){
				line_len = file_all_check(file_path, name_len, 0, tmp_buf);
				//printf("\n file_all_check = %d\n", line_len);
				if (line_len <= -2)
					return 4 + line_len;
				if (line_len >= 0){
					line_num = line_len;
					// 検出ファイル名
					i = compare_directory(uni_buf, hash_buf + line_num);
					utf16_to_cp(hash_buf + line_num, ascii_buf, MAX_LEN * 3, cp_output);
					while (hash_buf[line_num] != 0)
						line_num++;
					line_num++;
					// ハッシュ値
					while (hash_buf[line_num] != 0)
						line_num++;
					line_num++;
					// サイズ
					printf("%13S Found    : \"%s\"\n", hash_buf + line_num, ascii_buf);
					// 本来のファイル名
					utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
					if (i == 0){	// 同じ場所なら別名
						printf("            = Misnamed : \"%s\"\n", ascii_buf);
					} else {	// 別の場所なら移動
						printf("            = Moved    : \"%s\"\n", ascii_buf);
					}
					f_num++;
					if (hash_buf[off] == 'd'){
						d_num--;
					} else if (hash_buf[off] == 'm'){
						m_num--;
					}
				}
			}
			off += 2;
		}
		printf("\nMisnamed file count\t: %d\n", f_num);
	}
	printf("Damaged file count\t: %d\n", d_num);
	printf("Missing file count\t: %d\n", m_num);

	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// MD5 を比較する
//  0 = ファイルが存在して完全である
//  1 = ファイルが存在しない
//  2 = ファイルが破損してる
static int file_md5_check(
	int num,
	char *ascii_name,
	wchar_t *uni_name,
	wchar_t *file_path,
	unsigned char *hash)
{
	unsigned char buf[IO_SIZE];
	int len, off, bad_flag;
	unsigned int time_last, meta_data[8];
	__int64 file_size = 0, file_left;
	HANDLE hFile;
	PHMD5 ctx;

	prog_last = -1;
	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する
	wcscpy(file_path, base_dir);
	// 先頭の「..\」を許可してるので、基準ディレクトリから上に遡る。
	len = base_len - 1;
	off = 0;
	while ((uni_name[off] == '.') && (uni_name[off + 1] == '.') && (uni_name[off + 2] == '\\')){
		off += 3;
		file_path[len] = 0;
		while (file_path[len] != '\\'){
			file_path[len] = 0;
			len--;
		}
	}
	wcscat(file_path, uni_name + off);
	//printf("path = \"%S\"\n", file_path);

	// 読み込むファイルを開く
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		bad_flag = 1;	// 消失は記録しない
	} else {
		bad_flag = check_ini_state(num, meta_data, 16, buf + 128, hFile);
		memcpy(&file_size, meta_data, 8);
		if (bad_flag == 0){	// 記録がある時
			if (memcmp(hash, buf + 128, 16) != 0){
				bad_flag = 2;	// ハッシュ値が異なる
				if (switch_v & 2){	// 破損ファイルのハッシュ値を記録しておく
					wsprintf((wchar_t *)buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
						buf[128], buf[129], buf[130], buf[131], buf[132], buf[133], buf[134], buf[135],
						buf[136], buf[137], buf[138], buf[139], buf[140], buf[141], buf[142], buf[143]);
					wsprintf((wchar_t *)buf + 64, L"%I64d", file_size);
					if (add_hash(uni_name, (wchar_t *)buf, (wchar_t *)buf + 64) != 0){
						printf("file%d: cannot add hash\n", num);
						CloseHandle(hFile);
						return 1;
					}
				}
			} else {
				bad_flag = 0;	// 完全
			}
		} else if (bad_flag == -2){	// 記録が無い場合 (属性取得エラーは不明にする)
			file_left = file_size;
			Phmd5Begin(&ctx);	// 初期化
			while (file_left > 0){
				len = IO_SIZE;
				if (file_left < IO_SIZE)
					len = (int)file_left;
				if (!ReadFile(hFile, buf, len, &bad_flag, NULL) || (len != bad_flag))
					break;	// 読み取りエラーは必ず破損になる
				file_left -= len;
				// MD5 を更新する
				Phmd5Process(&ctx, buf, len);

				// 経過表示
				if (GetTickCount() / UPDATE_TIME != time_last){
					if (print_progress_file((int)(((file_size - file_left) * 1000) / file_size), uni_name)){
						CloseHandle(hFile);
						return 2;
					}
					time_last = GetTickCount() / UPDATE_TIME;
				}
			}
			if (file_left > 0){
				bad_flag = 2;	// エラー等で中断した場合は破損として扱い、検査結果を記録しない
			} else {
				Phmd5End(&ctx);	// 最終処理
//				printf("%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
//					ctx.hash[0], ctx.hash[1], ctx.hash[2], ctx.hash[3], ctx.hash[4], ctx.hash[5], ctx.hash[6], ctx.hash[7],
//					ctx.hash[8], ctx.hash[9], ctx.hash[10], ctx.hash[11], ctx.hash[12], ctx.hash[13], ctx.hash[14], ctx.hash[15]);
				if (memcmp(hash, ctx.hash, 16) != 0){
					bad_flag = 2;	// ハッシュ値が異なる、または途中まで
					if (switch_v & 2){	// 破損ファイルのハッシュ値を記録しておく
						wsprintf((wchar_t *)buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
							ctx.hash[0], ctx.hash[1], ctx.hash[2], ctx.hash[3], ctx.hash[4], ctx.hash[5], ctx.hash[6], ctx.hash[7],
							ctx.hash[8], ctx.hash[9], ctx.hash[10], ctx.hash[11], ctx.hash[12], ctx.hash[13], ctx.hash[14], ctx.hash[15]);
						wsprintf((wchar_t *)buf + 64, L"%I64d", file_size);
						if (add_hash(uni_name, (wchar_t *)buf, (wchar_t *)buf + 64) != 0){
							printf("file%d: cannot add hash\n", num);
							CloseHandle(hFile);
							return 1;
						}
					}
				} else {
					bad_flag = 0;	// 完全
				}
				// 完全か破損なら、ハッシュ値を記録する
				write_ini_state(num, meta_data, 16, ctx.hash);
			}
		}
		CloseHandle(hFile);
	}

	switch (bad_flag){
	case 0:
		printf("%13I64d Complete : \"%s\"\n", file_size, ascii_name);
		break;
	case 1:
		printf("            0 Missing  : \"%s\"\n", ascii_name);
		bad_flag = 4 + 8;
		break;
	case 2:
		printf("%13I64d Damaged  : \"%s\"\n", file_size, ascii_name);
		bad_flag = 4;
		break;
	default:	// IOエラー等
		printf("            ? Unknown  : \"%s\"\n", ascii_name);
		bad_flag = 4;
		break;
	}
	fflush(stdout);

	if (cancel_progress() != 0)	// キャンセル処理
		return 2;
	return bad_flag;
}

// MD5 ファイル
int verify_md5(
	char *ascii_buf,
	wchar_t *file_path)
{
	unsigned char hash[16];
	wchar_t *line_off, uni_buf[MAX_LEN], num_buf[3], tmp_buf[33];
	int i, off, num, line_num, line_len, name_len;
	int c_num, d_num, m_num, f_num;
	unsigned int err = 0, rv, comment;

	// ファイル数を調べる
	comment = 0;
	num = 0;
	line_num = 1;
	line_off = text_buf;
	while (*line_off != 0){	// 一行ずつ処理する
		line_len = 0;
		while (line_off[line_len] != 0){	// 改行までを一行とする
			if (line_off[line_len] == '\n')
				break;
			if (line_off[line_len] == '\r')
				break;
			line_len++;
		}
		// 行の内容が適正か調べる
		if (line_off[0] == ';'){	// コメント
			if (((comment & 1) == 0) && (line_len > 8) && (line_len < MAX_LEN)){
				// クリエイターの表示がまだなら
				if (wcsncmp(line_off, L"; Generated by ", 15) == 0){	// md5sum, Easy MD5 Creator
					wcsncpy(uni_buf, line_off + 15, line_len - 15);
					uni_buf[line_len - 15] = 0;
					comment = 1;
				} else if (wcsncmp(line_off, L"; Using ", 8) == 0){
					wcsncpy(uni_buf, line_off + 8, line_len - 8);
					uni_buf[line_len - 8] = 0;
					comment = 1;
				}
				if (comment & 1){
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Creator : %s\n", ascii_buf);
					comment |= 4;	// 同一行を二度表示しない
				}
			}
			if (((comment & 6) == 0) && (line_len < MAX_LEN)){
				// 最初のコメントの表示がまだなら
				i = 1;
				while (i < line_len){
					if (line_off[i] != ' ')
						break;
					i++;
				}
				if (i < line_len){
					wcsncpy(uni_buf, line_off + i, line_len - i);
					uni_buf[line_len - i] = 0;
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Comment : %s\n", ascii_buf);
				}
				comment |= 2;	// 「;」だけの行も認識する
			}
			comment &= ~4;	// bit 4 を消す
		} else if (line_off[0] == '#'){	// コメント
			if (((comment & 1) == 0) && (line_len > 8) && (line_len < MAX_LEN)){
				// クリエイターの表示がまだなら
				if (wcsncmp(line_off, L"# Generated by ", 15) == 0){	// OpenHashTab
					wcsncpy(uni_buf, line_off + 15, line_len - 15);
					uni_buf[line_len - 15] = 0;
					comment = 1;
				} else if (wcsncmp(line_off, L"# MD5 checksum generated by ", 28) == 0){	// IsoBuster
					wcsncpy(uni_buf, line_off + 28, line_len - 28);
					uni_buf[line_len - 28] = 0;
					comment = 1;
				} else if (wcsncmp(line_off, L"#MD5 checksums generated by ", 28) == 0){	// xACT
					wcsncpy(uni_buf, line_off + 28, line_len - 28);
					uni_buf[line_len - 28] = 0;
					comment = 1;
				} else if (wcsncmp(line_off, L"# MD5 checksums generated by ", 29) == 0){	// MD5summer
					wcsncpy(uni_buf, line_off + 29, line_len - 29);
					uni_buf[line_len - 29] = 0;
					comment = 1;
				}
				if (comment & 1){
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Creator : %s\n", ascii_buf);
					comment |= 4;	// 同一行を二度表示しない
				}
			}
			if (((comment & 6) == 0) && (line_len < MAX_LEN)){
				// 最初のコメントの表示がまだなら
				i = 1;
				while (i < line_len){
					if (line_off[i] != ' ')
						break;
					i++;
				}
				if (i < line_len){
					wcsncpy(uni_buf, line_off + i, line_len - i);
					uni_buf[line_len - i] = 0;
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Comment : %s\n", ascii_buf);
				}
				comment |= 2;	// 「#」だけの行も認識する
			}
			comment &= ~4;	// bit 4 を消す
		} else if (line_len > 33){	// コメントではない
			// MD5 の後がスペース (md5sum 形式)
			if ((line_off[32] == ' ') && (base16_len(line_off) == 32)){
				// ファイル名
				name_len = line_len - 33;
				if (base_len + name_len < MAX_LEN){
					// タイプ記号「*」までのスペースは複数個でもいい
					while (line_off[line_len - name_len] == ' ')
						name_len--;
					if (line_off[line_len - name_len] == '*')
						name_len--;
					wcsncpy(uni_buf, line_off + (line_len - name_len), name_len);
					uni_buf[name_len] = 0;
					rv = sanitize_filename(uni_buf);	// ファイル名を浄化する
					if (rv > 1){
						if ((comment & 8) == 0){
							comment |= 8;
							printf("\nWarning about filenames :\n");
						}
						if (rv == 16){
							printf("line%d: filename is invalid\n", line_num);
							num++;	// 浄化できないファイル名
						} else if ((rv & 6) == 0){
							utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
							printf("line%d: \"%s\" is invalid\n", line_num, ascii_buf);
						} else {
							utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
							printf("line%d: \"%s\" was sanitized\n", line_num, ascii_buf);
						}
					}
					if (rv != 16){
						file_num++;
						// MD5
						num_buf[2] = 0;
						for (i = 0; i < 16; i++){
							wcsncpy(num_buf, line_off + (i * 2), 2);
							hash[i] = (unsigned char)get_val32h(num_buf);
						}
						wsprintf(tmp_buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
							hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7],
							hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);
						if (add_hash(uni_buf, tmp_buf, L"?") != 0){
							printf("line%d: cannot add hash\n", line_num);
							return 1;
						}
					}
				} else {
					if ((comment & 8) == 0){
						comment |= 8;
						printf("\nWarning about filenames :\n");
					}
					printf("line%d: filename is invalid\n", line_num);
					num++;	// 長すぎるファイル名
				}

			// ファイル名の後にMD5
			} else if ((line_off[line_len - 33] == ' ') && (base16_len(line_off + (line_len - 32)) == 32)){
				// MD5 の前が括弧 (BSD/OpenSSL 形式)
				if (((wcsncmp(line_off, L"MD5(", 4) == 0) || (wcsncmp(line_off, L"MD5 (", 5) == 0))
						&& ((wcsncmp(line_off + (line_len - 35), L")= ", 3) == 0) ||
							(wcsncmp(line_off + (line_len - 36), L") = ", 4) == 0))){
					// ファイル名
					name_len = line_len - 35 - 4;
					if (base_len + name_len < MAX_LEN){
						if (line_off[line_len - 35] != ')')
							name_len--;
						if (line_off[3] == ' '){
							name_len--;
							wcsncpy(uni_buf, line_off + 5, name_len);
						} else {
							wcsncpy(uni_buf, line_off + 4, name_len);
						}
						uni_buf[name_len] = 0;
						rv = sanitize_filename(uni_buf);	// ファイル名を浄化する
						if (rv > 1){
							if ((comment & 8) == 0){
								comment |= 8;
								printf("\nWarning about filenames :\n");
							}
							if (rv == 16){
								printf("line%d: filename is invalid\n", line_num);
								num++;	// 浄化できないファイル名
							} else if ((rv & 6) == 0){
								utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
								printf("line%d: \"%s\" is invalid\n", line_num, ascii_buf);
							} else {
								utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
								printf("line%d: \"%s\" was sanitized\n", line_num, ascii_buf);
							}
						}
						if (rv != 16){
							file_num++;
							num_buf[2] = 0;
							for (i = 0; i < 16; i++){
								wcsncpy(num_buf, line_off + (line_len - 32 + (i * 2)), 2);
								hash[i] = (unsigned char)get_val32h(num_buf);
							}
							wsprintf(tmp_buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
								hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7],
								hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);
							if (add_hash(uni_buf, tmp_buf, L"?") != 0){
								printf("line%d: cannot add hash\n", line_num);
								return 1;
							}
						}
					} else {
						if ((comment & 8) == 0){
							comment |= 8;
							printf("\nWarning about filenames :\n");
						}
						printf("line%d: filename is invalid\n", line_num);
						num++;	// 長すぎるファイル名
					}
				} else {	// MD5 の前がスペース (Easy MD5 Creator 形式)
					// ファイル名
					name_len = line_len - 33;
					if (base_len + name_len < MAX_LEN){
						while (line_off[name_len - 1] == ' ')
							name_len--;
						wcsncpy(uni_buf, line_off, name_len);
						uni_buf[name_len] = 0;
						rv = sanitize_filename(uni_buf);	// ファイル名を浄化する
						if (rv > 1){
							if ((comment & 8) == 0){
								comment |= 8;
								printf("\nWarning about filenames :\n");
							}
							if (rv == 16){
								printf("line%d: filename is invalid\n", line_num);
								num++;	// 浄化できないファイル名
							} else if ((rv & 6) == 0){
								utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
								printf("line%d: \"%s\" is invalid\n", line_num, ascii_buf);
							} else {
								utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
								printf("line%d: \"%s\" was sanitized\n", line_num, ascii_buf);
							}
						}
						if (rv != 16){
							file_num++;
							// MD5
							num_buf[2] = 0;
							for (i = 0; i < 16; i++){
								wcsncpy(num_buf, line_off + (line_len - 32 + (i * 2)), 2);
								hash[i] = (unsigned char)get_val32h(num_buf);
							}
							wsprintf(tmp_buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
								hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7],
								hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);
							if (add_hash(uni_buf, tmp_buf, L"?") != 0){
								printf("line%d: cannot add hash\n", line_num);
								return 1;
							}
						}
					} else {
						if ((comment & 8) == 0){
							comment |= 8;
							printf("\nWarning about filenames :\n");
						}
						printf("line%d: filename is invalid\n", line_num);
						num++;	// 長すぎるファイル名
					}
				}
			} else {
				num++;	// 内容が認識できない行
			}
		} else if (line_len > 0){
			num++;	// 内容が認識できない行
		}
		// 次の行へ
		line_off += line_len;
		if (*line_off == '\n')
			line_off++;
		if (*line_off == '\r'){
			line_off++;
			if (*line_off == '\n')	// 「\r\n」を一つの改行として扱う
				line_off++;
		}
		line_num++;
	}
	if (comment & 8)
		printf("\n");
	// チェックサム・ファイルの状態
	if (num == 0){
		printf("Status  : Good\n");
	} else {
		printf("Status  : Damaged\n");
		err |= 16;	// 後で 256に変更する
	}
	if (file_num == 0){
		printf("valid file is not found\n");
		return 1;
	}

	printf("\nInput File list : %d\n", file_num);
	printf("            MD5 Hash             :  Filename\n");
	fflush(stdout);
	off = 0;
	name_len = hash_len;	// ソースファイルの状態まで
	while (off < name_len){
		// ファイル名
		utf16_to_cp(hash_buf + off, ascii_buf, MAX_LEN * 3, cp_output);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// ハッシュ値
		printf("%S : \"%s\"\n", hash_buf + off, ascii_buf);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// 状態
		off += 2;
	}

	printf("\nVerifying Input File   :\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	if (recent_data != 0){	// 前回の検査結果を使うなら
		PHMD5 ctx;
		// チェックサム・ファイル識別用にハッシュ値を計算する
		Phmd5Begin(&ctx);
		Phmd5Process(&ctx, (unsigned char *)text_buf, text_len * 2);
		Phmd5End(&ctx);
		// 前回の検査結果が存在するか
		check_ini_file(ctx.hash, text_len);
	}
	num = c_num = d_num = m_num = f_num = 0;
	off = 0;
	while (off < name_len){
		// ファイル名
		wcscpy(uni_buf, hash_buf + off);
		utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// ハッシュ値
		wcscpy(tmp_buf, hash_buf + off);
		num_buf[2] = 0;
		for (i = 0; i < 16; i++){
			num_buf[0] = tmp_buf[i * 2    ];
			num_buf[1] = tmp_buf[i * 2 + 1];
			hash[i] = (unsigned char)get_val32h(num_buf);
		}
		while (hash_buf[off] != 0)
			off++;
		off++;
		// 状態
		rv = file_md5_check(num, ascii_buf, uni_buf, file_path, hash);
		if (rv & 3){
			err = rv;
			return err;
		}
		if (rv & 0xC){	// Missing or Damaged
			err |= rv;
			err += 0x100;
		}
		if (rv == 0){
			hash_buf[off] = 'c';	// complete
			c_num++;
		} else if (rv == 4 + 8){
			hash_buf[off] = 'm';	// missing
			m_num++;
		} else {
			hash_buf[off] = 'd';	// damaged
			d_num++;
		}
		off += 2;
		num++;
	}
	printf("\nComplete file count\t: %d\n", c_num);

	// 消失か破損なら他のファイルと比較する
	if (((switch_v & 2) != 0) && ((err & 0xC) != 0)){
		printf("\nSearching misnamed file:\n");
		printf("         Size Status   :  Filename\n");
		fflush(stdout);
		off = 0;
		while (off < name_len){
			// ファイル名
			wcscpy(uni_buf, hash_buf + off);
			while (hash_buf[off] != 0)
				off++;
			off++;
			// ハッシュ値
			wcscpy(tmp_buf, hash_buf + off);
			while (hash_buf[off] != 0)
				off++;
			off++;
			// 状態
			if ((hash_buf[off] == 'd') || (hash_buf[off] == 'm')){
				line_len = file_all_check(file_path, name_len, 1, tmp_buf);
				if (line_len <= -2)
					return 4 + line_len;
				if (line_len >= 0){
					line_num = line_len;
					// 検出ファイル名
					i = compare_directory(uni_buf, hash_buf + line_num);
					utf16_to_cp(hash_buf + line_num, ascii_buf, MAX_LEN * 3, cp_output);
					while (hash_buf[line_num] != 0)
						line_num++;
					line_num++;
					// ハッシュ値
					while (hash_buf[line_num] != 0)
						line_num++;
					line_num++;
					// サイズ
					printf("%13S Found    : \"%s\"\n", hash_buf + line_num, ascii_buf);
					// 本来のファイル名
					utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
					if (i == 0){	// 同じ場所なら別名
						printf("            = Misnamed : \"%s\"\n", ascii_buf);
					} else {	// 別の場所なら移動
						printf("            = Moved    : \"%s\"\n", ascii_buf);
					}
					f_num++;
					if (hash_buf[off] == 'd'){
						d_num--;
					} else if (hash_buf[off] == 'm'){
						m_num--;
					}
				}
			}
			off += 2;
		}
		printf("\nMisnamed file count\t: %d\n", f_num);
	}
	printf("Damaged file count\t: %d\n", d_num);
	printf("Missing file count\t: %d\n", m_num);

	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// FLAC Fingerprint を比較する
//  0 = ファイルが存在して完全である
//  1 = ファイルが存在しない
//  2 = ファイルが破損してる
static int file_ffp_check(
	int num,
	char *ascii_name,
	wchar_t *uni_name,
	wchar_t *file_path,
	unsigned char *hash)
{
	unsigned char buf[42];
	wchar_t cmdline[MAX_LEN + 8];
	int len, rv, bad_flag;
	unsigned int meta_data[8];
	__int64 file_size = 0;
	HANDLE hFile;

	wcscpy(file_path, base_dir);
	wcscat(file_path, uni_name);
	//printf("path = \"%S\"\n", file_path);

	// 読み込むファイルを開く
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		bad_flag = 1;	// 消失は記録しない
	} else {
		bad_flag = check_ini_state(num, meta_data, 1, buf, hFile);
		memcpy(&file_size, meta_data, 8);
		if (bad_flag == 0){	// 記録がある時
			bad_flag = buf[0];	// ハッシュ値ではなく状態を直接記録してる
		} else if (bad_flag == -2){	// 記録が無い場合 (属性取得エラーは不明にする)
			if (file_size < 42){
				bad_flag = 2;	// ファイルが小さい
			} else {
				if (!ReadFile(hFile, buf, 42, &len, NULL) || (42 != len)){
					bad_flag = 2;	// 読み取りエラーは必ず破損になる
				} else {
					// フォーマットを確認する
					if ((buf[0] != 0x66) || (buf[1] != 0x4C) || (buf[2] != 0x61) || (buf[3] != 0x43)){ // fLaC
						bad_flag = 2;
					} else if (((buf[4] & 0x7F) != 0) || (buf[5] != 0) || (buf[6] != 0) || (buf[7] != 34)){ // streaminfo metadata のサイズ
						bad_flag = 2;
					} else if (memcmp(hash, buf + 26, 16) != 0){	// MD5 を比較する
						bad_flag = 2;	// ファイルに記録されてるハッシュ値が異なる
					}
				}
			}
			CloseHandle(hFile);

			if (bad_flag < 0){	// flac.exe を呼び出す
				STARTUPINFO si;
				PROCESS_INFORMATION pi;

				bad_flag = -3;
				memset(&si, 0, sizeof(si));
				si.cb = sizeof(si);
				memset(&pi, 0, sizeof(pi));
				// 実行ファイルを lpCommandLine に含めると PATH 環境を検索してくれる
				// lpApplicationName に実行ファイルを指定した時は検索しないことに注意！
				//wcscpy(cmdline, L"flac.exe -t ");
				wcscpy(cmdline, L"flac.exe -t --totally-silent ");	// エラー内容を表示しない
				// ファイル名がスペースを含む場合は「"」で囲む
				len = wcslen(uni_name);
				for (rv = 0; rv < len; rv++){
					if (uni_name[rv] == ' ')
						break;
				}
				if (rv < len){
					rv = wcslen(cmdline);
					cmdline[rv] = '"';
					wcscpy(cmdline + rv + 1, uni_name);	// 「"」で囲む
					cmdline[rv + len + 1] = '"';
					cmdline[rv + len + 2] = 0;
				} else {
					wcscat(cmdline, uni_name);	// そのまま
				}
				//printf("cmdline = \"%S\"\n", cmdline);
				if (CreateProcess(NULL, cmdline,
						NULL,	//プロセスのセキュリティー記述子
						NULL,	//スレッドのセキュリティー記述子
						FALSE,	//ハンドルを継承しない
						0,	//作成フラグ
						NULL,	//環境変数は引き継ぐ
						base_dir,	//カレントディレクトリー
						&si, &pi) != 0){
					// 終了を待つ（キャンセルできた方がいいかも？でも、実験できない・・・）
					rv = WaitForSingleObject(pi.hProcess, INFINITE);
					if (rv == WAIT_OBJECT_0){	// 完了したなら
						if (GetExitCodeProcess(pi.hProcess, &rv) != 0){	// 終了コードを取得する
							//printf("ExitCode = %d\n", rv);
							if (rv == 0){
								bad_flag = 0;	// 完全
							} else {
								bad_flag = 2;	// ファイルが破損またはエラー
							}
						}
					}
					if (pi.hThread != NULL)	// 不要なハンドルをクローズする
						CloseHandle(pi.hThread);
					CloseHandle(pi.hProcess);
				}
			}

			if (bad_flag >= 0)	// flac.exe にエラーが発生した時は記録しない
				write_ini_state(num, meta_data, 1, &bad_flag);	// 検査結果を記録する、完全か破損
		} else {
			CloseHandle(hFile);
		}
	}

	switch (bad_flag){
	case 0:
		printf("%13I64d Complete : \"%s\"\n", file_size, ascii_name);
		break;
	case 1:
		printf("            0 Missing  : \"%s\"\n", ascii_name);
		bad_flag = 4 | 8;
		break;
	case 2:
		printf("%13I64d Damaged  : \"%s\"\n", file_size, ascii_name);
		bad_flag = 4;
		break;
	case -3:	// flac.exe のエラー
		printf("%13I64d Unknown  : \"%s\"\n", file_size, ascii_name);
		bad_flag = 4;
		break;
	default:	// IOエラー等
		printf("            ? Unknown  : \"%s\"\n", ascii_name);
		bad_flag = 4;
		break;
	}
	fflush(stdout);

	if (cancel_progress() != 0)	// キャンセル処理
		return 2;
	return bad_flag;
}

// FLAC Fingerprint ファイル
int verify_ffp(
	char *ascii_buf,
	wchar_t *file_path)
{
	unsigned char hash[16];
	wchar_t *line_off, uni_buf[MAX_LEN], num_buf[3], tmp_buf[33];
	int i, off, num, line_num, line_len, name_len;
	int c_num, d_num, m_num;
	unsigned int err = 0, rv, comment;

	// ファイル数を調べる
	comment = 0;
	num = 0;
	line_num = 1;
	line_off = text_buf;
	while (*line_off != 0){	// 一行ずつ処理する
		line_len = 0;
		while (line_off[line_len] != 0){	// 改行までを一行とする
			if (line_off[line_len] == '\n')
				break;
			if (line_off[line_len] == '\r')
				break;
			line_len++;
		}
		// 行の内容が適正か調べる
		if (line_off[0] == ';'){	// コメント
			if (((comment & 1) == 0) && (line_len > 8) && (line_len < MAX_LEN)){
				// クリエイターの表示がまだなら
				if (wcsncmp(line_off, L"; Generated by ", 15) == 0){	// WIN-SFV32, SFV32nix
					wcsncpy(uni_buf, line_off + 15, line_len - 15);
					uni_buf[line_len - 15] = 0;
					comment |= 1;
				} else if (wcsncmp(line_off, L"; flac fingerprint file generated by ", 37) == 0){	// Trader's Little Helper
					wcsncpy(uni_buf, line_off + 37, line_len - 37);
					uni_buf[line_len - 37] = 0;
					comment |= 1;	// 同一行を二度表示しない
				}
				if (comment & 1){
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Creator : %s\n", ascii_buf);
					comment |= 4;
				}
			}
			if (((comment & 6) == 0) && (line_len < MAX_LEN)){
				// 最初のコメントの表示がまだなら
				i = 1;
				while (i < line_len){
					if (line_off[i] != ' ')
						break;
					i++;
				}
				if (i < line_len){
					wcsncpy(uni_buf, line_off + i, line_len - i);
					uni_buf[line_len - i] = 0;
					uni_buf[COMMENT_LEN - 1] = 0;	// 表示する文字数を制限する
					utf16_to_cp(uni_buf, ascii_buf, COMMENT_LEN * 3, cp_output);
					printf("Comment : %s\n", ascii_buf);
				}
				comment |= 2;	// 「;」だけの行も認識する
			}
			comment &= ~4;	// bit 4 を消す
		} else if ((line_len > 33) && (line_off[line_len - 33] == ':')	// MD5 の前が「:」
					&& (base16_len(line_off + (line_len - 32)) == 32)){	// 16進数で32文字
			// ファイル名
			name_len = line_len - 33;
			if (base_len + name_len < MAX_LEN){
				while (line_off[name_len - 1] == ' ')
					name_len--;
				wcsncpy(uni_buf, line_off, name_len);
				uni_buf[name_len] = 0;
				rv = sanitize_filename(uni_buf);	// ファイル名を浄化する
				if (rv > 1){
					if ((comment & 8) == 0){
						comment |= 8;
						printf("\nWarning about filenames :\n");
					}
					if (rv == 16){
						printf("line%d: filename is invalid\n", line_num);
						num++;	// 浄化できないファイル名
					} else if ((rv & 6) == 0){
						utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
						printf("line%d: \"%s\" is invalid\n", line_num, ascii_buf);
					} else {
						utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
						printf("line%d: \"%s\" was sanitized\n", line_num, ascii_buf);
					}
				}
				if (rv != 16){
					file_num++;
					// MD5
					num_buf[2] = 0;
					for (i = 0; i < 16; i++){
						wcsncpy(num_buf, line_off + (line_len - 32 + (i * 2)), 2);
						hash[i] = (unsigned char)get_val32h(num_buf);
					}
					wsprintf(tmp_buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
						hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7],
						hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);
					if (add_hash(uni_buf, tmp_buf, L"?") != 0){
						printf("line%d: cannot add hash\n", line_num);
						return 1;
					}
				}
			} else {
				if ((comment & 8) == 0){
					comment |= 8;
					printf("\nWarning about filenames :\n");
				}
				printf("line%d: filename is invalid\n", line_num);
				num++;	// 長すぎるファイル名
			}
		} else if (line_len > 0){
			//printf("line %d is invalid\n", line_num);
			num++;	// 内容が認識できない行
		}
		// 次の行へ
		line_off += line_len;
		if (*line_off == '\n')
			line_off++;
		if (*line_off == '\r'){
			line_off++;
			if (*line_off == '\n')	// 「\r\n」を一つの改行として扱う
				line_off++;
		}
		line_num++;
	}
	if (comment & 8)
		printf("\n");
	// チェックサム・ファイルの状態
	if (num == 0){
		printf("Status  : Good\n");
	} else {
		printf("Status  : Damaged\n");
		err |= 16;	// 後で 256に変更する
	}
	if (file_num == 0){
		printf("valid file is not found\n");
		return 1;
	}

	printf("\nInput File list : %d\n", file_num);
	printf("        FLAC Fingerprint         :  Filename\n");
	fflush(stdout);
	off = 0;
	name_len = hash_len;	// ソースファイルの状態まで
	while (off < name_len){
		// ファイル名
		utf16_to_cp(hash_buf + off, ascii_buf, MAX_LEN * 3, cp_output);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// ハッシュ値
		printf("%S : \"%s\"\n", hash_buf + off, ascii_buf);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// 状態
		off += 2;
	}

	printf("\nVerifying Input File   :\n");
	printf("         Size Status   :  Filename\n");
	fflush(stdout);
	if (recent_data != 0){	// 前回の検査結果を使うなら
		PHMD5 ctx;
		// チェックサム・ファイル識別用にハッシュ値を計算する
		Phmd5Begin(&ctx);
		Phmd5Process(&ctx, (unsigned char *)text_buf, text_len * 2);
		Phmd5End(&ctx);
		// 前回の検査結果が存在するか
		check_ini_file(ctx.hash, text_len);
	}
#ifdef TIMER
time_start = clock();
#endif
	num = c_num = d_num = m_num = 0;
	off = 0;
	while (off < name_len){
		// ファイル名
		wcscpy(uni_buf, hash_buf + off);
		utf16_to_cp(uni_buf, ascii_buf, MAX_LEN * 3, cp_output);
		while (hash_buf[off] != 0)
			off++;
		off++;
		// ハッシュ値
		wcscpy(tmp_buf, hash_buf + off);
		num_buf[2] = 0;
		for (i = 0; i < 16; i++){
			num_buf[0] = tmp_buf[i * 2    ];
			num_buf[1] = tmp_buf[i * 2 + 1];
			hash[i] = (unsigned char)get_val32h(num_buf);
		}
		while (hash_buf[off] != 0)
			off++;
		off++;
		// 状態
		rv = file_ffp_check(num, ascii_buf, uni_buf, file_path, hash);
		if (rv & 3){
			err = rv;
			return err;
		}
		if (rv & 0xC){	// Missing or Damaged
			err |= rv;
			err += 0x100;
		}
		if (rv == 0){
			hash_buf[off] = 'c';	// complete
			c_num++;
		} else if (rv == 4 + 8){
			hash_buf[off] = 'm';	// missing
			m_num++;
		} else {
			hash_buf[off] = 'd';	// damaged
			d_num++;
		}
		off += 2;
		num++;
	}
#ifdef TIMER
time_calc = clock() - time_start;
printf("calc %.3f sec\n", (double)time_calc / CLOCKS_PER_SEC);
#endif
	printf("\nComplete file count\t: %d\n", c_num);
	printf("Damaged file count\t: %d\n", d_num);
	printf("Missing file count\t: %d\n", m_num);

	return err;
}

