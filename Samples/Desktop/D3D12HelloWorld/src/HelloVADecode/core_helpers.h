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
