#pragma once

#include <windows.h>
#include <d3d12.h>

#include "d3dx12_macros.h"

/***************************************************************************************************************************
 Terms:
 (learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-subresources)
 - Slice: All subresources of a resource in an array of resources
 - Pitch: Distance from begin of X to the begin of the next

  Notes:
  [1] https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copybufferregion
*************************************************************************************************************************/


/************************************************************************************************
 Constants
*************************************************************************************************/

static const SIZE_T MAX_SIZE_T = (SIZE_T)(-1);

/************************************************************************************************
 Copy from source to dest, slice by slice, row by row
*************************************************************************************************/
static inline void MemcpySubresource(
    _In_ const D3D12_MEMCPY_DEST* pDest,
    _In_ const D3D12_SUBRESOURCE_DATA* pSrc,
    SIZE_T RowSizeInBytes,
    UINT NumRows,
    UINT NumSlices)
{
    for (UINT sliceIdx = 0; sliceIdx < NumSlices; ++sliceIdx)
    {
        // start of the dest resource
        BYTE* pDestSlice = (BYTE*)(pDest->pData) + pDest->SlicePitch * sliceIdx;
        // start of the source resource
        const BYTE* pSrcSlice = (const BYTE*)(pSrc->pData) + pSrc->SlicePitch * ((LONG_PTR) sliceIdx);
        // copy each row of the source slice to dest slice
        for (UINT rowIdx = 0; rowIdx < NumRows; ++rowIdx)
        {
            memcpy(
                pDestSlice + pDest->RowPitch * rowIdx,
                pSrcSlice + pSrc->RowPitch * ((LONG_PTR) rowIdx),
                RowSizeInBytes
            );
        }
    }
}

