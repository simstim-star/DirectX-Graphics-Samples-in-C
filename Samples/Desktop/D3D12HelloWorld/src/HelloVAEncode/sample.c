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

static void VALoadPipeline(DXSample* const sample);
static void VAInitDisplay(DXSample* const sample);
static void VAImportRenderTargets(DXSample* const sample);
static void VAPerformWorkload(DXSample* const sample);
static void VADestroy(DXSample* const sample);
static void VACreateSurfaces(DXSample* const sample);

/* Check features support */

static void VAEnsureVideoProcSupport(VADisplay* const vaDisplay);
static void VAEnsureH264EncSupport(VADisplay* const vaDisplay);
static void VAEnsureProcSupport(DXSample* const sample);
static void VAEnsureVideoFeaturesSupport(ID3D12VideoDevice* const spVideoDevice, D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT videoProcSupport);
static void VAEnsureEncSupport(DXSample* const sample);

static void ImportRenderTargetsToVA(DXSample* const sample);

/* Init processing and encoding contexts */

static void VAInitProcContext(DXSample* const sample);
static void VAInitEncContext(DXSample* const sample);

/* Perform VA work */

static void VAPerformEncodeFrame(DXSample* const sample, VASurfaceID dst_surface, VABufferID dst_compressedbit);
static void VAPerformBlit(
	VADisplay display,
	VAProcPipelineCaps ProcPipelineCaps,
	VAContextID context,
	VABufferID buffer,
	VASurfaceID* pInSurfaces,
	UINT inSurfacesCount,
	VARectangle* pSrcRegions,
	VARectangle* pDstRegions,
	VASurfaceID dstSurface,
	float alpha);


/*************************************************************************************
 Public functions
**************************************************************************************/

void Sample_Init(DXSample* const sample) {
	sample->viewport = (D3D12_VIEWPORT){ 0.0f, 0.0f, (float)sample->width, (float)sample->height };
	sample->scissorRect = (D3D12_RECT){ 0, 0, (LONG)sample->width, (LONG)sample->height };
	memset(&sample->va, 0, sizeof(sample->va));

	LoadPipeline(sample);
	LoadAssets(sample);
	VALoadPipeline(sample);
}

void Sample_Destroy(DXSample* const sample)
{
	WaitForPreviousFrame(sample);
	VADestroy(sample);
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
	// Currently only VARenderTargets[frameIndex] is used in the VA workload,
	// transition it back to present mode for the call below.

	VAPerformWorkload(sample);

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
	GetHardwareAdapter(factoryAsFactory1, &sample->adapter, FALSE);
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
		.BufferCount = FRAME_COUNT,
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
			.NumDescriptors = FRAME_COUNT,
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
		for (UINT nthSwapChainBuffer = 0; nthSwapChainBuffer < FRAME_COUNT; nthSwapChainBuffer++)
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
	const D3D12_RESOURCE_BARRIER transitionBarrierRT = CD3DX12_DefaultTransition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	CALL(ResourceBarrier, sample->commandList, 1, &transitionBarrierRT);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	CALL(GetCPUDescriptorHandleForHeapStart, sample->rtvHeap, &rtvHandle);
	const INT64 CurrentRtvOffset = sample->frameIndex * sample->rtvDescriptorSize;
	rtvHandle.ptr = (SIZE_T)((INT64)(rtvHandle.ptr) + CurrentRtvOffset);

	// Record commands
	CALL(ClearRenderTargetView, sample->commandList, rtvHandle, sample->va.colors[sample->va.curRegionVariation], 0, NULL);

	D3D12_RESOURCE_BARRIER transitionBarrierPresent = CD3DX12_DefaultTransition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);

	// Indicate that the back buffer will now be used to present
	CALL(ResourceBarrier, sample->commandList, 1, &transitionBarrierPresent);
	ExitIfFailed(CALL(Close, sample->commandList));
}

/*************************************************************************************
 Implementation of Video Acceleration functions
**************************************************************************************/

static void VALoadPipeline(DXSample* const sample)
{
	VAInitDisplay(sample);
	VAEnsureProcSupport(sample);
	VAEnsureEncSupport(sample);
	VACreateSurfaces(sample);
	ImportRenderTargetsToVA(sample);
	VAInitProcContext(sample);
	VAInitEncContext(sample);
}

