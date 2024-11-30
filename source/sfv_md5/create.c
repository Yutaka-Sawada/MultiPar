// create.c
// Copyright : 2024-11-30 Yutaka Sawada
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
#include "create.h"
#include "phmd5.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// SFV ファイル
int create_sfv(
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	__int64 *prog_now,		// 経過表示での現在位置
	__int64 total_size)		// 合計ファイル・サイズ
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len, crc, time_last;
	__int64 file_size = 0, file_left;
	HANDLE hFile;

	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する

	// 読み込むファイルを開く
	wcscpy(uni_buf, base_dir);
	wcscpy(uni_buf + base_len, file_name);
	hFile = CreateFile(uni_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		printf("\n");
		print_win32_err();
		return 1;
	}
	if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
		printf("\n");
		print_win32_err();
		CloseHandle(hFile);
		return 1;
	}
	file_left = file_size;

	// CRC を計算する
	crc = 0xFFFFFFFF;	// 初期化
	while (file_left > 0){
		len = IO_SIZE;
		if (file_left < IO_SIZE)
			len = (unsigned int)file_left;
		if (!ReadFile(hFile, buf, len, &rv, NULL) || (len != rv)){
			printf("\n");
			print_win32_err();
			CloseHandle(hFile);
			return 1;
		}
		file_left-= len;
		(*prog_now) += len;
		// CRC-32 を更新する
		crc = crc_update(crc, buf, len);

		// 経過表示
		if (GetTickCount() / UPDATE_TIME != time_last){
			if (print_progress((int)(((*prog_now) * 1000) / total_size))){
				CloseHandle(hFile);
				return 2;
			}
			time_last = GetTickCount() / UPDATE_TIME;
		}
	}
	crc ^= 0xFFFFFFFF;	// 最終処理
	CloseHandle(hFile);

	// チェックサムを記録する
	len = wcslen(file_name);
	for (rv = 0; rv < len; rv++){
		if (file_name[rv] == ' ')
			break;
	}
	if (rv < len){
		uni_buf[0] = '"';
		wcscpy(uni_buf + 1, file_name);	// 「"」で囲む
		uni_buf[len + 1] = '"';
		uni_buf[len + 2] = 0;
	} else {
		wcscpy(uni_buf, file_name);	// 変換前にコピーする
	}
	unix_directory(uni_buf);
	add_text(uni_buf);
	wsprintf(uni_buf, L" %08X\r\n", crc);
	add_text(uni_buf);

	return 0;
}

// MD5 ファイル
int create_md5(
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	__int64 *prog_now,		// 経過表示での現在位置
	__int64 total_size)		// 合計ファイル・サイズ
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len, time_last;
	__int64 file_size = 0, file_left;
	HANDLE hFile;
	PHMD5 ctx;

	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する

	// 読み込むファイルを開く
	wcscpy(uni_buf, base_dir);
	wcscpy(uni_buf + base_len, file_name);
	hFile = CreateFile(uni_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		printf("\n");
		print_win32_err();
		return 1;
	}
	if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
		printf("\n");
		print_win32_err();
		CloseHandle(hFile);
		return 1;
	}
	file_left = file_size;

	// MD5 を計算する
	Phmd5Begin(&ctx);	// 初期化
	while (file_left > 0){
		len = IO_SIZE;
		if (file_left < IO_SIZE)
			len = (unsigned int)file_left;
		if (!ReadFile(hFile, buf, len, &rv, NULL) || (len != rv)){
			printf("\n");
			print_win32_err();
			CloseHandle(hFile);
			return 1;
		}
		file_left-= len;
		(*prog_now) += len;
		// MD5 を更新する
		Phmd5Process(&ctx, buf, len);

		// 経過表示
		if (GetTickCount() / UPDATE_TIME != time_last){
			if (print_progress((int)(((*prog_now) * 1000) / total_size))){
				CloseHandle(hFile);
				return 2;
			}
			time_last = GetTickCount() / UPDATE_TIME;
		}
	}
	Phmd5End(&ctx);	// 最終処理
	CloseHandle(hFile);

	// チェックサムを記録する
	wsprintf(uni_buf, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X *",
		ctx.hash[0], ctx.hash[1], ctx.hash[2], ctx.hash[3], ctx.hash[4], ctx.hash[5], ctx.hash[6], ctx.hash[7],
		ctx.hash[8], ctx.hash[9], ctx.hash[10], ctx.hash[11], ctx.hash[12], ctx.hash[13], ctx.hash[14], ctx.hash[15]);
	add_text(uni_buf);
	wcscpy(uni_buf, file_name);	// 変換前にコピーする
	unix_directory(uni_buf);
	add_text(uni_buf);
	add_text(L"\r\n");

	return 0;
}

