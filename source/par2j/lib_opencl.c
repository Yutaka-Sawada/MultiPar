// lib_opencl.c
// Copyright : 2024-01-21 Yutaka Sawada
// License : GPL

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600	// Windows Vista or later
#endif

#include <stdio.h>

#include <windows.h>
#include <opencl.h>

#include "gf16.h"
#include "lib_opencl.h"

//#define DEBUG_OUTPUT // 実験用

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 関数の定義

// Platform API
typedef cl_int (CL_API_CALL *API_clGetPlatformIDs)(cl_uint, cl_platform_id *, cl_uint *);
typedef cl_int (CL_API_CALL *API_clGetPlatformInfo)(cl_platform_id, cl_platform_info, size_t, void *, size_t *);

// Device APIs
typedef cl_int (CL_API_CALL *API_clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
typedef cl_int (CL_API_CALL *API_clGetDeviceInfo)(cl_device_id, cl_device_info, size_t, void *, size_t *);

// Context APIs
typedef cl_context (CL_API_CALL *API_clCreateContext)(const cl_context_properties *, cl_uint, const cl_device_id *, void (CL_CALLBACK *)(const char *, const void *, size_t, void *), void *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseContext)(cl_context);
typedef cl_int (CL_API_CALL *API_clGetContextInfo)(cl_context, cl_context_info, size_t, void *, size_t *);

// Command Queue APIs
typedef cl_command_queue (CL_API_CALL *API_clCreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseCommandQueue)(cl_command_queue);

// Memory Object APIs
typedef cl_mem (CL_API_CALL *API_clCreateBuffer)(cl_context, cl_mem_flags, size_t, void *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseMemObject)(cl_mem);

// Program Object APIs
typedef cl_program (CL_API_CALL *API_clCreateProgramWithSource)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseProgram)(cl_program);
typedef cl_int (CL_API_CALL *API_clBuildProgram)(cl_program, cl_uint, const cl_device_id *,const char *, void (CL_CALLBACK *)(cl_program, void *), void *);
typedef cl_int (CL_API_CALL *API_clUnloadCompiler)(void);
typedef cl_int (CL_API_CALL *API_clUnloadPlatformCompiler)(cl_platform_id);	// OpenCL 1.2 で追加された
typedef cl_int (CL_API_CALL *API_clGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void *, size_t *);

// Kernel Object APIs
typedef cl_kernel (CL_API_CALL *API_clCreateKernel)(cl_program, const char *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseKernel)(cl_kernel);
typedef cl_int (CL_API_CALL *API_clSetKernelArg)(cl_kernel, cl_uint, size_t, const void *);
typedef cl_int (CL_API_CALL *API_clGetKernelWorkGroupInfo)(cl_kernel, cl_device_id, cl_kernel_work_group_info, size_t, void *, size_t *);

// Event Object APIs
typedef cl_int (CL_API_CALL *API_clReleaseEvent)(cl_event);
typedef cl_int (CL_API_CALL *API_clGetEventInfo)(cl_event, cl_event_info, size_t, void *, size_t *);

// Flush and Finish APIs
typedef cl_int (CL_API_CALL *API_clFlush)(cl_command_queue);
typedef cl_int (CL_API_CALL *API_clFinish)(cl_command_queue);

