#pragma once
#include <d3d12.h>

/******************************************************************************************************************************************************
The macro below defines a types and a converter function:
- a union CD3DX12_PIPELINE_STATE_STREAM_[[PIPELINE_STATE_TYPE]]: D3D12 runtime expects subobjects to be aligned to the natural word alignment 
of the system. This union is a way to guarantee that instead of using alignas.
- TO_PIPELINE_STATE_STREAM_[[PIPELINE_STATE_TYPE]]: a function to convert from type to the equivalent CD3DX12_PIPELINE_STATE_STREAM_[[PIPELINE_STATE_TYPE]]

Reference: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_pipeline_state_stream_desc
******************************************************************************************************************************************************/
#define CD3DX12_PIPELINE_STATE_STREAM(PIPELINE_STATE_TYPE, TYPE) typedef union CD3DX12_PIPELINE_STATE_STREAM_ ## PIPELINE_STATE_TYPE  \
{                                                                    \
	struct {                                                         \
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;                    \
		TYPE object;                                                 \
	};                                                               \
	void* raw;                                                       \
} CD3DX12_PIPELINE_STATE_STREAM_ ## PIPELINE_STATE_TYPE;             \
                                                                     \
CD3DX12_PIPELINE_STATE_STREAM_ ## PIPELINE_STATE_TYPE TO_PIPELINE_STATE_STREAM_ ## PIPELINE_STATE_TYPE(TYPE obj) {  \
   CD3DX12_PIPELINE_STATE_STREAM_ ## PIPELINE_STATE_TYPE token;              \
   token.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_## PIPELINE_STATE_TYPE;  \
   token.object = obj;                                                       \
   return token;                                                             \
}

CD3DX12_PIPELINE_STATE_STREAM(ROOT_SIGNATURE, ID3D12RootSignature*)
CD3DX12_PIPELINE_STATE_STREAM(INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC)
CD3DX12_PIPELINE_STATE_STREAM(PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE)
CD3DX12_PIPELINE_STATE_STREAM(VS, D3D12_SHADER_BYTECODE)
CD3DX12_PIPELINE_STATE_STREAM(PS, D3D12_SHADER_BYTECODE)
CD3DX12_PIPELINE_STATE_STREAM(DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1)
CD3DX12_PIPELINE_STATE_STREAM(DEPTH_STENCIL_FORMAT, DXGI_FORMAT)
CD3DX12_PIPELINE_STATE_STREAM(RENDER_TARGET_FORMATS, struct D3D12_RT_FORMAT_ARRAY) // MS forgot to typedef it, lol
