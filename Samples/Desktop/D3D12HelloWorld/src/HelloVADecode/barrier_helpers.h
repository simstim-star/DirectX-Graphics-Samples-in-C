#pragma once

#include <d3d12.h>

static inline D3D12_RESOURCE_BARRIER CD3DX12_Transition(
	_In_ ID3D12Resource* const pResource,
	const D3D12_RESOURCE_STATES stateBefore,
	const D3D12_RESOURCE_STATES stateAfter,
	const UINT subresource,
	const D3D12_RESOURCE_BARRIER_FLAGS flags)
{
	return (D3D12_RESOURCE_BARRIER) {
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Flags = flags,
			.Transition = {
				.pResource = pResource,
				.StateBefore = stateBefore,
				.StateAfter = stateAfter,
				.Subresource = subresource,
		}
	};
}