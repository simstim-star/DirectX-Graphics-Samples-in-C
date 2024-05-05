#include <dxgi1_6.h>
#include <assert.h>

#include "d3d12video.h"
#include "dxgi.h"
#include "sample.h"
#include "sample_commons.h"
#include "window.h"
#include "d3dcompiler.h"
#include "dxheaders/d3dx12_macros.h"
#include "dxheaders/d3dx12c_barriers.h"
#include "dxheaders/d3dx12c_core.h"

#include "DecodeParamBuffer.h"


/*************************************************************************************
 Forward declarations of private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample);
static void LoadAssets(DXSample* const sample);
static void WaitForPreviousFrame(DXSample* const sample);
static void PopulateCommandList(DXSample* const sample);
static void ReleaseAll(DXSample* const sample);

/*************************************************************************************
 Forward declarations of Video Acceleration functions
**************************************************************************************/

/* Common VA methods */

void LoadVAPipeline(DXSample* const sample);
void InitVADisplay(DXSample* const sample);
void ImportRenderTargetsToVA(DXSample* const sample);
void PerformVAWorkload(DXSample* const sample);
void DestroyVA(DXSample* const sample);
void CreateVASurfaces(DXSample* const sample);

/* Video Processor */

void EnsureVAProcSupport(DXSample* const sample);
void InitVAProcContext(DXSample* const sample);

/* Context for color rgb to yuv conversion */

void PerformVABlit(
	DXSample* const sample,
	VAContextID context,
	VABufferID buffer,
	VASurfaceID* pInSurfaces,
	UINT inSurfacesCount,
	VARectangle* pSrcRegions,
	VARectangle* pDstRegions,
	VASurfaceID dstSurface,
	float alpha);
void DestroyVAProc(DXSample* const sample);

/* Video Decode */

void EnsureVADecSupport(DXSample* const sample);
void InitVADecContext(DXSample* const sample);
void PerformVADecodeFrame(DXSample* const sample);
void DestroyVADec(DXSample* const sample);


/*************************************************************************************
 Public functions
**************************************************************************************/

void Sample_Init(DXSample* const sample) {
	sample->aspectRatio = (float)(sample->width) / (float)(sample->height);
	sample->viewport = (D3D12_VIEWPORT){ 0.0f, 0.0f, (float)(sample->width), (float)(sample->height) };
	sample->scissorRect = (D3D12_RECT){ 0, 0, (LONG)(sample->width), (LONG)(sample->height) };

	GetCurrentPath(sample->assetsPath, _countof(sample->assetsPath));
	LoadPipeline(sample);
	LoadAssets(sample);
	LoadVAPipeline(sample);
}

void Sample_Destroy(DXSample* const sample)
{
	WaitForPreviousFrame(sample);
	DestroyVA(sample);
	CloseHandle(sample->fenceEvent);
	ReleaseAll(sample);
}

void Sample_Update(DXSample* const sample) {}

void Sample_Render(DXSample* const sample)
{
	// Record all the commands we need to render the scene into the command list.
	// In this case, clear the render target with a predefined color.
	PopulateCommandList(sample);

	// Execute the command list.
	ID3D12CommandList* asCommandList = NULL;
	CAST(sample->commandList, asCommandList);
	ID3D12CommandList* ppCommandLists[] = { asCommandList };
	CALL(ExecuteCommandLists, sample->commandQueue, _countof(ppCommandLists), ppCommandLists);
	RELEASE(asCommandList);

	// Before calling PerformVAWorkload, we must ensure the following:
	//  1. The D3D12 resources to be used must be in D3D12_RESOURCE_STATE_COMMON state
	//      * PopulateCommandList is already transitioning the resource to D3D12_RESOURCE_STATE_PRESENT
	//          which happens to also match the definition of D3D12_RESOURCE_STATE_COMMON
	//  2. The D3D12 resources must not have any pending GPU operations
	//      * Call WaitForPreviousFrame below for this end, to wait for the ExecuteCommandLists below
	//          that clears this render target with a predefined solid color.

	WaitForPreviousFrame(sample);

	// Perform the VA workload on the current render target.
	// The VA driver internally manages any other state transitions and it is expected that
	// PerformVAWorkload calls vaSyncSurface, which ensures the affected resources are
	// back in COMMON state and all the GPU work flushed and finished on them.
	// Currently only m_VARenderTargets[m_frameIndex] is used in the VA workload,
	// transition it back to present mode for the call below.

	PerformVAWorkload(sample);

	// Present the frame.
	ExitIfFailed(CALL(Present, sample->swapChain, 1, 0));

	WaitForPreviousFrame(sample);
}

