#pragma once

#define COBJMACROS

#include "model.h"
#include "dxheaders/core_helpers.h"
#include <stdio.h>
#include <d3d12.h>
#include "macros.h"
#include "sample_commons.h"
#include "dxheaders/barrier_helpers.h"

#include "DirectXCollisionC.h"

/*****************************************************************
    Constants
******************************************************************/

const D3D12_INPUT_ELEMENT_DESC c_elementDescs[Attribute_Count] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
    { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
    { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
};

const uint32_t c_sizeMap[] =
{
    12, // Position
    12, // Normal
    8,  // TexCoord
    12, // Tangent
    12, // Bitangent
};

const uint32_t c_prolog = 'MSHL';

/*****************************************************************
    Private functions
******************************************************************/

static uint32_t GetFormatSize(DXGI_FORMAT format);

/*****************************************************************************************************************************
  Mesh-related types definitions
******************************************************************************************************************************/

// a description of the attributes that will be used by IA: the type (Position, Normal, tangent, etc)
// and the offset it takes.
typedef struct Attribute
{
    enum EType
    {
        Position,
        Normal,
        TexCoord,
        Tangent,
        Bitangent,
        Count
    } Type;
    uint32_t Offset;
} Attribute;

/*****************************************************************
    Private types
******************************************************************/


enum FileVersion
{
    FILE_VERSION_INITIAL = 0,
    CURRENT_FILE_VERSION = FILE_VERSION_INITIAL
};

struct FileHeader
{
    uint32_t Prolog;
    uint32_t Version;

    uint32_t MeshCount;
    uint32_t AccessorCount;
    uint32_t BufferViewCount;
    uint32_t BufferSize;
};

/*****************************************************************
    Public functions
******************************************************************/

uint32_t Mesh_GetLastMeshletPackCount(
    Span_Meshlet meshlets, 
    Span_Subset meshletSubsets, 
    uint32_t subsetIndex, 
    uint32_t maxNumVerticesThreadGroupCanProcess, 
    uint32_t maxNumPrimitivesThreadGroupCanProcess
)
{
    if (meshlets.count == 0) {
        return 0;
    }

    Subset subset = SPAN_AT(meshletSubsets, subsetIndex);
    Meshlet meshlet = SPAN_AT(meshlets, subset.Offset + subset.Count - 1);

    return min(maxNumVerticesThreadGroupCanProcess / meshlet.VertCount, maxNumPrimitivesThreadGroupCanProcess / meshlet.PrimCount);
}

void Mesh_GetPrimitive(Span_PackedTriangle PrimitiveIndices, uint32_t index, uint32_t* i0, uint32_t* i1, uint32_t* i2) {
    PackedTriangle prim = SPAN_AT(PrimitiveIndices, index);
    *(i0) = prim.i0;
    *(i1) = prim.i1;
    *(i2) = prim.i2;
}

uint32_t Mesh_GetVertexIndex(Span_uint8_t UniqueVertexIndices, uint32_t index, uint32_t indexSize)
{
    const uint8_t* addr = UniqueVertexIndices.data + index * indexSize;
    if (indexSize == 4)
    {
        return *((const uint32_t*)(addr));
    }
    return *((const uint16_t*)(addr));
}

void Mesh_Release(Mesh* m)
{
    for (int i = 0; i < m->numVerticesSpans; ++i) RELEASE(m->VertexResources[i]);
    RELEASE(m->IndexResource);
    RELEASE(m->MeshletResource);
    RELEASE(m->UniqueVertexIndexResource);
    RELEASE(m->PrimitiveIndexResource);
    RELEASE(m->CullDataResource);
    RELEASE(m->MeshInfoResource);
}

typedef struct {
    uint32_t Prolog;
    uint32_t Version;

    uint32_t MeshCount;
    uint32_t AccessorCount;
    uint32_t BufferViewCount;
    uint32_t BufferSize;
} FileHeader;

// An accessor is an instruction about how to read model buffer data
typedef struct Accessor
{
    uint32_t BufferViewIdx;
    uint32_t Offset;
    uint32_t Size;
    uint32_t Stride;
    uint32_t Count;
} Accessor;

// A buffer view tells us where to start reading the buffer (Offset)
// and the size of the chunk we want to read (Size)
typedef struct BufferView
{
    uint32_t Offset;
    uint32_t Size;
} BufferView;

// The header of the mesh is a collection of indices to mesh data 
typedef struct MeshHeader
{
    uint32_t AccessorIndex;
    uint32_t IndexSubsets;
    uint32_t AttributeAccessorIndex[Attribute_Count];

    uint32_t MeshletIndex;
    uint32_t MeshletSubsets;
    uint32_t UniqueVertexIndex;
    uint32_t PrimitiveIndex;
    uint32_t CullDataIndex;
} MeshHeader;

HRESULT Model_LoadFromFile(Model *const m, const wchar_t* const basepath, const wchar_t* const assetpath)
{
    size_t bufferSize = wcslen(basepath) + wcslen(assetpath) + 1;
    WCHAR* filePath = HeapAlloc(GetProcessHeap(), 0, bufferSize * sizeof(WCHAR));
    swprintf(filePath, bufferSize, L"%s%s", basepath, assetpath);
    FILE* file = _wfopen(filePath, L"rb");
    if (!file) {
        return E_INVALIDARG;
    }
    HeapFree(GetProcessHeap(), 0, filePath);

    // Read header
    FileHeader header;
    if (fread(&header, sizeof(header), 1, file) != 1)
    {
        fclose(file);
        return E_FAIL;
    }

    // Validate header
    if (header.Prolog != c_prolog || header.Version != CURRENT_FILE_VERSION)
    {
        fclose(file);
        return E_FAIL;
    }

    // Read mesh headers
    size_t meshHeaderDataSize = header.MeshCount * sizeof(MeshHeader);
    MeshHeader* meshesHeaders = malloc(meshHeaderDataSize);
    if (!meshesHeaders)
    {
        fclose(file);
        return E_FAIL;
    }
    if (fread(meshesHeaders, sizeof(MeshHeader), header.MeshCount, file) != header.MeshCount)
    {
        free(meshesHeaders);
        fclose(file);
        return E_FAIL;
    }

    // Read accessors
    size_t accessorDataSize = header.AccessorCount * sizeof(Accessor);
    Accessor* accessors = malloc(accessorDataSize);
    if (!accessors)
    {
        free(meshesHeaders);
        fclose(file);
        return E_FAIL;
    }
    if (fread(accessors, sizeof(Accessor), header.AccessorCount, file) != header.AccessorCount)
    {
        free(meshesHeaders);
        free(accessors);
        fclose(file);
        return E_FAIL;
    }

    // Read buffer views
    size_t bufferViewDataSize = header.BufferViewCount * sizeof(BufferView);
    BufferView* bufferViews = malloc(bufferViewDataSize);
    if (!bufferViews)
    {
        free(meshesHeaders);
        free(accessors);
        fclose(file);
        return E_FAIL;
    }
    if (fread(bufferViews, sizeof(BufferView), header.BufferViewCount, file) != header.BufferViewCount)
    {
        free(meshesHeaders);
        free(accessors);
        free(bufferViews);
        fclose(file);
        return E_FAIL;
    }

    // Read model buffer
    m->buffer = malloc(header.BufferSize);
    if (!m->buffer)
    {
        free(meshesHeaders);
        free(accessors);
        free(bufferViews);
        fclose(file);
        return E_FAIL;
    }
    if (fread(m->buffer, 1, header.BufferSize, file) != header.BufferSize)
    {
        free(meshesHeaders);
        free(accessors);
        free(bufferViews);
        free(m->buffer);
        fclose(file);
        return E_FAIL;
    }

    // Final EOF check
    char eofbyte;
    size_t readSize = fread(&eofbyte, 1, 1, file);
    if (readSize != 0 && !feof(file))  // readSize == 0 means expected EOF
    {
        free(meshesHeaders);
        free(accessors);
        free(bufferViews);
        free(m->buffer);
        fclose(file);
        return E_FAIL;
    }

    fclose(file);

    /* Now we will actually fill the model with the data we have loaded in memory */

    m->meshes = malloc(sizeof(Mesh) * header.MeshCount);
    m->nMeshes = header.MeshCount;
    for (uint32_t ithMesh = 0; ithMesh < header.MeshCount; ++ithMesh)
    {
        MeshHeader* meshHeader = &meshesHeaders[ithMesh];
        Mesh* mesh = &m->meshes[ithMesh];
        mesh->numVerticesSpans = 0;

        /* Load indices data */
        {
            Accessor* accessor = &accessors[meshHeader->AccessorIndex];  
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx];
            mesh->IndexSize = accessor->Size;
            mesh->IndexCount = accessor->Count;
            mesh->Indices = SPAN(uint8_t, m->buffer + bufferView->Offset, bufferView->Size);
        }

        /* Load index subset data */
        {
            Accessor* accessor = &accessors[meshHeader->IndexSubsets];  
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx]; 
            mesh->IndexSubsets = SPAN(Subset, (Subset*)(m->buffer + bufferView->Offset), accessor->Count);
        }

        /* Load vertex data */

        uint32_t* attributeBufferMap = malloc(sizeof(uint32_t) * Attribute_Count);
        int attributeBufferMapSize = 0;

        // As said before, LayoutDesc is merely an array of layout elements
        // We keep two variables for it in the mesh for convenience, but here
        // it is clear that they point to the same thing
        mesh->LayoutDesc.pInputElementDescs = mesh->LayoutElems;
        mesh->LayoutDesc.NumElements = 0;


        for (int jthAttribute = 0; jthAttribute < Attribute_Count; ++jthAttribute)
        {
            if (meshHeader->AttributeAccessorIndex[jthAttribute] == -1) {
                continue; // handle missing attributes
            }

            Accessor* accessor = &accessors[meshHeader->AttributeAccessorIndex[jthAttribute]];

            bool found = false;
            for (int k = 0; k < attributeBufferMapSize && !found; ++k)
            {
                found = attributeBufferMap[k] == accessor->BufferViewIdx;
            }

            if (found)
            {
                // Duplicate buffer views will not increase the size of Vertices and VertexStrides
                // If multiple attributes share the same buffer view, only the first occurrence will be added
                continue;
            }

            attributeBufferMap[attributeBufferMapSize] = accessor->BufferViewIdx;
            attributeBufferMapSize++;
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx];

            // the span to vertex data related to that bufferView
            Span_uint8_t vertsSpan = { .data = m->buffer + bufferView->Offset, .count = bufferView->Size };
            

            // init the mesh vertex data
            mesh->VertexStrides[jthAttribute] = accessor->Stride;
            mesh->VerticesSpans[jthAttribute] = vertsSpan;
            mesh->VertexCount = vertsSpan.count / accessor->Stride;  // Assuming count of vertices as the total size / stride
            mesh->numVerticesSpans++;
        }

        // Populate the vertex buffer metadata from accessors.
        for (int jthAttribute = 0; jthAttribute < Attribute_Count; ++jthAttribute)  // Assuming Attribute_Count is a constant or macro
        {
            if (meshHeader->AttributeAccessorIndex[jthAttribute] == -1) {
                continue;
            }

            Accessor* accessor = &accessors[meshHeader->AttributeAccessorIndex[jthAttribute]];
            uint32_t bufferViewIndex = UINT32_MAX;
            for (int k = 0; k < attributeBufferMapSize; ++k) 
            {
                if (attributeBufferMap[k] == accessor->BufferViewIdx)
                {
                    bufferViewIndex = k;
                    break;
                }
            }

            if (bufferViewIndex == UINT32_MAX)
            {
                continue;  // This should not happen if everything is set up correctly
            }

            // Create the input element descriptor for D3D12
            D3D12_INPUT_ELEMENT_DESC desc = c_elementDescs[jthAttribute];
            desc.InputSlot = (UINT)bufferViewIndex;  // Set the input slot index from the found index

            // Store the descriptor in the layout
            mesh->LayoutElems[mesh->LayoutDesc.NumElements] = desc;
            mesh->LayoutDesc.NumElements++;
        }

        // Meshlet data
        {
            Accessor* accessor = &accessors[meshHeader->MeshletIndex];
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx];

            mesh->Meshlets.data = (Meshlet*)(m->buffer + bufferView->Offset);
            mesh->Meshlets.count = accessor->Count;
        }

        // Meshlet Subset data
        {
            Accessor* accessor = &accessors[meshHeader->MeshletSubsets];
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx];

            mesh->MeshletSubsets.data = (Subset*)(m->buffer + bufferView->Offset);
            mesh->MeshletSubsets.count = accessor->Count;
        }

        // Unique Vertex Index data
        {
            Accessor* accessor = &accessors[meshHeader->UniqueVertexIndex];
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx];

            mesh->UniqueVertexIndices.data = m->buffer + bufferView->Offset;
            mesh->UniqueVertexIndices.count = bufferView->Size;
        }

        // Primitive Index data
        {
            Accessor* accessor = &accessors[meshHeader->PrimitiveIndex];
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx];

            mesh->PrimitiveIndices.data = (PackedTriangle*)(m->buffer + bufferView->Offset);
            mesh->PrimitiveIndices.count = accessor->Count;
        }

        // Cull data
        {
            Accessor* accessor = &accessors[meshHeader->CullDataIndex];
            BufferView* bufferView = &bufferViews[accessor->BufferViewIdx];

            mesh->CullingData.data = (CullData*)(m->buffer + bufferView->Offset);
            mesh->CullingData.count = accessor->Count;
        }
    }

    // Build bounding spheres for each mesh
    for (uint32_t ithMesh = 0; ithMesh < m->nMeshes; ++ithMesh) // m->meshCount would be the number of meshes
    {
        Mesh* mesh = &m->meshes[ithMesh]; // Access mesh using pointer

        uint32_t vbIndexPos = 0;

        // Find the index of the vertex buffer of the position attribute
        for (uint32_t j = 1; j < mesh->LayoutDesc.NumElements; ++j)
        {
            D3D12_INPUT_ELEMENT_DESC* desc = &mesh->LayoutElems[j]; // LayoutElems is an array
            if (strcmp(desc->SemanticName, "POSITION") == 0)
            {
                vbIndexPos = j;
                break;
            }
        }

        // Find the byte offset of the position attribute with its vertex buffer
        uint32_t positionOffset = 0;

        for (uint32_t j = 0; j < mesh->LayoutDesc.NumElements; ++j)
        {
            D3D12_INPUT_ELEMENT_DESC* desc = &mesh->LayoutElems[j];
            if (strcmp(desc->SemanticName, "POSITION") == 0)
            {
                break;
            }

            if (desc->InputSlot == vbIndexPos)
            {
                positionOffset += GetFormatSize(mesh->LayoutElems[j].Format);
            }
        }

        // Accessing vertices and calculating bounding sphere
        XMFLOAT3* v0 = (XMFLOAT3*)(mesh->VerticesSpans[vbIndexPos].data + positionOffset); // Casting data pointer to XMFLOAT3*
        uint32_t stride = mesh->VertexStrides[vbIndexPos];

        XMBoundingSphereFromPoints(&mesh->BoundingSphere, mesh->VertexCount, v0, stride);

        if (ithMesh == 0)
        {
            m->boundingSphere = mesh->BoundingSphere;
        }
        else
        {
            XMBoundingSphereMerged(&m->boundingSphere, &m->boundingSphere, &mesh->BoundingSphere);
        }
    }

    return S_OK;
}