// Enqueued Commands APIs
typedef cl_int (CL_API_CALL *API_clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (CL_API_CALL *API_clEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void *, cl_uint, const cl_event *, cl_event *);
typedef void * (CL_API_CALL *API_clEnqueueMapBuffer)(cl_command_queue, cl_mem, cl_bool, cl_map_flags, size_t, size_t, cl_uint, const cl_event *, cl_event *, cl_int *);
typedef cl_int (CL_API_CALL *API_clEnqueueUnmapMemObject)(cl_command_queue, cl_mem, void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (CL_API_CALL *API_clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t *, const size_t *, const size_t *, cl_uint, const cl_event *, cl_event *);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// グローバル変数

extern unsigned int cpu_flag;	// declared in common2.h
extern int cpu_num;

#define MAX_DEVICE 8

HMODULE hLibOpenCL = NULL;

cl_context OpenCL_context = NULL;
cl_command_queue OpenCL_command = NULL;
cl_kernel OpenCL_kernel = NULL;
cl_mem OpenCL_src = NULL, OpenCL_dst = NULL, OpenCL_buf = NULL;
size_t OpenCL_group_num;
int OpenCL_method = 0;	// 標準では GPU を使わず、動作は自動選択される

API_clCreateBuffer gfn_clCreateBuffer;
API_clReleaseMemObject gfn_clReleaseMemObject;
API_clSetKernelArg gfn_clSetKernelArg;
API_clFinish gfn_clFinish;
API_clEnqueueReadBuffer gfn_clEnqueueReadBuffer;
API_clEnqueueWriteBuffer gfn_clEnqueueWriteBuffer;
API_clEnqueueMapBuffer gfn_clEnqueueMapBuffer;
API_clEnqueueUnmapMemObject gfn_clEnqueueUnmapMemObject;
API_clEnqueueNDRangeKernel gfn_clEnqueueNDRangeKernel;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
入力
OpenCL_method : どのデバイスや関数を選ぶか
    0x100 = 速い機器を選ぶ,          0x200 = 遅い機器を選ぶ
  0x10000 = 1ブロックずつ計算する, 0x20000 = 2ブロックずつ計算しようとする
  0x40000 = 4-byte memory access,  0x80000 = try 16-byte memory access
 0x100000 = CL_MEM_COPY_HOST_PTR, 0x200000 = CL_MEM_USE_HOST_PTR
unit_size : ブロックの単位サイズ
src_max : ソース・ブロック個数

出力
return : エラー番号
src_max : 最大で何ブロックまでソースを読み込めるか
OpenCL_method : 動作フラグいろいろ
*/

// 0=成功, 1～エラー番号
int init_OpenCL(unsigned int unit_size, int *src_max)
{
	char buf[2048], *p_source;
	int err = 0, i, j;
	int gpu_power, count;
	int unified_memory;	// non zero = Integrated GPU
	size_t data_size, alloc_max;
	//FILE *fp;
	HRSRC res;
	HGLOBAL glob;
	API_clGetPlatformIDs fn_clGetPlatformIDs;
#ifdef DEBUG_OUTPUT
	API_clGetPlatformInfo fn_clGetPlatformInfo;
#endif
	API_clGetDeviceIDs fn_clGetDeviceIDs;
	API_clGetDeviceInfo fn_clGetDeviceInfo;
	API_clCreateContext fn_clCreateContext;
	API_clCreateCommandQueue fn_clCreateCommandQueue;
	API_clCreateProgramWithSource fn_clCreateProgramWithSource;
	API_clBuildProgram fn_clBuildProgram;
	API_clUnloadCompiler fn_clUnloadCompiler;
	API_clUnloadPlatformCompiler fn_clUnloadPlatformCompiler;
	API_clGetProgramBuildInfo fn_clGetProgramBuildInfo;
	API_clReleaseProgram fn_clReleaseProgram;
	API_clCreateKernel fn_clCreateKernel;
	API_clGetKernelWorkGroupInfo fn_clGetKernelWorkGroupInfo;
	API_clReleaseKernel fn_clReleaseKernel;
	cl_int ret;
	cl_uint num_platforms = 0, num_devices = 0, num_groups, param_value;
	cl_ulong param_value8;
	cl_platform_id platform_id[MAX_DEVICE], selected_platform;	// Intel, AMD, Nvidia などドライバーの提供元
	cl_device_id device_id[MAX_DEVICE], selected_device;	// CPU や GPU など
	cl_program program;

	// ライブラリーを読み込む
	hLibOpenCL = LoadLibraryA("OpenCL.DLL");
	if (hLibOpenCL == NULL)
		return 1;

	// 関数のエントリー取得
	err = 2;
	fn_clGetPlatformIDs = (API_clGetPlatformIDs)GetProcAddress(hLibOpenCL, "clGetPlatformIDs");
	if (fn_clGetPlatformIDs == NULL)
		return err;
#ifdef DEBUG_OUTPUT
	fn_clGetPlatformInfo = (API_clGetPlatformInfo)GetProcAddress(hLibOpenCL, "clGetPlatformInfo");
	if (fn_clGetPlatformInfo == NULL)
		return err;
#endif
	fn_clGetDeviceIDs = (API_clGetDeviceIDs)GetProcAddress(hLibOpenCL, "clGetDeviceIDs");
	if (fn_clGetDeviceIDs == NULL)
		return err;
	fn_clGetDeviceInfo = (API_clGetDeviceInfo)GetProcAddress(hLibOpenCL, "clGetDeviceInfo");
	if (fn_clGetDeviceInfo == NULL)
		return err;
	fn_clCreateContext = (API_clCreateContext)GetProcAddress(hLibOpenCL, "clCreateContext");
	if (fn_clCreateContext == NULL)
		return err;
	fn_clCreateCommandQueue = (API_clCreateCommandQueue)GetProcAddress(hLibOpenCL, "clCreateCommandQueue");
	if (fn_clCreateCommandQueue == NULL)
		return err;
	gfn_clCreateBuffer = (API_clCreateBuffer)GetProcAddress(hLibOpenCL, "clCreateBuffer");
	if (gfn_clCreateBuffer == NULL)
		return err;
	gfn_clReleaseMemObject = (API_clReleaseMemObject)GetProcAddress(hLibOpenCL, "clReleaseMemObject");
	if (gfn_clReleaseMemObject == NULL)
		return err;
	gfn_clEnqueueReadBuffer = (API_clEnqueueReadBuffer)GetProcAddress(hLibOpenCL, "clEnqueueReadBuffer");
	if (gfn_clEnqueueReadBuffer == NULL)
		return err;
	gfn_clEnqueueWriteBuffer = (API_clEnqueueWriteBuffer)GetProcAddress(hLibOpenCL, "clEnqueueWriteBuffer");
	if (gfn_clEnqueueWriteBuffer == NULL)
		return err;
	gfn_clEnqueueMapBuffer = (API_clEnqueueMapBuffer)GetProcAddress(hLibOpenCL, "clEnqueueMapBuffer");
	if (gfn_clEnqueueMapBuffer == NULL)
		return err;
	gfn_clEnqueueUnmapMemObject = (API_clEnqueueUnmapMemObject)GetProcAddress(hLibOpenCL, "clEnqueueUnmapMemObject");
	if (gfn_clEnqueueUnmapMemObject == NULL)
		return err;
	fn_clCreateProgramWithSource = (API_clCreateProgramWithSource)GetProcAddress(hLibOpenCL, "clCreateProgramWithSource");
	if (fn_clCreateProgramWithSource == NULL)
		return err;
	fn_clBuildProgram = (API_clBuildProgram)GetProcAddress(hLibOpenCL, "clBuildProgram");
	if (fn_clBuildProgram == NULL)
		return err;
	fn_clGetProgramBuildInfo = (API_clGetProgramBuildInfo)GetProcAddress(hLibOpenCL, "clGetProgramBuildInfo");
	if (fn_clGetProgramBuildInfo == NULL)
		return err;
	fn_clReleaseProgram = (API_clReleaseProgram)GetProcAddress(hLibOpenCL, "clReleaseProgram");
	if (fn_clReleaseProgram == NULL)
		return err;
	fn_clUnloadPlatformCompiler = (API_clUnloadPlatformCompiler)GetProcAddress(hLibOpenCL, "clUnloadPlatformCompiler");
	if (fn_clUnloadPlatformCompiler == NULL){	// OpenCL 1.1 なら clUnloadCompiler を使う
		fn_clUnloadCompiler = (API_clUnloadCompiler)GetProcAddress(hLibOpenCL, "clUnloadCompiler");
		if (fn_clUnloadCompiler == NULL)
			return err;
	}
	fn_clCreateKernel = (API_clCreateKernel)GetProcAddress(hLibOpenCL, "clCreateKernel");
	if (fn_clCreateKernel == NULL)
		return err;
	gfn_clSetKernelArg = (API_clSetKernelArg)GetProcAddress(hLibOpenCL, "clSetKernelArg");
	if (gfn_clSetKernelArg == NULL)
		return err;
	fn_clGetKernelWorkGroupInfo = (API_clGetKernelWorkGroupInfo)GetProcAddress(hLibOpenCL, "clGetKernelWorkGroupInfo");
	if (fn_clGetKernelWorkGroupInfo == NULL)
		return err;
	fn_clReleaseKernel = (API_clReleaseKernel)GetProcAddress(hLibOpenCL, "clReleaseKernel");
	if (fn_clReleaseKernel == NULL)
		return err;
	gfn_clFinish = (API_clFinish)GetProcAddress(hLibOpenCL, "clFinish");
	if (gfn_clFinish == NULL)
		return err;
	gfn_clEnqueueNDRangeKernel = (API_clEnqueueNDRangeKernel)GetProcAddress(hLibOpenCL, "clEnqueueNDRangeKernel");
	if (gfn_clEnqueueNDRangeKernel == NULL)
		return err;

	// OpenCL 環境の数
	ret = fn_clGetPlatformIDs(MAX_DEVICE, platform_id, &num_platforms);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 10;
	if (num_platforms > MAX_DEVICE)
		num_platforms = MAX_DEVICE;
	if (OpenCL_method & 0x200){	// 選択する順序と初期値を変える
		gpu_power = INT_MIN;
	} else {
		gpu_power = 0;
	}
	alloc_max = 0;

	for (i = 0; i < (int)num_platforms; i++){
#ifdef DEBUG_OUTPUT
		// 環境の情報表示
		if (fn_clGetPlatformInfo(platform_id[i], CL_PLATFORM_NAME, sizeof(buf), buf, NULL) == CL_SUCCESS)
			printf("\nPlatform[%d] = %s\n", i, buf);
		if (fn_clGetPlatformInfo(platform_id[i], CL_PLATFORM_VERSION, sizeof(buf), buf, NULL) == CL_SUCCESS)
			printf("Platform version = %s\n", buf);
#endif

		// 環境内の OpenCL 対応機器の数
		if (fn_clGetDeviceIDs(platform_id[i], CL_DEVICE_TYPE_GPU, MAX_DEVICE, device_id, &num_devices) != CL_SUCCESS)
			continue;
		if (num_devices > MAX_DEVICE)
			num_devices = MAX_DEVICE;

		for (j = 0; j < (int)num_devices; j++){
			// デバイスが利用可能か確かめる
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_AVAILABLE, sizeof(cl_uint), &param_value, NULL);
			if ((ret != CL_SUCCESS) || (param_value == CL_FALSE))
				continue;
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_COMPILER_AVAILABLE, sizeof(cl_uint), &param_value, NULL);
			if ((ret != CL_SUCCESS) || (param_value == CL_FALSE))
				continue;

#ifdef DEBUG_OUTPUT
			// 機器の情報表示
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_NAME, sizeof(buf), buf, NULL);
			if (ret == CL_SUCCESS)
				printf("\nDevice[%d] = %s\n", j, buf);
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_VERSION, sizeof(buf), buf, NULL);
			if (ret == CL_SUCCESS)
				printf("Device version = %s\n", buf);
#endif

			// 取得できなくてもエラーにしない
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl_uint), &param_value, NULL);
			if (ret == CL_SUCCESS){
				if (param_value != 0){
#ifdef DEBUG_OUTPUT
					printf("HOST_UNIFIED_MEMORY = %d\n", param_value);
#endif
					param_value = 1;
				}
			} else {	// CL_DEVICE_HOST_UNIFIED_MEMORY は OpenCL 2.0 以降で非推奨になった
				param_value = 0;
			}

			// 取得できない場合はエラーにする
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(cl_ulong), &param_value8, NULL);
			if (ret != CL_SUCCESS)
				continue;
