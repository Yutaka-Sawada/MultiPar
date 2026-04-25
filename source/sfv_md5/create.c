// create.c
// Copyright : 2026-04-23 Yutaka Sawada
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
#include <bcrypt.h>

#include "common.h"
#include "crc.h"
#include "create.h"
#include "phmd5.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// SFV ファイル
int create_sfv(
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	unsigned int *time_last,
	__int64 *prog_now,		// 経過表示での現在位置
	__int64 total_size)		// 合計ファイル・サイズ
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len, crc;
	__int64 file_size = 0, file_left;
	HANDLE hFile;

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
		if (GetTickCount() / UPDATE_TIME != (*time_last)){
			if (print_progress((int)(((*prog_now) * 1000) / total_size))){
				CloseHandle(hFile);
				return 2;
			}
			(*time_last) = GetTickCount() / UPDATE_TIME;
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
	unsigned int *time_last,
	__int64 *prog_now,		// 経過表示での現在位置
	__int64 total_size)		// 合計ファイル・サイズ
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;
	__int64 file_size = 0, file_left;
	HANDLE hFile;
	PHMD5 ctx;

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
		if (GetTickCount() / UPDATE_TIME != (*time_last)){
			if (print_progress((int)(((*prog_now) * 1000) / total_size))){
				CloseHandle(hFile);
				return 2;
			}
			(*time_last) = GetTickCount() / UPDATE_TIME;
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

// FLAC Fingerprint ファイル
int create_ffp(
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	__int64 *prog_now)		// 経過表示での現在位置
{
	unsigned char buf[42];
	unsigned int rv;
	__int64 file_size;
		HANDLE hFile;

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
	(*prog_now) += file_size;

	// streaminfo metadata block (42バイト) を読み取る
	if (!ReadFile(hFile, buf, 42, &rv, NULL) || (42 != rv)){
		printf("\n");
		print_win32_err();
		CloseHandle(hFile);
		return 1;
	}
	CloseHandle(hFile);

	// フォーマットを確認する
	if ((buf[0] != 0x66) || (buf[1] != 0x4C) || (buf[2] != 0x61) || (buf[3] != 0x43)){ // fLaC
		printf("\ninvalid FLAC format, %S\n", file_name);
		return 1;
	}
	if (((buf[4] & 0x7F) != 0) || (buf[5] != 0) || (buf[6] != 0) || (buf[7] != 34)){ // streaminfo metadata のサイズ
		printf("\ninvalid FLAC format, %S\n", file_name);
		return 1;
	}

	// ファイル・パス名がスペースを含んでいても「"」で囲まず、ファイル名だけにする
	get_file_name(file_name, uni_buf);
	add_text(uni_buf);

	// FLAC Fingerprint を記録する
	wsprintf(uni_buf, L":%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\r\n",
		buf[26], buf[27], buf[28], buf[29], buf[30], buf[31], buf[32], buf[33],
		buf[34], buf[35], buf[36], buf[37], buf[38], buf[39], buf[40], buf[41]);
	add_text(uni_buf);

	return 0;
}

#define NT_SUCCESS(Status)  (((NTSTATUS)(Status)) >= 0)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)

// Cryptography API: Next Generation が対応するハッシュ
int create_cng(
	int hash_type,			// 4 = SHA=1, 5 = SHA-256, 6 = SHA-384, 7 = SHA-512
	wchar_t *uni_buf,
	wchar_t *file_name,		// 検査対象のファイル名
	unsigned int *time_last,
	__int64 *prog_now,		// 経過表示での現在位置
	__int64 total_size)		// 合計ファイル・サイズ
{
	unsigned char buf[IO_SIZE], bHash[MAX_HASH];
	unsigned int rv, len;
	__int64 file_size = 0, file_left;
	HANDLE hFile;
	BCRYPT_ALG_HANDLE  hAlg  = NULL;
	BCRYPT_HASH_HANDLE hHash = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	DWORD cbHash = 0;
	DWORD cbHashObject = 0;
	PBYTE pbHashObject = NULL;
	LPCWSTR pszAlgId;

	// open an algorithm handle
	if (hash_type == 4){
		pszAlgId = BCRYPT_SHA1_ALGORITHM;
	} else if (hash_type == 5){
		pszAlgId = BCRYPT_SHA256_ALGORITHM;
	} else if (hash_type == 6){
		pszAlgId = BCRYPT_SHA384_ALGORITHM;
	} else if (hash_type == 7){
		pszAlgId = BCRYPT_SHA512_ALGORITHM;
	} else {	// 未知の形式
		printf("file format is unknown\n");
		return 1;
	}
	status = BCryptOpenAlgorithmProvider(&hAlg, pszAlgId, NULL, 0);
	if (!NT_SUCCESS(status)){
		printf("\nError 0x%x returned by BCryptOpenAlgorithmProvider\n", status);
		rv = 1;
		goto Cleanup;
	}

	// calculate the size of the buffer to hold the hash object
	status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &rv, 0);
	if (!NT_SUCCESS(status)){
		printf("\nError 0x%x returned by BCryptGetProperty\n", status);
		rv = 1;
		goto Cleanup;
	}

	// allocate the hash object on the heap
	pbHashObject = (PBYTE)HeapAlloc (GetProcessHeap(), 0, cbHashObject);
	if (NULL == pbHashObject){
		printf("\nError %u memory allocation failed\n", cbHashObject);
		rv = 1;
		goto Cleanup;
	}

	// calculate the length of the hash
	status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, sizeof(DWORD), &rv, 0);
	if (!NT_SUCCESS(status) || (cbHash > MAX_HASH)){
		printf("\nError 0x%x returned by BCryptGetProperty\n", status);
		rv = 1;
		goto Cleanup;
	}

	// create a hash
	status = BCryptCreateHash(hAlg, &hHash, pbHashObject, cbHashObject, NULL, 0, 0);
	if (!NT_SUCCESS(status)){
		printf("\nError 0x%x returned by BCryptCreateHash\n", status);
		rv = 1;
		goto Cleanup;
	}

	// 読み込むファイルを開く
	wcscpy(uni_buf, base_dir);
	wcscpy(uni_buf + base_len, file_name);
	hFile = CreateFile(uni_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		printf("\n");
		print_win32_err();
		rv = 1;
		goto Cleanup;
	}
	if (!GetFileSizeEx(hFile, (PLARGE_INTEGER)&file_size)){
		printf("\n");
		print_win32_err();
		CloseHandle(hFile);
		rv = 1;
		goto Cleanup;
	}
	file_left = file_size;

	// ハッシュ値を計算する
	while (file_left > 0){
		len = IO_SIZE;
		if (file_left < IO_SIZE)
			len = (unsigned int)file_left;
		if (!ReadFile(hFile, buf, len, &rv, NULL) || (len != rv)){
			printf("\n");
			print_win32_err();
			CloseHandle(hFile);
			rv = 1;
			goto Cleanup;
		}
		file_left-= len;
		(*prog_now) += len;

		// hash some data ハッシュ関数にデータを入力する
		status = BCryptHashData(hHash, (PBYTE)buf, len, 0);
		if (!NT_SUCCESS(status)){
			printf("\nError 0x%x returned by BCryptHashData\n", status);
			CloseHandle(hFile);
			rv = 1;
			goto Cleanup;
		}

		// 経過表示
		if (GetTickCount() / UPDATE_TIME != (*time_last)){
			if (print_progress((int)(((*prog_now) * 1000) / total_size))){
				CloseHandle(hFile);
				rv = 2;
				goto Cleanup;
			}
			(*time_last) = GetTickCount() / UPDATE_TIME;
		}
	}
	CloseHandle(hFile);

	// close the hash ハッシュ値の算出
	status = BCryptFinishHash(hHash, bHash, cbHash, 0);
	if (!NT_SUCCESS(status)){
		printf("\nError 0x%x returned by BCryptFinishHash\n", status);
		rv = 1;
		goto Cleanup;
	}

	// チェックサムを記録する
	for (len = 0; len < cbHash; len++)
		wsprintf(uni_buf + len * 2, L"%02X", bHash[len]);
	uni_buf[len * 2] = ' ';
	uni_buf[len * 2 + 1] = '*';
	uni_buf[len * 2 + 2] = 0;	// ハッシュ値の後に「 *」を追加する
	add_text(uni_buf);
	wcscpy(uni_buf, file_name);	// 変換前にコピーする
	unix_directory(uni_buf);
	add_text(uni_buf);
	add_text(L"\r\n");

	rv = 0;	// success mark
Cleanup:
	if (hAlg)
		BCryptCloseAlgorithmProvider(hAlg, 0);
	if (hHash)
		BCryptDestroyHash(hHash);
	if (pbHashObject)
		HeapFree(GetProcessHeap(), 0, pbHashObject);
	return rv;
}

