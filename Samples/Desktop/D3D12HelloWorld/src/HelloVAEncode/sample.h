#pragma once

#include "windows.h"
#include <va/va.h>
#include <va/va_win32.h>
#include <d3d12.h>
#include <stdio.h>

#define FRAME_COUNT          2
#define VA_NUM_RGBA_SURFACES 16
#define ALPHA_BLEND			 0.75f
#define REGIONS_SIZE_RATIO   1.2f
#define REGIONS_VARIATIONS   216
#define H264_MB_PIXEL_SIZE	 16

enum VA_H264DEC_BUFFER_INDEX {
	VA_H264ENC_BUFFER_INDEX_SEQ            = 0,
	VA_H264ENC_BUFFER_INDEX_PIC            = 1,
	VA_H264ENC_BUFFER_INDEX_SLICE          = 2,
	VA_H264ENC_BUFFER_INDEX_COMPRESSED_BIT = 3,
	NUM_VA_H264DEC_BUFFER_INDEX
};

typedef struct IDXGISwapChain3 IDXGISwapChain3;
typedef struct IDXGIAdapter1 IDXGIAdapter1;

typedef struct VideoAcceleration {
	// Common VA objects / methods
	VADisplay display;
	
	/* Note: We will have 16 RGBA surfaces
	 * 
	 */

	VASurfaceID renderTargets[FRAME_COUNT];
	VASurfaceID surfacesRGBA[VA_NUM_RGBA_SURFACES];
	VASurfaceID surfaceNV12;
	VAProcPipelineCaps procPipelineCaps;
	UINT numVPRegions;

	// Video Processor
	VAConfigID procConfigId;

	// Context for color rgb to yuv conversion
	VAContextID colorConvCtx;
	VABufferID colorConvBuf;

	// Context for single RGB -> RGB copy
	VAContextID copyCtx;
	VABufferID copyBuf;

	// Video Encode
	VAConfigID encConfigId;
	VAContextID encContextId;
	VABufferID encPipelineBufferId[NUM_VA_H264DEC_BUFFER_INDEX];
	FILE* writer;

	// Context for multiple RGB -> RGB blend
	VAContextID blendCtx;
	VABufferID blendBuf;
	UINT curRegionVariation;
	VARectangle blendRegions[REGIONS_VARIATIONS/*Prepare two sets of regions so there's some motion*/][VA_NUM_RGBA_SURFACES];
	float colors[REGIONS_VARIATIONS][4];
} VideoAcceleration;

typedef struct DXSample
{
	// Viewport dimensions.
	UINT width;
	UINT height;
	// Adapter info.
	BOOL useWarpDevice;
	// Window title.
	CHAR* title;

	// Pipeline objects.
	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;
	IDXGISwapChain3* swapChain;
	ID3D12Device* device;
	IDXGIAdapter1* adapter;
	ID3D12Resource* renderTargets[FRAME_COUNT];
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
	VideoAcceleration va;
} DXSample;


void Sample_Init(DXSample* const sample);
void Sample_Destroy(DXSample* const sample);
void Sample_Update(DXSample* const sample);
void Sample_Render(DXSample* const sample);