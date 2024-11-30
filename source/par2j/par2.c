// par2.c
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

#include <malloc.h>
#include <stdio.h>

#include <windows.h>
#include <imagehlp.h>

#include "common2.h"
#include "par2.h"
#include "crc.h"
#include "create.h"
#include "search.h"
#include "list.h"
#include "verify.h"
#include "repair.h"
#include "ini.h"
#include "reedsolomon.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// パリティを作成する
int par2_create(
	wchar_t *uni_buf,	// 作業用、入力されたコメントが入ってる
	int packet_limit,	// リカバリ・ファイルのパケット繰り返しの制限数
	int block_distri,	// パリティ・ブロックの分配方法
	int switch_p)		// インデックス・ファイルを作らない, ユニコードのファイル名も記録する
{
	unsigned char *tmp_p, *common_buf = NULL, *footer_buf;
	int err = 0, i, packet_num, common_size, footer_size;
	HANDLE *rcv_hFile = NULL;
	file_ctx_c *files = NULL;
	source_ctx_c *s_blk = NULL;
	parity_ctx_c *p_blk = NULL;

	init_crc_table();	// CRC 計算用のテーブルを作成する

	// ソース・ファイルの情報
	files = (file_ctx_c *)malloc(sizeof(file_ctx_c) * file_num);
	if (files == NULL){
		printf("malloc, %zd\n", sizeof(file_ctx_c) * file_num);
		err = 1;
		goto error_end;
	}

	// ソース・ファイルの情報を集める
	if (err = get_source_files(files))
		goto error_end;

	// ソース・ブロック番号ごとに、どこから読み込むのかを設定する
	s_blk = (source_ctx_c *)malloc(sizeof(source_ctx_c) * source_num);
	if (s_blk == NULL){
		printf("malloc, %zd\n", sizeof(source_ctx_c) * source_num);
		err = 1;
		goto error_end;
	}
	common_size = 0;
	for (i = 0; i < entity_num; i++){	// recovery set 内の順番でブロックを割り当てる
		footer_size = (int)(files[i].size / (__int64)block_size);
		while (footer_size > 0){	// フルサイズのブロック
			s_blk[common_size].file = i;
			s_blk[common_size].size = block_size;
			common_size++;
			footer_size--;
		}
		footer_size = (int)(files[i].size % (__int64)block_size);
		if (footer_size > 0){	// 半端なブロック
			s_blk[common_size].file = i;
			s_blk[common_size].size = footer_size;
			common_size++;
		}
	}

	// パケットを格納するバッファー
	common_size = 64 + 12 + (file_num * 16);	// Main packet
	common_size += ((64 + 56 + 3) * file_num) + (list_len * 3);	// File Description packet
	common_size += ((64 + 16) * entity_num) + (source_num * 20);	// Input File Slice Checksum packet
	if ((switch_p & 2) != 0)
		common_size += ((64 + 16 + 2) * file_num) + (list_len * 2);	// Unicode Filename packet
	common_size *= 2;	// 2倍確保する
	common_size += 64 + 12;	// Creator packet "par2j v*.*.*"
	if (uni_buf[0] != 0){
		//common_size += 64 + 3 + wcslen(uni_buf);	// ASCII Comment packet
		common_size += 64 + 16 + 2 + (int)(wcslen(uni_buf) * 2);	// Unicode Comment packet
	}
	common_buf = (unsigned char *)malloc(common_size);
	if (common_buf == NULL){
		printf("malloc, %d\n", common_size);
		err = 1;
		goto error_end;
	}
	printf("\n");

	// 1-pass方式が可能かどうかを先に判定する
	if (parity_num == 0){	// パリティ・ブロックを作らない場合
		err = -10;
	} else if (source_num <= 1){	// ソース・ブロックが一個だけなら
		err = -11;
	} else if (memory_use & 16){	// SSDなら1-pass方式を使わない
		err = -12;
	} else {
		// メモリーを確保できるか試す
		err = read_block_num(parity_num, 0, 256);
		if (err == 0)
			err = -13;
	}
#ifdef TIMER
	printf("read_block_num = %d\n", read_block_num(parity_num, 0, 256));
#endif
	if (err > 0){	// 1-pass方式が可能
#ifdef TIMER
		printf("1-pass processing is possible, %d\n", err);
#endif
		err = 0;

		// ファイルのハッシュ値とブロックのチェックサムを計算せずに、共通パケットを作成する
		common_size = set_common_packet_1pass(common_buf, &packet_num, (switch_p & 2) >> 1, files);
		if (common_size <= 2){
			err = common_size;
			goto error_end;
		}
		// 末尾パケットを作成する
		footer_buf = common_buf + (common_size * 2);	// 共通パケットの後
		footer_size = set_footer_packet(footer_buf, uni_buf, common_buf + 32);
		//printf("packet_num = %d, common_size = %d, footer_size = %d\n", packet_num, common_size, footer_size);
		// 大きいめに確保しておいて、サイズ確定後に縮小する
		tmp_p = (unsigned char *)realloc(common_buf, common_size * 2 + footer_size);
		if (tmp_p == NULL){
			printf("realloc, %d\n", common_size * 2 + footer_size);
			err = 1;
			goto error_end;
		} else {
			common_buf = tmp_p;
			footer_buf = tmp_p + (common_size * 2);	// 共通パケットの後
		}
		// 同じ Set ID の記録があれば消去しておく
		if (recent_data != 0)
			reset_ini_file(common_buf + 32);

		// 書庫ファイルに連結するためには、ファイル・ハンドルが必要となる
		if (split_size == 1){
			rcv_hFile = (HANDLE *)calloc(recovery_num, sizeof(HANDLE));
			if (rcv_hFile == NULL){
				printf("calloc, %zd\n", sizeof(HANDLE) * recovery_num);
				err = 1;
				goto error_end;
			}
		}

		// パリティ・ブロックを作成する
		uni_buf[MAX_LEN - 1] = switch_p & 1;	// インデックス・ファイルを作るかどうか
		err = rs_encode_1pass(ini_path, uni_buf, packet_limit, block_distri, packet_num,
					common_buf, common_size, footer_buf, footer_size, rcv_hFile, files, s_blk);
	}
	// 2-pass方式で続行する
	if (err < 0){
#ifdef TIMER
		printf("2-pass processing is selected, %d\n", err);
#endif
		if (err > -10){	// 作成済みのパケットは作り直さない
			// ハッシュ値だけ計算する
			if (err = set_common_packet_hash(common_buf, files))
				goto error_end;
			memcpy(common_buf + common_size, common_buf, common_size);	// 後の半分に前半のをコピーする
			//printf("packet_num = %d, common_size = %d, footer_size = %d\n", packet_num, common_size, footer_size);
			if (rcv_hFile){
				free(rcv_hFile);
				rcv_hFile = NULL;
			}
		} else {
			// 共通パケットを作成する
			if ((memory_use & 16) && (cpu_num >= 3) && (entity_num >= 2)){	// SSDなら複数ファイルを同時に処理する
				common_size = set_common_packet_multi(common_buf, &packet_num, (switch_p & 2) >> 1, files);
			} else {
				common_size = set_common_packet(common_buf, &packet_num, (switch_p & 2) >> 1, files);
			}
			if (common_size <= 2){
				err = common_size;
				goto error_end;
			}
			memcpy(common_buf + common_size, common_buf, common_size);	// 後の半分に前半のをコピーする
			// 末尾パケットを作成する
			footer_buf = common_buf + (common_size * 2);	// 共通パケットの後
			footer_size = set_footer_packet(footer_buf, uni_buf, common_buf + 32);
			//printf("packet_num = %d, common_size = %d, footer_size = %d\n", packet_num, common_size, footer_size);
			// 大きいめに確保しておいて、サイズ確定後に縮小する
			tmp_p = (unsigned char *)realloc(common_buf, common_size * 2 + footer_size);
			if (tmp_p == NULL){
				printf("realloc, %d\n", common_size * 2 + footer_size);
				err = 1;
				goto error_end;
			} else {
				common_buf = tmp_p;
				footer_buf = tmp_p + (common_size * 2);	// 共通パケットの後
			}
			// 同じ Set ID の記録があれば消去しておく
			if (recent_data != 0)
				reset_ini_file(common_buf + 32);
		}
		err = 0;

		// 既に完全なインデックス・ファイルが存在するなら作成しない
/*		if (GetFileAttributes(recovery_file) != INVALID_FILE_ATTRIBUTES){
			hFile = CreateFile(recovery_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (hFile != INVALID_HANDLE_VALUE){
				if (GetFileSize(hFile, NULL) == common_size + footer_size){
					unsigned int crc, crc2;
					// 既存のインデックス・ファイルの CRC-32 を求める
					crc = file_crc_part(hFile);
					crc2 = crc_update(0xFFFFFFFF, common_buf, common_size);
					crc2 = crc_update(crc2, footer_buf, footer_size) ^ 0xFFFFFFFF;
					//printf("CRC = %08X, %08X\n", crc, crc2);
					if (crc == crc2){
						switch_p |= 1;
						printf("index file already exists\n");
					}
				}
				CloseHandle(hFile);
			}
		}*/

		if ((switch_p & 1) == 0){	// インデックス・ファイルを作るときだけ
			HANDLE hFile;
			print_progress_text(0, "Making index file");
			// パリティ・ブロックを含まないリカバリ・ファイルを書き込む
			//move_away_file(recovery_file);	// 既存のファイルをどかす
			hFile = CreateFile(recovery_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile == INVALID_HANDLE_VALUE){
				if (GetLastError() == ERROR_PATH_NOT_FOUND){	// Path not found (3)
					make_dir(recovery_file);	// 途中のフォルダが存在しないのなら作成する
					hFile = CreateFile(recovery_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				}
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					printf_cp("cannot create file, %s\n", recovery_file);
					err = 1;
					goto error_end;
				}
			}
			if (!WriteFile(hFile, common_buf, common_size, &i, NULL)){
				print_win32_err();
				CloseHandle(hFile);
				err = 1;
				goto error_end;
			}
			if (!WriteFile(hFile, footer_buf, footer_size, &i, NULL)){
				print_win32_err();
				CloseHandle(hFile);
				err = 1;
				goto error_end;
			}
			CloseHandle(hFile);
			print_progress_done();
		}
		if (parity_num == 0)
			goto creation_end;	// パリティ・ブロックを作らない場合は一気に分割へ跳ぶ

		// パリティ・ブロックの領域を確保する
		p_blk = (parity_ctx_c *)malloc(sizeof(parity_ctx_c) * parity_num);
		if (p_blk == NULL){
			printf("malloc, %zd\n", sizeof(parity_ctx_c) * parity_num);
			err = 1;
			goto error_end;
		}
		rcv_hFile = (HANDLE *)calloc(recovery_num, sizeof(HANDLE));
		if (rcv_hFile == NULL){
			printf("calloc, %zd\n", sizeof(HANDLE) * recovery_num);
			err = 1;
			goto error_end;
		}

		// リカバリ・ファイルを作成して共通パケットをコピーする
		err = create_recovery_file(uni_buf, packet_limit, block_distri,
				packet_num, common_buf, common_size, footer_buf, footer_size, rcv_hFile, p_blk);
		if (err){
			delete_recovery_file(uni_buf, block_distri, switch_p & 1, rcv_hFile);
			goto error_end;
		}
		// Recovery Slice packet 用のパケット・ヘッダーを作成しておく
		set_packet_header((unsigned char *)uni_buf, common_buf + 32, 4, block_size + 4);
		free(common_buf);
		common_buf = NULL;

		// パリティ・ブロックを作成する
		print_progress_text(0, "Creating recovery slice");
		err = rs_encode(ini_path, (unsigned char *)uni_buf, rcv_hFile, files, s_blk, p_blk);
	}
	if (err){
		delete_recovery_file(uni_buf, block_distri, switch_p & 1, rcv_hFile);
		goto error_end;
	}

creation_end:
	// ソース・ファイルを分割する
	if (split_size >= 4){
		if (err = split_files(files, &common_size, &footer_size)){
			delete_split_files(files, common_size, footer_size);
			goto error_end;
		}
	// ソース・ファイルの書庫にリカバリ・レコードを追加する
	} else if (split_size == 1){
		if (err = append_recovery_file(list_buf + files[0].name, rcv_hFile, switch_p & 1))
			goto error_end;
	}

	printf("\nCreated successfully\n");
error_end:
	if (common_buf)
		free(common_buf);
	if (s_blk)
		free(s_blk);
	if (p_blk)
		free(p_blk);
	if (rcv_hFile){
		for (i = 0; i < recovery_num; i++){
			if (rcv_hFile[i])
				CloseHandle(rcv_hFile[i]);
		}
		free(rcv_hFile);
	}
	if (files)
		free(files);
	return err;
}

