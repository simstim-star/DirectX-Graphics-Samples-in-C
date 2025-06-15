#define COBJMACROS

#include "frame_resource.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include "dxheaders/d3dx12_macros.h"
#include "dxheaders/d3dx12c_core.h"
#include "DirectXMathC.h"

// The slope upwards of the city moving deeper into a row and/or column of buildings
static const FLOAT CITY_SLOPE = 0.02f;

static void SetCityPositions(FrameResource* const fr, FLOAT intervalCol, FLOAT intervalRow)
{
    for (UINT row = 0; row < fr->cityRowCount; row++)
    {
        FLOAT rowOffset = row * intervalRow;
        for (UINT col = 0; col < fr->cityColumnCount; col++)
        {
            FLOAT colOffset = col * intervalCol;
            // The y position ("up") is based off of the city's row and column position to prevent z-fighting.
            XMMATRIX translated = XM_MAT_TRANSLATION(colOffset, CITY_SLOPE * (row * fr->cityColumnCount + col), rowOffset);
            XM_STORE_FLOAT4X4(&fr->modelMatrices[row * fr->cityColumnCount + col], translated);
        }
    }
}

void FrameResource_Init(FrameResource* const fr, ID3D12Device* const device, UINT cityRowCount, UINT cityColumnCount){
    fr->fenceValue = 0;
    fr->cityRowCount = cityRowCount;
    fr->cityColumnCount = cityColumnCount;
    fr->modelMatrices = HeapAlloc(GetProcessHeap(), 0, sizeof(XMFLOAT4X4) * fr->cityRowCount * fr->cityColumnCount);

    // The command allocator is used by the main sample class when 
    // resetting the command list in the main update loop. Each frame 
    // resource needs a command allocator because command allocators 
    // cannot be reused until the GPU is done executing the commands 
    // associated with it.
    HRESULT hr = ID3D12Device_CreateCommandAllocator(device, 
        D3D12_COMMAND_LIST_TYPE_DIRECT, 
        __IID(&fr->commandAllocator),
        (void**)&fr->commandAllocator);
    if(FAILED(hr))    LogAndExit(hr);

    hr = ID3D12Device_CreateCommandAllocator(device, 
        D3D12_COMMAND_LIST_TYPE_BUNDLE, 
        __IID(&fr->bundleAllocator), 
        (void**)&fr->bundleAllocator);
    if(FAILED(hr)) LogAndExit(hr);

    D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadBuffer = CD3DX12_RESOURCE_DESC_BUFFER(sizeof(SceneConstantBuffer) * fr->cityRowCount * fr->cityColumnCount, D3D12_RESOURCE_FLAG_NONE, 0);
    
    // Create an upload heap for the constant buffers.
    hr = ID3D12Device_CreateCommittedResource(device,
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadBuffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        NULL,
        __IID(&fr->cbvUploadHeap),
        (void**)&fr->cbvUploadHeap);
    if (FAILED(hr)) LogAndExit(hr);

    // Map the constant buffers. Note that unlike D3D11, the resource 
    // does not need to be unmapped for use by the GPU. In this sample, 
    // the resource stays 'permenantly' mapped to avoid overhead with 
    // mapping/unmapping each frame.
    const D3D12_RANGE readRange = (D3D12_RANGE){ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU
    hr = ID3D12Resource_Map(fr->cbvUploadHeap, 0, &readRange, (void**)(&fr->pConstantBuffers));
    if(FAILED(hr)) LogAndExit(hr);

    // Update all of the model matrices once; our cities don't move so 
    // we don't need to do this ever again.
    SetCityPositions(fr, 8.0f, -8.0f);
}

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
    ID3D12RootSignature* const rootSignature)
{
    HRESULT hr = ID3D12Device_CreateCommandList(device, 
        0, 
        D3D12_COMMAND_LIST_TYPE_BUNDLE, 
        fr->bundleAllocator, 
        pso1, 
        __IID(&fr->bundle), 
        (void**)&fr->bundle);
    if(FAILED(hr)) LogAndExit(hr);
    NAME_D3D12_OBJECT(fr->bundle);

    FrameResource_PopulateCommandList(fr, fr->bundle, pso1, pso2, frameResourceIndex, numIndices, indexBufferViewDesc,
        vertexBufferViewDesc, cbvSrvDescriptorHeap, cbvSrvDescriptorSize, samplerDescriptorHeap, rootSignature);
    hr = ID3D12GraphicsCommandList_Close(fr->bundle);
    if (FAILED(hr)) LogAndExit(hr);
}

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
    ID3D12RootSignature* const rootSignature)
{
    // If the root signature matches the root signature of the caller, then
    // bindings are inherited, otherwise the bind space is reset.
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(commandList, rootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { cbvSrvDescriptorHeap, samplerDescriptorHeap };
    ID3D12GraphicsCommandList_SetDescriptorHeaps(commandList, _countof(ppHeaps), ppHeaps);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(commandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetIndexBuffer(commandList, indexBufferViewDesc);
    ID3D12GraphicsCommandList_IASetVertexBuffers(commandList, 0, 1, vertexBufferViewDesc);
    D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(cbvSrvDescriptorHeap, &cbvSrvHandle);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(commandList, 0, cbvSrvHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(samplerDescriptorHeap, &samplerHandle);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(commandList, 1, samplerHandle);

    // Calculate the descriptor offset due to multiple frame resources.
    // 1 SRV + how many CBVs we have currently.
    UINT frameResourceDescriptorOffset = 1 + (frameResourceIndex * fr->cityRowCount * fr->cityColumnCount);
    cbvSrvHandle.ptr += ((UINT64)frameResourceDescriptorOffset) * ((UINT64)cbvSrvDescriptorSize);

    BOOL usePso1 = TRUE;
    for (UINT row = 0; row < fr->cityRowCount; row++)
    {
        for (UINT col = 0; col < fr->cityColumnCount; col++)
        {
            // Alternate which PSO to use; the pixel shader is different on 
            // each just as a PSO setting demonstration.
            ID3D12GraphicsCommandList_SetPipelineState(commandList, usePso1 ? pso1 : pso2);
            usePso1 = !usePso1;
            // Set this city's CBV table and move to the next descriptor.
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(commandList, 2, cbvSrvHandle);
            cbvSrvHandle.ptr += (UINT64)cbvSrvDescriptorSize;
            ID3D12GraphicsCommandList_DrawIndexedInstanced(commandList, numIndices, 1, 0, 0, 0);
        }
    }
}

void XM_CALLCONV FrameResource_UpdateConstantBuffers(FrameResource* const fr, FXMMATRIX view, CXMMATRIX projection)
{
    XMMATRIX model;
    XMFLOAT4X4 mvp;

    for (UINT row = 0; row < fr->cityRowCount; row++)
    {
        for (UINT col = 0; col < fr->cityColumnCount; col++)
        {
            UINT currentCityBlock = row * fr->cityColumnCount + col;
            model = XMLoadFloat4x4(&fr->modelMatrices[currentCityBlock]);

            // Compute the model-view-projection matrix.
            XMMATRIX mv_XMMATRIX = XMMatrixMultiply(&model, view);
            XMMATRIX mvp_XMMATRIX = XMMatrixMultiply(&mv_XMMATRIX, projection);
            XMMATRIX mvpTranspose_XMMATRIX = XMMatrixTranspose(&mvp_XMMATRIX);
            XMStoreFloat4x4(&mvp, &mvpTranspose_XMMATRIX);

            // Copy this matrix into the appropriate location in the upload heap subresource.
            memcpy(&fr->pConstantBuffers[currentCityBlock], &mvp, sizeof(mvp));
        }
    }
}

void FrameResource_Clean(FrameResource* const fr)
{
    ID3D12Resource_Unmap(fr->cbvUploadHeap, 0, NULL);
    fr->pConstantBuffers = NULL;
    HeapFree(GetProcessHeap(), 0, fr->modelMatrices);
    RELEASE(fr->commandAllocator);
    RELEASE(fr->bundleAllocator);
    RELEASE(fr->bundle);
    RELEASE(fr->cbvUploadHeap);
}