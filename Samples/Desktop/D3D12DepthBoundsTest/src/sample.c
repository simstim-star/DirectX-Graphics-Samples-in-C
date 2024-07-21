#include <dxgi1_6.h>
#include <math.h>

#include "sample.h"
#include "sample_commons.h"
#include "PipelineStateSubobjects.h"
#include "window.h"
#include "d3dcompiler.h"
#include "dxheaders/d3dx12_macros.h"
#include "dxheaders/d3dx12c_barriers.h"
#include "dxheaders/d3dx12c_core.h"
#include "dxheaders/d3dx12c_root_signature.h"
#include "dxheaders/d3dx12c_resource_helpers.h"

/*************************************************************************************
 Forward declarations of private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample);
static void LoadAssets(DXSample* const sample);
static void PopulateCommandList(DXSample* const sample);
static void WaitForPreviousFrame(DXSample* const sample);
static void ReleaseAll(DXSample* const sample);

/*************************************************************************************
 Public functions
**************************************************************************************/

void Sample_Init(DXSample* const sample) {
	sample->frameIndex = 0,
	sample->frameNumber = 0,
	sample->viewport = (D3D12_VIEWPORT){ 
		.TopLeftX = 0.0f, .TopLeftY = 0.0f, 
		.Width = (float)sample->width, .Height = (float)sample->height,
		.MinDepth = D3D12_MIN_DEPTH, .MaxDepth = D3D12_MAX_DEPTH
	};
	sample->scissorRect = (D3D12_RECT){ 0, 0, (LONG)sample->width, (LONG)sample->height };
	sample->rtvDescriptorSize = 0;
	sample->aspectRatio = (float)sample->width / (float)sample->height;

	GetCurrentPath(sample->assetsPath, _countof(sample->assetsPath));
	LoadPipeline(sample);
	LoadAssets(sample);
}

void Sample_Destroy(DXSample* const sample)
{
	// Ensure that the GPU is no longer referencing resources that are about to be cleaned up
	WaitForPreviousFrame(sample);
	CloseHandle(sample->fenceEvent);
	ReleaseAll(sample);
}

void Sample_Update(DXSample* const sample) {
	sample->frameNumber++;
}

