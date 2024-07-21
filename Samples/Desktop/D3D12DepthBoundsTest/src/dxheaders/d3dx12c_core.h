#pragma once

#include <d3d12.h>

static inline D3D12_RESOURCE_DESC CD3DX12_RESOURCE_DESC_BUFFER(
	const UINT64 width,
	const D3D12_RESOURCE_FLAGS flags,
	const UINT64 alignment)
{
	return (D3D12_RESOURCE_DESC) {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Alignment = alignment,
		.Width = width,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_UNKNOWN,
		.SampleDesc = { .Count = 1, .Quality = 0 },
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = flags
	};
}

static inline D3D12_RASTERIZER_DESC CD3DX12_DEFAULT_RASTERIZER_DESC(void)
{
	return (D3D12_RASTERIZER_DESC) {
		.FillMode = D3D12_FILL_MODE_SOLID,
		.CullMode = D3D12_CULL_MODE_BACK,
		.FrontCounterClockwise = FALSE,
		.DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
		.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
		.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
		.DepthClipEnable = TRUE,
		.MultisampleEnable = FALSE,
		.AntialiasedLineEnable = FALSE,
		.ForcedSampleCount = 0,
		.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
	};
}

static inline D3D12_BLEND_DESC CD3DX12_DEFAULT_BLEND_DESC(void)
{
	D3D12_BLEND_DESC BlendDesc;
	BlendDesc.AlphaToCoverageEnable = FALSE;
	BlendDesc.IndependentBlendEnable = FALSE;
	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		BlendDesc.RenderTarget[i] = (D3D12_RENDER_TARGET_BLEND_DESC){
			.BlendEnable = FALSE, 
			.LogicOpEnable = FALSE,
			.SrcBlend = D3D12_BLEND_ONE,
			.DestBlend = D3D12_BLEND_ZERO,
			.BlendOp = D3D12_BLEND_OP_ADD,
			.SrcBlendAlpha = D3D12_BLEND_ONE,
			.DestBlendAlpha = D3D12_BLEND_ZERO,
			.BlendOpAlpha = D3D12_BLEND_OP_ADD,
			.LogicOp = D3D12_LOGIC_OP_NOOP,
			.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
		};
	}
	return BlendDesc;
}

static inline D3D12_DESCRIPTOR_RANGE1 CD3DX12_DESCRIPTOR_RANGE1(
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT numDescriptors,
	UINT baseShaderRegister,
	UINT registerSpace,
	D3D12_DESCRIPTOR_RANGE_FLAGS flags,
	UINT offsetInDescriptorsFromTableStart)
{
	return (D3D12_DESCRIPTOR_RANGE1) {
		.RangeType = rangeType,
		.NumDescriptors = numDescriptors,
		.BaseShaderRegister = baseShaderRegister,
		.RegisterSpace = registerSpace,
		.Flags = flags,
		.OffsetInDescriptorsFromTableStart = offsetInDescriptorsFromTableStart,
	};
}

static inline D3D12_ROOT_PARAMETER1 CD3DX12_ROOT_PARAMETER1_AsDescriptorTable(
	UINT numDescriptorRanges,
	_In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1* pDescriptorRanges,
	D3D12_SHADER_VISIBILITY visibility)
{
	return (D3D12_ROOT_PARAMETER1) {
		.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
		.ShaderVisibility = visibility,
		.DescriptorTable.NumDescriptorRanges = numDescriptorRanges,
		.DescriptorTable.pDescriptorRanges = pDescriptorRanges,
	};
}

static inline D3D12_DEPTH_STENCIL_DESC CD3DX12_DEFAULT_DEPTH_STENCIL_DESC(void)
{
	const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = { 
		.StencilFailOp = D3D12_STENCIL_OP_KEEP, 
		.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP, 
		.StencilPassOp = D3D12_STENCIL_OP_KEEP, 
		.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS 
	};

	return (D3D12_DEPTH_STENCIL_DESC) {
		.DepthEnable = TRUE,
		.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
		.DepthFunc = D3D12_COMPARISON_FUNC_LESS,
		.StencilEnable = FALSE,
		.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
		.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
		.FrontFace = defaultStencilOp,
		.BackFace = defaultStencilOp,
	};
}

static inline D3D12_HEAP_PROPERTIES CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE type) {
	return (D3D12_HEAP_PROPERTIES) {
		.Type = type,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 1,
		.VisibleNodeMask = 1,
	};
}

static inline D3D12_RESOURCE_DESC CD3DX12_RESOURCE_DESC(
	D3D12_RESOURCE_DIMENSION dimension,
	UINT64 alignment,
	UINT64 width,
	UINT height,
	UINT16 depthOrArraySize,
	UINT16 mipLevels,
	DXGI_FORMAT format,
	UINT sampleCount,
	UINT sampleQuality,
	D3D12_TEXTURE_LAYOUT layout,
	D3D12_RESOURCE_FLAGS flags)
{
	return (D3D12_RESOURCE_DESC) {
		.Dimension = dimension,
		.Alignment = alignment,
		.Width = width,
		.Height = height,
		.DepthOrArraySize = depthOrArraySize,
		.MipLevels = mipLevels,
		.Format = format,
		.SampleDesc.Count = sampleCount,
		.SampleDesc.Quality = sampleQuality,
		.Layout = layout,
		.Flags = flags,
	};
}

static inline D3D12_RESOURCE_DESC CD3DX12_TEX2D(
	DXGI_FORMAT format,
	UINT64 width,
	UINT height,
	UINT16 arraySize,
	UINT16 mipLevels,
	UINT sampleCount,
	UINT sampleQuality,
	D3D12_RESOURCE_FLAGS flags,
	D3D12_TEXTURE_LAYOUT layout,
	UINT64 alignment)
{
	return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE2D, alignment, width, height, arraySize,
		mipLevels, format, sampleCount, sampleQuality, layout, flags);
}