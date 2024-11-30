// verify.c
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
#include <stdlib.h>

#include <windows.h>

#include "common2.h"
#include "crc.h"
#include "md5_crc.h"
#include "ini.h"
#include "json.h"
#include "verify.h"


// 分割されたファイルを検索する為にファイル名を指定する
static void set_splited_filename(
	wchar_t *find_name,		// *filename* のフル・パス
	wchar_t *file_name,		// 本来のファイル名 filename.ext または filename
	int dir_len)			// フル・パスにおけるディレクトリ部分の長さ
{
	wchar_t *tmp_p;

	wcscpy(find_name, base_dir);
	if (dir_len > base_len){	// サブ・ディレクトリが有る場合
		wcscpy(find_name + base_len, file_name);
		find_name[dir_len] = '*';
		wcscpy(find_name + dir_len + 1, file_name + (dir_len - base_len));
	} else {	// サブ・ディレクトリが無い場合
		find_name[base_len] = '*';
		wcscpy(find_name + base_len + 1, file_name);
	}
	tmp_p = wcsrchr(find_name + dir_len + 2, '.');	// ディレクトリ部分と先頭の「.」は無視する
	if (tmp_p != NULL){
		if (wcslen(tmp_p) < EXT_LEN){	// 拡張子を取り除いて「*」に換える
			*tmp_p = '*';
			tmp_p++;
			*tmp_p = 0;
			return;
		}
	}
	wcscat(find_name, L"*");
}

// 文字化けしたファイル名のファイルを検索する為にファイル名を指定する
static int set_similar_filename(
	wchar_t *find_name,		// file*name.ext のフル・パス
	wchar_t *file_name,		// 本来のファイル名 file？name.ext
	int dir_len)			// フル・パスにおけるディレクトリ部分の長さ
{
	int i, len, count, ext_len;

	wcscpy(find_name, base_dir);
	wcscpy(find_name + base_len, file_name);

	// ファイル名内の non-ASCII 文字の数
	count = 0;
	i = 0;
	ext_len = -1;
	len = dir_len;	// ディレクトリ部分の文字化けは考慮しない
	while (find_name[len] != 0){
		if (find_name[len] >= 0x7F){
			count++;	// non-ASCII 文字の数
		} else {
			i++;	// ASCII 文字の数
			if (find_name[len] == '.')
				ext_len = len;	// 拡張子の位置
		}
		len++;
	}
	ext_len = len - ext_len;	// 拡張子を含まない場合は元の文字数を超える
	//printf("non = %d, ascii = %d, ext = %d\n", count, i, ext_len);

	// non-ASCII 文字を含まない場合は探さない
	// 拡張子以外に ASCII 文字を含まない、または ASCII 文字が半分未満なら、探さない
	if ((count == 0) || (i == ext_len) || (i < count))
		return 0;

	// non-ASCII 文字を「*」に置き換える
	count = dir_len;
	while (count < len){
		if (find_name[count] >= 0x80){
			if ((count > 0) && (find_name[count - 1] == '*')){	// 先の文字も「*」なら
				// それ以降を前にずらす
				for (i = count; i < len; i++)
					find_name[i] = find_name[i + 1];
				len--;
				count--;
			} else {
				find_name[count] = '*';
			}
		}
		count++;
	}

	return 1;
}

// 付加されてる部分が分割ファイルとして妥当か調べる
static int check_extra_part(
	wchar_t *find_name,		// 検出されたファイル名
	wchar_t *file_name,		// 本来のファイル名
	int len)				// 本来のファイル名の文字数
{
	int num = (int)wcslen(find_name);
	if (_wcsnicmp(find_name, file_name, len) == 0){	// 末尾に付加されてる
		// 追加された部分の先頭が「.」か「_」以外なら無視する
		if ((find_name[len] != '.') && (find_name[len] != '_'))
			return 1;

		// 追加された部分が数値以外なら無視する
		num = 0;
		len++;
		while (find_name[len] != 0){
			if ((find_name[len] < '0') || (find_name[len] > '9'))
				return 1;
			num = (num * 10) + (find_name[len] - '0');	// 番号を読み取る
			len++;
		}
		if ((num > 99999) || (num < 0))
			return 1;	// 数字がファイル分割の範囲外なら無視する
		return 0;

	} else if ((num > len) && (_wcsnicmp(find_name + (num - len), file_name, len) == 0)){	// 先頭に付加されてる
		if (num >= len + EXT_LEN)
			return 1;	// 追加された部分が長すぎる
		// ファイル名の不一致部分の最後が「_」以外なら無視する
		if (find_name[num - len - 1] != '_')
			return 1;
		return 0;

	} else {	// それ以外の場合
		wchar_t name_part[MAX_LEN], ext_part[EXT_LEN], file_ext[EXT_LEN], *tmp_p;

		wcscpy(name_part, find_name);
		ext_part[0] = 0;
		tmp_p = wcsrchr(name_part, '.');
		if (tmp_p != NULL){
			if (wcslen(tmp_p) < EXT_LEN){
				wcscpy(ext_part, tmp_p);	// 拡張子を記録しておく
				*tmp_p = 0;	// 拡張子を取り除く
			}
		}
		file_ext[0] = 0;
		tmp_p = wcsrchr(file_name, '.');
		if (tmp_p != NULL){
			if (wcslen(tmp_p) < EXT_LEN)
				wcscpy(file_ext, tmp_p);	// 拡張子を記録しておく
		}
		len -= (int)wcslen(file_ext);	// 本来のファイル名の長さから、拡張子の長さを引く
		//printf("name length = %d \n", len);

		// 拡張子が一致しない、または追加された拡張子が数値以外の場合は無視する
		if (_wcsicmp(file_ext, ext_part) != 0){
			if (ext_part[0] == 0)
				return 1;	// 拡張子が無ければ駄目

			// 拡張子が数値以外なら無視する
			num = 0;
			tmp_p = ext_part + 1;
			while (*tmp_p != 0){
				if ((*tmp_p < '0') || (*tmp_p > '9'))
					return 1;
				num = (num * 10) + (*tmp_p - '0');	// 番号を読み取る
				tmp_p++;
			}
			if ((num > 99999) || (num < 0))
				return 1;	// 数字がファイル分割の範囲外なら無視する

			// 拡張子が数字になっても、それ以外が完全に一致すれば許容する
			if ((name_part[len] == 0) && (_wcsnicmp(find_name, file_name, len) == 0))
				return 0;

			// 更に前の拡張子を捜す
			tmp_p = wcsrchr(name_part, '.');
			if (tmp_p != NULL){
				if (wcslen(tmp_p) < EXT_LEN){
					wcscpy(ext_part, tmp_p);	// 拡張子を記録しておく
					*tmp_p = 0;	// 拡張子を取り除く
				}
			} else {
				return 1;
			}
			if (_wcsicmp(file_ext, ext_part) != 0)
				return 1;	// それも一致しなければ駄目
		}

		// ファイル名がどこまで一致するか調べる
		if (_wcsnicmp(find_name, file_name, len) == 0){	// 拡張子との間にだけ付加されてる
			// ファイル名の不一致部分の最初が「.」と「_」以外なら無視する
			if ((name_part[len] != '.') && (name_part[len] != '_'))
				return 1;
			// ファイル名の不一致部分の最初以降に「.」を含むなら無視する
			if (wcschr(name_part + (len + 1), '.') != NULL)
				return 1;

			// 拡張子と付加部分が両方とも数値だけなら、末尾への追加と判定する
			len++;
			while (name_part[len] != 0){
				if ((name_part[len] < '0') || (name_part[len] > '9')){
					len = 0;
					break;
				}
				len++;
			}
			if (len > 0){	// 付加部分が数値のみ
				len = 1;
				while (file_ext[len] != 0){
					if ((file_ext[len] < '0') || (file_ext[len] > '9')){
						len = 0;
						break;
					}
					len++;
				}
				if (len > 0)	// 拡張子が数値のみ
					return 1;
			}
			return 0;

		} else {	// 何箇所かに付加されてる
			wchar_t name_lower[MAX_LEN];
			int i, j;
			// 比較用に小文字にする
			wcscpy(name_lower, file_name);
			_wcslwr(name_lower);
			if (file_ext[0] != 0)
				wcscat(name_part, file_ext);	// 拡張子を戻す
			_wcslwr(name_part);
			// どのくらい一致するかを調べる
			num = 0;
			i = 0;
			j = 0;
			while ((name_part[i] != 0) && (name_lower[j] != 0)){
				if (name_part[i] == name_lower[j]){
					num++;
					i++;
					j++;
				} else if (name_lower[j] >= 0x7F){	// 本来のファイル名に non-ASCII 文字があればとばす
					j++;
				} else {
					i++;
				}
			}
			//printf("match = %d, i = %d, j = %d\n", num, i, j);
			if ((num * 2 < i) || (num * 2 < j))
				return 1;	// 一致部分が半分よりも少ないと駄目
			return 0;
		}
	}
}

