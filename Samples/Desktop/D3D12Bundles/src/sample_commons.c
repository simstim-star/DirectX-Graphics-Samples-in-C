#define COBJMACROS

#include <stdio.h>
#include "sample_commons.h"
#include "d3d12.h"
#include <dxgi1_6.h>
#include "dxheaders/d3dx12_macros.h"

void LogAndExit(const HRESULT hr)
{
	if (FAILED(hr)) { // remove this check once every usage is refactored
		char s_str[64] = "";
		if (snprintf(s_str, 64, "ERROR: HRESULT 0x%08X\n", (UINT)hr) > 0) {
			OutputDebugString(s_str);
		}
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************************************************
	Retrieves the path of the executable file of the current process with a last slash ('\\') appended in the end.
	Example: C:\\path\\to\\my\\executable.exe\\
*******************************************************************************************************************/
void GetCurrentPath(_Out_writes_(pathSize) WCHAR* const path, UINT pathSize)
{
	if (path == NULL)
	{
		OutputDebugString("Assets path is NULL \n");
		exit(EXIT_FAILURE);
	}

	DWORD size = GetModuleFileNameW(NULL, path, pathSize);
	if (size == 0 || size == pathSize)
	{
		exit(EXIT_FAILURE);
	}

	WCHAR* lastSlash = wcsrchr(path, L'\\');
	if (lastSlash)
	{
		*(lastSlash + 1) = L'\0';
	}
}

void GetHardwareAdapter(
	IDXGIFactory1* const  pFactory,
	IDXGIAdapter1** ppAdapter,
	bool requestHighPerformanceAdapter)
{
	*ppAdapter = NULL;
	IDXGIAdapter1* adapter = NULL;

	// IDXGIFactory6 has EnumAdapterByGpuPreference
	IDXGIFactory6* factory6 = NULL;
	if (SUCCEEDED(CAST(pFactory, factory6)))
	{
		DXGI_GPU_PREFERENCE gpuPreference = requestHighPerformanceAdapter ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED;
		for (UINT adapterIndex = 0;
			SUCCEEDED(
				IDXGIFactory6_EnumAdapterByGpuPreference(
					factory6, 
					adapterIndex, 
					gpuPreference, 
					__IID(&adapter), 
					(void**) &adapter)
				);
			++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			IDXGIAdapter1_GetDesc1(adapter, &desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Software adapter
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the actual device yet
			IUnknown* adapterAsIUnknown = NULL;
			LogAndExit(CAST(adapter, adapterAsIUnknown));
			if (SUCCEEDED(D3D12CreateDevice(adapterAsIUnknown, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL)))
			{
				RELEASE(adapterAsIUnknown);
				break;
			}
		}
		RELEASE(factory6);
	}

	// Will enter here if your factory can't query a Factory6
	if (adapter == NULL)
	{
		for (UINT adapterIndex = 0; 
			SUCCEEDED(IDXGIFactory1_EnumAdapters1(pFactory, adapterIndex, &adapter)); 
			++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			IDXGIAdapter1_GetDesc1(adapter, &desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the actual device yet
			IUnknown* adapterAsIUnknown = NULL;
			LogAndExit(CAST(adapter, adapterAsIUnknown));
			if (SUCCEEDED(D3D12CreateDevice(adapterAsIUnknown, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL)))
			{
				RELEASE(adapterAsIUnknown);
				break;
			}
		}
	}
	*ppAdapter = adapter;
}

HRESULT ReadDataFromFile(LPCWSTR filename, byte** data, UINT* size)
{
#if WINVER >= _WIN32_WINNT_WIN8
	CREATEFILE2_EXTENDED_PARAMETERS extendedParams = {
		.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS),
		.dwFileAttributes = FILE_ATTRIBUTE_NORMAL,
		.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN,
		.dwSecurityQosFlags = SECURITY_ANONYMOUS,
		.lpSecurityAttributes = NULL,
		.hTemplateFile = NULL,
	};

	HANDLE file = CreateFile2(filename, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &extendedParams);
#else
	HANDLE file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, NULL));
#endif
	if (file == INVALID_HANDLE_VALUE)
	{
		return E_FAIL;
	}

	FILE_STANDARD_INFO fileInfo = {0};
	if (!GetFileInformationByHandleEx(file, FileStandardInfo, &fileInfo, sizeof(fileInfo)))
	{
		return E_FAIL;
	}

	if (fileInfo.EndOfFile.HighPart != 0)
	{
		return E_FAIL;
	}

	*data = HeapAlloc(GetProcessHeap(), 0, fileInfo.EndOfFile.LowPart);
	*size = fileInfo.EndOfFile.LowPart;

	if (!ReadFile(file, *data, fileInfo.EndOfFile.LowPart, NULL, NULL))
	{
		return E_FAIL;
	}

	return S_OK;
}

void CleanAllocatedDataFromFile(byte* data) {
	HeapFree(GetProcessHeap(), 0, data);
}