/* Init sample->vaDisplay based on the sample->adapter */
static void VAInitDisplay(DXSample* const sample) {
	DXGI_ADAPTER_DESC desc = { 0 };
	ExitIfFailed(CALL(GetDesc, sample->adapter, &desc));
	sample->va.display = vaGetDisplayWin32(&desc.AdapterLuid);
	assert(sample->va.display);

	int major_ver, minor_ver;
	VAStatus va_status = vaInitialize(sample->va.display, &major_ver, &minor_ver);
	VAExitIfFailed(va_status, "vaInitialize");
}

static void VAEnsureProcSupport(DXSample* const sample) {
	VAEnsureH264EncSupport(sample->va.display);
	VAEnsureVideoProcSupport(sample->va.display);

	D3D12_FEATURE_DATA_VIDEO_PROCESS_MAX_INPUT_STREAMS vpMaxInputStreams = {0};
	ID3D12VideoDevice* spVideoDevice;
	CAST(sample->device, spVideoDevice);
	CALL(CheckFeatureSupport, spVideoDevice, D3D12_FEATURE_VIDEO_PROCESS_MAX_INPUT_STREAMS, &vpMaxInputStreams, sizeof(vpMaxInputStreams));
	sample->va.numVPRegions = min(vpMaxInputStreams.MaxInputStreams, 4);
	D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT videoFeatures = (D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT){
		.NodeIndex = 0,
		.InputSample = { sample->width, sample->height,{ .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 } },
		.InputFieldType = D3D12_VIDEO_FIELD_TYPE_NONE,
		.InputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE,
		.InputFrameRate = {.Numerator = 30, .Denominator = 1 },
		.OutputFormat = { .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 },
		.OutputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE,
		.OutputFrameRate = {.Numerator = 30, .Denominator = 1 }
	};

	// Check VPBlit support from format {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709} to
	// format {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709}
	// It is the same, why check support?
	VAEnsureVideoFeaturesSupport(spVideoDevice, videoFeatures);

	// Check VPBlit support for format DXGI_FORMAT_R8G8B8A8_UNORM -> DXGI_FORMAT_R8G8B8A8_NV12
	videoFeatures.OutputFormat.Format = DXGI_FORMAT_NV12;
	videoFeatures.OutputFormat.ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
	VAEnsureVideoFeaturesSupport(spVideoDevice, videoFeatures);
	RELEASE(spVideoDevice);
}

static void VAEnsureH264EncSupport(VADisplay* const vaDisplay)
{
	// an entrypoint shows funcionality your card is capable of
	int num_entrypoints = 0;
	VAEntrypoint* entrypoints = HeapAlloc(GetProcessHeap(), 0, vaMaxNumEntrypoints(vaDisplay) * sizeof(VAEntrypoint));
	VAStatus va_status = vaQueryConfigEntrypoints(vaDisplay, VAProfileH264Main, entrypoints, &num_entrypoints);
	VAExitIfFailed(va_status, "vaQueryConfigEntrypoints for VAProfileNone");

	BOOL supportsSliceLevelEncForH264 = FALSE;
	for (int32_t i = 0; i < num_entrypoints && supportsSliceLevelEncForH264 != TRUE; i++) {
		supportsSliceLevelEncForH264 |= (entrypoints[i] == VAEntrypointEncSlice);
	}

	if (!supportsSliceLevelEncForH264) {
		// Please check D3D12 Video Encode supported platforms: https://devblogs.microsoft.com/directx/announcing-new-directx-12-feature-video-encoding/#video-encode-api-supported-platforms
		VAExitIfFailed(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT, "VAEntrypointVideoProc not supported.");
	}
	HeapFree(GetProcessHeap(), 0, entrypoints);
}

static void VAEnsureVideoProcSupport(VADisplay* const vaDisplay) {
	int num_entrypoints = vaMaxNumEntrypoints(vaDisplay);
	VAEntrypoint* entrypoints = HeapAlloc(GetProcessHeap(), 0, num_entrypoints * sizeof(VAEntrypoint));
	VAStatus va_status = vaQueryConfigEntrypoints(vaDisplay, VAProfileNone, entrypoints, &num_entrypoints);
	VAExitIfFailed(va_status, "vaQueryConfigEntrypoints for VAProfileNone");

	BOOL supportsVideoProcessing = FALSE;
	for (int32_t i = 0; !supportsVideoProcessing && i < num_entrypoints; i++) {
		supportsVideoProcessing |= (entrypoints[i] == VAEntrypointVideoProc);
	}

	if (!supportsVideoProcessing) {
		VAExitIfFailed(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT, "VAEntrypointVideoProc not supported.");
	}
	HeapFree(GetProcessHeap(), 0, entrypoints);
}

