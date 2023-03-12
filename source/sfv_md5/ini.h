#ifndef _INI_H_
#define _INI_H_

#ifdef __cplusplus
extern "C" {
#endif


#define INI_NAME_LEN	38	// 検査結果ファイルのファイル名の文字数

extern int recent_data;

unsigned int time_f_u(FILETIME *file_time);

int check_ini_file(unsigned char *set_hash, unsigned int set_len);
void close_ini_file(void);
void write_ini_file(void);

int check_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	HANDLE hFile);			// そのファイルのハンドル

void write_ini_state(
	int num,				// ファイル番号
	unsigned int meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	int state);			// 状態


#ifdef __cplusplus
}
#endif

#endif
