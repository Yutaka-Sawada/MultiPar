/*----------------------------------------------------------------------------
;
; MD5 hash generator -- Paul Houle (paulhoule.com) 11/13/2017
;
; Called only from phmd5.c -- see phmd5.h for overview
;
; This is the same logic in phmd5a.asm, implemented (after the fact) in C.
; The C compiler must support the "_rotl()" function generated inline to
; achieve maximal performance.
;
; It turns out the MSFT C compiler, coupled with newer processor, results
; in timings comparable to the 32-bit only phmd5a.asm hand-written assembly.
; Therefore this "C" implementation is now used.
; Avoiding assembly allows the code to be compiled either 32 or 64 bit.
;
; Note that a "little-endian" memory architecture is assumed.
;
; The Fx() and MD5STEP() macros were written by Colin Plumb in 1993.
; MD5STEP() was changed slightly to match how phmd5a.asm operates.
;
;---------------------------------------------------------------------------*/

/*
#include <stdlib.h>						// for _rotl()
#include "phmd5.h"


// MD5 Optimisation Tricks by Anime Tosho
// https://github.com/animetosho/md5-optimisation#optimisation-tricks-single-buffer
// Dependency shortcut in G function
#define F1(x, y, z) ((x & y) + (~x & z))

//#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

#define MD5STEP(f, w, x, y, z, ix, s, sc) \
	w = _rotl(w + f(x, y, z) + ((unsigned *) pdata)[ix] + sc, s) + x

void Phmd5DoBlocks(
	unsigned char *hash,
	char *pdata,
	size_t bytecnt
) {
	unsigned __int32 a = *(unsigned __int32 *) &hash[ 0];
	unsigned __int32 b = *(unsigned __int32 *) &hash[ 4];
	unsigned __int32 c = *(unsigned __int32 *) &hash[ 8];
	unsigned __int32 d = *(unsigned __int32 *) &hash[12];

	do {
		MD5STEP(F1, a, b, c, d,  0,  7, 0xd76aa478);
		MD5STEP(F1, d, a, b, c,  1, 12, 0xe8c7b756);
		MD5STEP(F1, c, d, a, b,  2, 17, 0x242070db);
		MD5STEP(F1, b, c, d, a,  3, 22, 0xc1bdceee);
		MD5STEP(F1, a, b, c, d,  4,  7, 0xf57c0faf);
		MD5STEP(F1, d, a, b, c,  5, 12, 0x4787c62a);
		MD5STEP(F1, c, d, a, b,  6, 17, 0xa8304613);
		MD5STEP(F1, b, c, d, a,  7, 22, 0xfd469501);
		MD5STEP(F1, a, b, c, d,  8,  7, 0x698098d8);
		MD5STEP(F1, d, a, b, c,  9, 12, 0x8b44f7af);
		MD5STEP(F1, c, d, a, b, 10, 17, 0xffff5bb1);
		MD5STEP(F1, b, c, d, a, 11, 22, 0x895cd7be);
		MD5STEP(F1, a, b, c, d, 12,  7, 0x6b901122);
		MD5STEP(F1, d, a, b, c, 13, 12, 0xfd987193);
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

		a += *(unsigned __int32 *) &hash[ 0];
		b += *(unsigned __int32 *) &hash[ 4];
		c += *(unsigned __int32 *) &hash[ 8];
		d += *(unsigned __int32 *) &hash[12];

		*(unsigned __int32 *) &hash[ 0] = a;
		*(unsigned __int32 *) &hash[ 4] = b;
		*(unsigned __int32 *) &hash[ 8] = c;
		*(unsigned __int32 *) &hash[12] = d;

		pdata += 64;
	} while (bytecnt -= 64);
}

#undef MD5STEP

// for update with null bytes
#define MD5STEP(f, w, x, y, z, ix, s, sc) \
	w = _rotl(w + f(x, y, z) + sc, s) + x

void Phmd5DoBlocksZero(
	unsigned char *hash,
	size_t bytecnt
) {
	unsigned __int32 a = *(unsigned __int32 *) &hash[ 0];
	unsigned __int32 b = *(unsigned __int32 *) &hash[ 4];
	unsigned __int32 c = *(unsigned __int32 *) &hash[ 8];
	unsigned __int32 d = *(unsigned __int32 *) &hash[12];

	do {
		MD5STEP(F1, a, b, c, d,  0,  7, 0xd76aa478);
		MD5STEP(F1, d, a, b, c,  1, 12, 0xe8c7b756);
		MD5STEP(F1, c, d, a, b,  2, 17, 0x242070db);
		MD5STEP(F1, b, c, d, a,  3, 22, 0xc1bdceee);
		MD5STEP(F1, a, b, c, d,  4,  7, 0xf57c0faf);
		MD5STEP(F1, d, a, b, c,  5, 12, 0x4787c62a);
		MD5STEP(F1, c, d, a, b,  6, 17, 0xa8304613);
		MD5STEP(F1, b, c, d, a,  7, 22, 0xfd469501);
		MD5STEP(F1, a, b, c, d,  8,  7, 0x698098d8);
		MD5STEP(F1, d, a, b, c,  9, 12, 0x8b44f7af);
		MD5STEP(F1, c, d, a, b, 10, 17, 0xffff5bb1);
		MD5STEP(F1, b, c, d, a, 11, 22, 0x895cd7be);
		MD5STEP(F1, a, b, c, d, 12,  7, 0x6b901122);
		MD5STEP(F1, d, a, b, c, 13, 12, 0xfd987193);
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

		a += *(unsigned __int32 *) &hash[ 0];
		b += *(unsigned __int32 *) &hash[ 4];
		c += *(unsigned __int32 *) &hash[ 8];
		d += *(unsigned __int32 *) &hash[12];

		*(unsigned __int32 *) &hash[ 0] = a;
		*(unsigned __int32 *) &hash[ 4] = b;
		*(unsigned __int32 *) &hash[ 8] = c;
		*(unsigned __int32 *) &hash[12] = d;

	} while (bytecnt -= 64);
}

#undef MD5STEP

*/