/*************************************************************************************
 Private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample)
{
	int isDebugFactory = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	ID3D12Debug* debugController = NULL;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		CALL(EnableDebugLayer, debugController);
		isDebugFactory |= DXGI_CREATE_FACTORY_DEBUG;
		RELEASE(debugController);
	}
#endif

	IDXGIFactory4* factory = NULL;
	ExitIfFailed(CreateDXGIFactory2(isDebugFactory, IID_PPV_ARGS(&factory)));

	/* Create device */

	IDXGIFactory1* factoryAsFactory1 = NULL;
	ExitIfFailed(CAST(factory, factoryAsFactory1));
	GetHardwareAdapter(factoryAsFactory1, &sample->adapter, false);
	RELEASE(factoryAsFactory1);

	IUnknown* hardwareAdapterAsUnknown = NULL;
	ExitIfFailed(CAST(sample->adapter, hardwareAdapterAsUnknown));
	ExitIfFailed(D3D12CreateDevice(hardwareAdapterAsUnknown, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&sample->device)));
	RELEASE(hardwareAdapterAsUnknown);

	/* Create command queue */

	D3D12_COMMAND_QUEUE_DESC queueDesc = { 
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE, 
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT 
	};
	ID3D12CommandQueue* commandQueue = NULL;
	ExitIfFailed(CALL(CreateCommandQueue, sample->device, &queueDesc, IID_PPV_ARGS(&sample->commandQueue)));

	/* Create swap chain */

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
		.BufferCount = 2,
		.Width = sample->width,
		.Height = sample->height,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.SampleDesc.Count = 1,
	};

	IUnknown* commandQueueAsIUnknown = NULL;
	ExitIfFailed(CAST(sample->commandQueue, commandQueueAsIUnknown));
	IDXGISwapChain1* swapChainAsSwapChain1 = NULL;
	ExitIfFailed(CALL(CreateSwapChainForHwnd,
		factory,
		commandQueueAsIUnknown,        // Swap chain needs the queue so that it can force a flush on it
		G_HWND,
		&swapChainDesc,
		NULL,
		NULL,
		&swapChainAsSwapChain1
	));
	RELEASE(commandQueueAsIUnknown);
	ExitIfFailed(CAST(swapChainAsSwapChain1, sample->swapChain));
	RELEASE(swapChainAsSwapChain1);
	ExitIfFailed(CALL(MakeWindowAssociation, factory, G_HWND, DXGI_MWA_NO_ALT_ENTER));
	sample->frameIndex = CALL(GetCurrentBackBufferIndex, sample->swapChain);

	/* Create descriptor heaps (only 2 RTVs in this example) */
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.NumDescriptors = FrameCount,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};
		ExitIfFailed(CALL(CreateDescriptorHeap, sample->device, &rtvHeapDesc, IID_PPV_ARGS(&sample->rtvHeap)));
		sample->rtvDescriptorSize = CALL(GetDescriptorHandleIncrementSize, sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	/* Create frame resources on the descriptor heaps above */
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
		CALL(GetCPUDescriptorHandleForHeapStart, sample->rtvHeap, &rtvHandle);
		// Create a RTV for each frame.
		for (UINT nthSwapChainBuffer = 0; nthSwapChainBuffer < FrameCount; nthSwapChainBuffer++)
		{
			// set resource at renderTargets[nthSwapChainBuffer] as the nth-SwapChainBuffer
			ExitIfFailed(CALL(GetBuffer, sample->swapChain, nthSwapChainBuffer, IID_PPV_ARGS(&sample->renderTargets[nthSwapChainBuffer])));
			// create a RTV on the heap related to the handle
			CALL(CreateRenderTargetView, sample->device, sample->renderTargets[nthSwapChainBuffer], NULL, rtvHandle);
			// walk an offset equivalent to one descriptor to go to next space to store the next RTV
			rtvHandle.ptr = (SIZE_T)((INT64)(rtvHandle.ptr) + (INT64)(sample->rtvDescriptorSize));
		}
	}
	ExitIfFailed(CALL(CreateCommandAllocator, sample->device, D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sample->commandAllocator)));
	RELEASE(factory);
}

