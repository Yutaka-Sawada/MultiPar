void calc_table(__local uint *mtab, int id, int factor)
{
	int i, sum = 0;

	for (i = 0; i < 8; i++){
		sum = (id & (1 << i)) ? (sum ^ factor) : sum;
		factor = (factor & 0x8000) ? ((factor << 1) ^ 0x1100B) : (factor << 1);
	}
	mtab[id] = sum;

	sum = (sum << 4) ^ (((sum << 16) >> 31) & 0x88058) ^ (((sum << 17) >> 31) & 0x4402C) ^ (((sum << 18) >> 31) & 0x22016) ^ (((sum << 19) >> 31) & 0x1100B);
	sum = (sum << 4) ^ (((sum << 16) >> 31) & 0x88058) ^ (((sum << 17) >> 31) & 0x4402C) ^ (((sum << 18) >> 31) & 0x22016) ^ (((sum << 19) >> 31) & 0x1100B);

	mtab[id + 256] = sum;
}

__kernel void method0(
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
		barrier(CLK_LOCAL_MEM_FENCE);
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
		barrier(CLK_LOCAL_MEM_FENCE);
	}
}

__kernel void method3(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	int blk_num)
{
	__global uint *blk_src;
	__local uint mtab[512];
	int i, blk, chk_size, remain, pos;
	uint lo, hi, sum1, sum2;
	const int work_id = get_global_id(0) * 2;
	const int work_size = get_global_size(0) * 2;
	const int table_id = get_local_id(0);

	remain = BLK_SIZE;
	chk_size = CHK_SIZE;
	while (remain > 0){
		if (chk_size > remain)
			chk_size = remain;

		for (i = work_id; i < chk_size; i += work_size){
			dst[i    ] = 0;
			dst[i + 1] = 0;
		}

		blk_src = src;
		for (blk = 0; blk < blk_num; blk++){
			calc_table(mtab, table_id, factors[blk]);
			barrier(CLK_LOCAL_MEM_FENCE);

			for (i = work_id; i < chk_size; i += work_size){
				pos = (i & ~7) + ((i & 7) >> 1);
				lo = blk_src[pos    ];
				hi = blk_src[pos + 4];
				sum1 = mtab[(uchar)(lo >> 16)] ^ mtab[256 + (uchar)(hi >> 16)];
				sum2 = mtab[lo >> 24] ^ mtab[256 + (hi >> 24)];
				sum1 <<= 16;
				sum2 <<= 16;
				sum1 ^= mtab[(uchar)lo] ^ mtab[256 + (uchar)hi];
				sum2 ^= mtab[(uchar)(lo >> 8)] ^ mtab[256 + (uchar)(hi >> 8)];
				dst[pos    ] ^= (sum1 & 0x00FF00FF) | ((sum2 & 0x00FF00FF) << 8);
				dst[pos + 4] ^= ((sum1 & 0xFF00FF00) >> 8) | (sum2 & 0xFF00FF00);
			}
			blk_src += BLK_SIZE;
			barrier(CLK_LOCAL_MEM_FENCE);
		}

		src += CHK_SIZE;
		dst += CHK_SIZE;
		remain -= CHK_SIZE;
	}
}

__kernel void method4(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	int blk_num)
{
	__local int table[16];
	__local uint cache[256];
	int i, j, blk, pos, sht, mask;
	uint sum;
	const int work_id = get_global_id(0);
	const int work_size = get_global_size(0);

	for (i = work_id; i < BLK_SIZE; i += work_size)
		dst[i] = 0;

	for (blk = 0; blk < blk_num; blk++){
		if (get_local_id(0) == 0){
			pos = factors[blk] << 16;
			table[0] = pos;
			for (j = 1; j < 16; j++){
				pos = (pos << 1) ^ ((pos >> 31) & 0x100B0000);
				table[j] = pos;
			}
		}
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			pos = i & 255;
			cache[pos] = src[i];
			barrier(CLK_LOCAL_MEM_FENCE);

			sum = 0;
			sht = (i & 60) >> 2;
			pos &= ~60;
			for (j = 15; j >= 0; j--){
				mask = (table[j] << sht) >> 31;
				sum ^= mask & cache[pos];
				pos += 4;
			}
			dst[i] ^= sum;
			barrier(CLK_LOCAL_MEM_FENCE);
		}
		src += BLK_SIZE;
	}
}