static void VAEnsureVideoFeaturesSupport(ID3D12VideoDevice* const spVideoDevice, D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT features)
{
	ExitIfFailed(CALL(CheckFeatureSupport, spVideoDevice, D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &features, sizeof(features)));
	if ((features.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) == 0) {
		VAExitIfFailed(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT, "Video features not supported.");
	}
}

/* Check for encode capability for VAProfileH264Main */
static void VAEnsureEncSupport(DXSample* const sample) {
	int num_entrypoints = vaMaxNumEntrypoints(sample->va.display);
	VAEntrypoint* const entrypoints = HeapAlloc(GetProcessHeap(), 0, num_entrypoints * sizeof(VAEntrypoint));
	VAStatus va_status = vaQueryConfigEntrypoints(sample->va.display, VAProfileH264Main, entrypoints, &num_entrypoints);
	VAExitIfFailed(va_status, "vaQueryConfigEntrypoints for VAProfileH264Main");

	BOOL supportsH264 = FALSE;
	for (int32_t i = 0; i < num_entrypoints && !supportsH264; i++) {
		// VAEntrypointVLD means that your card is capable to encode the format queried in vaQueryConfigEntrypoints
		// See that we queried for VAProfileH264Main above
		supportsH264 |= (entrypoints[i] == VAEntrypointVLD);
	}

	if (!supportsH264) {
		VAExitIfFailed(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT, "VAEntrypointVLD not supported for VAProfileH264Main.");
	}
	HeapFree(GetProcessHeap(), 0, entrypoints);
}

/* Creates the sample->VASurfaceNV12, a NV12 surface to receive the image data */
static void VACreateSurfaces(DXSample* const sample)
{
	VASurfaceAttrib createSurfacesAttribList[2] = {
		// Describes the pixel format of the surface
		{
			.type = VASurfaceAttribPixelFormat,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type = VAGenericValueTypeInteger,
				/* FourCC ("four-character code") is a sequence of four bytes used to uniquely identify data formats */
				.value = VA_FOURCC_RGBA, // 4 bytes per pixel, 1 byte per color
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

	// create #VA_NUM_RGBA_SURFACES surfaces with the attributes defined above
	VAStatus va_status = vaCreateSurfaces(
		sample->va.display,
		VA_RT_FORMAT_RGB32, // 4 bytes per pixel, 1 byte per color
		sample->width,
		sample->height,
		sample->va.surfacesRGBA,
		VA_NUM_RGBA_SURFACES,
		createSurfacesAttribList,
		_countof(createSurfacesAttribList)
	);
	VAExitIfFailed(va_status, "vaCreateSurfaces");

	createSurfacesAttribList[0].value.value.i = VA_FOURCC_NV12; // YUV 4:2:0 8-bit
	va_status = vaCreateSurfaces(
		sample->va.display,
		VA_RT_FORMAT_YUV420, // YUV 4:2:0 8-bit
		sample->width,
		sample->height,
		&sample->va.surfaceNV12,
		1,
		createSurfacesAttribList,
		_countof(createSurfacesAttribList));
	VAExitIfFailed(va_status, "vaCreateSurfaces");
}

/* Creates VASurfaces on the render targets, so that we can render there with VA */
static void VAImportRenderTargets(DXSample* const sample) {
	// Handles to render targets that we will associate with corresponding
	// output surface in the call to vaCreateSurfaces
	HANDLE renderTargetsHandles[FRAME_COUNT];

	// Create a handle to the render target for each frame
	for (size_t i = 0; i < FRAME_COUNT; i++)
	{
		ID3D12DeviceChild* renderTargetResourceAsDeviceChild = NULL;
		ExitIfFailed(CAST(sample->renderTargets[i], renderTargetResourceAsDeviceChild));
		// Take a shared handle to a render target. The render target will be shared between the 
		// sample->device and ???
		ExitIfFailed(CALL(CreateSharedHandle, 
			sample->device,
			renderTargetResourceAsDeviceChild,
			NULL,
			GENERIC_ALL,
			NULL,
			&renderTargetsHandles[i]
		));
		RELEASE(renderTargetResourceAsDeviceChild);
	}

	VASurfaceAttrib createSurfacesAttribList[3] = {
		// [0]
		{
			.type = VASurfaceAttribPixelFormat,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type = VAGenericValueTypeInteger,
				// Based on the default render target
				// format DXGI_FORMAT_R8G8B8A8_UNORM
				.value.i = VA_FOURCC_RGBA,
			},
		},
		// [1]
		{
			.type = VASurfaceAttribMemoryType,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type =  VAGenericValueTypeInteger,
				// Using NTHandles for interop is the safe way of sharing resources between the 
				// VADisplay D3D12 device and the app/sample D3D12 independent devices
				.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE,
			},
		},
		// [2]
		// handles to the render targets.
		// this defines the surfaces to be created in the render targets.
		{
			.type = VASurfaceAttribExternalBufferDescriptor,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value = {
				.type = VAGenericValueTypePointer,
				.value.p = renderTargetsHandles,
			},
		},
	};
	
	// Creates VASurface objects by importing handles of existing D3D12 resources 
	// (ie. create va surfaces in the render targets)
	VAStatus va_status = vaCreateSurfaces(
		sample->va.display,
		VA_RT_FORMAT_RGB32,
		sample->width,
		sample->height,
		sample->va.renderTargets,
		FRAME_COUNT,
		createSurfacesAttribList,
		_countof(createSurfacesAttribList)
	);
	VAExitIfFailed(va_status, "vaCreateSurfaces");
}

