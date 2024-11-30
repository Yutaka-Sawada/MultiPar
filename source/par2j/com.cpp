// com.c
// Copyright : 2024-11-30 Yutaka Sawada
// License : GPL

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601	// Windows 7 or later
#endif

#include <windows.h>
#include <Shobjidl.h>

extern "C" {	// C 言語の関数呼び出しに対応する

/*
Sample code
https://docs.microsoft.com/ja-jp/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperation-copyitem

HRESULT CopyItem(__in PCWSTR pszSrcItem, __in PCWSTR pszDest, PCWSTR pszNewName)
{
	//
	// Initialize COM as STA.
	//
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); 
	if (SUCCEEDED(hr))
	{
		IFileOperation *pfo;

		//
		// Create the IFileOperation interface 
		//
		hr = CoCreateInstance(CLSID_FileOperation, 
							NULL, 
							CLSCTX_ALL, 
							IID_PPV_ARGS(&pfo));
		if (SUCCEEDED(hr))
		{
			//
			// Set the operation flags. Turn off all UI from being shown to the
			// user during the operation. This includes error, confirmation,
			// and progress dialogs.
			//
			hr = pfo->SetOperationFlags(FOF_NO_UI);
			if (SUCCEEDED(hr))
			{
				//
				// Create an IShellItem from the supplied source path.
				//
				IShellItem *psiFrom = NULL;
				hr = SHCreateItemFromParsingName(pszSrcItem, 
												NULL, 
												IID_PPV_ARGS(&psiFrom));
				if (SUCCEEDED(hr))
				{
					IShellItem *psiTo = NULL;

					if (NULL != pszDest)
					{
						//
						// Create an IShellItem from the supplied 
						// destination path.
						//
						hr = SHCreateItemFromParsingName(pszDest, 
														NULL,
														IID_PPV_ARGS(&psiTo));
					}

					if (SUCCEEDED(hr))
					{
						//
						// Add the operation
						//
						hr = pfo->CopyItem(psiFrom, psiTo, pszNewName, NULL);

						if (NULL != psiTo)
						{
							psiTo->Release();
						}
					}

					psiFrom->Release();
				}

				if (SUCCEEDED(hr))
				{
					//
					// Perform the operation to copy the file.
					//
					hr = pfo->PerformOperations();
				}
			}

			//
			// Release the IFileOperation interface.
			//
			pfo->Release();
		}

		CoUninitialize();
	}
	return hr;
}
*/

HRESULT DeleteItem(__in PCWSTR pszSrcItem)
{
	//
	// Initialize COM as STA.
	//
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); 
	if (SUCCEEDED(hr))
	{
		IFileOperation *pfo;

		//
		// Create the IFileOperation interface 
		//
		hr = CoCreateInstance(CLSID_FileOperation, 
							NULL, 
							CLSCTX_ALL, 
							IID_PPV_ARGS(&pfo));
		if (SUCCEEDED(hr))
		{
			//
			// Set the operation flags. Turn off all UI from being shown to the
			// user during the operation. This includes error, confirmation,
			// and progress dialogs.
			//
			hr = pfo->SetOperationFlags(FOF_ALLOWUNDO | FOF_NO_UI);
			if (SUCCEEDED(hr))
			{
				//
				// Create an IShellItem from the supplied source path.
				//
				IShellItem *psiFrom = NULL;
				hr = SHCreateItemFromParsingName(pszSrcItem, 
												NULL, 
												IID_PPV_ARGS(&psiFrom));
				if (SUCCEEDED(hr))
				{
					//
					// Add the operation
					//
					hr = pfo->DeleteItem(psiFrom, NULL);

					psiFrom->Release();
				}

				if (SUCCEEDED(hr))
				{
					//
					// Perform the operation to delete the file.
					//
					hr = pfo->PerformOperations();
				}
			}

			//
			// Release the IFileOperation interface.
			//
			pfo->Release();
		}

		CoUninitialize();
	}
	return hr;
}


}	// end of extern "C"

