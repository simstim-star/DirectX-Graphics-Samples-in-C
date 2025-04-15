#define COBJMACROS

#include <dxgi1_6.h>
#include <stdio.h>

#include "sample.h"
#include "sample_commons.h"
#include "window.h"
#include "d3dcompiler.h"
#include "dxheaders/d3dx12_macros.h"
#include "dxheaders/d3dx12c_barriers.h"
#include "dxheaders/d3dx12c_core.h"
#include "dxheaders/d3dx12c_root_signature.h"
#include "dxheaders/d3dx12c_resource_helpers.h"
#include "occcity.h"
#include "frame_resource.h"

/*************************************************************************************
 Forward declarations of private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample);
static void LoadAssets(DXSample* const sample);
static void PopulateCommandList(DXSample* const sample);
static void CreateFrameResources(DXSample* const sample);
static void ReleaseAll(DXSample* const sample);
static void LoadShaderData(const WCHAR* const base, const WCHAR* const shaderRelativePath, UINT8** shaderData, UINT* shaderDataLen);

/*************************************************************************************
 Public functions
**************************************************************************************/

void Sample_Init(DXSample* const sample) {
	sample->aspectRatio = (float)(sample->width) / (float)(sample->height);
	sample->frameIndex = 0;
	sample->frameCounter = 0;
	sample->fenceValue = 0;
	sample->viewport = (D3D12_VIEWPORT){ 
		.TopLeftX = 0.0f, 
		.TopLeftY = 0.0f, 
		.Width = (float)(sample->width), 
		.Height = (float)(sample->height),
		.MinDepth = 0.0f,
		.MaxDepth = 1.0f
	};
	sample->scissorRect = (D3D12_RECT){ 0, 0, (LONG)(sample->width), (LONG)(sample->height) };
	sample->rtvDescriptorSize = 0;
	sample->currentFrameResourceIndex = 0;
	sample->currFrameResource = NULL;
	GetCurrentPath(sample->assetsPath, _countof(sample->assetsPath));

	StepTimer_Init(&sample->timer);
	sample->camera = SimpleCamera_Spawn((XMFLOAT3) { 8, 8, 30 });

	LoadPipeline(sample);
	LoadAssets(sample);
}

void Sample_Destroy(DXSample* const sample)
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	{
		const UINT64 fenceValue = sample->fenceValue;
		const UINT64 lastCompletedFence = CALL(GetCompletedValue, sample->fence);
		// Signal and increment the fence value.
		LogAndExit(CALL(Signal, sample->commandQueue, sample->fence, sample->fenceValue));
		// Wait until the previous frame is finished.
		if (lastCompletedFence < fenceValue)
		{
			LogAndExit(CALL(SetEventOnCompletion, sample->fence, fenceValue, sample->fenceEvent));
			WaitForSingleObject(sample->fenceEvent, INFINITE);
		}
	}

	for (UINT i = 0; i < FrameCount; i++)
	{
		FrameResource_Clean(sample->frameResources[i]);
		HeapFree(GetProcessHeap(), 0, sample->frameResources[i]);
	}

	ReleaseAll(sample);
}

