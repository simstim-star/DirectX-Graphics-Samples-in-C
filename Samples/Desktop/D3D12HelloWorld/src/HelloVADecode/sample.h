#pragma once

#include "Windows.h"
#include <va/va.h>
#include <va/va_win32.h>
#include <stdbool.h>
#include <d3d12.h>

#define FrameCount 2

enum VA_H264DEC_BUFFER_INDEX {
	VA_H264DEC_BUFFER_INDEX_PIC = 0,
	VA_H264DEC_BUFFER_INDEX_QMATRIX = 1,
	VA_H264DEC_BUFFER_INDEX_COMPRESSED_BIT = 2,
	VA_H264DEC_BUFFER_INDEX_SLICE = 3,
	NUM_VA_H264DEC_BUFFER_INDEX
};

typedef struct IDXGISwapChain3 IDXGISwapChain3;
typedef struct IDXGIAdapter1 IDXGIAdapter1;

typedef struct DXSample
{
	// Viewport dimensions.
	UINT width;
	UINT height;
	float aspectRatio;
	// Adapter info.
	bool useWarpDevice;
	// Root assets path.
	WCHAR assetsPath[512];
	// Window title.
	CHAR* title;

	// Pipeline objects.
	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;
	IDXGISwapChain3* swapChain;
	ID3D12Device* device;
	IDXGIAdapter1* adapter;
	ID3D12Resource* renderTargets[FrameCount];
	ID3D12CommandAllocator* commandAllocator;
	ID3D12CommandQueue* commandQueue;
	ID3D12RootSignature* rootSignature;
	ID3D12DescriptorHeap* rtvHeap;
	ID3D12PipelineState* pipelineState;
	ID3D12GraphicsCommandList* commandList;

	UINT rtvDescriptorSize;

	// App resources.
	ID3D12Resource* vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

	// Synchronization objects.
	UINT frameIndex;
	HANDLE fenceEvent;
	ID3D12Fence* fence;
	UINT64 fenceValue;

	/* VA Objects */

	// Common VA objects / methods
	VADisplay vaDisplay;
	VASurfaceID VARenderTargets[FrameCount];
	VASurfaceID VASurfaceNV12;
	VAProcPipelineCaps ProcPipelineCaps;

	// Video Processor
	VAConfigID VAProcConfigId;

	// Context for color rgb to yuv conversion
	VAContextID vaColorConvCtx;
	VABufferID vaColorConvBuf;

	// Video Decode
	VAConfigID VADecConfigId;
	VAContextID VADecContextId;
	VABufferID VADecPipelineBufferId[NUM_VA_H264DEC_BUFFER_INDEX];
} DXSample;


void Sample_Init(DXSample* const sample);
void Sample_Destroy(DXSample* const sample);
void Sample_Update(DXSample* const sample);
void Sample_Render(DXSample* const sample);