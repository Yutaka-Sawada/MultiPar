// SIMD function by Yutaka Sawada 2021-02-05

#include <string.h>
#include <emmintrin.h>	// MMX ~ SSE2 命令セットを使用する場合インクルード
#include "phmd5.h"


//#define F1(x, y, z) (((y ^ z) & x) ^ z)
#define F1(x, y, z) _mm_xor_si128(_mm_and_si128(_mm_xor_si128(y, z), x), z)

//#define F2(x, y, z) ((z & x) + (~z & y))
#define F2(x, y, z) _mm_or_si128(_mm_and_si128(x, z), _mm_andnot_si128(z, y))

//#define F3(x, y, z) (x ^ y ^ z)
#define F3(x, y, z) _mm_xor_si128(x, _mm_xor_si128(y, z))

//#define F4(x, y, z) (y ^ (x | ~z))
#define F4(x, y, z) _mm_xor_si128(y, _mm_or_si128(x, _mm_xor_si128(z, _mm_cmpeq_epi32(z, z))))
//#define F4(x, y, z) _mm_xor_si128(y, _mm_or_si128(x, _mm_xor_si128(z, _mm_set1_epi32(0xffffffff))))


// ビットローテーションをシャッフル命令で置き換える
#define MD5STEP(f, w, x, y, z, ix, s, sc) w = _mm_add_epi32(_mm_srli_epi64(_mm_shuffle_epi32(_mm_add_epi32(_mm_add_epi32(w, _mm_add_epi32(XX##ix, _mm_set1_epi32(sc))), f(x, y, z)), _MM_SHUFFLE(2, 2, 0, 0)), 32 - s), x)

// 展開した場合
/*
#define MD5STEP(f, w, x, y, z, ix, s, sc) { \
	w = _mm_add_epi32(_mm_add_epi32(w, _mm_add_epi32(XX##ix, _mm_set1_epi32(sc))), f(x, y, z)); \
	w = _mm_shuffle_epi32(w, _MM_SHUFFLE(2, 2, 0, 0)); \
	w = _mm_srli_epi64(w, 32 - s); \
	w = _mm_add_epi32(w, x); \
}
*/

// Read two 32-bit integers twice
// XX##a = [a0, a1,  0,  0] read 8-bytes each (little endian)
// XX##b = [b0, b1,  0,  0]
// XX##a = [a0, b0, a1, b1] after _mm_unpacklo_epi32(XX##a, XX##b)
// XX##b = [a1, a1, b1, b1] after _mm_unpackhi_epi32(XX##a, XX##a)
// XX##a = [a0, a0, b0, b0] after _mm_unpacklo_epi32(XX##a, XX##a)
#define READ2(a, b, x) { \
	XX##a = _mm_loadl_epi64((__m128i *) (pdata  + x)); \
	XX##b = _mm_loadl_epi64((__m128i *) (pdata2 + x)); \
	XX##a = _mm_unpacklo_epi32(XX##a, XX##b); \
	XX##b = _mm_unpackhi_epi32(XX##a, XX##a); \
	XX##a = _mm_unpacklo_epi32(XX##a, XX##a); \
}

// Read four 32-bit integers twice
// XX##a = [a0, a1, a2, a3] read 16-bytes each (little endian)
// XX##b = [b0, b1, b2, b3]
// XX##c = [a2, b2, a3, b3] after _mm_unpackhi_epi32(XX##a, XX##b)
// XX##a = [a0, b0, a1, b1] after _mm_unpacklo_epi32(XX##a, XX##b)
// XX##b = [a1, a1, b1, b1] after _mm_unpackhi_epi32(XX##a, XX##a)
// XX##a = [a0, a0, b0, b0] after _mm_unpacklo_epi32(XX##a, XX##a)
// XX##d = [a3, a3, b3, b3] after _mm_unpackhi_epi32(XX##c, XX##c)
// XX##c = [a2, a2, b2, b2] after _mm_unpacklo_epi32(XX##c, XX##c)
#define READ4(a, b, c, d, x) { \
	XX##a = _mm_loadu_si128((__m128i *) (pdata  + x)); \
	XX##b = _mm_loadu_si128((__m128i *) (pdata2 + x)); \
	XX##c = _mm_unpackhi_epi32(XX##a, XX##b); \
	XX##a = _mm_unpacklo_epi32(XX##a, XX##b); \
	XX##b = _mm_unpackhi_epi32(XX##a, XX##a); \
	XX##a = _mm_unpacklo_epi32(XX##a, XX##a); \
	XX##d = _mm_unpackhi_epi32(XX##c, XX##c); \
	XX##c = _mm_unpacklo_epi32(XX##c, XX##c); \
}

