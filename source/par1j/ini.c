// ini.c
// Copyright : 2022-01-15 Yutaka Sawada
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

#include <malloc.h>
#include <stdio.h>

#include <windows.h>

#include "common1.h"
#include "ini.h"

/*
ファイル名は 1_#.bin (2+32+4 = 38文字)
「#」部分は Set ID を 16進数表記にする。

ファイル・フォーマット
<!-- 検査結果ファイル識別用 -->
2: 検査結果の書式バージョン
4: 6バイト目以降からの CRC-32 チェックサム
<!-- リカバリ・セットの識別用 -->
4: ソース・ブロック数 (PAR1ではブロックを持つファイルだけが Set ID に反映される)

<!-- 検査状態をファイルごとに記録する -->
前半 13バイトでファイル項目を識別して、後半 17バイトに状態を保存する。
1: 0～255=ソース・ファイル番号 (リカバリ・ファイルは常に 255)
4: ボリュームのシリアル番号
8: ファイルのオブジェクトID
8: ソース・ファイルのサイズ
4: ソース・ファイルの作成日時
4: ソース・ファイルの更新日時
1: ファイルの状態 0=完全, 1=破損あるいはエラー, 4=追加, +16=リカバリ・ファイル

*/

#define INI_VERSION	0x1270	// 検査結果の書式が決まった時のバージョン
#define REUSE_MIN	16384	// ファイル・サイズがこれより大きければ検査結果を利用する (16KB 以上 2GB 未満)
#define HEADER_SIZE	10
#define STATE_SIZE	30
#define STATE_READ	136
//#define VERBOSE		1		// 状態を冗長に出力する

static HANDLE hIniBin = NULL;	// バイナリ・データ用の検査結果ファイル
static int ini_off;

// 検査結果をどのくらいの期間保存するか
int recent_data = 0;
/*
  0 = 検査結果の再利用機能を無効にする(読み込まないし、記録もしない)
1～7= 前回の検査結果を読み込んで、今回のを記録する。
      指定された期間よりも経過した古い記録は削除される。
      1= 1日, 2= 3日, 3= 1週間, 4= 半月, 5= 1ヶ月, 6= 1年, 7= 無制限
 +8 = 同じセットの記録を削除する、今回の結果は記録する。
      他のセットは指定された期間よりも古いものだけ削除する。
*/

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// File Time (UTC) を UNIX Time (UTC) に変換する
static unsigned int time_f_u(FILETIME *file_time)
{
	unsigned __int64 int8;

	// 1970/01/01 の File Time との差を求める
	memcpy(&int8, file_time, 8);
	int8 = (int8 - 116444736000000000) / 10000000;

	return (unsigned int)int8;
}

#ifdef VERBOSE
// File Time (UTC) を日付の文字列に変換する
static void time_f_date(FILETIME *ft, char *buf)
{
	int len;
	SYSTEMTIME st_utc, st;

	FileTimeToSystemTime(ft, &st_utc);
	if (SystemTimeToTzSpecificLocalTime(NULL, &st_utc, &st) == 0)
		memcpy(&st, &st_utc, sizeof(SYSTEMTIME));
	len = GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE | LOCALE_USE_CP_ACP, &st, NULL, buf, 32);
	if (len > 0){
		buf[len - 1] = ' ';
		len = GetTimeFormatA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, &st, NULL, buf + len, 32 - len);
	}
	if (len == 0){
		wsprintfA(buf, "%4d/%02d/%02d %2d:%02d:%02d",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	}
}
#endif

// 2GB 未満のファイルの開始位置以降のハッシュ値を計算する
static unsigned int file_crc_part(HANDLE hFile)
{
	unsigned char buf[4096], *p;
	unsigned int rv, crc = 0xFFFFFFFF;
	unsigned int len, tmp, i;

	// 末尾まで読み込む
	do {
		if (!ReadFile(hFile, buf, 4096, &rv, NULL) || (rv == 0))
			break;
		// CRC-32 計算
		p = buf;
		len = rv;
		while (len--){
			tmp = (*p++);
			for (i = 0; i < 8; i++){
				if ((tmp ^ crc) & 1){
					crc = (crc >> 1) ^ 0xEDB88320;
				} else {
					crc = crc >> 1;
				}
				tmp = tmp >> 1;
			}
		}
	} while (rv > 0);

	return crc ^ 0xFFFFFFFF;
}

