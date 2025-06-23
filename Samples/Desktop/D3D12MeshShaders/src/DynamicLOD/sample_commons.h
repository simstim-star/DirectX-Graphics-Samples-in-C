#pragma once

#include "windows.h"
#include "dxgi.h"
#include "stdint.h"

void LogErrAndExit(const HRESULT hr);
void GetCurrentPath(_Out_writes_(pathSize) WCHAR* const path, UINT pathSize);
void GetHardwareAdapter(IDXGIFactory1* const pFactory, IDXGIAdapter1** ppAdapter, BOOL requestHighPerformanceAdapter);
HRESULT ReadDataFromFile(LPCWSTR filename, byte** data, UINT* size);

int DivRoundUp_int(int num, int denom);
uint32_t DivRoundUp_uint32(uint32_t num, uint32_t denom);
