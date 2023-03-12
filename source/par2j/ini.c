// ini.c
// Copyright : 2022-10-12 Yutaka Sawada
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

#include "common2.h"
#include "crc.h"
#include "md5_crc.h"
#include "ini.h"

/*
ファイル名は 2_#.bin (2+32+4 = 38文字)
「#」部分は Set ID を 16進数表記にする。

ファイル・フォーマット
<!-- 検査結果ファイル識別用 -->
2: 検査結果の書式バージョン
4: 6バイト目以降からの CRC-32 チェックサム
4: .INI ファイルの CRC-32 チェックサム
<!-- リカバリ・セットの識別用 -->
4: ブロック・サイズ (32-bit 以下)
4: 合計ファイル数
4: リカバリ・セットのファイル数
4: ファイル名用に確保される領域の文字数 (全てのファイル情報を書き込んだかどうかの識別)
<!-- リカバリ・セット内に記録されているソース・ファイルのデータを繰り返す -->
8: ファイル・サイズ
16: MD5 ハッシュ値
16: MD5-16k ハッシュ値
2: ファイル名 (UTF-8) のバイト数 (末尾の null 文字は含めない)
1～MAX_LEN *3: UTF-8 のファイル名 (末尾の null 文字は含めない)
<!-- ここまでソース・ファイルの数だけ繰り返す -->

<!-- リカバリ・ファイルの情報はファイルごとに記録する -->
8: リカバリ・ファイルのサイズ
4: リカバリ・ファイルの作成日時
4: リカバリ・ファイルの更新日時
4: リカバリ・ファイル内に含まれるパケットの数
4: リカバリ・ファイル内に含まれるパリティ・ブロックの数
1: リカバリ・ファイルの状態 0xFF=未検査, 0=完全, その他=何らかのエラー
<!-- リカバリ・ファイル内に含まれるパリティ・ブロックのデータを繰り返す -->
2: パリティ・ブロックの番号
8: パリティ・ブロックの開始位置
<!-- ここまでパリティ・ブロックの数だけ繰り返す -->

<!-- ソース・ブロックのチェックサムは一箇所に記録する -->
20 * ソース・ブロックの数: MD5 & CRC-32 を繰り返す

<!-- ソース・ブロックのチェックサムが足りなくても記録する -->
1 * ソース・ファイルの数: そのファイルのチェックサムが欠落してるか (0xFF=欠落)
20 * ソース・ブロックの数: MD5 & CRC-32 を繰り返す

<!-- ソース・ファイルのハッシュ値の検査結果は候補ごとに記録する -->
8: ファイルの現在のサイズ (ソース・ファイル本来のサイズとは別)
4: ファイルの作成日時
4: ファイルの更新日時
4: ファイルの検査結果 0～=何ブロック目まで一致, -3=完全に一致

<!-- 破損ファイルの検査状態はファイル単位で記録する -->
8: ファイルの現在のサイズ
4: ファイルの作成日時
4: ファイルの更新日時
4: 記録したソース・ブロックの項目数
<!-- ファイル内に含まれるソース・ブロックのデータを繰り返す -->
2: ソース・ブロックの番号
8: ソース・ブロックの開始位置
1: ソース・ブロックの検出状態 0=完全, 1=エラー訂正が必要, 8/9=分割されたブロック
<!-- ここまでソース・ブロックの数だけ繰り返す -->

.INI ファイルの内容 (UTF-16でエンコードされたテキスト)
[Set]
Creator = そのままで記録する
Comment = 浄化してから記録する
Checksum = ソース・ブロックのチェックサムの開始位置

[PAR]
ファイル識別番号 = リカバリ・ファイル情報の開始位置

[State]
ソース・ファイル番号とファイル識別番号 = ファイルの状態とハッシュ値の検査結果

[Damage] と [Simple]
ソース・ファイル番号とファイル識別番号 = ファイル状態とソース・ブロック情報の開始位置
(比較対象が指定されて無いなら、ソース・ファイル番号を記録しない)

*/