// 破損したソース・ファイルの作業ファイルを無視する
static int avoid_temp_file(
	wchar_t *comp_path,		// 比較するファイルのパス
	file_ctx_r *files)		// 各ソース・ファイルの情報
{
	int i, len, len2;

	len2 = (int)wcslen(comp_path);
	// 作業ファイルの末尾は決まってる wcslen(L"_par.tmp") = 8
	if (len2 <= 8)
		return 0;
	if (wcscmp(comp_path + (len2 - 8), L"_par.tmp") != 0)
		return 0;

	// ソース・ファイルごとに比較する
	for (i = 0; i < entity_num; i++){
		// 空のファイルやフォルダは作業ファイルが存在しない
		if (files[i].size == 0)
			continue;
		// 完全・追加・別名・移動のファイルは作業ファイルが存在しない
		if ((files[i].state & 3) == 0)
			continue;
		// 上書き修復する場合は作業ファイルが存在しない
		if (files[i].state & 4)
			continue;

		len = (int)wcslen(list_buf + files[i].name);
		if ((len2 == len + 8) && (_wcsnicmp(list_buf + files[i].name, comp_path, len) == 0))
			return 1;	// 作業ファイル
	}
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ソート時に項目を比較する
static int sort_cmp_crc(const void *elem1, const void *elem2)
{
	unsigned int crc1, crc2;

	crc1 = ((unsigned int *)elem1)[1];
	crc2 = ((unsigned int *)elem2)[1];

	if (crc1 < crc2)
		return -1;
	if (crc1 > crc2)
		return 1;
	return 0;
}

// 二分探索法を改造して同じキーに対応する
static int binary_search(
	int *order,			// 配列
	int high,			// 配列の要素数 = 探索範囲の上限要素 +1
	unsigned int key,	// 目的の値
	source_ctx_r *s_blk)
{
	int mid, low = 0;	// 探索範囲の下限要素

	while (low < high){
		mid = (low + high) >> 1;	// 配列の中央位置を計算
		if (s_blk[order[mid]].crc > key){			// 目的の値の方が小さい
			high = mid;		// 上限位置を変更する
		} else if (s_blk[order[mid]].crc < key){	// 目的の値の方が大きい
			low = mid + 1;	// 下限位置を変更する
		} else {	// 目的の値と一致
			// 同じ key のブロックが複数存在するかもしれないので
			while ((mid > low) && (s_blk[order[mid - 1]].crc == key))
				mid--;
			return mid;
		}
	}

	return 0x00FFFFFF;	// 見つからなかった
}

// 二分探索法を改造して一致しない場合は high 側を返す
static int binary_search_high(
	unsigned int *keys,	// 配列
	int low,			// 探索範囲の下限要素
	int high,			// 配列の要素数、探索範囲の上限要素+1
	unsigned int key)	// 目的の値
{
	int mid;

	while (low < high){		// 下限位置の方が上限位置を超えたら、目的の値は存在しない
		mid = (low + high) >> 1;	// 配列の中央位置を計算
		if (keys[mid] > key){			// 目的の値の方が小さい
			high = mid;		// 上限位置を変更する
		} else if (keys[mid] < key){	// 目的の値の方が大きい
			low = mid + 1;	// 下限位置を変更する
		} else {						// 目的の値と一致
			// 同じ key のブロックが複数存在するかもしれないので
			while ((mid > low) && (keys[mid - 1] == key))
				mid--;
			return mid;
		}
	}

	return high;	// 見つからなかった場合は、次に大きい位置を返す
}

// ソートされたデータに対して一定間隔ごとにインデックスを作る。
// 目的の値 -> インデックスで直前の値 -> 線形探索 で一致するかどうか
static void init_index_search(
	unsigned int *keys,	// データの配列
	int data_count,		// データ数
	int *index,			// 目次
	int index_bit)		// 目次の大きさ、(32-bit値 >> shift) = 目次の項目
{
	int i, off = 0;

	for (i = 0; i < (1 << index_bit); i++){
		off = binary_search_high(keys, off, data_count, i << (32 - index_bit));
		index[i] = off;
	}
}

// 目次の大きさによって探索速度が変わる。
static int index_search(
	unsigned int *keys,	// 配列
	int data_count,		// 配列の要素数 = 探索範囲の上限要素 +1
	unsigned int key,	// 目的の値
	int *index,			// 目次
	int index_shift)	// (32-bit値 >> shift) = 目次の項目
{
	int i;

	i = index[key >> index_shift];	// 配列内のどこにあるか
	// リニア・サーチで残りから探す
	while (i < data_count){
		if (keys[i] > key)
			return 0x00FFFFFF;	// 上回るなら目的の値は存在しない
		if (keys[i] == key)
			return i;
		i++;
	}
	return 0x00FFFFFF;	// 見つからなかった
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 読み込み用のサブ・スレッド
static DWORD WINAPI thread_read(LPVOID lpParameter)
{
	unsigned char *buf;
	unsigned int rv;
	slice_ctx *sc;

	sc = (slice_ctx *)lpParameter;
	buf = sc->buf + (block_size * 2);

	WaitForSingleObject(sc->run, INFINITE);	// 読み込み開始の合図を待つ
	while (sc->size){	// 読みこみエラーが発生してもループから抜けない
		// ファイルから読み込む
		if (!ReadFile(sc->hFile, buf, sc->size, &rv, NULL)){
			print_win32_err();	// 0x17 = ERROR_CRC, 0x21 = ERROR_LOCK_VIOLATION
			//memset(buf, 0, sc->size);	// 読み込み時にエラーが発生した部分は 0 にしておく
			sc->size = 0xFFFFFFFF;	// それ以上の検査を中止する
		} else if (sc->size < rv){
			memset(buf + rv, 0, sc->size - rv);	// 指定サイズを読み込めなかった分は 0 にしておく
			// エラーではない？ので続行する
		}
		//_mm_sfence();	// メモリーへの書き込みを完了する
		SetEvent(sc->end);	// 読み込み終了を通知する
		WaitForSingleObject(sc->run, INFINITE);	// 読み込み開始の合図を待つ
	}

	// 終了処理
	CloseHandle(sc->run);
	CloseHandle(sc->end);
	return 0;
}

// スライス検査の準備をする
// -1=スライス検査できない、または不要, 0=準備完了, 1=スライド検査せず
int init_verification(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)
{
	unsigned char *short_use;
	int i, j, num, file_block, block_count, index_bit, short_count;
	int *order = NULL, *short_crcs;
	unsigned int last_size;
//	unsigned int time_last;

	// 作業ファイル用
	sc->num = -1;
	sc->hFile_tmp = NULL;

	// スライド検索するスライスが何個存在するか
	j = 0;
	block_count = 0;
	short_count = 0;
	sc->min_size = block_size;
	for (num = 0; num < entity_num; num++){
		if ((files[num].size > 0) && ((files[num].state & 0x80) == 0)){	// チェックサムがあるファイルだけ比較する
			file_block = (int)(files[num].size / (__int64)block_size);	// フルサイズのブロックの数
			block_count += file_block;
			last_size = (unsigned int)(files[num].size - ((__int64)file_block * (__int64)block_size));
			if (last_size > 0){
				short_count++;	// 半端なブロックを含む
				if ((sc->min_size > last_size) && (sc->min_size > 4))	// 4バイトより大きければ
					sc->min_size = last_size;
			}
			if (files[num].state & 0x03)	// 消失 0x01、破損 0x02 ならスライスを探す
				j++;	// 検査する必要があるソース・ファイルの数
		}
	}
	//printf("target = %d, full = %d, short = %d, min_size = %d\n", j, block_count, short_count, sc->min_size);
	if (j == 0)
		return -1;	// 完全またはチェックサムが無いので、スライスを探さなくていい
	if (switch_v & 4)
		return 0;	// 順列検査ではスライド検査しない
	if (block_count == 0)
		return 1;	// スライド検査する必要なし、簡易検査で十分
	sc->block_count = block_count;
	sc->short_count = short_count;

	// インデックス・サーチで使う目次
	index_bit = 4;	// 目次の大きさは ブロック数 / 5～8 にする
	while ((1 << (index_bit + 3)) < block_count)
		index_bit++;
	// CRC-32 の順序を格納するバッファーを確保する
	i = sizeof(int) * ((block_count * 2) + (1 << index_bit));
	//printf("index_bit = %d (%d), CRC buffer size = %d\n", index_bit, 1 << index_bit, i);
	if (short_count > 0){
		i += sizeof(int) * entity_num + ((entity_num + 3) & ~3);	// short_crc 用の領域
	}
	order = (int *)malloc(i);
	if (order == NULL)
		return -1;	// CRC-32 すら比較できないので簡易検査もできない
	j = 0;
	for (num = 0; num < entity_num; num++){
		if ((files[num].state & 0x80) == 0){	// チェックサムがあるファイルだけ比較する
			file_block = (int)(files[num].size / (__int64)block_size);	// フルサイズのブロックの数
			for (i = files[num].b_off; i < files[num].b_off + file_block; i++){
				order[j * 2    ] = i;	// 最初はブロック番号にしておく
				order[j * 2 + 1] = s_blk[i].crc;
				j++;
			}
		}
	}
//	for (j = 0; j < block_count; j++)
//		printf("order[%3d] = %3d, %08x\n", j, order[j * 2], order[j * 2 + 1]);

	// 昇順に並び替える
	qsort(order, block_count, sizeof(int) * 2, sort_cmp_crc);
	// [番号, CRC] の順を並び替えて [番号] だけにする
	for (i = 1; i < block_count; i++)
		order[i] = order[i * 2];	// 番号を前に移動させる
	// 並び替えられた CRC も記録しておく
	for (i = 0; i < block_count; i++){
		order[block_count + i] = s_blk[order[i]].crc;	// 並び替えた後の順序にする
		//printf("crc[%2d]: Block[%2d], 0x%08X\n", i, order[i], s_blk[order[i]].crc);
	}
	// インデックス・サーチ用の目次を作る
	init_index_search(order + block_count, block_count, order + (block_count * 2), index_bit);
	sc->index_shift = 32 - index_bit;
	sc->order = order;

	// スライド検査しないならここまで
	if (switch_v & 1)
		return 0;

	// 詳細検査用の作業領域を確保する
	//printf("\n block_size = %d, allocation size = %I64d\n", block_size, (__int64)block_size * 3);
	if ((size_t)block_size * 3 < get_mem_size(0))
		sc->buf = (unsigned char *)malloc((size_t)block_size * 3);
	if (sc->buf == NULL)
		return 1;	// メモリー不足でスライド検査できない、簡易検査にする

//time_last = GetTickCount();
	if (short_count > 0){	// 半端なブロックを比較する準備
		short_crcs = order + ((block_count * 2) + (1 << index_bit));
		short_use = (unsigned char *)(short_crcs + entity_num);
		for (num = 0; num < entity_num; num++){
			short_use[num] = 0;	// チェックサムなし、または半端なブロックでない
			if ((files[num].state & 0x80) == 0){	// チェックサムがあるファイルだけ比較する
				last_size = (unsigned int)(files[num].size % (__int64)block_size);
				if (last_size > 0){
					//file_block = (int)(files[num].size / (__int64)block_size);	// フルサイズのブロックの数
					//j = files[num].b_off + file_block;	// 末尾ブロックの番号
					//printf("block[%4d] : size = %d, pad = %d\n", j, s_blk[j].size, block_size - s_blk[j].size);
					// ブロック・サイズが大きいとパディング部分を取り除くのに時間がかかるから、必要になってから行う
					//short_crcs[num] = crc_reverse_zero(s_blk[j].crc, block_size - s_blk[j].size);
					if (files[num].b_num > 1){
						short_use[num] = 2;	// 末尾の半端なブロック
					} else {
						short_use[num] = 1;	// 小さなファイル
					}
				}
			}
		}
	}
//time_last = GetTickCount() - time_last;
//printf("CRC setup: %d mil sec\n", time_last);

	// 読み込み用スレッドの準備をする
	sc->run = CreateEvent(NULL, FALSE, FALSE, NULL);	// 両方とも Auto Reset にする
	if (sc->run == NULL){
		free(sc->buf);
		sc->buf = NULL;
		return 1;
	}
	sc->end = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (sc->end == NULL){
		free(sc->buf);
		sc->buf = NULL;
		CloseHandle(sc->run);
		sc->run = NULL;
		return 1;
	}
	//_mm_sfence();	// メモリーへの書き込みを完了してからスレッドを起動する
	sc->h = (HANDLE)_beginthreadex(NULL, STACK_SIZE, thread_read, (LPVOID)sc, 0, NULL);
	if (sc->h == NULL){
		free(sc->buf);
		sc->buf = NULL;
		CloseHandle(sc->run);
		CloseHandle(sc->end);
		sc->run = NULL;
		sc->end = NULL;
		return 1;
	}

	return 0;
}

// スライス断片を比較用にメモリー上に一時保管する
void save_flake(
	unsigned char *buf,
	int id,			// ブロック番号
	int flake_size,	// 断片のサイズ
	int side,		// どちら側か 1=front, 0=rear
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
				//printf("flake[%d] = %d, size = %d, add %d ok\n", i, id, flake_size, side);
				if (side){	// 前半に上書きコピーする
					memcpy(p, buf, flake_size);
					fc->front_size = flake_size;
				} else {	// 後半に上書きコピーする
					memcpy(p + (block_size - flake_size), buf, flake_size);
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
		//printf("mem_size = %zu MB, max_count = %d, flake_count = %d \n", mem_size >> 20, max_count, sc->flake_count);
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
		//printf("slide from %p to %p, %zu bytes \n", p + sizeof(flake_ctx) + block_size, p, mem_size);
		memmove(p, p + sizeof(flake_ctx) + block_size, mem_size);
		rv = sc->flake_count - 1;	// 前にずらして末尾領域に保管する
	}

	//printf("flake[%d] = %d, size = %u, save %d ok\n", rv, id, flake_size, side);
	p = sc->flk_buf + (sizeof(flake_ctx) + (size_t)block_size) * rv;
	fc = (flake_ctx *)p;
	p += sizeof(flake_ctx);
	if (side){	// 前半に上書きコピーする
		memcpy(p, buf, flake_size);
		fc->front_size = flake_size;
		fc->rear_size = 0;
	} else {	// 後半に上書きコピーする
		memcpy(p + (block_size - flake_size), buf, flake_size);
		fc->front_size = 0;
		fc->rear_size = flake_size;
	}
	fc->id = id;
}

// 保管されてるスライス断片を調べて完全なスライスを探す
int check_flake(
	wchar_t *temp_path,			// 作業用、基準ディレクトリが入ってる
	file_ctx_r *files,			// 各ソース・ファイルの情報
	source_ctx_r *s_blk,		// 各ソース・ブロックの情報
	slice_ctx *sc)
{
	unsigned char *p, err_mag;
	int i, j, rv, num, err_off;
	flake_ctx *fc;

	p = sc->flk_buf;
	for (j = 0; j < sc->flake_count; j++){
		fc = (flake_ctx *)p;
		p += sizeof(flake_ctx);
		i = fc->id;
		if (i != -1){	// スライス断片が記録されてる
			if (s_blk[i].exist != 0){	// ブロックの断片を保管した後に、そのブロックが発見されていたら
				fc->id = -1;	// 不要になった領域を空ける
				//printf("flake[%d] = %d, release\n", j, i);
			// 両側のスライス断片が揃ってる場合だけ、スライス断片の合計がブロック・サイズ以上になる
			} else if ((unsigned int)(fc->front_size) + (unsigned int)(fc->rear_size) >= block_size){
				// 分割されてるか試すスライスはブロック・サイズのものだけ
				rv = correct_error(p, s_blk[i].size, s_blk[i].hash, s_blk[i].crc, &err_off, &err_mag);
				//printf("flake[%d] = %d, check = %d\n", j, i, rv);
				if (rv >= 0){	// 完全なブロックを新たに発見した (発見済みは先に除外してる)
					if (switch_v & 16){	// コピーする
						num = s_blk[i].file;
						if (open_temp_file(temp_path, num, files, sc))
							return 1;
						if (file_write_data(sc->hFile_tmp,
								(__int64)(i - files[num].b_off) * (__int64)block_size, p, s_blk[i].size)){
							printf("file_write_data, %d\n", i);
							return 1;
						}
					}
					s_blk[i].exist = 0x1002;	// このファイルで断片が一致した印
					first_num++;
				}
				fc->id = -1;	// 結果にかかわらず、比較が終わったら領域を空ける
			}
		}
		p += block_size;	// 次の保管領域へ
	}

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define MISS_LIMIT 6

// ソース・ブロックのチェックサムを調べる
// ファイル番号が既知な場合は、前後に同時にゴミが付かなければ位相のずれにも対応する
// ブロック単位での読み込みエラーを無視して継続する
// -2=キャンセル, -1=エラー, 0=見つからない, 1～=見つかった個数
static int search_block_simple(
	wchar_t *temp_path,		// 作業用、基準ディレクトリが入ってる
	int num1,				// file_ctx におけるファイル番号 (未知なら -1)
	unsigned int last_size,	// 末尾ブロックのサイズ
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	__int64 file_size,		// 存在するファイルのサイズ
	__int64 last_off,		// どこから検査を開始するか (ここまでは既に検査済み)
	int flag_split,			// 0以外 = 後方からは検索しない
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)
{
	unsigned char hash[20];
	int i, j, b_last, find_num, find_flag, find_next, short_next;
	int block_count, num, i1, i2, i3, i4, miss_num, miss_max;
	int *order, *index, index_shift;
	unsigned int crc, *crcs;
	unsigned int time_last;
	__int64 file_off, short_off;

	if (file_size < last_off + last_size)
		return 0;	// 小さすぎるファイルは調べない
	block_count = sc->block_count;
	miss_num = 0;	// 何連続で検出できなかったか
	miss_max = (int)((file_size >> 4) / (__int64)block_size);	// size/16 ブロック個まで (6%)
	if (miss_max < MISS_LIMIT)
		miss_max = MISS_LIMIT;
	find_num = 0;	// このファイル内で何ブロック見つけたか
	find_next = -1;	// 次に見つかると予想したブロックの番号
	short_next = -1;
	if (num1 >= 0){	// ファイル番号が指定されてるなら
		b_last = files[num1].b_off + files[num1].b_num - 1;	// 末尾ブロックの番号
		find_next = (int)(last_off / (__int64)block_size);	// 検査開始位置のブロック番号
		find_next += files[num1].b_off;
		if ((last_off < files[num1].size) &&	// ファイル本来のサイズよりも小さいなら、末尾ブロックは未検出
				(last_size < block_size)){		// 末尾の半端なブロックの番号と想定位置
			short_next = b_last;
			if (find_next == short_next)
				find_next = -1;	// 予想が重複したら末尾ブロックとして探す
			short_off = files[num1].size - last_size;
		}
		//printf("file = %d, find_next = %d\n", num1, find_next);
		//printf("short_off = %I64d, short_next = %d\n", short_off, short_next);
	} else if (file_size < (__int64)block_size){	// ブロック・サイズよりも小さければ
		// ファイル・サイズ以下の半端なスライスを含むファイルを探す
		find_flag = -1;
		for (num = 0; num < entity_num; num++){	// ファイル番号
			if (files[num].size == file_size){	// サイズが同じなら
				// ハッシュ値を計算する
				if (find_flag < 0){
					if (file_md5_crc32_block(hFile, 0, (unsigned int)(files[num].size), hash) != 0)
						break;	// エラーなら比較を終了する
					find_flag = 0;
				}
				if (find_flag == 0){
					i = files[num].b_off;
					if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
						if (s_blk[i].exist == 0){	// 未発見のブロックなら
							if (switch_v & 16){	// コピーする
								if (open_temp_file(temp_path, num, files, sc))
									return -1;
								if (file_copy_data(hFile, 0, sc->hFile_tmp, 0, s_blk[i].size)){
									printf("file_copy_data, %d\n", i);
									return -1;
								}
							}
							s_blk[i].exist = 2;
							first_num++;
						}
						find_num++;
						write_ini_verify(i, 0, 0);
						s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
						//printf("file size = %I64d, slice size = %d, T %d, F %d\n", file_size, s_blk[i].size, i, num);	// Tiny
						break;
					}
				}
			}
		}
		return find_num;
/* 複数のファイルと比較すると重くなるかも？
		crc = 0;
		for (num = 0; num < entity_num; num++){	// ファイル番号
			if (files[num].size <= file_size){
				// ハッシュ値を計算する
				if (crc != (unsigned int)(files[num].size)){
					if (file_md5_crc32_block(hFile, 0, (unsigned int)(files[num].size), hash) != 0)
						break;	// エラーなら比較を終了する
					crc = (unsigned int)(files[num].size);	// 計算した際のサイズを記録しておく
				}
				if (crc == (unsigned int)(files[num].size)){
					i = files[num].b_off;
					if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
						if (s_blk[i].exist == 0){	// 未発見のブロックなら
							if (switch_v & 16){	// コピーする
								if (open_temp_file(temp_path, num, files, sc))
									return -1;
								if (file_copy_data(hFile, 0, sc->hFile_tmp, 0, s_blk[i].size)){
									printf("file_copy_data, %d\n", i);
									return -1;
								}
							}
							s_blk[i].exist = 2;
							first_num++;
						}
						find_num++;
						write_ini_verify(i, 0, 0);
						s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
						//printf("file size = %I64d, slice size = %d, T %d, F %d\n", file_size, s_blk[i].size, i, num);	// Tiny
					}
				}
			}
		}
		return find_num;
*/
	}
	// インデックス・サーチ用
	order = sc->order;
	crcs = order + block_count;
	index = crcs + block_count;
	index_shift = sc->index_shift;
	time_last = GetTickCount();

	// ブロック・サイズごとに探す
	if (block_count > 0){
		// 前から順にチェックサムを比較する
		file_off = last_off;	// 検査開始位置から調べる
		//printf("search from %I64d, file %d, next = %d\n", file_off, num1, find_next);
		while (file_off + (__int64)block_size <= file_size){
			find_flag = -2;
			// 次の番号のブロックがその位置にあるかを先に調べる (発見済みでも)
			if ((short_next >= 0) && (file_off == short_off)){
				i = short_next;
				if (file_md5_crc32_block(hFile, file_off, s_blk[i].size, hash) != 0){
					return find_num;	// エラーなら検査を終了する
				} else {
					if (memcmp(hash, s_blk[i].hash, 20) == 0)	// チェックサムが一致するか確かめる
						find_flag = 0;
				}
			}
			if (find_flag < 0){
				if (file_md5_crc32_block(hFile, file_off, block_size, hash) != 0)
					return find_num;	// エラーなら検査を終了する
				if ((find_next >= 0) && (memcmp(hash, s_blk[find_next].hash, 20) == 0)){	// チェックサムが一致するか確かめる
					i = find_next;
					find_flag = 3;
				}
				if (find_flag < 0){	// 予想したブロックが見つからなかった場合は全てのブロックと比較する
					memcpy(&crc, hash + 16, 4);
					crc ^= window_mask;	// 記録されてるチェックサムから初期値と最終処理の 0xFFFFFFF を取り除く
					// CRC-32 が一致する番号
//					j = binary_search(order, block_count, crc, s_blk);
//					j = index_search(crcs, block_count, crc, index, index_shift);
					j = 0x00FFFFFF;	// 見つからなかった
					i = index[crc >> index_shift];	// 配列内のどこにあるか
					// リニア・サーチで残りから探す
					while (i < block_count){
						if (crcs[i] > crc)
							break;	// 上回るなら目的の値は存在しない
						if (crcs[i] == crc){
							j = i;
							break;
						}
						i++;
					}
					// 一致するブロックを優先度別に記録する
					i1 = i2 = i3 = i4 = -1;
					for (; j < block_count; j++){	// 複数ブロックが一致するかもしれないので
						if (crcs[j] != crc)
							break;	// CRC-32 が一致しなくなったら抜ける
						i = order[j];
						if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
							find_flag = 4;
							if ((s_blk[i].exist & 0x1000) == 0){	// このファイル内で未発見のブロックを優先的に探す (内容が重複してる場合に順序を保つ為)
								if (s_blk[i].file == num1){	// 指定されたファイル番号のブロック
									i1 = i;
									break;	// 最優先のブロックを見つけた場合は直ちにループから抜ける
								} else {
									i2 = i;
									if (num1 < 0)
										break;
								}
							} else {	// このファイル内で発見済みのブロックも比較する (内容をコピーして追加した改変を認識する)
								if (s_blk[i].file == num1){
									i3 = i;
								} else {
									i4 = i;
								}
							}
						}
					}
					// 最も優先度の高いブロックとして認識する
					if (i1 >= 0){
						i = i1;	// 指定されたファイル番号で未発見のブロック
					} else if (i2 >= 0){
						i = i2;	// 他のファイル番号で未発見のブロック
					} else if (i3 >= 0){
						i = i3;	// 指定されたファイル番号で発見済みのブロック
					} else if (i4 >= 0){
						i = i4;	// 他のファイル番号で発見済みのブロック
					}
				}
			}
			if (find_flag >= 0){	// その位置でブロックのチェックサムが一致すれば
				num = s_blk[i].file;	// 見つけたブロックが属するファイル番号
				if (s_blk[i].exist == 0){	// 未発見のブロックなら
					if (switch_v & 16){	// コピーする
						if (open_temp_file(temp_path, num, files, sc))
							return -1;
						if (file_copy_data(hFile, file_off, sc->hFile_tmp,
								(__int64)(i - files[num].b_off) * (__int64)block_size, s_blk[i].size)){
							printf("file_copy_data, %d\n", i);
							return -1;
						}
					}
					s_blk[i].exist = 2;
					first_num++;
				}
				if ((s_blk[i].exist & 0x1000) == 0){	// 同一ファイルで重複していても一回だけ記録する
					find_num++;
					write_ini_verify(i, 0, file_off);
				}
				s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
/*				if (find_flag == 0){
					printf("%9I64d : L %d, F %d\n", file_off, i, s_blk[i].file);	// Last
				} else if (find_flag == 3){
					printf("%9I64d : N %d, F %d\n", file_off, i, s_blk[i].file);	// Next
				} else {
					printf("%9I64d : S %d, F %d\n", file_off, i, s_blk[i].file);	// Search
				}*/
				// 次に見つかるブロックを予測する
				find_next = i + 1;
				if (find_next >= source_num){	// 最後までいった
					short_next = -1;
					find_next = -1;
				} else if (s_blk[find_next].file != num){	// 異なるファイルは連続しない
					short_next = -1;
					find_next = -1;
				} else if (s_blk[find_next].size < block_size){	// 半端なブロックは別に調べる
					short_next = find_next;
					short_off = file_off + block_size;
					//printf("short_off = %I64d, short_next = %d, file = %d\n", short_off, short_next, num);
					find_next = -1;
				} else {
					short_next = files[num].b_off + files[num].b_num - 1;
					if (s_blk[short_next].size < block_size){	// 半端なブロックは別に調べる
						short_off = file_off + (__int64)(short_next - i) * (__int64)block_size;
						//printf("short_off = %I64d, short_next = %d, file = %d\n", short_off, short_next, num);
					} else {
						short_next = -1;
					}
				}
				file_off += s_blk[i].size;
				last_off = file_off;	// 最後に見つけたブロックの終端
				miss_num = 0;
			} else {
				file_off += block_size;
				miss_num++;
				//printf("miss = %d / %d\n", miss_num, miss_max);
				if (miss_num >= miss_max)
					break;
			}
			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress_file((int)((file_off * 1000) / file_size), first_num, file_name))
					return -2;
				time_last = GetTickCount();
			}
		}
	}
	// 末尾の半端なブロックの検査
	if ((short_next >= 0) && (short_off + s_blk[short_next].size <= file_size) && ((s_blk[short_next].exist & 0x1000) == 0)){
		file_off = short_off;
		i = short_next;
		if (file_md5_crc32_block(hFile, file_off, s_blk[i].size, hash) != 0){
			return find_num;	// エラーなら検査を終了する
		} else {
			if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
				num = s_blk[i].file;	// 見つけたブロックが属するファイル番号
				if (s_blk[i].exist == 0){	// 未発見のブロックなら
					if (switch_v & 16){	// コピーする
						if (open_temp_file(temp_path, num, files, sc))
							return -1;
						if (file_copy_data(hFile, file_off, sc->hFile_tmp,
								(__int64)(i - files[num].b_off) * (__int64)block_size, s_blk[i].size)){
							printf("file_copy_data, %d\n", i);
							return -1;
						}
					}
					s_blk[i].exist = 2;
					first_num++;
				}
				if ((s_blk[i].exist & 0x1000) == 0){	// 同一ファイルで重複していても一回だけ記録する
					find_num++;
					write_ini_verify(i, 0, file_off);
				}
				s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
				//printf("%9I64d : L2 %d, F %d\n", file_off, i, s_blk[i].file);	// Last
			}
		}
	}
	if ((num1 >= 0) && (files[num1].b_num >= 2) && (file_size >= last_size) && ((s_blk[b_last].exist & 0x1000) == 0)){
		if (file_size >= files[num1].size){	// 本来のサイズよりも大きいなら
			file_off = files[num1].size - last_size;
		} else if (file_size >= (__int64)block_size + last_size){	// ブロック倍よりも大きいなら
			file_off = last_off;
		} else {	// 末尾ブロックのサイズなら
			file_off = 0;
		}
		if (file_md5_crc32_block(hFile, file_off, last_size, hash) != 0){
			return find_num;	// エラーなら検査を終了する
		} else {
			i = b_last;
			if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
				if (s_blk[i].exist == 0){	// 未発見のブロックなら
					if (switch_v & 16){	// コピーする
						if (open_temp_file(temp_path, num1, files, sc))
							return -1;
						if (file_copy_data(hFile, file_off, sc->hFile_tmp, file_off, last_size)){
							printf("file_copy_data, %d\n", i);
							return -1;
						}
					}
					s_blk[i].exist = 2;
					first_num++;
				}
				find_num++;
				write_ini_verify(i, 0, file_off);
				s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
				//printf("%9I64d : L1 %d, F %d\n", file_off, i, s_blk[i].file);	// Last
			}
		}
	}

	// ファイル番号が指定されてない場合は後方からの検索をしない
	// 最後に見つかったブロックがファイルの終端なら、それ以上の検査をしない
	if ((num1 < 0) || (file_size <= last_off + last_size))
		return find_num;
	// 指定されたファイルと同じサイズなら、位相のずれは無いとみなす
	if ((miss_num < miss_max) && (file_size == files[num1].size))
		return find_num;
	// 分割ファイルなら後方からの検索をしない (類似名ファイルとはサイズで区別する)
	if ((flag_split != 0) && (file_size <= files[num1].size / 2))
		return find_num;

	// 末尾ブロックの検査
	file_off = file_size - last_size;	// 末尾にあるはずの終端ブロックの位置
	miss_num = 0;
	if ((s_blk[b_last].exist & 0x1000) == 0){
		miss_num = 1;
		if (file_off >= last_off){
			if (file_md5_crc32_block(hFile, file_off, last_size, hash) != 0)
				return find_num;	// エラーなら検査を終了する
			i = b_last;
			if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
				if (s_blk[i].exist == 0){	// 未発見のブロックなら
					if (switch_v & 16){	// コピーする
						if (open_temp_file(temp_path, num1, files, sc))
							return -1;
						if (file_copy_data(hFile, file_off, sc->hFile_tmp, files[num1].size - last_size, last_size)){
							printf("file_copy_data, %d\n", i);
							return -1;
						}
					}
					s_blk[i].exist = 2;
					first_num++;
				}
				find_num++;
				write_ini_verify(i, 0, file_off);
				s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
				//printf("%9I64d : RL %d, F %d\n", file_off, i, s_blk[i].file);	// Last
				miss_num = 0;
			}
		}
	}
	// ファイル内にブロックが複数あるなら遡って探す
	if (files[num1].b_num >= 2){	// 常に block_count > 0 になる
		file_off -= block_size;	// 一個前の位置にしておく
		//printf("sencond search from = %I64d to %I64d\n", file_off, last_off);
		find_next = b_last - 1;
		while (file_off > last_off){	// 前方からの検索で見つけた最後のブロックまで
			find_flag = -2;
			// 次の番号のブロックがその位置にあるかを先に調べる (発見済みでも)
			if (file_md5_crc32_block(hFile, file_off, block_size, hash) != 0){
				return find_num;	// エラーなら検査を終了する
			} else {
				if ((find_next >= 0) && (memcmp(hash, s_blk[find_next].hash, 20) == 0)){	// チェックサムが一致するか確かめる
					i = find_next;
					find_flag = 3;
				}
				if (find_flag < 0){
					memcpy(&crc, hash + 16, 4);
					crc ^= window_mask;	// 記録されてるチェックサムから初期値と最終処理の 0xFFFFFFF を取り除く
					// CRC-32 が一致する番号
//					j = binary_search(order, block_count, crc, s_blk);
//					j = index_search(crcs, block_count, crc, index, index_shift);
					j = 0x00FFFFFF;	// 見つからなかった
					i = index[crc >> index_shift];	// 配列内のどこにあるか
					// リニア・サーチで残りから探す
					while (i < block_count){
						if (crcs[i] > crc)
							break;	// 上回るなら目的の値は存在しない
						if (crcs[i] == crc){
							j = i;
							break;
						}
						i++;
					}
					// 一致するブロックを優先度別に記録する
					i1 = i2 = i3 = i4 = -1;
					for (; j < block_count; j++){	// 複数ブロックが一致するかもしれないので
						if (crcs[j] != crc)
							break;	// CRC-32 が一致しなくなったら抜ける
						i = order[j];
						if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
							find_flag = 4;
							if ((s_blk[i].exist & 0x1000) == 0){	// このファイル内で未発見のブロックを優先的に探す
								if (s_blk[i].file == num1){	// 指定されたファイル番号のブロック
									i1 = i;
									break;	// 最優先のブロックを見つけた場合は直ちにループから抜ける
								} else {
									i2 = i;
								}
							} else {	// このファイル内で発見済みのブロック
								if (s_blk[i].file == num1){
									i3 = i;
								} else {
									i4 = i;
								}
							}
						}
					}
					// 最も優先度の高いブロックとして認識する (前方からの時とは順序が異なる)
					if (i1 >= 0){
						i = i1;	// 指定されたファイル番号で未発見のブロック
					} else if (i3 >= 0){
						i = i3;	// 指定されたファイル番号で発見済みのブロック
					} else if (i2 >= 0){
						i = i2;	// 他のファイル番号で未発見のブロック
					} else if (i4 >= 0){
						i = i4;	// 他のファイル番号で発見済みのブロック
					}
				}
			}
			if (find_flag >= 0){	// その位置でブロックのチェックサムが一致すれば
				num = s_blk[i].file;	// 見つけたブロックが属するファイル番号
				if (s_blk[i].exist == 0){	// 未発見のブロックなら
					if (switch_v & 16){	// コピーする
						if (open_temp_file(temp_path, num, files, sc))
							return -1;
						if (file_copy_data(hFile, file_off, sc->hFile_tmp,
								(__int64)(i - files[num].b_off) * (__int64)block_size, block_size)){
							printf("file_copy_data, %d\n", i);
							return -1;
						}
					}
					s_blk[i].exist = 2;
					first_num++;
				}
				if ((s_blk[i].exist & 0x1000) == 0){	// 同一ファイルで重複していても一回だけ記録する
					find_num++;
					write_ini_verify(i, 0, file_off);
				}
				s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
/*				if (find_flag == 3){
					printf("%9I64d : RN %d, F %d\n", file_off, i, s_blk[i].file);	// Next
				} else {
					printf("%9I64d : RS %d, F %d\n", file_off, i, s_blk[i].file);	// Search
				}*/
				// 次に見つかるブロックを予測する
				find_next = i - 1;
				if (find_next < 0){	// 最初までいった
					find_next = -1;
				} else if (s_blk[find_next].file != num){	// 異なるファイルは連続しない
					find_next = -1;
				}
				miss_num = 0;
			} else {
				miss_num++;
				//printf("miss = %d / %d\n", miss_num, miss_max);
				if (miss_num >= miss_max)
					break;
			}
			file_off -= block_size;
			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress_file((int)(((file_size - file_off) * 1000) / (file_size - last_off)), first_num, file_name))
					return -2;
				time_last = GetTickCount();
			}
		}
	}

	return find_num;
}