void Phmd5DoBlocks2(
	unsigned char *hash,
	unsigned char *hash2,
	char *pdata,
	char *pdata2,
	size_t bytecnt
) {
	__m128i h0, h1, h2, h3;
	__m128i a, b, c, d;
	__m128i XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7;
	__m128i XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15;

	// same method as READ4
	h0 = _mm_loadu_si128((__m128i *) hash);
	h1 = _mm_loadu_si128((__m128i *) hash2);
	h2 = _mm_unpackhi_epi32(h0, h1);
	h0 = _mm_unpacklo_epi32(h0, h1);
	h1 = _mm_unpackhi_epi32(h0, h0);
	h0 = _mm_unpacklo_epi32(h0, h0);
	h3 = _mm_unpackhi_epi32(h2, h2);
	h2 = _mm_unpacklo_epi32(h2, h2);
//	h0 = _mm_set_epi32(0, *(unsigned __int32 *) &hash2[ 0], 0, *(unsigned __int32 *) &hash[ 0] );
//	h1 = _mm_set_epi32(0, *(unsigned __int32 *) &hash2[ 4], 0, *(unsigned __int32 *) &hash[ 4] );
//	h2 = _mm_set_epi32(0, *(unsigned __int32 *) &hash2[ 8], 0, *(unsigned __int32 *) &hash[ 8] );
//	h3 = _mm_set_epi32(0, *(unsigned __int32 *) &hash2[12], 0, *(unsigned __int32 *) &hash[12] );
	_mm_store_si128(&a, h0);
	_mm_store_si128(&b, h1);
	_mm_store_si128(&c, h2);
	_mm_store_si128(&d, h3);

	do {
//		READ4( 0,  1,  2,  3,  0);
		READ2( 0,  1,  0);
		MD5STEP(F1, a, b, c, d,  0,  7, 0xd76aa478);
		MD5STEP(F1, d, a, b, c,  1, 12, 0xe8c7b756);
		READ2( 2,  3,  8);
		MD5STEP(F1, c, d, a, b,  2, 17, 0x242070db);
		MD5STEP(F1, b, c, d, a,  3, 22, 0xc1bdceee);
//		READ4( 4,  5,  6,  7, 16);
		READ2( 4,  5, 16);
		MD5STEP(F1, a, b, c, d,  4,  7, 0xf57c0faf);
		MD5STEP(F1, d, a, b, c,  5, 12, 0x4787c62a);
		READ2( 6,  7, 24);
		MD5STEP(F1, c, d, a, b,  6, 17, 0xa8304613);
		MD5STEP(F1, b, c, d, a,  7, 22, 0xfd469501);
//		READ4( 8,  9, 10, 11, 32);
		READ2( 8,  9, 32);
		MD5STEP(F1, a, b, c, d,  8,  7, 0x698098d8);
		MD5STEP(F1, d, a, b, c,  9, 12, 0x8b44f7af);
		READ2(10, 11, 40);
		MD5STEP(F1, c, d, a, b, 10, 17, 0xffff5bb1);
		MD5STEP(F1, b, c, d, a, 11, 22, 0x895cd7be);
//		READ4(12, 13, 14, 15, 48);
		READ2(12, 13, 48);
		MD5STEP(F1, a, b, c, d, 12,  7, 0x6b901122);
		MD5STEP(F1, d, a, b, c, 13, 12, 0xfd987193);
		READ2(14, 15, 56);
		MD5STEP(F1, c, d, a, b, 14, 17, 0xa679438e);
		MD5STEP(F1, b, c, d, a, 15, 22, 0x49b40821);

		MD5STEP(F2, a, b, c, d,  1,  5, 0xf61e2562);
		MD5STEP(F2, d, a, b, c,  6,  9, 0xc040b340);
		MD5STEP(F2, c, d, a, b, 11, 14, 0x265e5a51);
		MD5STEP(F2, b, c, d, a,  0, 20, 0xe9b6c7aa);
		MD5STEP(F2, a, b, c, d,  5,  5, 0xd62f105d);
		MD5STEP(F2, d, a, b, c, 10,  9, 0x02441453);
		MD5STEP(F2, c, d, a, b, 15, 14, 0xd8a1e681);
		MD5STEP(F2, b, c, d, a,  4, 20, 0xe7d3fbc8);
		MD5STEP(F2, a, b, c, d,  9,  5, 0x21e1cde6);
		MD5STEP(F2, d, a, b, c, 14,  9, 0xc33707d6);
		MD5STEP(F2, c, d, a, b,  3, 14, 0xf4d50d87);
		MD5STEP(F2, b, c, d, a,  8, 20, 0x455a14ed);
		MD5STEP(F2, a, b, c, d, 13,  5, 0xa9e3e905);
		MD5STEP(F2, d, a, b, c,  2,  9, 0xfcefa3f8);
		MD5STEP(F2, c, d, a, b,  7, 14, 0x676f02d9);
		MD5STEP(F2, b, c, d, a, 12, 20, 0x8d2a4c8a);

		MD5STEP(F3, a, b, c, d,  5,  4, 0xfffa3942);
		MD5STEP(F3, d, a, b, c,  8, 11, 0x8771f681);
		MD5STEP(F3, c, d, a, b, 11, 16, 0x6d9d6122);
		MD5STEP(F3, b, c, d, a, 14, 23, 0xfde5380c);
		MD5STEP(F3, a, b, c, d,  1,  4, 0xa4beea44);
		MD5STEP(F3, d, a, b, c,  4, 11, 0x4bdecfa9);
		MD5STEP(F3, c, d, a, b,  7, 16, 0xf6bb4b60);
		MD5STEP(F3, b, c, d, a, 10, 23, 0xbebfbc70);
		MD5STEP(F3, a, b, c, d, 13,  4, 0x289b7ec6);
		MD5STEP(F3, d, a, b, c,  0, 11, 0xeaa127fa);
		MD5STEP(F3, c, d, a, b,  3, 16, 0xd4ef3085);
		MD5STEP(F3, b, c, d, a,  6, 23, 0x04881d05);
		MD5STEP(F3, a, b, c, d,  9,  4, 0xd9d4d039);
		MD5STEP(F3, d, a, b, c, 12, 11, 0xe6db99e5);
		MD5STEP(F3, c, d, a, b, 15, 16, 0x1fa27cf8);
		MD5STEP(F3, b, c, d, a,  2, 23, 0xc4ac5665);

		MD5STEP(F4, a, b, c, d,  0,  6, 0xf4292244);
		MD5STEP(F4, d, a, b, c,  7, 10, 0x432aff97);
		MD5STEP(F4, c, d, a, b, 14, 15, 0xab9423a7);
		MD5STEP(F4, b, c, d, a,  5, 21, 0xfc93a039);
		MD5STEP(F4, a, b, c, d, 12,  6, 0x655b59c3);
		MD5STEP(F4, d, a, b, c,  3, 10, 0x8f0ccc92);
		MD5STEP(F4, c, d, a, b, 10, 15, 0xffeff47d);
		MD5STEP(F4, b, c, d, a,  1, 21, 0x85845dd1);
		MD5STEP(F4, a, b, c, d,  8,  6, 0x6fa87e4f);
		MD5STEP(F4, d, a, b, c, 15, 10, 0xfe2ce6e0);
		MD5STEP(F4, c, d, a, b,  6, 15, 0xa3014314);
		MD5STEP(F4, b, c, d, a, 13, 21, 0x4e0811a1);
		MD5STEP(F4, a, b, c, d,  4,  6, 0xf7537e82);
		MD5STEP(F4, d, a, b, c, 11, 10, 0xbd3af235);
		MD5STEP(F4, c, d, a, b,  2, 15, 0x2ad7d2bb);
		MD5STEP(F4, b, c, d, a,  9, 21, 0xeb86d391);

		a = _mm_add_epi32(a, h0);
		b = _mm_add_epi32(b, h1);
		c = _mm_add_epi32(c, h2);
		d = _mm_add_epi32(d, h3);

		_mm_store_si128(&h0, a);
		_mm_store_si128(&h1, b);
		_mm_store_si128(&h2, c);
		_mm_store_si128(&h3, d);

		pdata += 64;
		pdata2 += 64;
	} while (bytecnt -= 64);

	*(unsigned __int32 *) &hash[ 0] = _mm_cvtsi128_si32(h0);
	*(unsigned __int32 *) &hash[ 4] = _mm_cvtsi128_si32(h1);
	*(unsigned __int32 *) &hash[ 8] = _mm_cvtsi128_si32(h2);
	*(unsigned __int32 *) &hash[12] = _mm_cvtsi128_si32(h3);
	h0 = _mm_srli_si128(h0, 8);	// right shift 8-bytes
	h1 = _mm_srli_si128(h1, 8);
	h2 = _mm_srli_si128(h2, 8);
	h3 = _mm_srli_si128(h3, 8);
	*(unsigned __int32 *) &hash2[ 0] = _mm_cvtsi128_si32(h0);
	*(unsigned __int32 *) &hash2[ 4] = _mm_cvtsi128_si32(h1);
	*(unsigned __int32 *) &hash2[ 8] = _mm_cvtsi128_si32(h2);
	*(unsigned __int32 *) &hash2[12] = _mm_cvtsi128_si32(h3);
}

