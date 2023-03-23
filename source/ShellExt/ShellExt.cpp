// Copyright : 2023-03-23 Yutaka Sawada
// License : The MIT license

// ShellExt.cpp : DLL アプリケーション用のエントリ ポイントを定義します。
//

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600	// Windows Vista or later
#endif

#include <windows.h>
#include <shlobj.h>
#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")


// 定義
#define MAX_LEN		1024	// ファイル名の最大文字数 (末尾のNULL文字も含む)
//#define DEBUG_OUTPUT		// デバッグ出力するかどうか

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// クラスファクトリの作成 (IClassFactoryインターフェイスを継承する)
class CShellExtClassFactory : public IClassFactory
{
private:
	// 参照カウント
	long m_cRef;

public:
	// コンストラクタ・デストラクタ
	CShellExtClassFactory();
	~CShellExtClassFactory();

	//IUnknown インターフェイスのメソッド
	STDMETHODIMP			QueryInterface(REFIID, void **);
	STDMETHODIMP_(ULONG)	AddRef();
	STDMETHODIMP_(ULONG)	Release();

	//IClassFactory インターフェイスのメソッド
	STDMETHODIMP			CreateInstance(LPUNKNOWN, REFIID, LPVOID FAR *);
	STDMETHODIMP			LockServer(BOOL);
};

class CShellExtension :	public IShellExtInit,
						public IContextMenu
{
private:
	long			m_cRef;		// オブジェクトの参照カウント
	LPDATAOBJECT	m_pDataObj;	// エクスプローラから受け取るデータオブジェクト
	int single_file;
	int CheckData(void);		// 選択したファイルの検査
	int DoCommand(UINT idCmd);	// 選択した拡張メニューのコマンド実行
	int DoCommand7zip(void);

public:
	// コンストラクタ・デストラクタ
	CShellExtension();
	~CShellExtension();

	// IUnknown インターフェイスのメソッド
	STDMETHODIMP			QueryInterface(REFIID, void **);
	STDMETHODIMP_(ULONG)	AddRef();
	STDMETHODIMP_(ULONG)	Release();

	// IShellExtInit インターフェイスのメソッド
	STDMETHODIMP Initialize(LPCITEMIDLIST pIDFolder, LPDATAOBJECT pDataObj, HKEY hKeyID);

	// IContextMenu インターフェイスのメソッド
	STDMETHODIMP QueryContextMenu(
		HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags);
	STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO lpcmi);
	STDMETHODIMP GetCommandString(
		UINT_PTR idCmd, UINT uFlags, UINT FAR *reserved, LPSTR pszName, UINT cchMax);
};

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// {333EFDA5-A74E-4df4-A225-92A7AF81F29A}
static const GUID CLSID_ShellExt = 
{ 0x333efda5, 0xa74e, 0x4df4, { 0xa2, 0x25, 0x92, 0xa7, 0xaf, 0x81, 0xf2, 0x9a } };

HINSTANCE g_inst = NULL;
HBITMAP g_bmp, g_bmp2;
long g_cRefDll = 0;

#define TOTAL_LENGTH 256
#define MIN_LENGTH 2

// 動作設定 (MultiPar.ini から読み込む)
int menu_behavior;

#ifdef DEBUG_OUTPUT
// デバッグ用の作業領域
wchar_t debug_buf[MAX_LEN];
HANDLE hDebug = NULL;

// ファイルを開いて末尾に移動する
BOOL open_debug_file(void)
{
	DWORD len = GetModuleFileName(g_inst, debug_buf, MAX_LEN);
	if ((len > 0) && (len < MAX_LEN)){
		while (len > 0){
			len--;
			if (debug_buf[len] == '\\'){
				len++;
				break;
			}
		}
		debug_buf[len] = 0;	// ファイル名を消す
		wcscat(debug_buf, L"debug.txt");
		// ファイルを開いて末尾に移動する
		hDebug = CreateFile(debug_buf, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
		if (hDebug != INVALID_HANDLE_VALUE){
			len = SetFilePointer(hDebug, 0, NULL, FILE_END);
			if (len == 0){
				// BOMを書き込む
				debug_buf[0] = 0xFEFF;
				debug_buf[1] = 0;
				DWORD write_size;
				if (!WriteFile(hDebug, debug_buf, 2, &write_size, NULL)){
					CloseHandle(hDebug);
					hDebug = NULL;
				}
			}
			return TRUE;
		}
	}
	hDebug = NULL;
	return FALSE;
}
#endif

// 管理者権限で動いてるか調べる Administrator privileges
// http://umezawa.dyndns.info/wordpress/?p=5191
static BOOL check_admin(void)
{
	HANDLE hToken;
	TOKEN_ELEVATION elevation;
	DWORD cb;

	if (OpenProcessToken(GetCurrentProcess(), GENERIC_READ, &hToken)){
		if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cb)){
			if (elevation.TokenIsElevated){	// Run as Administrator
				TOKEN_ELEVATION_TYPE elevtype;
				if (GetTokenInformation(hToken, TokenElevationType, &elevtype, sizeof(elevtype), &cb)){
					if (elevtype == TokenElevationTypeDefault){
						// User is administrator and UAC (User Account Control) is disabled.
						CloseHandle(hToken);
						return TRUE;
					}
				}
			}
		}
		CloseHandle(hToken);
	}

	return FALSE;
}

// 7-Zip のパスが正しいか確認してキーを閉じる
static int check_path_7zip(HKEY hKey, wchar_t *buf)
{
	unsigned long ret, type, size;

	size = (MAX_PATH - 7) * 2;
	ret = RegQueryValueEx(hKey, L"Path", NULL, &type, (LPBYTE)buf, &size);
	if (ret == ERROR_SUCCESS){
		if ((type == REG_SZ) && (size >= 6) && (buf[size / 2 - 2] == '\\')){
			// 7-Zip の実行ファイルが存在するか確かめる
			wcscat(buf, L"7zG.exe");
			type = GetFileAttributes(buf);
			if ((type == INVALID_FILE_ATTRIBUTES) || (type & FILE_ATTRIBUTE_DIRECTORY)){
				buf[0] = 0;
				ret = 100002;	// ファイルが存在しない
			}
		} else {
			buf[0] = 0;
			ret = 100001;	// key は存在するけど形式が違う
		}
	}
	RegCloseKey(hKey);
	return ret;
}

// 7-Zip のディレクトリを取得する
static void get_path_7zip(wchar_t *buf)
{
	int ret;
	HKEY hKey;

	buf[0] = 0;	// 正常に取得できた場合は、ここに値が入る
	ret = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\7-Zip", 0, KEY_READ, &hKey);
	if (ret == ERROR_SUCCESS)
		ret = check_path_7zip(hKey, buf);

	if (ret != ERROR_SUCCESS){
		// 関連付けやインストール時の選択肢によっては HKLM に値が記録される？32-bit 版の 7-Zip
		ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\7-Zip", 0, KEY_READ, &hKey);
		if (ret == ERROR_SUCCESS)
			ret = check_path_7zip(hKey, buf);
	}

	if (ret != ERROR_SUCCESS){
		// 64-bit 版の 7-Zip のキーは 32-bit 用のレジストリには書き込まれてない。
		ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\7-Zip", 0, KEY_READ | KEY_WOW64_64KEY, &hKey);
		if (ret == ERROR_SUCCESS)
			check_path_7zip(hKey, buf);
	}
}

static void InitBitmapInfo(BITMAPINFO *pbmi, LONG cx, LONG cy)
{
	ZeroMemory(pbmi, sizeof(BITMAPINFO));
	pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pbmi->bmiHeader.biWidth = cx;
	pbmi->bmiHeader.biHeight = cy;
	pbmi->bmiHeader.biPlanes = 1;
	pbmi->bmiHeader.biBitCount = 32;
	pbmi->bmiHeader.biCompression = BI_RGB;
}

