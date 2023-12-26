void calc_table(__local uint *mtab, int id, int factor)
{
	int i, sum, mask;

	mask = (id & 1) ? 0xFFFF : 0;
	sum = mask & factor;
	for (i = 1; i < 8; i++){
		mask = (factor & 0x8000) ? 0x1100B : 0;
		factor = (factor << 1) ^ mask;
		mask = (id & (1 << i)) ? 0xFFFF : 0;
		sum ^= mask & factor;
	}
	mtab[id] = sum;

	sum = (sum << 4) ^ (((sum << 16) >> 31) & 0x88058) ^ (((sum << 17) >> 31) & 0x4402C) ^ (((sum << 18) >> 31) & 0x22016) ^ (((sum << 19) >> 31) & 0x1100B);
	sum = (sum << 4) ^ (((sum << 16) >> 31) & 0x88058) ^ (((sum << 17) >> 31) & 0x4402C) ^ (((sum << 18) >> 31) & 0x22016) ^ (((sum << 19) >> 31) & 0x1100B);

	mtab[id + 256] = sum;
}

void calc_table2(__local uint *mtab, int id, int factor, int factor2)
{
	int i, sum, sum2, mask;

	mask = (id & 1) ? 0xFFFF : 0;
	sum = mask & factor;
	sum2 = mask & factor2;
	for (i = 1; i < 8; i++){
		mask = (factor & 0x8000) ? 0x1100B : 0;
		factor = (factor << 1) ^ mask;
		mask = (factor2 & 0x8000) ? 0x1100B : 0;
		factor2 = (factor2 << 1) ^ mask;
		mask = (id & (1 << i)) ? 0xFFFF : 0;
		sum ^= mask & factor;
		sum2 ^= mask & factor2;
	}
	mtab[id] = sum | (sum2 << 16);

	sum = (sum << 4) ^ (((sum << 16) >> 31) & 0x88058) ^ (((sum << 17) >> 31) & 0x4402C) ^ (((sum << 18) >> 31) & 0x22016) ^ (((sum << 19) >> 31) & 0x1100B);
	sum = (sum << 4) ^ (((sum << 16) >> 31) & 0x88058) ^ (((sum << 17) >> 31) & 0x4402C) ^ (((sum << 18) >> 31) & 0x22016) ^ (((sum << 19) >> 31) & 0x1100B);
	sum2 = (sum2 << 4) ^ (((sum2 << 16) >> 31) & 0x88058) ^ (((sum2 << 17) >> 31) & 0x4402C) ^ (((sum2 << 18) >> 31) & 0x22016) ^ (((sum2 << 19) >> 31) & 0x1100B);
	sum2 = (sum2 << 4) ^ (((sum2 << 16) >> 31) & 0x88058) ^ (((sum2 << 17) >> 31) & 0x4402C) ^ (((sum2 << 18) >> 31) & 0x22016) ^ (((sum2 << 19) >> 31) & 0x1100B);

	mtab[id + 256] = sum | (sum2 << 16);
}