// リカバリ・ファイルの構成を試算する
int par2_trial(
	wchar_t *uni_buf,	// 作業用、入力されたコメントが入ってる
	int packet_limit,	// リカバリ・ファイルのパケット繰り返しの制限数
	int block_distri,	// パリティ・ブロックの分配方法
	int switch_p)		// インデックス・ファイルを作らない, ユニコードのファイル名も記録する
{
	int packet_num, common_size, footer_size;
	__int64 total_data_size;

	// ソース・ファイルの情報から共通パケットのサイズを計算する
	common_size = measure_common_packet(&packet_num, (switch_p & 2) >> 1);
	if (common_size <= 2)
		return common_size;

	// 末尾パケットのサイズを計算する
	footer_size = measure_footer_packet(uni_buf);

	// リカバリ・ファイルのサイズを計算する
	printf("\nPAR File count\t: %d\n", recovery_num + 1 - (switch_p & 1));
	printf("         Size  Packet  Slice :  Filename\n");
	fflush(stdout);
	total_data_size = total_file_size;
	measure_recovery_file(uni_buf, packet_limit, block_distri, packet_num, common_size, footer_size, switch_p & 1);
	printf("\nPAR File total size\t: %I64d\n\n", total_file_size);

	// 効率を計算する
	if ((source_num == 0) || (total_file_size == 0)){
		packet_num = 0;
		footer_size = 0;
		common_size = 0;
	} else {
		double rate1, rate2;
		rate1 = (double)total_data_size / ((double)block_size * (double)source_num);
		rate2 = (double)block_size * (double)parity_num / (double)total_file_size;
		// 整数型に変換して、小数点以下第2位を切り捨てる (doubleのままだと四捨五入される)
		packet_num = (int)(rate1 * 1000);
		footer_size = (int)(rate2 * 1000);
		common_size = (int)(rate1 * rate2 * 1000);
	}
	printf("File data in Blocks\t: %d.%d%%\n", packet_num / 10, packet_num % 10);
	printf("Blocks in PAR files\t: %d.%d%%\n", footer_size / 10, footer_size % 10);
	printf("Efficiency rate\t\t: %d.%d%%\n", common_size / 10, common_size % 10);

	printf("\nTrial end\n");
	return 0;
}