// SIMD function by Yutaka Sawada 2021-02-04

#include <string.h>
#include <emmintrin.h>	// for MMX ~ SSE2
#include "phmd5.h"


// delays `x` dependency by Anime Tosho

//#define F1(x, y, z) (((y ^ z) & x) ^ z)
#define F1(x, y, z) _mm_xor_si128(_mm_and_si128(_mm_xor_si128(y, z), x), z)

//#define F2(x, y, z) ((z & x) + (~z & y))
#define F2(x, y, z) _mm_or_si128(_mm_and_si128(x, z), _mm_andnot_si128(z, y))

//#define F3(x, y, z) (x ^ y ^ z)
#define F3(x, y, z) _mm_xor_si128(x, _mm_xor_si128(y, z))

//#define F4(x, y, z) (y ^ (x | ~z))
#define F4(x, y, z) _mm_xor_si128(y, _mm_or_si128(x, _mm_xor_si128(z, _mm_cmpeq_epi32(z, z))))

#define MD5STEP(f, w, x, y, z, ix, s, sc) w = _mm_add_epi32(_mm_srli_epi64(_mm_shuffle_epi32(_mm_add_epi32(_mm_add_epi32(w, _mm_set1_epi32(((unsigned __int32 *) pdata)[ix] + sc)), f(x, y, z)), _MM_SHUFFLE(0, 0, 0, 0)), 32 - s), x)
/*
#define MD5STEP(f, w, x, y, z, ix, s, sc) { \
	w = _mm_add_epi32(_mm_add_epi32(w, _mm_set1_epi32(((unsigned __int32 *) pdata)[ix] + sc)), f(x, y, z)); \
	w = _mm_shuffle_epi32(w, _MM_SHUFFLE(0, 0, 0, 0)); \
	w = _mm_srli_epi64(w, 32 - s); \
	w = _mm_add_epi32(w, x); \
}
*/

