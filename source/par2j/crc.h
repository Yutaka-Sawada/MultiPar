#ifndef _CRC_H_
#define _CRC_H_

#ifdef __cplusplus
extern "C" {
#endif


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// CRC-32 計算用

extern unsigned int crc_table[256];
extern unsigned int reverse_table[256];	// CRC-32 逆算用のテーブル

// CRC 計算用のテーブルを作る
void init_crc_table(void);

// CRC-32 を更新する
unsigned int crc_update(unsigned int crc, unsigned char *buf, unsigned int len);
unsigned int crc_update_std(unsigned int crc, unsigned char *buf, unsigned int len);

// 全て 0 のデータの CRC-32 を更新する
unsigned int crc_update_zero(unsigned int crc, unsigned int len);

// 内容が全て 0 のデータの CRC-32 を逆算するための関数
unsigned int crc_reverse_zero(unsigned int crc, unsigned int len);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// par2cmdline を参考にした関数
// window サイズの CRC を計算してある所に、1バイトずつ追加と削除をして、CRC を更新する
extern unsigned int window_table[256];
extern unsigned int window_mask;

void onepass_window_gen(unsigned int window_size);
void onepass_window_gen_short(unsigned int short_size, unsigned int short_table[256]);

// マクロなら
#define CRC_SLIDE_CHAR(x,y,z) (crc_table[((x) & 0xFF) ^ (y)] ^ ((x) >> 8) ^ window_table[z])

/*
// インライン展開なら
__inline unsigned int crc_slide_char(unsigned int crc, unsigned char chNew, unsigned char chOld){
	return crc_table[(crc & 0xFF) ^ chNew] ^ (crc >> 8) ^ window_table[chOld];
}
*/


#ifdef __cplusplus
}
#endif

#endif

