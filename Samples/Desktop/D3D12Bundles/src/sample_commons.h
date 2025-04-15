#pragma once

#include <windows.h>
#include <stdbool.h>

typedef struct IDXGIFactory1 IDXGIFactory1;
typedef struct IDXGIAdapter1 IDXGIAdapter1;
typedef struct ID3D12Object ID3D12Object;

typedef struct float3 { float x; float y;  float z; } float3;
typedef struct float4 { float x; float y; float z; float w; } float4;
typedef struct Vertex { float3 position; float4 color; } Vertex;


void LogAndExit(const HRESULT hr);
void GetCurrentPath(_Out_writes_(pathSize) WCHAR* const path, UINT pathSize);
void GetHardwareAdapter(IDXGIFactory1* const pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter);

HRESULT ReadDataFromFile(LPCWSTR filename, byte** data, UINT* size);
void CleanAllocatedDataFromFile(byte* data);