/* Create the context for color rgb to yuv conversion (sample->vaColorConvCtx) */
static void VAInitProcContext(DXSample* const sample)
{
	VAStatus va_status = vaCreateConfig(
		sample->va.display,
		VAProfileNone,
		VAEntrypointVideoProc,
		NULL,
		0,
		&sample->va.procConfigId
	);

	va_status = vaCreateContext(
		sample->va.display,
		sample->va.procConfigId,
		sample->width,
		sample->height,
		VA_PROGRESSIVE,
		sample->va.renderTargets, // hint
		FRAME_COUNT,
		&sample->va.colorConvCtx
	);
	VAExitIfFailed(va_status, "vaCreateContext");

	va_status = vaCreateBuffer(
		sample->va.display,
		sample->va.colorConvCtx,
		VAProcPipelineParameterBufferType,
		sizeof(VAProcPipelineParameterBuffer),
		1,
		NULL,
		&sample->va.colorConvBuf
	);
	VAExitIfFailed(va_status, "vaCreateBuffer");

	// Context for single RGB -> RGB copy
	{
		va_status = vaCreateContext(
			sample->va.display,
			sample->va.procConfigId,
			sample->width,
			sample->height,
			VA_PROGRESSIVE,
			sample->va.renderTargets,
			FRAME_COUNT,
			&sample->va.copyCtx);
		VAExitIfFailed(va_status, "vaCreateContext");

		va_status = vaCreateBuffer(
			sample->va.display,
			sample->va.copyCtx,
			VAProcPipelineParameterBufferType,
			sizeof(VAProcPipelineParameterBuffer),
			1,
			NULL,
			&sample->va.copyBuf);
		VAExitIfFailed(va_status, "vaCreateBuffer");
	}

	// Context for multiple RGB -> RGB blend
	{
		va_status = vaCreateContext(
			sample->va.display,
			sample->va.procConfigId,
			sample->width,
			sample->height,
			VA_PROGRESSIVE,
			sample->va.renderTargets,
			FRAME_COUNT,
			&sample->va.blendCtx);
		VAExitIfFailed(va_status, "vaCreateContext");

		va_status = vaCreateBuffer(
			sample->va.display,
			sample->va.blendCtx,
			VAProcPipelineParameterBufferType,
			sizeof(VAProcPipelineParameterBuffer),
			1,
			NULL,
			&sample->va.blendBuf);
		VAExitIfFailed(va_status, "vaCreateBuffer");

		va_status = vaQueryVideoProcPipelineCaps(
			sample->va.display,
			sample->va.blendCtx,
			NULL,
			0,
			&sample->va.procPipelineCaps);
		VAExitIfFailed(va_status, "vaQueryVideoProcPipelineCaps");

		INT XIncrement = sample->width / sample->va.numVPRegions;
		INT YIncrement = sample->height / sample->va.numVPRegions;
		INT XShift = 6;
		INT YShift = 6;
		for (INT i = 0; i < REGIONS_VARIATIONS; i++)
		{
			sample->va.blendRegions[i][sample->va.numVPRegions - 1].x = max(0, XIncrement - i);
			sample->va.blendRegions[i][sample->va.numVPRegions - 1].y = max(0, YIncrement - i);
			sample->va.blendRegions[i][sample->va.numVPRegions - 1].width = sample->width / 2;
			sample->va.blendRegions[i][sample->va.numVPRegions - 1].height = sample->height / 2;

			for (INT j = 0; j < sample->va.numVPRegions - 1; j++)
			{
				sample->va.blendRegions[i][j].x = min(sample->width, j * XIncrement + i * 0.25f * j * XShift);
				sample->va.blendRegions[i][j].y = min(sample->height, j * YIncrement + i * 0.5f * YShift);
				sample->va.blendRegions[i][j].width = REGIONS_SIZE_RATIO * XIncrement;
				sample->va.blendRegions[i][j].height = REGIONS_SIZE_RATIO * YIncrement;
			}

			sample->va.colors[i][0] = 0.0f;
			sample->va.colors[i][1] = 0.2f;
			sample->va.colors[i][2] = 0.1f + min(0.4f, i * 0.0125);
			sample->va.colors[i][3] = 1.0f;
		}
	}
}

