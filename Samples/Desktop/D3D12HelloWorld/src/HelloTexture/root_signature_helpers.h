#pragma once

#include <windows.h>
#include <d3d12.h>

/***************************************************************************************************************************
 Note: It seems that D3D_ROOT_SIGNATURE_VERSION_1_2 was only added in Agility SDK 1.610.3 [1], to "enable the use
       of D3D12DDI_STATIC_SAMPLER_0100" [2], which is a new field called pStaticSamplers (a D3D12_STATIC_SAMPLER_DESC1 *) that 
       will be in D3D12_ROOT_SIGNATURE_DESC2. There is nothing about it yet in learn.microsoft.com, even with SDK 1.610.4
       being available for download for quite some time now.
       If you want to use version 1_2, you can use NuGet with the packages.config to get DirectX12 Agility SDK 1.613.2. This SDK
       is not shipped directly with Windows as of today.

 [1] devblogs.microsoft.com/directx/agility-sdk-1-610-0-renderpass-and-minor-vulkan-compatibility-improvements/
 [2] microsoft.github.io/DirectX-Specs/d3d/VulkanOn12.html
****************************************************************************************************************************/


/*************************************************************************************
 This function checks if the provided pRootSignatureDesc is compatible with the MaxVersion.
 If they are compatible, it will simply call D3D12SerializeRootSignature with this desc,
 but if they are not (ie. MaxVersion < pRootSignatureDesc->Version), it will try to take what 
 it can from the pRootSignatureDesc, fill another Desc compatible with MaxVersion and call
 D3D12SerializeRootSignature with this new desc.
*************************************************************************************/
static inline HRESULT D3DX12SerializeVersionedRootSignature(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc,
    D3D_ROOT_SIGNATURE_VERSION MaxVersion,
    _Outptr_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    if (ppErrorBlob != NULL)
    {
        *ppErrorBlob = NULL;
    }

    switch (MaxVersion)
    {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
        switch (pRootSignatureDesc->Version)
        {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            return D3D12SerializeRootSignature(&pRootSignatureDesc->Desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, ppBlob, ppErrorBlob);

        case D3D_ROOT_SIGNATURE_VERSION_1_1:
#if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION >= 609)
        case D3D_ROOT_SIGNATURE_VERSION_1_2:
#endif
        {
            HRESULT hr = S_OK;
            const SIZE_T ParametersSize = sizeof(D3D12_ROOT_PARAMETER) * pRootSignatureDesc->Desc_1_1.NumParameters;
            void* pParameters = (ParametersSize > 0) ? HeapAlloc(GetProcessHeap(), 0, ParametersSize) : NULL;
            if (ParametersSize > 0 && pParameters == NULL)
            {
                hr = E_OUTOFMEMORY;
            }

            // Points to a block of memory that will allocate our new 1_0 parameters that will substitute the original 1_1
            D3D12_ROOT_PARAMETER* pParameters_1_0 = (D3D12_ROOT_PARAMETER*)(pParameters);

            if (SUCCEEDED(hr))
            {
                /********************************************************************
                Convert each parameter from a Desc_1_1 to a parameter of a Desc_1_0.
                *********************************************************************/
                for (UINT n = 0; n < pRootSignatureDesc->Desc_1_1.NumParameters; n++)
                {
                    __analysis_assume(ParametersSize == sizeof(D3D12_ROOT_PARAMETER) * pRootSignatureDesc->Desc_1_1.NumParameters);
                    pParameters_1_0[n].ParameterType = pRootSignatureDesc->Desc_1_1.pParameters[n].ParameterType;
                    pParameters_1_0[n].ShaderVisibility = pRootSignatureDesc->Desc_1_1.pParameters[n].ShaderVisibility;

                    // D3D12_ROOT_PARAMETER has a union of DescriptorTable, Constants, Descriptor
                    // Depending of the original ParameterType, we will initialize a different member
                    switch (pRootSignatureDesc->Desc_1_1.pParameters[n].ParameterType)
                    {
                    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                        pParameters_1_0[n].Constants.Num32BitValues = pRootSignatureDesc->Desc_1_1.pParameters[n].Constants.Num32BitValues;
                        pParameters_1_0[n].Constants.RegisterSpace = pRootSignatureDesc->Desc_1_1.pParameters[n].Constants.RegisterSpace;
                        pParameters_1_0[n].Constants.ShaderRegister = pRootSignatureDesc->Desc_1_1.pParameters[n].Constants.ShaderRegister;
                        break;

                    case D3D12_ROOT_PARAMETER_TYPE_CBV:
                    case D3D12_ROOT_PARAMETER_TYPE_SRV:
                    case D3D12_ROOT_PARAMETER_TYPE_UAV:
                        pParameters_1_0[n].Descriptor.RegisterSpace = pRootSignatureDesc->Desc_1_1.pParameters[n].Descriptor.RegisterSpace;
                        pParameters_1_0[n].Descriptor.ShaderRegister = pRootSignatureDesc->Desc_1_1.pParameters[n].Descriptor.ShaderRegister;
                        break;

                    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                    {
                        const D3D12_ROOT_DESCRIPTOR_TABLE1* table_1_1 = &pRootSignatureDesc->Desc_1_1.pParameters[n].DescriptorTable;
                        const SIZE_T DescriptorRangesSize = sizeof(D3D12_DESCRIPTOR_RANGE) * table_1_1->NumDescriptorRanges;
                        void* pDescriptorRanges = (DescriptorRangesSize > 0 && SUCCEEDED(hr)) ? HeapAlloc(GetProcessHeap(), 0, DescriptorRangesSize) : NULL;
                        if (DescriptorRangesSize > 0 && pDescriptorRanges == NULL)
                        {
                            hr = E_OUTOFMEMORY;
                        }

                        // Points to a block of memory that will allocate our new 1_0 descriptor ranges 
                        // that will substitute the original 1_1
                        D3D12_DESCRIPTOR_RANGE* pDescriptorRanges_1_0 = (D3D12_DESCRIPTOR_RANGE*)(pDescriptorRanges);

                        if (SUCCEEDED(hr))
                        {
                            for (UINT x = 0; x < table_1_1->NumDescriptorRanges; x++)
                            {
                                __analysis_assume(DescriptorRangesSize == sizeof(D3D12_DESCRIPTOR_RANGE) * table_1_1->NumDescriptorRanges);
                                pDescriptorRanges_1_0[x].BaseShaderRegister = table_1_1->pDescriptorRanges[x].BaseShaderRegister;
                                pDescriptorRanges_1_0[x].NumDescriptors = table_1_1->pDescriptorRanges[x].NumDescriptors;
                                pDescriptorRanges_1_0[x].OffsetInDescriptorsFromTableStart = table_1_1->pDescriptorRanges[x].OffsetInDescriptorsFromTableStart;
                                pDescriptorRanges_1_0[x].RangeType = table_1_1->pDescriptorRanges[x].RangeType;
                                pDescriptorRanges_1_0[x].RegisterSpace = table_1_1->pDescriptorRanges[x].RegisterSpace;
                            }
                        }

                        D3D12_ROOT_DESCRIPTOR_TABLE *table_1_0 = &pParameters_1_0[n].DescriptorTable;
                        table_1_0->NumDescriptorRanges = table_1_1->NumDescriptorRanges;
                        table_1_0->pDescriptorRanges = pDescriptorRanges_1_0;
                    }
                    break;

                    default:
                        break;
                    }
                }
            }
            
            D3D12_STATIC_SAMPLER_DESC* pStaticSamplers = NULL;
            // Check note in the beginning of the file
#if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION >= 609)
            // I cleaned up a bit from the official class, where they mix accesses to Desc_1_1 and Desc_1_2 here just to avoid
            // creating an additional desc_1_2 variable (https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3dx12_root_signature.h#L1100)
            if (pRootSignatureDesc->Desc_1_2.NumStaticSamplers > 0 && pRootSignatureDesc->Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
            {
                const SIZE_T SamplersSize = sizeof(D3D12_STATIC_SAMPLER_DESC) * pRootSignatureDesc->Desc_1_2.NumStaticSamplers;
                // Points to a block of memory that will allocate our new D3D12_STATIC_SAMPLER_DESC samplers
                // that will substitute the original D3D12_STATIC_SAMPLER_DESC1 samplers
                pStaticSamplers = (D3D12_STATIC_SAMPLER_DESC*)(HeapAlloc(GetProcessHeap(), 0, SamplersSize));

                if (pStaticSamplers == NULL)
                {
                    hr = E_OUTOFMEMORY;
                }
                else
                {
                    /* Convert 1_2 samplers to 1_0 samplers if possible */
                    for (UINT n = 0; n < pRootSignatureDesc->Desc_1_2.NumStaticSamplers; ++n)
                    {
                        // Extract everything that is not D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR. If there is anything else, 
                        // we can't port to a D3D12_STATIC_SAMPLER_DESC, because new D3D12_SAMPLER_FLAGS may be incompatible
                        // (check microsoft.github.io/DirectX-Specs/d3d/VulkanOn12.html#non-normalized-texture-sampling-coordinates
                        // to see that D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES, for example, is not so simple)
                        // Note that the BorderColor field in D3D12_STATIC_SAMPLER_DESC is the enum D3D12_STATIC_BORDER_COLOR,
                        // which is fine to use with the UINT flag (even more, this is the standard behavior)
                        if ((pRootSignatureDesc->Desc_1_2.pStaticSamplers[n].Flags & ~D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR) != 0)
                        {
                            hr = E_INVALIDARG;
                            break;
                        }
                        // will discard the flags
                        memcpy(pStaticSamplers + n, pRootSignatureDesc->Desc_1_2.pStaticSamplers + n, sizeof(D3D12_STATIC_SAMPLER_DESC));
                    }
                }
            }
#endif

            if (SUCCEEDED(hr))
            {
                /* Call the serialize with the converted 1_0 from 1_1 */
                const D3D12_ROOT_SIGNATURE_DESC desc_1_0 = (D3D12_ROOT_SIGNATURE_DESC){
                    pRootSignatureDesc->Desc_1_1.NumParameters,
                    pParameters_1_0,
                    pRootSignatureDesc->Desc_1_1.NumStaticSamplers,
                    pStaticSamplers == NULL ? pRootSignatureDesc->Desc_1_1.pStaticSamplers : pStaticSamplers,
                    pRootSignatureDesc->Desc_1_1.Flags
                };
                hr = D3D12SerializeRootSignature(&desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, ppBlob, ppErrorBlob);
            }

            /* Clean resources used to create another desc */

            if (pParameters)
            {
                for (UINT n = 0; n < pRootSignatureDesc->Desc_1_1.NumParameters; n++)
                {
                    if (pRootSignatureDesc->Desc_1_1.pParameters[n].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                    {
                        const D3D12_DESCRIPTOR_RANGE *pDescriptorRanges_1_0 = pParameters_1_0[n].DescriptorTable.pDescriptorRanges;
                        HeapFree(GetProcessHeap(), 0, (void*)(pDescriptorRanges_1_0));
                    }
                }
                HeapFree(GetProcessHeap(), 0, pParameters);
            }

            if (pStaticSamplers)
            {
                HeapFree(GetProcessHeap(), 0, pStaticSamplers);
            }

            /* Send response about function result */
            return hr;
        }

        default:
            break;
        }
        break;

    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        switch (pRootSignatureDesc->Version)
        {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            return D3D12SerializeVersionedRootSignature(pRootSignatureDesc, ppBlob, ppErrorBlob);

            // Check note in the beginning of the file
#if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION >= 609)
        case D3D_ROOT_SIGNATURE_VERSION_1_2:
        {
            HRESULT hr = S_OK;
            D3D12_STATIC_SAMPLER_DESC* pStaticSamplers = NULL;
            if (pRootSignatureDesc->Desc_1_2.NumStaticSamplers > 0)
            {
                const SIZE_T SamplersSize = sizeof(D3D12_STATIC_SAMPLER_DESC) * pRootSignatureDesc->Desc_1_2.NumStaticSamplers;
                pStaticSamplers = (D3D12_STATIC_SAMPLER_DESC*)(HeapAlloc(GetProcessHeap(), 0, SamplersSize));

                if (pStaticSamplers == NULL)
                {
                    hr = E_OUTOFMEMORY;
                }
                else
                {
                    for (UINT n = 0; n < pRootSignatureDesc->Desc_1_2.NumStaticSamplers; ++n)
                    {
                        if ((pRootSignatureDesc->Desc_1_2.pStaticSamplers[n].Flags & ~D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR) != 0)
                        {
                            hr = E_INVALIDARG;
                            break;
                        }
                        memcpy(pStaticSamplers + n, pRootSignatureDesc->Desc_1_2.pStaticSamplers + n, sizeof(D3D12_STATIC_SAMPLER_DESC));
                    }
                }
            }

            if (SUCCEEDED(hr))
            {
                /* Call the serialize with the converted 1_1 from 1_2 */
                const D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedSignatureDesc_1_1 = {
                    .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
                    .Desc_1_1 = {
                        .NumParameters = pRootSignatureDesc->Desc_1_2.NumParameters,
                        .pParameters = pRootSignatureDesc->Desc_1_2.pParameters,
                        .NumStaticSamplers = pRootSignatureDesc->Desc_1_2.NumStaticSamplers,
                        .pStaticSamplers = pStaticSamplers == NULL ? pRootSignatureDesc->Desc_1_1.pStaticSamplers : pStaticSamplers,
                        .Flags = pRootSignatureDesc->Desc_1_1.Flags,
                    },
                };
                hr = D3D12SerializeVersionedRootSignature(&versionedSignatureDesc_1_1, ppBlob, ppErrorBlob);
            }

            if (pStaticSamplers)
            {
                HeapFree(GetProcessHeap(), 0, pStaticSamplers);
            }

            return hr;
        }
#endif

        default:
            break;
        }
        break;

#if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION >= 609)
    case D3D_ROOT_SIGNATURE_VERSION_1_2:
#endif
    default:
        return D3D12SerializeVersionedRootSignature(pRootSignatureDesc, ppBlob, ppErrorBlob);
    }

    return E_INVALIDARG;
}