static void LoadAssets(DXSample* const sample)
{
	/* Create the command list */
	ExitIfFailed(CALL(CreateCommandList, sample->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, sample->commandAllocator, NULL, IID_PPV_ARGS(&sample->commandList)));
	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now
	ExitIfFailed(CALL(Close, sample->commandList));

	// Create synchronization objects and wait until assets have been uploaded to the GPU
	{
		ExitIfFailed(CALL(CreateFence, sample->device, 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sample->fence)));
		sample->fenceValue = 1;

		// Create an event handle to use for frame synchronization
		sample->fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (sample->fenceEvent == NULL)
		{
			ExitIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop, but for now, we just want to wait for setup to 
		// complete before continuing
		WaitForPreviousFrame(sample);
	}
}

// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
// It's implemented as such for simplicity, but doesn't really make good use of the frame
// buffers, as we always wait for all work to be done before starting work on the other buffer.
// In the D3D12HelloFrameBuffering example, we keep a fenceValue for each buffer to keep track if
// the work for this buffer is already done and it's ready to receive more.
static void WaitForPreviousFrame(DXSample* const sample)
{
	// Signal to the fence the current fenceValue
	ExitIfFailed(CALL(Signal, sample->commandQueue, sample->fence, sample->fenceValue));
	// Wait until the frame is finished (ie. reached the signal sent right above) 
	if (CALL(GetCompletedValue, sample->fence) < sample->fenceValue)
	{
		ExitIfFailed(CALL(SetEventOnCompletion, sample->fence, sample->fenceValue, sample->fenceEvent));
		WaitForSingleObject(sample->fenceEvent, INFINITE);
	}
	sample->fenceValue++;
	sample->frameIndex = CALL(GetCurrentBackBufferIndex, sample->swapChain);
}

// Record all the commands we need to render the scene into the command list
static void PopulateCommandList(DXSample* const sample)
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress
	ExitIfFailed(CALL(Reset, sample->commandAllocator));

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording
	ExitIfFailed(CALL(Reset, sample->commandList, sample->commandAllocator, sample->pipelineState));

	// Indicate that the back buffer will be used as a render target
	const D3D12_RESOURCE_BARRIER transitionBarrierRT = CD3DX12_Transition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_NONE);
	CALL(ResourceBarrier, sample->commandList, 1, &transitionBarrierRT);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	CALL(GetCPUDescriptorHandleForHeapStart, sample->rtvHeap, &rtvHandle);
	const INT64 CurrentRtvOffset = sample->frameIndex * sample->rtvDescriptorSize;
	rtvHandle.ptr = (SIZE_T)((INT64)(rtvHandle.ptr) + CurrentRtvOffset);

	// Record commands
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	CALL(ClearRenderTargetView, sample->commandList, rtvHandle, clearColor, 0, NULL);

	D3D12_RESOURCE_BARRIER transitionBarrierPresent = CD3DX12_Transition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_NONE);

	// Indicate that the back buffer will now be used to present
	CALL(ResourceBarrier, sample->commandList, 1, &transitionBarrierPresent);
	ExitIfFailed(CALL(Close, sample->commandList));
}

void ReleaseAll(DXSample* const sample)
{
	RELEASE(sample->swapChain);
	RELEASE(sample->device);
	for (int i = 0; i < FrameCount; ++i) {
		RELEASE(sample->renderTargets[i]);
	}
	RELEASE(sample->commandAllocator);
	RELEASE(sample->commandQueue);
	RELEASE(sample->rootSignature);
	RELEASE(sample->rtvHeap);
	RELEASE(sample->pipelineState);
	RELEASE(sample->commandList);
	RELEASE(sample->vertexBuffer);
	RELEASE(sample->fence);
}


/*************************************************************************************
 Implementation of Video Acceleration functions
**************************************************************************************/
void LoadVAPipeline(DXSample* const sample)
{
	InitVADisplay(sample);
	EnsureVAProcSupport(sample);
	EnsureVADecSupport(sample);
	CreateVASurfaces(sample);
	ImportRenderTargetsToVA(sample);
	InitVAProcContext(sample);
	InitVADecContext(sample);
}

