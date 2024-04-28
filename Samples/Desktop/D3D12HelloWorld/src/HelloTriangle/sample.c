#include <dxgi1_6.h>

#include "sample.h"
#include "sample_commons.h"
#include "macros.h"
#include "window.h"
#include "d3dcompiler.h"
#include "barrier_helpers.h"
#include "core_helpers.h"

/*************************************************************************************
 Forward declarations of private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample);
static void LoadAssets(DXSample* const sample);
static void WaitForPreviousFrame(DXSample* const sample);
static void PopulateCommandList(DXSample* const sample);
static void ReleaseAll(DXSample* const sample);

/*************************************************************************************
 Public functions
**************************************************************************************/

void Sample_Init(DXSample* const sample) {
	sample->aspectRatio = (float)(sample->width) / (float)(sample->height);
	sample->frameIndex = 0;
	sample->viewport = (D3D12_VIEWPORT){ 0.0f, 0.0f, (float)(sample->width), (float)(sample->height) };
	sample->scissorRect = (D3D12_RECT){ 0, 0, (LONG)(sample->width), (LONG)(sample->height) };
	sample->rtvDescriptorSize = 0;
	GetCurrentPath(sample->assetsPath, _countof(sample->assetsPath));

	LoadPipeline(sample);
	LoadAssets(sample);
}

void Sample_Destroy(DXSample* const sample)
{
	WaitForPreviousFrame(sample);
	CloseHandle(sample->fenceEvent);
	ReleaseAll(sample);
}

void Sample_Update(DXSample* const sample) {}

void Sample_Render(DXSample* const sample)
{
	PopulateCommandList(sample);

	ID3D12CommandList* asCommandList = NULL;
	CAST(sample->commandList, asCommandList);
	ID3D12CommandList* ppCommandLists[] = { asCommandList };
	CALL(ExecuteCommandLists, sample->commandQueue, _countof(ppCommandLists), ppCommandLists);
	RELEASE(asCommandList);

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

	D3D12_COMMAND_QUEUE_DESC queueDesc = { 0 };
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ID3D12CommandQueue* commandQueue = NULL;
	ExitIfFailed(CALL(CreateCommandQueue, sample->device, &queueDesc, IID_PPV_ARGS(&sample->commandQueue)));

	/* Create swap chain */

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = 1280;
	swapChainDesc.Height = 720;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

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
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = { 0 };
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
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

		/* Create PSO */

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { 0 };
		psoDesc.pRootSignature = sample->rootSignature;
		psoDesc.InputLayout = (D3D12_INPUT_LAYOUT_DESC){
			.pInputElementDescs = inputElementDescs,
			.NumElements = _countof(inputElementDescs)
		};
		psoDesc.VS = (D3D12_SHADER_BYTECODE){
			.pShaderBytecode = CALL(GetBufferPointer, vertexShader),
			.BytecodeLength = CALL(GetBufferSize, vertexShader),
		};
		psoDesc.PS = (D3D12_SHADER_BYTECODE){
			.pShaderBytecode = CALL(GetBufferPointer, pixelShader),
			.BytecodeLength = CALL(GetBufferSize, pixelShader),
		};
		psoDesc.RasterizerState = CD3DX12_DEFAULT_RASTERIZER_DESC();
		psoDesc.BlendState = CD3DX12_DEFAULT_BLEND_DESC();
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		ExitIfFailed(CALL(CreateGraphicsPipelineState, sample->device, &psoDesc, IID_PPV_ARGS(&sample->pipelineState)));
	}

	/* Create the command list */

	ExitIfFailed(CALL(CreateCommandList, sample->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, sample->commandAllocator, sample->pipelineState, IID_PPV_ARGS(&sample->commandList)));
	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now
	ExitIfFailed(CALL(Close, sample->commandList));

	/* Create the vertex buffer, populate it and set a view to it */
	{
		// Coordinates are in relation to the screen center, left-handed (+z to screen inside, +y up, +x right)
		const Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.25f * sample->aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }},
			{ { 0.25f, -0.25f * sample->aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.25f, -0.25f * sample->aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }},
		};
		const UINT vertexBufferSize = sizeof(triangleVertices);
		const D3D12_HEAP_PROPERTIES heapPropertyUpload = (D3D12_HEAP_PROPERTIES){
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1,
		};
		const D3D12_RESOURCE_DESC bufferResource = CD3DX12_RESOURCE_DESC_BUFFER(vertexBufferSize, D3D12_RESOURCE_FLAG_NONE, 0);

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer
		ExitIfFailed(CALL(CreateCommittedResource, sample->device,
			&heapPropertyUpload,
			D3D12_HEAP_FLAG_NONE,
			&bufferResource,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			IID_PPV_ARGS(&sample->vertexBuffer)));

		// We will open the vertexBuffer memory (that is in the GPU) for the CPU to write the triangle data 
		// in it. To do that, we use the Map() function, which enables the CPU to read from or write 
		// to the vertex buffer's memory directly
		UINT8* pVertexDataBegin = NULL; // UINT8 to represent byte-level manipulation
		// We do not intend to read from this resource on the CPU, only write
		const D3D12_RANGE readRange = (D3D12_RANGE){ .Begin = 0, .End = 0 };
		ExitIfFailed(CALL(Map, sample->vertexBuffer, 0, &readRange, (void**)(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		// While mapped, the GPU cannot access the buffer, so it's important to minimize the time 
		// the buffer is mapped.
		CALL(Unmap, sample->vertexBuffer, 0, NULL);

		// Initialize the vertex buffer view
		sample->vertexBufferView.BufferLocation = CALL(GetGPUVirtualAddress, sample->vertexBuffer);
		sample->vertexBufferView.StrideInBytes = sizeof(Vertex);
		sample->vertexBufferView.SizeInBytes = vertexBufferSize;
	}

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

	// Set necessary state
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
	CALL(OMSetRenderTargets, sample->commandList, 1, &rtvHandle, FALSE, NULL);

	// Record commands
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	CALL(ClearRenderTargetView, sample->commandList, rtvHandle, clearColor, 0, NULL);
	CALL(IASetVertexBuffers, sample->commandList, 0, 1, &sample->vertexBufferView);
	CALL(IASetPrimitiveTopology, sample->commandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	CALL(DrawInstanced, sample->commandList, 3, 1, 0, 0);

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