/*******************************************************************************************
   Create the encode context (sample->VAEncContextId) and fill the buffers (identified by 
   the ids in sample->VAEncPipelineBufferId[4]) that will be used as data when rendering 
   data to the sample->VASurfaceNV12
********************************************************************************************/
static void VAInitEncContext(DXSample* const sample)
{
	sample->va.writer = fopen("output_bitstream.h264", "wb");

	VAStatus va_status = vaCreateConfig(
		sample->va.display,
		VAProfileH264Main,
		VAEntrypointEncSlice,
		NULL,
		0,
		&sample->va.encConfigId
	);
	VAExitIfFailed(va_status, "vaCreateConfig");

	va_status = vaCreateContext(
		sample->va.display,
		sample->va.encConfigId,
		sample->width,
		sample->height,
		VA_PROGRESSIVE,
		sample->va.renderTargets,
		FRAME_COUNT,
		&sample->va.encContextId
	);
	VAExitIfFailed(va_status, "vaCreateContext");

	va_status = vaCreateBuffer(
		sample->va.display,
		sample->va.encContextId,
		VAEncSequenceParameterBufferType,
		sizeof(VAEncSequenceParameterBufferH264),
		1,
		NULL,
		&sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_SEQ]
	);
	VAExitIfFailed(va_status, "vaCreateBuffer");

	va_status = vaCreateBuffer(
		sample->va.display,
		sample->va.encContextId,
		VAEncPictureParameterBufferType,
		sizeof(VAEncPictureParameterBufferH264),
		1,
		NULL,
		&sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_PIC]
	);
	VAExitIfFailed(va_status, "vaCreateBuffer");

	va_status = vaCreateBuffer(
		sample->va.display,
		sample->va.encContextId,
		VAEncSliceParameterBufferType,
		sizeof(VAEncSliceParameterBufferH264),
		1,
		NULL,
		&sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_SLICE]
	);
	VAExitIfFailed(va_status, "vaCreateBuffer");

	va_status = vaCreateBuffer(
		sample->va.display,
		sample->va.encContextId,
		VAEncCodedBufferType,
		// Worst case within reason assume same as uncompressed surface
		sample->width * sample->height * 3,
		1,
		NULL,
		&sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_COMPRESSED_BIT]
	);
	VAExitIfFailed(va_status, "vaCreateBuffer");
}

