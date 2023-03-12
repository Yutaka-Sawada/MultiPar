/*----------------------------------------------------------------------------
;
; MD5 hash generator -- Paul Houle (paulhoule.com) 11/13/2017
;
; This code is in the public domain.  Please attribute the author.
;
; There are a lot of MD5 generators; here's another.  This one targets a
; little-endian memory architecture only (eg X86).  The benefit of this
; is speed -- bytes within larger elements never need to be reversed,
; which means the source data can be processed in-place.
;
; Though other compilers might be usable, this was developed using
; Microsoft 32/64-bit C 12.0 [Version 18.00.30723].  Vendor specific
; definitions (eg. _rotl, __int32, __int64) are used.
; Build commands:
;
;	cl /c /Ox phmd5.c
;	cl /c /Ox phmd5a.c
;
; Link the resulting .obj's into your executable and #include "phmd5.h"
;
; How to call the routines to generate a hash:
;
;	(1) Allocate a PHMD5 type struct -- it's small, can be static or local.
;		A pointer to this struct is the first argument to all functions.
;
;	(2) Call Phmd5Begin() once -- this initializes the PHMD5 struct.
;
;	(3) Call Phmd5Process() as many times as necessary for all data
;		to be included in the MD5 hash.
;
;	(4) Call Phmd5End() once.  The final 16-byte MD5 hash will then be
;		available in PHMD5->hash.  Note the finished hash is a simple array
;		of bytes, and must be treated/displayed/copied/etc that way.
;
; For best performance the Phmd5Process() "pdata" pointer should be 32-bit
; aligned (a multiple of 4) and "bytecnt" should be a multiple of 64.
; As long as both of these conditions continue to be met the input data is
; processed in-place; otherwise, some speed (10-15%) is lost as the data
; is copied to an internal blocking buffer before being proceessed.
;
;---------------------------------------------------------------------------*/

#ifndef _PHMD5_DEFINED					// include guard
#define _PHMD5_DEFINED

#include <stddef.h>
typedef struct {
	unsigned char hash[16];				// final 16-byte hash winds up here
	unsigned __int64 totbyt;			// processed byte count
	char buf[64];						// input blocking buffer
} PHMD5;

void Phmd5Begin(PHMD5 *pmd5);
void Phmd5Process(PHMD5 *pmd5, char *pdata, size_t bytecnt);
void Phmd5End(PHMD5 *pmd5);
void Phmd5DoBlocks(unsigned char *hash, char *pdata, size_t bytecnt);

#endif