/********************************************************************************************************
 Performs the copy of data to the intermediate and from the intermediate to the final destination
********************************************************************************************************/
static inline  UINT64 DoUpdateSubresources(
    _In_ ID3D12GraphicsCommandList* pCmdList,
    _In_ ID3D12Resource* pDestinationResource,
    _In_ ID3D12Resource* pIntermediate,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
    UINT64 RequiredSize,
    // All arrays below must be populated (e.g. by calling GetCopyableFootprints)
    // This is the layout of the destination
    _In_reads_(NumSubresources) const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
    _In_reads_(NumSubresources) const UINT* pNumRows,
    _In_reads_(NumSubresources) const UINT64* pRowSizesInBytes,
    _In_reads_(NumSubresources) const D3D12_SUBRESOURCE_DATA* pSrcData)
{
    // Minor validation
#if !defined(_WIN32)
    const D3D12_RESOURCE_DESC* IntermediateDesc = CALL(GetDesc, pIntermediate);
    const D3D12_RESOURCE_DESC* DestinationDesc = CALL(GetDesc, pDestinationResource);
#else
    D3D12_RESOURCE_DESC tmpDesc1, tmpDesc2;
    const D3D12_RESOURCE_DESC* IntermediateDesc = CALL(GetDesc, pIntermediate, &tmpDesc1);
    const D3D12_RESOURCE_DESC* DestinationDesc = CALL(GetDesc, pDestinationResource, &tmpDesc2);
#endif
    if (IntermediateDesc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||    // the intermediate should always be a buffer
        IntermediateDesc->Width < RequiredSize + pLayouts[0].Offset ||       // capable of storing the required destination data
        RequiredSize > MAX_SIZE_T ||
        (DestinationDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&    // if trying a upload to buffer dst, "consider copying
        (FirstSubresource != 0 || NumSubresources != 1)))                    // an entire resource" [1]
    {
        return 0;
    }

    // bytes on intermediate 
    BYTE* pData;
    HRESULT hr = CALL(Map, pIntermediate, 0, NULL, (void**)(&pData));
    if (FAILED(hr))
    {
        return 0;
    }
    // for each resource, copy from source data to pData, which is mapped to pIntermediate
    for (UINT subresourceIdx = 0; subresourceIdx < NumSubresources; ++subresourceIdx)
    {
        if (pRowSizesInBytes[subresourceIdx] > MAX_SIZE_T) {
            return 0;
        }
        D3D12_MEMCPY_DEST DestData = { 
            .pData = pData + pLayouts[subresourceIdx].Offset, 
            .RowPitch = pLayouts[subresourceIdx].Footprint.RowPitch, 
            .SlicePitch = ((SIZE_T) pLayouts[subresourceIdx].Footprint.RowPitch) * ((SIZE_T) pNumRows[subresourceIdx]) 
        };
        MemcpySubresource(&DestData, 
            &pSrcData[subresourceIdx], 
            (SIZE_T)(pRowSizesInBytes[subresourceIdx]), 
            pNumRows[subresourceIdx], 
            pLayouts[subresourceIdx].Footprint.Depth
        );
    }
    CALL(Unmap, pIntermediate, 0, NULL);

    // with data on the intermediate, now is time to send it to the destination
    if (DestinationDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        CALL(CopyBufferRegion, pCmdList, pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
    }
    else
    {
        // copy each subresource from intermediate to destination
        for (UINT i = 0; i < NumSubresources; ++i)
        {
            D3D12_TEXTURE_COPY_LOCATION Dst = {
                .pResource = pDestinationResource,
                .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                .PlacedFootprint = {0},
                .SubresourceIndex = i + FirstSubresource,
            };
            D3D12_TEXTURE_COPY_LOCATION Src = {
                .pResource = pIntermediate,
                .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                .PlacedFootprint = pLayouts[i],
            };
            CALL(CopyTextureRegion, pCmdList, &Dst, 0, 0, 0, &Src, NULL);
        }
    }
    return RequiredSize;
}

/******************************************************************************************************************
 Heap-allocating UpdateSubresources implementation. Pushes pSrcData to a pDestinationResource using the pIntermediate
 as a "bridge"
*******************************************************************************************************************/
static inline  UINT64 UpdateSubresources(
    _In_ ID3D12GraphicsCommandList* pCmdList,
    _In_ ID3D12Resource* pDestinationResource,
    _In_ ID3D12Resource* pIntermediate,
    UINT64 IntermediateOffset,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
    _In_reads_(NumSubresources) const D3D12_SUBRESOURCE_DATA* pSrcData)
{
    UINT64 RequiredSize = 0;
    // We adopt a Heap-allocating implementation to have control of the size here
    // A Stack-allocating is more complicated, as you need to be aware in compile time of the number of subresources you'll need
    const UINT64 MemToAlloc = (UINT64)(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
    if (MemToAlloc > SIZE_MAX)
    {
        return 0;
    }
    void* pMem = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(MemToAlloc));
    if (pMem == NULL)
    {
        return 0;
    }
    // it's safe to "break" pMem in parts as below (https://stackoverflow.com/a/28969326/14815076)
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = (D3D12_PLACED_SUBRESOURCE_FOOTPRINT*)(pMem);
    UINT64* pRowSizesInBytes = (UINT64*)(pLayouts + NumSubresources);
    UINT* pNumRows = (UINT*)(pRowSizesInBytes + NumSubresources);

#if !defined(_WIN32)
    const D3D12_RESOURCE_DESC* Desc = CALL(GetDesc, pDestinationResource);
#else
    D3D12_RESOURCE_DESC tmpDesc;
    const D3D12_RESOURCE_DESC* Desc = CALL(GetDesc, pDestinationResource, &tmpDesc);
#endif
    // retrive the footprint of the pDestinationResource. 
    ID3D12Device* pDevice = NULL;
    CALL(GetDevice, pDestinationResource, IID_PPV_ARGS(&pDevice));
    CALL(GetCopyableFootprints, pDevice, Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);
    RELEASE(pDevice);

    const UINT64 Result = DoUpdateSubresources(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData);
    HeapFree(GetProcessHeap(), 0, pMem);
    return Result;
}

/********************************************************************************************************
 Returns required size for a intermediate buffer to be capable of storing the subresources that we will
 pass to a destination later
*********************************************************************************************************/
static inline  UINT64 GetRequiredIntermediateSize(
    _In_ ID3D12Resource* pDestinationResource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources)
{
#if !defined(_WIN32)
    const D3D12_RESOURCE_DESC* Desc = CALL(GetDesc, pDestinationResource);
#else
    D3D12_RESOURCE_DESC tmpDesc;
    const D3D12_RESOURCE_DESC *Desc = CALL(GetDesc, pDestinationResource, &tmpDesc);
#endif
    UINT64 RequiredSize = 0;

    ID3D12Device* pDevice = NULL;
    CALL(GetDevice, pDestinationResource, IID_PPV_ARGS(&pDevice));
    // Gets a resource layout of the subresources. Here only interested in the size footprint
    CALL(GetCopyableFootprints, pDevice, Desc, FirstSubresource, NumSubresources, 0, NULL, NULL, NULL, &RequiredSize);
    RELEASE(pDevice);

    return RequiredSize;
}