HRESULT Model_UploadGpuResources(Model *model, ID3D12Device2* device, ID3D12CommandQueue* cmdQueue, ID3D12CommandAllocator* cmdAlloc, ID3D12GraphicsCommandList6* cmdList)
{
    for (uint32_t i = 0; i < model->nMeshes; ++i)
    {
        Mesh *m = &model->meshes[i];

        // Create committed D3D resources of proper sizes
        D3D12_RESOURCE_DESC indexDesc = CD3DX12_RESOURCE_DESC_BUFFER(m->Indices.count, D3D12_RESOURCE_FLAG_NONE, 0);
        D3D12_RESOURCE_DESC meshletDesc = CD3DX12_RESOURCE_DESC_BUFFER(m->Meshlets.count * sizeof(m->Meshlets.data[0]), D3D12_RESOURCE_FLAG_NONE, 0);
        D3D12_RESOURCE_DESC cullDataDesc = CD3DX12_RESOURCE_DESC_BUFFER(m->CullingData.count * sizeof(m->CullingData.data[0]), D3D12_RESOURCE_FLAG_NONE, 0);
        D3D12_RESOURCE_DESC vertexIndexDesc = CD3DX12_RESOURCE_DESC_BUFFER(DivRoundUp_int(m->UniqueVertexIndices.count, 4) * 4, D3D12_RESOURCE_FLAG_NONE, 0);
        D3D12_RESOURCE_DESC primitiveDesc = CD3DX12_RESOURCE_DESC_BUFFER(m->PrimitiveIndices.count * sizeof(m->PrimitiveIndices.data[0]), D3D12_RESOURCE_FLAG_NONE, 0);
        D3D12_RESOURCE_DESC meshInfoDesc = CD3DX12_RESOURCE_DESC_BUFFER(sizeof(MeshInfo), D3D12_RESOURCE_FLAG_NONE, 0);
    
        D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    
        HRESULT hr = ID3D12Device_CreateCommittedResource(device, 
            &defaultHeap, 
            D3D12_HEAP_FLAG_NONE, 
            &indexDesc,
            D3D12_RESOURCE_STATE_COMMON,
            NULL, 
            &IID_ID3D12Resource,
            (void**) & m->IndexResource);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device, 
            &defaultHeap, 
            D3D12_HEAP_FLAG_NONE, 
            &meshletDesc, 
            D3D12_RESOURCE_STATE_COMMON,
            NULL, 
            &IID_ID3D12Resource,
            (void**) & m->MeshletResource);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device, 
            &defaultHeap, 
            D3D12_HEAP_FLAG_NONE, 
            &cullDataDesc, 
            D3D12_RESOURCE_STATE_COMMON,
            NULL, 
            &IID_ID3D12Resource,
            (void**)&m->CullDataResource);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device,
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE, 
            &vertexIndexDesc, 
            D3D12_RESOURCE_STATE_COMMON,
            NULL, 
            &IID_ID3D12Resource,
            (void**)&m->UniqueVertexIndexResource);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device, 
            &defaultHeap, 
            D3D12_HEAP_FLAG_NONE, 
            &primitiveDesc, 
            D3D12_RESOURCE_STATE_COMMON,
            NULL, 
            &IID_ID3D12Resource,
            (void**)&m->PrimitiveIndexResource);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device,
            &defaultHeap, 
            D3D12_HEAP_FLAG_NONE, 
            &meshInfoDesc, 
            D3D12_RESOURCE_STATE_COMMON,
            NULL, 
            &IID_ID3D12Resource,
            (void**)&m->MeshInfoResource);
        if (FAILED(hr)) LogErrAndExit(hr);

        m->IBView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(m->IndexResource);
        m->IBView.Format = m->IndexSize == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        m->IBView.SizeInBytes = m->IndexCount * m->IndexSize;

        for (uint32_t j = 0; j < m->numVerticesSpans; ++j)
        {
            D3D12_RESOURCE_DESC vertexDesc = CD3DX12_RESOURCE_DESC_BUFFER(m->VerticesSpans[j].count, D3D12_RESOURCE_FLAG_NONE, 0);
            ID3D12Device_CreateCommittedResource(device,
                &defaultHeap, 
                D3D12_HEAP_FLAG_NONE, 
                &vertexDesc, 
                D3D12_RESOURCE_STATE_COMMON,
                NULL, 
                &IID_ID3D12Resource,
                (void**)&m->VertexResources[j]);
            m->VBViews[j].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(m->VertexResources[j]);
            m->VBViews[j].SizeInBytes = (uint32_t)(m->VerticesSpans[j].count);
            m->VBViews[j].StrideInBytes = m->VertexStrides[j];
        }

        // Create upload resources
        ID3D12Resource*             vertexUploads[Attribute_Count];
        ID3D12Resource*             indexUpload;
        ID3D12Resource*             meshletUpload;
        ID3D12Resource*             cullDataUpload;
        ID3D12Resource*             uniqueVertexIndexUpload;
        ID3D12Resource*             primitiveIndexUpload;
        ID3D12Resource*             meshInfoUpload;

        D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        hr = ID3D12Device_CreateCommittedResource(device,
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &indexDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &IID_ID3D12Resource,
            (void**)&indexUpload);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device,
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &meshletDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &IID_ID3D12Resource,
            (void**)&meshletUpload);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device,
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &cullDataDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &IID_ID3D12Resource,
            (void**)&cullDataUpload);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device,
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &vertexIndexDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &IID_ID3D12Resource,
            (void**)&uniqueVertexIndexUpload);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device,
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &primitiveDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &IID_ID3D12Resource,
            (void**)&primitiveIndexUpload);
        if (FAILED(hr)) LogErrAndExit(hr);

        hr = ID3D12Device_CreateCommittedResource(device,
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &meshInfoDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &IID_ID3D12Resource,
            (void**)&meshInfoUpload);
        if (FAILED(hr)) LogErrAndExit(hr);

        for (uint32_t j = 0; j < m->numVerticesSpans; ++j)
        {
            D3D12_RESOURCE_DESC vertexDesc = CD3DX12_RESOURCE_DESC_BUFFER(m->VerticesSpans[j].count, D3D12_RESOURCE_FLAG_NONE, 0);
            hr = ID3D12Device_CreateCommittedResource(device,
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &vertexDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                NULL,
                &IID_ID3D12Resource,
                (void**)&vertexUploads[j]);
            if (FAILED(hr)) LogErrAndExit(hr);

            uint8_t* memory = NULL;
            ID3D12Resource_Map(vertexUploads[j], 0, NULL, (void**)&memory);
            memcpy(memory, m->VerticesSpans[j].data, m->VerticesSpans[j].count);
            ID3D12Resource_Unmap(vertexUploads[j], 0, NULL);
        }

        {
            uint8_t* memory = NULL;
            ID3D12Resource_Map(indexUpload, 0, NULL, (void**)(&memory));
            memcpy(memory, m->Indices.data, m->Indices.count);
            ID3D12Resource_Unmap(indexUpload, 0, NULL);
        }

        {
            uint8_t* memory = NULL;
            ID3D12Resource_Map(meshletUpload, 0, NULL, (void**)(&memory));
            memcpy(memory, m->Meshlets.data, m->Meshlets.count * sizeof(m->Meshlets.data[0]));
            ID3D12Resource_Unmap(meshletUpload, 0, NULL);
        }

        {
            uint8_t* memory = NULL;
            ID3D12Resource_Map(cullDataUpload, 0, NULL, (void**)(&memory));
            memcpy(memory, m->CullingData.data, m->CullingData.count * sizeof(m->CullingData.data[0]));
            ID3D12Resource_Unmap(cullDataUpload, 0, NULL);
        }

        {
            uint8_t* memory = NULL;
            ID3D12Resource_Map(uniqueVertexIndexUpload, 0, NULL, (void**)(&memory));
            memcpy(memory, m->UniqueVertexIndices.data, m->UniqueVertexIndices.count);
            ID3D12Resource_Unmap(uniqueVertexIndexUpload, 0, NULL);
        }

        {
            uint8_t* memory = NULL;
            ID3D12Resource_Map(primitiveIndexUpload, 0, NULL, (void**)(&memory));
            memcpy(memory, m->PrimitiveIndices.data, m->PrimitiveIndices.count * sizeof(m->PrimitiveIndices.data[0]));
            ID3D12Resource_Unmap(primitiveIndexUpload, 0, NULL);
        }

        {
            MeshInfo info = {0};
            info.IndexSize = m->IndexSize;
            info.MeshletCount = (uint32_t)(m->Meshlets.count);
            info.LastMeshletVertCount = SPAN_BACK(m->Meshlets).VertCount;
            info.LastMeshletPrimCount = SPAN_BACK(m->Meshlets).PrimCount;


            uint8_t* memory = NULL;
            ID3D12Resource_Map(meshInfoUpload, 0, NULL, (void**)(&memory));
            memcpy(memory, &info, sizeof(MeshInfo));
            ID3D12Resource_Unmap(meshInfoUpload, 0, NULL);
        }

        // Populate our command list
        ID3D12GraphicsCommandList_Reset(cmdList, cmdAlloc, NULL);

        for (uint32_t j = 0; j < m->numVerticesSpans; ++j)
        {
            ID3D12GraphicsCommandList_CopyResource(cmdList, m->VertexResources[j], vertexUploads[j]);
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_Transition(m->VertexResources[j], 
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                D3D12_RESOURCE_BARRIER_FLAG_NONE);
            ID3D12GraphicsCommandList_ResourceBarrier(cmdList, 1, &barrier);
        }

        D3D12_RESOURCE_BARRIER postCopyBarriers[6];

        ID3D12GraphicsCommandList_CopyResource(cmdList, m->IndexResource, indexUpload);
        postCopyBarriers[0] = CD3DX12_Transition(m->IndexResource, 
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAG_NONE);

        ID3D12GraphicsCommandList_CopyResource(cmdList, m->MeshletResource, meshletUpload);
        postCopyBarriers[1] = CD3DX12_Transition(m->MeshletResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAG_NONE);

        ID3D12GraphicsCommandList_CopyResource(cmdList, m->CullDataResource, cullDataUpload);
        postCopyBarriers[2] = CD3DX12_Transition(m->CullDataResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAG_NONE);

        ID3D12GraphicsCommandList_CopyResource(cmdList, m->UniqueVertexIndexResource, uniqueVertexIndexUpload);
        postCopyBarriers[3] = CD3DX12_Transition(m->UniqueVertexIndexResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAG_NONE);

        ID3D12GraphicsCommandList_CopyResource(cmdList, m->PrimitiveIndexResource, primitiveIndexUpload);
        postCopyBarriers[4] = CD3DX12_Transition(m->PrimitiveIndexResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAG_NONE);

        ID3D12GraphicsCommandList_CopyResource(cmdList, m->MeshInfoResource, meshInfoUpload);
        postCopyBarriers[5] = CD3DX12_Transition(m->MeshInfoResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_BARRIER_FLAG_NONE);

        ID3D12GraphicsCommandList_ResourceBarrier(cmdList,
            ARRAYSIZE(postCopyBarriers), 
            postCopyBarriers);

        ID3D12GraphicsCommandList_Close(cmdList);
        
        ID3D12CommandList *asCmdList = NULL;
        hr = ID3D12Object_QueryInterface(cmdList, &IID_ID3D12CommandList, (void**)&asCmdList);
        if (FAILED(hr)) LogErrAndExit(hr);
        ID3D12CommandList* ppCommandLists[] = { asCmdList };
        ID3D12CommandQueue_ExecuteCommandLists(cmdQueue, 1, ppCommandLists);
        RELEASE(asCmdList);

        // Create our sync fence
        ID3D12Fence *fence;
        hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
        if (FAILED(hr)) LogErrAndExit(hr);

        ID3D12CommandQueue_Signal(cmdQueue, fence, 1);

        // Wait for GPU
        if (ID3D12Fence_GetCompletedValue(fence) != 1)
        {
            HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
            ID3D12Fence_SetEventOnCompletion(fence, 1, event);

            WaitForSingleObjectEx(event, INFINITE, false);
            CloseHandle(event);
        }
    }
    return S_OK;
}

/*****************************************************************
    Private functions
******************************************************************/

static uint32_t GetFormatSize(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
        case DXGI_FORMAT_R32G32B32_FLOAT: return 12;
        case DXGI_FORMAT_R32G32_FLOAT: return 8;
        case DXGI_FORMAT_R32_FLOAT: return 4;
    }
    return 0;
}