#define INI_VERSION	0x1270	// 検査結果の書式が決まった時のバージョン
#define REUSE_MIN	16384	// ファイル・サイズがこれより大きければ検査結果を利用する (16KB 以上 2GB 未満)
#define HEADER_SIZE	26
#define HEADER_EACH	42
#define PARITY_SIZE	25
#define PARITY_EACH	10
#define CHKSUM_EACH	20
#define STATE_SIZE	20
#define STATE_COUNT	5
#define SOURCE_EACH	11
#define MAX_EACH	512
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
+16 = そのファイルの検査結果だけ参照・記録する
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

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
BASE32 でバイト配列を文字列にする
12-byte -> 96-bit -> 5-bit * 20
15-byte -> 120-bit -> 5-bit * 24
*/
static void base32_encode3(
	wchar_t *buf,		// 最大で 21文字分必要 (末尾の null 文字含めて)
	unsigned int v2,
	unsigned int v1,
	unsigned int v0)
{
	wchar_t base32_table[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

	buf[ 0] = base32_table[ v2 >> 31        ];
	buf[ 1] = base32_table[(v2 >> 26) & 0x1F];
	buf[ 2] = base32_table[(v2 >> 21) & 0x1F];
	buf[ 3] = base32_table[(v2 >> 16) & 0x1F];
	buf[ 4] = base32_table[(v2 >> 11) & 0x1F];
	buf[ 5] = base32_table[(v2 >>  6) & 0x1F];
	buf[ 6] = base32_table[(v2 >>  1) & 0x1F];
	buf[ 7] = base32_table[((v2 << 4) | (v1 >> 28)) & 0x1F];
	buf[ 8] = base32_table[(v1 >> 23) & 0x1F];
	buf[ 9] = base32_table[(v1 >> 18) & 0x1F];
	buf[10] = base32_table[(v1 >> 13) & 0x1F];
	buf[11] = base32_table[(v1 >>  8) & 0x1F];
	buf[12] = base32_table[(v1 >>  3) & 0x1F];
	buf[13] = base32_table[((v1 << 2) | (v0 >> 30)) & 0x1F];
	buf[14] = base32_table[(v0 >> 25) & 0x1F];
	buf[15] = base32_table[(v0 >> 20) & 0x1F];
	buf[16] = base32_table[(v0 >> 15) & 0x1F];
	buf[17] = base32_table[(v0 >> 10) & 0x1F];
	buf[18] = base32_table[(v0 >>  5) & 0x1F];
	buf[19] = base32_table[ v0        & 0x1F];
	buf[20] = 0;
}

static void base32_encode4(
	wchar_t *buf,		// 最大で 25文字分必要 (末尾の null 文字含めて)
	unsigned int v3,	// ファイル番号は 3バイトまで
	unsigned int v2,
	unsigned int v1,
	unsigned int v0)
{
	wchar_t base32_table[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

	v3 += 1;	// num = 0 でも base32_encode3 と見分けが付くように +1 する

	buf[ 0] = base32_table[(v3 >> 19) & 0x1F];
	buf[ 1] = base32_table[(v3 >> 14) & 0x1F];
	buf[ 2] = base32_table[(v3 >>  9) & 0x1F];
	buf[ 3] = base32_table[(v3 >>  4) & 0x1F];
	buf[ 4] = base32_table[((v3 << 1) | (v2 >> 31)) & 0x1F];
	buf[ 5] = base32_table[(v2 >> 26) & 0x1F];
	buf[ 6] = base32_table[(v2 >> 21) & 0x1F];
	buf[ 7] = base32_table[(v2 >> 16) & 0x1F];
	buf[ 8] = base32_table[(v2 >> 11) & 0x1F];
	buf[ 9] = base32_table[(v2 >>  6) & 0x1F];
	buf[10] = base32_table[(v2 >>  1) & 0x1F];
	buf[11] = base32_table[((v2 << 4) | (v1 >> 28)) & 0x1F];
	buf[12] = base32_table[(v1 >> 23) & 0x1F];
	buf[13] = base32_table[(v1 >> 18) & 0x1F];
	buf[14] = base32_table[(v1 >> 13) & 0x1F];
	buf[15] = base32_table[(v1 >>  8) & 0x1F];
	buf[16] = base32_table[(v1 >>  3) & 0x1F];
	buf[17] = base32_table[((v1 << 2) | (v0 >> 30)) & 0x1F];
	buf[18] = base32_table[(v0 >> 25) & 0x1F];
	buf[19] = base32_table[(v0 >> 20) & 0x1F];
	buf[20] = base32_table[(v0 >> 15) & 0x1F];
	buf[21] = base32_table[(v0 >> 10) & 0x1F];
	buf[22] = base32_table[(v0 >>  5) & 0x1F];
	buf[23] = base32_table[ v0        & 0x1F];
	buf[24] = 0;
}

// 圧縮する
static void compress_text(wchar_t *buf)
{
	wchar_t replace_table[] = L"!#$%&()*,-.:<>?@]^_`{|}~";
	int i, j, len;

	// 先頭 (最上位) の 0 (BASE32/64 の表記では A) を消去する
	if (buf[0] == 'A'){
		len = 1;	// A が何文字続くか
		while (buf[len] == 'A')
			len++;
		// 残りを先頭に移す
		for (i = len; buf[i] != 0; i++)
			buf[i - len] = buf[i];
		buf[i - len] = 0;
	}

	// 連続する 0 (BASE32/64 の表記では A) を一文字に置き換える
	for (j = 0; buf[j] != 0; j++){
		if ((buf[j] == 'A') && (buf[j + 1] == 'A')){
			len = 2;	// A が何文字続くか
			while (buf[j + len] == 'A')
				len++;
			// 24個の置換文字で 25連続まで対応できる
			buf[j] = replace_table[len - 2];
			// 残りを前に移す
			for (i = j + len; buf[i] != 0; i++)
				buf[i - len + 1] = buf[i];
			buf[i - len + 1] = 0;
		}
	}
}

// 置換文字の番号を返す
static int replace_decode(wchar_t c)
{
	// 置換用の文字は「 "';=[\」を除外して 24個
	wchar_t replace_table[] = L"!#$%&()*,-.:<>?@]^_`{|}~";
	int i;

	for (i = 0; i < 24; i++){
		if (replace_table[i] == c)
			break;
	}

	return i;	// 置換用の文字で無い場合は 24 が戻る
}

// 伸張する
static void expand_text(wchar_t *buf, int orig_count)
{
	int i, j, len, cur_count;

	cur_count = (int)wcslen(buf);

	// 置換された文字を探して、元に戻す
	for (j = 0; buf[j] != 0; j++){
		len = replace_decode(buf[j]) + 2;	// A が何文字続いてたのか
		if (len < 26){
			// 残りを後ろに移す
			for (i = cur_count; i > j; i--)
				buf[i + len - 1] = buf[i];
			// 連続した A に戻す
			for (i = j; i < j + len; i++)
				buf[i] = 'A';
			cur_count += len - 1;	// 文字数を増やしておく
			j += len - 1;
		}
	}

	if (cur_count < orig_count){	// 本来の文字数よりも短いなら
		len = orig_count - cur_count;
		// 残りを末尾に移す
		for (i = cur_count; i >= 0; i--)
			buf[i + len] = buf[i];
		// 先頭に A を追加する
		for (i = 0; i < len; i++)
			buf[i] = 'A';
	}
}

// ファイルのオブジェクトID + ボリュームのシリアル番号 を文字列に整形する
static void format_id(
	wchar_t *buf,
	int num,
	unsigned int id[3])
{
	if (num >= 0){	// ファイル番号を指定する
		base32_encode4(buf, num, id[2], id[1], id[0]);
	} else {
		base32_encode3(buf, id[2], id[1], id[0]);
	}
	compress_text(buf);
}

/*
BASE64 でバイト配列と文字列を変換する
19-byte -> 152-bit -> 6-bit * 26
*/
static void base64_encode5(
	wchar_t *buf,			// 最大で 27文字分必要 (末尾の null 文字含めて)
	unsigned int state[4],
	int result)			// 検査結果は 3バイトまで
{
	wchar_t base64_table[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned int v0, v1, v2, v3, v4; 

	v4 = state[1];	// 64-bit 整数のファイル・サイズの上位 (0 になることが多い)
	v3 = state[0];	// 64-bit 整数のファイル・サイズの下位
	v2 = state[2];	// 作成日時
	v1 = state[3] - state[2];	// 更新日時と作成日時の差分
	v0 = result + 3;	// 正の整数にするため、-3 -> 0, 0～ -> 3～ になる

	// 上位に多い 0 を先頭に置くため、ファイル・サイズと日時は Big endian で記録する
	buf[ 0] = base64_table[(v4 >> 26) & 0x3F];
	buf[ 1] = base64_table[(v4 >> 20) & 0x3F];
	buf[ 2] = base64_table[(v4 >> 14) & 0x3F];
	buf[ 3] = base64_table[(v4 >>  8) & 0x3F];
	buf[ 4] = base64_table[(v4 >>  2) & 0x3F];
	buf[ 5] = base64_table[((v4 << 4) | (v3 >> 28)) & 0x3F];
	buf[ 6] = base64_table[(v3 >> 22) & 0x3F];
	buf[ 7] = base64_table[(v3 >> 16) & 0x3F];
	buf[ 8] = base64_table[(v3 >> 10) & 0x3F];
	buf[ 9] = base64_table[(v3 >>  4) & 0x3F];
	buf[10] = base64_table[((v3 << 2) | (v2 >> 30)) & 0x3F];
	buf[11] = base64_table[(v2 >> 24) & 0x3F];
	buf[12] = base64_table[(v2 >> 18) & 0x3F];
	buf[13] = base64_table[(v2 >> 12) & 0x3F];
	buf[14] = base64_table[(v2 >>  6) & 0x3F];
	buf[15] = base64_table[ v2        & 0x3F];
	// 上位に多い 0 を末尾に置くため、日時の差分と v0 は Little endian で記録する
	buf[16] = base64_table[ v1        & 0x3F];
	buf[17] = base64_table[(v1 >>  6) & 0x3F];
	buf[18] = base64_table[(v1 >> 12) & 0x3F];
	buf[19] = base64_table[(v1 >> 18) & 0x3F];
	buf[20] = base64_table[(v1 >> 24) & 0x3F];
	buf[21] = base64_table[((v1 >> 30) | (v0 << 2)) & 0x3F];
	buf[22] = base64_table[(v0 >>  4) & 0x3F];
	buf[23] = base64_table[(v0 >> 10) & 0x3F];
	buf[24] = base64_table[(v0 >> 16) & 0x3F];
	buf[25] = base64_table[(v0 >> 22) & 0x3F];
	buf[26] = 0;

	//printf("0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X\n", v4, v3, v2, v1, v0);
	//printf("BASE64: %S\n", buf);
	compress_text(buf);
	//printf("Write : %S\n", buf);
}

// BASE64 文字の番号を返す
static int base64_decode(wchar_t c)
{
	wchar_t base64_table[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int i;

	for (i = 0; i < 64; i++){
		if (base64_table[i] == c)
			break;
	}

	return i;
}

static unsigned int base64_decode5(
	wchar_t *buf,			// 最大で 27文字分必要 (末尾の null 文字含めて)
	unsigned int state[4])
{
	unsigned int v0, v1, v2, v3, v4; 

	//printf("Read  : %S\n", buf);
	expand_text(buf, 26);
	//printf("BASE64: %S\n", buf);

	// ファイル・サイズと日時を Big endian で読み取る
	v4  = base64_decode(buf[ 0]) << 26;
	v4 |= base64_decode(buf[ 1]) << 20;
	v4 |= base64_decode(buf[ 2]) << 14;
	v4 |= base64_decode(buf[ 3]) <<  8;
	v4 |= base64_decode(buf[ 4]) <<  2;
	v3  = base64_decode(buf[ 5]);
	v4 |= v3 >> 4;
	v3  = v3 << 28;
	v3 |= base64_decode(buf[ 6]) << 22;
	v3 |= base64_decode(buf[ 7]) << 16;
	v3 |= base64_decode(buf[ 8]) << 10;
	v3 |= base64_decode(buf[ 9]) <<  4;
	v2  = base64_decode(buf[10]);
	v3 |= v2 >> 2;
	v2  = v2 << 30;
	v2 |= base64_decode(buf[11]) << 24;
	v2 |= base64_decode(buf[12]) << 18;
	v2 |= base64_decode(buf[13]) << 12;
	v2 |= base64_decode(buf[14]) <<  6;
	v2 |= base64_decode(buf[15]);
	// 日時の差分と v0 は Little endian で読み取る
	v1  = base64_decode(buf[16]);
	v1 |= base64_decode(buf[17]) <<  6;
	v1 |= base64_decode(buf[18]) << 12;
	v1 |= base64_decode(buf[19]) << 18;
	v1 |= base64_decode(buf[20]) << 24;
	v0  = base64_decode(buf[21]);
	v1 |= v0 << 30;
	v0  = v0 >> 2;
	v0 |= base64_decode(buf[22]) <<  4;
	v0 |= base64_decode(buf[23]) << 10;
	v0 |= base64_decode(buf[24]) << 16;
	v0 |= base64_decode(buf[25]) << 22;

	state[1] = v4;	// 64-bit 整数のファイル・サイズの上位
	state[0] = v3;	// 64-bit 整数のファイル・サイズの下位
	state[2] = v2;	// 作成日時
	state[3] = v2 + v1;
	//printf("0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X\n", v4, v3, v2, v1, v0);
	return v0 - 3;	// 正の整数にするため、-3 -> 0, 0～ -> 3～ になる
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 検査結果ファイルを利用する為の関数

// リカバリ・ファイルの新規作成時に、同じ Set ID の記録があれば消去しておく
void reset_ini_file(unsigned char *set_id)
{
	int i;
	wchar_t ini_name[INI_NAME_LEN + 1];

	// 設定ファイルのパス
	swprintf(ini_name, _countof(ini_name), L"2_%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X.bin",
		set_id[0], set_id[1], set_id[2], set_id[3], set_id[4], set_id[5], set_id[6], set_id[7],
		set_id[8], set_id[9], set_id[10], set_id[11],set_id[12], set_id[13], set_id[14], set_id[15]);

	// バイナリ・データ用の設定ファイルを消去する
	wcscat(ini_path, ini_name);
	DeleteFile(ini_path);	// .bin ファイルを削除する

	// .INI ファイルに拡張子を変えておく
	i = (int)wcslen(ini_path);
	ini_path[i - 3] = 'i';
	ini_path[i - 2] = 'n';
	ini_path[i - 1] = 'i';
	DeleteFile(ini_path);	// .ini ファイルを削除する
}

// ini_path で指定されてる検査結果ファイルを削除する
static void delete_ini_file(void)
{
	int len;

	// 開いてるなら閉じる
	if (hIniBin != NULL){
		CloseHandle(hIniBin);
		hIniBin = NULL;
	}

	DeleteFile(ini_path);	// .ini ファイルを削除する
	len = (int)wcslen(ini_path);
	ini_path[len - 3] = 'b';
	ini_path[len - 2] = 'i';
	ini_path[len - 1] = 'n';
	DeleteFile(ini_path);	// .bin ファイルを削除する

	recent_data = 0;	// これ以降は検査結果は利用できない
}

// 検査するリカバリ・ファイルが同じであれば、再検査する必要は無い
int check_ini_file(unsigned char *set_id)
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
	swprintf(ini_name, _countof(ini_name), L"2_%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X.bin",
		set_id[0], set_id[1], set_id[2], set_id[3], set_id[4], set_id[5], set_id[6], set_id[7],
		set_id[8], set_id[9], set_id[10], set_id[11],set_id[12], set_id[13], set_id[14], set_id[15]);

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
	wcscat(path, L"2_*.bin");	// 文字数は後でチェックする
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
					i = (int)wcslen(path);
					path[i - 3] = 'i';
					path[i - 2] = 'n';
					path[i - 1] = 'i';
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
						i = (int)wcslen(path);
						path[i - 3] = 'i';
						path[i - 2] = 'n';
						path[i - 1] = 'i';
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
	hIniBin = CreateFile(ini_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
					FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_FLAG_RANDOM_ACCESS, NULL);
	if (hIniBin == INVALID_HANDLE_VALUE){
		hIniBin = NULL;
		recent_data = 0;	// 開けない場合は再利用しない
		return 0;
	}
	// .INI ファイルに拡張子を変えておく
	i = (int)wcslen(ini_path);
	ini_path[i - 3] = 'i';
	ini_path[i - 2] = 'n';
	ini_path[i - 1] = 'i';
	//printf("ini file path = %S\n", ini_path);

	// 既に同名の (Set ID が同じ) ファイルが存在する場合
	set_data[0] = INI_VERSION >> 8;	// 検査結果の書式バージョン
	set_data[1] = INI_VERSION & 0xFF;
	memset(set_data +  2, 0, 8);	// CRC-32 は後で書き込む
	memcpy(set_data + 10, &block_size, 4);
	memcpy(set_data + 14, &file_num, 4);
	memcpy(set_data + 18, &entity_num, 4);
	memset(set_data + 22, 0, 4);	// 検査未了の印に 0 にしておく
	if (match == 1){
		i = file_read_data(hIniBin, 0, set_data + HEADER_SIZE, HEADER_SIZE);
		if ((set_data[HEADER_SIZE    ] != (INI_VERSION >> 8)) ||
			(set_data[HEADER_SIZE + 1] != (INI_VERSION & 0xFF))){
			i = 1;	// 古いバージョンの検査結果は参照しない
		}
		if (i == 0){	// 検査結果が破損してないか確かめる
			if (SetFilePointer(hIniBin, 6, 0, FILE_BEGIN) != INVALID_SET_FILE_POINTER)
				time_now = file_crc_part(hIniBin);
			i = memcmp(&time_now, set_data + (HEADER_SIZE + 2), 4);
			if (i == 0){	// .INI ファイルの破損も確かめる
				HANDLE hFile;
				time_limit = 0;
				hFile = CreateFile(ini_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile != INVALID_HANDLE_VALUE){
					time_limit = file_crc_part(hFile);
					CloseHandle(hFile);
				} else {	// .INI ファイルへの読み書きができなければ再利用しない
					CloseHandle(hIniBin);
					hIniBin = NULL;
					recent_data = 0;
					return 0;
				}
				i = memcmp(&time_limit, set_data + (HEADER_SIZE + 6), 4);
			}
		}
		if (i == 0)		// ブロック・サイズとファイル数を比較する
			i = memcmp(set_data + 10, set_data + (HEADER_SIZE + 10), 12);
		if (i != 0){	// ID が同じでも Set 内容が異なる場合は削除する
			DeleteFile(ini_path);
			if (SetFilePointer(hIniBin, 0, 0, FILE_BEGIN) != INVALID_SET_FILE_POINTER)
				SetEndOfFile(hIniBin);
			match = 0;
		}
	}

	// 一致しなかった場合は今回のデータを記録する
	if (match == 0){
		if (file_write_data(hIniBin, 0, set_data, HEADER_SIZE) != 0)
			delete_ini_file();
		return 0;
#ifdef VERBOSE
	} else {
		time_f_date(&ft, (char *)path);
		printf("Date of result: %s\n", (char *)path);
#endif
	}

	return match;
}

// 検査結果ファイルを閉じる
void close_ini_file(void)
{
	if (((recent_data & 0x0F) != 0) && (hIniBin != NULL)){
		unsigned int new_crc, old_crc;
		HANDLE hFile;

		// 閉じる前に検査結果の CRC-32 を計算して記録しておく
		hFile = CreateFile(ini_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE){
			new_crc = file_crc_part(hFile);
			CloseHandle(hFile);
		} else {
			CloseHandle(hIniBin);
			hIniBin = NULL;
			return;
		}
		file_read_data(hIniBin, 6, (unsigned char *)&old_crc, 4);
		if (new_crc != old_crc)	// .INI ファイルが変更されていれば、それも書き込む
			file_write_data(hIniBin, 6, (unsigned char *)&new_crc, 4);
		FlushFileBuffers(hIniBin);
		file_read_data(hIniBin, 2, (unsigned char *)&old_crc, 4);
		new_crc = file_crc_part(hIniBin);
		if (new_crc != old_crc)	// 検査結果が同じなら更新しない
			file_write_data(hIniBin, 2, (unsigned char *)&new_crc, 4);
		CloseHandle(hIniBin);
		hIniBin = NULL;
	}
}

void write_ini_file2(unsigned char *par_client, wchar_t *par_comment)
{
	if (recent_data == 0)
		return;

	// クリエーターは ASCII 文字なので「"」で囲んで記録する
	if (par_client[0] != 0){
		wchar_t uni_buf[COMMENT_LEN + 2];
		int i;
		uni_buf[0] = '"';
		for (i = 0; par_client[i] != 0; i++){
			uni_buf[i + 1] = par_client[i];
		}
		uni_buf[i + 1] = '"';
		uni_buf[i + 2] = 0;
		WritePrivateProfileString(L"Set", L"Creator", uni_buf, ini_path);
	} else {
		WritePrivateProfileString(L"Set", L"Creator", NULL, ini_path);
	}

	// コメントを記録する (制御文字は浄化済み)
	if (par_comment[0] != 0){
		int i;
		// 7-bit ASCII の範囲に収まるか調べる
		for (i = 0; par_comment[i] != 0; i++){
			if (par_comment[i] & 0xFF80){
				i = -1;
				break;
			}
		}
		if (i < 0){	// UTF-16 -> 16進数に変換して記録する
			wchar_t uni_buf[COMMENT_LEN * 4];
			for (i = 0; par_comment[i] != 0; i++){
				swprintf(uni_buf + (i * 4), _countof(uni_buf) - i * 4, L"%04X", (unsigned short)(par_comment[i]));
			}
			uni_buf[i * 4] = 0;
			WritePrivateProfileString(L"Set", L"Comment", uni_buf, ini_path);
		} else {	// ASCII 文字を Unicode と区別するために () で囲んで記録する
			wchar_t uni_buf[COMMENT_LEN + 2];
			uni_buf[0] = '(';
			for (i = 0; par_comment[i] != 0; i++){
				uni_buf[i + 1] = par_comment[i];
			}
			uni_buf[i + 1] = ')';
			uni_buf[i + 2] = 0;
			WritePrivateProfileString(L"Set", L"Comment", uni_buf, ini_path);
		}
	} else {
		WritePrivateProfileString(L"Set", L"Comment", NULL, ini_path);
	}
}

void write_ini_file(
	file_ctx_r *files)	// 各ソース・ファイルの情報
{
	unsigned char buf[MAX_LEN * 3];
	int i, len;

	if (recent_data == 0)
		return;

	for (i = 0; i < file_num; i++){
		if (files[i].name <= 0)
			return;	// File Description packet が欠落してる場合は記録しない
	}

	// ファイル位置を HEADER_SIZE 直後にする
	i = SetFilePointer(hIniBin, HEADER_SIZE, NULL, FILE_BEGIN);
	if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
		delete_ini_file();
		return;
	}
	for (i = 0; i < file_num; i++){
		// ソース・ファイルの情報
		memcpy(buf    , &(files[i].size), 8);
		memcpy(buf + 8, files[i].hash, 32);
		// ファイル名は UTF-8 に変換してから記録する
		utf16_to_utf8(list_buf + files[i].name, buf + HEADER_EACH);
		len = (int)strlen(buf + HEADER_EACH);
		memcpy(buf + 40, &len, 2);
		if (WriteFile(hIniBin, buf, HEADER_EACH + len, &len, NULL) == 0){
			delete_ini_file();
			return;
		}
	}

	// ファイル名領域の文字数をファイル情報を全て書き込んだ印にする
	if (file_write_data(hIniBin, HEADER_SIZE - 4, (unsigned char *)(&list_max), 4) != 0)
		delete_ini_file();
}

int read_ini_file(
	wchar_t *uni_buf,
	file_ctx_r *files)	// 各ソース・ファイルの情報
{
	unsigned char buf[MAX_LEN * 3];
	int i, j, len;

	// ファイル名領域の文字数を取得する
	if (file_read_data(hIniBin, HEADER_SIZE - 4, (unsigned char *)(&list_max), 4) != 0){
		delete_ini_file();
		return 1;
	}
	if (list_max == 0)	// ソース・ファイルの検査が完了してない
		return 1;		// 前回の検査結果はそのままで再度検査する
	list_len = 1;	// 先頭に不明用の null 文字を置く
	list_buf = (wchar_t *)malloc(list_max * 2);
	if (list_buf == NULL){
		delete_ini_file();
		return 1;
	}
	list_buf[0] = 0;

	// ファイル位置を HEADER_SIZE 直後にする
	i = SetFilePointer(hIniBin, HEADER_SIZE, NULL, FILE_BEGIN);
	if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
		free(list_buf);
		delete_ini_file();
		return 1;
	}
	for (j = 0; j < file_num; j++){
		if (!ReadFile(hIniBin, buf, HEADER_EACH, &i, NULL) || (HEADER_EACH != i)){
			print_win32_err();
			free(list_buf);
			delete_ini_file();
			return 1;
		}
		// ソース・ファイルの情報
		memcpy(&(files[j].size), buf, 8);
		memcpy(files[j].hash, buf + 8, 32);
		len = 0;
		memcpy(&len, buf + 40, 2);
		if (!ReadFile(hIniBin, buf, len, &i, NULL) || (len != i)){
			print_win32_err();
			free(list_buf);
			delete_ini_file();
			return 1;
		}
		buf[len] = 0;	// 末尾に null 文字を追加しておく
		utf8_to_utf16(buf, uni_buf);
		len = (int)wcslen(uni_buf);	// ファイル名の文字数
		wcscpy(list_buf + list_len, uni_buf);
		//printf("list_max = %d, list_len = %d, len = %d\n", list_max, list_len, len);
		//printf("%S\nhash = %02X, size = %I64d\n", list_buf + list_len, files[j].hash[0], files[j].size);
		files[j].name = list_len;
		list_len += (len + 1);
	}

	if (GetPrivateProfileString(L"Set", L"Creator", L"", uni_buf, COMMENT_LEN, ini_path) > 0)
		printf("Creator : %S\n", uni_buf);	// ASCII なのでそのまま表示する

	i = GetPrivateProfileString(L"Set", L"Comment", L"", uni_buf, MAX_LEN, ini_path);
	if (i > 0){
		if (uni_buf[0] < '0'){
			uni_buf[i - 1] = 0;
			printf("Comment : %S\n", uni_buf + 1);	// ASCII なのでそのまま表示する
		} else {
			wchar_t tmp_buf[5];
			tmp_buf[4] = 0;
			for (i = 0; uni_buf[i * 4] >= '0'; i++){
				tmp_buf[0] = uni_buf[i * 4    ];
				tmp_buf[1] = uni_buf[i * 4 + 1];
				tmp_buf[2] = uni_buf[i * 4 + 2];
				tmp_buf[3] = uni_buf[i * 4 + 3];
				uni_buf[i] = (unsigned short)wcstoul(tmp_buf, NULL, 16);
			}
			uni_buf[i] = 0;
			if (utf16_to_cp(uni_buf, buf, cp_output))
				buf[0] = 0;
			if (buf[0] != 0)
				printf("Comment : %s\n", buf);
		}
	}

	return 0;
}

// 検査するリカバリ・ファイルが同じであれば、再検査する必要は無い
int check_ini_recovery(
	HANDLE hFile,			// リカバリ・ファイルのハンドル
	unsigned int meta[7])	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
{
	unsigned char buf[PARITY_SIZE];
	wchar_t item[32];
	BY_HANDLE_FILE_INFORMATION fi;

	// 現在のファイル属性を取得する
	recent_data &= ~0x10;
	memset(&fi, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	if (GetFileInformationByHandle(hFile, &fi) != 0){
		meta[0] = fi.nFileSizeLow;
		meta[1] = fi.nFileSizeHigh;
		if ((recent_data == 0) || ((fi.nFileSizeLow <= REUSE_MIN) && (fi.nFileSizeHigh == 0)))
			return 0;	// 小さなファイルはサイズだけ戻す
		meta[2] = time_f_u(&(fi.ftCreationTime));
		meta[3] = time_f_u(&(fi.ftLastWriteTime));
		meta[4] = fi.dwVolumeSerialNumber;
		meta[5] = fi.nFileIndexLow;
		meta[6] = fi.nFileIndexHigh;
		recent_data |= 0x10;	// 大きなファイルだけ検査結果を参照する
	} else {
		return -1;	// 属性の読み取りエラー
	}

	// 記録されてるデータと比較する
	format_id(item, -1, meta + 4);	// ファイル識別番号を項目名にする
	ini_off = GetPrivateProfileInt(L"PAR", item, 0, ini_path);
	if (ini_off != 0){	// リカバリ・ファイルの検査記録があるなら
		if (file_read_data(hIniBin, ini_off, buf, PARITY_SIZE) != 0){
			delete_ini_file();
			return 0;
		}
#ifdef VERBOSE
	printf("check state, offset = %d, state = %02X\n", ini_off, buf[24]);
#endif
		if (memcmp(buf, meta, 16) != 0)	// ファイル状態が異なる
			ini_off = 0;
	}

	// 一致しなかった場合は今回のデータを記録する
	if (ini_off == 0){
		ini_off = GetFileSize(hIniBin, NULL);	// 設定ファイルのサイズ = 末尾の offset
		// 未検査なので状態は 0xFF で埋める
		memcpy(buf, meta, 16);
		memset(buf + 16, 0xFF, PARITY_SIZE - 16);
		if (file_write_data(hIniBin, ini_off, buf, PARITY_SIZE) != 0)
			delete_ini_file();
#ifdef VERBOSE
	printf("start state, offset = %d\n", ini_off);
#endif
		return 0;
	}

	return 1;
}

// パリティ・ブロックの状態を書き込む
void write_ini_recovery(
	int id,		// パリティ・ブロックの番号
	__int64 off)
{
	unsigned char buf[PARITY_EACH];
	int i;

	if ((recent_data & 0x10) == 0)
		return;	// 小さなファイルは検査結果を記録しない

	memcpy(buf    , &id, 2);	// id は 2バイトで保存する
	memcpy(buf + 2, &off, 8);
	if (WriteFile(hIniBin, buf, PARITY_EACH, &i, NULL) == 0){
		delete_ini_file();
		return;
	}
}

// リカバリ・ファイルの状態を書き込む
void write_ini_recovery2(
	int packet_count,		// そのリカバリ・ファイル内に含まれるパケットの数
	int block_count,		// そのリカバリ・ファイル内に含まれるパリティ・ブロックの数
	int bad_flag,			// そのリカバリ・ファイルの状態
	unsigned int meta[7])	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
{
	if ((recent_data & 0x10) == 0)
		return;	// 小さなファイルは検査結果を記録しない
	recent_data &= ~0x10;

	if (packet_count != -1){
		unsigned char buf[PARITY_SIZE];
		wchar_t item[32], uni_buf[16];
		// 見つかったパリティ・ブロックの数を書き込む
		memcpy(buf    , &packet_count, 4);
		memcpy(buf + 4, &block_count, 4);
		memcpy(buf + 8, &bad_flag, 1);
		if (file_write_data(hIniBin, ini_off + 16, buf, PARITY_SIZE - 16) != 0){
			delete_ini_file();
			return;
		}
		format_id(item, -1, meta + 4);	// ファイル識別番号を項目名にする
		swprintf(uni_buf, _countof(uni_buf), L"%d", ini_off);
		WritePrivateProfileString(L"PAR", item, uni_buf, ini_path);
#ifdef VERBOSE
		printf("write state, offset = %d, state = %02X\n", ini_off, bad_flag);
#endif

	} else {	// エラー発生なら途中までの検査結果を削除する
		int i = SetFilePointer(hIniBin, ini_off, NULL, FILE_BEGIN);
		if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
			delete_ini_file();
			return;
		}
		if (!SetEndOfFile(hIniBin)){
			delete_ini_file();
			return;
		}
#ifdef VERBOSE
		printf("clear state, offset = %d\n", ini_off);
#endif
	}
}

int read_ini_recovery(
	int num,
	int *packet_count,		// そのリカバリ・ファイル内に含まれるパケットの数
	int *block_count,		// そのリカバリ・ファイル内に含まれるパリティ・ブロックの数
	int *bad_flag,			// そのリカバリ・ファイルの状態
	parity_ctx_r *p_blk)	// 各パリティ・ブロックの情報
{
	unsigned char buf[PARITY_EACH * MAX_EACH], *buf_p;
	int i, j, max, left, new_count = 0;

	// 状態を読み込む
	if (file_read_data(hIniBin, ini_off + 16, buf, PARITY_SIZE - 16) != 0){
		delete_ini_file();
		return -1;
	}
	*bad_flag = 0;
	memcpy(packet_count, buf    , 4);
	memcpy(block_count , buf + 4, 4);
	memcpy(bad_flag    , buf + 8, 1);

	// パリティ・ブロック情報を読み込む
	max = *block_count;
	left = 0;
	for (i = 0; i < max; i++){
		if (left == 0){	// データを最大 MAX_EACH 個分読み込む
			left = max - i;
			if (left > MAX_EACH)
				left = MAX_EACH;
			if (!ReadFile(hIniBin, buf, PARITY_EACH * left, &j, NULL) || ((PARITY_EACH * left) != j)){
				delete_ini_file();
				return -1;
			}
			buf_p = buf;
		}
		j = 0;
		memcpy(&j, buf_p, 2);
		if (p_blk[j].exist == 0){
			new_count++;	// リカバリ・ファイル内の新しいパリティ・ブロックの数
			p_blk[j].exist = 1;
			p_blk[j].file = num;
			memcpy(&(p_blk[j].off), buf_p + 2, 8);
		}
		buf_p += PARITY_EACH;	// 次のデータへ
		left--;
	}

	return new_count;
}

// Input File Slice Checksum を書き込む
void write_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	unsigned char buf[CHKSUM_EACH * MAX_EACH];
	int i, j, k, max;

	if (recent_data == 0)
		return;

	ini_off = GetPrivateProfileInt(L"Set", L"Checksum", 0, ini_path);
	if (ini_off != 0)	// 既にチェックサムが記録されてるなら
		return;

	// チェックサムの開始位置
	ini_off = GetFileSize(hIniBin, NULL);	// 設定ファイルのサイズ = 末尾の offset
	// チェックサムの終了位置までファイルを拡大する
	i = SetFilePointer(hIniBin, ini_off + (source_num * CHKSUM_EACH), NULL, FILE_BEGIN);
	if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
		delete_ini_file();
		return;
	}
	if (SetEndOfFile(hIniBin) == 0){
		delete_ini_file();
		return;
	}

	for (j = 0; j < entity_num; j++){
		if (files[j].size > 0){
			max = files[j].b_off;
			// ファイル位置をソース・ファイルに合わせる
			i = SetFilePointer(hIniBin, ini_off + (CHKSUM_EACH * max), NULL, FILE_BEGIN);
			if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
				delete_ini_file();
				return;
			}
			max += files[j].b_num;
			i = files[j].b_off;
			while (i < max){
				for (k = 0; (k < MAX_EACH) && (i + k < max); k++)
					memcpy(buf + (CHKSUM_EACH * k), s_blk[i + k].hash, CHKSUM_EACH);
				i += k;
				if (!WriteFile(hIniBin, buf, CHKSUM_EACH * k, &k, NULL)){
					delete_ini_file();
					return;
				}
			}
		}
	}
	// チェックサムを正しく書き込めた場合だけ項目を作る
	swprintf((wchar_t *)buf, sizeof(buf) / 2, L"%d", ini_off);
	WritePrivateProfileString(L"Set", L"Checksum", (wchar_t *)buf, ini_path);
}