// .BIN ファイルの offset バイト目から size バイトのデータを buf に読み込む
static int file_read_bin(
	int offset,
	unsigned char *buf,
	unsigned int size)
{
	unsigned int rv;

	// ファイルの位置を offsetバイト目にする
	rv = SetFilePointer(hIniBin, offset, NULL, FILE_BEGIN);
	if (rv == INVALID_SET_FILE_POINTER)
		return 1;

	// size バイトを読み込む
	if ((!ReadFile(hIniBin, buf, size, &rv, NULL)) || (size != rv))
		return 1;	// 指定サイズを読み込めなかったらエラーになる

	return 0;
}

// .BIN ファイルの offset バイト目に size バイトのデータを buf から書き込む
static int file_write_bin(
	int offset,
	unsigned char *buf,
	unsigned int size)
{
	unsigned int rv;

	// ファイルの位置を offsetバイト目にする
	rv = SetFilePointer(hIniBin, offset, NULL, FILE_BEGIN);
	if (rv == INVALID_SET_FILE_POINTER)
		return 1;

	// size バイトを書き込む
	if (!WriteFile(hIniBin, buf, size, &rv, NULL))
		return 1;

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 検査結果ファイルを利用する為の関数

// ini_path で指定されてる検査結果ファイルを削除する
static void delete_ini_file(void)
{
	// 開いてるなら閉じる
	if (hIniBin != NULL){
		CloseHandle(hIniBin);
		hIniBin = NULL;
	}

	DeleteFile(ini_path);	// .bin ファイルを削除する

	recent_data = 0;	// これ以降は検査結果は利用できない
}

// 検査するチェックサム・ファイルが同じであれば、再検査する必要は無い
int check_ini_file(unsigned char *set_hash, int block_num)
{
	unsigned char set_data[HEADER_SIZE * 2];
	wchar_t path[MAX_LEN], ini_name[INI_NAME_LEN + 1];
	int i, match;
	unsigned int file_time, time_now, time_limit;
	HANDLE hFind;
	WIN32_FIND_DATA FindData;
	FILETIME ft;

	if (recent_data == 0)
		return 0;

	// 設定ファイルのパス
	swprintf(ini_name, _countof(ini_name), L"1_%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X.bin",
		set_hash[0], set_hash[1], set_hash[2], set_hash[3], set_hash[4], set_hash[5], set_hash[6], set_hash[7],
		set_hash[8], set_hash[9], set_hash[10], set_hash[11],set_hash[12], set_hash[13], set_hash[14], set_hash[15]);

	// 期限を設定する
	switch (recent_data & 7){
	case 1:
		time_limit = 3600 * 24;	// 1 day
		break;
	case 2:
		time_limit = 3600 * 24 * 3;	// 3 days
		break;
	case 3:
		time_limit = 3600 * 24 * 7;	// 1 week
		break;
	case 4:
		time_limit = 3600 * 24 * 15;	// half month
		break;
	case 5:
		time_limit = 3600 * 24 * 30;	// 1 month
		break;
	case 6:
		time_limit = 3600 * 24 * 365;	// 1 year
		break;
	case 7:
		time_limit = 0xFFFFFFFF;	// unlimited
		break;
	default:	// 期間が指定されてない
		recent_data = 0;
		return 0;
	}
	GetSystemTimeAsFileTime(&ft);	// 現在のシステム時刻 (UTC) を取得する
	time_now = time_f_u(&ft);	// UNIX Time に変換する

	// 記録の日時を調べて、古いのは削除する
	match = 0;
	wcscpy(path, ini_path);
	wcscat(path, L"1_*.bin");	// 文字数は後でチェックする
	hFind = FindFirstFile(path, &FindData);
	if (hFind != INVALID_HANDLE_VALUE){
		do {
			if (((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) &&
					(wcslen(FindData.cFileName) == INI_NAME_LEN)){
				// 指定時間より古い記録は削除する
				file_time = time_f_u(&(FindData.ftLastWriteTime));	// 更新日時
				if (time_now - file_time > time_limit){
					wcscpy(path, ini_path);
					wcscat(path, FindData.cFileName);
					DeleteFile(path);
#if VERBOSE > 1
					time_f_date(&(FindData.ftLastWriteTime), (char *)path);
					printf("Delete result: %S, %s\n", FindData.cFileName, (char *)path);
#endif
				} else if (wcscmp(FindData.cFileName, ini_name) == 0){
					if (recent_data & 8){	// 検査結果を読み込まないが、今回のは書き込む場合
						// 以前の検査結果が存在すれば削除する
						wcscpy(path, ini_path);
						wcscat(path, ini_name);
						DeleteFile(path);
#if VERBOSE > 1
						printf("Delete result: %S\n", ini_name);
#endif
					} else {
						match = 1;	// 設定ファイルが存在する
					}
#ifdef VERBOSE
					ft = FindData.ftLastWriteTime;	// 検査結果の更新日時
#endif
				}
			}
		} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
		FindClose(hFind);
	}

	// バイナリ・データ用の設定ファイルを開く
	wcscat(ini_path, ini_name);
	hIniBin = CreateFile(ini_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS, NULL);
	if (hIniBin == INVALID_HANDLE_VALUE){
		hIniBin = NULL;
		recent_data = 0;	// 開けない場合は再利用しない
		return 0;
	}
	//printf("ini file path = %S\n", ini_path);

	// 既に同名の (Set ID が同じ) ファイルが存在する場合
	set_data[0] = INI_VERSION >> 8;	// 検査結果の書式バージョン
	set_data[1] = INI_VERSION & 0xFF;
	memset(set_data +  2, 0, 4);	// CRC-32 は後で書き込む
	memcpy(set_data +  6, &block_num, 4);
	if (match != 0){	// ID が同じでも Set 内容が異なる場合は初期化する
		i = file_read_bin(0, set_data + HEADER_SIZE, HEADER_SIZE);
		if ((set_data[HEADER_SIZE    ] != (INI_VERSION >> 8)) ||
			(set_data[HEADER_SIZE + 1] != (INI_VERSION & 0xFF))){
			i = 1;	// 古いバージョンの検査結果は参照しない
		}
		if (i == 0){	// 検査結果が破損してないか確かめる
			if (SetFilePointer(hIniBin, 6, 0, FILE_BEGIN) != INVALID_SET_FILE_POINTER)
				time_now = file_crc_part(hIniBin);
			i = memcmp(&time_now, set_data + (HEADER_SIZE + 2), 4);
		}
		if (i == 0)	// ソース・ブロック数を比較する
			i = memcmp(set_data + 6, set_data + (HEADER_SIZE + 6), HEADER_SIZE - 6);
		if (i != 0)	// ID が同じでも Set 内容が異なる場合は初期化する
			match = 0;
	}

	// 一致しなかった場合は今回のデータを記録する
	if (match == 0){
		// 今回のデータを書き込む
		if (file_write_bin(0, set_data, HEADER_SIZE) != 0){
			delete_ini_file();
			return 0;
		}
		if (SetEndOfFile(hIniBin) == 0){
			delete_ini_file();
			return 0;
		}
		return 0;
#ifdef VERBOSE
	} else {
		time_f_date(&ft, (char *)path);
		printf("Date of result: %s\n", (char *)path);
#endif
	}

	return 1;
}

// 検査結果ファイルを閉じる
void close_ini_file(void)
{
	if ((recent_data != 0) && (hIniBin != NULL)){
		unsigned int new_crc, old_crc;

		// 閉じる前に検査結果の CRC-32 を計算して記録しておく
		FlushFileBuffers(hIniBin);
		file_read_bin(2, (unsigned char *)&old_crc, 4);
		new_crc = file_crc_part(hIniBin);
		if (new_crc != old_crc)	// 検査結果が同じなら更新しない
			file_write_bin(2, (unsigned char *)&new_crc, 4);
		CloseHandle(hIniBin);
		hIniBin = NULL;
	}
}

// 検査結果が記録されてるかどうか
// -1=エラー, -2=記録なし、またはファイル項目はあるが状態が変化してる
// ファイルの状態 0=完全, 1=破損, 2=追加
int check_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	HANDLE hFile)			// そのファイルのハンドル
{
	unsigned char buf[STATE_SIZE * STATE_READ], buf2[STATE_SIZE];
	unsigned int rv, off;
	BY_HANDLE_FILE_INFORMATION fi;

	// 現在のファイル属性を取得する
	memset(&fi, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	if (GetFileInformationByHandle(hFile, &fi) != 0){
		meta[0] = fi.nFileSizeLow;
		meta[1] = fi.nFileSizeHigh;
		if ((recent_data == 0) || ((fi.nFileSizeLow <= REUSE_MIN) && (fi.nFileSizeHigh == 0)))
			return -2;	// 小さなファイルは記録を参照しない
		meta[2] = time_f_u(&(fi.ftCreationTime));
		meta[3] = time_f_u(&(fi.ftLastWriteTime));
		meta[4] = fi.dwVolumeSerialNumber;
		meta[5] = fi.nFileIndexLow;
		meta[6] = fi.nFileIndexHigh;
	} else {
		return -1;	// 属性の読み取りエラー
	}

	// 参照するファイル項目
	buf2[0] = (unsigned char)(num & 0xFF);
	memcpy(buf2 + 1, meta + 4, 12);
	memcpy(buf2 + 13, meta, 16);
	buf2[29] = (unsigned char)(num >> 8);

	// ヘッダーの直後から開始する
	if (SetFilePointer(hIniBin, HEADER_SIZE, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER){
		delete_ini_file();
		return -2;
	}
	ini_off = HEADER_SIZE;	// ファイル項目の位置
	// 一度に STATE_READ 個ずつ読み込む
	while (ReadFile(hIniBin, buf, STATE_SIZE * STATE_READ, &rv, NULL) != 0){
		if (rv < STATE_SIZE)
			break;
		// ファイル識別番号から同じファイルの記録を探す
		for (off = 0; off < rv; off += STATE_SIZE){
			if ((memcmp(buf + off, buf2, 13) == 0) && ((buf[off + 29] & 0x10) == buf2[29])){
				ini_off += off;	// 同じファイルの記録があった
#ifdef VERBOSE
				printf("check state, num = %d, offset = %d, state = 0x%02X\n", num & 0xFF, ini_off, buf[off + 29]);
#endif
				// ファイルのサイズと日時を比較する
				if (memcmp(buf + (off + 13), buf2 + 13, 16) == 0)
					return buf[off + 29] & 0x0F;
				return -2;	// 状態が変化してる
			}
		}
		ini_off += rv;
	}

	ini_off = 0;
	return -2;	// これ以上の検査記録は無い
}

// ソース・ファイル状態は完全か破損だけ (消失だと検査しない)
void write_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	int state)				// 状態、0=完全, 1=破損, 2=追加
{
	unsigned char buf[STATE_SIZE];

	if ((recent_data == 0) || ((meta[0] <= REUSE_MIN) && (meta[1] == 0)))
		return;	// 小さなファイルは検査結果を記録しない

	if (ini_off == 0){	// 記録が無ければ末尾に追加する
		ini_off = GetFileSize(hIniBin, NULL);
		if (ini_off == INVALID_FILE_SIZE){
			delete_ini_file();
			return;
		}
	}

	// 今回の状態を書き込む
	buf[0] = (unsigned char)(num & 0xFF);
	memcpy(buf + 1, meta + 4, 12);
	memcpy(buf + 13, meta, 16);
	buf[29] = (unsigned char)((state & 0x0F) | (num >> 8));
#ifdef VERBOSE
	printf("write state, num = %d, offset = %d, state = 0x%02X\n", num & 0xFF, ini_off, buf[29]);
#endif
	if (file_write_bin(ini_off, buf, STATE_SIZE) != 0){
		delete_ini_file();
		return;
	}
}