#ifdef DEBUG_OUTPUT
			printf("MAX_MEM_ALLOC_SIZE = %I64d MB\n", param_value8 >> 20);
#endif
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &num_groups, NULL);
			if (ret != CL_SUCCESS)
				continue;
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &data_size, NULL);
			if (ret != CL_SUCCESS)
				continue;
#ifdef DEBUG_OUTPUT
			printf("MAX_COMPUTE_UNITS = %d\n", num_groups);
			printf("MAX_WORK_GROUP_SIZE = %zd\n", data_size);
#endif
			// MAX_COMPUTE_UNITS * MAX_WORK_GROUP_SIZE で計算力を測る、外付けGPUなら値を倍にする
			count = (2 - param_value) * (int)data_size * num_groups;
			if (OpenCL_method & 0x200)	// Prefer slower device
				count *= -1;	// 符号を変える
			//printf("prev = %d, now = %d\n", gpu_power, count);
			if ((count > gpu_power) && (data_size >= 256) &&	// 256以上ないとテーブルを作れない
					(param_value8 / 8 > (cl_ulong)unit_size)){	// CL_DEVICE_MAX_MEM_ALLOC_SIZE に収まるか
				gpu_power = count;
				selected_device = device_id[j];	// 使うデバイスの ID
				selected_platform = platform_id[i];
				OpenCL_group_num = num_groups;	// ワークグループ数は COMPUTE_UNITS 数にする
				alloc_max = (size_t)param_value8;
				unified_memory = param_value;	// 0 = discrete GPU, 1 = integrated GPU

				// AMD や Intel の GPU ではメモリー領域が全体の 1/4 とは限らない
				ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(cl_ulong), &param_value8, NULL);
				if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
					printf("GLOBAL_MEM_SIZE = %I64d MB\n", param_value8 >> 20);