void Sample_Update(DXSample* const sample) {
	Tick(&sample->timer);

	if (sample->frameCounter == 50)
	{
		// Update window text with FPS value.
		wchar_t fps[64];
		swprintf_s(fps, 64, L"%ufps", sample->timer.framesPerSecond);
		SetWindowTextW(G_HWND, fps);
		sample->frameCounter = 0;
	}

	sample->frameCounter++;

	// Get current GPU progress against submitted workload. Resources still scheduled 
	// for GPU execution cannot be modified or else undefined behavior will result.
	const UINT64 lastCompletedFence = CALL(GetCompletedValue, sample->fence);

	// Move to the next frame resource.
	sample->currentFrameResourceIndex = (sample->currentFrameResourceIndex + 1) % FrameCount;
	sample->currFrameResource = sample->frameResources[sample->currentFrameResourceIndex];

	// Make sure that this frame resource isn't still in use by the GPU.
	// If it is, wait for it to complete.
	if (sample->currFrameResource->fenceValue != 0 && sample->currFrameResource->fenceValue > lastCompletedFence)
	{
		LogAndExit(CALL(SetEventOnCompletion, sample->fence, sample->currFrameResource->fenceValue, sample->fenceEvent));
		WaitForSingleObject(sample->fenceEvent, INFINITE);
	}

	// If the work on this frame is done, we can update the camera and the frame resources
	SimpleCamera_Update(&sample->camera, TicksToSeconds(sample->timer.elapsedTicks));
	XMMATRIX viewMatrix = SimpleCamera_GetViewMatrix(sample->camera.position, sample->camera.lookDirection, sample->camera.upDirection);
	XMMATRIX projMatrix = SimpleCamera_GetProjectionMatrix(0.8f, sample->aspectRatio, 1.0f, 1000.0f);
	FrameResource_UpdateConstantBuffers(sample->currFrameResource, &viewMatrix, &projMatrix);
}

void Sample_Render(DXSample* const sample)
{
	PopulateCommandList(sample);

	ID3D12CommandList* asCommandList = NULL;
	CAST(sample->commandList, asCommandList);
	ID3D12CommandList* ppCommandLists[] = { asCommandList };
	ID3D12CommandQueue_ExecuteCommandLists(sample->commandQueue, _countof(ppCommandLists), ppCommandLists);
	RELEASE(asCommandList);

	// Present and update the frame index for the next frame.
	HRESULT hr = IDXGISwapChain3_Present(sample->swapChain, 1, 0);
	if (FAILED(hr)) LogAndExit(hr);
	sample->frameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(sample->swapChain);

	// Signal and increment the fence value.
	sample->currFrameResource->fenceValue = sample->fenceValue;
	hr = ID3D12CommandQueue_Signal(sample->commandQueue, sample->fence, sample->fenceValue);
	if (FAILED(hr)) LogAndExit(hr);
	sample->fenceValue++;
}

void Sample_OnKeyDown(DXSample* const sample, UINT8 key) {
	SimpleCamera_OnKeyDown(&sample->camera, key);
}

void Sample_OnKeyUp(DXSample* const sample, UINT8 key) {
	SimpleCamera_OnKeyUp(&sample->camera, key);
}