static void ImportRenderTargetsToVA(DXSample* const sample)
{
	VASurfaceAttrib createSurfacesAttribList[3] = {
		{
			VASurfaceAttribPixelFormat,
			VA_SURFACE_ATTRIB_SETTABLE,
			{
				VAGenericValueTypeInteger,
				// Based on the default render target
				// format DXGI_FORMAT_R8G8B8A8_UNORM
				VA_FOURCC_RGBA,
			},
		},
		{
			VASurfaceAttribMemoryType,
			VA_SURFACE_ATTRIB_SETTABLE,
			{
				VAGenericValueTypeInteger,
				// Using NTHandles for interop is the safe way of sharing resources between the 
				// VADisplay D3D12 device and the app/sample D3D12 independent devices
				VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE,
			},
		},
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

	// The value here is an array of num_surfaces pointers to HANDLE so
	// each handle can be associated with the corresponding output surface
	// in the call to vaCreateSurfaces
	HANDLE renderTargets[FRAME_COUNT];
	for (size_t i = 0; i < FRAME_COUNT; i++)
	{
		ID3D12DeviceChild* asDeviceChild = NULL;
		CAST(sample->renderTargets[i], asDeviceChild);
		ExitIfFailed(CALL(CreateSharedHandle, sample->device, asDeviceChild,
			NULL, GENERIC_ALL, NULL, &renderTargets[i]));
		RELEASE(asDeviceChild);
	}
	createSurfacesAttribList[2].value.value.p = renderTargets;

	VAStatus va_status = vaCreateSurfaces(
		sample->va.display,
		VA_RT_FORMAT_RGB32,
		sample->width,
		sample->height,
		sample->va.renderTargets,
		FRAME_COUNT,
		createSurfacesAttribList,
		_countof(createSurfacesAttribList));
	VAExitIfFailed(va_status, "vaCreateSurfaces");
}


/* From H264 data to an NV12 surface.
*  From an NV12 surface to the RGBA render target
*/
static void VAPerformWorkload(DXSample* const sample)
{
	// Copy the cleared render target with solid color into RGBA surfaces
	// This needs to be the first one, because the render target is still cleared
	for (UINT i = 0; i < sample->va.numVPRegions; i++) {
		VAPerformBlit(sample->va.display, 
			sample->va.procPipelineCaps, 
			sample->va.copyCtx, 
			sample->va.copyBuf,
			&sample->va.renderTargets[sample->frameIndex], // src surface
			1,											   // count src surfaces
			NULL,										   // src region rectangle
			NULL,                                          // dst region rectangle
			sample->va.surfacesRGBA[i],					   // dst surface
			1.0f										   // alpha
		);
	}


	// Blit the source surface sample->numVPRegions times in different regions in the output surface
	// Blend, translate and scale src_regions into dst_regions of the render target
	VAPerformBlit(sample->va.display, 
		sample->va.procPipelineCaps, 
		sample->va.blendCtx,                      
		sample->va.blendBuf,
		sample->va.surfacesRGBA,                                 // src surface         
		sample->va.numVPRegions,                                 // count src surface
		sample->va.blendRegions[sample->va.curRegionVariation],  // src region rectangle
		sample->va.blendRegions[sample->va.curRegionVariation],  // dst region rectangle
		sample->va.renderTargets[sample->frameIndex],            // dst surface
		ALPHA_BLEND                                              // alpha
	);

	sample->va.curRegionVariation = ((sample->va.curRegionVariation + 1) % REGIONS_VARIATIONS);

	// Color convert RGB into NV12 for encode
	VAPerformBlit(sample->va.display, 
		sample->va.procPipelineCaps, 
		sample->va.colorConvCtx, 
		sample->va.colorConvBuf, 
		&sample->va.renderTargets[sample->frameIndex],           // src surface
		1,														 // count src surface
		NULL,													 // src region rectangle
		NULL,													 // dst region rectangle
		sample->va.surfaceNV12,									 // dst surface
		1.0f													 // alpha
	);

	// Encode render target frame into an H.264 bitstream
	VAPerformEncodeFrame(sample, sample->va.surfaceNV12, sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_COMPRESSED_BIT]);
}