#define FAIL_MAX 8
#define FAIL_LIMIT 10
#define FAIL_TIME 128

// スライド検査でソース・ブロックを探す (サブ・スレッドがブロックを読み込む)
// ブロック・サイズ * 3 < 4GB であること
// -2=キャンセル, -1=エラー, 0=見つからない, 1～=見つかった個数
static int search_block_slide(
	wchar_t *temp_path,		// 作業用、基準ディレクトリが入ってる
	int num1,				// file_ctx における初期状態のファイル番号 (未知なら -1)
	unsigned int last_size,	// 末尾ブロックのサイズ
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	__int64 file_size,		// 存在するファイルのサイズ
	__int64 last_off,		// どこから検査を開始するか (ここまでは既に検査済み)
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)
{
	unsigned char *buf, hash[16], hash2[16], err_mag, *short_use;
	int i, j, find_num, find_flag, find_next, find_last, short_next, short2_next, tmp_next;
	int block_count, short_count, tiny_count, tiny_skip, num, i1, i2, i3, i4;
	int *order, *index, index_shift;
	unsigned int len, off, end_off, err_off;
	unsigned int prev_crc, fail_count, rear_off, overlap_count;
	unsigned int crc, *crcs, *short_crcs;
	unsigned int time_last, time_slide;
	__int64 file_off, file_next, short_off, short2_off, tmp_off, fail_off;

	if (file_size + 1 < last_off + (__int64)(sc->min_size))
		return 0;	// 小さすぎるファイルは調べない
	find_num = 0;	// このファイル内で何ブロック見つけたか
	find_next = -1;	// 次に見つかると予想したブロックの番号
	find_last = -1;	// 最後に見つけたブロックの番号 (-1=不明)
	short_next = -1;	// 予想される末尾ブロックの番号
	short2_next = -1;
	fail_count = 0;	// CRC は一致したけど MD5 が違った回数
	fail_off = 0;
	rear_off = 0;
	if (num1 >= 0){	// ファイル番号が指定されてるなら
		find_next = (int)(last_off / (__int64)block_size);	// 検査開始位置のブロック番号
		find_next += files[num1].b_off;
		if (last_off > 0)	// 完全なブロックが何個か見つかってるなら
			find_last = find_next - 1;	// 最後に見つけたブロックの番号
		if ((last_off >= files[num1].size) || (last_off + block_size > file_size + 1))
			find_next = -1;	// 予想位置がファイル・サイズを超えると駄目
		if ((last_size < block_size) && (last_off < files[num1].size)){	// 末尾の半端なブロックの番号と想定位置
			tmp_next = files[num1].b_off + files[num1].b_num - 1;	// 末尾ブロックの番号
			if (find_next == tmp_next)
				find_next = -1;	// 予想が重複したら末尾ブロックとして探す
			if ((files[num1].b_num >= 2) && (files[num1].size <= file_size + 1)){	// 本来の位置を調べる
				short_next = tmp_next;
				short_off = files[num1].size - last_size;
			} else if ((last_off == 0) && (file_size == last_size)){	// ファイルが1ブロック未満でも、同じサイズならエラー訂正を試みる
				short_next = tmp_next;
				short_off = 0;
			}
			if (last_size < file_size){	// 末尾を調べる
				short2_next = tmp_next;
				short2_off = file_size - last_size;
			}
		}
		if (file_size > files[num1].size){
			rear_off = (unsigned int)((file_size - files[num1].size) % (__int64)block_size);
		} else if (file_size < files[num1].size){
			rear_off = block_size - (unsigned int)((files[num1].size - file_size) % (__int64)block_size);
		}
/*		printf("file = %d, find_next = %d, find_last = %d, rear_off = %d\n", num1, find_next, find_last, rear_off);
		if (short_next >= 0)
			printf("short_off  = %I64d, short_next  = %d\n", short_off, short_next);
		if (short2_next >= 0)
			printf("short2_off = %I64d, short2_next = %d\n", short2_off, short2_next);*/
	}
	file_off = last_off;	// 検査開始位置から調べる
	buf = sc->buf;
	// インデックス・サーチ用
	block_count = sc->block_count;
	order = sc->order;
	crcs = order + block_count;
	index = crcs + block_count;
	index_shift = sc->index_shift;
	// 半端なブロックのパディング部分を取り除いた CRC-32
	short_count = sc->short_count;
	short_crcs = index + (unsigned int)(1 << (32 - index_shift));
	short_use = (unsigned char *)(short_crcs + entity_num);
	tiny_count = 0;
	tiny_skip = 0;

	// 最初はブロック・サイズの倍まで読み込む
	if (file_size < file_off + ((__int64)block_size * 2)){
		len = (unsigned int)(file_size - file_off);
		memset(buf + len, 0, (block_size * 2) - len);
	} else {
		len = block_size * 2;
	}
	if (file_read_data(hFile, file_off, buf, len))
		return 0;	// エラーなら検査を終了する
	if (file_size <= file_off + (__int64)block_size){	// 検査範囲がブロック・サイズ以下なら
		file_next = file_size + 1;	// 最初のループで検査を終える
	} else {
		file_next = file_off + len;	// ファイル・ポインターの位置
	}
	sc->hFile = hFile;
	time_last = GetTickCount();
	if (short_count > 0){	// 小さなファイルの個数
		for (num = 0; num < entity_num; num++){
			if (short_use[num] & 1)
				tiny_count++;
		}
	}
	// 小さなファイルの半端なブロックの検査
	if ((short_count > 0) && (last_off == 0)){
		//printf("tiny search from %I64d, size = %d, count = %d, %d\n", file_off, len, short_count, tiny_count);
		for (num = 0; num < entity_num; num++){	// ファイル番号
			if (short_use[num] == 0)
				continue;	// 半端なブロックを含まないファイルは除外する
			i = files[num].b_off + files[num].b_num - 1;	// 末尾ブロックの番号
			last_size = s_blk[i].size;
			// 未発見で、検査データより小さな、半端な末尾ブロックなら
			if ((last_size <= len) && ((s_blk[i].exist & 0x1000) == 0)){
				if ((short_use[num] & 4) == 0){	// パディング部分を取り除いた CRC-32 を逆算する
					short_crcs[num] = crc_reverse_zero(s_blk[i].crc, block_size - last_size);
					short_use[num] |= 4;	// CRC-32 計算済みの印
				}
				//printf("tiny %d, size = %d, CRC = 0x%08X, 0x%08X\n", num, last_size, crc, short_crcs[num]);
				if (crc_update(0, buf, last_size) == short_crcs[num]){	// CRC-32 が一致した
					data_md5_block(buf, last_size, hash);
					if (memcmp(hash, s_blk[i].hash, 16) == 0){	// 更に MD5 も一致すれば
						if (s_blk[i].exist == 0){	// 未発見のブロックなら
							if (switch_v & 16){	// コピーする
								if (open_temp_file(temp_path, num, files, sc))
									return -1;
								if (file_write_data(sc->hFile_tmp,
										(__int64)(i - files[num].b_off) * (__int64)block_size, buf, last_size)){
									printf("file_write_data, %d\n", i);
									return -1;
								}
							}
							s_blk[i].exist = 2;
							first_num++;
						}
						if ((s_blk[i].exist & 0x1000) == 0){	// 同一ファイルで重複していても一回だけ記録する
							find_num++;
							write_ini_verify(i, 0, file_off);
						}
						s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
						//printf("%9I64d,%8d : T %d, F %d\n", file_off, last_size, i, s_blk[i].file);	// Tiny
						if (last_off < file_off + last_size)
							last_off = file_off + last_size;	// 一番大きな半端なブロックの終端
						find_next = -2;	// 小さなファイルが見つかった = ブロック検出の予想が外れた
						if (i == short_next){	// この末尾ブロックは検出済み
							short_next = -1;
						} else if (i == short2_next){
							short2_next = -1;
						}

						// 経過表示
						if (GetTickCount() - time_last >= UPDATE_TIME){
							if (print_progress_file(0, first_num, file_name))
								return -2;
							time_last = GetTickCount();
						}
					}	// 他のより大きなファイルとも一致してるかもしれないので続行する
				}
			}
		}
		if (find_next != -2){	// 小さなファイルが見つからなかったなら
			tiny_skip = -1;
			if ((*((unsigned short *)buf) == 0x7A37) && (*((unsigned int *)(buf + 2)) == 0x1C27AFBC)){	// 7-Zip書庫の SignatureHeader
				tiny_skip = 32;
				//printf("tiny_skip = 32 (7-Zip header size)\n");
			} else {
				i1 = 0;
				while (*((unsigned int *)(buf + i1)) == 0x04034b50){	// ZIP書庫の local file header
					i2 = *((unsigned int *)(buf + i1 + 18));	// compressed size
					i3 = *((unsigned int *)(buf + i1 + 22));	// uncompressed size
					if (i2 != i3)
						break;	// 圧縮されてる
					i3 = *((unsigned short *)(buf + i1 + 26));	// file name length
					i4 = *((unsigned short *)(buf + i1 + 28));	// extra field length
					if ((__int64)i1 + 30 + i3 + i4 + i2 > file_size)
						break;	// ファイルが途中まで
					i1 += 30 + i3 + i4;
					if (i2 > 0){	// 空のファイル（サブ・ディレクトリ）は無視する
						tiny_skip = i1;
						//printf("tiny_skip = %d (ZIP header size + %d + %d)\n", tiny_skip, i3, i4);
						break;
					}
				}
			}
		}
	}

	// ブロック・サイズごとに探す
	if (((block_count > 0) && ((file_off + (__int64)block_size <= file_size)
			|| (find_next >= 0))) || (short_next >= 0) || (short2_next >= 0)){	// ブロックの位置を予想して探す
		// 前からスライドさせながらチェックサムを比較する
		//printf("slide search from %I64d, file %d, next = %d\n", file_off, num1, find_next);
		off = 0;	// buf 内でのオフセット
		if (file_size >= (__int64)block_size){
			end_off = block_size;
		} else {
			end_off = (unsigned int)file_size;
		}
		crc = crc_update(0, buf, block_size);
		while (1){
			//printf("Loop from %9I64d+%8d for %d\n", file_off, off, end_off);
			if (file_next < file_size){	// ファイルの終端に達していなければ、残りを読み込む
				if ((file_next + (__int64)block_size) > file_size){
					len = (unsigned int)(file_size - file_next);
				} else {
					len = block_size;
				}
				sc->size = len;
				//_mm_sfence();
				SetEvent(sc->run);	// サブ・スレッドに読み込みを開始させる
				//printf("read from %I64d, %d byte\n", file_next, len);
			}

			time_slide = GetTickCount();	// スライド検査の開始時刻を記録しておく
			overlap_count = 0;	// ブロック・サイズをスライドする間に何回見つけたか
			while (off < end_off){
				find_flag = -2;
				// 次の番号のブロックがその位置にあるかを先に調べる (発見済みでも)
				if (((short_next >= 0) && (file_off + off == short_off)) ||
						((short2_next >= 0) && (file_off + off == short2_off))){	// 半端なブロックなら
					if ((short_next >= 0) && (file_off + off == short_off)){
						i = short_next;
					} else {
						i = short2_next;
					}
					num = s_blk[i].file;
					if ((short_use[num] & 4) == 0){	// パディング部分を取り除いた CRC-32 を逆算する
						short_crcs[num] = crc_reverse_zero(s_blk[i].crc, block_size - s_blk[i].size);
						short_use[num] |= 4;	// CRC-32 計算済みの印
					}
					//printf("short[%d], size = %d, short_crc = 0x%08X\n", i, s_blk[i].size, short_crcs[num]);
					find_flag = correct_error(buf + off, s_blk[i].size, s_blk[i].hash, short_crcs[num], &err_off, &err_mag);
					if (find_flag == 0)
						find_flag = 2;
				}
				if ((find_flag < 0) && (find_next >= 0) && (file_off + off == last_off)){	// フルサイズのブロックなら
					i = find_next;
					if (crc == s_blk[i].crc){
						data_md5(buf + off, block_size, hash);
						find_flag = -3;	// MD5 計算済みの印
						if (memcmp(hash, s_blk[i].hash, 16) == 0)	// 更に MD5 も一致すれば
							find_flag = 3;
					} else {	// 相対位置が順当な時だけエラー訂正を試みる
						find_flag = correct_error2(buf + off, block_size, crc, s_blk[i].hash, s_blk[i].crc, &err_off, &err_mag);
					}
				}
				if (find_flag < 0){	// 予想したブロックが見つからなかった場合は全てのブロックと比較する
//					j = binary_search(order, block_count, crc, s_blk);
//					j = index_search(crcs, block_count, crc, index, index_shift);
					j = 0x00FFFFFF;	// 見つからなかった
					i = index[crc >> index_shift];	// 配列内のどこにあるか
					// リニア・サーチで残りから探す
					while (i < block_count){
						if (crcs[i] > crc)
							break;	// 上回るなら目的の値は存在しない
						if (crcs[i] == crc){
							j = i;
							break;
						}
						i++;
					}
					// 一致するブロックを優先度別に記録する
					i1 = i2 = i3 = i4 = -1;
					for (; j < block_count; j++){	// 複数ブロックが一致するかもしれないので
						if (crcs[j] != crc)
							break;	// CRC-32 が一致しなくなったら抜ける
						i = order[j];
						if (find_flag != -3){	// CRC-32 が一致したら MD5 を計算する
							data_md5(buf + off, block_size, hash);
							find_flag = -3;	// MD5 計算済みの印
						}
						if (memcmp(hash, s_blk[i].hash, 16) == 0){	// MD5 も一致するか確かめる
							if ((s_blk[i].exist & 0x1000) == 0){	// このファイル内で未発見のブロックを優先的に探す (内容が重複してる場合に順序を保つ為)
								if (s_blk[i].file == num1){	// 指定されたファイル番号のブロック
									i1 = i;
									break;	// 最優先のブロックを見つけた場合は直ちにループから抜ける
								} else {
									i2 = i;
									if (num1 < 0)
										break;
								}
							} else {	// このファイル内で発見済みのブロックも比較する (内容をコピーして追加した改変を認識する)
										// 半端なブロックはスライド検査で見つけられないので、相対位置を参考にするため
								if (s_blk[i].file == num1){
									i3 = i;
								} else {
									i4 = i;
								}
							}
						} else {			// CRC-32 は一致しても MD5 が違うことは、偶然にも発生する
							find_flag = -3;	// そこで、発生間隔の平均でフリーズしそうか判定する
						}
					}
					// 最も優先度の高いブロックとして認識する
					if (i1 >= 0){
						find_flag = 4;
						i = i1;	// 指定されたファイル番号で未発見のブロック
					} else if (i2 >= 0){
						find_flag = 4;
						i = i2;	// 他のファイル番号で未発見のブロック
					} else if (i3 >= 0){
						find_flag = 4;
						i = i3;	// 指定されたファイル番号で発見済みのブロック
					} else if (i4 >= 0){
						find_flag = 4;
						i = i4;	// 他のファイル番号で発見済みのブロック
					}
				}
				// ブロックが見つからなかった場合は、前回検出ブロックの後だけ、小さなファイルと比較する
				// 比較対象が多すぎると遅くなるので、狭い範囲で見つからなければあきらめる
				if ((find_flag < 0) && (tiny_count > 0) && (file_off + off == last_off + tiny_skip)){
					//printf("tiny search at %d\n", off);
					i1 = -1;
					i2 = 0;
					for (num = 0; num < entity_num; num++){	// ファイル番号
						if ((short_use[num] & 3) != 1)
							continue;	// 小さなファイルだけ比較する
						i = files[num].b_off;	// 小さなファイルの先頭ブロックの番号
						if ((s_blk[i].exist & 0x1000) != 0)
							continue;	// 同じファイル内で検出済みなら除外する
						last_size = s_blk[i].size;
						if ((short_use[num] & 4) == 0){	// パディング部分を取り除いた CRC-32 を逆算する
							short_crcs[num] = crc_reverse_zero(s_blk[i].crc, block_size - last_size);
							short_use[num] |= 4;	// CRC-32 計算済みの印
						}
						if (crc_update(0, buf + off, last_size) == short_crcs[num]){	// CRC-32 が一致した
							data_md5_block(buf + off, last_size, hash);
							if (memcmp(hash, s_blk[i].hash, 16) == 0){	// 更に MD5 も一致すれば
								// 複数のスライスと一致した場合は大きい方を採用する
								if (last_size > (unsigned int)i2){
									i1 = i;
									i2 = last_size;
								}
							} else {
								find_flag = -3;
							}
						}
					}
					if (i1 >= 0){	// 小さなファイルが見つかった
						find_flag = 4;
						i = i1;
					} else if (tiny_skip == 0){	// ブロック直後なら
						i1 = 0;
						while (*((unsigned int *)(buf + off + i1)) == 0x04034b50){	// ZIP書庫の local file header
							i2 = *((unsigned int *)(buf + off + i1 + 18));	// compressed size
							i3 = *((unsigned int *)(buf + off + i1 + 22));	// uncompressed size
							if (i2 != i3)
								break;	// 圧縮されてる
							i3 = *((unsigned short *)(buf + off + i1 + 26));	// file name length
							i4 = *((unsigned short *)(buf + off + i1 + 28));	// extra field length
							if (last_off + i1 + 30 + i3 + i4 + i2 > file_size)
								break;	// ファイルが途中まで
							i1 += 30 + i3 + i4;
							if (i2 > 0){	// 空のファイル（サブ・ディレクトリ）は無視する
								tiny_skip = i1;
								//printf("tiny_skip = %d (header size + %d + %d)\n", tiny_skip, i3, i4);
								break;
							}
						}
					} else {
						tiny_skip = 0;
					}
				}
				if (find_flag >= 0){	// その位置でブロックのチェックサムが一致すれば
					overlap_count++;
					num = s_blk[i].file;	// 見つけたブロックが属するファイル番号
					// ファイルが途中から始まっているなら、ブロックが分割されてると仮定する
					// 検査結果の記録を読み取りやすいように、本来の検出ブロックより先に記録する
					if ((sc->flake_count >= 0) && (file_off == 0) && (off > 1)){	// 分割断片は 2バイト以上必要
						i1 = i - 1;	// 一個前のブロックの番号
						if (((i1 == files[num].b_off) && (off + 1 == block_size))	// ブロック・サイズ-1 なら訂正できる
								|| (i1 > files[num].b_off)){	// 先頭ブロックは除外する
							// そのファイルで最初にブロックを見つけた時なので、そのファイル内では未発見
							//printf("slice[%3d] : rear  flake size = %d \n", i1, off);
							find_num++;
							write_ini_verify(i1, 8, off);	// 断片のサイズ
							if (s_blk[i1].exist == 0)	// 未発見なら後半の断片を保管する
								save_flake(buf, i1, off, 0, sc);
						}
					}
					if (s_blk[i].exist == 0){	// 未発見のブロックなら
						if (switch_v & 16){	// コピーする
							if (open_temp_file(temp_path, num, files, sc))
								return -1;
							if (file_write_data(sc->hFile_tmp,
									(__int64)(i - files[num].b_off) * (__int64)block_size, buf + off, s_blk[i].size)){
								printf("file_write_data, %d\n", i);
								return -1;
							}
						}
						s_blk[i].exist = 2;
						first_num++;
					}
					if ((s_blk[i].exist & 0x1000) == 0){	// 同一ファイルで重複していても一回だけ記録する
						find_num++;
						write_ini_verify(i, find_flag, file_off + off);
					}
					if (find_flag == 1)	// 訂正したエラーを戻しておく
						buf[off + err_off] ^= err_mag;
					if (find_flag > 3){	// スライド検査で見つけた場合
						if (file_off + off + 1 < last_off){	// 前回に見つけたブロックと今回のが重なってるなら
							if (last_off - block_size < file_off)
								overlap_count++;
							if ((overlap_count >= FAIL_MAX) && (overlap_count >= (off >> FAIL_LIMIT))){	// 検出数が多すぎる場合だけ
								find_flag = 5;	// 重なってない所から次のブロックを探す
								err_off = (unsigned int)(last_off - file_off);	// 2バイト以上ずれる
								if (err_off > block_size)
									err_off = block_size;	// 最大でもブロック・サイズまで
								//printf("cover [%I64d to %I64d], last [%I64d to %I64d], next off = %d, match = %d\n",
								//		file_off + off, file_off + off + block_size, last_off - block_size, last_off, err_off, overlap_count);
							}
						} else if ((find_last < i) && (find_last >= files[num].b_off)){
							// 前回見つけたブロックが同じファイルに所属していて
							if (file_off + off == last_off + ((__int64)block_size * (__int64)(i - find_last - 1)))
								find_flag = 3;	// 今回見つけたブロックの位置がずれてなければ
						}
					} else if (s_blk[i].exist & 0x1000){
						// 順当な位置で見つけた場合でも、同じファイル内で検出済みだったのなら
						find_flag = 4;	// 破損してる可能性がある
					}
					s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
					last_off = file_off + off + s_blk[i].size;	// 最後に見つけたブロックの終端
					find_last = i;
/*					if (find_flag == 1){
						printf("%9I64d+%8u : C %d, F %d\n", file_off, off, i, s_blk[i].file);	// Corrected
					} else if (find_flag == 2){
						printf("%9I64d+%8u : L %d, F %d\n", file_off, off, i, s_blk[i].file);	// Last
					} else if (find_flag == 3){
						printf("%9I64d+%8u : N %d, F %d\n", file_off, off, i, s_blk[i].file);	// Next
					} else if (find_flag == 5){
						printf("%9I64d+%8u : O %d, F %d\n", file_off, off, i, s_blk[i].file);	// Overlap
					} else if (s_blk[i].size < block_size){
						printf("%9I64d+%8u : T %d, F %d, Size %d\n", file_off, off, i, s_blk[i].file, s_blk[i].size);	// Tiny
					} else {
						printf("%9I64d+%8u : S %d, F %d\n", file_off, off, i, s_blk[i].file);	// Search
					}*/
					// 次に見つかるブロックを予測する
					find_next = i + 1;
					if ((find_next >= source_num) || (s_blk[find_next].file != num)){
						// 最後までいった、またはファイルが異なる
						find_next = -1;
						if ((short_next >= 0) && ((s_blk[short_next].exist & 0x1000) != 0))
							short_next = -1;
						if ((short2_next >= 0) && ((s_blk[short2_next].exist & 0x1000) != 0))
							short2_next = -1;
					} else if (s_blk[find_next].size < block_size){	// 半端なブロックは別に調べる
						if (file_off + off + block_size + s_blk[find_next].size <= file_size){	// ファイル内に収まってる時だけ
							tmp_next = find_next;
							tmp_off = file_off + off + block_size;
							if (find_flag <= 3){	// 順当な位置で見つけた場合
								if ((tmp_next == short_next) && (tmp_off == short_off)){
									// 予測済みのと一致するなら何もしない
								} else if ((short_next >= 0) && (short2_next < 0)){	// 予測と異なるけど、別のが空いてるなら、そっちに記録する
									//printf("short2_off = %I64d, short2_next = %d, file = %d\n", tmp_off, tmp_next, num);
									short2_next = tmp_next;
									short2_off = tmp_off;
								} else {
									if ((short_next >= 0) && (tmp_next == short2_next) && (tmp_off == short2_off)){	// 既に予測済みのと一致するなら入れ替える
										short2_next = short_next;
										short2_off = short_off;
										//printf("exchange short2_off = %I64d, short2_next = %d\n", short2_off, short2_next);
									}
									//printf("short_off = %I64d, short_next = %d, file = %d\n", tmp_off, tmp_next, num);
									short_next = tmp_next;
									short_off = tmp_off;
								}
							} else if ((short_next < 0) &&
									(((__int64)block_size * (__int64)(tmp_next - files[num].b_off) == tmp_off) ||
									(tmp_off + s_blk[tmp_next].size == file_size))){
								// 検出ブロックが順当でなくても、末尾ブロックの開始位置や末端がファイル・サイズに一致すれば
								//printf("short_off = %I64d, short_next = %d, file = %d\n", tmp_off, tmp_next, num);
								short_next = tmp_next;
								short_off = tmp_off;
							} else {
								//printf("short2_off = %I64d, short2_next = %d, file = %d\n", tmp_off, tmp_next, num);
								short2_next = tmp_next;
								short2_off = tmp_off;
							}
						}
						find_next = -1;
					} else {
						tmp_next = files[num].b_off + files[num].b_num - 1;	// 末尾ブロックの番号
						if (s_blk[tmp_next].size < block_size){	// 半端なブロックは別に調べる
							tmp_off = file_off + off + (__int64)(tmp_next - i) * (__int64)block_size;
							if (tmp_off + s_blk[tmp_next].size <= file_size){	// ファイル内に収まってる時だけ
								if (find_flag <= 3){	// 順当な位置で見つけた場合
									if ((tmp_next == short_next) && (tmp_off == short_off)){
										// 予測済みのと一致するなら何もしない
									} else if ((short_next >= 0) && (short2_next < 0)){	// 予測と異なるけど、別のが空いてるなら、そっちに記録する
										//printf("far short2_off = %I64d, short2_next = %d, file = %d\n", tmp_off, tmp_next, num);
										short2_next = tmp_next;
										short2_off = tmp_off;
									} else {
										if ((short_next >= 0) && (tmp_next == short2_next) && (tmp_off == short2_off)){	// 既に予測済みのと一致するなら入れ替える
											short2_next = short_next;
											short2_off = short_off;
											//printf("exchange short2_off = %I64d, short2_next = %d\n", short2_off, short2_next);
										}
										//printf("far short_off = %I64d, short_next = %d, file = %d\n", tmp_off, tmp_next, num);
										short_next = tmp_next;
										short_off = tmp_off;
									}
								} else if ((short_next < 0) &&
										(((__int64)block_size * (__int64)(tmp_next - files[num].b_off) == tmp_off) ||
										(tmp_off + s_blk[tmp_next].size == file_size))){
									// 検出ブロックが順当でなくても、末尾ブロックの開始位置や末端がファイル・サイズに一致すれば
									//printf("far short_off = %I64d, short_next = %d, file = %d\n", tmp_off, tmp_next, num);
									short_next = tmp_next;
									short_off = tmp_off;
								} else if ((short2_next != tmp_next) || (short2_off != tmp_off)){
									//printf("far short2_off = %I64d, short2_next = %d, file = %d\n", tmp_off, tmp_next, num);
									short2_next = tmp_next;
									short2_off = tmp_off;
								}
							}
						}
					}
					tiny_skip = 0;	// 小さなファイルをブロック直後に一回だけ探す
					// ファイルが途中で終わっているなら、ブロックが分割されてると仮定する
					if ((sc->flake_count >= 0) &&
							(file_off + off + s_blk[i].size + block_size - 1 > file_size) &&
							(file_off + off + s_blk[i].size + 1 < file_size)){	// 分割断片は 2～ブロック・サイズ-2 バイト
						i1 = i + 1;	// 一個後ろブロックの番号 (ファイルの末尾ブロックは除く)
						i3 = files[num].b_off + files[num].b_num - 1;	// そのファイルの末尾のブロック番号
						//printf("id = %d, start = %d, max = %d \n", i1, files[num].b_off, i3);
						if ((i1 < i3) && ((s_blk[i1].exist & 0x1000) == 0)){	// ファイル内で未発見なら
							i2 = (int)(file_size - file_off - off - s_blk[i].size);	// 断片のサイズ
							//printf("slice[%3d] : front flake size = %d \n", i1, i2);
							find_num++;
							write_ini_verify(i1, 9, file_off + off + s_blk[i].size);	// 断片の位置
							if (s_blk[i1].exist == 0)	// 未発見なら後半の断片を保管する
								save_flake(buf + (off + s_blk[i].size), i1, i2, 1, sc);
						}
					}
					if (find_flag <= 3){	// 見つけたのが順当なブロックなら
						// 次のブロックは今のブロックの後ろに続くと予想する
						off += s_blk[i].size;
						if (off <= end_off)
							crc = crc_update(0, buf + off, block_size);
						time_slide = GetTickCount();	// スライド検査の開始時刻を更新する
					} else { // ブロック番号が順番通りになってないなら (破損してる個所)
						// ファイル内容に重複があると、後のブロックを先に見つけてしまって、
						// ブロック・サイズ分ずらすと本来のブロックを見つけれなくなるので 1バイトずらす
						prev_crc = crc;
						crc = CRC_SLIDE_CHAR(crc, buf[off + block_size], buf[off]);
						off++;
						if (crc == prev_crc){	// 同じ CRC のブロックを重なった範囲内で発見しないようにする
							data_md5(buf + off, block_size, hash2);
							if (memcmp(hash, hash2, 16) == 0){	// 1バイトずらした後でも MD5 が同じなら、同じデータが連続してる
								//int off2 = off - 1;
								while ((off < end_off) && (file_off + off < last_off) && (crc == prev_crc)){
									crc = CRC_SLIDE_CHAR(crc, buf[off + block_size], buf[off]);
									off++;
								}
							//	printf("Because data is same, skip %d after find.\n", off - off2);
							//} else {
							//	printf("Though CRC is same after find, MD5 is different.\n");
							}
						}
						if ((find_flag == 5) && (off < err_off)){
							off = err_off;	// 前回発見したブロックの終了位置
							if (off <= end_off)
								crc = crc_update(0, buf + off, block_size);
							//printf("off = %d after cover, end_off = %d\n", off, end_off);
						}
					}
					fail_count = 0;	// 誤検出回数を初期化する
					fail_off = file_off + off;	// スライド検査の開始位置
				} else {	// 発見できなかったら、検査位置を 1バイトずらす
					prev_crc = crc;
					crc = CRC_SLIDE_CHAR(crc, buf[off + block_size], buf[off]);
					off++;
					if (crc == prev_crc){	// 1バイトずらしても同じ CRC なら、データが同じ区間は無視する
						if (find_flag != -3)
							data_md5(buf + off - 1, block_size, hash);
						data_md5(buf + off, block_size, hash2);
						if (memcmp(hash, hash2, 16) == 0){	// 1バイトずらした後でも MD5 が同じなら、同じデータが連続してる
							//int off2 = off - 1;
							while ((off < end_off) && (crc == prev_crc)){
								crc = CRC_SLIDE_CHAR(crc, buf[off + block_size], buf[off]);
								off++;
							}
							find_flag = -4;	// CRC と MD5 の両方が同じだった
							//printf("Because data is same, skip %d.\n", off - off2);
						} else {
							find_flag = -3;	// 後で MD5 が異なる回数を数える
							//printf("Though CRC is same, MD5 is different.\n");
						}
					}
					if (find_flag == -3){	// MD5 が異なる回数を数える
						fail_count++;
						//printf("MD5 fail %u, off = %I64d, Ave = %u\n", fail_count, file_off + off, (unsigned int)((file_off + off - fail_off) / (__int64)fail_count));
						if (fail_count == FAIL_MAX){
							// CRC が同じで MD5 が異なることが多すぎ (直近 8回の発生間隔が平均 1KB 以下) なら
							if (file_off + off - fail_off <= (__int64)(FAIL_MAX << FAIL_LIMIT)){
								// 検査が遅くなってる (スライドし始めて 128 ms 以上経過) なら
								if (GetTickCount() - time_slide >= FAIL_TIME){
									if (off <= rear_off){	// 一時的に簡易検査の前方一致・後方一致に切り替える
										if (off < rear_off){
											off = rear_off;
											crc = crc_update(0, buf + off, block_size);
										}
										//printf("skip to rear %d, off = %I64d\n", off, file_off + off);
									} else if (off < end_off){
										off = end_off;
										if (off == block_size)	// 次のループの分を計算しておく
											crc = crc_update(0, buf + off, block_size);
										//printf("skip to block, off = %I64d\n", file_off + off);
									}
								}
							}
							fail_count = 0;	// 回数を数えなおす
							fail_off = file_off + off;
						}
					}
				}
			}

			if (file_next > file_size)
				break;	// バッファーの終端でもうファイルの残りが無いなら終わる
			// ブロックの終端に達したのならバッファー内容をずらす
			//printf("%9I64d, off = %d, next = %d\n", file_off, off, find_next);
			memcpy(buf, buf + block_size, block_size);	// 二個目を先頭に移す
			off -= block_size;
			if (file_next < file_size){	// ファイルの終端に達していなければ、残りを読み込む
/*				if ((file_next + (__int64)block_size) > file_size){
					len = (unsigned int)(file_size - file_next);
					memset(buf + len, 0, block_size - len);
				} else {
					len = block_size;
				}
				if (!ReadFile(hFile, buf + block_size, len, &i, NULL) || (len != (unsigned int)i))
					memset(buf + block_size, 0, len);	// 読み込み時にエラーが発生した部分は 0 にしておく
*/
				WaitForSingleObject(sc->end, INFINITE);	// サブ・スレッドの読み込み終了の合図を待つ
				if (sc->size == 0xFFFFFFFF)
					return find_num;	// 読みこみ時にエラーが発生したら、検査を終了する
				memcpy(buf + block_size, buf + (block_size * 2), len);	// 読み込んでおいた分を移す
				memset(buf + (block_size + len), 0, block_size - len);
				file_next += len;	// ファイル・ポインターの位置
			} else if (file_next == file_size){	// 既に読み込みが終わってるなら
				end_off = (unsigned int)(file_size - file_off - (__int64)block_size);	// 最後のループだけ検査範囲が狭い
				//printf("last loop: off = %u, end_off = %u\n", off, end_off);
				if (off >= end_off)
					break;	// 検査する所が残ってない
				memset(buf + end_off, 0, (block_size * 2) - end_off);
				file_next++;	// 次にループしないようにしておく
			}
			if (off > 0)	// CRCを計算しなおしておく
				crc = crc_update(0, buf + off, block_size);
			file_off += block_size;

			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress_file((int)((file_off * 1000) / file_size), first_num, file_name))
					return -2;
				time_last = GetTickCount();
			}
		}
	}

	// 小さなファイルまたは末尾の半端なブロックの検査
	if ((short_count > 0) && (last_off + (__int64)(sc->min_size) <= file_size)){
		if ((file_off == 0) && (file_size <= (__int64)block_size * 2)){	// 先頭部分は既に読み込んでる
			len = (unsigned int)file_size;
			i2 = 0;
		} else {
			len = block_size * 2 - 1;	// 最大でブロック・サイズ * 2 - 1 バイトを読み込む
			if ((__int64)len > file_size)
				len = (unsigned int)file_size;
			file_off = file_size - len;
			//printf("last_off = %I64d, min size = %d\n", last_off, sc->min_size);
			if (file_off < last_off){
				file_off = last_off;
				len = (unsigned int)(file_size - file_off);
			}
			i2 = -1;	// まだファイルからデータを読み込んでない印
		}
		//printf("last search from %I64d, size = %d\n", file_off, len);
		for (num = 0; num < entity_num; num++){	// ファイル番号
			if (short_use[num] == 0)
				continue;	// 半端なブロックを含まないファイルは除外する
			i = files[num].b_off + files[num].b_num - 1;	// 末尾ブロックの番号
			last_size = s_blk[i].size;
			// 未発見で、検査データより小さな、半端な末尾ブロックなら
			if ((last_size <= len) && ((s_blk[i].exist & 0x1000) == 0)){
				if (i2 < 0){	// 必要になって初めて読み込む
					i2 = 0;
					if (file_read_data(hFile, file_off, buf, len))
						return find_num;	// エラーなら検査を終了する
					//buf[len] = 0;	// ずらす分だけ末尾を 0 で埋めておく
				}
				if ((short_use[num] & 4) == 0){	// パディング部分を取り除いた CRC-32 を逆算する
					short_crcs[num] = crc_reverse_zero(s_blk[i].crc, block_size - last_size);
					short_use[num] |= 4;	// CRC-32 計算済みの印
				}
				off = len - last_size;
				if (crc_update(0, buf + off, last_size) == short_crcs[num]){	// CRC-32 が一致した
					data_md5_block(buf + off, last_size, hash);
					if (memcmp(hash, s_blk[i].hash, 16) == 0){	// 更に MD5 も一致すれば
						// 末尾ブロックがファイルの途中にあれば、先のブロックが分割されてると仮定する
						if ((sc->flake_count >= 0) && (file_off == 0) && (off > 1) && (off < block_size)){
							i1 = i - 1;	// 一個前のブロックの番号
							if (((i1 == files[num].b_off) && (off + 1 == block_size))	// ブロック・サイズ-1 なら訂正できる
									|| (i1 > files[num].b_off)){	// 先頭ブロックは除外する
								// そのファイルで最初にブロックを見つけた時なので、そのファイル内では未発見
								//printf("slice[%3d] : rear  flake size = %d \n", i1, off);
								find_num++;
								write_ini_verify(i1, 8, off);	// 断片のサイズ
								if (s_blk[i1].exist == 0)	// 未発見なら後半の断片を保管する
									save_flake(buf, i1, off, 0, sc);
							}
						}
						if (s_blk[i].exist == 0){	// 未発見のブロックなら
							if (switch_v & 16){	// コピーする
								if (open_temp_file(temp_path, num, files, sc))
									return -1;
								if (file_write_data(sc->hFile_tmp,
										(__int64)(i - files[num].b_off) * (__int64)block_size, buf + off, last_size)){
									printf("file_write_data, %d\n", i);
									return -1;
								}
							}
							s_blk[i].exist = 2;
							first_num++;
						}
						if ((s_blk[i].exist & 0x1000) == 0){	// 同一ファイルで重複していても一回だけ記録する
							find_num++;
							write_ini_verify(i, 0, file_off + off);
						}
						s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
						//printf("%9I64d,%8d : E %d, F %d\n", file_off + off, last_size, i, s_blk[i].file);	// End

						// 経過表示
						if (GetTickCount() - time_last >= UPDATE_TIME){
							if (print_progress_file(1000, first_num, file_name))
								return -2;
							time_last = GetTickCount();
						}
					}	// 他のより大きな末尾ブロックとも一致してるかもしれないので続行する
				}
			}
		}
	}

	return find_num;
}

