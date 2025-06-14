#pragma once
#include <d3d12.h>
#include <cstdint>
#include "step_timer.h"
#include "simple_camera.h"

extern const float    c_fovy;
extern const wchar_t* c_lodFilenames[];
extern const wchar_t* c_ampShaderFilename;
extern const wchar_t* c_meshShaderFilename;
extern const wchar_t* c_pixelShaderFilename;

enum RenderMode
{
    Flat,
    Meshlets,
    LOD,
    Count
};

typedef struct D3D12DynamicLOD
{
    // Pipeline objects.
    CD3DX12_VIEWPORT                    viewport;
    CD3DX12_RECT                        scissorRect;

    uint32_t                            rtvDescriptorSize;
    uint32_t                            dsvDescriptorSize;
    uint32_t                            srvDescriptorSize;

    ID3D12Device2*               device;
    IDXGISwapChain3*             swapChain;
    ID3D12Resource*              renderTargets[FrameCount];
    ID3D12Resource*              depthStencil;
    ID3D12CommandAllocator*      commandAllocators[FrameCount];
    ID3D12CommandQueue*          commandQueue;

    // Synchronization objects.
    uint32_t                            frameIndex;
    uint32_t                            frameCounter;
    HANDLE                              fenceEvent;
    ID3D12Fence*                 fence;
    UINT64                              fenceValues[FrameCount];

    ID3D12DescriptorHeap*        rtvHeap;
    ID3D12DescriptorHeap*        dsvHeap;
    ID3D12DescriptorHeap*        srvHeap;

    ID3D12RootSignature*         rootSignature;
    ID3D12PipelineState*         pipelineState;
    ID3D12Resource*              constantBuffer;
    ID3D12Resource*              instanceBuffer;
    ID3D12Resource*              instanceUpload;

    ID3D12GraphicsCommandList6*  commandList;
    Constants* constantData;
    Instance* instanceData;

    StepTimer                           timer;
    SimpleCamera                        camera;
    std::vector<Model*>                  lods;
    RenderMode                          renderMode;
    uint32_t                            instanceLevel;

    uint32_t                            instanceCount;
    bool                                updateInstances;
};

