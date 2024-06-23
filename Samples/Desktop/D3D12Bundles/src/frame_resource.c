#include "frame_resource.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include "dxheaders/d3dx12_macros.h"
#include "dxheaders/d3dx12c_core.h"
#include "DirectXMathC.h"

static void SetCityPositions(FrameResource* const fr, FLOAT intervalX, FLOAT intervalZ)
{
    for (UINT i = 0; i < fr->cityRowCount; i++)
    {
        FLOAT cityOffsetZ = i * intervalZ;
        for (UINT j = 0; j < fr->cityColumnCount; j++)
        {
            FLOAT cityOffsetX = j * intervalX;
            // The y position is based off of the city's row and column position to prevent z-fighting.
            XMMATRIX translated = XMMatrixTranslation(cityOffsetX, 0.02f * (i * fr->cityColumnCount + j), cityOffsetZ);
            XMStoreFloat4x4(&fr->modelMatrices[i * fr->cityColumnCount + j], &translated);
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
    ExitIfFailed(CALL(CreateCommandAllocator, device, D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&fr->commandAllocator)));
    ExitIfFailed(CALL(CreateCommandAllocator, device, D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&fr->bundleAllocator)));

    D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadBuffer = CD3DX12_RESOURCE_DESC_BUFFER(sizeof(SceneConstantBuffer) * fr->cityRowCount * fr->cityColumnCount, D3D12_RESOURCE_FLAG_NONE, 0);
    // Create an upload heap for the constant buffers.
    ExitIfFailed(CALL(CreateCommittedResource, device, 
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadBuffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        NULL,
        IID_PPV_ARGS(&fr->cbvUploadHeap))
    );

    // Map the constant buffers. Note that unlike D3D11, the resource 
    // does not need to be unmapped for use by the GPU. In this sample, 
    // the resource stays 'permenantly' mapped to avoid overhead with 
    // mapping/unmapping each frame.
    const D3D12_RANGE readRange = (D3D12_RANGE){ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU
    ExitIfFailed(CALL(Map, fr->cbvUploadHeap, 0, &readRange, (void**)(&fr->pConstantBuffers)));

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
    ExitIfFailed(CALL(CreateCommandList, device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE, fr->bundleAllocator, pso1, IID_PPV_ARGS(&fr->bundle)));
    NAME_D3D12_OBJECT(fr->bundle);
    FrameResource_PopulateCommandList(fr, fr->bundle, pso1, pso2, frameResourceIndex, numIndices, indexBufferViewDesc,
        vertexBufferViewDesc, cbvSrvDescriptorHeap, cbvSrvDescriptorSize, samplerDescriptorHeap, rootSignature);
    ExitIfFailed(CALL(Close, fr->bundle));
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
    CALL(SetGraphicsRootSignature, commandList, rootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { cbvSrvDescriptorHeap, samplerDescriptorHeap };
    CALL(SetDescriptorHeaps, commandList, _countof(ppHeaps), ppHeaps);
    CALL(IASetPrimitiveTopology, commandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    CALL(IASetIndexBuffer, commandList, indexBufferViewDesc);
    CALL(IASetVertexBuffers, commandList, 0, 1, vertexBufferViewDesc);
    D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle;
    CALL(GetGPUDescriptorHandleForHeapStart, cbvSrvDescriptorHeap, &cbvSrvHandle);
    CALL(SetGraphicsRootDescriptorTable, commandList, 0, cbvSrvHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle;
    CALL(GetGPUDescriptorHandleForHeapStart, samplerDescriptorHeap, &samplerHandle);
    CALL(SetGraphicsRootDescriptorTable, commandList, 1, samplerHandle);

    // Calculate the descriptor offset due to multiple frame resources.
    // 1 SRV + how many CBVs we have currently.
    UINT frameResourceDescriptorOffset = 1 + (frameResourceIndex * fr->cityRowCount * fr->cityColumnCount);
    cbvSrvHandle.ptr += ((UINT64)frameResourceDescriptorOffset) * ((UINT64)cbvSrvDescriptorSize);

    BOOL usePso1 = TRUE;
    for (UINT i = 0; i < fr->cityRowCount; i++)
    {
        for (UINT j = 0; j < fr->cityColumnCount; j++)
        {
            // Alternate which PSO to use; the pixel shader is different on 
            // each just as a PSO setting demonstration.
            CALL(SetPipelineState, commandList, usePso1 ? pso1 : pso2);
            usePso1 = !usePso1;
            // Set this city's CBV table and move to the next descriptor.
            CALL(SetGraphicsRootDescriptorTable, commandList, 2, cbvSrvHandle);
            cbvSrvHandle.ptr = (UINT64)((INT64)cbvSrvHandle.ptr + (INT64)cbvSrvDescriptorSize);
            CALL(DrawIndexedInstanced, commandList, numIndices, 1, 0, 0, 0);
        }
    }
}

void XM_CALLCONV FrameResource_UpdateConstantBuffers(FrameResource* const fr, FXMMATRIX view, CXMMATRIX projection)
{
    XMMATRIX model;
    XMFLOAT4X4 mvp;

    for (UINT i = 0; i < fr->cityRowCount; i++)
    {
        for (UINT j = 0; j < fr->cityColumnCount; j++)
        {
            model = XMLoadFloat4x4(&fr->modelMatrices[i * fr->cityColumnCount + j]);

            // Compute the model-view-projection matrix.
            XMMATRIX mv_XMMATRIX = XMMatrixMultiply(&model, view);
            XMMATRIX mvp_XMMATRIX = XMMatrixMultiply(&mv_XMMATRIX, projection);
            XMMATRIX mvpTranspose_XMMATRIX = XMMatrixTranspose(&mvp_XMMATRIX);
            XMStoreFloat4x4(&mvp, &mvpTranspose_XMMATRIX);

            // Copy this matrix into the appropriate location in the upload heap subresource.
            memcpy(&fr->pConstantBuffers[i * fr->cityColumnCount + j], &mvp, sizeof(mvp));
        }
    }
}

void FrameResource_Clean(FrameResource* const fr)
{
    CALL(Unmap, fr->cbvUploadHeap, 0, NULL);
    fr->pConstantBuffers = NULL;
    HeapFree(GetProcessHeap(), 0, fr->modelMatrices);
    RELEASE(fr->commandAllocator);
    RELEASE(fr->bundleAllocator);
    RELEASE(fr->bundle);
    RELEASE(fr->cbvUploadHeap);
}