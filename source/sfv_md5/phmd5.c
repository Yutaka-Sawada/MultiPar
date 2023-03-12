/*----------------------------------------------------------------------------
;
; MD5 hash generator -- Paul Houle (paulhoule.com) 11/13/2017
;
; Non-time critical C logic.  All API entry points are here.
; See phmd5.h for documentation.
;
;---------------------------------------------------------------------------*/

#include <string.h>
#include "phmd5.h"

// First call -- initialize pmd5 structure for use.
void Phmd5Begin(PHMD5 *pmd5) {
	unsigned __int32 *uhash = (unsigned __int32 *) pmd5->hash;

	uhash[0] = 0x67452301;				// init hash per rfc1321
	uhash[1] = 0xEFCDAB89;
	uhash[2] = 0x98BADCFE;
	uhash[3] = 0x10325476;

	pmd5->totbyt = 0;					// init count of data bytes processed
}

// Last call -- after this, pmd5->hash holds final MD5 hash.
void Phmd5End(PHMD5 *pmd5) {
	char pad[72];						// pad buffer (worst case is 72 bytes)
	unsigned padc;						// size of needed pad (9-72 bytes)

	padc = 64 - ((unsigned) pmd5->totbyt & 63); // pad to 64-byte boundary
	if (padc < 9) padc += 64;			// add a block if we need more room
	memset(pad, 0, padc);				// clear entire pad area
	pad[0] = (char) 0x80;				// place input stream terminator
										// place 64-bit input data bit count
	*(unsigned __int64 *) &pad[padc - 8] = pmd5->totbyt << 3;
	Phmd5Process(pmd5, pad, padc);		// process the pad
}

// Work done here -- call for as many input blocks that need to be processed.
// pdata points to the input data, bytecnt is pdata size (0..n bytes).
// See phmd5.h regarding how to use this optimally.
void Phmd5Process(PHMD5 *pmd5, char *pdata, size_t bytecnt) {
	unsigned resid = (unsigned) pmd5->totbyt;

	pmd5->totbyt += bytecnt;			// update total bytes processed

	resid &= 63;						// count of bytes now in pmd5->buf

	// This block handles the case of residual data in pmd5->buf.
	// After this block pmd5->buf is empty (except perhaps on exit).

	if (resid) {						// if residual exists,
		unsigned cb = 64 - resid;
		if (cb > bytecnt) cb = (unsigned) bytecnt;
		memcpy(pmd5->buf + resid, pdata, cb);
		pdata += cb;
		bytecnt -= cb;
		if (resid + cb < 64) return;
		Phmd5DoBlocks(pmd5->hash, pmd5->buf, 64);
	}

	// This block processes input data in-place, if the data is dword
	// aligned and in 64-byte chunks.

	if ((unsigned) bytecnt & ~63 && ((size_t) pdata & 3) == 0) {
		Phmd5DoBlocks(pmd5->hash, pdata, bytecnt & ~63);
		pdata += bytecnt & ~63;
		bytecnt &= 63;
	}

	while (bytecnt) {					// handle residual/non-aligned data
		unsigned cb = 64 > (unsigned) bytecnt ? (unsigned) bytecnt : 64;
		memcpy(pmd5->buf, pdata, cb);
		pdata += cb;
		bytecnt -= cb;
		if (cb < 64) return;
		Phmd5DoBlocks(pmd5->hash, pmd5->buf, 64);
	};
}