/* Init sample->vaDisplay based on the sample->adapter */
void InitVADisplay(DXSample* const sample) {
	DXGI_ADAPTER_DESC desc = { 0 };
	ExitIfFailed(CALL(GetDesc, sample->adapter, &desc));
	sample->vaDisplay = vaGetDisplayWin32(&desc.AdapterLuid);
	assert(sample->vaDisplay);

	int major_ver, minor_ver;
	VAStatus va_status = vaInitialize(sample->vaDisplay, &major_ver, &minor_ver);
	vaExitIfFailed(va_status, "vaInitialize");
}

/* Creates VASurfaces on the render targets, so that we can render there with VA */
void ImportRenderTargetsToVA(DXSample* const sample) {
	VASurfaceAttrib createSurfacesAttribList[3] = {
		{
			.type = VASurfaceAttribPixelFormat,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type = VAGenericValueTypeInteger,
				// Based on the default render target
				// format DXGI_FORMAT_R8G8B8A8_UNORM
				.value = VA_FOURCC_RGBA,
			},
		},
		{
			.type = VASurfaceAttribMemoryType,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type =  VAGenericValueTypeInteger,
				// Using NTHandles for interop is the safe way of sharing resources between the 
				// VADisplay D3D12 device and the app/sample D3D12 independent devices
				.value = VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE,
			},
		},
		// handles to the render targets
		{
			VASurfaceAttribExternalBufferDescriptor,
			VA_SURFACE_ATTRIB_SETTABLE,
			{
				VAGenericValueTypePointer,
				// createSurfacesAttribList[2].value.value.p is set in code below
				0,
			},
		},
	};

	// Array of handles to render targets that we will associate with 
	// corresponding output surface in the call to vaCreateSurfaces
	HANDLE renderTargets[FrameCount];

	// Create a handle to the render target for each frame
	for (size_t i = 0; i < FrameCount; i++)
	{
		ID3D12DeviceChild* renderTargetResourceAsDeviceChild = NULL;
		ExitIfFailed(CAST(sample->renderTargets[i], renderTargetResourceAsDeviceChild));
		// take a shared handle to a render target
		HRESULT hr = CALL(CreateSharedHandle, sample->device, renderTargetResourceAsDeviceChild,
			NULL,
			GENERIC_ALL,
			NULL,
			&renderTargets[i]);
		ExitIfFailed(hr);
		RELEASE(renderTargetResourceAsDeviceChild);
	}
	createSurfacesAttribList[2].value.value.p = renderTargets;

	// Creates VASurface objects by importing handles of existing D3D12 resources 
	// (ie. create va surfaces in the render targets)
	VAStatus va_status = vaCreateSurfaces(
		sample->vaDisplay,
		VA_RT_FORMAT_RGB32,
		sample->width,
		sample->height,
		sample->VARenderTargets,
		FrameCount,
		createSurfacesAttribList,
		_countof(createSurfacesAttribList));
	vaExitIfFailed(va_status, "vaCreateSurfaces");
}

void PerformVAWorkload(DXSample* const sample)
{
	// Decode H264 into a NV12 surface
	PerformVADecodeFrame(sample);
	// Color convert NV12 into the RGBA render target
	PerformVABlit(sample,
		sample->vaColorConvCtx,
		sample->vaColorConvBuf,
		&sample->VASurfaceNV12,
		1,
		NULL,
		NULL,
		sample->VARenderTargets[sample->frameIndex],
		1.0f
	);
}

void DestroyVA(DXSample* const sample) {
	DestroyVAProc(sample);
	DestroyVADec(sample);

	VAStatus va_status = vaDestroySurfaces(sample->vaDisplay, sample->VARenderTargets, FrameCount);
	vaExitIfFailed(va_status, "vaDestroySurfaces");

	va_status = vaDestroySurfaces(sample->vaDisplay, &sample->VASurfaceNV12, 1);
	vaExitIfFailed(va_status, "vaDestroySurfaces");

	vaTerminate(sample->vaDisplay);
	vaExitIfFailed(va_status, "vaTerminate");
}