// SSE2 version
void Phmd5DoBlocks(
	unsigned char *hash,
	char *pdata,
	size_t bytecnt
) {
	__m128i h0, h1, h2, h3;
	__m128i a, b, c, d;

	h0 = _mm_loadu_si128((__m128i *) hash);	// h0 = [a0, a1, a2, a3] (little endian)
	h1 = _mm_srli_si128(h0,  4);			// h1 = [a1, a2, a3,  0]
	h2 = _mm_srli_si128(h0,  8);			// h2 = [a2, a3,  0,  0]
	h3 = _mm_srli_si128(h0, 12);			// h3 = [a3,  0,  0,  0]
	_mm_store_si128(&a, h0);
	_mm_store_si128(&b, h1);
	_mm_store_si128(&c, h2);
	_mm_store_si128(&d, h3);

	do {
		MD5STEP(F1, a, b, c, d,  0,  7, 0xd76aa478);
		MD5STEP(F1, d, a, b, c,  1, 12, 0xe8c7b756);
		MD5STEP(F1, c, d, a, b,  2, 17, 0x242070db);
		MD5STEP(F1, b, c, d, a,  3, 22, 0xc1bdceee);
		MD5STEP(F1, a, b, c, d,  4,  7, 0xf57c0faf);
		MD5STEP(F1, d, a, b, c,  5, 12, 0x4787c62a);
		MD5STEP(F1, c, d, a, b,  6, 17, 0xa8304613);
		MD5STEP(F1, b, c, d, a,  7, 22, 0xfd469501);
		MD5STEP(F1, a, b, c, d,  8,  7, 0x698098d8);
		MD5STEP(F1, d, a, b, c,  9, 12, 0x8b44f7af);
		MD5STEP(F1, c, d, a, b, 10, 17, 0xffff5bb1);
		MD5STEP(F1, b, c, d, a, 11, 22, 0x895cd7be);
		MD5STEP(F1, a, b, c, d, 12,  7, 0x6b901122);
		MD5STEP(F1, d, a, b, c, 13, 12, 0xfd987193);
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
	} while (bytecnt -= 64);

	*(unsigned __int32 *) &hash[ 0] = _mm_cvtsi128_si32(h0);
	*(unsigned __int32 *) &hash[ 4] = _mm_cvtsi128_si32(h1);
	*(unsigned __int32 *) &hash[ 8] = _mm_cvtsi128_si32(h2);
	*(unsigned __int32 *) &hash[12] = _mm_cvtsi128_si32(h3);
}


// for update with null bytes
#define MD5STEP0(f, w, x, y, z, s, sc) w = _mm_add_epi32(_mm_srli_epi64(_mm_shuffle_epi32(_mm_add_epi32(_mm_add_epi32(w, _mm_set1_epi32(sc)), f(x, y, z)), _MM_SHUFFLE(0, 0, 0, 0)), 32 - s), x)