/*************************************************************************************
 Private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample)
{
	int isDebugFactory = 0;
	HRESULT hr;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	ID3D12Debug* debugController = NULL;
	hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	if (SUCCEEDED(hr))
	{
		ID3D12Debug_EnableDebugLayer(debugController);
		isDebugFactory |= DXGI_CREATE_FACTORY_DEBUG;
		RELEASE(debugController);
	}
#endif

	IDXGIFactory4* factory = NULL;
	hr = CreateDXGIFactory2(isDebugFactory, IID_PPV_ARGS(&factory));
	if (FAILED(hr)) LogAndExit(hr);

	/* Create device */

	IDXGIAdapter1* hardwareAdapter = NULL;
	IDXGIFactory1* factoryAsFactory1 = NULL;
	hr = CAST(factory, factoryAsFactory1);
	if (FAILED(hr)) LogAndExit(hr);
	GetHardwareAdapter(factoryAsFactory1, &hardwareAdapter, false);
	RELEASE(factoryAsFactory1);

	IUnknown* hardwareAdapterAsUnknown = NULL;
	LogAndExit(CAST(hardwareAdapter, hardwareAdapterAsUnknown));
	LogAndExit(D3D12CreateDevice(hardwareAdapterAsUnknown, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&sample->device)));
	RELEASE(hardwareAdapterAsUnknown);
	RELEASE(hardwareAdapter);

	/* Create command queue */

	D3D12_COMMAND_QUEUE_DESC queueDesc = { .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE, .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
	ID3D12CommandQueue* commandQueue = NULL;
	LogAndExit(CALL(CreateCommandQueue, sample->device, &queueDesc, IID_PPV_ARGS(&sample->commandQueue)));

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
	LogAndExit(CAST(sample->commandQueue, commandQueueAsIUnknown));
	IDXGISwapChain1* swapChainAsSwapChain1 = NULL;
	LogAndExit(CALL(CreateSwapChainForHwnd,
		factory,
		commandQueueAsIUnknown,        // Swap chain needs the queue so that it can force a flush on it
		G_HWND,
		&swapChainDesc,
		NULL,
		NULL,
		&swapChainAsSwapChain1
	));
	RELEASE(commandQueueAsIUnknown);
	LogAndExit(CAST(swapChainAsSwapChain1, sample->swapChain));
	RELEASE(swapChainAsSwapChain1);

	LogAndExit(CALL(MakeWindowAssociation, factory, G_HWND, DXGI_MWA_NO_ALT_ENTER));
	sample->frameIndex = CALL(GetCurrentBackBufferIndex, sample->swapChain);

	/* Create descriptor heaps (3 RTVs, 1 DSV, 1 SRV, many CBVs for resources and a Sampler in this example) */
	{
		//RTVs
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.NumDescriptors = FrameCount,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
		};
		LogAndExit(CALL(CreateDescriptorHeap, sample->device, &rtvHeapDesc, IID_PPV_ARGS(&sample->rtvHeap)));
		sample->rtvDescriptorSize = CALL(GetDescriptorHandleIncrementSize, sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// DSV
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {
			.NumDescriptors = 1,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
		};
		LogAndExit(CALL(CreateDescriptorHeap, sample->device, &dsvHeapDesc, IID_PPV_ARGS(&sample->dsvHeap)));

		// SRV and CBV
		D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {
			.NumDescriptors =
				FrameCount * CityRowCount * CityColumnCount        // FrameCount frames * CityRowCount * CityColumnCount.
				+ 1,                                               // + 1 for the SRV
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		};
		LogAndExit(CALL(CreateDescriptorHeap, sample->device, &cbvSrvHeapDesc, IID_PPV_ARGS(&sample->cbvSrvHeap)));
		sample->cbvSrvDescriptorSize = CALL(GetDescriptorHandleIncrementSize, sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		NAME_D3D12_OBJECT(sample->cbvSrvHeap);

		// Sampler
		D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {
			.NumDescriptors = 1,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		};
		LogAndExit(CALL(CreateDescriptorHeap, sample->device, &samplerHeapDesc, IID_PPV_ARGS(&sample->samplerHeap)));
	}

	LogAndExit(CALL(CreateCommandAllocator, sample->device, D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sample->commandAllocator)));
	RELEASE(factory);
}

