#pragma once

#include "windows.h"
#include <stdint.h>
#include <stdbool.h>
#include <d3d12.h>
#include "shared.h"
#include "step_timer.h"
#include "simple_camera.h"
#include "model.h"
#include <dxgi1_6.h>

#define FrameCount 2
#define LodsCount 6

extern const float     c_fovy;
extern const wchar_t*  c_lodFilenames[LodsCount];
extern const wchar_t*  c_ampShaderFilename;
extern const wchar_t*  c_meshShaderFilename;
extern const wchar_t*  c_pixelShaderFilename;

enum RenderMode
{
    Flat,
    Meshlets,
    LOD,
    Count
};

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
    CHAR* title;

    // Pipeline objects.
    D3D12_VIEWPORT               viewport;
    D3D12_RECT                   scissorRect;

    uint32_t                     rtvDescriptorSize;
    uint32_t                     dsvDescriptorSize;
    uint32_t                     srvDescriptorSize;

    ID3D12Device2*               device;
    IDXGISwapChain3*             swapChain;
    ID3D12Resource*              renderTargets[FrameCount];
    ID3D12Resource*              depthStencil;
    ID3D12CommandAllocator*      commandAllocators[FrameCount];
    ID3D12CommandQueue*          commandQueue;

    // Synchronization objects.
    uint32_t                    frameIndex;
    uint32_t                    frameCounter;
    HANDLE                      fenceEvent;
    ID3D12Fence*                fence;
    UINT64                      fenceValues[FrameCount];

    ID3D12DescriptorHeap*       rtvHeap;
    ID3D12DescriptorHeap*       dsvHeap;
    ID3D12DescriptorHeap*       srvHeap;

    ID3D12RootSignature*        rootSignature;
    ID3D12PipelineState*        pipelineState;
    ID3D12Resource*             constantBuffer;
    ID3D12Resource*             instanceBuffer;
    ID3D12Resource*             instanceUpload;

    ID3D12GraphicsCommandList6* commandList;
    Constants*                  constantData;
    Instance*                   instanceData;

    StepTimer                   timer;
    SimpleCamera                camera;
    Model                       lods[LodsCount];
    enum RenderMode             renderMode;
    uint32_t                    instanceLevel;

    uint32_t                    instanceCount;
    bool                        updateInstances;

} DXSample;


void Sample_Init(DXSample* const sample);
void Sample_Destroy(DXSample* sample);
void Sample_Update(DXSample* const sample);
void Sample_Render(DXSample* const sample);
void Sample_KeyDown(DXSample* const sample, UINT8 key);
void Sample_KeyUp(DXSample* const sample, UINT8 key);

// for anomalous exiting cleanup
void Sample_AtExit(DXSample* sample);