// zero fill version
void Phmd5DoBlocksZero(
	unsigned char *hash,
	size_t bytecnt
) {
	__m128i h0, h1, h2, h3;
	__m128i a, b, c, d;

	h0 = _mm_loadu_si128((__m128i *) hash);	// h0 = [a0, a1, a2, a3] (little endian)
	h1 = _mm_srli_si128(h0,  4);			// h1 = [a1, a2, a3,  0]
	h2 = _mm_srli_si128(h0,  8);			// h2 = [a2, a3,  0,  0]
	h3 = _mm_srli_si128(h0, 12);			// h3 = [a3,  0,  0,  0]
	_mm_store_si128(&a, h0);
	_mm_store_si128(&b, h1);
	_mm_store_si128(&c, h2);
	_mm_store_si128(&d, h3);

	do {
		MD5STEP0(F1, a, b, c, d,  7, 0xd76aa478);
		MD5STEP0(F1, d, a, b, c, 12, 0xe8c7b756);
		MD5STEP0(F1, c, d, a, b, 17, 0x242070db);
		MD5STEP0(F1, b, c, d, a, 22, 0xc1bdceee);
		MD5STEP0(F1, a, b, c, d,  7, 0xf57c0faf);
		MD5STEP0(F1, d, a, b, c, 12, 0x4787c62a);
		MD5STEP0(F1, c, d, a, b, 17, 0xa8304613);
		MD5STEP0(F1, b, c, d, a, 22, 0xfd469501);
		MD5STEP0(F1, a, b, c, d,  7, 0x698098d8);
		MD5STEP0(F1, d, a, b, c, 12, 0x8b44f7af);
		MD5STEP0(F1, c, d, a, b, 17, 0xffff5bb1);
		MD5STEP0(F1, b, c, d, a, 22, 0x895cd7be);
		MD5STEP0(F1, a, b, c, d,  7, 0x6b901122);
		MD5STEP0(F1, d, a, b, c, 12, 0xfd987193);
		MD5STEP0(F1, c, d, a, b, 17, 0xa679438e);
		MD5STEP0(F1, b, c, d, a, 22, 0x49b40821);

		MD5STEP0(F2, a, b, c, d,  5, 0xf61e2562);
		MD5STEP0(F2, d, a, b, c,  9, 0xc040b340);
		MD5STEP0(F2, c, d, a, b, 14, 0x265e5a51);
		MD5STEP0(F2, b, c, d, a, 20, 0xe9b6c7aa);
		MD5STEP0(F2, a, b, c, d,  5, 0xd62f105d);
		MD5STEP0(F2, d, a, b, c,  9, 0x02441453);
		MD5STEP0(F2, c, d, a, b, 14, 0xd8a1e681);
		MD5STEP0(F2, b, c, d, a, 20, 0xe7d3fbc8);
		MD5STEP0(F2, a, b, c, d,  5, 0x21e1cde6);
		MD5STEP0(F2, d, a, b, c,  9, 0xc33707d6);
		MD5STEP0(F2, c, d, a, b, 14, 0xf4d50d87);
		MD5STEP0(F2, b, c, d, a, 20, 0x455a14ed);
		MD5STEP0(F2, a, b, c, d,  5, 0xa9e3e905);
		MD5STEP0(F2, d, a, b, c,  9, 0xfcefa3f8);
		MD5STEP0(F2, c, d, a, b, 14, 0x676f02d9);
		MD5STEP0(F2, b, c, d, a, 20, 0x8d2a4c8a);

		MD5STEP0(F3, a, b, c, d,  4, 0xfffa3942);
		MD5STEP0(F3, d, a, b, c, 11, 0x8771f681);
		MD5STEP0(F3, c, d, a, b, 16, 0x6d9d6122);
		MD5STEP0(F3, b, c, d, a, 23, 0xfde5380c);
		MD5STEP0(F3, a, b, c, d,  4, 0xa4beea44);
		MD5STEP0(F3, d, a, b, c, 11, 0x4bdecfa9);
		MD5STEP0(F3, c, d, a, b, 16, 0xf6bb4b60);
		MD5STEP0(F3, b, c, d, a, 23, 0xbebfbc70);
		MD5STEP0(F3, a, b, c, d,  4, 0x289b7ec6);
		MD5STEP0(F3, d, a, b, c, 11, 0xeaa127fa);
		MD5STEP0(F3, c, d, a, b, 16, 0xd4ef3085);
		MD5STEP0(F3, b, c, d, a, 23, 0x04881d05);
		MD5STEP0(F3, a, b, c, d,  4, 0xd9d4d039);
		MD5STEP0(F3, d, a, b, c, 11, 0xe6db99e5);
		MD5STEP0(F3, c, d, a, b, 16, 0x1fa27cf8);
		MD5STEP0(F3, b, c, d, a, 23, 0xc4ac5665);

		MD5STEP0(F4, a, b, c, d,  6, 0xf4292244);
		MD5STEP0(F4, d, a, b, c, 10, 0x432aff97);
		MD5STEP0(F4, c, d, a, b, 15, 0xab9423a7);
		MD5STEP0(F4, b, c, d, a, 21, 0xfc93a039);
		MD5STEP0(F4, a, b, c, d,  6, 0x655b59c3);
		MD5STEP0(F4, d, a, b, c, 10, 0x8f0ccc92);
		MD5STEP0(F4, c, d, a, b, 15, 0xffeff47d);
		MD5STEP0(F4, b, c, d, a, 21, 0x85845dd1);
		MD5STEP0(F4, a, b, c, d,  6, 0x6fa87e4f);
		MD5STEP0(F4, d, a, b, c, 10, 0xfe2ce6e0);
		MD5STEP0(F4, c, d, a, b, 15, 0xa3014314);
		MD5STEP0(F4, b, c, d, a, 21, 0x4e0811a1);
		MD5STEP0(F4, a, b, c, d,  6, 0xf7537e82);
		MD5STEP0(F4, d, a, b, c, 10, 0xbd3af235);
		MD5STEP0(F4, c, d, a, b, 15, 0x2ad7d2bb);
		MD5STEP0(F4, b, c, d, a, 21, 0xeb86d391);

		a = _mm_add_epi32(a, h0);
		b = _mm_add_epi32(b, h1);
		c = _mm_add_epi32(c, h2);
		d = _mm_add_epi32(d, h3);

		_mm_store_si128(&h0, a);
		_mm_store_si128(&h1, b);
		_mm_store_si128(&h2, c);
		_mm_store_si128(&h3, d);

	} while (bytecnt -= 64);

	*(unsigned __int32 *) &hash[ 0] = _mm_cvtsi128_si32(h0);
	*(unsigned __int32 *) &hash[ 4] = _mm_cvtsi128_si32(h1);
	*(unsigned __int32 *) &hash[ 8] = _mm_cvtsi128_si32(h2);
	*(unsigned __int32 *) &hash[12] = _mm_cvtsi128_si32(h3);
}