static void LoadAssets(DXSample* const sample)
{
	// Resources below need to stay in scope until the command list that references 
	// them has finished executing on the GPU. We will flush the GPU at the end of
	// this method to ensure the resources are not prematurely destroyed.
	ID3D12Resource* vertexBufferUploadHeap = NULL;
	ID3D12Resource* indexBufferUploadHeap = NULL;
	ID3D12Resource* textureUploadHeap = NULL;

	/* Create root signature */
	{
		// This is the highest version the sample supports. 
		// If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = { .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1 };
		if (FAILED(CALL(CheckFeatureSupport, sample->device, D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		D3D12_DESCRIPTOR_RANGE1 ranges[3] = {
			CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, /* register(t0) */ 0, 0,
			D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND),
			CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, /* register(s0) */ 0, 0,
			D3D12_DESCRIPTOR_RANGE_FLAG_NONE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND),
			CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, /* register(b0) */ 0, 0,
			D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		};

		D3D12_ROOT_PARAMETER1 rootParameters[3] = {
			CD3DX12_ROOT_PARAMETER1_AsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL),
			CD3DX12_ROOT_PARAMETER1_AsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL),
			CD3DX12_ROOT_PARAMETER1_AsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL)
		};

		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {
			.Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
			.Desc_1_1.NumParameters = _countof(rootParameters),
			.Desc_1_1.pParameters = rootParameters,
			.Desc_1_1.NumStaticSamplers = 0,
			.Desc_1_1.pStaticSamplers = NULL,
			.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
		};

		// Note: Blobs don't seem to be actually RefCounted. We can call Release on them even after the count has reached zero
		// and they also don't show up when calling ReportLiveObjects. Therefore, there is no need to release them.
		ID3DBlob* signature = NULL;
		ID3DBlob* error = NULL;

		LogAndExit(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		const LPVOID bufferPointer = CALL(GetBufferPointer, signature);
		const SIZE_T bufferSize = CALL(GetBufferSize, signature);
		LogAndExit(CALL(CreateRootSignature, sample->device, 0, bufferPointer, bufferSize, IID_PPV_ARGS(&sample->rootSignature)));
	}

	/* Create the pipeline state, which includes loading shaders */
	{
		UINT8* pVertexShaderData;
		UINT8* pPixelShaderData1;
		UINT8* pPixelShaderData2;
		UINT vertexShaderDataLength;
		UINT pixelShaderDataLength1;
		UINT pixelShaderDataLength2;

		/* Load pre-compiled shaders */
		LoadShaderData(sample->assetsPath, L"shaders/shader_mesh_simple_vert.cso", &pVertexShaderData, &vertexShaderDataLength);
		LoadShaderData(sample->assetsPath, L"shaders/shader_mesh_simple_pixel.cso", &pPixelShaderData1, &pixelShaderDataLength1);
		LoadShaderData(sample->assetsPath, L"shaders/shader_mesh_alt_pixel.cso", &pPixelShaderData2, &pixelShaderDataLength2);

		D3D12_RASTERIZER_DESC rasterizerStateDesc = CD3DX12_DEFAULT_RASTERIZER_DESC();
		rasterizerStateDesc.CullMode = D3D12_CULL_MODE_NONE;

		// Describe and create the graphics pipeline state objects (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
			.InputLayout = { 
				.pInputElementDescs = SampleAssets_StandardVertexDescription, 
			    .NumElements = _countof(SampleAssets_StandardVertexDescription)
		     },
			.pRootSignature = sample->rootSignature,
			.VS = (D3D12_SHADER_BYTECODE){
				.pShaderBytecode = pVertexShaderData,
				.BytecodeLength = vertexShaderDataLength,
			},
			.PS = (D3D12_SHADER_BYTECODE){
				.pShaderBytecode = pPixelShaderData1,
				.BytecodeLength = pixelShaderDataLength1,
			},
			.RasterizerState = rasterizerStateDesc,
			.BlendState = CD3DX12_DEFAULT_BLEND_DESC(),
			.DepthStencilState = CD3DX12_DEFAULT_DEPTH_STENCIL_DESC(),
			.SampleMask = UINT_MAX,
			.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
			.NumRenderTargets = 1,
			.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM,
			.DSVFormat = DXGI_FORMAT_D32_FLOAT,
			.SampleDesc.Count = 1,
		};

		LogAndExit(CALL(CreateGraphicsPipelineState, sample->device, &psoDesc, IID_PPV_ARGS(&sample->pipelineState1)));
		NAME_D3D12_OBJECT(sample->pipelineState1);

		// Modify the description to use an alternate pixel shader and create a second PSO.
		psoDesc.PS = (D3D12_SHADER_BYTECODE){
				.pShaderBytecode = pPixelShaderData2,
				.BytecodeLength = pixelShaderDataLength2,
		};
		LogAndExit(CALL(CreateGraphicsPipelineState, sample->device, &psoDesc, IID_PPV_ARGS(&sample->pipelineState2)));
		NAME_D3D12_OBJECT(sample->pipelineState2);

		CleanAllocatedDataFromFile(pVertexShaderData);
		CleanAllocatedDataFromFile(pPixelShaderData1);
		CleanAllocatedDataFromFile(pPixelShaderData2);
	}

	/* Create the command list */

	LogAndExit(CALL(CreateCommandList, sample->device, 
		0, 
		D3D12_COMMAND_LIST_TYPE_DIRECT, 
		sample->commandAllocator, 
		NULL, 
		IID_PPV_ARGS(&sample->commandList))
	);
	NAME_D3D12_OBJECT(sample->commandList);

	// Create render target views (RTVs).
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	CALL(GetCPUDescriptorHandleForHeapStart, sample->rtvHeap, &rtvHandle);
	for (UINT i = 0; i < FrameCount; i++)
	{
		LogAndExit(CALL(GetBuffer, sample->swapChain, i, IID_PPV_ARGS(&sample->renderTargets[i])));
		CALL(CreateRenderTargetView, sample->device, sample->renderTargets[i], NULL, rtvHandle);
		// walk an offset equivalent to one descriptor to go to next space to store the next RTV
		rtvHandle.ptr += sample->rtvDescriptorSize;
		NAME_D3D12_OBJECT_INDEXED(sample->renderTargets, i);
	}

	// Read in mesh data for vertex/index buffers.
	UINT8* pMeshData;
	UINT meshDataLength;
	LoadShaderData(sample->assetsPath, SampleAssets_DATA_FILE_NAME, &pMeshData, &meshDataLength);
	// Create the vertex buffer.
	{
		D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC_BUFFER(SampleAssets_VERTEX_DATA_SIZE, D3D12_RESOURCE_FLAG_NONE, 0);
		LogAndExit(CALL(CreateCommittedResource, sample->device,
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&vertexBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			NULL,
			IID_PPV_ARGS(&sample->vertexBuffer))
		);

		D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC vertexBufferDescUpload = CD3DX12_RESOURCE_DESC_BUFFER(SampleAssets_VERTEX_DATA_SIZE, D3D12_RESOURCE_FLAG_NONE, 0);
		LogAndExit(CALL(CreateCommittedResource, sample->device,
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&vertexBufferDescUpload,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			IID_PPV_ARGS(&vertexBufferUploadHeap))
		);

		NAME_D3D12_OBJECT(sample->vertexBuffer);

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the vertex buffer.
		D3D12_SUBRESOURCE_DATA vertexData = {
			.pData = pMeshData + SampleAssets_VERTEX_DATA_OFFSET,
			.RowPitch = SampleAssets_VERTEX_DATA_SIZE,
			.SlicePitch = vertexData.RowPitch,
		};
		UpdateSubresources(sample->commandList, sample->vertexBuffer, vertexBufferUploadHeap, 0, 0, 1, &vertexData);
		D3D12_RESOURCE_BARRIER transitionCopyDestToVertexBuffer = CD3DX12_DefaultTransition(sample->vertexBuffer,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		CALL(ResourceBarrier, sample->commandList, 1, &transitionCopyDestToVertexBuffer);

		// Initialize the vertex buffer view.
		sample->vertexBufferView.BufferLocation = CALL(GetGPUVirtualAddress, sample->vertexBuffer);
		sample->vertexBufferView.StrideInBytes = SampleAssets_STANDARD_VERTEX_STRIDE;
		sample->vertexBufferView.SizeInBytes = SampleAssets_VERTEX_DATA_SIZE;
	}

	// Create the index buffer.
	{
		D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC_BUFFER(SampleAssets_INDEX_DATA_SIZE, D3D12_RESOURCE_FLAG_NONE, 0);
		LogAndExit(CALL(CreateCommittedResource, sample->device,
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&indexBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			NULL,
			IID_PPV_ARGS(&sample->indexBuffer)));

		D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC indexBufferDescUpload = CD3DX12_RESOURCE_DESC_BUFFER(SampleAssets_INDEX_DATA_SIZE, D3D12_RESOURCE_FLAG_NONE, 0);

		LogAndExit(CALL(CreateCommittedResource, sample->device,
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&indexBufferDescUpload,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			IID_PPV_ARGS(&indexBufferUploadHeap)));

		NAME_D3D12_OBJECT(sample->indexBuffer);

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the index buffer.
		D3D12_SUBRESOURCE_DATA indexData = {
			.pData = pMeshData + SampleAssets_INDEX_DATA_OFFSET,
			.RowPitch = SampleAssets_INDEX_DATA_SIZE,
			.SlicePitch = indexData.RowPitch,
		};

		UpdateSubresources(sample->commandList, sample->indexBuffer, indexBufferUploadHeap, 0, 0, 1, &indexData);
		D3D12_RESOURCE_BARRIER transitionCopyDestToIndexBuffer = CD3DX12_DefaultTransition(sample->indexBuffer,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_INDEX_BUFFER);
		CALL(ResourceBarrier, sample->commandList, 1, &transitionCopyDestToIndexBuffer);

		// Describe the index buffer view.
		sample->indexBufferView.BufferLocation = CALL(GetGPUVirtualAddress, sample->indexBuffer);
		sample->indexBufferView.Format = SampleAssets_STANDARD_INDEX_FORMAT;
		sample->indexBufferView.SizeInBytes = SampleAssets_INDEX_DATA_SIZE;

		sample->numIndices = SampleAssets_INDEX_DATA_SIZE / 4U;    // R32_UINT (SampleAssets_StandardIndexFormat) = 4 bytes each.
	}

	// Create the texture and sampler.
	{
		// Describe and create a Texture2D.
		D3D12_RESOURCE_DESC textureDesc = {
			.MipLevels = SampleAssets_Textures[0].MipLevels,
			.Format = SampleAssets_Textures[0].Format,
			.Width = SampleAssets_Textures[0].Width,
			.Height = SampleAssets_Textures[0].Height,
			.Flags = D3D12_RESOURCE_FLAG_NONE,
			.DepthOrArraySize = 1,
			.SampleDesc.Count = 1,
			.SampleDesc.Quality = 0,
			.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		};
		D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		LogAndExit(CALL(CreateCommittedResource, sample->device,
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			NULL,
			IID_PPV_ARGS(&sample->texture))
		);
		NAME_D3D12_OBJECT(sample->texture);

		const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(sample->texture, 0, subresourceCount);
		D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC uploadBuffer = CD3DX12_RESOURCE_DESC_BUFFER(uploadBufferSize, D3D12_RESOURCE_FLAG_NONE, 0);
		LogAndExit(CALL(CreateCommittedResource, sample->device,
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&uploadBuffer,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			IID_PPV_ARGS(&textureUploadHeap))
		);

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the Texture2D.
		D3D12_SUBRESOURCE_DATA textureData = {
			.pData = pMeshData + SampleAssets_Textures[0].Data[0].Offset,
			.RowPitch = SampleAssets_Textures[0].Data[0].Pitch,
			.SlicePitch = SampleAssets_Textures[0].Data[0].Size,
		};

		UpdateSubresources(sample->commandList, sample->texture, textureUploadHeap, 0, 0, subresourceCount, &textureData);
		D3D12_RESOURCE_BARRIER fromCopyToPixelShaderResource = CD3DX12_DefaultTransition(sample->texture,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		CALL(ResourceBarrier, sample->commandList, 1, &fromCopyToPixelShaderResource);

		// Describe and create a sampler.
		D3D12_SAMPLER_DESC samplerDesc = {
			.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.MinLOD = 0,
			.MaxLOD = D3D12_FLOAT32_MAX,
			.MipLODBias = 0.0f,
			.MaxAnisotropy = 1,
			.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
		};
		D3D12_CPU_DESCRIPTOR_HANDLE samplerHandle;
		CALL(GetCPUDescriptorHandleForHeapStart, sample->samplerHeap, &samplerHandle);
		CALL(CreateSampler, sample->device, &samplerDesc, samplerHandle);


		// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Format = SampleAssets_Textures->Format,
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Texture2D.MipLevels = 1,
		};
		// SRV will be the first
		D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle;
		CALL(GetCPUDescriptorHandleForHeapStart, sample->cbvSrvHeap, &cbvSrvHandle);
		CALL(CreateShaderResourceView, sample->device, sample->texture, &srvDesc, cbvSrvHandle);
	}

	CleanAllocatedDataFromFile(pMeshData);

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
		LogAndExit(CALL(CreateCommittedResource, sample->device,
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&sample->depthStencil)
		));

		NAME_D3D12_OBJECT(sample->depthStencil);

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
		CALL(GetCPUDescriptorHandleForHeapStart, sample->dsvHeap, &dsvHandle);
		CALL(CreateDepthStencilView, sample->device, sample->depthStencil, &depthStencilDesc, dsvHandle);
	}

	// Close the command list and execute it to begin the initial GPU setup.
	LogAndExit(CALL(Close, sample->commandList));
	ID3D12CommandList* asCommandList = NULL;
	CAST(sample->commandList, asCommandList);
	ID3D12CommandList* ppCommandLists[] = { asCommandList };
	CALL(ExecuteCommandLists, sample->commandQueue, _countof(ppCommandLists), ppCommandLists);
	RELEASE(asCommandList);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		LogAndExit(CALL(CreateFence, sample->device, sample->fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sample->fence)));
		sample->fenceValue++;

		// Create an event handle to use for frame synchronization.
		sample->fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (sample->fenceEvent == NULL)
		{
			LogAndExit(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.

		// Signal and increment the fence value.
		const UINT64 fenceToWaitFor = sample->fenceValue;
		LogAndExit(CALL(Signal, sample->commandQueue, sample->fence, fenceToWaitFor));
		sample->fenceValue++;

		// Wait until the fence is completed.
		LogAndExit(CALL(SetEventOnCompletion, sample->fence, fenceToWaitFor, sample->fenceEvent));
		WaitForSingleObject(sample->fenceEvent, INFINITE);
	}

	CreateFrameResources(sample);
	RELEASE(vertexBufferUploadHeap);
	RELEASE(indexBufferUploadHeap);
	RELEASE(textureUploadHeap);
}