// SIMD version updates two MD5 at once.
// The data must be dword (4-bytes) aligned.
void Phmd5Process2(PHMD5 *pmd5, PHMD5 *pmd52, char *pdata, size_t bytecnt) {
	char *pdata2;
	size_t bytefin, bytecnt2;
	unsigned cb, resid, resid2;

	pdata2 = pdata;
	bytecnt2 = bytecnt;
	resid = (unsigned) pmd5->totbyt;
	resid2 = (unsigned) pmd52->totbyt;
	pmd5->totbyt += bytecnt;			// update total bytes processed
	pmd52->totbyt += bytecnt;

	resid &= 63;						// count of bytes now in pmd5->buf
	resid2 &= 63;

	// This block handles the case of residual data in pmd5->buf.
	// After this block pmd5->buf is empty (except perhaps on exit).

	if (resid) {						// if residual exists,
		cb = 64 - resid;
		if (cb > bytecnt) cb = (unsigned) bytecnt;
		memcpy(pmd5->buf + resid, pdata, cb);
		pdata += cb;
		bytecnt -= cb;
		if (resid + cb == 64) Phmd5DoBlocks(pmd5->hash, pmd5->buf, 64);
	}
	bytefin = bytecnt & ~63;
	if (resid2) {
		cb = 64 - resid2;
		if (cb > bytecnt2) cb = (unsigned) bytecnt2;
		memcpy(pmd52->buf + resid2, pdata2, cb);
		pdata2 += cb;
		bytecnt2 -= cb;
		if (bytecnt2 < bytefin) bytefin = bytecnt2 & ~63;	// shorter size
		if (resid2 + cb == 64) Phmd5DoBlocks(pmd52->hash, pmd52->buf, 64);
	}

	// This block processes input data in-place, if the data is dword
	// aligned and in 64-byte chunks.

	if (bytefin) {
		//Phmd5DoBlocks(pmd5->hash, pdata, bytefin);
		//Phmd5DoBlocks(pmd52->hash, pdata2, bytefin);
		Phmd5DoBlocks2(pmd5->hash, pmd52->hash, pdata, pdata2, bytefin);
		pdata += bytefin;
		pdata2 += bytefin;
		bytecnt -= bytefin;
		bytecnt2 -= bytefin;
	}

	while (bytecnt) {					// handle residual/non-aligned data
		cb = 64 > (unsigned) bytecnt ? (unsigned) bytecnt : 64;
		memcpy(pmd5->buf, pdata, cb);
		pdata += cb;
		bytecnt -= cb;
		if (cb < 64) break;
		Phmd5DoBlocks(pmd5->hash, pmd5->buf, 64);
	};
	while (bytecnt2) {
		cb = 64 > (unsigned) bytecnt2 ? (unsigned) bytecnt2 : 64;
		memcpy(pmd52->buf, pdata2, cb);
		pdata2 += cb;
		bytecnt2 -= cb;
		if (cb < 64) break;
		Phmd5DoBlocks(pmd52->hash, pmd52->buf, 64);
	};
}

