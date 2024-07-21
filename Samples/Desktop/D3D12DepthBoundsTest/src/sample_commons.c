#include <stdio.h>
#include "sample_commons.h"
#include "d3d12.h"
#include <dxgi1_6.h>
#include "dxheaders/d3dx12_macros.h"

void ExitIfFailed(const HRESULT hr)
{
	if (FAILED(hr)) {
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
			SUCCEEDED(CALL(EnumAdapterByGpuPreference, factory6, adapterIndex, gpuPreference, IID_PPV_ARGS(&adapter)));
			++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			CALL(GetDesc1, adapter, &desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Software adapter
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the actual device yet
			IUnknown* adapterAsIUnknown = NULL;
			ExitIfFailed(CAST(adapter, adapterAsIUnknown));
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
		for (UINT adapterIndex = 0; SUCCEEDED(CALL(EnumAdapters1, pFactory, adapterIndex, &adapter)); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			CALL(GetDesc1, adapter, &desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the actual device yet
			IUnknown* adapterAsIUnknown = NULL;
			ExitIfFailed(CAST(adapter, adapterAsIUnknown));
			if (SUCCEEDED(D3D12CreateDevice(adapterAsIUnknown, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL)))
			{
				RELEASE(adapterAsIUnknown);
				break;
			}
		}
	}
	*ppAdapter = adapter;
}