/* Creates the sample->VASurfaceNV12, a NV12 surface to receive the image data */
void CreateVASurfaces(DXSample* const sample)
{
	VASurfaceAttrib createSurfacesAttribList[2] = {
		// Describes the pixel format of the surface
		{
			.type = VASurfaceAttribPixelFormat,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type = VAGenericValueTypeInteger,
				/*
				 - FourCC ("four-character code") is a sequence of four bytes used to uniquely identify data formats.
				 - NV12 YUV format: colour information (Cr, Cb) is stored at a lower resolution than the intensity data (Y). 
				 https://en.wikipedia.org/wiki/YCbCr
				 https://learn.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering#nv12
				*/
				.value = VA_FOURCC_NV12,
			},
		},
		{
			.type = VASurfaceAttribMemoryType,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type = VAGenericValueTypeInteger,
				.value = VA_SURFACE_ATTRIB_MEM_TYPE_VA,
			},
		},
	};

	VAStatus va_status = vaCreateSurfaces(
		sample->vaDisplay,
		VA_RT_FORMAT_YUV420,
		sample->width,
		sample->height,
		&sample->VASurfaceNV12,
		1,
		createSurfacesAttribList,
		_countof(createSurfacesAttribList)
	);
	vaExitIfFailed(va_status, "vaCreateSurfaces");
}

/* Checks if our vaDisplay supports 
  - video pre/post-processing 
  - blit from DXGI_FORMAT_R8G8B8A8_NV12 to DXGI_FORMAT_R8G8B8A8_UNORM
*/
void EnsureVAProcSupport(DXSample* const sample) {
	// an entrypoint shows funcionality your card is capable of
	int num_entrypoints = vaMaxNumEntrypoints(sample->vaDisplay);
	VAEntrypoint* entrypoints = HeapAlloc(GetProcessHeap(), 0, num_entrypoints * sizeof(VAEntrypoint));
	VAStatus va_status = vaQueryConfigEntrypoints(
		sample->vaDisplay,
		VAProfileNone,
		entrypoints,
		&num_entrypoints);
	vaExitIfFailed(va_status, "vaQueryConfigEntrypoints for VAProfileNone");

	bool supportsVideoProcessing = false;
	for (int32_t i = 0; !supportsVideoProcessing && i < num_entrypoints; i++) {
		if (entrypoints[i] == VAEntrypointVideoProc)
			supportsVideoProcessing = true;
	}

	if (!supportsVideoProcessing) {
		vaExitIfFailed(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT, "VAEntrypointVideoProc not supported.");
	}

	// Check VPBlit support for format DXGI_FORMAT_R8G8B8A8_NV12 -> DXGI_FORMAT_R8G8B8A8_UNORM
	D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT dx12ProcCaps =
	{
		.NodeIndex = 0,
		.InputSample = { sample->width, sample->height, { DXGI_FORMAT_NV12, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 }},
		.InputFieldType = D3D12_VIDEO_FIELD_TYPE_NONE,
		.InputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE,
		.InputFrameRate = { 30, 1 },
		.OutputFormat = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709},
		.OutputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE,
		.OutputFrameRate = { 30, 1 },
	};
	
	ID3D12VideoDevice* spVideoDevice;
	CAST(sample->device, spVideoDevice);
	ExitIfFailed(CALL(CheckFeatureSupport, spVideoDevice, D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &dx12ProcCaps, sizeof(dx12ProcCaps)));
	if ((dx12ProcCaps.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) == 0) {
		vaExitIfFailed(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT, "VAEntrypointVideoProc not supported for conversion DXGI_FORMAT_NV12 to DXGI_FORMAT_R8G8B8A8_UNORM.");
	}
	RELEASE(spVideoDevice);
	HeapFree(GetProcessHeap(), 0, entrypoints);
}

/* Create the context for color rgb to yuv conversion (sample->vaColorConvCtx) */
void InitVAProcContext(DXSample* const sample)
{
	VAStatus va_status = vaCreateConfig(
		sample->vaDisplay,
		VAProfileNone,
		VAEntrypointVideoProc,
		NULL,
		0,
		&sample->VAProcConfigId);
	vaExitIfFailed(va_status, "vaCreateConfig");

	va_status = vaCreateContext(
		sample->vaDisplay,
		sample->VAProcConfigId,
		sample->width,
		sample->height,
		VA_PROGRESSIVE,
		sample->VARenderTargets,
		FrameCount,
		&sample->vaColorConvCtx);
	vaExitIfFailed(va_status, "vaCreateContext");

	va_status = vaCreateBuffer(
		sample->vaDisplay,
		sample->vaColorConvCtx,
		VAProcPipelineParameterBufferType,
		sizeof(VAProcPipelineParameterBuffer),
		1,
		NULL,
		&sample->vaColorConvBuf);
	vaExitIfFailed(va_status, "vaCreateBuffer");
}

