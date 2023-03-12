#ifndef __GF_JIT__
#define __GF_JIT__

// from ParPar; "x86_jit.c"
#include <stdint.h>
#include <windows.h>

#ifdef _WIN64
typedef unsigned __int64 FAST_U8;
typedef unsigned __int64 FAST_U16;
typedef unsigned __int64 FAST_U32;
#define FAST_U8_SIZE 8
#define FAST_U16_SIZE 8
#define FAST_U32_SIZE 8
#else
typedef unsigned __int32 FAST_U8;
typedef unsigned __int32 FAST_U16;
typedef unsigned __int32 FAST_U32;
#define FAST_U8_SIZE 4
#define FAST_U16_SIZE 4
#define FAST_U32_SIZE 4
#endif

#define MAX_CPU	18	// Max number of threads

unsigned char *jit_code = NULL;
int *jit_id;

// 最初と最後に呼び出すこと (MAX_CPU スレッドまで対応できる)
static __inline int jit_alloc(void){	// 4KB should be enough (but, multiply for multi-core)
	if (jit_code != NULL)
		return 0;
	jit_code = VirtualAlloc(NULL, 4096 * MAX_CPU, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (jit_code == NULL)
		return -1;
	jit_id = (int *)(jit_code + 4096 - 4 * MAX_CPU);
	return 0;
}

static __inline void jit_free(void){
	if (jit_code == NULL)
		return;
	VirtualFree(jit_code, 0, MEM_RELEASE);
	jit_code = NULL;
}


// registers
#define AX 0
#define BX 3
#define CX 1
#define DX 2
#define DI 7
#define SI 6
#define BP 5
#define SP 4

// conditional jumps
#define JE  0x4
#define JNE 0x5
#define JL  0xC
#define JGE 0xD
#define JLE 0xE
#define JG  0xF


#if defined(_WIN64)	// 64-bit 版なら
	#define RXX_PREFIX *((*jit_ptr)++) = 0x48;
#else
	#define RXX_PREFIX
#endif

static __inline void _jit_rex_pref(unsigned char **jit_ptr, uint8_t xreg, uint8_t xreg2){
#ifdef _WIN64
	if (xreg > 7 || xreg2 > 7){
		*((*jit_ptr)++) = 0x40 | (xreg2 >>3) | ((xreg >>1)&4);
	}
#endif
}

static __inline void _jit_movaps_load(unsigned char **jit_ptr, uint8_t xreg, uint8_t mreg, int32_t offs){
	_jit_rex_pref(jit_ptr, xreg, 0);
	xreg &= 7;
	if ((offs+128) & ~0xFF){
		*(int32_t*)(*jit_ptr) = 0x80280F | (xreg <<19) | (mreg <<16);
		*(int32_t*)((*jit_ptr) +3) = offs;
		(*jit_ptr) += 7;
	} else if (offs){
		*(int32_t*)(*jit_ptr) = 0x40280F | (xreg <<19) | (mreg <<16) | (offs <<24);
		(*jit_ptr) += 4;
	} else {
		// can overflow, but we don't care
		*(int32_t*)(*jit_ptr) = 0x280F | (xreg <<19) | (mreg <<16);
		(*jit_ptr) += 3;
	}
}
static __inline void _jit_movaps_store(unsigned char **jit_ptr, uint8_t mreg, int32_t offs, uint8_t xreg){
	_jit_rex_pref(jit_ptr, xreg, 0);
	xreg &= 7;
	if ((offs+128) & ~0xFF){
		*(int32_t*)(*jit_ptr) = 0x80290F | (xreg <<19) | (mreg <<16);
		*(int32_t*)((*jit_ptr) +3) = offs;
		jit_ptr += 7;
	} else if (offs){
		*(int32_t*)(*jit_ptr) = 0x40290F | (xreg <<19) | (mreg <<16) | (offs <<24);
		(*jit_ptr) += 4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)(*jit_ptr) = 0x290F | (xreg <<19) | (mreg <<16);
		(*jit_ptr) += 3;
	}
}