// メニュー・アイコン用のビットマップを読み込む
static int load_menu_icon(void)
{
	int size_w, size_h;
	HICON hIcon;
	LPVOID lpBits;
	BITMAPINFO bmi;

	if (g_bmp != NULL)
		return 0;

	size_w = GetSystemMetrics(SM_CXSMICON);
	size_h = GetSystemMetrics(SM_CYSMICON);

	// ARGB 形式のビットマップを作成する
	InitBitmapInfo(&bmi, size_w, size_h);
	g_bmp = CreateDIBSection(NULL, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, &lpBits, NULL, 0);
	if (g_bmp == NULL)
		return 1;

	// アイコンをロードする
	hIcon = (HICON)LoadImage(g_inst, MAKEINTRESOURCE(100), IMAGE_ICON, size_w, size_h, LR_DEFAULTCOLOR);
	if (hIcon == NULL){
		DeleteObject(g_bmp);
		g_bmp = NULL;
		return 2;
	}

	// ビットマップに ARGB 形式のアイコンをコピーする（アルファ値が無いと見た目がおかしくなる）
	HDC hDC;
	HBITMAP prev_bmp;
	hDC = CreateCompatibleDC(NULL);
	if (hDC == NULL){
		DestroyIcon(hIcon);
		DeleteObject(g_bmp);
		g_bmp = NULL;
		return 3;
	}
	prev_bmp = (HBITMAP)SelectObject(hDC, g_bmp);
	if (DrawIconEx(hDC, 0, 0, hIcon, size_w, size_h, 0, NULL, DI_NORMAL) == 0){
		SelectObject(hDC, prev_bmp);
		DeleteDC(hDC);
		DestroyIcon(hIcon);
		DeleteObject(g_bmp);
		g_bmp = NULL;
		return 4;
	}
	SelectObject(hDC, prev_bmp);
	DeleteDC(hDC);
	DestroyIcon(hIcon);

	return 0;
}

// Uxtheme.h を参照すること。Windows Vista 以降なら対応してる
typedef DWORD ARGB;

static int ConvertToPARGB32(HDC hdc, __inout ARGB *pargb, HBITMAP hbmp, int size_x, int size_y, int cxRow)
{
	BITMAPINFO bmi;
	InitBitmapInfo(&bmi, size_x, size_y);

	HRESULT hr = E_OUTOFMEMORY;
	HANDLE hHeap = GetProcessHeap();
	void *pvBits = HeapAlloc(hHeap, 0, bmi.bmiHeader.biWidth * 4 * bmi.bmiHeader.biHeight);
	if (pvBits){
		hr = E_UNEXPECTED;
		if (GetDIBits(hdc, hbmp, 0, bmi.bmiHeader.biHeight, pvBits, &bmi, DIB_RGB_COLORS) == bmi.bmiHeader.biHeight){
			ULONG cxDelta = cxRow - bmi.bmiHeader.biWidth;
			ARGB *pargbMask = static_cast<ARGB *>(pvBits);

			for (ULONG y = bmi.bmiHeader.biHeight; y; --y){
				for (ULONG x = bmi.bmiHeader.biWidth; x; --x){
					if (*pargbMask++){	// transparent pixel
						*pargb++ = 0;
					} else {	// opaque pixel
						*pargb++ |= 0xFF000000;
					}
				}

				pargb += cxDelta;
			}

			hr = S_OK;
		}

		HeapFree(hHeap, 0, pvBits);
	}

	return hr;
}

static bool HasAlpha(__in ARGB *pargb, int size_x, int size_y, int cxRow)
{
	int cxDelta = cxRow - size_x;
	for (int y = size_y; y; --y){
		for (int x = size_x; x; --x){
			if (*pargb++ & 0xFF000000){
				return true;
			}
		}

		pargb += cxDelta;
	}

	return false;
}

// 実行ファイルからメニュー・アイコン用のビットマップを読み込む
static int load_menu_icon2(wchar_t *file_path)
{
	int size_w, size_h, rv;
	HICON hIcon;
	LPVOID lpBits;
	BITMAPINFO bmi;

	if (g_bmp2 != NULL)
		return 0;

	size_w = GetSystemMetrics(SM_CXSMICON);
	size_h = GetSystemMetrics(SM_CYSMICON);

	// ARGB 形式のビットマップを作成する
	InitBitmapInfo(&bmi, size_w, size_h);
	g_bmp2 = CreateDIBSection(NULL, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, &lpBits, NULL, 0);
	if (g_bmp2 == NULL){
		return 3;
	}

	// アイコンを実行ファイルから読み取る
	rv = ExtractIconEx(file_path, 0, NULL, &hIcon, 1);
	if ((hIcon == NULL) || (rv != 1)){
		DeleteObject(g_bmp2);
		g_bmp2 = NULL;
		return 4;
	}

	// ビットマップにアイコンをコピーする
	HDC hDC;
	HBITMAP prev_bmp;
	hDC = CreateCompatibleDC(NULL);
	if (hDC == NULL){
		DeleteObject(g_bmp2);
		g_bmp2 = NULL;
		DestroyIcon(hIcon);
		return 5;
	}
	prev_bmp = (HBITMAP)SelectObject(hDC, g_bmp2);
	// ARGB形式に変換する作業バッファーを用意する
	BLENDFUNCTION bfAlpha = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	BP_PAINTPARAMS paintParams = {0};
	paintParams.cbSize = sizeof(paintParams);
	paintParams.dwFlags = BPPF_ERASE;
	paintParams.pBlendFunction = &bfAlpha;
	HDC hdcBuffer;
	RECT rcIcon;
	SetRect(&rcIcon, 0, 0, size_w, size_h);
	HPAINTBUFFER hPaintBuffer = BeginBufferedPaint(hDC, &rcIcon, BPBF_DIB, &paintParams, &hdcBuffer);
	if (hPaintBuffer){
		if (DrawIconEx(hdcBuffer, 0, 0, hIcon, size_w, size_h, 0, NULL, DI_NORMAL) == 0){
			EndBufferedPaint(hPaintBuffer, FALSE);
			SelectObject(hDC, prev_bmp);
			DeleteDC(hDC);
			DestroyIcon(hIcon);
			DeleteObject(g_bmp2);
			g_bmp2 = NULL;
			return 6;
		}
		// ConvertBufferToPARGB32 の間はエラーになってもアルファ無しのアイコンを表示する
		RGBQUAD *prgbQuad;
		int cxRow;
		if (GetBufferedPaintBits(hPaintBuffer, &prgbQuad, &cxRow) == S_OK){
			ARGB *pargb = reinterpret_cast<ARGB *>(prgbQuad);
			if (!HasAlpha(pargb, size_w, size_h, cxRow)){	// アルファ値が無ければ
				ICONINFO info;
				if (GetIconInfo(hIcon, &info)){
					if (info.hbmMask)
					    ConvertToPARGB32(hdcBuffer, pargb, info.hbmMask, size_w, size_h, cxRow);
					DeleteObject(info.hbmColor);
					DeleteObject(info.hbmMask);
				}
			}
		}
		EndBufferedPaint(hPaintBuffer, TRUE);
	}
	SelectObject(hDC, prev_bmp);
	DeleteDC(hDC);
	DestroyIcon(hIcon);

	return 0;
}

// 言語別のテキスト (MultiParShlExt.ini から読み込む)
int offset_c, offset_v, offset_a;
wchar_t menu_item[TOTAL_LENGTH];

