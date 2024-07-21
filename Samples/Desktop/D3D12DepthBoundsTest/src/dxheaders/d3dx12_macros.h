#pragma once

// A problem is that many of these IID are in different headers, but to use this macro, all of them should be included
#define __extractIID(X) _Generic((X), \
              ID3D12Device **: &IID_ID3D12Device, \
              ID3D12Device2 **: &IID_ID3D12Device2, \
              ID3D12Debug **: &IID_ID3D12Debug, \
              IDXGIAdapter1 **: &IID_IDXGIAdapter1, \
              IDXGIFactory1 **: &IID_IDXGIFactory1, \
              IDXGIFactory4 **: &IID_IDXGIFactory4, \
              IDXGIFactory6 **: &IID_IDXGIFactory6, \
              ID3D12CommandQueue **: &IID_ID3D12CommandQueue, \
              ID3D12CommandList **: &IID_ID3D12CommandList, \
              ID3D12Resource **: &IID_ID3D12Resource, \
              ID3D12RootSignature **: &IID_ID3D12RootSignature, \
              ID3D12PipelineState **: &IID_ID3D12PipelineState, \
              ID3D12GraphicsCommandList **: &IID_ID3D12GraphicsCommandList, \
              ID3D12GraphicsCommandList1 **: &IID_ID3D12GraphicsCommandList1, \
              ID3D12DescriptorHeap **: &IID_ID3D12DescriptorHeap, \
              ID3D12CommandAllocator **: &IID_ID3D12CommandAllocator, \
              IDXGISwapChain1 **: &IID_IDXGISwapChain1, \
              IDXGISwapChain3 **: &IID_IDXGISwapChain3, \
              ID3DBlob **: &IID_ID3DBlob, \
              IUnknown **: &IID_IUnknown, \
              ID3D12Object **: &IID_ID3D12Object, \
              ID3D12Fence **: &IID_ID3D12Fence)

#define IID_PPV_ARGS(ppType) __extractIID(ppType), (void**)(ppType)

// The ## __VA_ARGS__ is not portable, being a GCC extension https://gcc.gnu.org/onlinedocs/gcc/Variadic-Macros.html
// But apparently, it also works in MSVC as well
#define CALL(METHOD, CALLER, ...) CALLER->lpVtbl->METHOD(CALLER, ## __VA_ARGS__)

// cast COM style
#define CAST(from, to) CALL(QueryInterface, from, IID_PPV_ARGS(&to))

// Calls Release and makes the pointer NULL if RefCount reaches zero
#define RELEASE(ptr) if(ptr && ptr->lpVtbl->Release(ptr) == 0) ptr = NULL

// Assign a name to the object to aid with debugging.
#if defined(_DEBUG) || defined(DBG)
#define NAME_D3D12_OBJECT(x) CALL(SetName, x, L"" #x);                        
#define NAME_D3D12_OBJECT_INDEXED(x, i)  {\
                WCHAR fullName[50];\
                if (swprintf_s(fullName, 50, L"%s[%u]", L"" #x, (UINT)i) > 0) CALL(SetName, x[i], fullName);\
               }
#else
#define NAME_D3D12_OBJECT(x)
#define NAME_D3D12_OBJECT_INDEXED(x, n)
#endif