// Input File Slice Checksum を読み込む
int read_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	unsigned char buf[CHKSUM_EACH * MAX_EACH], *buf_p;
	int i, j, k, max, left;

	if (recent_data == 0)
		return -1;

	// チェックサムが記録されてるかを確かめる
	ini_off = GetPrivateProfileInt(L"Set", L"Checksum", 0, ini_path);
	if (ini_off == 0)	// チェックサムが記録されて無いなら
		return -1;

	// 存在しないチェックサムは記録から読み込む
	for (j = 0; j < entity_num; j++){
		if ((files[j].size > 0) && (files[j].state & 0x80)){
			max = files[j].b_off;
			// ファイル位置をソース・ファイルに合わせる
			i = SetFilePointer(hIniBin, ini_off + (CHKSUM_EACH * max), NULL, FILE_BEGIN);
			if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
				delete_ini_file();
				return -1;
			}
			max += files[j].b_num;
			left = 0;
			for (i = files[j].b_off; i < max; i++){
				if (left == 0){	// データを MAX_EACH 個分読み込む
					left = max - i;
					if (left > MAX_EACH)
						left = MAX_EACH;
					if (!ReadFile(hIniBin, buf, CHKSUM_EACH * left, &k, NULL) || ((CHKSUM_EACH * left) != k)){
						delete_ini_file();
						return -1;
					}
					buf_p = buf;
				}
				memcpy(s_blk[i].hash, buf_p, 20);
				memcpy(&(s_blk[i].crc), buf_p + 16, 4);
				s_blk[i].crc ^= window_mask;	// CRC の初期値と最終処理の 0xFFFFFFFF を取り除く
				buf_p += CHKSUM_EACH;	// 次のデータへ
				left--;
			}
			files[j].state = 0;	// そのファイルのチェックサムを全て読み込んだ
		}
	}

