#ifndef _GF16_H_
#define _GF16_H_

#ifdef __cplusplus
extern "C" {
#endif


//extern unsigned short *galois_log_table;
extern unsigned int cpu_flag;

int galois_create_table(void);	// Returns 0 on success, -1 on failure

unsigned short galois_multiply(int x, int y);
unsigned short galois_multiply_fix(int x, int log_y);
unsigned short galois_divide(int x, int y);

unsigned short galois_power(int x, int y);	// 乗数計算用に追加
unsigned short galois_reciprocal(int x);	// 逆数計算用に追加
void galois_free_table(void);	// 解放用に追加

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern int sse_unit;

void galois_region_multiply(
	unsigned short *r1,	// Region to multiply
	unsigned short *r2,	// Products go here
	unsigned int count,	// Count of number in short
	int factor);		// Number to multiply by

void galois_region_divide(
	unsigned short *r1,	// Region to divide. products go here
	unsigned int count,	// Count of number in short
	int factor);		// Number to divide by

void galois_align_xor(
	unsigned char *r1,	// Region to multiply
	unsigned char *r2,	// Products go here
	unsigned int len);	// Byte length

// 領域掛け算用の関数定義
typedef void (* REGION_MULTIPLY) (
	unsigned char *r1,	// Region to multiply
	unsigned char *r2,	// Products go here
	unsigned int len,	// Byte length
	int factor);		// Number to multiply by
REGION_MULTIPLY galois_align_multiply;

typedef void (* REGION_MULTIPLY2) (
	unsigned char *src1,	// Region to multiply
	unsigned char *src2,
	unsigned char *dst,		// Products go here
	unsigned int len,		// Byte length
	int factor1,			// Number to multiply by
	int factor2);
REGION_MULTIPLY2 galois_align_multiply2;

// 領域並び替え用の関数定義
typedef void (* REGION_ALTMAP) (unsigned char *data, unsigned int bsize);
REGION_ALTMAP galois_altmap_change;
REGION_ALTMAP galois_altmap_return;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define HASH_SIZE 16
#define HASH_RANGE 128

void checksum16(unsigned char *data, unsigned char *hash, int byte_size);

// 領域並び替えとチェックサム計算の関数定義
typedef void (* region_checksum) (unsigned char *data, unsigned char *hash, int byte_size);
region_checksum checksum16_altmap;
region_checksum checksum16_return;


#ifdef __cplusplus
}
#endif

#endif