// ソース・ファイルの破損や欠損を調べる
int par2_verify(
	wchar_t *uni_buf)	// 作業用
{
	char ascii_buf[MAX_LEN * 3];
	unsigned char set_id[16], *work_buf = NULL;
	int err = 0, i, j, need_repair, parity_now, recovery_lost;
	file_ctx_r *files = NULL;
	source_ctx_r *s_blk = NULL;
	parity_ctx_r *p_blk = NULL;

	if (switch_v & 4)	// 順列検査なら追加検査を無視して簡易検査にする
		switch_v = (switch_v & ~2) | 1;
	init_crc_table();	// CRC 計算用のテーブルを作成する

	// リカバリ・ファイルを検索する
	if (search_recovery_files() != 0){
		err = 1;
		goto error_end;
	}

	// Main packet を探す (パケット・サイズは SEARCH_SIZE * 2 まで)
	work_buf = (unsigned char *)malloc(SEARCH_SIZE * 3);
	if (work_buf == NULL){
		printf("malloc, %d\n", SEARCH_SIZE * 3);
		err = 1;
		goto error_end;
	}
	if (err = search_main_packet(work_buf, set_id)){
		printf("valid file is not found\n");
		goto error_end;
	}

	// ソース・ファイルの情報
	files = (file_ctx_r *)malloc(sizeof(file_ctx_r) * file_num);
	if (files == NULL){
		printf("malloc, %zd\n", sizeof(file_ctx_r) * file_num);
		err = 1;
		goto error_end;
	}
	// Main packet から File ID を読み取る
	for (i = 0; i < file_num; i++){
		memcpy(files[i].id, work_buf + (16 * i), 16);
		files[i].name = -1;
	}
	onepass_window_gen(block_size);	// ブロック単位でずらして検査するための CRC テーブルを作る

	i = check_ini_file(set_id);	// 検査するかどうか
	if (i != 0){	// 検査済みなら記録を読み込む
		if (read_ini_file(uni_buf, files))
			i = 0;	// 記録を読み込めなかった
	}
	if (i == 0){	// 検査する
		// ファイル情報のパケットを探す
		if (err = search_file_packet(ascii_buf, work_buf, uni_buf, set_id, 1, files))
			goto error_end;
		write_ini_file(files);	// ファイル情報を記録しておく
	}
	if (err = set_file_data(ascii_buf, files))	// ソース・ファイル情報を確認して集計する
		goto error_end;

	// ソース・ブロックの情報
	s_blk = (source_ctx_r *)malloc(sizeof(source_ctx_r) * source_num);
	if (s_blk == NULL){
		printf("malloc, %zd\n", sizeof(source_ctx_r) * source_num);
		err = 1;
		goto error_end;
	}
	for (i = 0; i < entity_num; i++){
		// ファイルごとにソース・ブロックの情報を設定する
		j = files[i].b_off;	// そのファイル内のブロックの開始番号
		parity_now = (int)(files[i].size / (__int64)block_size);
		while (parity_now > 0){	// フルサイズのブロック
			s_blk[j].file = i;
			s_blk[j].size = block_size;
			s_blk[j].exist = 0;
			j++;
			parity_now--;
		}
		parity_now = (int)(files[i].size % (__int64)block_size);
		if (parity_now > 0){	// 半端なブロック
			s_blk[j].file = i;
			s_blk[j].size = parity_now;
			s_blk[j].exist = 0;
			j++;
		}
	}
	if (parity_num > 0){
		p_blk = (parity_ctx_r *)malloc(sizeof(parity_ctx_r) * parity_num);
		if (p_blk == NULL){
			printf("malloc, %zd\n", sizeof(parity_ctx_r) * parity_num);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++)
			p_blk[i].exist = 0;
	}

	// 修復用のパケットを探す
	recovery_lost = search_recovery_packet(ascii_buf, work_buf, uni_buf, set_id, NULL, files, s_blk, p_blk);
	if (recovery_lost < 0){
		err = -recovery_lost;
		goto error_end;
	}
	free(work_buf);
	work_buf = NULL;
	// チェックサムが揃ってるかを確かめる
	for (i = 0; i < entity_num; i++){
		if (files[i].state & 0x80){
			//printf_cp("missing checksum, %s\n", list_buf + files[i].name);
			break;
		}
	}
	if (i == entity_num){
		write_ini_checksum(files, s_blk);	// チェックサムを記録しておく
	} else {
		if (read_ini_checksum(files, s_blk)){	// チェックサムが記録されてるなら読み込む
			update_ini_checksum(files, s_blk);	// 存在するチェックサムだけ読み書きする
			printf("\nInput File Slice Checksum packet is missing\n");
		}
	}

	parity_now = first_num;	// 利用可能なパリティ・ブロックの数
	j = 0;
	for (i = 0; i < parity_num; i++){
		if (p_blk[i].exist != 0)
			j = i + 1;	// 利用できる最大値
	}
	if ((j > 0) && (j < parity_num)){	// ブロックが少ないなら
		// 使った分までに縮小する
		parity_ctx_r *tmp_p_blk;
		tmp_p_blk = (parity_ctx_r *)realloc(p_blk, sizeof(parity_ctx_r) * j);
		if (tmp_p_blk != NULL)
			p_blk = tmp_p_blk;
		parity_num = j;	// 本来のパリティ・ブロック数として扱う
	}
	if (parity_now == 0){	// パリティが無ければ
		parity_num = 0;
		free(p_blk);
		p_blk = NULL;
	}
	printf("\nRecovery Slice count\t: %d\n", parity_num);
	printf("Recovery Slice found\t: %d\n", parity_now);

	// ソース・ファイルが完全かどうかを調べる
	// ファイルの状態は 完全、消失、追加、破損(完全なブロックの数) の4種類
	if ((memory_use & 16) && (cpu_num >= 3) && (entity_num >= 2)){	// SSDなら複数ファイルを同時に処理する
		err = check_file_complete_multi(ascii_buf, uni_buf, files, s_blk);
	} else {
		err = check_file_complete(ascii_buf, uni_buf, files, s_blk);
	}
	if (err)
		goto error_end;

	// ソース・ファイルが不完全なら別名・移動ファイルを探す
	if (err = search_misnamed_file(ascii_buf, uni_buf, files, s_blk))
		goto error_end;

	// 破損・分割・類似名のファイルから使えるスライスを探す
	if (err = search_file_slice(ascii_buf, uni_buf, files, s_blk))
		goto error_end;

	// 検査が終わったらメモリーを解放する
	free(recv_buf);
	recv_buf = NULL;
	if (switch_b & 16){
		recovery_lost *= -1;	// 表示しないようにマイナスにする
	} else if (recv2_buf){
		free(recv2_buf);
		recv2_buf = NULL;
	}
	if (list2_buf){
		free(list2_buf);
		list2_buf = NULL;
	}

	// ソース・ブロックを比較して、利用可能なブロックを増やす
	if (first_num < source_num)
		search_calculable_slice(files, s_blk);

	// 検査結果を集計する
	err = result_file_state(ascii_buf, &need_repair, parity_now, recovery_lost, files, s_blk);

	// ソースファイルが完全ならリカバリファイルを削除する
	if ((need_repair == 0) && ((switch_b & 16) != 0))
		purge_recovery_file();

error_end:
	close_ini_file();
	if (recv_buf)
		free(recv_buf);
	if (recv2_buf)
		free(recv2_buf);
	if (s_blk)
		free(s_blk);
	if (p_blk)
		free(p_blk);
	if (files)
		free(files);
	return err;
}