static __inline void _jit_push(unsigned char **jit_ptr, uint8_t reg){
	*((*jit_ptr)++) = 0x50 | reg;
}
static __inline void _jit_pop(unsigned char **jit_ptr, uint8_t reg){
	*((*jit_ptr)++) = 0x58 | reg;
}
static __inline void _jit_jcc(unsigned char **jit_ptr, char op, uint8_t* addr){
	int32_t target = (int32_t)(addr - (*jit_ptr) -2);
	if((target+128) & ~0xFF) {
		*((*jit_ptr)++) = 0x0F;
		*((*jit_ptr)++) = 0x80 | op;
		*(int32_t*)(*jit_ptr) = target -4;
		(*jit_ptr) += 4;
	} else {
		*(int16_t*)(*jit_ptr) = 0x70 | op | ((int8_t)target << 8);
		(*jit_ptr) += 2;
	}
}
static __inline void _jit_cmp_r(unsigned char **jit_ptr, uint8_t reg, uint8_t reg2){
	RXX_PREFIX
	*(int16_t*)(*jit_ptr) = 0xC039 | (reg2 << 11) | (reg << 8);
	(*jit_ptr) += 2;
}
static __inline void _jit_add_i(unsigned char **jit_ptr, uint8_t reg, int32_t val){
	RXX_PREFIX
	*(int16_t*)(*jit_ptr) = 0xC081 | (reg << 8);
	(*jit_ptr) += 2;
	*(int32_t*)(*jit_ptr) = val;
	(*jit_ptr) += 4;
}
static __inline void _jit_sub_r(unsigned char **jit_ptr, uint8_t reg, uint8_t reg2){
	RXX_PREFIX
	*(int16_t*)(*jit_ptr) = 0xC029 | (reg2 << 11) | (reg << 8);
	(*jit_ptr) += 2;
}
static __inline void _jit_and_i(unsigned char **jit_ptr, uint8_t reg, int32_t val){
	RXX_PREFIX
	*(int16_t*)(*jit_ptr) = 0xE081 | (reg << 11);
	(*jit_ptr) += 2;
	*(int32_t*)(*jit_ptr) = val;
	(*jit_ptr) += 4;
}
static __inline void _jit_mov_i(unsigned char **jit_ptr, uint8_t reg, intptr_t val){
#ifdef _WIN64
	if (val > 0x3fffffff || val < 0x40000000){
		*(int16_t*)(*jit_ptr) = 0xB848 | (reg << 8);
		(*jit_ptr) += 2;
		*(int64_t*)(*jit_ptr) = val;
		(*jit_ptr) += 8;
	} else {
		*(int32_t*)(*jit_ptr) = 0xC0C748 | (reg << 16);
		(*jit_ptr) += 3;
		*(int32_t*)(*jit_ptr) = (int32_t)val;
		(*jit_ptr) += 4;
	}
#else
	*((*jit_ptr)++) = 0xB8 | reg;
	*(int32_t*)(*jit_ptr) = (int32_t)val;
	(*jit_ptr) += 4;
#endif
}
static __inline void _jit_mov_r(unsigned char **jit_ptr, uint8_t reg, uint8_t reg2){
	RXX_PREFIX
	*(int16_t*)(*jit_ptr) = 0xC089 | (reg2 << 11) | (reg << 8);
	(*jit_ptr) += 2;
}
static __inline void _jit_nop(unsigned char **jit_ptr){
	*((*jit_ptr)++) = 0x90;
}
static __inline void _jit_align32(unsigned char **jit_ptr){
	while ((intptr_t)(*jit_ptr) & 0x1F){
		_jit_nop(jit_ptr);
	}
}
static __inline void _jit_ret(unsigned char **jit_ptr){
	*((*jit_ptr)++) = 0xC3;
}

#endif	//__GF_JIT__