#ifdef VERBOSE
	printf("read checksum, offset = %d\n", ini_off);
#endif
	return 0;
}

// Input File Slice Checksum が不完全でも読み書きする
void update_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	unsigned char buf[CHKSUM_EACH * MAX_EACH], *buf_p, checksum_state;
	int i, j, k, max, left, first_time = 0;

	if (recent_data == 0)
		return;

	ini_off = GetPrivateProfileInt(L"Set", L"Checksum2", 0, ini_path);
	if (ini_off == 0){	// チェックサムがまだ記録されて無いなら、記録する領域を確保する
		// ファイル位置を末尾にする
		i = SetFilePointer(hIniBin, 0, NULL, FILE_END);
		if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
			delete_ini_file();
			return;
		}
		// チェックサムの開始位置
		ini_off = GetFileSize(hIniBin, NULL);	// 設定ファイルのサイズ = 末尾の offset
		// チェックサムの終了位置までファイルを拡大する
		i = SetFilePointer(hIniBin, source_num * CHKSUM_EACH + entity_num, NULL, FILE_CURRENT);
		if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
			delete_ini_file();
			return;
		}
		if (SetEndOfFile(hIniBin) == 0){
			delete_ini_file();
			return;
		}
		first_time = -1;
	}

	for (j = 0; j < entity_num; j++){
		if (files[j].size > 0){
			if (first_time){
				checksum_state = 1;
			} else {	// チェックサム状態を読み込む
				if (file_read_data(hIniBin, ini_off + j, &checksum_state, 1) != 0){
					delete_ini_file();
					return;
				}
			}
			max = files[j].b_off;
			// ファイル位置をソース・ファイルに合わせる
			i = SetFilePointer(hIniBin, ini_off + (CHKSUM_EACH * max + entity_num), NULL, FILE_BEGIN);
			if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
				delete_ini_file();
				return;
			}
			if ((checksum_state != 0) && ((files[j].state & 0x80) == 0)){	// 存在するチェックサムだけ記録する
				max += files[j].b_num;
				i = files[j].b_off;
				while (i < max){
					for (k = 0; (k < MAX_EACH) && (i + k < max); k++)
						memcpy(buf + (CHKSUM_EACH * k), s_blk[i + k].hash, CHKSUM_EACH);
					i += k;
					if (!WriteFile(hIniBin, buf, CHKSUM_EACH * k, &k, NULL)){
						delete_ini_file();
						return;
					}
				}
			} else if ((checksum_state == 0) && ((files[j].state & 0x80) != 0)){	// 存在しないチェックサムは記録から読み込む
				max += files[j].b_num;
				left = 0;
				for (i = files[j].b_off; i < max; i++){
					if (left == 0){	// データを MAX_EACH 個分読み込む
						left = max - i;
						if (left > MAX_EACH)
							left = MAX_EACH;
						if (!ReadFile(hIniBin, buf, CHKSUM_EACH * left, &k, NULL) || ((CHKSUM_EACH * left) != k)){
							delete_ini_file();
							return;
						}
						buf_p = buf;
					}
					memcpy(s_blk[i].hash, buf_p, 20);
					memcpy(&(s_blk[i].crc), buf_p + 16, 4);
					s_blk[i].crc ^= window_mask;	// CRC の初期値と最終処理の 0xFFFFFFFF を取り除く
					buf_p += CHKSUM_EACH;	// 次のデータへ
					left--;
				}
				files[j].state = 0;	// そのファイルのチェックサムを全て読み込んだ
			}
			// チェックサムの状態も記録する
			if ((first_time) || ((checksum_state != 0) && ((files[j].state & 0x80) == 0))){
				checksum_state = (unsigned char)(files[j].state);
				if (file_write_data(hIniBin, ini_off + j, &checksum_state, 1) != 0){
					delete_ini_file();
					return;
				}
			}
		}
	}