#endif
					// 領域一個あたりのサイズは全体の 1/4 までにする（VRAMを使いすぎると不安定になる）
					param_value8 /= 4;
					if ((cl_ulong)alloc_max > param_value8)
						alloc_max = (size_t)param_value8;
				}
			}
		}
	}

	if (alloc_max == 0){
#ifdef DEBUG_OUTPUT
		printf("\nAvailable GPU device was not found.\n");
#endif
		return 3;
	}

#ifdef DEBUG_OUTPUT
	// デバイスの情報表示
	ret = fn_clGetPlatformInfo(selected_platform, CL_PLATFORM_NAME, sizeof(buf), buf, NULL);
	if (ret == CL_SUCCESS)
		printf("\nSelected platform = %s\n", buf);
	ret = fn_clGetDeviceInfo(selected_device, CL_DEVICE_NAME, sizeof(buf), buf, NULL);
	if (ret == CL_SUCCESS)
		printf("Selected device = %s\n", buf);
#endif

	// OpenCL 利用環境を作成する
	OpenCL_context = fn_clCreateContext(NULL, 1, &selected_device, NULL, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 11;
	OpenCL_command = fn_clCreateCommandQueue(OpenCL_context, selected_device, 0, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 12;

	// 最大で何ブロック分のメモリー領域を保持できるのか（ここではまだ確保しない）
	// 後で実際に確保する量はこれよりも少なくなる
	count = (int)(alloc_max / unit_size);	// 確保できるメモリー量から逆算する
	if (*src_max > count)
		*src_max = count;	// 指定されたソース・ブロックの個数が無理なら減らす

#ifdef DEBUG_OUTPUT
	data_size = (size_t)unit_size * count;
	printf("src buf : %zd KB (%d blocks), possible\n", data_size >> 10, count);
#endif

/*
	// テキスト形式の OpenCL C ソース・コードを読み込む
	err = 4;
	fp = fopen("source.cl", "r");
	if (fp == NULL){
		printf("cannot open source code file\n");
		return err;
	}
	ret = fseek(fp, 0, SEEK_END);
	if (ret != 0){
		printf("cannot read source code file\n");
		fclose(fp);
		return err;
	}
	data_size = ftell(fp);
	ret = fseek(fp, 0, SEEK_SET);
	if (ret != 0){
		printf("cannot read source code file\n");
		fclose(fp);
		return err;
	}
	if (data_size > 102400){ // 100 KB まで
		fclose(fp);
		printf("source code file is too large\n");
		return err;
	}
	p_source = (char *)malloc(data_size + 1);
	if (p_source == NULL){
		fclose(fp);
		printf("malloc error\n");
		return err;
	}
	data_size = fread(p_source, 1, data_size, fp);
	fclose(fp);
	printf("Source code length = %d characters\n", data_size);
	p_source[data_size] = 0;	// 末尾を null 文字にする

	// プログラムを作成する
	program = fn_clCreateProgramWithSource(OpenCL_context, 1, (char **)&p_source, NULL, &ret);
	if (ret != CL_SUCCESS){
		free(p_source);
		return (ret << 8) | 20;
	}
	free(p_source);	// もうテキストは要らないので開放しておく
*/

	// リソースから OpenCL C ソース・コードを読み込む
	err = 4;
	// Referred to "Embedding OpenCL Kernel Files in the Application on Windows"
	res = FindResource(NULL, L"#1", L"RT_STRING");	// find the resource
	if (res == NULL){
#ifdef DEBUG_OUTPUT
		printf("cannot find resource\n");
#endif
		return err;
	}
	glob = LoadResource(NULL, res);	// load the resource.
	if (glob == NULL){
#ifdef DEBUG_OUTPUT
		printf("cannot load resource\n");
#endif
		return err;
	}
	p_source = (char *)LockResource(glob);	// lock the resource to get a char*
	if (p_source == NULL){
#ifdef DEBUG_OUTPUT
		printf("cannot lock resource\n");
#endif
		return err;
	}
	data_size = SizeofResource(NULL, res);
	if (data_size == 0){
#ifdef DEBUG_OUTPUT
		printf("cannot get size of resource\n");
#endif
		return err;
	}
	//printf("OpenCL source code length = %zd characters\n", data_size);

	// プログラムを作成する
	program = fn_clCreateProgramWithSource(OpenCL_context, 1, (char **)&p_source, &data_size, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 20;
	FreeResource(glob);	// not required ?

	// 定数を指定する
	wsprintfA(buf, "-cl-fast-relaxed-math -D BLK_SIZE=%d", unit_size / 4);

	// 使用する OpenCL デバイス用にコンパイルする
	ret = fn_clBuildProgram(program, 1, &selected_device, buf, NULL, NULL);
	if (ret != CL_SUCCESS){
#ifdef DEBUG_OUTPUT
		buf[0] = 0;
		printf("clBuildProgram : Failed\n");
		if (fn_clGetProgramBuildInfo(program, selected_device, CL_PROGRAM_BUILD_LOG, sizeof(buf), buf, NULL) == CL_SUCCESS)
			printf("%s\n", buf);
#endif
		return (ret << 8) | 21;
	}

	// 計算方式を選択する
	if ((((cpu_flag & 0x101) == 1) || ((cpu_flag & 0x110) == 0x10)) && (sse_unit == 32)){
		int select_method;	// SSSE3 & ALTMAP または AVX2 ならデータの並び替え対応版を使う
		if (OpenCL_method & 0x80000){	// 16-byte and 2 blocks
			select_method = 12;
		} else if (OpenCL_method & 0x40000){	// 4-byte and 2 blocks
			select_method = 10;
		} else if (OpenCL_method & 0x20000){	// 16-byte
			select_method = 4;
		} else if (OpenCL_method & 0x10000){	// 4-byte
			select_method = 2;
		} else {	// kernel を作って詳細を確かめる
			size_t item2, item4, item10, item12;
			cl_kernel kernel2, kernel4, kernel10, kernel12;
			item2 = item4 = item10 = item12 = 0;
			// まずは一番重くて速い奴を調べる
			wsprintfA(buf, "method%d", 12);
			kernel12 = fn_clCreateKernel(program, buf, &ret);
			if (ret == CL_SUCCESS){
				ret = fn_clGetKernelWorkGroupInfo(kernel12, selected_device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &item12, NULL);
				if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
					printf("\nTesting %s\n", buf);
					printf("PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %zu\n", item12);
#endif
				}
			}
			if (item12 >= 32){	// 32以上あれば余裕で動くとみなす
				select_method = 12;
				OpenCL_kernel = kernel12;
#ifdef DEBUG_OUTPUT
				printf("\nSelected method%d\n", select_method);
#endif
			} else {	// 他の奴と比較する
				wsprintfA(buf, "method%d", 2);
				kernel2 = fn_clCreateKernel(program, buf, &ret);
				if (ret == CL_SUCCESS){
					ret = fn_clGetKernelWorkGroupInfo(kernel2, selected_device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &item2, NULL);
					if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
						printf("\nTesting %s\n", buf);
						printf("PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %zu\n", item2);
#endif
					}
				}
				if (item12 >= item2){
					select_method = 12;
					OpenCL_kernel = kernel12;
					ret = fn_clReleaseKernel(kernel2);
#ifdef DEBUG_OUTPUT
					if (ret != CL_SUCCESS)
						printf("clReleaseKernel : Failed\n");
					printf("\nSelected method%d\n", select_method);
#endif
				} else {
					ret = fn_clReleaseKernel(kernel12);
#ifdef DEBUG_OUTPUT
					if (ret != CL_SUCCESS)
						printf("clReleaseKernel : Failed\n");
#endif
					wsprintfA(buf, "method%d", 10);
					kernel10 = fn_clCreateKernel(program, buf, &ret);
					if (ret == CL_SUCCESS){
						ret = fn_clGetKernelWorkGroupInfo(kernel10, selected_device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &item10, NULL);
						if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
							printf("\nTesting %s\n", buf);
							printf("PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %zu\n", item10);
#endif
						}
					}
					if (item10 >= item2){
						select_method = 10;
						OpenCL_kernel = kernel10;
						ret = fn_clReleaseKernel(kernel2);
#ifdef DEBUG_OUTPUT
						if (ret != CL_SUCCESS)
							printf("clReleaseKernel : Failed\n");
						printf("\nSelected method%d\n", select_method);
#endif
					} else {
						wsprintfA(buf, "method%d", 4);
						kernel4 = fn_clCreateKernel(program, buf, &ret);
						if (ret == CL_SUCCESS){
							ret = fn_clGetKernelWorkGroupInfo(kernel4, selected_device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &item4, NULL);
							if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
								printf("\nTesting %s\n", buf);
								printf("PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %zu\n", item4);
#endif
							}
						}
						if (item4 >= item2){
							select_method = 4;
							OpenCL_kernel = kernel4;
							ret = fn_clReleaseKernel(kernel2);
#ifdef DEBUG_OUTPUT
							if (ret != CL_SUCCESS)
								printf("clReleaseKernel : Failed\n");
							printf("\nSelected method%d\n", select_method);
#endif
						} else {
							select_method = 2;
							OpenCL_kernel = kernel2;
							ret = fn_clReleaseKernel(kernel4);
#ifdef DEBUG_OUTPUT
							if (ret != CL_SUCCESS)
								printf("clReleaseKernel : Failed\n");
							printf("\nSelected method%d\n", select_method);
#endif
						}
					}
				}
			}
		}
		OpenCL_method |= select_method;
	} else if (((cpu_flag & 128) != 0) && (sse_unit == 256)){
		OpenCL_method |= 16;	// JIT(SSE2) は bit ごとに上位から 16バイトずつ並ぶ
		// ローカルのテーブルサイズが異なることに注意
		// XOR 方式以外は 2KB (4バイト * 256項目 * 2個) 使う
		// XOR (JIT) は 64バイト (4バイト * 16項目) 使う
	} else {
		int select_method;	// 並び替えられてないデータ用
		if (OpenCL_method & 0x40000){	// 4-byte and 2 blocks
			select_method = 9;
		} else if (OpenCL_method & 0x10000){	// 4-byte
			select_method = 1;
		} else {	// kernel を作って詳細を確かめる
			size_t item1, item9;
			cl_kernel kernel1, kernel9;
			item1 = item9 = 0;
			// まずは一番重くて速い奴を調べる
			wsprintfA(buf, "method%d", 9);
			kernel9 = fn_clCreateKernel(program, buf, &ret);
			if (ret == CL_SUCCESS){
				ret = fn_clGetKernelWorkGroupInfo(kernel9, selected_device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &item9, NULL);
				if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
					printf("\nTesting %s\n", buf);
					printf("PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %zu\n", item9);
#endif
				}
			}
			if (item9 >= 32){	// 32以上あれば余裕で動くとみなす
				select_method = 9;
				OpenCL_kernel = kernel9;
#ifdef DEBUG_OUTPUT
				printf("\nSelected method%d\n", select_method);
#endif
			} else {	// 他の奴と比較する
				wsprintfA(buf, "method%d", 1);
				kernel1 = fn_clCreateKernel(program, buf, &ret);
				if (ret == CL_SUCCESS){
					ret = fn_clGetKernelWorkGroupInfo(kernel1, selected_device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &item1, NULL);
					if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
						printf("\nTesting %s\n", buf);
						printf("PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %zu\n", item1);
#endif
					}
				}
				if (item9 >= item1){
					select_method = 9;
					OpenCL_kernel = kernel9;
					ret = fn_clReleaseKernel(kernel1);
#ifdef DEBUG_OUTPUT
					if (ret != CL_SUCCESS)
						printf("clReleaseKernel : Failed\n");
					printf("\nSelected method%d\n", select_method);
#endif
				} else {
					select_method = 1;
					OpenCL_kernel = kernel1;
					ret = fn_clReleaseKernel(kernel9);
#ifdef DEBUG_OUTPUT
					if (ret != CL_SUCCESS)
						printf("clReleaseKernel : Failed\n");
					printf("\nSelected method%d\n", select_method);
#endif
				}
			}
		}
		OpenCL_method |= select_method;
	}

	// カーネル関数を抽出する
	if (OpenCL_kernel == NULL){
		wsprintfA(buf, "method%d", OpenCL_method & 31);
		OpenCL_kernel = fn_clCreateKernel(program, buf, &ret);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 22;
#ifdef DEBUG_OUTPUT
		printf("CreateKernel : %s\n", buf);
		ret = fn_clGetKernelWorkGroupInfo(OpenCL_kernel, selected_device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &data_size, NULL);
		if (ret == CL_SUCCESS)
			printf("PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %zu\n", data_size);
#endif
	}

	// カーネルが実行できる work item 数を調べる
	ret = fn_clGetKernelWorkGroupInfo(OpenCL_kernel, selected_device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &data_size, NULL);
	if ((ret == CL_SUCCESS) && (data_size < 256)){	// 最低でも 256 以上は必要
#ifdef DEBUG_OUTPUT
		printf("KERNEL_WORK_GROUP_SIZE = %zd\n", data_size);
#endif
		return (ret << 8) | 23;
	}

	// プログラムを破棄する
	ret = fn_clReleaseProgram(program);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 24;

	// これ以上コンパイルしない
	if (fn_clUnloadPlatformCompiler != NULL){	// OpenCL 1.2 なら
		fn_clUnloadPlatformCompiler(selected_platform);
	} else {	// OpenCL 1.1 なら
		fn_clUnloadCompiler();
	}

	// work group 数が必要以上に多い場合は減らす
	if (OpenCL_method & 4){
		// work item 一個が 32バイトずつ計算する、256個なら work group ごとに 8KB 担当する
		data_size = unit_size / 8192;
	} else if (OpenCL_method & 2){
		// work item 一個が 8バイトずつ計算する、256個なら work group ごとに 2KB 担当する
		data_size = unit_size / 2048;
	} else {
		// work item 一個が 4バイトずつ計算する、256個なら work group ごとに 1KB 担当する
		data_size = unit_size / 1024;
	}
	if (OpenCL_group_num > data_size){
		OpenCL_group_num = data_size;
		printf("Number of work groups is reduced to %zd\n", OpenCL_group_num);
	}

	// データへのアクセス方法をデバイスによって変える
	if (OpenCL_method & 0x200000){
		OpenCL_method |= 32;
	} else if ((OpenCL_method & 0x100000) == 0){
		if (unified_memory){
			OpenCL_method |= 32;	// Integrated GPU なら CL_MEM_USE_HOST_PTR を使う
		} else {	// Discrete GPU でも Nvidia のは動作を変える
			ret = fn_clGetDeviceInfo(selected_device, CL_DEVICE_VERSION, sizeof(buf), buf, NULL);
			if (ret == CL_SUCCESS){
				if (strstr(buf, "CUDA") != NULL)
					OpenCL_method |= 32;	// NVIDIA GPU なら CL_MEM_USE_HOST_PTR を使う
			}
		}
	}

	// 出力先は1ブロック分だけあればいい
	// CL_MEM_ALLOC_HOST_PTRを使えばpinned memoryになるらしい
	data_size = unit_size;
	if (OpenCL_method & 8)
		data_size *= 2;	// 2ブロックずつ計算できるように、2倍確保しておく
	OpenCL_dst = gfn_clCreateBuffer(OpenCL_context, CL_MEM_ALLOC_HOST_PTR, data_size, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 13;
#ifdef DEBUG_OUTPUT
	printf("dst buf : %zd KB (%zd Bytes), OK\n", data_size >> 10, data_size);
#endif

	// factor は最大個数分 (src_max個)
	data_size = sizeof(unsigned short) * (*src_max);
	if (OpenCL_method & 8)
		data_size *= 2;	// 2ブロックずつ計算できるように、2倍確保しておく
	OpenCL_buf = gfn_clCreateBuffer(OpenCL_context, CL_MEM_READ_ONLY, data_size, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 14;
#ifdef DEBUG_OUTPUT
	printf("factor buf : %zd Bytes (%d factors), OK\n", data_size, (*src_max));
#endif

	// カーネル引数を指定する
	ret = gfn_clSetKernelArg(OpenCL_kernel, 1, sizeof(cl_mem), &OpenCL_dst);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 101;
	ret = gfn_clSetKernelArg(OpenCL_kernel, 2, sizeof(cl_mem), &OpenCL_buf);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 102;

#ifdef DEBUG_OUTPUT
	// ワークアイテム数
	printf("\nMax number of work items = %zd (256 * %zd)\n", OpenCL_group_num * 256, OpenCL_group_num);
#endif
	OpenCL_method &= 0xFF;	// 最後に選択設定を消去する

	return 0;
}

int free_OpenCL(void)
{
	API_clReleaseContext fn_clReleaseContext;
	API_clReleaseCommandQueue fn_clReleaseCommandQueue;
	API_clReleaseKernel fn_clReleaseKernel;
	int err = 0;	// 最初のエラーだけ記録する
	cl_int ret;

	if (hLibOpenCL == NULL)
		return 0;

	// OpenCL 関連のリソースを開放する
	if (OpenCL_command != NULL){
		// 動作中なら終了するのを待つ
		ret = gfn_clFinish(OpenCL_command);
		if ((err == 0) && (ret != CL_SUCCESS))
			err = (ret << 8) | 1;

		if (OpenCL_buf != NULL){
			ret = gfn_clReleaseMemObject(OpenCL_buf);
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 10;
			OpenCL_buf = NULL;
		}
		if (OpenCL_src != NULL){
			ret = gfn_clReleaseMemObject(OpenCL_src);
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 11;
			OpenCL_src = NULL;
		}
		if (OpenCL_dst != NULL){
			ret = gfn_clReleaseMemObject(OpenCL_dst);
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 12;
			OpenCL_dst = NULL;
		}
		if (OpenCL_kernel != NULL){
			fn_clReleaseKernel = (API_clReleaseKernel)GetProcAddress(hLibOpenCL, "clReleaseKernel");
			if (fn_clReleaseKernel != NULL){
				ret = fn_clReleaseKernel(OpenCL_kernel);
				OpenCL_kernel = NULL;
			} else {
				ret = 1;
			}
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 3;
		}
		fn_clReleaseCommandQueue = (API_clReleaseCommandQueue)GetProcAddress(hLibOpenCL, "clReleaseCommandQueue");
		if (fn_clReleaseCommandQueue != NULL){
			ret = fn_clReleaseCommandQueue(OpenCL_command);
			OpenCL_command = NULL;
		} else {
			ret = 1;
		}
		if ((err == 0) && (ret != CL_SUCCESS))
			err = (ret << 8) | 4;
	}
	if (OpenCL_context != NULL){
		fn_clReleaseContext = (API_clReleaseContext)GetProcAddress(hLibOpenCL, "clReleaseContext");
		if (fn_clReleaseContext != NULL){
			ret = fn_clReleaseContext(OpenCL_context);
			OpenCL_context = NULL;
		} else {
			ret = 1;
		}
		if ((err == 0) && (ret != CL_SUCCESS))
			err = (ret << 8) | 5;
	}

	FreeLibrary(hLibOpenCL);
	hLibOpenCL = NULL;

	return err;
}

void info_OpenCL(char *buf, int buf_size)
{
	API_clGetContextInfo fn_clGetContextInfo;
	API_clGetDeviceInfo fn_clGetDeviceInfo;
	cl_int ret;

	if ((hLibOpenCL == NULL) || (OpenCL_context == NULL))
		return;

	fn_clGetContextInfo = (API_clGetContextInfo)GetProcAddress(hLibOpenCL, "clGetContextInfo");
	fn_clGetDeviceInfo = (API_clGetDeviceInfo)GetProcAddress(hLibOpenCL, "clGetDeviceInfo");
	if ((fn_clGetContextInfo != NULL) && (fn_clGetDeviceInfo != NULL)){
		cl_device_id device_id;

		ret = fn_clGetContextInfo(OpenCL_context, CL_CONTEXT_DEVICES, sizeof(cl_device_id), &device_id, NULL);
		if (ret == CL_SUCCESS){
			ret = fn_clGetDeviceInfo(device_id, CL_DEVICE_NAME, buf_size / 2, buf, NULL);
			if (ret == CL_SUCCESS){
				ret = fn_clGetDeviceInfo(device_id, CL_DEVICE_VERSION, buf_size / 2, buf + buf_size / 2, NULL);
				if (ret != CL_SUCCESS)
					buf[buf_size / 2] = 0;
				printf("\nOpenCL : %s, %s, 256*%d\n", buf, buf + buf_size / 2, (int)OpenCL_group_num);
			}
		}
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ソース・ブロックをデバイス側にコピーする
int gpu_copy_blocks(
	unsigned char *data,	// ブロックのバッファー (境界は 4096にすること)
	unsigned int unit_size,	// 4096の倍数にすること
	int src_num)			// 何ブロックをコピーするのか
{
	size_t data_size;
	cl_int ret;
	cl_mem_flags flags;

	// Integrated GPU と Discrete GPU の違いに関係なく、使う分だけ毎回メモリー領域を確保する
	data_size = (size_t)unit_size * src_num;
	if (OpenCL_method & 32){	// AMD's APU や Integrated GPU なら ZeroCopy する
		// 実際に比較してみると GeForce GPU でもメモリー消費量が少なくてコピーが速い
		// NVIDIA GPU は CL_MEM_USE_HOST_PTR でも VRAM 上にキャッシュするので速いらしい
		flags = CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR;
	} else {	// Discrete GPU ならデータを VRAM にコピーする
		// AMD GPU は明示的にコピーするよう指定しないといけない
		flags = CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR;
	}
	OpenCL_src = gfn_clCreateBuffer(OpenCL_context, flags, data_size, data, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 1;
#ifdef DEBUG_OUTPUT
		//printf("refer buf : %d KB (%d blocks), OK\n", data_size >> 10, src_num);
#endif

	// メモリー領域を指定する
	ret = gfn_clSetKernelArg(OpenCL_kernel, 0, sizeof(cl_mem), &OpenCL_src);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 100;

	return 0;
}

// ソース・ブロックを掛け算する
int gpu_multiply_blocks(
	int src_num,			// Number of multiplying source blocks
	unsigned short *mat,	// Matrix of numbers to multiply by
	unsigned short *mat2,	// Set to calculate 2 blocks at once
	unsigned char *buf,		// Products go here
	unsigned int len)		// Byte length
{
	unsigned __int64 *vram, *src, *dst;
	size_t global_size, local_size;
	cl_int ret;

	// 倍率の配列をデバイス側に書き込む
	if (mat2 == NULL){	// 1ブロック分だけコピーする
		ret = gfn_clEnqueueWriteBuffer(OpenCL_command, OpenCL_buf, CL_FALSE, 0, sizeof(short) * src_num, mat, 0, NULL, NULL);
	} else {	// 2ブロックずつ計算する場合は、配列のサイズも２倍になる
		if ((size_t)mat2 == 1){	// アドレスが 1 になることはあり得ないので、識別できる
			ret = gfn_clEnqueueWriteBuffer(OpenCL_command, OpenCL_buf, CL_FALSE, 0, sizeof(short) * src_num * 2, mat, 0, NULL, NULL);
		} else {	// 2回コピーする
			size_t data_size = sizeof(short) * src_num;
			ret = gfn_clEnqueueWriteBuffer(OpenCL_command, OpenCL_buf, CL_FALSE, 0, data_size, mat, 0, NULL, NULL);
			if (ret != CL_SUCCESS)
				return (ret << 8) | 10;
			// もう一つの配列は違う場所からコピーする
			ret = gfn_clEnqueueWriteBuffer(OpenCL_command, OpenCL_buf, CL_FALSE, data_size, data_size, mat2, 0, NULL, NULL);
		}
	}
	if (ret != CL_SUCCESS)
		return (ret << 8) | 11;

	// 引数を指定する
	ret = gfn_clSetKernelArg(OpenCL_kernel, 3, sizeof(int), &src_num);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 103;

	// カーネル並列実行
	local_size = 256;	// テーブルやキャッシュのため、work item 数は 256 に固定する
	global_size = OpenCL_group_num * 256;
	//printf("group num = %d, global size = %d, local size = %d \n", OpenCL_group_num, global_size, local_size);
	ret = gfn_clEnqueueNDRangeKernel(OpenCL_command, OpenCL_kernel, 1, NULL, &global_size, &local_size, 0, NULL, NULL);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 12;

	// 出力内容をホスト側に反映させる
	vram = gfn_clEnqueueMapBuffer(OpenCL_command, OpenCL_dst, CL_TRUE, CL_MAP_READ, 0, len, 0, NULL, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 13;

	// 8バイトごとに XOR する (SSE2 で XOR しても速くならず)
	src = vram;
	dst = (unsigned __int64 *)buf;
	while (len > 0){
		*dst ^= *src;
		dst++;
		src++;
		len -= 8;
	}

	// ホスト側でデータを変更しなくても、clEnqueueMapBufferと対で呼び出さないといけない
	ret = gfn_clEnqueueUnmapMemObject(OpenCL_command, OpenCL_dst, vram, 0, NULL, NULL);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 14;

	return 0;
}

// 確保したVRAMとメモリーを解放する
int gpu_finish(void)
{
	cl_int ret;

	// 全ての処理が終わるのを待つ
	ret = gfn_clFinish(OpenCL_command);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 30;

	if (OpenCL_src != NULL){	// 確保されてる場合は解除する
		ret = gfn_clReleaseMemObject(OpenCL_src);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 31;
		OpenCL_src = NULL;
	}

	return 0;
}