// 整列順にソース・ブロックのチェックサムを調べる（位相のずれに対処しない）
// -2=キャンセル, -1=エラー, 0=見つからない, 1～=見つかった個数
static int search_block_align(
	int num1,				// file_ctx におけるファイル番号 (未知なら -1)
	unsigned int last_size,	// 末尾ブロックのサイズ
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	__int64 file_size,		// 存在するファイルのサイズ
	__int64 file_off,		// どこから検査を開始するか (ここまでは既に検査済み)
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	unsigned char hash[20];
	int i, b_last, find_num, find_next;
	unsigned int time_last;

	if (num1 < 0)	// ファイル番号が指定されてないと検査できない
		return 0;
	if (file_size < file_off + last_size)
		return 0;	// 小さすぎるファイルは調べない
	find_num = 0;	// このファイル内で何ブロック見つけたか
	b_last = files[num1].b_off + files[num1].b_num - 1;	// 末尾ブロックの番号
	find_next = (int)(file_off / (__int64)block_size);	// 検査開始位置のブロック番号
	find_next += files[num1].b_off;
	time_last = GetTickCount();

	// 前から順にブロック・サイズごとにチェックサムを比較する
	//printf("search from %I64d, file %d, from %d to %d\n", file_off, num1, find_next, b_last);
	// 本来のファイルサイズまでしか調べない
	while ((file_off + (__int64)block_size <= file_size) && (file_off + (__int64)block_size <= files[num1].size)){
		// 次の番号のブロックがその位置にあるかを先に調べる (発見済みでも)
		if (file_md5_crc32_block(hFile, file_off, block_size, hash) != 0)
			return -1;	// ファイルアクセスのエラーを致命的と見なす
		if (memcmp(hash, s_blk[find_next].hash, 20) == 0){	// チェックサムが一致するか確かめる
			i = find_next;
			// その位置でブロックのチェックサムが一致すれば
			if (s_blk[i].exist == 0){	// 未発見のブロックなら
				s_blk[i].exist = 2;
				first_num++;
			}
			if ((s_blk[i].exist & 0x1000) == 0){	// 同一ファイルで重複していても一回だけ記録する
				find_num++;
				write_ini_verify(i, 0, file_off);
			}
			s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
			//printf("%9I64d : N %d\n", file_off, i);	// Next
		}
		find_next++;
		file_off += block_size;
		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress_file((int)((file_off * 1000) / file_size), first_num, file_name))
				return -2;
			time_last = GetTickCount();
		}
	}

	// 末尾の半端なブロックの検査
	if ((last_size < block_size) && (file_size >= files[num1].size)){
		file_off = files[num1].size - last_size;
		if (file_md5_crc32_block(hFile, file_off, last_size, hash) != 0){
			return -1;	// ファイルアクセスのエラーを致命的と見なす
		} else {
			i = b_last;
			if (memcmp(hash, s_blk[i].hash, 20) == 0){	// チェックサムが一致するか確かめる
				if (s_blk[i].exist == 0){	// 未発見のブロックなら
					s_blk[i].exist = 2;
					first_num++;
				}
				find_num++;
				write_ini_verify(i, 0, file_off);
				s_blk[i].exist |= 0x1000;	// このファイル内で見つけた印
				//printf("%9I64d : L %d\n", file_off, i);	// Last
			}
		}
	}

	return find_num;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイルごとに見つけたスライス数を表示する