// ソース・ファイルの破損や欠損を修復する
int par2_repair(
	wchar_t *uni_buf)	// 作業用
{
	char ascii_buf[MAX_LEN * 3];
	unsigned char set_id[16], *work_buf = NULL;
	int err = 0, i, j, need_repair, recovery_lost;
	int parity_now, lost_num, block_count;
	HANDLE *rcv_hFile = NULL;
	file_ctx_r *files = NULL;
	source_ctx_r *s_blk = NULL;
	parity_ctx_r *p_blk = NULL;

	switch_v |= 16;		// 検査後に保存する
	if (switch_v & 4){	// 順列検査なら追加検査を無視して簡易検査にする
		switch_v = (switch_v & ~2) | 1;
		switch_b = 0;	// バックアップ設定を無効にする
	}
	init_crc_table();	// CRC 計算用のテーブルを作成する

	// リカバリ・ファイルを検索する
	if (search_recovery_files() != 0){
		err = 1;
		goto error_end;
	}

	// Main packet を探す (パケット・サイズは SEARCH_SIZE * 2 まで)
	work_buf = (unsigned char *)malloc(SEARCH_SIZE * 3);
	if (work_buf == NULL){
		printf("malloc, %d\n", SEARCH_SIZE * 3);
		err = 1;
		goto error_end;
	}
	if (err = search_main_packet(work_buf, set_id)){
		printf("valid file is not found\n");
		goto error_end;
	}

	// ソース・ファイルの情報
	files = (file_ctx_r *)malloc(sizeof(file_ctx_r) * file_num);
	if (files == NULL){
		printf("malloc, %zd\n", sizeof(file_ctx_r) * file_num);
		err = 1;
		goto error_end;
	}
	// Main packet から File ID を読み取る
	for (i = 0; i < file_num; i++){
		memcpy(files[i].id, work_buf + (16 * i), 16);
		files[i].name = -1;
	}
	onepass_window_gen(block_size);	// ブロック単位でずらして検査するための CRC テーブルを作る

	i = check_ini_file(set_id);	// 検査するかどうか
	if (i != 0){	// 検査済みなら記録を読み込む
		if (read_ini_file(uni_buf, files))
			i = 0;	// 記録を読み込めなかった
	}
	if (i == 0){	// 検査する
		// ファイル情報のパケットを探す
		if (err = search_file_packet(ascii_buf, work_buf, uni_buf, set_id, 1, files))
			goto error_end;
		write_ini_file(files);	// ファイル情報を記録しておく
	}
	if (err = set_file_data(ascii_buf, files))	// ソース・ファイル情報を確認して集計する
		goto error_end;

	// リカバリ・ファイルのハンドル
	rcv_hFile = (HANDLE *)calloc(recovery_num, sizeof(HANDLE));
	if (rcv_hFile == NULL){
		printf("calloc, %zd\n", sizeof(HANDLE) * recovery_num);
		err = 1;
		goto error_end;
	}
	// ソース・ブロックの情報
	s_blk = (source_ctx_r *)malloc(sizeof(source_ctx_r) * source_num);
	if (s_blk == NULL){
		printf("malloc, %zd\n", sizeof(source_ctx_r) * source_num);
		err = 1;
		goto error_end;
	}
	for (i = 0; i < entity_num; i++){
		// ファイルごとにソース・ブロックの情報を設定する
		j = files[i].b_off;	// そのファイル内のブロックの開始番号
		parity_now = (int)(files[i].size / (__int64)block_size);
		while (parity_now > 0){	// フルサイズのブロック
			s_blk[j].file = i;
			s_blk[j].size = block_size;
			s_blk[j].exist = 0;
			j++;
			parity_now--;
		}
		parity_now = (int)(files[i].size % (__int64)block_size);
		if (parity_now > 0){	// 半端なブロック
			s_blk[j].file = i;
			s_blk[j].size = parity_now;
			s_blk[j].exist = 0;
			j++;
		}
	}
	if (parity_num > 0){
		p_blk = (parity_ctx_r *)malloc(sizeof(parity_ctx_r) * parity_num);
		if (p_blk == NULL){
			printf("malloc, %zd\n", sizeof(parity_ctx_r) * parity_num);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++)
			p_blk[i].exist = 0;
	}

	// 修復用のパケットを探す
	recovery_lost = search_recovery_packet(ascii_buf, work_buf, uni_buf, set_id, rcv_hFile, files, s_blk, p_blk);
	if (recovery_lost < 0){
		err = -recovery_lost;
		goto error_end;
	}
	free(work_buf);
	work_buf = NULL;
	// チェックサムが揃ってるかを確かめる
	for (i = 0; i < entity_num; i++){
		if (files[i].state & 0x80)
			break;
	}
	if (i == entity_num){
		write_ini_checksum(files, s_blk);	// チェックサムを記録しておく
	} else {
		if (read_ini_checksum(files, s_blk)){	// チェックサムが記録されてるなら読み込む
			update_ini_checksum(files, s_blk);	// 存在するチェックサムだけ読み書きする
			printf("\nInput File Slice Checksum packet is missing\n");
		}
	}

	parity_now = first_num;	// 利用可能なパリティ・ブロックの数
	j = 0;
	for (i = 0; i < parity_num; i++){
		if (p_blk[i].exist != 0)
			j = i + 1;	// 利用できる最大値
	}
	if ((j > 0) && (j < parity_num)){	// ブロックが少ないなら
		// 使った分までに縮小する
		parity_ctx_r *tmp_p_blk;
		tmp_p_blk = (parity_ctx_r *)realloc(p_blk, sizeof(parity_ctx_r) * j);
		if (tmp_p_blk != NULL)
			p_blk = tmp_p_blk;
		parity_num = j;	// 本来のパリティ・ブロック数として扱う
	}
	if (parity_now == 0){	// パリティが無ければ
		parity_num = 0;
		free(p_blk);
		p_blk = NULL;
	}
	printf("\nRecovery Slice count\t: %d\n", parity_num);
	printf("Recovery Slice found\t: %d\n", parity_now);

	// ソース・ファイルが完全かどうかを一覧表示する
	// ファイルの状態は 完全、消失、追加、破損(完全なブロックの数) の4種類
	if ((memory_use & 16) && (cpu_num >= 3) && (entity_num >= 2)){	// SSDなら複数ファイルを同時に処理する
		err = check_file_complete_multi(ascii_buf, uni_buf, files, s_blk);
	} else {
		err = check_file_complete(ascii_buf, uni_buf, files, s_blk);
	}
	if (err)
		goto error_end;

	// ソース・ファイルが不完全なら別名・移動ファイルを探す
	if (err = search_misnamed_file(ascii_buf, uni_buf, files, s_blk))
		goto error_end;

	// 消失・破損ファイルがあるなら、その作業ファイルを作成する
	wcscpy(uni_buf, base_dir);
	for (i = 0; i < entity_num; i++){
		if (files[i].size == 0)
			continue;
		if (((files[i].state & 3) != 0) && ((files[i].state & 4) == 0)){ 
			// 作業用のソース・ファイルを作る
			get_temp_name(list_buf + files[i].name, uni_buf + base_len);
			if (create_temp_file(uni_buf, files[i].size)){
				printf_cp("cannot create file, %s\n", uni_buf);
				err = 1;
				goto error_end;
			}
		}
	}

	// 破損・分割・類似名のファイルから使えるスライスを探す
	if (err = search_file_slice(ascii_buf, uni_buf, files, s_blk))
		goto error_end;

	// 検査が終わったらメモリーを解放する
	free(recv_buf);
	recv_buf = NULL;
	if (switch_b & 16){
		recovery_lost *= -1;	// 表示しないようにマイナスにする
	} else if (recv2_buf){
		free(recv2_buf);
		recv2_buf = NULL;
	}
	if (list2_buf){
		free(list2_buf);
		list2_buf = NULL;
	}

	// ソース・ブロックを比較して、利用可能なブロックを増やす
	block_count = 0;	// 逆算可能なブロック数
	if (first_num < source_num)
		block_count = search_calculable_slice(files, s_blk);

	// 検査結果を集計する
	err = result_file_state(ascii_buf, &need_repair, parity_now, recovery_lost, files, s_blk);
	lost_num = source_num - first_num;
	if (need_repair == 0){
		if (switch_b & 16){
			if (rcv_hFile){	// 削除前にリカバリ・ファイルを閉じる
				for (i = 0; i < recovery_num; i++){
					if (rcv_hFile[i])
						CloseHandle(rcv_hFile[i]);
				}
				free(rcv_hFile);
				rcv_hFile = NULL;
			}
			purge_recovery_file();	// ソースファイルが完全ならリカバリファイルを削除する
		}
		goto error_end;	// 全て完全なので修復する必要なし
	} else if ((lost_num > parity_now) && ((need_repair & 0x2FFFFFFF) == 0)){
		if (switch_b & 4)
			replace_incomplete(uni_buf, ascii_buf, files, s_blk);	// 再構築したファイルで置き換える
		goto error_end;	// ブロック不足で、簡易修復や再構築ができるファイルも無い
	} else if (need_repair == 0x40000000){
		goto error_end;	// non-recovery set のファイルだけが消失・破損してる
	}
	if (need_repair & 0x20000000)	// 消失・破損したファイルのスライスが全て利用可能な場合
		block_count |= 0x20000000;	// 修復後の確認検査が必要な印

/*
	// 修復するかどうかを入力してもらう
	printf(" continue ? [Yes/No] : ");
	i = _getche();
	printf("\n");
	if ((i != 'y') && (i != 'Y'))
		goto error_end;
*/

	// パリティ・ブロックの数が修復に必要な量よりも多すぎるなら最大値を調節する
	// 逆行列の計算に失敗した時に別のパリティ・ブロックを使えるように +3個は残しておく
	if ((lost_num > 0) && (parity_now > lost_num + 3)){
		parity_ctx_r *tmp_p_blk;
		int max_num = 0;
		j = parity_num;
		for (i = 0; i < j; i++){
			if (p_blk[i].exist != 0){
				max_num++;
				if (max_num > lost_num + 3){
					max_num--;
					p_blk[i].exist = 0;
				} else {
					parity_num = i + 1;
				}
			}
		}
		tmp_p_blk = (parity_ctx_r *)realloc(p_blk, sizeof(parity_ctx_r) * parity_num);
		if (tmp_p_blk != NULL)
			p_blk = tmp_p_blk;
	}

	// ブロックを復元しないのなら、先にリカバリ・ファイルを閉じる
	if (lost_num == 0){
		for (i = 0; i < recovery_num; i++){
			if (rcv_hFile[i]){
				CloseHandle(rcv_hFile[i]);
				rcv_hFile[i] = NULL;
			}
		}
		free(rcv_hFile);
		rcv_hFile = NULL;
	}

	// 簡単な修復を先に行う、まだ修復の必要なファイルの数が戻る
	need_repair = simple_repair(ascii_buf, need_repair & 0x0FFFFFFF, files);

	// ブロック単位で復元することができない、または必要が無いならここで終わる
	if ((((block_count & 0x20000000) == 0) && (lost_num > parity_now))
			|| ((lost_num == 0) && (need_repair == 0))){
		if (switch_b & 4)
			replace_incomplete(uni_buf, ascii_buf, files, s_blk);	// 再構築したファイルで置き換える
		goto repair_end;
	}
	block_count &= 0x0FFFFFFF;

	if ((lost_num > 0) || (block_count > 0))
		printf("\nRepairing file  :\n");
	if (block_size == 4){	// 破損したソース・ファイルを作り直す
		if (err = restore_block4(uni_buf, files, s_blk))
			goto error_end;
	} else if (block_count > 0){	// 同じブロックを流用する、または逆算する
		if (err = restore_block(uni_buf, block_count, files, s_blk))
			goto error_end;
	}

	if ((lost_num > 0) && (lost_num <= parity_now)){	// 失われたブロックを復元する
		err = rs_decode(uni_buf, lost_num, rcv_hFile, files, s_blk, p_blk);
		if (err)
			goto error_end;
	}

	if (rcv_hFile){	// 検査前にリカバリ・ファイルを閉じる
		for (i = 0; i < recovery_num; i++){
			if (rcv_hFile[i])
				CloseHandle(rcv_hFile[i]);
		}
		free(rcv_hFile);
		rcv_hFile = NULL;
	}

	// 正しく修復できたか調べて結果表示する
	printf("\nVerifying repair: %d\n", need_repair);
	printf(" Status   :  Filename\n");
	fflush(stdout);
	if (err = verify_repair(uni_buf, ascii_buf, files, s_blk))
		goto error_end;

repair_end:
	err = 16;
	lost_num = 0;
	for (i = 0; i < file_num; i++){
		if (files[i].state & 0x3F)
			lost_num++;
	}
	if (lost_num == 0){	// 全て修復できたなら
		printf("\nRepaired successfully\n");
		need_repair = 0;
	} else {
		printf("\nFailed to repair %d file(s)\n", lost_num);
		err |= 4;
	}
	if (recovery_lost > 0)
		err |= 256;

	// ソースファイルを全て復元できたらリカバリファイルを削除する
	if ((need_repair == 0) && ((switch_b & 16) != 0))
		purge_recovery_file();

error_end:
	close_ini_file();
	if (recv_buf)
		free(recv_buf);
	if (recv2_buf)
		free(recv2_buf);
	if (s_blk)
		free(s_blk);
	if (p_blk)
		free(p_blk);
	if (rcv_hFile){
		for (i = 0; i < recovery_num; i++){
			if (rcv_hFile[i])
				CloseHandle(rcv_hFile[i]);
		}
		free(rcv_hFile);
	}
	if (files){
		if (need_repair)
			delete_work_file(uni_buf, files);
		free(files);
	}
	return err;
}