__kernel void method1(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	int blk_num)
{
	__local uint mtab[512];
	int i, blk;
	uint v, sum;
	const int work_id = get_global_id(0);
	const int work_size = get_global_size(0);
	const int table_id = get_local_id(0);

	for (i = work_id; i < BLK_SIZE; i += work_size)
		dst[i] = 0;

	for (blk = 0; blk < blk_num; blk++){
		barrier(CLK_LOCAL_MEM_FENCE);
		calc_table(mtab, table_id, factors[blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			v = src[i];
			sum = mtab[(uchar)(v >> 16)] ^ mtab[256 + (v >> 24)];
			sum <<= 16;
			sum ^= mtab[(uchar)v] ^ mtab[256 + (uchar)(v >> 8)];
			dst[i] ^= sum;
		}
		src += BLK_SIZE;
	}
}

__kernel void method2(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	int blk_num)
{
	__local uint mtab[512];
	int i, blk, pos;
	uint lo, hi, sum1, sum2;
	const int work_id = get_global_id(0) * 2;
	const int work_size = get_global_size(0) * 2;
	const int table_id = get_local_id(0);

	for (i = work_id; i < BLK_SIZE; i += work_size){
		dst[i    ] = 0;
		dst[i + 1] = 0;
	}

	for (blk = 0; blk < blk_num; blk++){
		barrier(CLK_LOCAL_MEM_FENCE);
		calc_table(mtab, table_id, factors[blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			pos = (i & ~7) + ((i & 7) >> 1);
			lo = src[pos    ];
			hi = src[pos + 4];
			sum1 = mtab[(uchar)(lo >> 16)] ^ mtab[256 + (uchar)(hi >> 16)];
			sum2 = mtab[lo >> 24] ^ mtab[256 + (hi >> 24)];
			sum1 <<= 16;
			sum2 <<= 16;
			sum1 ^= mtab[(uchar)lo] ^ mtab[256 + (uchar)hi];
			sum2 ^= mtab[(uchar)(lo >> 8)] ^ mtab[256 + (uchar)(hi >> 8)];
			dst[pos    ] ^= (sum1 & 0x00FF00FF) | ((sum2 & 0x00FF00FF) << 8);
			dst[pos + 4] ^= ((sum1 & 0xFF00FF00) >> 8) | (sum2 & 0xFF00FF00);
		}
		src += BLK_SIZE;
	}
}

__kernel void method4(
	__global uint4 *src,
	__global uint4 *dst,
	__global ushort *factors,
	int blk_num)
{
	__local uint mtab[512];
	int i, blk;
	uchar4 r0, r1, r2, r3, r4, r5, r6, r7;
	uchar16 lo, hi;
	const int work_id = get_global_id(0) * 2;
	const int work_size = get_global_size(0) * 2;
	const int table_id = get_local_id(0);

	for (i = work_id; i < BLK_SIZE / 4; i += work_size){
		dst[i    ] = 0;
		dst[i + 1] = 0;
	}

	for (blk = 0; blk < blk_num; blk++){
		barrier(CLK_LOCAL_MEM_FENCE);
		calc_table(mtab, table_id, factors[blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE / 4; i += work_size){
			lo = as_uchar16(src[i    ]);
			hi = as_uchar16(src[i + 1]);
			r0 = (uchar4)(as_uchar2((ushort)(mtab[lo.s0] ^ mtab[256 + hi.s0])), as_uchar2((ushort)(mtab[lo.s1] ^ mtab[256 + hi.s1])));
			r1 = (uchar4)(as_uchar2((ushort)(mtab[lo.s2] ^ mtab[256 + hi.s2])), as_uchar2((ushort)(mtab[lo.s3] ^ mtab[256 + hi.s3])));
			r2 = (uchar4)(as_uchar2((ushort)(mtab[lo.s4] ^ mtab[256 + hi.s4])), as_uchar2((ushort)(mtab[lo.s5] ^ mtab[256 + hi.s5])));
			r3 = (uchar4)(as_uchar2((ushort)(mtab[lo.s6] ^ mtab[256 + hi.s6])), as_uchar2((ushort)(mtab[lo.s7] ^ mtab[256 + hi.s7])));
			r4 = (uchar4)(as_uchar2((ushort)(mtab[lo.s8] ^ mtab[256 + hi.s8])), as_uchar2((ushort)(mtab[lo.s9] ^ mtab[256 + hi.s9])));
			r5 = (uchar4)(as_uchar2((ushort)(mtab[lo.sa] ^ mtab[256 + hi.sa])), as_uchar2((ushort)(mtab[lo.sb] ^ mtab[256 + hi.sb])));
			r6 = (uchar4)(as_uchar2((ushort)(mtab[lo.sc] ^ mtab[256 + hi.sc])), as_uchar2((ushort)(mtab[lo.sd] ^ mtab[256 + hi.sd])));
			r7 = (uchar4)(as_uchar2((ushort)(mtab[lo.se] ^ mtab[256 + hi.se])), as_uchar2((ushort)(mtab[lo.sf] ^ mtab[256 + hi.sf])));
			dst[i    ] ^= as_uint4((uchar16)(r0.x, r0.z, r1.x, r1.z, r2.x, r2.z, r3.x, r3.z, r4.x, r4.z, r5.x, r5.z, r6.x, r6.z, r7.x, r7.z));
			dst[i + 1] ^= as_uint4((uchar16)(r0.y, r0.w, r1.y, r1.w, r2.y, r2.w, r3.y, r3.w, r4.y, r4.w, r5.y, r5.w, r6.y, r6.w, r7.y, r7.w));
		}
		src += BLK_SIZE / 4;
	}
}

__kernel void method9(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	int blk_num)
{
	__local uint mtab[512];
	int i, blk;
	uint v, sum, sum2;
	const int work_id = get_global_id(0);
	const int work_size = get_global_size(0);
	const int table_id = get_local_id(0);

	for (i = work_id; i < BLK_SIZE; i += work_size){
		dst[i] = 0;
		dst[i + BLK_SIZE] = 0;
	}

	for (blk = 0; blk < blk_num; blk++){
		barrier(CLK_LOCAL_MEM_FENCE);
		calc_table2(mtab, table_id, factors[blk], factors[blk_num + blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			v = src[i];
			sum  = mtab[(uchar)v] ^ mtab[256 + (uchar)(v >> 8)];
			sum2 = mtab[(uchar)(v >> 16)] ^ mtab[256 + (v >> 24)];
			dst[i] ^= (sum & 0xFFFF) | (sum2 << 16);
			dst[i + BLK_SIZE] ^= (sum >> 16) | (sum2 & 0xFFFF0000);
		}
		src += BLK_SIZE;
	}
}

__kernel void method10(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	int blk_num)
{
	__local uint mtab[512];
	int i, blk, pos;
	uint lo, hi, sum1, sum2, sum3, sum4;
	const int work_id = get_global_id(0) * 2;
	const int work_size = get_global_size(0) * 2;
	const int table_id = get_local_id(0);

	for (i = work_id; i < BLK_SIZE; i += work_size){
		dst[i    ] = 0;
		dst[i + 1] = 0;
		dst[i + BLK_SIZE    ] = 0;
		dst[i + BLK_SIZE + 1] = 0;
	}

	for (blk = 0; blk < blk_num; blk++){
		barrier(CLK_LOCAL_MEM_FENCE);
		calc_table2(mtab, table_id, factors[blk], factors[blk_num + blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			pos = (i & ~7) + ((i & 7) >> 1);
			lo = src[pos    ];
			hi = src[pos + 4];
			sum1 = mtab[(uchar)lo] ^ mtab[256 + (uchar)hi];
			sum2 = mtab[(uchar)(lo >> 8)] ^ mtab[256 + (uchar)(hi >> 8)];
			sum3 = mtab[(uchar)(lo >> 16)] ^ mtab[256 + (uchar)(hi >> 16)];
			sum4 = mtab[lo >> 24] ^ mtab[256 + (hi >> 24)];
			dst[pos    ] ^= (sum1 & 0xFF) | ((sum2 & 0xFF) << 8) | ((sum3 & 0xFF) << 16) | (sum4 << 24);
			dst[pos + 4] ^= ((sum1 >> 8) & 0xFF) | (sum2 & 0xFF00) | ((sum3 & 0xFF00) << 8) | ((sum4 & 0xFF00) << 16);
			dst[pos + BLK_SIZE    ] ^= ((sum1 >> 16) & 0xFF) | ((sum2 >> 8) & 0xFF00) | (sum3 & 0xFF0000) | ((sum4 & 0xFF0000) << 8);
			dst[pos + BLK_SIZE + 4] ^= (sum1 >> 24) | ((sum2 >> 16) & 0xFF00) | ((sum3 >> 8) & 0xFF0000) | (sum4 & 0xFF000000);
		}
		src += BLK_SIZE;
	}
}

__kernel void method12(
	__global uint4 *src,
	__global uint4 *dst,
	__global ushort *factors,
	int blk_num)
{
	__local uint mtab[512];
	int i, blk;
	uchar4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, rA, rB, rC, rD, rE, rF;
	uchar16 lo, hi;
	const int work_id = get_global_id(0) * 2;
	const int work_size = get_global_size(0) * 2;
	const int table_id = get_local_id(0);

	for (i = work_id; i < BLK_SIZE / 4; i += work_size){
		dst[i    ] = 0;
		dst[i + 1] = 0;
		dst[i + BLK_SIZE / 4    ] = 0;
		dst[i + BLK_SIZE / 4 + 1] = 0;
	}

	for (blk = 0; blk < blk_num; blk++){
		barrier(CLK_LOCAL_MEM_FENCE);
		calc_table2(mtab, table_id, factors[blk], factors[blk_num + blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE / 4; i += work_size){
			lo = as_uchar16(src[i    ]);
			hi = as_uchar16(src[i + 1]);
			r0 = as_uchar4(mtab[lo.s0] ^ mtab[256 + hi.s0]);
			r1 = as_uchar4(mtab[lo.s1] ^ mtab[256 + hi.s1]);
			r2 = as_uchar4(mtab[lo.s2] ^ mtab[256 + hi.s2]);
			r3 = as_uchar4(mtab[lo.s3] ^ mtab[256 + hi.s3]);
			r4 = as_uchar4(mtab[lo.s4] ^ mtab[256 + hi.s4]);
			r5 = as_uchar4(mtab[lo.s5] ^ mtab[256 + hi.s5]);
			r6 = as_uchar4(mtab[lo.s6] ^ mtab[256 + hi.s6]);
			r7 = as_uchar4(mtab[lo.s7] ^ mtab[256 + hi.s7]);
			r8 = as_uchar4(mtab[lo.s8] ^ mtab[256 + hi.s8]);
			r9 = as_uchar4(mtab[lo.s9] ^ mtab[256 + hi.s9]);
			rA = as_uchar4(mtab[lo.sa] ^ mtab[256 + hi.sa]);
			rB = as_uchar4(mtab[lo.sb] ^ mtab[256 + hi.sb]);
			rC = as_uchar4(mtab[lo.sc] ^ mtab[256 + hi.sc]);
			rD = as_uchar4(mtab[lo.sd] ^ mtab[256 + hi.sd]);
			rE = as_uchar4(mtab[lo.se] ^ mtab[256 + hi.se]);
			rF = as_uchar4(mtab[lo.sf] ^ mtab[256 + hi.sf]);
			dst[i    ] ^= as_uint4((uchar16)(r0.x, r1.x, r2.x, r3.x, r4.x, r5.x, r6.x, r7.x, r8.x, r9.x, rA.x, rB.x, rC.x, rD.x, rE.x, rF.x));
			dst[i + 1] ^= as_uint4((uchar16)(r0.y, r1.y, r2.y, r3.y, r4.y, r5.y, r6.y, r7.y, r8.y, r9.y, rA.y, rB.y, rC.y, rD.y, rE.y, rF.y));
			dst[i + BLK_SIZE / 4    ] ^= as_uint4((uchar16)(r0.z, r1.z, r2.z, r3.z, r4.z, r5.z, r6.z, r7.z, r8.z, r9.z, rA.z, rB.z, rC.z, rD.z, rE.z, rF.z));
			dst[i + BLK_SIZE / 4 + 1] ^= as_uint4((uchar16)(r0.w, r1.w, r2.w, r3.w, r4.w, r5.w, r6.w, r7.w, r8.w, r9.w, rA.w, rB.w, rC.w, rD.w, rE.w, rF.w));
		}
		src += BLK_SIZE / 4;
	}
}

__kernel void method16(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	int blk_num)
{
	__local int table[16];
	__local uint cache[256];
	int i, j, blk, pos, mask, tmp;
	uint sum;
	const int work_id = get_global_id(0);
	const int work_size = get_global_size(0);

	for (i = work_id; i < BLK_SIZE; i += work_size)
		dst[i] = 0;

	for (blk = 0; blk < blk_num; blk++){
		if (get_local_id(0) == 0){
			tmp = factors[blk];
			table[0] = tmp;
			for (j = 1; j < 16; j++){
				mask = (tmp & 0x8000) ? 0x1100B : 0;
				tmp = (tmp << 1) ^ mask;
				table[j] = tmp;
			}
		}
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			pos = i & 255;
			cache[pos] = src[i];
			barrier(CLK_LOCAL_MEM_FENCE);

			sum = 0;
			tmp = (i & 60) >> 2;
			tmp = 0x8000 >> tmp;
			pos &= ~60;
			for (j = 15; j >= 0; j--){
				mask = (table[j] & tmp) ? 0xFFFFFFFF : 0;
				sum ^= mask & cache[pos];
				pos += 4;
			}
			dst[i] ^= sum;
			barrier(CLK_LOCAL_MEM_FENCE);
		}
		src += BLK_SIZE;
	}
}