/* Encode the frame into the sample->VASurfaceNV12 */
static void VAPerformEncodeFrame(DXSample* const sample, VASurfaceID dst_surface, VABufferID dst_compressedbit)
{
	VAStatus va_status = vaBeginPicture(sample->va.display, sample->va.encContextId, dst_surface);
	VAExitIfFailed(va_status, "vaBeginPicture");

	// VAEncSequenceParameterBufferH264
	{
		VAEncSequenceParameterBufferH264* pMappedBuf;
		va_status = vaMapBuffer(sample->va.display, sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_SEQ], (void**)&pMappedBuf);
		VAExitIfFailed(va_status, "vaMapBuffer");
		memset(pMappedBuf, 0, sizeof(*pMappedBuf));

		// Level 4.1 as per H.264 codec standard
		pMappedBuf->level_idc = 41;

		// 2 * fps_num for 30fps
		pMappedBuf->time_scale = 2 * 30;
		// fps_den
		pMappedBuf->num_units_in_tick = 1;

		pMappedBuf->intra_idr_period = 1;
		pMappedBuf->seq_fields.bits.pic_order_cnt_type = 2;

		va_status = vaUnmapBuffer(sample->va.display, sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_SEQ]);
		VAExitIfFailed(va_status, "vaUnMapBuffer");
	}

	// VAEncPictureParameterBufferH264
	{
		VAEncPictureParameterBufferH264* pMappedBuf;
		va_status = vaMapBuffer(sample->va.display, sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_PIC], (void**)&pMappedBuf);
		VAExitIfFailed(va_status, "vaMapBuffer");
		memset(pMappedBuf, 0, sizeof(*pMappedBuf));

		pMappedBuf->pic_fields.bits.idr_pic_flag = 1;
		// We can use always 0 as each frame is an IDR which resets the GOP
		pMappedBuf->CurrPic.TopFieldOrderCnt = 0;
		pMappedBuf->CurrPic.picture_id = dst_surface;
		pMappedBuf->coded_buf = dst_compressedbit;

		va_status = vaUnmapBuffer(sample->va.display, sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_PIC]);
		VAExitIfFailed(va_status, "vaUnMapBuffer");
	}

	// VAEncSliceParameterBufferH264
	{
		VAEncSliceParameterBufferH264* pMappedBuf;
		va_status = vaMapBuffer(sample->va.display, sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_SLICE], (void**)&pMappedBuf);
		VAExitIfFailed(va_status, "vaMapBuffer");
		memset(pMappedBuf, 0, sizeof(*pMappedBuf));

		pMappedBuf->num_macroblocks = (sample->width / H264_MB_PIXEL_SIZE * sample->height / H264_MB_PIXEL_SIZE);
		pMappedBuf->slice_type = 2; // intra slice
		va_status = vaUnmapBuffer(sample->va.display, sample->va.encPipelineBufferId[VA_H264ENC_BUFFER_INDEX_SLICE]);
		VAExitIfFailed(va_status, "vaUnMapBuffer");
	}

	// Apply encode, send the first 3 seq, pic, slice buffers
	vaRenderPicture(sample->va.display, sample->va.encContextId, sample->va.encPipelineBufferId, 3);

	va_status = vaEndPicture(sample->va.display, sample->va.encContextId);
	VAExitIfFailed(va_status, "vaEndPicture");

	// Wait for completion on GPU for the indicated VABuffer/VASurface
	// Attempt vaSyncBuffer if VA driver implements it first
	va_status = vaSyncBuffer(sample->va.display, dst_compressedbit, VA_TIMEOUT_INFINITE);
	if (va_status != VA_STATUS_ERROR_UNIMPLEMENTED)
	{
		VAExitIfFailed(va_status, "vaSyncBuffer");
	}
	else
	{
		// Legacy API call otherwise
		va_status = vaSyncSurface(sample->va.display, dst_surface);
		VAExitIfFailed(va_status, "vaSyncSurface");
	}

	// Flush encoded bitstream to disk
	{
		VACodedBufferSegment* buf_list, * buf;
		va_status = vaMapBuffer(sample->va.display, dst_compressedbit, (void**)&buf_list);
		VAExitIfFailed(va_status, "vaMapBuffer");
		for (buf = buf_list; buf; buf = (VACodedBufferSegment*)buf->next) {
			fwrite((char*)buf->buf, 1, buf->size, sample->va.writer);
		}

		va_status = vaUnmapBuffer(sample->va.display, dst_compressedbit);
		VAExitIfFailed(va_status, "vaUnMapBuffer");
	}
}