// ソース・ファイルの一覧を表示する
int par2_list(
	wchar_t *uni_buf,	// 作業用
	int switch_h)		// ハッシュ値も表示する
{
	char ascii_buf[MAX_LEN * 3];
	unsigned char set_id[16], *work_buf = NULL;
	int err = 0, i, file_block;
	file_ctx_r *files = NULL;

	if (switch_v & 7)	// 簡易検査、追加検査、順列検査の設定を無効にする
		switch_v &= ~7;

	// Main packet を探す (対応する Main packet のサイズは SEARCH_SIZE * 2 まで)
	work_buf = (unsigned char *)malloc(SEARCH_SIZE * 3);
	if (work_buf == NULL){
		printf("malloc, %d\n", SEARCH_SIZE * 3);
		err = 1;
		goto error_end;
	}
	recv_buf = recovery_file;	// 指定されたリカバリ・ファイルだけ調べる
	recv_len = (int)wcslen(recovery_file);
	recovery_num = 1;
	if (err = search_main_packet(work_buf, set_id)){
		printf("valid file is not found\n");
		goto error_end;
	}

	// ソース・ファイルの情報
	files = (file_ctx_r *)malloc(sizeof(file_ctx_r) * file_num);
	if (files == NULL){
		printf("malloc, %zd\n", sizeof(file_ctx_r) * file_num);
		err = 1;
		goto error_end;
	}
	// Main packet から File ID を読み取る
	for (i = 0; i < file_num; i++){
		memcpy(files[i].id, work_buf + (16 * i), 16);
		files[i].name = -2;
	}

	// ファイル情報のパケットを探す
	if (err = search_file_packet(ascii_buf, work_buf, uni_buf, set_id, 0, files))
		goto error_end;
	free(work_buf);
	work_buf = NULL;

	// ソース・ファイルの一覧を表示する
	printf("\nInput File list\t:\n");
	if (switch_h){
		printf("         Size  Slice             MD5 Hash             :  Filename\n");
	} else {
		printf("         Size  Slice :  Filename\n");
	}
	for (i = 0; i < file_num; i++){
		if (files[i].name < 0){	// ファイル情報が無くても処理を継続する
			if (switch_h){
				printf("            ?      ?                 ?                : Unknown\n");
			} else {
				printf("            ?      ? : Unknown\n");
			}
			err |= 4;
			continue;
		}
		utf16_to_cp(list_buf + files[i].name, ascii_buf, cp_output);
		if (files[i].size > 0){
			total_file_size += files[i].size;
			if (i < entity_num){
				file_block = (int)((files[i].size + (__int64)block_size - 1) / (__int64)block_size);	// ソース・ブロックの数
				source_num += file_block;
			} else {
				file_block = 0;
			}
			if (switch_h){
				printf("%13I64d %6d ", files[i].size, file_block);
				print_hash(files[i].hash);
				printf(" : \"%s\"\n", ascii_buf);
			} else {
				printf("%13I64d %6d : \"%s\"\n", files[i].size, file_block, ascii_buf);
			}
		} else {	// 空のファイルやフォルダ
			if (switch_h){
				printf("            0      0                                  : \"%s\"\n", ascii_buf);
			} else {
				printf("            0      0 : \"%s\"\n", ascii_buf);
			}
		}
	}

	printf("\nInput File total size\t: %I64d\n", total_file_size);
	printf("Input File Slice count\t: %d\n", source_num);
	if (err == 0){
		printf("\nListed successfully\n");
	} else {
		printf("\nFile Description packet is missing\n");
	}

error_end:
	if (work_buf)
		free(work_buf);
	if (files)
		free(files);
	return err;
}

