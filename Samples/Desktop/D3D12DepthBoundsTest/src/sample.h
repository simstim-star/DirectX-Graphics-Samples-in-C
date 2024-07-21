#pragma once

#include <windows.h>
#include <stdbool.h>
#include <d3d12.h>

#define FrameCount 2

typedef struct IDXGISwapChain3 IDXGISwapChain3;

typedef struct DXSample
{
    UINT width;
    UINT height;
    float aspectRatio;
    // Window title.
    CHAR* title;
    // Root assets path.
    WCHAR assetsPath[512];

    // Pipeline objects.
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;
    IDXGISwapChain3* swapChain;
    ID3D12Device2* device;
    ID3D12Resource* depthStencil;
    ID3D12Resource* renderTargets[FrameCount];
    ID3D12CommandAllocator* commandAllocator;
    ID3D12CommandQueue* commandQueue;
    ID3D12RootSignature* rootSignature;
    ID3D12DescriptorHeap* rtvHeap;
    ID3D12DescriptorHeap* dsvHeap;
    ID3D12PipelineState* pipelineState;
    ID3D12PipelineState* depthOnlyPipelineState;
    ID3D12GraphicsCommandList1* commandList;
    UINT rtvDescriptorSize;
    bool DepthBoundsTestSupported;

    // App resources.
    ID3D12Resource *vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    // Synchronization objects.
    UINT frameIndex;
    UINT frameNumber;
    HANDLE fenceEvent;
    ID3D12Fence *fence;
    UINT64 fenceValue;
} DXSample;


void Sample_Init(DXSample* const sample);
void Sample_Destroy(DXSample* const sample);
void Sample_Update(DXSample* const sample);
void Sample_Render(DXSample* const sample);