void PerformVABlit(
	DXSample* const sample,
	VAContextID context,
	VABufferID buffer,
	VASurfaceID* pInSurfaces,
	UINT inSurfacesCount,
	VARectangle* pSrcRegions,
	VARectangle* pDstRegions,
	VASurfaceID dstSurface,
	float alpha)
{
	assert(inSurfacesCount == 1);

	VAStatus va_status;
	va_status = vaBeginPicture(sample->vaDisplay, context, dstSurface);
	vaExitIfFailed(va_status, "vaBeginPicture");

	for (size_t i = 0; i < inSurfacesCount; i++)
	{
		VAProcPipelineParameterBuffer* pipeline_param;
		va_status = vaMapBuffer(sample->vaDisplay, buffer, (void**)&pipeline_param);
		memset(pipeline_param, 0, sizeof(VAProcPipelineParameterBuffer));
		vaExitIfFailed(va_status, "vaMapBuffer");
		pipeline_param->surface = pInSurfaces[i];
		if (pSrcRegions)
			pipeline_param->surface_region = &pSrcRegions[i];
		if (pDstRegions)
			pipeline_param->output_region = &pDstRegions[i];

		// Check the VA platform can perform global alpha
		// blend using the queried capabilities previously
		VABlendState blend;
		if (sample->ProcPipelineCaps.blend_flags & VA_BLEND_GLOBAL_ALPHA)
		{
			memset(&blend, 0, sizeof(VABlendState));
			blend.flags = VA_BLEND_GLOBAL_ALPHA;
			blend.global_alpha = alpha;
			pipeline_param->blend_state = &blend;
		}

		va_status = vaUnmapBuffer(sample->vaDisplay, buffer);
		vaExitIfFailed(va_status, "vaUnMapBuffer");

		// Apply VPBlit
		vaRenderPicture(sample->vaDisplay, context, &buffer, 1);
	}
	va_status = vaEndPicture(sample->vaDisplay, context);
	vaExitIfFailed(va_status, "vaEndPicture");

	// Wait for completion on GPU for the indicated VASurface
	va_status = vaSyncSurface(sample->vaDisplay, dstSurface);
	vaExitIfFailed(va_status, "vaSyncSurface");
}

void DestroyVAProc(DXSample* const sample)
{
	VAStatus va_status = vaDestroyConfig(sample->vaDisplay, sample->VAProcConfigId);
	vaExitIfFailed(va_status, "vaDestroyConfig");
	va_status = vaDestroyContext(sample->vaDisplay, sample->vaColorConvCtx);
	vaExitIfFailed(va_status, "vaDestroyContext");
	va_status = vaDestroyBuffer(sample->vaDisplay, sample->vaColorConvBuf);
	vaExitIfFailed(va_status, "vaDestroyBuffer");
}

/* Check for decode capability for VAProfileH264Main */
void EnsureVADecSupport(DXSample* const sample) {
	int num_entrypoints = vaMaxNumEntrypoints(sample->vaDisplay);
	VAEntrypoint* entrypoints = HeapAlloc(GetProcessHeap(), 0, num_entrypoints * sizeof(VAEntrypoint));
	VAStatus va_status = vaQueryConfigEntrypoints(
		sample->vaDisplay,
		VAProfileH264Main,
		entrypoints,
		&num_entrypoints);
	vaExitIfFailed(va_status, "vaQueryConfigEntrypoints for VAProfileH264Main");

	bool supportsH264Dec = false;
	for (int32_t i = 0; !supportsH264Dec && i < num_entrypoints; i++) {
		// VAEntrypointVLD means that your card is capable to decode the format queried in vaQueryConfigEntrypoints
		// See that we queried for VAProfileH264Main above
		if (entrypoints[i] == VAEntrypointVLD)
			supportsH264Dec = true;
	}

	if (!supportsH264Dec) {
		vaExitIfFailed(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT, "VAEntrypointVLD not supported for VAProfileH264Main.");
	}
	HeapFree(GetProcessHeap(), 0, entrypoints);
}

