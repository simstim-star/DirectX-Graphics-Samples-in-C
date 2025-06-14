#pragma once

// A problem is that many of these IID are in different headers, but to use this macro, all of them should be included
#define IID(X) _Generic((X), \
              ID3D12Device **: &IID_ID3D12Device, \
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
              ID3D12DescriptorHeap **: &IID_ID3D12DescriptorHeap, \
              ID3D12CommandAllocator **: &IID_ID3D12CommandAllocator, \
              IDXGISwapChain1 **: &IID_IDXGISwapChain1, \
              IDXGISwapChain3 **: &IID_IDXGISwapChain3, \
              ID3DBlob **: &IID_ID3DBlob, \
              IUnknown **: &IID_IUnknown, \
              ID3D12Fence **: &IID_ID3D12Fence)

#define CAST(from, to) ID3D12Object_QueryInterface(from, IID(&to), &to)

// Calls Release and makes the pointer NULL if RefCount reaches zero
#define RELEASE(ptr) if(ptr && ID3D12Object_Release(ptr) == 0) ptr = NULL