static int show_file_slice(
	char *ascii_buf,
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, id, find_num, find_total;

	find_total = 0;	// 利用可能なソース・ブロックの数
	find_num = 0;
	id = -1;	// ファイル番号
	for (i = 0; i < source_num; i++){
		if (s_blk[i].exist != 0){
			find_total++;
			if (s_blk[i].exist & 0x1000){	// 今回の検査で発見した印
				if (s_blk[i].file != id){
					if (id >= 0){
						utf16_to_cp(list_buf + files[id].name, ascii_buf, cp_output);
						printf("            = %8d : \"%s\"\n", find_num, ascii_buf);
					}
					id = s_blk[i].file;	// 見つけたスライスが属するファイル番号
					find_num = 0;
				}
				find_num++;
				s_blk[i].exist &= 0x0FFF;	// ファイル内で検出の印を消す
			}
		}
	}
	if (id >= 0){
		utf16_to_cp(list_buf + files[id].name, ascii_buf, cp_output);
		printf("            = %8d : \"%s\"\n", find_num, ascii_buf);
	}

	return find_total;
}

// 破損ファイルの先頭から破損箇所までを作業ファイルにコピーする
static int file_copy_size(
	int num1,			// ファイル番号
	wchar_t *file_name,	// 表示するファイル名
	__int64 file_size,	// 経過を比較するファイル・サイズ
	__int64 copy_size,	// コピーするサイズ
	HANDLE hFile,		// ファイルのハンドル
	HANDLE hFile_tmp)	// 作業ファイルのハンドル
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;
	unsigned int time_last, block_end;
	__int64 copy_end;
	LARGE_INTEGER qwi;	// Quad Word Integer

	// ファイル位置を先頭にする
	qwi.QuadPart = 0;
	if (!SetFilePointerEx(hFile, qwi, NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}
	if (!SetFilePointerEx(hFile_tmp, qwi, NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// コピーする
	time_last = GetTickCount();
	block_end = 0;
	copy_end = 0;
	while (copy_size > 0){
		len = IO_SIZE;
		if (copy_size < IO_SIZE)
			len = (unsigned int)copy_size;
		copy_size -= len;
		block_end += len;
		if (!ReadFile(hFile, buf, len, &rv, NULL)){
			print_win32_err();
			return 1;
		}
		if (len != rv)
			return 1;	// 指定サイズを読み込めなかったらエラーになる
		if (!WriteFile(hFile_tmp, buf, len, &rv, NULL)){
			print_win32_err();
			return 1;
		}

		if (block_end >= block_size){	// ブロック・サイズごとに
			block_end -= block_size;
			copy_end += block_size;
			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress_file((int)((copy_end * 1000) / file_size), -1, file_name))
					return 2;
				time_last = GetTickCount();
			}
		}
	}

	return 0;
}