static int load_setting(void)
{
	wchar_t path[MAX_PATH], path2[MAX_PATH], lang_num[8];
	int rv, lang_id;

	// 設定ファイルが存在するディレクトリ
	// DLL と呼び出す EXE ファイルは同じディレクトリに置くこと
	rv = GetModuleFileName(g_inst, path, MAX_PATH);
	if ((rv == 0) || (rv >= MAX_PATH))
		return 1;
	while (rv > 0){
		rv--;
		if (path[rv] == '\\'){
			path[rv] = 0;
			break;
		}
	}

	// アプリケーション・データのディレクトリを決める
	rv = 0;
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, path2))){
		//MessageBox(NULL, path2, L"program files path", MB_OK);
		// ProgramFiles の位置と比較する
		int i, max = 0;
		while (path2[max] != 0)
			max++;
		for (i = 0; i < max; i++){
			if (path2[i] != path[i])
				break;
		}
		if (i == max){
			if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path2))){
				//MessageBox(NULL, path2, L"application data path", MB_OK);
				i = 0;
				while (path2[i] != 0)
					i++;
				if (i < MAX_PATH - 10 - 18){
					wcscat(path2, L"\\MultiPar");
					rv = 2;	// AppData を使う
				}
			}
		}
	}
	if (rv != 2)
		wcscpy(path2, path);	// DLL が存在するディレクトリを使う
	wcscat(path2, L"\\MultiPar.ini");
	//MessageBox(NULL, path2, L"MultiPar.ini path", MB_OK);

	// 動作設定を読み込む
	menu_behavior = GetPrivateProfileInt(L"Option", L"ShellExtension", 0, path2);
	lang_id = GetPrivateProfileInt(L"Option", L"Language", -1, path2);

	if ((menu_behavior & 16) == 0){	// メニューにアイコンを付ける
		if (load_menu_icon() != 0)
			menu_behavior |= 16;	// アイコン作成失敗
	}

	if ((menu_behavior & 32) == 0){	// 7-Zip がインストールされてなければ使わない
		get_path_7zip(path2);
		if (path2[0] == 0){
			menu_behavior |= 32;	// メニューを表示しない
		} else if ((menu_behavior & 16) == 0){
			load_menu_icon2(path2);	// アイコン取得に失敗してもメニューは表示する
		}
	}

	// 言語ごとのメニュー項目を取得する
	wcscat(path, L"\\MultiParShlExt.ini");
	if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES)
		return 1;	// 言語別テキストが無い
	// 指定された言語のテキストを読み込む
	if (lang_id < 0)
		lang_id = GetUserDefaultLangID();
	wsprintf(lang_num, L"0x%04x", lang_id);
	rv = GetPrivateProfileString(lang_num, L"MenuTitle", L"", menu_item, TOTAL_LENGTH, path);
	if (rv < MIN_LENGTH){	// その言語の設定項目が無いので、読み込みに失敗した
		// 同じ言語に複数の地域が割り振られてる場合は別ので代用する
		unsigned int pri_id, sub_id;
		pri_id = lang_id & 0x03FF;	// PRIMARYLANGID だけにする
		for (sub_id = 1; sub_id < 64; sub_id++){	// SUBLANGID を変更する
			lang_id = pri_id | (sub_id << 10);
			wsprintf(lang_num, L"0x%04x", lang_id);
			rv = GetPrivateProfileString(lang_num, L"MenuTitle", L"", menu_item, TOTAL_LENGTH, path);
			if (rv >= MIN_LENGTH)
				break;
		}
		if (rv < MIN_LENGTH){	// 英語のリソースが存在すればそれで代用する
			lang_id = 0x409;
			wsprintf(lang_num, L"0x%04x", lang_id);
			rv = GetPrivateProfileString(lang_num, L"MenuTitle", L"", menu_item, TOTAL_LENGTH, path);
		}
	}
	if (rv >= MIN_LENGTH){	// 設定項目を読み込み成功した
		offset_c = rv + 1;
		if (offset_c >= TOTAL_LENGTH - MIN_LENGTH)
			return 1;
		rv = GetPrivateProfileString(lang_num, L"Create", L"", menu_item + offset_c, TOTAL_LENGTH - offset_c, path);
		if (rv < MIN_LENGTH)
			return 1;
		offset_v = offset_c + rv + 1;
		if (offset_v >= TOTAL_LENGTH - MIN_LENGTH){
			menu_behavior |= 8;		// disable sub-menu for Verify
			menu_behavior |= 32;	// disable archive
			return 0;
		}
		if ((menu_behavior & 8) == 0){
			rv = GetPrivateProfileString(lang_num, L"Verify", L"", menu_item + offset_v, TOTAL_LENGTH - offset_v, path);
			offset_a = offset_v + rv + 1;
			if (rv < MIN_LENGTH)
				menu_behavior |= 8;		// disable sub-menu for Verify
			if (offset_a >= TOTAL_LENGTH - MIN_LENGTH){
				menu_behavior |= 32;	// disable archive
				return 0;
			}
		} else {
			offset_a = offset_v;
		}
		if ((menu_behavior & 32) == 0){
			rv = GetPrivateProfileString(lang_num, L"Archive", L"", menu_item + offset_a, TOTAL_LENGTH - offset_a, path);
			if (rv < MIN_LENGTH){
				menu_behavior |= 32;	// disable archive
				return 0;
			}
		}
	} else {
		return 1;
	}

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern "C" BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH){
		g_inst = hInstance;
		DisableThreadLibraryCalls(hInstance);
		g_bmp = NULL;
		g_bmp2 = NULL;
		menu_behavior = -1;	// 設定はまだ読み込まれてない
	} else if (dwReason == DLL_PROCESS_DETACH){
		if (g_bmp != NULL){
			DeleteObject(g_bmp);
			g_bmp = NULL;
		}
		if (g_bmp2 != NULL){
			DeleteObject(g_bmp2);
			g_bmp2 = NULL;
		}
	}
	return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
#ifdef DEBUG_OUTPUT
	if ((hDebug != NULL) && (g_cRefDll == 0)){
		SYSTEMTIME st;
		GetLocalTime(&st);
		wsprintf(debug_buf, L"DllCanUnloadNow = %04d/%02d/%02d %02d:%02d:%02d\r\n",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
		DWORD write_size;
		WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL);
		CloseHandle(hDebug);
		hDebug = NULL;
	}
#endif

	//参照カウントが 0 ならS_OK
	return (g_cRefDll == 0 ? S_OK : S_FALSE);
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppvOut)
{
	*ppvOut = NULL;

#ifdef DEBUG_OUTPUT
	if (open_debug_file()){
		SYSTEMTIME st;
		GetLocalTime(&st);
		wsprintf(debug_buf, L"DllGetClassObject = %04d/%02d/%02d %02d:%02d:%02d\r\n",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
		DWORD write_size;
		if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
			CloseHandle(hDebug);
			hDebug = NULL;
		}
	}
#endif

	if (IsEqualIID(rclsid, CLSID_ShellExt)){
		// クラスファクトリの作成。
		CShellExtClassFactory *pcf = new CShellExtClassFactory;
		if (pcf){
			HRESULT hr = pcf->QueryInterface(riid, ppvOut);
			pcf->Release();
			return hr;
		} else {
			return E_OUTOFMEMORY;
		}
	}
	//失敗時はCLASS_E_CLASSNOTAVAILABLEを返す
	return CLASS_E_CLASSNOTAVAILABLE;
}

