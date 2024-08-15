#pragma once

#include "windows.h"
#include <va/va.h>

typedef struct float3 { float x; float y;  float z; } float3;
typedef struct float4 { float x; float y; float z; float w; } float4;
typedef struct Vertex { float3 position; float4 color; } Vertex;

typedef struct IDXGIFactory1 IDXGIFactory1;
typedef struct IDXGIAdapter1 IDXGIAdapter1;

void ExitIfFailed(const HRESULT hr);
void VAExitIfFailed(VAStatus va_status, const char* func);
void GetCurrentPath(_Out_writes_(pathSize) WCHAR* const path, UINT pathSize);
void GetHardwareAdapter(IDXGIFactory1* const pFactory, IDXGIAdapter1** ppAdapter, BOOL requestHighPerformanceAdapter);