/*
* Display the buffer content on the dstSurface
*/
static void VAPerformBlit(
	VADisplay display,
	VAProcPipelineCaps ProcPipelineCaps,
	VAContextID context,
	VABufferID buffer,
	VASurfaceID* pInSurfaces,
	UINT inSurfacesCount,
	VARectangle* pSrcRegions,
	VARectangle* pDstRegions,
	VASurfaceID dstSurface,
	float alpha)
{
	assert(inSurfacesCount <= VA_NUM_RGBA_SURFACES);

	VAStatus va_status;
	va_status = vaBeginPicture(display, context, dstSurface);
	VAExitIfFailed(va_status, "vaBeginPicture");


	for (size_t i = 0; i < inSurfacesCount; i++)
	{
		VAProcPipelineParameterBuffer* pipeline_param;
		va_status = vaMapBuffer(display, buffer, (void**)&pipeline_param);
		VAExitIfFailed(va_status, "vaMapBuffer");
		memset(pipeline_param, 0, sizeof(VAProcPipelineParameterBuffer));
		pipeline_param->surface = pInSurfaces[i];
		if (pSrcRegions)
			pipeline_param->surface_region = &pSrcRegions[i];
		if (pDstRegions)
			pipeline_param->output_region = &pDstRegions[i];

		// Check if the VA platform can perform global alpha
		// blend using the queried capabilities previously
		VABlendState blend;
		if (ProcPipelineCaps.blend_flags & VA_BLEND_GLOBAL_ALPHA)
		{
			memset(&blend, 0, sizeof(VABlendState));
			blend.flags = VA_BLEND_GLOBAL_ALPHA;
			blend.global_alpha = alpha;
			pipeline_param->blend_state = &blend;
		}

		va_status = vaUnmapBuffer(display, buffer);
		VAExitIfFailed(va_status, "vaUnMapBuffer");

		// Apply VPBlit
		vaRenderPicture(display, context, &buffer, 1);
	}
	va_status = vaEndPicture(display, context);
	VAExitIfFailed(va_status, "vaEndPicture");

	// Wait for completion on GPU for the indicated VASurface
	va_status = vaSyncSurface(display, dstSurface);
	VAExitIfFailed(va_status, "vaSyncSurface");
}

/************************************************************************************
 Clean up
*************************************************************************************/

static void VADestroy(DXSample* const sample) {
	// DestroyProc
	VAStatus va_status = vaDestroyConfig(sample->va.display, sample->va.procConfigId);
	VAExitIfFailed(va_status, "vaDestroyConfig");
	va_status = vaDestroyContext(sample->va.display, sample->va.copyCtx);
	VAExitIfFailed(va_status, "vaDestroyContext");
	va_status = vaDestroyContext(sample->va.display, sample->va.blendCtx);
	VAExitIfFailed(va_status, "vaDestroyContext");
	va_status = vaDestroyContext(sample->va.display, sample->va.colorConvCtx);
	VAExitIfFailed(va_status, "vaDestroyContext");
	va_status = vaDestroyBuffer(sample->va.display, sample->va.copyBuf);
	VAExitIfFailed(va_status, "vaDestroyBuffer");
	va_status = vaDestroyBuffer(sample->va.display, sample->va.colorConvBuf);
	VAExitIfFailed(va_status, "vaDestroyBuffer");
	va_status = vaDestroyBuffer(sample->va.display, sample->va.blendBuf);
	VAExitIfFailed(va_status, "vaDestroyBuffer");

	// DestroyEnc
	va_status = vaDestroyConfig(sample->va.display, sample->va.encConfigId);
	VAExitIfFailed(va_status, "vaDestroyConfig");

	// DestroyContext
	va_status = vaDestroyContext(sample->va.display, sample->va.encContextId);
	VAExitIfFailed(va_status, "vaDestroyContext");

	// DestroyBuffers
	for (UINT i = 0; i < _countof(sample->va.encPipelineBufferId); i++) {
		vaDestroyBuffer(sample->va.display, sample->va.encPipelineBufferId[i]);
		VAExitIfFailed(va_status, "vaDestroyBuffer");
	}

	// DestroySurfaces
	va_status = vaDestroySurfaces(sample->va.display, sample->va.renderTargets, FRAME_COUNT);
	VAExitIfFailed(va_status, "vaDestroySurfaces");

    va_status = vaDestroySurfaces(sample->va.display, sample->va.surfacesRGBA, VA_NUM_RGBA_SURFACES);
	VAExitIfFailed(va_status, "vaDestroySurfaces");

    va_status = vaDestroySurfaces(sample->va.display, &sample->va.surfaceNV12, 1);
	VAExitIfFailed(va_status, "vaDestroySurfaces");

	// Terminate VA
	vaTerminate(sample->va.display);
	VAExitIfFailed(va_status, "vaTerminate");

	// Close file
	fclose(sample->va.writer);
}

static void ReleaseAll(DXSample* const sample)
{
	RELEASE(sample->swapChain);
	RELEASE(sample->device);
	RELEASE(sample->adapter);
	for (int i = 0; i < FRAME_COUNT; ++i) {
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