void Sample_Render(DXSample* const sample)
{
	PopulateCommandList(sample);

	ID3D12CommandList* asCommandList = NULL;
	CAST(sample->commandList, asCommandList);
	ID3D12CommandList* ppCommandLists[] = { asCommandList };
	CALL(ExecuteCommandLists, sample->commandQueue, _countof(ppCommandLists), ppCommandLists);
	RELEASE(asCommandList);

	// Present and update the frame index for the next frame.
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

	IDXGIAdapter1* hardwareAdapter = NULL;
	IDXGIFactory1* factoryAsFactory1 = NULL;
	ExitIfFailed(CAST(factory, factoryAsFactory1));
	GetHardwareAdapter(factoryAsFactory1, &hardwareAdapter, false);
	RELEASE(factoryAsFactory1);

	IUnknown* hardwareAdapterAsUnknown = NULL;
	ExitIfFailed(CAST(hardwareAdapter, hardwareAdapterAsUnknown));
	ExitIfFailed(D3D12CreateDevice(hardwareAdapterAsUnknown, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&sample->device)));
	RELEASE(hardwareAdapterAsUnknown);
	RELEASE(hardwareAdapter);

	/* Create command queue */

	D3D12_COMMAND_QUEUE_DESC queueDesc = { .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE, .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
	ID3D12CommandQueue* commandQueue = NULL;
	ExitIfFailed(CALL(CreateCommandQueue, sample->device, &queueDesc, IID_PPV_ARGS(&sample->commandQueue)));

	/* Create swap chain */

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
		.BufferCount = FrameCount,
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

	/* Create descriptor heaps */
	{
		//RTVs
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.NumDescriptors = FrameCount,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
		};
		ExitIfFailed(CALL(CreateDescriptorHeap, sample->device, &rtvHeapDesc, IID_PPV_ARGS(&sample->rtvHeap)));
		sample->rtvDescriptorSize = CALL(GetDescriptorHandleIncrementSize, sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// DSV
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {
			.NumDescriptors = 1,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
		};
		ExitIfFailed(CALL(CreateDescriptorHeap, sample->device, &dsvHeapDesc, IID_PPV_ARGS(&sample->dsvHeap)));
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
	/* Create root signature */
	{
		const D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {
			.NumParameters = 0,
			.pParameters = NULL,
			.NumStaticSamplers = 0,
			.pStaticSamplers = NULL,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		};

		// Note: Blobs don't seem to be actually RefCounted. We can call Release on them even after the count has reached zero
		// and they also don't show up when calling ReportLiveObjects. Therefore, there is no need to release them.
		ID3DBlob* signature = NULL;
		ID3DBlob* error = NULL;
		ExitIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		const LPVOID bufferPointer = CALL(GetBufferPointer, signature);
		const SIZE_T bufferSize = CALL(GetBufferSize, signature);
		ExitIfFailed(CALL(CreateRootSignature, sample->device, 0, bufferPointer, bufferSize, IID_PPV_ARGS(&sample->rootSignature)));
	}

	/* Create the pipeline state, which includes compiling and loading shaders */
	{
		ID3DBlob* vertexShader = NULL;
		ID3DBlob* pixelShader = NULL;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		const UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		const UINT compileFlags = 0;
#endif
		const wchar_t* shadersPath = wcscat(sample->assetsPath, L"shaders/shaders.hlsl");
		ExitIfFailed(D3DCompileFromFile(shadersPath, NULL, NULL, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, NULL));
		ExitIfFailed(D3DCompileFromFile(shadersPath, NULL, NULL, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, NULL));

		// Define the vertex input layout
		const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{.SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0,
			  .AlignedByteOffset = 0, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0
			},
			{.SemanticName = "COLOR", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .InputSlot = 0,
			  .AlignedByteOffset = 12, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0
			}
		};

		// To bring up depth bounds feature, DX12 introduces a new concept to create pipeline state, called PSO stream. 
		// It is required to use PSO stream to enable depth bounds test.
		//
		// PSO stream is a more flexible way to extend the design of pipeline state. In this new concept, you can think 
		// each subobject (e.g. Root Signature, Vertex Shader, or Pixel Shader) in the pipeline state as a token and the 
		// whole pipeline state is a token stream. To create a PSO stream, you describe a set of subobjects required for rendering, and 
		// then use the descriptor to create the a PSO. For any pipeline state subobject not found in the descriptor, 
		// defaults will be used. Defaults will also be used if an old version of a subobject is found in the stream. For example, 
		// an old DepthStencil State desc would not contain depth bounds test information so the depth bounds test value will  
		// default to disabled.

		// Define the pipeline state for rendering a triangle with depth bounds test enabled.
		struct RENDER_WITH_DBT_PSO_STREAM
		{
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
			CD3DX12_PIPELINE_STATE_STREAM_VS VS;
			CD3DX12_PIPELINE_STATE_STREAM_PS PS;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1 DepthStencilState; // New depth stencil subobject with depth bounds test toggle
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		} renderWithDBTPSOStream;

		// Wraps an array of render target format(s).
		struct D3D12_RT_FORMAT_ARRAY RTFormatArray = {0};
		RTFormatArray.NumRenderTargets = 1;
		RTFormatArray.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

		renderWithDBTPSOStream.RootSignature = TO_PIPELINE_STATE_STREAM_ROOT_SIGNATURE(sample->rootSignature);
		renderWithDBTPSOStream.InputLayout = TO_PIPELINE_STATE_STREAM_INPUT_LAYOUT((D3D12_INPUT_LAYOUT_DESC) { inputElementDescs, _countof(inputElementDescs) });
		renderWithDBTPSOStream.PrimitiveTopologyType = TO_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		renderWithDBTPSOStream.VS = TO_PIPELINE_STATE_STREAM_VS((D3D12_SHADER_BYTECODE) {
			.pShaderBytecode = CALL(GetBufferPointer, vertexShader),
			.BytecodeLength = CALL(GetBufferSize, vertexShader),
		});
		renderWithDBTPSOStream.PS = TO_PIPELINE_STATE_STREAM_PS((D3D12_SHADER_BYTECODE) {
			.pShaderBytecode = CALL(GetBufferPointer, pixelShader),
			.BytecodeLength = CALL(GetBufferSize, pixelShader),
		});
		renderWithDBTPSOStream.DSVFormat = TO_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT(DXGI_FORMAT_D32_FLOAT);
		renderWithDBTPSOStream.RTVFormats = TO_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS(RTFormatArray);

		// Create the descriptor of the PSO stream
		const D3D12_PIPELINE_STATE_STREAM_DESC renderWithDBTPSOStreamDesc = { sizeof(renderWithDBTPSOStream), &renderWithDBTPSOStream };
		// Check for the feature support of Depth Bounds Test
		D3D12_FEATURE_DATA_D3D12_OPTIONS2 Options = {0};
		sample->DepthBoundsTestSupported = SUCCEEDED(CALL(CheckFeatureSupport, sample->device, D3D12_FEATURE_D3D12_OPTIONS2, 
			&Options, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS2))) && Options.DepthBoundsTestSupported;

		// Create a PSO with depth bounds test enabled (or disabled, based on the result of the feature query).
		D3D12_DEPTH_STENCIL_DESC1 depthDesc = {
			.DepthEnable = FALSE,
			.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
			.DepthFunc = D3D12_COMPARISON_FUNC_LESS,
			.StencilEnable = FALSE,
			.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
			.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
			.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS },
			.BackFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS },
			.DepthBoundsTestEnable = sample->DepthBoundsTestSupported,
		};
		renderWithDBTPSOStream.DepthStencilState = TO_PIPELINE_STATE_STREAM_DEPTH_STENCIL1(depthDesc);
		ExitIfFailed(CALL(CreatePipelineState, sample->device, &renderWithDBTPSOStreamDesc, IID_PPV_ARGS(&sample->pipelineState)));

		// Create a PSO to prime depth only.
		struct DEPTH_ONLY_PSO_STREAM
		{
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
			CD3DX12_PIPELINE_STATE_STREAM_VS VS;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		} depthOnlyPSOStream;

		depthOnlyPSOStream.RootSignature = TO_PIPELINE_STATE_STREAM_ROOT_SIGNATURE(sample->rootSignature);
		depthOnlyPSOStream.InputLayout = TO_PIPELINE_STATE_STREAM_INPUT_LAYOUT((D3D12_INPUT_LAYOUT_DESC) { inputElementDescs, _countof(inputElementDescs) });
		depthOnlyPSOStream.PrimitiveTopologyType = TO_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		depthOnlyPSOStream.VS = TO_PIPELINE_STATE_STREAM_VS((D3D12_SHADER_BYTECODE) {
			.pShaderBytecode = CALL(GetBufferPointer, vertexShader),
			.BytecodeLength = CALL(GetBufferSize, vertexShader),
		});
		depthOnlyPSOStream.DSVFormat = TO_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT(DXGI_FORMAT_D32_FLOAT);
		depthOnlyPSOStream.RTVFormats = TO_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS(RTFormatArray);

		const D3D12_PIPELINE_STATE_STREAM_DESC depthOnlyPSOStreamDesc = { sizeof(depthOnlyPSOStream), &depthOnlyPSOStream };
		ExitIfFailed(CALL(CreatePipelineState, sample->device, &depthOnlyPSOStreamDesc, IID_PPV_ARGS(&sample->depthOnlyPipelineState)));
	}

	// Create the command list.
	ExitIfFailed(CALL(CreateCommandList, sample->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, sample->commandAllocator, 
		sample->pipelineState, IID_PPV_ARGS(&sample->commandList)));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	ExitIfFailed(CALL(Close, sample->commandList));

	// Create the vertex buffer.
	{
		// Define the geometry for a triangle. Note how the z coordinates are slightly different for each point,
		// with the top closer and right farther. We will dynamically change our DepthBounds gradually in the range
		// Min [0, 0.25] and Max [0.75, 1].
		Vertex triangleVertices[] =
		{
			{ { 0.00f,  0.25f * sample->aspectRatio, 0.1f },    // Top
			{ 1.0f,   0.0f,  0.0f,  1.0f } },        // Red
			{ { 0.25f, -0.25f * sample->aspectRatio, 0.9f },    // Right
			{ 0.0f,   1.0f,  0.0f,  1.0f } },        // Green
			{ { -0.25f, -0.25f * sample->aspectRatio, 0.5f },    // Left
			{ 0.0f,   0.0f,  1.0f,  1.0f } }         // Blue
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);
		const D3D12_RESOURCE_DESC bufferResource = CD3DX12_RESOURCE_DESC_BUFFER(vertexBufferSize, D3D12_RESOURCE_FLAG_NONE, 0);
		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		ExitIfFailed(CALL(CreateCommittedResource, sample->device,
			&(D3D12_HEAP_PROPERTIES){
				.Type = D3D12_HEAP_TYPE_UPLOAD,
				.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
				.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
				.CreationNodeMask = 1,
				.VisibleNodeMask = 1,
			},
			D3D12_HEAP_FLAG_NONE,
			&bufferResource,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			IID_PPV_ARGS(&sample->vertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin = NULL;
		const D3D12_RANGE readRange = (D3D12_RANGE){ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU.
		ExitIfFailed(CALL(Map, sample->vertexBuffer, 0, &readRange, (void**)(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		CALL(Unmap, sample->vertexBuffer, 0, NULL);

		// Initialize the vertex buffer view.
		sample->vertexBufferView.BufferLocation = CALL(GetGPUVirtualAddress, sample->vertexBuffer);
		sample->vertexBufferView.StrideInBytes = sizeof(Vertex);
		sample->vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Create the depth stencil view.
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {
			.Format = DXGI_FORMAT_D32_FLOAT,
			.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
			.Flags = D3D12_DSV_FLAG_NONE,
		};

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {
			.Format = DXGI_FORMAT_D32_FLOAT,
			.DepthStencil.Depth = 1.0f,
			.DepthStencil.Stencil = 0,
		};
		D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_RESOURCE_DESC texDesc = CD3DX12_TEX2D(DXGI_FORMAT_D32_FLOAT, sample->width, sample->height,
			1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_TEXTURE_LAYOUT_UNKNOWN, 0);
		ExitIfFailed(CALL(CreateCommittedResource, sample->device,
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&sample->depthStencil)
		));

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
		CALL(GetCPUDescriptorHandleForHeapStart, sample->dsvHeap, &dsvHandle);
		CALL(CreateDepthStencilView, sample->device, sample->depthStencil, &depthStencilDesc, dsvHandle);
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ExitIfFailed(CALL(CreateFence, sample->device, 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sample->fence)));
		sample->fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		sample->fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (sample->fenceEvent == NULL)
		{
			ExitIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForPreviousFrame(sample);
	}
}

void PopulateCommandList(DXSample* const sample)
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ExitIfFailed(CALL(Reset, sample->commandAllocator));

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ExitIfFailed(CALL(Reset, sample->commandList, sample->commandAllocator, sample->depthOnlyPipelineState));
	// Set necessary state.
	CALL(SetGraphicsRootSignature, sample->commandList, sample->rootSignature);
	CALL(RSSetViewports, sample->commandList, 1, &sample->viewport);
	CALL(RSSetScissorRects, sample->commandList, 1, &sample->scissorRect);

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
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
	CALL(GetCPUDescriptorHandleForHeapStart, sample->dsvHeap, &dsvHandle);
	CALL(OMSetRenderTargets, sample->commandList, 1, &rtvHandle, FALSE, &dsvHandle);

	// Record commands.
	const float clearColor[] = { 0.392f, 0.584f, 0.929f, 1.0f };
	CALL(ClearRenderTargetView, sample->commandList, rtvHandle, clearColor, 0, NULL);
	CALL(ClearDepthStencilView, sample->commandList, dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
	CALL(IASetPrimitiveTopology, sample->commandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	CALL(IASetVertexBuffers, sample->commandList, 0, 1, &sample->vertexBufferView);

	// Render only the depth stencil view of the triangle to prime the depth value of the triangle
	CALL(DrawInstanced, sample->commandList, 3, 1, 0, 0);

	// Move depth bounds so we can see they move. Depth bound test will test against DEST depth
	// that we primed previously
	const FLOAT f = 0.125f + sinf((sample->frameNumber & 0x7F) / 127.f) * 0.125f;      // [0.. 0.25]
	if (sample->DepthBoundsTestSupported)
	{
		CALL(OMSetDepthBounds, sample->commandList, 0.0f + f, 1.0f - f);
	}

	// Render the triangle with depth bounds
	CALL(SetPipelineState, sample->commandList, sample->pipelineState);
	CALL(DrawInstanced, sample->commandList, 3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER transitionBarrierPresent = CD3DX12_Transition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_NONE);

	// Indicate that the back buffer will now be used to present.
	CALL(ResourceBarrier, sample->commandList, 1, &transitionBarrierPresent);

	ExitIfFailed(CALL(Close, sample->commandList));
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

static void ReleaseAll(DXSample* const sample)
{
	RELEASE(sample->swapChain);
	RELEASE(sample->device);
	for (int i = 0; i < FrameCount; ++i) {
		RELEASE(sample->renderTargets[i]);
	}
	RELEASE(sample->depthStencil);
	RELEASE(sample->commandList);
	RELEASE(sample->commandQueue);
	RELEASE(sample->commandAllocator);
	RELEASE(sample->pipelineState);
	RELEASE(sample->depthOnlyPipelineState);
	RELEASE(sample->rootSignature);
	RELEASE(sample->rtvHeap);
	RELEASE(sample->dsvHeap);
	RELEASE(sample->vertexBuffer);
	RELEASE(sample->fence);
}