#ifdef VERBOSE
	printf("update checksum, offset = %d\n", ini_off);
#endif
	if (first_time){	// 最初にチェックサムを正しく書き込めた場合だけ項目を作る
		swprintf((wchar_t *)buf, sizeof(buf) / 2, L"%d", ini_off);
		WritePrivateProfileString(L"Set", L"Checksum2", (wchar_t *)buf, ini_path);
	}
}

// ソース・ファイルのハッシュ値の検査結果を記録する
void write_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	int result)				// 検査結果 0～=何ブロック目まで一致, -3=完全に一致
{
	wchar_t item[32], uni_buf[32];

	if ((recent_data & 0x10) == 0)
		return;	// 小さなファイルは検査結果を記録しない
	recent_data &= ~0x10;

	// 今回の状態を書き込む
	format_id(item, num, meta + 4);	// ファイル識別番号を項目名にする
	base64_encode5(uni_buf, meta, result);
	WritePrivateProfileString(L"State", item, uni_buf, ini_path);
#ifdef VERBOSE
	printf("write state, num = %d, state = %d\n", num, result);
#endif
}

// ソース・ファイルのハッシュ値の検査結果が記録されてるかどうか
// -1=属性の読み取りエラー, -2=記録なし, 0～=何ブロック目まで一致, -3=完全に一致
int check_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	HANDLE hFile)			// そのファイルのハンドル
{
	wchar_t item[32], uni_buf[32];
	int state[4], result;
	BY_HANDLE_FILE_INFORMATION fi;

	// 現在のファイル属性を取得する
	recent_data &= ~0x10;
	memset(&fi, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	if (GetFileInformationByHandle(hFile, &fi) != 0){
		meta[0] = fi.nFileSizeLow;
		meta[1] = fi.nFileSizeHigh;
		if ((recent_data == 0) || ((fi.nFileSizeLow <= REUSE_MIN) && (fi.nFileSizeHigh == 0)))
			return -2;	// 小さなファイルはサイズだけ戻す
		meta[2] = time_f_u(&(fi.ftCreationTime));
		meta[3] = time_f_u(&(fi.ftLastWriteTime));
		meta[4] = fi.dwVolumeSerialNumber;
		meta[5] = fi.nFileIndexLow;
		meta[6] = fi.nFileIndexHigh;
		recent_data |= 0x10;	// 大きなファイルだけ検査結果を参照する
	} else {
		return -1;	// 属性の読み取りエラー、検査自体を行わない
	}

	// 記録されてる状態を読み込む
	format_id(item, num, meta + 4);	// ファイル識別番号を項目名にする
	result = GetPrivateProfileString(L"State", item, L"", uni_buf, _countof(uni_buf), ini_path);
	if (result == 0)	// 項目が見つからない
		return -2;
	result = base64_decode5(uni_buf, state);
	if (memcmp(meta, state, 16) != 0)	// ファイル状態が異なる
		return -2;
#ifdef VERBOSE
	printf("check state, num = %d, state = %d\n", num, result);
#endif

	return result;
}

// パスで指定されたソース・ファイルが完全だと書き込む
void write_ini_complete(
	int num,				// ファイル番号
	wchar_t *file_path)		// ソース・ファイルの絶対パス
{
	wchar_t item[32], uni_buf[32];
	unsigned int meta[7];
	HANDLE hFile;
	BY_HANDLE_FILE_INFORMATION fi;

	if ((recent_data & 0x0F) == 0)
		return;

	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return;
	// 現在のファイル属性を取得する
	memset(&fi, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	if (GetFileInformationByHandle(hFile, &fi) != 0){
		meta[0] = fi.nFileSizeLow;
		meta[1] = fi.nFileSizeHigh;
		if ((fi.nFileSizeHigh == 0) && (fi.nFileSizeLow <= REUSE_MIN)){
			CloseHandle(hFile);
			return;	// 小さなファイルは検査結果を記録しない
		}
		meta[2] = time_f_u(&(fi.ftCreationTime));
		meta[3] = time_f_u(&(fi.ftLastWriteTime));
		meta[4] = fi.dwVolumeSerialNumber;
		meta[5] = fi.nFileIndexLow;
		meta[6] = fi.nFileIndexHigh;
	} else {
		CloseHandle(hFile);
		return;	// 属性の読み取りエラー
	}
	CloseHandle(hFile);

	// 完全という状態を書き込む
	format_id(item, num, meta + 4);	// ファイル識別番号を項目名にする
	base64_encode5(uni_buf, meta, -3);	// 完全 = -3
	WritePrivateProfileString(L"State", item, uni_buf, ini_path);
#ifdef VERBOSE
	printf("write state, num = %d, state = complete\n", num);
#endif
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 末尾が途切れた時の為、指定サイズ読み込めなかった場合でもエラーにしない
static int file_read_slice(
	HANDLE hFileRead,
	__int64 offset,
	unsigned char *buf,
	unsigned int size)
{
	unsigned int rv, rv2;

	// ファイルの位置を offsetバイト目にする
	rv2 = (unsigned int)(offset >> 32);
	rv = (unsigned int)offset;
	rv = SetFilePointer(hFileRead, rv, &rv2, FILE_BEGIN);
	if ((rv == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
		print_win32_err();
		return 1;
	}

	// size バイトを読み込む
	if (!ReadFile(hFileRead, buf, size, &rv, NULL)){
		print_win32_err();
		return 2;	// 指定サイズを読み込めなくてもエラーにしない
	}
	if (rv < size)
		memset(buf + rv, 0, size - rv);	// 足りなかった分は 0 で埋める

	return 0;
}

// スライス断片を読み取ってメモリー上に一時保管する
static void read_flake(
	HANDLE hFile,		// ソース・ファイルのハンドル
	__int64 file_off,
	int id,				// ブロック番号
	int flake_size,		// 断片のサイズ
	int side,			// どちら側か 1=front, 0=rear
	slice_ctx *sc)
{
	unsigned char *p;
	int i, rv;
	size_t mem_size;
	flake_ctx *fc;

	rv = -1;	// 拡張するかどうか (空き領域の番号)
	if (sc->flake_count > 0){	// 保管場所があるか調べる
		p = sc->flk_buf;
		for (i = 0; i < sc->flake_count; i++){
			fc = (flake_ctx *)p;
			p += sizeof(flake_ctx);
			if (fc->id == id){	// 既にスライス断片が記録されてる
				//printf("read flake[%d] = %d, size = %d, add ok\n", i, id, flake_size);
				if (side){	// 前半に上書きコピーする
					if (file_read_slice(hFile, file_off, p, flake_size) != 0)
						return;	// 読み込み失敗
					fc->front_size = flake_size;
				} else {	// 後半に上書きコピーする
					if (file_read_slice(hFile, file_off, p + (block_size - flake_size), flake_size) != 0)
						return;	// 読み込み失敗
					fc->rear_size = flake_size;
				}
				return;	// 追加して終わる
			} else if (fc->id == -1){	// 空き領域が存在する
				rv = i;	// 拡張する必要なし
			}
			p += block_size;	// 次の保管領域へ
		}
	}

	if (rv == -1){	// スライス断片の記録領域を拡張する
		int max_count;
		mem_size = get_mem_size(0) / 2;	// 使えるサイズの半分までにする
		max_count = (int)(mem_size / (sizeof(flake_ctx) + (size_t)block_size));
		i = (source_num - entity_num) / 2;	// 原理的に分割断片は「ソース・ブロック数 - ファイル数」の半分以下
		if (max_count > i)
			max_count = i;
		i = source_num - first_num;	// 未発見のソース・ブロックの数までにする
		if (max_count > i)
			max_count = i;
		//printf("mem_size = %zu MB, max_count = %d \n", mem_size >> 20, max_count);
		if (sc->flake_count < max_count){
			mem_size = sc->flake_count + 1;
			mem_size *= sizeof(flake_ctx) + (size_t)block_size;
			p = (unsigned char *)realloc(sc->flk_buf, mem_size);
			if (p != NULL){
				sc->flk_buf = p;
				rv = sc->flake_count;	// 新規作成した領域の番号
				sc->flake_count += 1;
			}
		}
		if (rv == -1){
			if (sc->flake_count > 0){	// これ以上拡張できないなら末尾に入れる
				rv = 0;	// 先頭を空けてずらす
			} else {	// 領域自体を確保できない場合は保管しない
				sc->flake_count = -1;
				return;
			}
		}
	}
	if (rv < sc->flake_count - 1){	// 末尾領域以外が空いてるなら
		p = sc->flk_buf + (sizeof(flake_ctx) + (size_t)block_size) * rv;
		mem_size = (sizeof(flake_ctx) + (size_t)block_size) * (sc->flake_count - rv - 1);
		//printf("slide from %p to %p, %zu bytes \n", p + (sizeof(flake_ctx) + block_size), p, mem_size);
		memmove(p, p + sizeof(flake_ctx) + block_size, mem_size);
		rv = sc->flake_count - 1;	// 前にずらして末尾領域に保管する
	}

	//printf("read flake[%d] = %d, size = %u, save ok\n", rv, id, flake_size);
	p = sc->flk_buf + (sizeof(flake_ctx) + (size_t)block_size) * rv;
	fc = (flake_ctx *)p;
	p += sizeof(flake_ctx);
	fc->id = -1;	// 読み込み失敗した時のために初期化しておく
	if (side){	// 前半に上書きコピーする
		if (file_read_slice(hFile, file_off, p, flake_size) != 0)
			return;	// 読み込み失敗
		fc->front_size = flake_size;
		fc->rear_size = 0;
	} else {	// 後半に上書きコピーする
		if (file_read_slice(hFile, file_off, p + (block_size - flake_size), flake_size) != 0)
			return;	// 読み込み失敗
		fc->front_size = 0;
		fc->rear_size = flake_size;
	}
	fc->id = id;
}

// スライス単位の検査結果
// 0～=そのファイル内に含まれるスライスの数, -1=エラー, -3=記録なし
int check_ini_verify(
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	int num1,				// チェックサムを比較したファイルの番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)			// スライス検査用の情報
{
	unsigned char buf[SOURCE_EACH * MAX_EACH], *buf_p, err_mag;
	wchar_t item[MAX_LEN];
	int i, j, max, left, num;
	unsigned int err_off, flag;
	unsigned int time_last;
	__int64 file_off, file_size;
	BY_HANDLE_FILE_INFORMATION fi;

	// 現在のファイル属性を取得する
	recent_data &= ~0x10;
	memset(&fi, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
	if (GetFileInformationByHandle(hFile, &fi) != 0){
		meta[0] = fi.nFileSizeLow;
		meta[1] = fi.nFileSizeHigh;
		if ((recent_data == 0) || ((fi.nFileSizeLow <= REUSE_MIN) && (fi.nFileSizeHigh == 0)))
			return -3;	// 小さなファイルはサイズだけ戻す
		meta[2] = time_f_u(&(fi.ftCreationTime));
		meta[3] = time_f_u(&(fi.ftLastWriteTime));
		meta[4] = fi.dwVolumeSerialNumber;
		meta[5] = fi.nFileIndexLow;
		meta[6] = fi.nFileIndexHigh;
		recent_data |= 0x10;	// 大きなファイルだけ検査結果を参照する
	} else {
		meta[0] = 0;
		meta[1] = 0;
		return -1;	// 属性の読み取りエラー
	}

	// ファイル番号が指定されてる場合は、番号も記録する
	format_id(item, num1, meta + 4);	// ファイル識別番号を項目名にする
	// 簡易検査と詳細検査を区別する
	if (switch_v & 4){
		ini_off = GetPrivateProfileInt(L"Align", item, 0, ini_path);
	} else if (switch_v & 1){
		ini_off = GetPrivateProfileInt(L"Simple", item, 0, ini_path);
	} else {
		ini_off = GetPrivateProfileInt(L"Damage", item, 0, ini_path);
	}

	if (ini_off != 0){
		if (file_read_data(hIniBin, ini_off, buf, STATE_SIZE) != 0){
			delete_ini_file();
			return -3;
		}
		memcpy(&max, buf + 16, 4);
		//printf("offset = %d, count = %d\n", ini_off, max);
		if (memcmp(meta, buf, 16) != 0)	// ファイル状態が異なる
			ini_off = 0;
	}

	// 一致しなかった場合は今回のデータを記録する
	if (ini_off == 0){
		ini_off = GetFileSize(hIniBin, NULL);	// 設定ファイルのサイズ = 末尾の offset
		memcpy(buf, meta, 16);
		memset(buf + 16, 0xFF, 4);	// 検査未了の印
		if (file_write_data(hIniBin, ini_off, buf, STATE_SIZE) != 0){
			delete_ini_file();
			return -3;
		}
#ifdef VERBOSE
		printf("start state, offset = %d, switch = %d\n", ini_off, switch_v & 7);
#endif
		return -3;
	}

#ifdef VERBOSE
	printf("read  state, offset = %d, switch = %d, find_num = %d\n", ini_off, switch_v & 7, max);
#endif

	// 一致するなら記録されてるデータを読み込む
	//printf("read_ini, %S = %d\n", item, max);
	time_last = GetTickCount();
	memcpy(&file_size, meta, 8);
	i = SetFilePointer(hIniBin, ini_off + STATE_SIZE, NULL, FILE_BEGIN);
	if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
		delete_ini_file();
		return -3;
	}
	wcscpy(item, base_dir);	// 基準ディレクトリを入れておく
	left = 0;
	for (i = 0; i < max; i++){
		if (left == 0){	// データを MAX_EACH 個分読み込む
			left = max - i;
			if (left > MAX_EACH)
				left = MAX_EACH;
			if (!ReadFile(hIniBin, buf, SOURCE_EACH * left, &j, NULL) || ((SOURCE_EACH * left) != j)){
				delete_ini_file();
				return -3;
			}
			buf_p = buf;
		}
		flag = 0;
		memcpy(&flag, buf_p + 10, 1);	// エラー訂正するかどうか
		j = 0;
		memcpy(&j, buf_p, 2);	// ソース・ブロックの番号
		memcpy(&file_off, buf_p + 2, 8);
		//printf("id = %d, off = %I64d, flag = %d\n", j, file_off, flag);
		if (flag & 8){	// 分割された断片
			if ((sc->flake_count >= 0) && (s_blk[j].exist == 0)){	// 未発見のブロックなら
				if (flag == 8){	// 後半の断片
					read_flake(hFile, 0, j, (int)file_off, 0, sc);
				} else {	// 前半の断片
					__int64 file_size;
					memcpy(&file_size, meta, 8);
					read_flake(hFile, file_off, j, (int)(file_size - file_off), 1, sc);
				}
			}
		} else {
			if (s_blk[j].exist == 0){	// 未発見のブロックなら
				s_blk[j].exist = 2;
				first_num += 1;
				if ((switch_v & (16 | 4)) == 16){	// コピーする
					num = s_blk[j].file;
					if (open_temp_file(item, num, files, sc))
						return -1;
					if (flag != 1){	// そのままコピーする
						if (file_copy_data(hFile, file_off, sc->hFile_tmp,
								(__int64)(j - files[num].b_off) * (__int64)block_size, s_blk[j].size)){
							printf("file_copy_data, %d\n", j);
							return -1;
						}
					} else {	// 詳細検査でエラー訂正してからコピーする
						if (file_read_slice(hFile, file_off, sc->buf, s_blk[j].size) != 0){
							printf("file_read_slice, %d\n", j);
							return -1;
						} else {	// エラー訂正するブロックは少ないのでその場で CRC を逆算する
							flag = crc_reverse_zero(s_blk[j].crc, block_size - s_blk[j].size);
							if (correct_error(sc->buf, s_blk[j].size, s_blk[j].hash, flag, &err_off, &err_mag) < 0){
								printf("fail to correct slice, %d\n", j);
								return -1;
							} else {
								if (file_write_data(sc->hFile_tmp, (__int64)(j - files[num].b_off) * (__int64)block_size, sc->buf, s_blk[j].size) != 0){
									printf("file_write_data, %d\n", j);
									return -1;
								}
							}
						}
					}

					// 経過表示
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress_file((int)((file_off * 1000) / file_size), first_num, file_name))
							return -2;
						time_last = GetTickCount();
					}
				}
			}
			s_blk[j].exist |= 0x1000;	// このファイル内で見つけた印
		}
		buf_p += SOURCE_EACH;	// 次のデータへ
		left--;
	}
	return max;
}

// ソース・ブロックの状態を書き込む
void write_ini_verify(
	int id,		// ソース・ブロックの番号
	int flag,	// 1=エラー訂正が必要, 8=断片の後半, 9=断片の前半
	__int64 off)
{
	unsigned char buf[SOURCE_EACH];
	int i;

	if ((recent_data & 0x10) == 0)
		return;	// 小さなファイルは検査結果を記録しない

	memcpy(buf     , &id, 2);
	memcpy(buf +  2, &off, 8);
	memcpy(buf + 10, &flag, 1);
	if (WriteFile(hIniBin, buf, SOURCE_EACH, &i, NULL) == 0){
		delete_ini_file();
		return;
	}
}

void write_ini_verify2(
	int num1,				// チェックサムを比較したファイルの番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	int max)				// 記録したブロック数
{
	if ((recent_data & 0x10) == 0)
		return;	// 小さなファイルは検査結果を記録しない
	recent_data &= ~0x10;

	if (max >= 0){
		wchar_t item[32], uni_buf[16];
		// 見つかったソース・ブロックの数を書き込む
		if (file_write_data(hIniBin, ini_off + 16, (unsigned char *)(&max), 4) != 0){
			delete_ini_file();
			return;
		}
		// ファイル番号が指定されてる場合は、番号も記録する
		format_id(item, num1, meta + 4);	// ファイル識別番号を項目名にする
		swprintf(uni_buf, _countof(uni_buf), L"%d", ini_off);
		// 簡易検査と詳細検査を区別する
		if (switch_v & 4){
			WritePrivateProfileString(L"Align", item, uni_buf, ini_path);
		} else if (switch_v & 1){
			WritePrivateProfileString(L"Simple", item, uni_buf, ini_path);
		} else {
			WritePrivateProfileString(L"Damage", item, uni_buf, ini_path);
		}
#ifdef VERBOSE
		printf("write state, offset = %d, switch = %d\n", ini_off, switch_v);
#endif

	} else {	// エラー発生なら途中までの検査結果を削除する
		int i = SetFilePointer(hIniBin, ini_off, NULL, FILE_BEGIN);
		if ((i == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)){
			delete_ini_file();
			return;
		}
		if (!SetEndOfFile(hIniBin)){
			delete_ini_file();
			return;
		}
#ifdef VERBOSE
		printf("clear state, offset = %d, error = %d\n", ini_off, -max);
#endif
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 作業用のテンポラリ・ファイルを開く
int open_temp_file(
	wchar_t *temp_path,		// 作業用、基準ディレクトリが入ってる
	int num,				// 見つけたスライスが属するファイル番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	slice_ctx *sc)
{
	if (num == sc->num)
		return 0;	// 既に開かれてるならそのまま

	if (sc->hFile_tmp)	// 他の作業ファイルが既に開かれてるなら閉じる
		CloseHandle(sc->hFile_tmp);

	// スライスが所属する作業ファイルを開く
	sc->hFile_tmp = handle_temp_file(list_buf + files[num].name, temp_path);
	if (sc->hFile_tmp == INVALID_HANDLE_VALUE){
		sc->hFile_tmp = NULL;
		return 1;
	}
	sc->num = num;

	return 0;
}

