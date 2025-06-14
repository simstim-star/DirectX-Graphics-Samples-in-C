#pragma once

#include "windows.h"
#include "dxgi.h"
#include "stdint.h"

void LogErrAndExit(const HRESULT hr);
void GetCurrentPath(_Out_writes_(pathSize) WCHAR* const path, UINT pathSize);
void GetHardwareAdapter(IDXGIFactory1* const pFactory, IDXGIAdapter1** ppAdapter, BOOL requestHighPerformanceAdapter);
HRESULT ReadDataFromFile(LPCWSTR filename, byte** data, UINT* size);