// CRC-32 チェックサムを使って自分自身の破損を検出する
int par2_checksum(wchar_t *uni_buf)	// 作業用
{
	unsigned int rv, crc, chk, chk2;
	unsigned char *pAddr;
	HANDLE hFile, hMap;

	init_crc_table();	// CRC 計算用のテーブルを作成する

	// 実行ファイルのパスを取得する
	rv = GetModuleFileName(NULL, uni_buf, MAX_LEN);
	if ((rv == 0) || (rv >= MAX_LEN))
		return 1;
	//printf("%S\n", uni_buf);

	// 実行ファイルの PE checksum と CRC-32 を検証する
	hFile = CreateFile(uni_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		return 1;
	}
	rv = GetFileSize(hFile, &chk2);
	if (rv == INVALID_FILE_SIZE)
		return 1;
	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, chk2, rv, NULL);
	if (hMap == NULL){
		CloseHandle(hFile);
		return 1;
	}
	pAddr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, rv);
	if (pAddr == NULL){
		CloseHandle(hMap);
		CloseHandle(hFile);
		return 1;
	}
	if (CheckSumMappedFile(pAddr, rv, &chk2, &chk) == NULL){	// PE checksum
		UnmapViewOfFile(pAddr);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return 1;
	}
	crc = crc_update_std(0xFFFFFFFF, pAddr, rv) ^ 0xFFFFFFFF;	// CRC-32
	UnmapViewOfFile(pAddr);
	CloseHandle(hMap);
	CloseHandle(hFile);

	if (chk != chk2)
		return 2;
#ifndef _WIN64
	if (crc != 0x22222222)
#else
	if (crc != 0x22222A64)
#endif
		return 3;
	return 0;
}

