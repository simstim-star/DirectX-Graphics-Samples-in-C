#pragma once

#include <windows.h>
#include <stdbool.h>
#include <d3d12.h>
#include "simple_camera.h"
#include "step_timer.h"

#define FrameCount 3
#define CityRowCount 10
#define CityColumnCount 3
#define UseBundles 1

typedef struct IDXGISwapChain3 IDXGISwapChain3;

typedef struct DXSample
{
    // Viewport dimensions.
    UINT width;
    UINT height;
    float aspectRatio;
    // Adapter info.
    bool useWarpDevice;
    // Root assets path.
    WCHAR assetsPath[512];
    // Window title.
    CHAR *title;

    // Pipeline objects.
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;
    IDXGISwapChain3 *swapChain;
    ID3D12Device *device;
    ID3D12Resource *renderTargets[FrameCount];
    ID3D12Resource *depthStencil;
    ID3D12CommandAllocator *commandAllocator;
    ID3D12CommandQueue *commandQueue;
    ID3D12RootSignature *rootSignature;
    ID3D12DescriptorHeap *rtvHeap;
    ID3D12DescriptorHeap *cbvSrvHeap;
    ID3D12DescriptorHeap *dsvHeap;
    ID3D12DescriptorHeap *samplerHeap;
    ID3D12PipelineState *pipelineState1;
    ID3D12PipelineState *pipelineState2;
    ID3D12GraphicsCommandList *commandList;

    // App resources.
    UINT numIndices;
    ID3D12Resource *vertexBuffer;
    ID3D12Resource *indexBuffer;
    ID3D12Resource *texture;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;
    StepTimer timer;
    UINT cbvSrvDescriptorSize;
    UINT rtvDescriptorSize;
    SimpleCamera camera;

    // Frame resources.
    struct FrameResource* frameResources[FrameCount];
    struct FrameResource* currFrameResource;
    UINT currentFrameResourceIndex;

    // Synchronization objects.
    UINT frameIndex;
    UINT frameCounter;
    HANDLE fenceEvent;
    ID3D12Fence *fence;
    UINT64 fenceValue;
} DXSample;


void Sample_Init(DXSample* const sample);
void Sample_Destroy(DXSample* const sample);
void Sample_Update(DXSample* const sample);
void Sample_Render(DXSample* const sample);
void Sample_OnKeyDown(DXSample* const sample, UINT8 key);
void Sample_OnKeyUp(DXSample* const sample, UINT8 key);
