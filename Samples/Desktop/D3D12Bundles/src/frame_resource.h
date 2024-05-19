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

void FrameResource_Init(FrameResource *fr, ID3D12Device* pDevice, UINT cityRowCount, UINT cityColumnCount);
void FrameResource_Clean(FrameResource* fr);

void FrameResource_InitBundle(FrameResource* fr,
    ID3D12Device* pDevice, 
    ID3D12PipelineState* pPso1, 
    ID3D12PipelineState* pPso2,
    UINT frameResourceIndex, 
    UINT numIndices, 
    D3D12_INDEX_BUFFER_VIEW* pIndexBufferViewDesc, 
    D3D12_VERTEX_BUFFER_VIEW* pVertexBufferViewDesc,
    ID3D12DescriptorHeap* pCbvSrvDescriptorHeap, 
    UINT cbvSrvDescriptorSize, 
    ID3D12DescriptorHeap* pSamplerDescriptorHeap, 
    ID3D12RootSignature* pRootSignature
);

void FrameResource_PopulateCommandList(FrameResource* fr,
    ID3D12GraphicsCommandList* pCommandList, 
    ID3D12PipelineState* pPso1, 
    ID3D12PipelineState* pPso2,
    UINT frameResourceIndex, 
    UINT numIndices, 
    D3D12_INDEX_BUFFER_VIEW* pIndexBufferViewDesc, 
    D3D12_VERTEX_BUFFER_VIEW* pVertexBufferViewDesc,
    ID3D12DescriptorHeap* pCbvSrvDescriptorHeap, 
    UINT cbvSrvDescriptorSize, 
    ID3D12DescriptorHeap* pSamplerDescriptorHeap, 
    ID3D12RootSignature* pRootSignature
);

void XM_CALLCONV FrameResource_UpdateConstantBuffers(FrameResource* fr, FXMMATRIX view, CXMMATRIX projection);