// ソース・ファイルのスライスを探す (分割ファイルも)
// 0=完了, 1=エラー, 2=キャンセル
int check_file_slice(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	int num,				// file_ctx におけるファイル番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)			// スライス検査用の情報
{
	wchar_t temp_path[MAX_LEN], *file_name;
	int i, find_num, b_last, dir_len, name_off, name_len, num1;
	unsigned int last_size, meta_data[7];
	__int64 file_size;	// 存在するファイルのサイズは本来のサイズとは異なることもある
	HANDLE hFile, hFind;
	WIN32_FIND_DATA FindData;

	file_name = list_buf + files[num].name;
	wcscpy(temp_path, base_dir);	// 基準ディレクトリを入れておく

	// 末尾スライスの大きさ (PAR2 のチェックサムはパディング部分を含む)
	last_size = (unsigned int)(files[num].size % (__int64)block_size);
	if (last_size == 0)
		last_size = block_size;
	b_last = files[num].b_off + files[num].b_num;
	if (files[num].state & 0x80){
		num1 = -1;	// チェックサムが存在しない場合はファイル番号を指定しない
	} else {
		num1 = num;
	}

	if (files[num].state & 0x1A){	// 破損 0x02、追加 0x10、破損して別名 0x28 ならソース・ファイルを検査する
		int comp_num;
		wcscpy(file_path, base_dir);
		wcscpy(file_path + base_len, file_name);
		hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE){	// 存在するはずのソース・ファイルにアクセスできない
			print_win32_err();
			return 1;	// スライス認識や作業ファイルの再設定がめんどうなのでエラーにする
		}
		prog_last = -1;	// 経過表示がまだの印
		find_num = check_ini_verify(file_name, hFile, num1, meta_data, files, s_blk, sc);
		// find_num = ファイル内で検出したスライス数 (他のファイルのスライスも含む)
		memcpy(&file_size, meta_data, 8);
		//printf("ini = %d, file_size = %I64d, first count = %d\n", find_num, file_size, first_num);
		if (file_size + 1 < (__int64)(sc->min_size)){	// 小さすぎるファイルは調べない
			find_num = 0;
		} else if (find_num != -1){	// エラー以外なら
			// 破損ファイルの先頭に完全なスライスが存在するか
			__int64 file_off = 0;
			comp_num = 0;	// 先頭の完全なスライスの数
			if (files[num].state & 2){	// 破損なら上位 24-bit に先頭の完全なスライス数が記録されてる
				comp_num = (unsigned int)(files[num].state) >> 8;
				files[num].state &= 0xFF;	// 上位 24-bit の記録を消しておく
				if (comp_num > 0){	// 破損ファイルの先頭に完全なスライスが見つかってるなら
					for (i = files[num].b_off; i < files[num].b_off + comp_num; i++){
						s_blk[i].exist = 0x1002;	// このファイル内で見つけた印
						file_off += block_size;
					}
					if ((switch_v & (16 | 4)) == 16){	// コピーする
						if (open_temp_file(temp_path, num, files, sc)){
							CloseHandle(hFile);
							return 1;
						}
						if (i = file_copy_size(num1, file_name, file_size, file_off, hFile, sc->hFile_tmp)){
							CloseHandle(hFile);
							return i;
						}
					}
				}
			} else if (files[num].state & 8){	// 破損して別名が見つかっても、破損箇所は検査する
				comp_num = (unsigned int)(files[num].state) >> 8;
				files[num].state &= 0xFF;	// 上位 24-bit の記録を消しておく
				if (comp_num > 0){
					for (i = files[num].b_off; i < files[num].b_off + comp_num; i++){
						s_blk[i].exist = 0x1001;	// このファイル内で見つけた印 (別名ファイル内では完全)
						file_off += block_size;
					}
				}
			} else if (files[num].state & 16){	// 追加なら全てのスライスが完全なはず
				comp_num = files[num].b_num;
				for (i = files[num].b_off; i < b_last; i++)
					s_blk[i].exist = 0x1001;	// このファイル内で見つけた印 (本来のサイズまでは完全)
			}
			if (comp_num == files[num].b_num)
				file_off = files[num].size;	// 最後まで完全
			//printf("first search = %d, file_off = %I64d, state = 0x%08X\n", comp_num, file_off, files[num].state);

			// 検査結果として記録されるのは file_off 以降で見つかったスライスのみ
			if (find_num >= 0){	// 検査結果の記録がある
				find_num += comp_num;
			} else if (find_num == -3){	// 検査結果の記録が無ければ
				//printf("switch_v = %d, file_off = %I64d\n", switch_v, file_off);
				// 破損個所以降を調べる
				if (switch_v & 4){	// 順列検査
					find_num = search_block_align(num1, last_size, file_name,
							hFile, file_size, file_off, files, s_blk);
				} else if (switch_v & 1){	// 簡易検査
					find_num = search_block_simple(temp_path, num1, last_size, file_name,
							hFile, file_size, file_off, 0, files, s_blk, sc);
				} else {	// 詳細検査
					find_num = search_block_slide(temp_path, num1, last_size, file_name,
							hFile, file_size, file_off, files, s_blk, sc);
				}
				if (find_num >= 0){	// 検査結果を記録する
					write_ini_verify2(num1, meta_data, find_num);
					find_num += comp_num;
				} else {	// エラーが発生したら
					write_ini_verify2(num1, meta_data, -1);	// 検査中の記録を破棄する
				}
			}
		}
		CloseHandle(hFile);	// 検査したファイルを閉じる
		if (find_num < 0)	// エラー
			return -find_num;	// 検査時にエラーが発生した
		if (find_num > 0){	// スライスが検出された時
			if (sc->flake_count > 0){	// スライス断片の一致を調べる
				if (check_flake(temp_path, files, s_blk, sc) != 0)
					return 1;
			}
			//printf("find item = %d, first count = %d\n", find_num, first_num);
			print_progress_file(-1, first_num, NULL);	// 重複を除外した利用可能なソース・ブロック数を表示する
		}

		// ソース・ファイルはスライスを検出できなくても検査結果を表示する
		utf16_to_cp(file_name, ascii_buf, cp_output);
		if (files[num].state & 16){	// 追加なら
			printf("%13I64d Appended : \"%s\"\n", file_size, ascii_buf);
		} else {	// 破損、または破損して別名
			printf("%13I64d Damaged  : \"%s\"\n", file_size, ascii_buf);
		}
		// 指定されたファイルに属するスライスを見つけた数
		comp_num = 0;
		for (i = files[num].b_off; i < b_last; i++){
			if (s_blk[i].exist & 0x1000){
				comp_num++;
				s_blk[i].exist &= 0x0FFF;	// ファイル内で検出の印を消す
			}
		}
		printf("            = %8d : \"%s\"\n", comp_num, ascii_buf);
		if (find_num > comp_num)	// 他のファイルに属するスライスを見つけた
			show_file_slice(ascii_buf, files, s_blk);
		fflush(stdout);
		find_num = comp_num;

	} else {	// 消失なら、他でスライスを見つけてないか確かめる
		find_num = 0;
		for (i = files[num].b_off; i < b_last; i++){
			if (s_blk[i].exist != 0)
				find_num++;
		}
	}

	// このファイルのスライスが全て検出済みなら、分割ファイルを探さない
	if (find_num == files[num].b_num)
		return 0;

	// 順列検査では分割ファイルを探さない
	if (switch_v & 4)
		return 0;

	// 分割ファイルや類似名のファイルを探す
	get_base_dir(file_name, file_path);	// 記録されてるファイル名のサブ・ディレクトリ
	dir_len = base_len + (int)wcslen(file_path);	// ディレクトリ部分の長さ
	set_splited_filename(file_path, file_name, dir_len);
	//printf_cp("search  %s\n", file_path);
	hFind = FindFirstFile(file_path, &FindData);
	if ((hFind == INVALID_HANDLE_VALUE) && (files[num].state == 1)){	// 消失でチェックサムがある
		// 元のファイルが消失してるのに、分割ファイルが全く見つからなければ
		if (set_similar_filename(file_path, file_name, dir_len) != 0){	// 文字化けファイルを探す
			//printf_cp("similar %s\n", file_path);
			hFind = FindFirstFile(file_path, &FindData);
		}
	}
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;
	name_off = (int)(offset_file_name(file_name) - list_buf);	// 見つけたファイルとの比較用にファイル名の相対位置
	name_len = (int)wcslen(list_buf + name_off);
	do {
		if (cancel_progress() != 0){	// キャンセル処理
			FindClose(hFind);
			return 2;
		}

		//printf_cp("find = %s\n", FindData.cFileName);
		// フォルダは無視する
		if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;
		// 発見したファイル名が長すぎる場合は無視する
		if (dir_len + wcslen(FindData.cFileName) >= MAX_LEN)
			continue;
		// ファイル・サイズが最小ブロックよりも小さいファイルは無視する
		file_size = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
		if (file_size + 1 < (__int64)(sc->min_size))
			continue;
		// ファイル名に付加されてる部分が妥当でないファイルは無視する
		if (check_extra_part(FindData.cFileName, list_buf + name_off, name_len))
			continue;

		// 破損してないリカバリ・ファイルは無視する
		wcscpy(temp_path + base_len, file_path + base_len);
		wcscpy(temp_path + dir_len, FindData.cFileName);	// 完全ファイル・パスにする
		if (search_file_path(recv_buf, recv_len, temp_path))
			continue;
		// ソース・ファイルや検査済みの分割・類似名ファイルは無視する
		wcscpy(file_path, temp_path + base_len);	// 基準ディレクトリからの相対ファイル・パスにする
		if (search_file_path(list_buf + 1, list_len - 1, file_path))
			continue;
		// 破損したソース・ファイルの作業ファイルは無視する・・・付加部分をチェックしてるので不要
		//printf_cp("verify = %s\n", file_path);

		// 分割先ファイルを検査する
		hFile = CreateFile(temp_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE){	// ファイルを開けたなら内容を検査する
			prog_last = -1;	// 経過表示がまだの印
			find_num = check_ini_verify(FindData.cFileName, hFile, num1, meta_data, files, s_blk, sc);
			//printf("ini = %d, file_size = %I64d\n", find_num, file_size);
			if (find_num == -3){	// 検査結果の記録が無ければ
				if (switch_v & 1){	// 簡易検査
					find_num = search_block_simple(temp_path, num1, last_size, FindData.cFileName,
								hFile, file_size, 0, 1, files, s_blk, sc);
				} else {	// 詳細検査
					find_num = search_block_slide(temp_path, num1, last_size, FindData.cFileName,
								hFile, file_size, 0, files, s_blk, sc);
				}
				// 検査結果を記録する
				write_ini_verify2(num1, meta_data, find_num);
			}
			CloseHandle(hFile);	// 検査したファイルを閉じる
			if (add_file_path(file_path)){	// 検査済みファイルのファイル名を記録する
				printf("add_file_path\n");
				find_num = -1;
			}
			if (find_num < 0){
				FindClose(hFind);
				return -find_num;	// 検査時にエラーが発生した
			}
			if (find_num > 0){	// スライスが検出された時
				if (sc->flake_count > 0){	// スライス断片の一致を調べる
					if (check_flake(temp_path, files, s_blk, sc) != 0){
						FindClose(hFind);
						return 1;
					}
				}
				//printf("find item = %d, first count = %d\n", find_num, first_num);
				print_progress_file(-1, first_num, NULL);	// 重複を除外した利用可能なソース・ブロック数を表示する

				// 検査したファイル名を表示する (分割先のファイル・サイズにする)
				utf16_to_cp(file_path, ascii_buf, cp_output);
				printf("%13I64d Found    : \"%s\"\n", file_size, ascii_buf);
				json_add_found(file_path, 0);
				// ファイルごとに見つけたスライス数を表示する
				show_file_slice(ascii_buf, files, s_blk);
				fflush(stdout);
				if (first_num >= source_num){	// 全てのスライスが見つかったら
					FindClose(hFind);	// その時点で検索を中止する
					return 0;
				}
			} else {
				print_progress_done();	// 経過表示があれば 100% にしておく
			}
		}
	} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
	FindClose(hFind);

	return 0;
}

