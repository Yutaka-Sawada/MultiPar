
// CRC 計算用のテーブルを作る
void init_crc_table(void);

// CRC-32 を更新する
unsigned int crc_update(unsigned int crc, unsigned char *buf, unsigned int len);