// Record all the commands we need to render the scene into the command list
static void PopulateCommandList(DXSample* const sample)
{
	// Command list allocators can only be reset when the associated
	// command lists have finished execution on the GPU; apps should use
	// fences to determine GPU execution progress.
	LogAndExit(CALL(Reset, sample->currFrameResource->commandAllocator));

	// However, when ExecuteCommandList() is called on a particular command
	// list, that command list can then be reset at any time and must be before
	// re-recording.
	LogAndExit(CALL(Reset, sample->commandList, sample->currFrameResource->commandAllocator, sample->pipelineState1));

	// Set necessary state.
	CALL(SetGraphicsRootSignature, sample->commandList, sample->rootSignature);

	ID3D12DescriptorHeap* ppHeaps[] = { sample->cbvSrvHeap, sample->samplerHeap };
	CALL(SetDescriptorHeaps, sample->commandList, _countof(ppHeaps), ppHeaps);
	CALL(RSSetViewports, sample->commandList, 1, &sample->viewport);
	CALL(RSSetScissorRects, sample->commandList, 1, &sample->scissorRect);

	// Indicate that the back buffer will be used as a render target.
	const D3D12_RESOURCE_BARRIER transitionBarrierRT = CD3DX12_DefaultTransition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	CALL(ResourceBarrier, sample->commandList, 1, &transitionBarrierRT);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvCPUHandle;
	CALL(GetCPUDescriptorHandleForHeapStart, sample->rtvHeap, &rtvCPUHandle);
	rtvCPUHandle.ptr = (SIZE_T)((INT64)rtvCPUHandle.ptr + ((INT64)sample->frameIndex) * ((INT64)sample->rtvDescriptorSize));
	D3D12_CPU_DESCRIPTOR_HANDLE dsvCPUHandle;
	CALL(GetCPUDescriptorHandleForHeapStart, sample->dsvHeap, &dsvCPUHandle);
	CALL(OMSetRenderTargets, sample->commandList, 1, &rtvCPUHandle, FALSE, &dsvCPUHandle);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	CALL(ClearRenderTargetView, sample->commandList, rtvCPUHandle, clearColor, 0, NULL);
	CALL(ClearDepthStencilView, sample->commandList, dsvCPUHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

	if (UseBundles)
	{
		// Execute the prebuilt bundle.
		CALL(ExecuteBundle, sample->commandList, sample->currFrameResource->bundle);
	}
	else
	{
		// Populate a new command list.
		FrameResource_PopulateCommandList(sample->currFrameResource,
			sample->commandList,
			sample->pipelineState1,
			sample->pipelineState2,
			sample->currentFrameResourceIndex,
			sample->numIndices,
			&sample->indexBufferView,
			&sample->vertexBufferView,
			sample->cbvSrvHeap,
			sample->cbvSrvDescriptorSize,
			sample->samplerHeap,
			sample->rootSignature
		);
	}

	// Indicate that the back buffer will now be used to present.
	const D3D12_RESOURCE_BARRIER transitionBarrierRTtoPresent = CD3DX12_DefaultTransition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	CALL(ResourceBarrier, sample->commandList, 1, &transitionBarrierRTtoPresent);
	LogAndExit(CALL(Close, sample->commandList));
}

// Create the resources that will be used every frame.
static void CreateFrameResources(DXSample* const sample)
{
	D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle;
	CALL(GetCPUDescriptorHandleForHeapStart, sample->cbvSrvHeap, &cbvSrvHandle);
	// Start offseted one descriptor size, to move past the SRV in slot 1
	cbvSrvHandle.ptr = (SIZE_T)(((INT64)cbvSrvHandle.ptr) + ((INT64)sample->cbvSrvDescriptorSize));

	// Initialize each frame resource.
	for (UINT frame = 0; frame < FrameCount; frame++)
	{
		FrameResource* pFrameResource = HeapAlloc(GetProcessHeap(), 0, sizeof(FrameResource));
		FrameResource_Init(pFrameResource, sample->device, CityRowCount, CityColumnCount);

		UINT64 cbOffset = 0;
		for (UINT row = 0; row < CityRowCount; row++)
		{
			for (UINT col = 0; col < CityColumnCount; col++)
			{
				D3D12_GPU_VIRTUAL_ADDRESS gpuVirtualAddress = CALL(GetGPUVirtualAddress, pFrameResource->cbvUploadHeap);
				// Describe and create a constant buffer view (CBV).
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
					.BufferLocation = gpuVirtualAddress + cbOffset,
					.SizeInBytes = sizeof(SceneConstantBuffer),
				};
				cbOffset += cbvDesc.SizeInBytes;
				CALL(CreateConstantBufferView, sample->device, &cbvDesc, cbvSrvHandle);
				cbvSrvHandle.ptr = (SIZE_T)(((INT64)cbvSrvHandle.ptr) + ((INT64)sample->cbvSrvDescriptorSize));
			}
		}
		FrameResource_InitBundle(pFrameResource,
			sample->device,
			sample->pipelineState1,
			sample->pipelineState2,
			frame,
			sample->numIndices,
			&sample->indexBufferView,
			&sample->vertexBufferView,
			sample->cbvSrvHeap,
			sample->cbvSrvDescriptorSize,
			sample->samplerHeap,
			sample->rootSignature);

		sample->frameResources[frame] = pFrameResource;
	}
}


static void LoadShaderData(const WCHAR* const base, const WCHAR* const shaderRelativePath, UINT8** shaderData, UINT* shaderDataLen) {
	size_t bufferSize = wcslen(base) + wcslen(shaderRelativePath) + 1;
	WCHAR* shaderDataPath = HeapAlloc(GetProcessHeap(), 0, bufferSize * sizeof(WCHAR));
	swprintf(shaderDataPath, bufferSize, L"%s%s", base, shaderRelativePath);
	LogAndExit(ReadDataFromFile(shaderDataPath, shaderData, shaderDataLen));
	HeapFree(GetProcessHeap(), 0, shaderDataPath);
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
	RELEASE(sample->rootSignature);
	RELEASE(sample->rtvHeap);
	RELEASE(sample->cbvSrvHeap);
	RELEASE(sample->dsvHeap);
	RELEASE(sample->samplerHeap);
	RELEASE(sample->pipelineState1);
	RELEASE(sample->pipelineState2);
	RELEASE(sample->vertexBuffer);
	RELEASE(sample->indexBuffer);
	RELEASE(sample->texture);
	RELEASE(sample->fence);
}