// 分割ファイルに含まれる他のファイルのスライスを探す
// 0=完了, 1=エラー, 2=キャンセル
int search_file_split(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	int num,				// file_ctx におけるファイル番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)			// スライス検査用の情報
{
	wchar_t temp_path[MAX_LEN], *file_name;
	int find_num, dir_len, name_off, name_len;
	unsigned int meta_data[7];
	__int64 file_size;	// 存在するファイルのサイズは本来のサイズとは異なることもある
	HANDLE hFile, hFind;
	WIN32_FIND_DATA FindData;

	if (files[num].state & 0x03){	// 先に検索済みなら除外する
		int i, b_last;
		b_last = files[num].b_off + files[num].b_num;
		find_num = 0;
		for (i = files[num].b_off; i < b_last; i++){
			if (s_blk[i].exist != 0)
				find_num++;
		}
		if (find_num < files[num].b_num)
			return 0;	// 消失と破損で、このファイルのスライスが不足してる場合は既に検索済み
	}

	file_name = list_buf + files[num].name;
	wcscpy(temp_path, base_dir);	// 基準ディレクトリを入れておく

	// 分割ファイルを探す
	get_base_dir(file_name, file_path);	// 記録されてるファイル名のサブ・ディレクトリ
	dir_len = base_len + (int)wcslen(file_path);	// ディレクトリ部分の長さ
	set_splited_filename(file_path, file_name, dir_len);
	//printf_cp("search  %s\n", file_path);
	hFind = FindFirstFile(file_path, &FindData);
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;
	name_off = (int)(offset_file_name(file_name) - list_buf);	// 見つけたファイルとの比較用にファイル名の相対位置
	name_len = (int)wcslen(list_buf + name_off);
	do {
		if (cancel_progress() != 0){	// キャンセル処理
			FindClose(hFind);
			return 2;
		}

		//printf_cp("find = %s\n", FindData.cFileName);
		// フォルダは無視する
		if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;
		// 発見したファイル名が長すぎる場合は無視する
		if (dir_len + wcslen(FindData.cFileName) >= MAX_LEN)
			continue;
		// ファイル・サイズが最小ブロックよりも小さいファイルは無視する
		file_size = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
		if (file_size < (__int64)(sc->min_size))
			continue;
		// ファイル名に付加されてる部分が妥当でないファイルは無視する
		if (check_extra_part(FindData.cFileName, list_buf + name_off, name_len))
			continue;

		// 破損してないリカバリ・ファイルは無視する
		wcscpy(temp_path + base_len, file_path + base_len);
		wcscpy(temp_path + dir_len, FindData.cFileName);	// 完全ファイル・パスにする
		if (search_file_path(recv_buf, recv_len, temp_path))
			continue;
		// ソース・ファイルや検査済みの分割・類似名ファイルは無視する
		wcscpy(file_path, temp_path + base_len);	// 基準ディレクトリからの相対ファイル・パスにする
		if (search_file_path(list_buf + 1, list_len - 1, file_path))
			continue;
		// 破損したソース・ファイルの作業ファイルは無視する・・・付加部分をチェックしてるので不要
		//printf_cp("verify = %s\n", file_path);

		// 分割先ファイルを検査する
		hFile = CreateFile(temp_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE){	// ファイルを開けたなら内容を検査する
			prog_last = -1;	// 経過表示がまだの印
			find_num = check_ini_verify(FindData.cFileName, hFile, -1, meta_data, files, s_blk, sc);
			//printf("ini = %d, file_size = %I64d\n", find_num, file_size);
			if (find_num == -3){	// 検査結果の記録が無ければ
				if (switch_v & 1){	// 簡易検査
					find_num = search_block_simple(temp_path, -1, 0, FindData.cFileName,
								hFile, file_size, 0, 1, files, s_blk, sc);
				} else {	// 詳細検査
					find_num = search_block_slide(temp_path, -1, 0, FindData.cFileName,
								hFile, file_size, 0, files, s_blk, sc);
				}
				// 検査結果を記録する
				write_ini_verify2(-1, meta_data, find_num);
			}
			CloseHandle(hFile);	// 検査したファイルを閉じる
			if (add_file_path(file_path)){	// 検査済みファイルのファイル名を記録する
				printf("add_file_path\n");
				find_num = -1;
			}
			if (find_num < 0){
				FindClose(hFind);
				return -find_num;	// 検査時にエラーが発生した
			}
			if (find_num > 0){	// スライスが検出された時
				if (sc->flake_count > 0){	// スライス断片の一致を調べる
					if (check_flake(temp_path, files, s_blk, sc) != 0){
						FindClose(hFind);
						return 1;
					}
				}
				print_progress_file(-1, first_num, NULL);	// 重複を除外した利用可能なソース・ブロック数を表示する

				// 検査したファイル名を表示する (分割先のファイル・サイズにする)
				utf16_to_cp(file_path, ascii_buf, cp_output);
				printf("%13I64d Found    : \"%s\"\n", file_size, ascii_buf);
				json_add_found(file_path, 0);
				// ファイルごとに見つけたスライス数を表示する
				show_file_slice(ascii_buf, files, s_blk);
				fflush(stdout);
				if (first_num >= source_num){	// 全てのスライスが見つかったら
					FindClose(hFind);	// その時点で検索を中止する
					return 0;
				}
			} else {
				print_progress_done();	// 経過表示があれば 100% にしておく
			}
		}
	} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
	FindClose(hFind);

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 指定された外部ファイルを検査する
// 0=検査完了, -1=エラー, -2=キャンセル
int check_external_file(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)			// スライス検査用の情報
{
	wchar_t *file_name;
	int list2_off = 0, len, find_num;
	unsigned int meta_data[7];
	__int64 file_size;
	HANDLE hFile;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

/*{
FILE *fp;
fp = fopen("list_buf.txt", "wb");
fwrite(list_buf, 2, list_len, fp);
fclose(fp);
}*/

	wcscpy(file_path, base_dir);	// 基準ディレクトリを入れておく
	while (list2_off < list2_len){
		if (cancel_progress() != 0)	// キャンセル処理
			return 2;

		file_name = list2_buf + list2_off;
		len = (int)wcslen(file_name);
		if (!GetFileAttributesEx(file_name, GetFileExInfoStandard, &AttrData)){
			list2_off += len + 1;
			continue;
		}

		// ファイル・サイズが最小ブロックよりも小さいファイルは無視する
		file_size = ((__int64)(AttrData.nFileSizeHigh) << 32) | (__int64)(AttrData.nFileSizeLow);
		if (file_size < (__int64)(sc->min_size)){
			list2_off += len + 1;
			continue;
		}
		// 破損してないリカバリ・ファイルは無視する
		if (search_file_path(recv_buf, recv_len, file_name)){
			list2_off += len + 1;
			continue;
		}
		if (_wcsnicmp(base_dir, file_name, base_len) == 0){	// 同じディレクトリなら
			// ソース・ファイルや検査済みの分割・類似名ファイルは無視する
			// 破損したソース・ファイルの作業ファイルは無視する
			if ((search_file_path(list_buf + 1, list_len - 1, file_name + base_len)) ||
					(avoid_temp_file(file_name + base_len, files))){
				list2_off += len + 1;
				continue;
			}
		}
		utf16_to_cp(file_name, ascii_buf, cp_output);
		//printf("\n verifying, %s\n", ascii_buf);

		// ファイルを検査してスライスを探す
		hFile = CreateFile(file_name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE){
			prog_last = -1;	// 経過表示がまだの印
			find_num = check_ini_verify(file_name, hFile, -1, meta_data, files, s_blk, sc);
			if (find_num == -3){	// 検査結果の記録が無ければ
				if (switch_v & 1){	// 簡易検査
					find_num = search_block_simple(file_path, -1, 0, file_name,
								hFile, file_size, 0, 0, files, s_blk, sc);
				} else {	// 詳細検査
					find_num = search_block_slide(file_path, -1, 0, file_name,
								hFile, file_size, 0, files, s_blk, sc);
				}
				// 検査結果を記録する
				write_ini_verify2(-1, meta_data, find_num);
			}
			CloseHandle(hFile);	// 検査したファイルを閉じる
			if (find_num < 0){
				return find_num;	// 検査時にエラーが発生した
			} else if (find_num > 0){	// スライスが検出された時
				if (sc->flake_count > 0){	// スライス断片の一致を調べる
					if (check_flake(file_path, files, s_blk, sc) != 0)
						return -1;
				}
				print_progress_file(-1, first_num, NULL);	// 重複を除外した利用可能なソース・ブロック数を表示する

				// 検査したファイル名を表示する
				if (_wcsnicmp(base_dir, file_name, base_len) == 0){	// 基準ディレクトリ以下なら
					utf16_to_cp(file_name + base_len, ascii_buf, cp_output);	// 相対パスにする
					printf("%13I64d Found    : \"%s\"\n", file_size, ascii_buf);
					json_add_found(file_name + base_len, 0);
				} else {	// 基準ディレクトリの外側なら
					path_to_cp(file_name, ascii_buf, cp_output);	// パスも表示する
					printf("%13I64d External : \"%s\"\n", file_size, ascii_buf);
					json_add_found(file_name, 1);
				}
				// ファイルごとに見つけたスライス数を表示する
				show_file_slice(ascii_buf, files, s_blk);
				fflush(stdout);
				if (first_num >= source_num)	// 全てのスライスが見つかれば抜ける
					return 0;
			} else {
				print_progress_done();	// 経過表示があれば 100% にしておく
			}
		}

		list2_off += len + 1;	// 次のファイルへ
	}

	return 0;
}

// 複数の別名ファイルに共通する部分があれば、他のファイルも同じと予想する
static int check_common_misname(
	wchar_t *find_name,		// 検索条件のファイル名が戻る
	file_ctx_r *files)		// 各ソース・ファイルの情報
{
	wchar_t common_part[MAX_LEN], filename[MAX_LEN], *tmp_p;
	int i, len, misname_count, lost_count;

	common_part[0] = 0;
	misname_count = 0;
	lost_count = 0;
	for (i = 0; i < entity_num; i++){
		if ((files[i].state & 0x28) == 0x20){	// 別名なら (破損して別名は含めない)
			wcscpy(filename, list_buf + files[i].name2);
			if (wcschr(filename, '\\') != NULL)	// サブ・ディレクトリを含む場合は判定不能
				return 0;
			_wcslwr(filename);	// 小文字に変換して比較する
			//printf_cp("misname %s\n", filename);
			if (common_part[0] == 0){	// 最初なら拡張子の「.」以降を省いてコピーする
				wcscpy(common_part, filename);
				tmp_p = wcsrchr(common_part + 1, '.');	// 先頭の「.」は無視する
				if (tmp_p != NULL){
					if (wcslen(tmp_p) < EXT_LEN)	// ピリオドを残して拡張子を取り除く
						tmp_p[1] = 0;
				}
			} else {	// 二個目以降は共通部分を比較する
				len = 0;
				while (common_part[len] == filename[len]){
					if (filename[len] == 0)
						break;
					len++;
				}
				if (len < 2)
					return 0;	// 最低でも 2文字は共通部分が必要
				common_part[len] = 0;
			}
			misname_count++;
		} else if (files[i].state == 0x01){	// 消失 (チェックサム無しは無視する)
			lost_count++;
		}
	}

	// 検索には、別名ファイルが二個以上、消失ファイルが一個以上必要
	if ((misname_count < 2) || (lost_count < 1))
		return 0;

	//printf("common = \"%S\", len = %d\n", common_part, len);
	// 数値の途中なら、その手前までにする
	// hoge.part1.txt と hoge.part10.txt の共通部分は「hoge.part1」だが、
	// 「hoge.part1*」で検索しても hoge.part2.txt は見つからない
	while (len > 0){
		i = common_part[len - 1];
		if ((i < '0') || (i > '9')){	// 数値でなければいい
			break;
		} else {	// 共通部分の末尾が数値なら取り除く
			len--;
			common_part[len] = 0;
		}
	}
	if (len < 2)
		return 0;	// 最低でも 2文字は共通部分が必要

	common_part[len    ] = '*';	// 最後に検索文字をつける
	common_part[len + 1] = 0;
	wcscpy(find_name, common_part);
	return 1;
}

// 基準ディレクトリ内を検索して、名前が異なってるソース・ファイルを探す
// 名前の異なるファイルも詳細検査するので、アーカイブ内のスライスを検出できる
// 0=検査完了, -1=エラー, -2=キャンセル
int search_additional_file(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc)			// スライス検査用の情報
{
	int find_num;
	unsigned int meta_data[7];
	__int64 file_size;
	HANDLE hFind, hFile;
	WIN32_FIND_DATA FindData;

	wcscpy(file_path, base_dir);
	if (switch_v & 2){	// 追加検査なら基準ディレクトリ直下のファイルを全て探す
		wcscpy(file_path + base_len, L"*");
	} else {	// 複数の別名ファイルに共通する部分があれば、それを検索条件にして追加検査する
		if (check_common_misname(file_path + base_len, files) == 0)
			return 0;	// 追加検査しない設定で共通部分も無ければ、検索しない
	}

	// 基準ディレクトリ内を検索する
	//printf_cp("\n search path = %s \n", file_path);
	hFind = FindFirstFile(file_path, &FindData);
	if (hFind == INVALID_HANDLE_VALUE)
		return 0;
	do {
		if (cancel_progress() != 0){	// キャンセル処理
			FindClose(hFind);
			return 2;
		}

		// フォルダは無視する
		if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;
		// ファイル・サイズが最小ブロックよりも小さいファイルは無視する
		file_size = ((__int64)(FindData.nFileSizeHigh) << 32) | (__int64)(FindData.nFileSizeLow);
		if (file_size < (__int64)(sc->min_size))
			continue;
		// 破損してないリカバリ・ファイルは無視する
		wcscpy(file_path + base_len, FindData.cFileName);
		if (search_file_path(recv_buf, recv_len, file_path))
			continue;
		// ソース・ファイルや検査済みの分割・類似名ファイルは無視する
		if (search_file_path(list_buf + 1, list_len - 1, FindData.cFileName))
			continue;
		// 破損したソース・ファイルの作業ファイルは無視する
		if (avoid_temp_file(FindData.cFileName, files))
			continue;
		// 検査済みの指定ファイルは無視する
		if (list2_buf){
			if (search_file_path(list2_buf, list2_len, file_path))
				continue;
		}
		utf16_to_cp(FindData.cFileName, ascii_buf, cp_output);
		//printf("\n verifying, %s\n", ascii_buf);

		// ファイルを検査してソース・ブロックを探す
		hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE){
			prog_last = -1;	// 経過表示がまだの印
			find_num = check_ini_verify(FindData.cFileName, hFile, -1, meta_data, files, s_blk, sc);
			if (find_num == -3){	// 検査結果の記録が無ければ
				if (switch_v & 1){	// 簡易検査
					find_num = search_block_simple(file_path, -1, 0, FindData.cFileName,
								hFile, file_size, 0, 0, files, s_blk, sc);
				} else {	// 詳細検査
					find_num = search_block_slide(file_path, -1, 0, FindData.cFileName,
								hFile, file_size, 0, files, s_blk, sc);
				}
				// 検査結果を記録する
				write_ini_verify2(-1, meta_data, find_num);
			}
			CloseHandle(hFile);	// 検査したファイルを閉じる
			if (find_num < 0){
				FindClose(hFind);
				return find_num;	// 検査時にエラーが発生した
			} else if (find_num > 0){	// スライスが検出された時
				if (sc->flake_count > 0){	// スライス断片の一致を調べる
					if (check_flake(file_path, files, s_blk, sc) != 0){
						FindClose(hFind);
						return -1;
					}
				}
				print_progress_file(-1, first_num, NULL);	// 重複を除外した利用可能なソース・ブロック数を表示する

				// 検査したファイル名を表示する
				utf16_to_cp(FindData.cFileName, ascii_buf, cp_output);
				printf("%13I64d Found    : \"%s\"\n", file_size, ascii_buf);
				json_add_found(FindData.cFileName, 0);
				// ファイルごとに見つけたスライス数を表示する
				show_file_slice(ascii_buf, files, s_blk);
				fflush(stdout);
				if (first_num >= source_num){	// 全てのスライスが見つかれば抜ける
					FindClose(hFind);
					return 0;
				}
			} else {
				print_progress_done();	// 経過表示があれば 100% にしておく
			}
		}
	} while (FindNextFile(hFind, &FindData));	// 次のファイルを検索する
	FindClose(hFind);

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// スライスを逆算するか、共通してるスライスを探す
int search_calculable_slice(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	unsigned char hash[20];
	int i, j, k, num, b_last, lost_count, block_count;
	int zero_count, reve_count, dupl_count;
	int *order = NULL;

	block_count = 0;
	lost_count = 0;
	for (num = 0; num < entity_num; num++){
		if ((files[num].state & 0x80) == 0){	// チェックサムがあるファイルだけ比較する
			b_last = files[num].b_off + files[num].b_num;
			for (i = files[num].b_off; i < b_last; i++){
				if (s_blk[i].exist != 0){
					block_count++;	// 比較可能なスライスの数
				} else {
					lost_count++;	// 失われてるスライスの数
				}
			}
		}
	}
	if (lost_count == 0)
		return 0;	// 比較しないで終わる

	if (block_size <= 4){	// ブロック・サイズが 4バイト以下なら全て逆算できる
		reve_count = lost_count - first_num;
		first_num += reve_count;
		for (num = 0; num < entity_num; num++){
			if ((files[num].state & 0x80) == 0){	// チェックサムがあるファイルだけ比較する
				if (files[num].state & 0x0A){	// 破損 0x02、破損して別名 0x28 なら
					files[num].state &= 0xFF;	// 上位 24-bit の記録を消しておく
					// 本来は先頭の完全なスライスの状態は 2 だが、コピーせずに全て復元する
					if (files[num].state & 0x02){	// 破損でも全てのスライスを利用できる
						b_last = files[num].b_off + files[num].b_num;
						for (i = files[num].b_off; i < b_last; i++)
							s_blk[i].exist = 5;	// CRC で内容を復元できる
					}
				}
			}
		}
		printf("\nComparing lost slice\t: %d\n", reve_count);
		printf("Reversible slice count\t: %d\n", reve_count);
		fflush(stdout);
		return reve_count;
	}

	//printf("lost_count = %d, block_count = %d\n", lost_count, block_count);
	if (block_count > 0){
		// CRC-32 の順序を格納するバッファーを確保する
		order = (int *)malloc(block_count * sizeof(int) * 2);
		if (order == NULL)
			return 0;
		block_count = 0;
		for (num = 0; num < entity_num; num++){
			if ((files[num].state & 0x80) == 0){	// チェックサムがあるファイルだけ比較する
				b_last = files[num].b_off + files[num].b_num;
				for (i = files[num].b_off; i < b_last; i++){
					if (s_blk[i].exist != 0){	// 比較可能なスライス
						order[block_count * 2    ] = i;	// 最初はブロック番号にしておく
						order[block_count * 2 + 1] = s_blk[i].crc;
						block_count++;
					}
				}
			}
		}
		// 昇順に並び替える
		qsort(order, block_count, sizeof(int) * 2, sort_cmp_crc);
		// [番号, CRC] の順を並び替えて [番号] だけにする
		for (i = 1; i < block_count; i++)
			order[i] = order[i * 2];
	}

	printf("\nComparing lost slice\t: %d within %d\n", lost_count, block_count);
	fflush(stdout);
	zero_count = 0;
	reve_count = 0;
	dupl_count = 0;
	data_md5_crc32_zero(hash);	// 全て 0のブロックのチェックサムを計算しておく
	for (num = 0; num < entity_num; num++){
		if ((files[num].state & 0x80) == 0){	// チェックサムがあるファイルだけ比較する
			b_last = files[num].b_off + files[num].b_num;
			for (i = files[num].b_off; i < b_last; i++){
				if (s_blk[i].exist == 0){	// 失われてるスライス
					if (memcmp(s_blk[i].hash, hash, 20) == 0){
						s_blk[i].exist = 3;	// 内容が全て 0 のブロック
						zero_count++;
						first_num++;
						//printf(" block[%d] = zero only\n", i);

					} else if (s_blk[i].size <= 4){
						// そのブロック内容が 4バイト以下なら逆算する
						s_blk[i].exist = 5;	// CRC で内容を復元できる
						reve_count++;
						first_num++;
						//printf(" block[%d] = reversible\n", i);

					} else if (block_count > 0){
						// 失われたスライスと同じ内容のスライスを探す
/*						for (j = 0; j < block_count; j++){	// linear search
							k = order[j];
							if (s_blk[i].size == s_blk[k].size){	// サイズが一致すれば
								if (memcmp(s_blk[i].hash, s_blk[k].hash, 20) == 0){
									// ブロックのチェックサムが一致するなら
									s_blk[i].exist = 4;	// 同じ内容のソース・ブロックが存在する
									s_blk[i].file = k;	// そのブロック番号を記録しておく
									dupl_count++;
									first_num++;
									//printf(" lost block[%d] = existent block[%d]\n", i, k);
									break;
								}
							}
						}
*/
						j = binary_search(order, block_count, s_blk[i].crc, s_blk);	// CRC-32 が一致する番号
						for (; j < block_count; j++){	// 複数ブロックが一致するかもしれないので
							k = order[j];
							if (s_blk[i].crc != s_blk[k].crc)
								break;	// CRC-32 が一致しなくなったら抜ける
							if (s_blk[i].size == s_blk[k].size){	// サイズが一致すれば
								if (memcmp(s_blk[i].hash, s_blk[k].hash, 16) == 0){
									// ブロックのチェックサムが一致するなら
									s_blk[i].exist = 4;	// 同じ内容のソース・ブロックが存在する
									s_blk[i].file = k;	// そのブロック番号を記録しておく
									dupl_count++;
									first_num++;
									//printf(" lost block[%d] = existent block[%d]\n", i, k);
									break;
								}
							}
						}
					}
					if (zero_count + reve_count + dupl_count >= lost_count)
						break;	// 十分な数のブロックを見つけたら抜ける
				}
			}
		}
	}
	printf("Null byte slice count\t: %d\n", zero_count);
	printf("Reversible slice count\t: %d\n", reve_count);
	printf("Duplicate slice count\t: %d\n", dupl_count);
	fflush(stdout);

	if (order)
		free(order);
	return zero_count + reve_count + dupl_count;
}

