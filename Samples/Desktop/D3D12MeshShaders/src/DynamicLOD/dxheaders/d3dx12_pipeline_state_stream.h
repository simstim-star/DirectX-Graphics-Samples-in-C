#pragma once
#include <d3d12.h>
#include "core_helpers.h"

/******************************************************************************************************************************************************************************************
 * READ THIS TO UNDERSTAND THE CODE BELOW																																				  *
 *																																														  *
 * When we call ID3D12Device2_CreatePipelineState, we will pass a D3D12_PIPELINE_STATE_STREAM_DESC																						  *
 * This desc contains a pPipelineStateSubobjectStream, which is a void*. This pointer should be the																						  *
 * address of a data structure that describes as a bytestream an arbitrary pipeline state subobject.																					  *
 * This pipeline state subobject should be composed of other void* aligned objects defined by																							  *
 * {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE, <ADEQUATE-DATA-FOR-TYPE>}.																														  *
 *																																														  *
 * References:																																											  *
 * D3D12_PIPELINE_STATE_STREAM_DESC - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_pipeline_state_stream_desc												  *
 * D3D12_PIPELINE_STATE_SUBOBJECT_TYPE - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_pipeline_state_subobject_type											  *
 * CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT - https://learn.microsoft.com/pt-br/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-subobject (we implement the structs below directly  *
 * instead of using template specializations)																																			  *
 ******************************************************************************************************************************************************************************************/

// This is just a helper to avoid writing the whole pipelineMeshStateStreamFromDesc code in the sample code
typedef struct D3DX12_MESH_SHADER_PIPELINE_STATE_DESC
{
	ID3D12RootSignature*          pRootSignature;
	D3D12_SHADER_BYTECODE         AS;
	D3D12_SHADER_BYTECODE         MS;
	D3D12_SHADER_BYTECODE         PS;
	D3D12_BLEND_DESC              BlendState;
	UINT                          SampleMask;
	D3D12_RASTERIZER_DESC         RasterizerState;
	D3D12_DEPTH_STENCIL_DESC      DepthStencilState;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
	UINT                          NumRenderTargets;
	DXGI_FORMAT                   RTVFormats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
	DXGI_FORMAT                   DSVFormat;
	DXGI_SAMPLE_DESC              SampleDesc;
	UINT                          NodeMask;
	D3D12_CACHED_PIPELINE_STATE   CachedPSO;
	D3D12_PIPELINE_STATE_FLAGS    Flags;
} D3DX12_MESH_SHADER_PIPELINE_STATE_DESC;

#if defined(_MSC_VER)
#if defined(_WIN64)
#define ALIGN_PTR __declspec(align(8))
#else
#define ALIGN_PTR __declspec(align(4))
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define ALIGN_PTR __attribute__((aligned(sizeof(void*))))
#else
#error "ALIGN_PTR is not defined for this compiler."
#endif

// Reference https://learn.microsoft.com/pt-br/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-flags
// It's defined as typedef CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT<D3D12_PIPELINE_STATE_FLAGS, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS> CD3DX12_PIPELINE_STATE_STREAM_FLAGS
// in C++. 
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_FLAGS {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; 
	D3D12_PIPELINE_STATE_FLAGS inner;
} CD3DX12_PIPELINE_STATE_STREAM_FLAGS;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-node-mask
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; 
	UINT inner;
} CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-root-signature
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	ID3D12RootSignature* inner;
} CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE;

// https://learn.microsoft.com/pt-br/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-ps
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_PS {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_SHADER_BYTECODE inner;
} CD3DX12_PIPELINE_STATE_STREAM_PS;

// https://github.com/microsoft/D3D12TranslationLayer/blob/master/external/d3dx12.h#L2330C1-L2330C127
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_AS {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_SHADER_BYTECODE inner;
} CD3DX12_PIPELINE_STATE_STREAM_AS;

// https://github.com/microsoft/D3D12TranslationLayer/blob/master/external/d3dx12.h#L2331C1-L2331C126
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_MS {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_SHADER_BYTECODE inner;
} CD3DX12_PIPELINE_STATE_STREAM_MS;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-blend-desc
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_BLEND_DESC inner;
} CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-depth-stencil1
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1 {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_DEPTH_STENCIL_DESC1 inner;
} CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-depth-stencil-format
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	DXGI_FORMAT inner;
} CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-rasterizer
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_RASTERIZER_DESC inner;
} CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-render-target-formats
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	struct D3D12_RT_FORMAT_ARRAY inner;
} CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-sample-desc
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	DXGI_SAMPLE_DESC inner;
} CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-sample-mask
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	UINT inner;
} CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-cached-pso
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_CACHED_PSO {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_CACHED_PIPELINE_STATE inner;
} CD3DX12_PIPELINE_STATE_STREAM_CACHED_PSO;

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/cd3dx12-pipeline-state-stream-view-instancing
typedef struct ALIGN_PTR CD3DX12_PIPELINE_STATE_STREAM_VIEW_INSTANCING {
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
	D3D12_VIEW_INSTANCING_DESC inner;
} CD3DX12_PIPELINE_STATE_STREAM_VIEW_INSTANCING;

// https://github.com/microsoft/D3D12TranslationLayer/blob/master/external/d3dx12.h#L2527
typedef struct CD3DX12_PIPELINE_MESH_STATE_STREAM
{
	CD3DX12_PIPELINE_STATE_STREAM_FLAGS Flags;
	CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK NodeMask;
	CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
	CD3DX12_PIPELINE_STATE_STREAM_PS PS;
	CD3DX12_PIPELINE_STATE_STREAM_AS AS;
	CD3DX12_PIPELINE_STATE_STREAM_MS MS;
	CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
	CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1 DepthStencilState;
	CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
	CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
	CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
	CD3DX12_PIPELINE_STATE_STREAM_CACHED_PSO CachedPSO;
	CD3DX12_PIPELINE_STATE_STREAM_VIEW_INSTANCING ViewInstancingDesc;
} CD3DX12_PIPELINE_MESH_STATE_STREAM;

CD3DX12_PIPELINE_MESH_STATE_STREAM pipelineMeshStateStreamFromDesc(const D3DX12_MESH_SHADER_PIPELINE_STATE_DESC* const desc) {
	return (CD3DX12_PIPELINE_MESH_STATE_STREAM) {
		.Flags = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,
			.inner = desc->Flags,
		},
		.NodeMask = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK,
			.inner = desc->NodeMask,
		},
		.pRootSignature = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
			.inner = desc->pRootSignature,
		},
		.PS = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
			.inner = desc->PS,
		},
		.AS = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS,
			.inner = desc->AS,
		},
		.MS = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS,
			.inner = desc->MS,
		},
		.BlendState = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
			.inner = desc->BlendState,
		},
		.DepthStencilState = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1,
			.inner = CD3DX12_TO_DEPTH_STENCIL_DESC1(&desc->DepthStencilState),
		},
		.DSVFormat = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
			.inner = desc->DSVFormat,
		},
		.RasterizerState = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
			.inner = desc->RasterizerState,
		},
		.RTVFormats = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
			.inner = CD3DX12_RT_FORMAT_ARRAY(desc->RTVFormats, desc->NumRenderTargets),
		},
		.SampleDesc = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
			.inner = desc->SampleDesc,
		},
		.SampleMask = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
			.inner = desc->SampleMask,
		},
		.CachedPSO = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO,
			.inner = desc->CachedPSO,
		},
		.ViewInstancingDesc = {
			.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
			.inner = CD3DX12_DEFAULT_VIEW_INSTANCING_DESC(),
		},
	};
}
