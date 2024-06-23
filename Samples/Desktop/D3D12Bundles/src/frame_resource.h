#pragma once

#include <windows.h>
#include <DirectXMathC.h>

typedef struct ID3D12Device ID3D12Device;
typedef struct ID3D12CommandAllocator ID3D12CommandAllocator;
typedef struct ID3D12GraphicsCommandList ID3D12GraphicsCommandList;
typedef struct ID3D12Resource ID3D12Resource;
typedef struct ID3D12PipelineState ID3D12PipelineState;
typedef struct ID3D12DescriptorHeap ID3D12DescriptorHeap;
typedef struct ID3D12RootSignature ID3D12RootSignature;
typedef struct D3D12_INDEX_BUFFER_VIEW D3D12_INDEX_BUFFER_VIEW;
typedef struct D3D12_VERTEX_BUFFER_VIEW D3D12_VERTEX_BUFFER_VIEW;

typedef struct SceneConstantBuffer
{
    XMFLOAT4X4 mvp;        // Model-view-projection (MVP) matrix.
    FLOAT padding[48];
} SceneConstantBuffer;

typedef struct FrameResource
{
    ID3D12CommandAllocator *commandAllocator;
    ID3D12CommandAllocator *bundleAllocator;
    ID3D12GraphicsCommandList *bundle;
    ID3D12Resource *cbvUploadHeap;
    SceneConstantBuffer* pConstantBuffers;
    UINT64 fenceValue;

    XMFLOAT4X4 *modelMatrices;
    UINT cityRowCount;
    UINT cityColumnCount;
} FrameResource;

void FrameResource_Init(FrameResource * const fr, ID3D12Device* const device, UINT cityRowCount, UINT cityColumnCount);
void FrameResource_Clean(FrameResource* const fr);

void FrameResource_InitBundle(FrameResource* const fr,
    ID3D12Device* const device,
    ID3D12PipelineState* const pso1,
    ID3D12PipelineState* const pso2,
    UINT frameResourceIndex, 
    UINT numIndices, 
    D3D12_INDEX_BUFFER_VIEW* const indexBufferViewDesc,
    D3D12_VERTEX_BUFFER_VIEW* const vertexBufferViewDesc,
    ID3D12DescriptorHeap* const cbvSrvDescriptorHeap,
    UINT cbvSrvDescriptorSize, 
    ID3D12DescriptorHeap* const samplerDescriptorHeap,
    ID3D12RootSignature* const rootSignature
);

void FrameResource_PopulateCommandList(FrameResource* const fr,
    ID3D12GraphicsCommandList* const commandList,
    ID3D12PipelineState* const pso1,
    ID3D12PipelineState* const pso2,
    UINT frameResourceIndex, 
    UINT numIndices, 
    D3D12_INDEX_BUFFER_VIEW* const indexBufferViewDesc,
    D3D12_VERTEX_BUFFER_VIEW* const vertexBufferViewDesc,
    ID3D12DescriptorHeap* const cbvSrvDescriptorHeap,
    UINT cbvSrvDescriptorSize, 
    ID3D12DescriptorHeap* const samplerDescriptorHeap,
    ID3D12RootSignature* const rootSignature
);

void XM_CALLCONV FrameResource_UpdateConstantBuffers(FrameResource* const fr, FXMMATRIX view, CXMMATRIX projection);