// レジストリ登録 regsvr32.exe ShellExt.dll
// https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/shell/reg-shell-exts.md
STDAPI DllRegisterServer(void)
{
	wchar_t CLSID_str[] = L"{333EFDA5-A74E-4df4-A225-92A7AF81F29A}";
	wchar_t ext_name[] = L"MultiPar Shell Extension";
	wchar_t path[MAX_PATH], buf[128];
	unsigned int len;
	HKEY hBaseKey, hKey = NULL;

/*{	// for debug
	len = load_setting();
	wsprintf(buf, L"return value = %d", len);
	MessageBox(NULL, buf, NULL, 0);

	return E_FAIL;
}*/

	len = GetModuleFileName(g_inst, path, MAX_PATH);
	if ((len == 0) || (len >= MAX_PATH))
		return E_FAIL;

	// 管理者権限で動いてる場合は参照先を変える
	if (check_admin()){
		hBaseKey = HKEY_LOCAL_MACHINE;
	} else {
		hBaseKey = HKEY_CURRENT_USER;
	}

	// CLSID に COM オブジェクトを登録する
	wsprintf(buf, L"Software\\Classes\\CLSID\\%s", CLSID_str);
	if (RegCreateKeyEx(hBaseKey, buf, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
		goto error_end;
	RegCloseKey(hKey);
	hKey = NULL;
	wsprintf(buf, L"Software\\Classes\\CLSID\\%s\\InprocServer32", CLSID_str);
	if (RegCreateKeyEx(hBaseKey, buf, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
		goto error_end;
	if (RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE*)path, (len+1)*2) != ERROR_SUCCESS)
		goto error_end;
	lstrcpy(buf, L"Apartment");
	len = lstrlen(buf);
	if (RegSetValueEx(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)buf, (len+1)*2) != ERROR_SUCCESS)
		goto error_end;
	RegCloseKey(hKey);
	hKey = NULL;

	// 全てのファイルとフォルダに Shell Extension を追加する
	wsprintf(buf, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s", ext_name);
	if (RegCreateKeyEx(hBaseKey, buf, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
		goto error_end;
	len = lstrlen(CLSID_str);
	if (RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE*)CLSID_str, (len+1)*2) != ERROR_SUCCESS)
		goto error_end;
	RegCloseKey(hKey);
	hKey = NULL;
	wsprintf(buf, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\%s", ext_name);
	if (RegCreateKeyEx(hBaseKey, buf, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
		goto error_end;
	if (RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE*)CLSID_str, (len+1)*2) != ERROR_SUCCESS)
		goto error_end;
	RegCloseKey(hKey);
	hKey = NULL;

	// 許可を登録する
	if (RegOpenKeyEx(hBaseKey, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS){
		len = lstrlen(ext_name);
		RegSetValueEx(hKey, CLSID_str, 0, REG_SZ, (const BYTE*)ext_name, (len+1)*2);	// 登録できなくてもエラーにしない
		RegCloseKey(hKey);
	}
	hKey = NULL;

	// 更新を通知する
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

	return S_OK;
error_end:
	if (hKey)
		RegCloseKey(hKey);
	return E_FAIL;
}

// レジストリ登録解除 regsvr32.exe /u ShellExt.dll
STDAPI DllUnregisterServer(void)
{
	wchar_t CLSID_str[] = L"{333EFDA5-A74E-4df4-A225-92A7AF81F29A}";
	wchar_t ext_name[] = L"MultiPar Shell Extension";
	wchar_t buf[128];
	unsigned long err = 0, empty, len, ret;
	HKEY hBaseKey, hKey;
	FILETIME ft;

	// 管理者権限で動いてる場合は参照先を変える
	if (check_admin()){
		hBaseKey = HKEY_LOCAL_MACHINE;
	} else {
		hBaseKey = HKEY_CURRENT_USER;
	}

	// CLSID の COM オブジェクトを削除する
	wsprintf(buf, L"Software\\Classes\\CLSID\\%s\\InprocServer32", CLSID_str);
	ret = RegDeleteKey(hBaseKey, buf);
	if ((ret != ERROR_SUCCESS) && (ret != ERROR_FILE_NOT_FOUND))
		err++;
	wsprintf(buf, L"Software\\Classes\\CLSID\\%s", CLSID_str);
	ret = RegDeleteKey(hBaseKey, buf);
	if ((ret != ERROR_SUCCESS) && (ret != ERROR_FILE_NOT_FOUND))
		err++;

	// 全てのファイルとフォルダの Shell Extension を削除する
	wsprintf(buf, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s", ext_name);
	ret = RegDeleteKey(hBaseKey, buf);
	if ((ret != ERROR_SUCCESS) && (ret != ERROR_FILE_NOT_FOUND))
		err++;
	wsprintf(buf, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\%s", ext_name);
	ret = RegDeleteKey(hBaseKey, buf);
	if ((ret != ERROR_SUCCESS) && (ret != ERROR_FILE_NOT_FOUND))
		err++;

	// 空のエントリーを削除する
	empty = 1;
	if (RegOpenKeyEx(hBaseKey, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers", 0, KEY_READ, &hKey) == ERROR_SUCCESS){
		// ContextMenuHandlers 内に他のデータが無ければ項目自体を削除する。
		len = _countof(buf);
		if (RegEnumKeyEx(hKey, 0, buf, &len, NULL, NULL, NULL, &ft) != ERROR_NO_MORE_ITEMS){
			empty = 0;
		} else {
			len = _countof(buf);
			if (RegEnumValue(hKey, 0, buf, &len, NULL, NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS)
				empty = 0;
		}
		RegCloseKey(hKey);
		if (empty && (RegDeleteKey(hBaseKey, L"Software\\Classes\\*\\shellex\\ContextMenuHandlers") != ERROR_SUCCESS))
			err++;
	}
	if (empty){
		if (RegOpenKeyEx(hBaseKey, L"Software\\Classes\\*\\shellex", 0, KEY_READ, &hKey) == ERROR_SUCCESS){
			// shellex 内に他のデータが無ければ項目自体を削除する。
			len = _countof(buf);
			if (RegEnumKeyEx(hKey, 0, buf, &len, NULL, NULL, NULL, &ft) != ERROR_NO_MORE_ITEMS){
				empty = 0;
			} else {
				len = _countof(buf);
				if (RegEnumValue(hKey, 0, buf, &len, NULL, NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS)
					empty = 0;
			}
			RegCloseKey(hKey);
			if (empty && (RegDeleteKey(hBaseKey, L"Software\\Classes\\*\\shellex") != ERROR_SUCCESS))
				err++;
		}
		if (empty){
			if (RegOpenKeyEx(hBaseKey, L"Software\\Classes\\*", 0, KEY_READ, &hKey) == ERROR_SUCCESS){
				// * 内に他のデータが無ければ項目自体を削除する。
				len = _countof(buf);
				if (RegEnumKeyEx(hKey, 0, buf, &len, NULL, NULL, NULL, &ft) != ERROR_NO_MORE_ITEMS){
					empty = 0;
				} else {
					len = _countof(buf);
					if (RegEnumValue(hKey, 0, buf, &len, NULL, NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS)
						empty = 0;
				}
				RegCloseKey(hKey);
				if (empty && (RegDeleteKey(hBaseKey, L"Software\\Classes\\*") != ERROR_SUCCESS))
					err++;
			}
		}
	}
	empty = 1;
	if (RegOpenKeyEx(hBaseKey, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers", 0, KEY_READ, &hKey) == ERROR_SUCCESS){
		// ContextMenuHandlers 内に他のデータが無ければ項目自体を削除する。
		len = _countof(buf);
		if (RegEnumKeyEx(hKey, 0, buf, &len, NULL, NULL, NULL, &ft) != ERROR_NO_MORE_ITEMS){
			empty = 0;
		} else {
			len = _countof(buf);
			if (RegEnumValue(hKey, 0, buf, &len, NULL, NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS)
				empty = 0;
		}
		RegCloseKey(hKey);
		if (empty && (RegDeleteKey(hBaseKey, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers") != ERROR_SUCCESS))
			err++;
	}
	if (empty){
		if (RegOpenKeyEx(hBaseKey, L"Software\\Classes\\Directory\\shellex", 0, KEY_READ, &hKey) == ERROR_SUCCESS){
			// shellex 内に他のデータが無ければ項目自体を削除する。
			len = _countof(buf);
			if (RegEnumKeyEx(hKey, 0, buf, &len, NULL, NULL, NULL, &ft) != ERROR_NO_MORE_ITEMS){
				empty = 0;
			} else {
				len = _countof(buf);
				if (RegEnumValue(hKey, 0, buf, &len, NULL, NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS)
					empty = 0;
			}
			RegCloseKey(hKey);
			if (empty && (RegDeleteKey(hBaseKey, L"Software\\Classes\\Directory\\shellex") != ERROR_SUCCESS))
				err++;
		}
		if (empty){
			if (RegOpenKeyEx(hBaseKey, L"Software\\Classes\\Directory", 0, KEY_READ, &hKey) == ERROR_SUCCESS){
				// Directory 内に他のデータが無ければ項目自体を削除する。
				len = _countof(buf);
				if (RegEnumKeyEx(hKey, 0, buf, &len, NULL, NULL, NULL, &ft) != ERROR_NO_MORE_ITEMS){
					empty = 0;
				} else {
					len = _countof(buf);
					if (RegEnumValue(hKey, 0, buf, &len, NULL, NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS)
						empty = 0;
				}
				RegCloseKey(hKey);
				if (empty && (RegDeleteKey(hBaseKey, L"Software\\Classes\\Directory") != ERROR_SUCCESS))
					err++;
			}
		}
	}

	// 許可を削除する
	if (RegOpenKeyEx(hBaseKey, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS){
		ret = RegDeleteValue(hKey, CLSID_str);
		if ((ret != ERROR_SUCCESS) && (ret != ERROR_FILE_NOT_FOUND))	// 項目自体が存在しない場合はエラーにしない
			err++;
		RegCloseKey(hKey);
	}

	// 更新を通知する
	// https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/shell/reg-shell-exts.md
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

	if (err == 0)
		return S_OK;
	return E_FAIL;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

CShellExtClassFactory::CShellExtClassFactory()
{
	m_cRef = 1;
    InterlockedIncrement(&g_cRefDll);
}

CShellExtClassFactory::~CShellExtClassFactory()
{
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CShellExtClassFactory::QueryInterface(REFIID riid, void **ppvObject)
{
	*ppvObject = NULL;

	// Any interface on this object is the object pointer
	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)){
		*ppvObject = (LPCLASSFACTORY)this;
	} else {
		return E_NOINTERFACE;
	}

	AddRef();

	return S_OK;
}

STDMETHODIMP_(ULONG) CShellExtClassFactory::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CShellExtClassFactory::Release()
{
	ULONG cRef = InterlockedDecrement(&m_cRef);
	if (cRef == 0){
		delete this;
	}
	return cRef;
}

// IClassFactory::CreateInstance()
STDMETHODIMP CShellExtClassFactory::CreateInstance(
	LPUNKNOWN pUnkOuter, REFIID riid, LPVOID *ppvObject)
{
	*ppvObject = NULL;

	// 集合をサポートしないので却下(?)。SDKサンプルより
	if (pUnkOuter)
		return CLASS_E_NOAGGREGATION;

	// シェル拡張オブジェクトを作成する。
	// そのあとシェルはppvObjectのIID_IShellExtInitを引数に
	// QueryInterfaceメソッドを呼び出し、初期化します
	CShellExtension *pShellExt = new CShellExtension();
	if (pShellExt == NULL)
		return E_OUTOFMEMORY;

	// 目的のインターフェイスのポインタを取得
	HRESULT hr = pShellExt->QueryInterface(riid, ppvObject);
	pShellExt->Release();

	return hr;
}

// IClassFactory::LockServer()
STDMETHODIMP CShellExtClassFactory::LockServer(BOOL fLock)
{
	if (fLock){
		InterlockedIncrement(&g_cRefDll);
	} else {
		InterlockedDecrement(&g_cRefDll);
	}
	return S_OK;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

CShellExtension::CShellExtension()
{
	m_cRef = 1;
	m_pDataObj = NULL;

	InterlockedIncrement(&g_cRefDll);
}

CShellExtension::~CShellExtension()
{
	if (m_pDataObj)
		m_pDataObj->Release();

	InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CShellExtension::QueryInterface(REFIID riid, void **ppvObject)
{
	*ppvObject = NULL;

	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IContextMenu)){
		*ppvObject = (LPCONTEXTMENU)this;
	} else if (IsEqualIID(riid, IID_IShellExtInit)){
		*ppvObject = (LPSHELLEXTINIT)this;
	} else {
		return E_NOINTERFACE;
	}

	AddRef();

	return S_OK;
}

STDMETHODIMP_(ULONG) CShellExtension::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CShellExtension::Release()
{
	ULONG cRef = InterlockedDecrement(&m_cRef);
	if (cRef == 0){
		delete this;
	}
	return cRef;	// ローカル変数を使っているのは delete 後でも値を返せるように
}


// Initializing Shell Extension Handlers
// https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/shell/int-shell-exts.md
STDMETHODIMP CShellExtension::Initialize(
	LPCITEMIDLIST pIDFolder, LPDATAOBJECT pDataObj, HKEY hRegKey)
{
	// 何回も呼ばれるので、二回目以降は、オブジェクトを解放
	if (m_pDataObj){
		m_pDataObj->Release();
		m_pDataObj = NULL;
	}
	if (pDataObj){
		m_pDataObj = pDataObj;
		pDataObj->AddRef();
		return CheckData();
	}

	return E_INVALIDARG;
}

STDMETHODIMP CShellExtension::QueryContextMenu(
	HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
	// uFlags に CMF_DEFAULTONLY が含まれる場合は何もしない
	if (uFlags & (CMF_DEFAULTONLY | CMF_VERBSONLY | CMF_NOVERBS))
		return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

#ifdef DEBUG_OUTPUT
	if (hDebug){
		wsprintf(debug_buf, L"QueryContextMenu\r\nindexMenu = %u\r\nidCmdFirst = %u\r\nidCmdLast  = %u\r\nuFlags = 0x%X\r\n", indexMenu, idCmdFirst, idCmdLast, uFlags & 0xFFFF);
		DWORD write_size;
		if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
			CloseHandle(hDebug);
			hDebug = NULL;
		}
	}
#endif

	UINT idCmdMax = 0;
	HMENU hSubmenu = NULL;
	MENUITEMINFO mii;

	if ((menu_behavior < 0) && (load_setting() != 0)){	// 読み込むのは一回だけ
	//if (load_setting() != 0){	// read setting everytime for debug
		wcscpy(menu_item, L"&MultiPar");
		offset_c = (int)wcslen(menu_item) + 1;
		wcscpy(menu_item + offset_c, L"Create Recovery Files");
		if ((menu_behavior & 8) == 0){
			offset_v = offset_c + (int)wcslen(menu_item + offset_c) + 1;
			wcscpy(menu_item + offset_v, L"Verify Recovery File");
			if ((menu_behavior & 32) == 0){
				offset_a = offset_v + (int)wcslen(menu_item + offset_v) + 1;
				wcscpy(menu_item + offset_a, L"Archive and Create Recovery Files");
			}
		} else {
			offset_a = offset_v;
		}
	}

	if (menu_behavior & 1){	// 上に区切りを追加する
		UINT menu_state;

		if (menu_behavior & 2){	// 上下を区切る場合は QuickPar と同じ独立配置にする
			do {
				menu_state = GetMenuState(hMenu, indexMenu, MF_BYPOSITION);
				indexMenu++;
				if (menu_state & MF_SEPARATOR)
					break;
			} while (menu_state != -1);
		} else {	// 上にだけ区切りを付ける
			menu_state = GetMenuState(hMenu, indexMenu, MF_BYPOSITION);
			if ((menu_state & MF_SEPARATOR) == 0)	// まだ区切りがない場合だけ
				InsertMenu(hMenu, indexMenu++, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);
		}
	}

	// 右クリック・メニューに項目を追加する
	// ID = 0: Create or Verify
	// ID = 1: Verify
	// ID = 2: Archive and Create (and Append)
	ZeroMemory(&mii, sizeof(MENUITEMINFO));
	mii.cbSize = sizeof(MENUITEMINFO);
	if (idCmdLast - idCmdFirst >= 1){	// サブ・メニューを追加できるか
		if ((menu_behavior & 64) == 0)
			hSubmenu = CreatePopupMenu();
		if (idCmdLast - idCmdFirst < 3){
			menu_behavior |= 32;
			if (idCmdLast - idCmdFirst < 2)
				menu_behavior |= 8;
		}
	}
	if (hSubmenu == NULL){	// 選択肢無しでトップ・メニューだけにする
		mii.fMask = MIIM_ID | MIIM_STRING;
		mii.wID = idCmdFirst;
	} else {
		mii.fMask = MIIM_STRING | MIIM_SUBMENU;
		mii.hSubMenu = hSubmenu;
	}
	mii.dwTypeData = menu_item;
	if (((menu_behavior & 16) == 0) && (g_bmp != NULL)){	// メニューにアイコンを付ける
		mii.fMask |= MIIM_BITMAP;
		mii.hbmpItem = g_bmp;
	}
	if (hSubmenu != NULL){	// サブ・メニューを作る
		InsertMenu(hSubmenu, -1, MF_STRING | MF_BYPOSITION, idCmdFirst, menu_item + offset_c);
		if ((menu_behavior & (8 | 32)) == 0){
			if (menu_behavior & 4)
				InsertMenu(hSubmenu, -1, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);
			if ((menu_behavior & 16) || (g_bmp2 == NULL)){	// アイコンを表示しないなら
				InsertMenu(hSubmenu, -1, MF_STRING | MF_BYPOSITION, idCmdFirst + 2, menu_item + offset_a);
			} else {
				MENUITEMINFO mii2;
				ZeroMemory(&mii2, sizeof(MENUITEMINFO));
				mii2.cbSize = sizeof(MENUITEMINFO);
				mii2.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
				mii2.wID = idCmdFirst + 2;
				mii2.dwTypeData = menu_item + offset_a;
				mii2.hbmpItem = g_bmp2;
				InsertMenuItem(hSubmenu, -1, TRUE, &mii2);
			}
			idCmdMax = 2;
		}
		if ((single_file) && ((menu_behavior & 8) == 0)){
			if (menu_behavior & 4)
				InsertMenu(hSubmenu, -1, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);
			InsertMenu(hSubmenu, -1, MF_STRING | MF_BYPOSITION, idCmdFirst + 1, menu_item + offset_v);
			if (idCmdMax < 1)
				idCmdMax = 1;
		}
	}
	InsertMenuItem(hMenu, indexMenu++, TRUE, &mii);

	if (menu_behavior & 2)
		InsertMenu(hMenu, indexMenu++, MF_SEPARATOR | MF_BYPOSITION, 0, NULL);

	// Microsoft のページによって説明が異なる・・・でも、サンプル・コードは最大 offset +1 を返す
#ifdef DEBUG_OUTPUT
	if (hDebug){
		wsprintf(debug_buf, L"menu_behavior = %d\r\nidCmdMax = %u\r\n", menu_behavior, idCmdMax);
		DWORD write_size;
		if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
			CloseHandle(hDebug);
			hDebug = NULL;
		}
	}
#endif

	// How to Implement the IContextMenu Interface
	// https://docs.microsoft.com/en-us/windows/win32/shell/how-to-implement-the-icontextmenu-interface
	// 追加したコマンド ID の最大番号 + 1 を返す（ID は連番で無くてもいい）
	//return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(idCmdFirst + idCmdMax + 1));

	// Shobjidl.h  (Windows XP or later)
	// https://docs.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-querycontextmenu
	// 追加したコマンド ID の最大番号 - idCmdFirst + 1 を返す（ID は連番で無くてもいい）
	return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(idCmdMax + 1));
}

STDMETHODIMP CShellExtension::InvokeCommand(LPCMINVOKECOMMANDINFO lpcmi)
{
	// HIWORD(lpcmi->lpVerb)が0の時だけ処理する。
	if (HIWORD(lpcmi->lpVerb) == 0){
		// LOWORD(lpcmi->lpVerb) はクリックされたメニューIDです。
		// これは QueryContextMenu() の InsertMenu で指定した コマンド ID - idCmdFirst です。
		UINT idCmd = LOWORD(lpcmi->lpVerb);

		//wsprintf(menu_item + 128, L"ID = %d", idCmd);
		//MessageBox(lpcmi->hwnd, NULL, menu_item + 128, MB_OK);

		if (idCmd <= 1)
			return DoCommand(idCmd);
		if (idCmd == 2)
			return DoCommand7zip();
	}
	return E_INVALIDARG;
}

// Windows 2000/XP には非対応なのでヘルプのテキストは表示しない
STDMETHODIMP CShellExtension::GetCommandString(
	UINT_PTR idCmd, UINT uFlags, UINT FAR *reserved, LPSTR pszName, UINT cchMax)
{
	if (idCmd > 2)	// コマンド番号が範囲外
		return E_FAIL;

	if (uFlags == GCS_VALIDATEW){
		return S_OK;
	} else if (uFlags == GCS_VERBW){
		int offset;
		if (idCmd == 1){	// Verify
			offset = offset_v;
		} if (idCmd == 2){	// Archive
			offset = offset_a;
		} else {	// Create or Verify
			offset = offset_c;
		}
		lstrcpyn((LPWSTR)pszName, menu_item + offset, cchMax);
	} else {
		return E_INVALIDARG;
	}

	return NOERROR;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 取得データを検査する
// S_OK 以外を返すとメニューを表示しない
int CShellExtension::CheckData(void){
	// 以下のコードでまず、HDROPを得る
	FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM stg = { TYMED_HGLOBAL };
	HDROP     hDrop;

	if (FAILED(m_pDataObj->GetData(&fmt, &stg)))
		return E_INVALIDARG;

	// HDROPを取得
	hDrop = (HDROP)GlobalLock(stg.hGlobal);
	if (hDrop == NULL)
		return E_INVALIDARG;

	// ファイル数チェック
	UINT uNumFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

#ifdef DEBUG_OUTPUT
	if (hDebug){
		wsprintf(debug_buf, L"uNumFiles = %u\r\n", uNumFiles);
		DWORD write_size;
		if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
			CloseHandle(hDebug);
			hDebug = NULL;
		}
	}
#endif

	// ファイルが0個なら帰る(一応チェック)
	if (uNumFiles == 0){
		GlobalUnlock(stg.hGlobal);
		ReleaseStgMedium(&stg);
		return E_INVALIDARG;
	}

#ifdef DEBUG_OUTPUT
	if (hDebug){
		// 最初のファイルのパスを記録する
		UINT req = DragQueryFile(hDrop, 0, NULL, 0);
		wsprintf(debug_buf, L"first_path_length = %u\r\nfirst_path =\r\n", req);
		DWORD write_size;
		if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
			CloseHandle(hDebug);
			hDebug = NULL;
		}

		// 文字数を多めに確保しておくこと
		if ((hDebug != NULL) && (req < _countof(debug_buf))){
			if (DragQueryFile(hDrop, 0, debug_buf, _countof(debug_buf)) > 0){
				if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
					CloseHandle(hDebug);
					hDebug = NULL;
				}
			}
		}
		if (hDebug){
			wcscpy(debug_buf, L"\r\n");
			if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
				CloseHandle(hDebug);
				hDebug = NULL;
			}
		}
	}
#endif

	single_file = 0;
	if (uNumFiles == 1){	// ファイルが一個なら
		wchar_t buf[MAX_PATH + 32];
		unsigned int attr;

		if (DragQueryFile(hDrop, 0, buf, _countof(buf)) == 0){
			GlobalUnlock(stg.hGlobal);
			ReleaseStgMedium(&stg);
			return E_INVALIDARG;
		}

		// ファイルの属性を検査する
		attr = GetFileAttributes(buf);
		if (((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) &&
			((attr & FILE_ATTRIBUTE_SYSTEM) == 0)){
			single_file = 1;
		}
	}
	GlobalUnlock(stg.hGlobal);
	ReleaseStgMedium(&stg);

#ifdef DEBUG_OUTPUT
	if (hDebug){
		wsprintf(debug_buf, L"single_file = %d\r\n", single_file);
		DWORD write_size;
		if (!WriteFile(hDebug, debug_buf, (DWORD)wcslen(debug_buf) * 2, &write_size, NULL)){
			CloseHandle(hDebug);
			hDebug = NULL;
		}
	}
#endif

	return S_OK;
}

int CShellExtension::DoCommand(UINT idCmd){
	wchar_t path[MAX_PATH], buf[MAX_PATH + 32];
	unsigned int i, len, uNumFiles;

	// DLL が存在するディレクトリ
	// DLL と呼び出す EXE ファイルは同じディレクトリに置くこと
	len = GetModuleFileName(g_inst, path, MAX_PATH);
	if ((len == 0) || (len >= MAX_PATH))
		return E_INVALIDARG;
	for (i = len - 1; i > 0; i--){
		if (path[i] == '\\'){
			path[i] = 0;
			break;
		}
	}
	//MessageBox(NULL, path , L"DLL path", MB_OK);

	// 以下のコードでまず、HDROPを得る
	FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM stg = { TYMED_HGLOBAL };
	HDROP     hDrop;

	if (FAILED(m_pDataObj->GetData(&fmt, &stg)))
		return E_INVALIDARG;

	// HDROPを取得
	hDrop = (HDROP)GlobalLock(stg.hGlobal);
	if (hDrop == NULL)
		return E_INVALIDARG;
	uNumFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);	// ファイル数

	// 検査なら
	if ((idCmd == 1) || ((idCmd == 0) && (uNumFiles == 1))){
		wchar_t argvs[MAX_PATH + 16];

		// コマンドラインを作る
		if (idCmd == 1){
			wcscpy(argvs, L"/verify ");
		} else {
			argvs[0] = 0;
		}

		// 選択されたファイル名の取得
		if (DragQueryFile(hDrop, 0, buf, _countof(buf)) == 0){
			GlobalUnlock(stg.hGlobal);
			ReleaseStgMedium(&stg);
			return E_INVALIDARG;
		}

		// コマンドラインに追加する
		if (wcschr(buf, ' ') != NULL){	// スペースを含む場合は"で囲む
			wcscat(argvs, L"\"");
			wcscat(argvs, buf);
			wcscat(argvs, L"\"");
		} else {
			wcscat(argvs, buf);
		}

		GlobalUnlock(stg.hGlobal);
		ReleaseStgMedium(&stg);

		//MessageBox(NULL, argvs, L"param command", MB_OK);
		ShellExecute(NULL, L"open", L"MultiPar.exe", argvs, path, SW_SHOWNORMAL);
		return NOERROR;
	}

	// ファイル数チェック
	len = 0;
	for (i = 0; i < uNumFiles; i++){
		// 選択されたファイル名の文字数
		len += DragQueryFile(hDrop, i, NULL, 0);
	}
	len += uNumFiles * 3 + 8;	// コマンドと "" の分だけ余裕を見ておく

	// 文字数が多いならファイル・リストを使う
	if (len >= 32768){
		char buf2[MAX_PATH * 3];
		wchar_t list_path[MAX_PATH];
		unsigned long rv;
		HANDLE hFile;

		// アプリケーション・データのディレクトリを決める
		rv = 0;
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, list_path))){
			//MessageBox(NULL, list_path, L"program files path", MB_OK);
			// ProgramFiles の位置と比較する
			int i, max = 0;
			while (list_path[max] != 0)
				max++;
			for (i = 0; i < max; i++){
				if (list_path[i] != path[i])
					break;
			}
			if (i == max){
				if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, list_path))){
					//MessageBox(NULL, list_path, L"application data path", MB_OK);
					i = 0;
					while (list_path[i] != 0)
						i++;
					if (i < MAX_PATH - 10 - 18){
						wcscat(list_path, L"\\MultiPar");
						rv = 2;	// AppData を使う
					}
				}
			}
		}
		if (rv != 2)
			wcscpy(list_path, path);	// DLL が存在するディレクトリを使う
		wcscat(list_path, L"\\MultiPar_list.tmp");
		//MessageBox(NULL, list_path, L"file-list path", MB_OK);

		// テキスト・ファイルを開く
		hFile = CreateFile(list_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE){
			GlobalUnlock(stg.hGlobal);
			ReleaseStgMedium(&stg);
			return E_INVALIDARG;
		}

		// ファイルごとに記録する
		for (i = 0; i < uNumFiles; i++){
			// 選択されたファイル名の取得
			if (DragQueryFile(hDrop, i, buf, _countof(buf)) == 0){
				GlobalUnlock(stg.hGlobal);
				ReleaseStgMedium(&stg);
				CloseHandle(hFile);
				return E_INVALIDARG;
			}

			// UTF-8 に変換する
			len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, buf2, MAX_PATH * 3, NULL, NULL);
			if (len > 0){	// len は末尾の null 文字を含む
				buf2[len - 1] = 0x0A;	// 末尾に改行を追加する
				if (!WriteFile(hFile, buf2, len, &rv, NULL)){
					GlobalUnlock(stg.hGlobal);
					ReleaseStgMedium(&stg);
					CloseHandle(hFile);
				}
			}
		}
		GlobalUnlock(stg.hGlobal);
		ReleaseStgMedium(&stg);

		// テキスト・ファイルを閉じる
		CloseHandle(hFile);

		// コマンドラインを作る（先頭の空白は実行ファイルのパスの代わり）
		wcscpy(buf, L" /create /list ");
		if (wcschr(list_path, ' ') != NULL){	// スペースを含む場合は"で囲む
			wcscat(buf, L"\"");
			wcscat(buf, list_path);
			wcscat(buf, L"\"");
		} else {
			wcscat(buf, list_path);
		}
		//MessageBox(NULL, buf, L"file-list command", MB_OK);
		ShellExecute(NULL, L"open", L"MultiPar.exe", buf, path, SW_SHOWNORMAL);

	} else {	// ファイル数が少ないならコマンドラインで渡す
		wchar_t argvs[32768];
		STARTUPINFO si;
		PROCESS_INFORMATION pi;

		// コマンドラインを作る（先頭の空白は実行ファイルのパスの代わり）
		wcscpy(argvs, L" /create");

		// ファイルごとに記録する
		for (i = 0; i < uNumFiles; i++){
			// 選択されたファイル名の取得
			if (DragQueryFile(hDrop, i, buf, _countof(buf)) == 0){
				GlobalUnlock(stg.hGlobal);
				ReleaseStgMedium(&stg);
				return E_INVALIDARG;
			}

			// コマンドラインに追加していく
			if (wcschr(buf, ' ') != NULL){	// スペースを含む場合は"で囲む
				wcscat(argvs, L" \"");
				wcscat(argvs, buf);
				wcscat(argvs, L"\"");
			} else {
				wcscat(argvs, L" ");
				wcscat(argvs, buf);
			}
		}
		GlobalUnlock(stg.hGlobal);
		ReleaseStgMedium(&stg);

		//len = wcslen(argvs);
		//wsprintf(buf, L"length = %d", len);
		//MessageBox(NULL, buf, L"param length", MB_OK);
		//MessageBox(NULL, argvs, L"param command", MB_OK);
		// Windows 2000 だと ShellExecute のコマンドラインは 2000文字ぐらいまでなので、パスが長いと無理
		// ShellExecute(NULL, L"open", L"MultiPar.exe", argvs, path, SW_SHOWNORMAL);
		// CreateProcess の引数は 32768文字ぐらいまでいける。
		wcscat(path, L"\\MultiPar.exe");
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOWNORMAL;
		if (CreateProcess(path, argvs, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)){
			// スレッドハンドルとプロセスハンドルの解放
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		}
	}

	return NOERROR;
}

int CShellExtension::DoCommand7zip(void){
	wchar_t path[MAX_PATH], buf[MAX_PATH], argvs[32768];
	unsigned int i, len, max, uNumFiles;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	// DLL が存在するディレクトリ
	// DLL と呼び出す EXE ファイルは同じディレクトリに置くこと
	len = GetModuleFileName(g_inst, path, MAX_PATH);
	if ((len == 0) || (len >= MAX_PATH))
		return E_INVALIDARG;
	for (i = len - 1; i > 0; i--){
		if (path[i] == '\\'){
			path[i] = 0;
			break;
		}
	}
	//MessageBox(NULL, path , L"DLL path", MB_OK);

	get_path_7zip(buf);	// 7-Zip のディレクトリを取得する
	if (buf[0] == 0){
		menu_behavior |= 32;	// 次からメニューを表示しない
		//MessageBoxA(GetDesktopWindow(), "Cannot find 7-Zip." , "MultiPar shell extension", MB_OK | MB_ICONERROR);
		return E_INVALIDARG;
	}

	// 以下のコードでまず、HDROPを得る
	FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM stg = { TYMED_HGLOBAL };
	HDROP     hDrop;

	if (FAILED(m_pDataObj->GetData(&fmt, &stg)))
		return E_INVALIDARG;

	// HDROPを取得
	hDrop = (HDROP)GlobalLock(stg.hGlobal);
	if (hDrop == NULL)
		return E_INVALIDARG;
	uNumFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);	// ファイル数

	// コマンドラインを作る
	if (wcschr(buf, ' ') != NULL){	// スペースを含む場合は"で囲む
		wcscpy(argvs, L"\"");
		wcscat(argvs, buf);
		wcscat(argvs, L"\"");
	} else {
		wcscpy(argvs, buf);
	}
	// 「-saa」を追加すると書庫ファイルを別の形式で圧縮できるようになる
	// 「--」を追加するとファイル名の先頭が「-」でもオプション扱いにならない
	//wcscat(argvs, L" a -ad -saa -- ");
	wcscat(argvs, L" /archive:7zip ");	// MultiPar 用のコマンドに置き換える
	// 選択された最初のファイル名
	if (DragQueryFile(hDrop, 0, buf, _countof(buf)) == 0){
		GlobalUnlock(stg.hGlobal);
		ReleaseStgMedium(&stg);
		return E_INVALIDARG;
	}
	len = (unsigned int)wcslen(buf);
	if (uNumFiles > 1){	// ファイルが複数ならその親フォルダーの名前を書庫に付ける
		for (i = len - 1; i > 0; i--){
			if (buf[i] == '\\'){	// 親フォルダー名の末尾
				max = i;
				for (i = i - 1; i > 0; i--){
					if (buf[i] == '\\'){	// 親フォルダー名の先頭
						for (len = 1; i + len < max; len++)
							buf[max + len] = buf[i + len];
						buf[max + len] = 0;
						break;
					}
				}
				break;
			}
		}
	}
	// コマンドラインに書庫名を追加する
	if (wcschr(buf, ' ') != NULL){	// スペースを含む場合は"で囲む
		wcscat(argvs, L"\"");
		wcscat(argvs, buf);
		wcscat(argvs, L"\"");
	} else {
		wcscat(argvs, buf);
	}

	// ファイル名の文字数チェック
	len = 0;
	for (i = 0; i < uNumFiles; i++){
		// 選択されたファイル名の文字数
		len += DragQueryFile(hDrop, i, NULL, 0);
	}
	len += uNumFiles * 3 + (unsigned int)wcslen(argvs);	// コマンドと "" の分だけ余裕を見ておく

	// 文字数が多いならファイル・リストを使う
	if (len >= 32768){
		char buf2[MAX_PATH * 3];
		wchar_t list_path[MAX_PATH];
		unsigned long rv;
		HANDLE hFile;

		// アプリケーション・データのディレクトリを決める
		rv = 0;
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, list_path))){
			//MessageBox(NULL, list_path, L"program files path", MB_OK);
			// ProgramFiles の位置と比較する
			max = 0;
			while (list_path[max] != 0)
				max++;
			for (i = 0; i < max; i++){
				if (list_path[i] != path[i])
					break;
			}
			if (i == max){
				if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, list_path))){
					//MessageBox(NULL, list_path, L"application data path", MB_OK);
					i = 0;
					while (list_path[i] != 0)
						i++;
					if (i < MAX_PATH - 10 - 18){
						wcscat(list_path, L"\\MultiPar");
						rv = 2;	// AppData を使う
					}
				}
			}
		}
		if (rv != 2)
			wcscpy(list_path, path);	// DLL が存在するディレクトリを使う
		wcscat(list_path, L"\\MultiPar_list.tmp");
		//MessageBox(NULL, list_path, L"file-list path", MB_OK);

		// テキスト・ファイルを開く
		hFile = CreateFile(list_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE){
			GlobalUnlock(stg.hGlobal);
			ReleaseStgMedium(&stg);
			return E_INVALIDARG;
		}

		// ファイルごとに記録する
		for (i = 0; i < uNumFiles; i++){
			// 選択されたファイル名の取得
			if (DragQueryFile(hDrop, i, buf, _countof(buf)) == 0){
				GlobalUnlock(stg.hGlobal);
				ReleaseStgMedium(&stg);
				CloseHandle(hFile);
				return E_INVALIDARG;
			}

			// UTF-8 に変換する
			len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, buf2, MAX_PATH * 3, NULL, NULL);
			if (len > 0){	// len は末尾の null 文字を含む
				buf2[len - 1] = 0x0A;	// 末尾に改行を追加する
				if (!WriteFile(hFile, buf2, len, &rv, NULL)){
					GlobalUnlock(stg.hGlobal);
					ReleaseStgMedium(&stg);
					CloseHandle(hFile);
				}
			}
		}
		GlobalUnlock(stg.hGlobal);
		ReleaseStgMedium(&stg);

		// テキスト・ファイルを閉じる
		CloseHandle(hFile);

		// コマンドラインにファイル・リスト名を追加する
		wcscat(argvs, L" @\"");
		wcscat(argvs, list_path);
		wcscat(argvs, L"\"");

	} else {	// ファイル数が少ないならコマンドラインで渡す
		// ファイルごとに記録する
		for (i = 0; i < uNumFiles; i++){
			// 選択されたファイル名の取得
			if (DragQueryFile(hDrop, i, buf, _countof(buf)) == 0){
				GlobalUnlock(stg.hGlobal);
				ReleaseStgMedium(&stg);
				return E_INVALIDARG;
			}

			// コマンドラインに追加していく
			if (wcschr(buf, ' ') != NULL){	// スペースを含む場合は"で囲む
				wcscat(argvs, L" \"");
				wcscat(argvs, buf);
				wcscat(argvs, L"\"");
			} else {
				wcscat(argvs, L" ");
				wcscat(argvs, buf);
			}
		}
		GlobalUnlock(stg.hGlobal);
		ReleaseStgMedium(&stg);
	}

	// 書庫ファイルを MultiPar で開く
	//MessageBox(NULL, argvs, L"command param", MB_OK);
	wcscat(path, L"\\MultiPar.exe");
	ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOWNORMAL;
	if (CreateProcess(path, argvs, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)){
		// スレッドハンドルとプロセスハンドルの解放
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}

	return NOERROR;
}