/* Create the decode context (sample->VADecContextId) */
void InitVADecContext(DXSample* const sample)
{
	VAStatus va_status = vaCreateConfig(
		sample->vaDisplay,
		VAProfileH264Main,
		VAEntrypointVLD,
		NULL,
		0,
		&sample->VADecConfigId);
	vaExitIfFailed(va_status, "vaCreateConfig");

	va_status = vaCreateContext(
		sample->vaDisplay,
		sample->VADecConfigId,
		sample->width,
		sample->height,
		VA_PROGRESSIVE,
		sample->VARenderTargets,
		FrameCount,
		&sample->VADecContextId);
	vaExitIfFailed(va_status, "vaCreateContext");

	/* The context will receive the data below, which we will be able to 
	   identify with their buffer id, which is stored in the sample->VADecPipelineBufferId 
	*/

	va_status = vaCreateBuffer(
		sample->vaDisplay,
		sample->VADecContextId,
		VAPictureParameterBufferType,
		sizeof(VAPictureParameterBufferH264),
		1,
		(void*)g_PicParams_AVC,
		&sample->VADecPipelineBufferId[VA_H264DEC_BUFFER_INDEX_PIC]);
	vaExitIfFailed(va_status, "vaCreateBuffer");

	va_status = vaCreateBuffer(
		sample->vaDisplay,
		sample->VADecContextId,
		VAIQMatrixBufferType,
		sizeof(VAIQMatrixBufferH264),
		1,
		(void*)g_Qmatrix_AVC,
		&sample->VADecPipelineBufferId[VA_H264DEC_BUFFER_INDEX_QMATRIX]);
	vaExitIfFailed(va_status, "vaCreateBuffer");

	va_status = vaCreateBuffer(
		sample->vaDisplay,
		sample->VADecContextId,
		VASliceDataBufferType,
		sizeof(g_Bitstream_AVC),
		1,
		(void*)g_Bitstream_AVC,
		&sample->VADecPipelineBufferId[VA_H264DEC_BUFFER_INDEX_COMPRESSED_BIT]);
	vaExitIfFailed(va_status, "vaCreateBuffer");

	va_status = vaCreateBuffer(
		sample->vaDisplay,
		sample->VADecContextId,
		VASliceParameterBufferType,
		sizeof(VASliceParameterBufferH264),
		1,
		(void*)g_SlcParams_AVC,
		&sample->VADecPipelineBufferId[VA_H264DEC_BUFFER_INDEX_SLICE]);
	vaExitIfFailed(va_status, "vaCreateBuffer");
}

/* Decode the frame into the sample->VASurfaceNV12 */
void PerformVADecodeFrame(DXSample* const sample) {
	VAStatus va_status;
	va_status = vaBeginPicture(sample->vaDisplay, sample->VADecContextId, sample->VASurfaceNV12);
	vaExitIfFailed(va_status, "vaBeginPicture");

	vaRenderPicture(sample->vaDisplay, sample->VADecContextId, sample->VADecPipelineBufferId, _countof(sample->VADecPipelineBufferId));

	va_status = vaEndPicture(sample->vaDisplay, sample->VADecContextId);
	vaExitIfFailed(va_status, "vaEndPicture");

	// Wait for completion on GPU for the indicated VASurface
	va_status = vaSyncSurface(sample->vaDisplay, sample->VASurfaceNV12);
	vaExitIfFailed(va_status, "vaSyncSurface");
}

void DestroyVADec(DXSample* const sample) {
	VAStatus va_status = vaDestroyConfig(sample->vaDisplay, sample->VADecConfigId);
	vaExitIfFailed(va_status, "vaDestroyConfig");

	va_status = vaDestroyContext(sample->vaDisplay, sample->VADecContextId);
	vaExitIfFailed(va_status, "vaDestroyContext");

	for (UINT i = 0; i < _countof(sample->VADecPipelineBufferId); i++) {
		vaDestroyBuffer(sample->vaDisplay, sample->VADecPipelineBufferId[i]);
		vaExitIfFailed(va_status, "vaDestroyBuffer");
	}
}