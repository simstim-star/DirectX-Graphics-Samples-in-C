#define COBJMACROS

#include <stdio.h>
#include "sample.h"
#include "sample_commons.h"
#include "macros.h"
#include "window.h"
#include "d3dcompiler.h"
#include "dxheaders/barrier_helpers.h"
#include "dxheaders/core_helpers.h"
#include "dxheaders/d3dx12_pipeline_state_stream.h"
#include "DirectXMathC.h"

#include "dxgidebug.h"

/*************************************************************************************
 Constants
**************************************************************************************/

enum DescriptorHeapIndex
{
	SRV_MeshInfoLODs = 0,
	SRV_VertexLODs = SRV_MeshInfoLODs + MAX_LOD_LEVELS,
	SRV_MeshletLODs = SRV_VertexLODs + MAX_LOD_LEVELS,
	SRV_UniqueVertexIndexLODs = SRV_MeshletLODs + MAX_LOD_LEVELS,
	SRV_PrimitiveIndexLODs = SRV_UniqueVertexIndexLODs + MAX_LOD_LEVELS,
	SRV_Count = SRV_PrimitiveIndexLODs + MAX_LOD_LEVELS,
};

const float c_fovy = XM_PI / 3.0f;

// Limit our dispatch threadgroup count to 65536 for indexing simplicity.
const uint32_t c_maxGroupDispatchCount = 65536u;

const wchar_t* c_lodFilenames[LodsCount] =
{
	L"..\\Assets\\Dragon_LOD0.bin",
	L"..\\Assets\\Dragon_LOD1.bin",
	L"..\\Assets\\Dragon_LOD2.bin",
	L"..\\Assets\\Dragon_LOD3.bin",
	L"..\\Assets\\Dragon_LOD4.bin",
	L"..\\Assets\\Dragon_LOD5.bin",
};

const wchar_t* c_ampShaderFilename = L"shaders/MeshletAS.cso";
const wchar_t* c_meshShaderFilename = L"shaders/MeshletMS.cso";
const wchar_t* c_pixelShaderFilename = L"shaders/MeshletPS.cso";

/*************************************************************************************
 Forward declarations of private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample);
static void LoadAssets(DXSample* const sample);
static void WaitForGpu(DXSample* sample);
static void PopulateCommandList(DXSample* const sample);
static void ReleaseAll(DXSample* const sample);
static void LoadShaderData(const WCHAR* const base, const WCHAR* const shaderRelativePath, UINT8** shaderData, UINT* shaderDataLen);
D3D12_CPU_DESCRIPTOR_HANDLE OffsetDescHandle(D3D12_CPU_DESCRIPTOR_HANDLE srvHandle, uint32_t index, uint32_t srvDescriptorSize);
void MoveToNextFrame(DXSample* sample);
void RegenerateInstances(DXSample* sample);

UINT32 AlignU32(UINT32 size);
UINT64  AlignU64(UINT64 size);

/*************************************************************************************
 Public functions
**************************************************************************************/

void Sample_Init(DXSample* const sample) {
	sample->aspectRatio = (float)(sample->width) / (float)(sample->height);
	sample->frameIndex = 0;
	sample->frameCounter = 0;
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
	sample->dsvDescriptorSize = 0;
	sample->srvDescriptorSize = 0;
	GetCurrentPath(sample->assetsPath, _countof(sample->assetsPath));

	StepTimer_Init(&sample->timer);
	sample->camera = SimpleCamera_Spawn((XMFLOAT3) { 0, 75, 150 });
	sample->camera.moveSpeed = 150.0f;

	sample->frameIndex = 0;
	sample->frameCounter = 0;
	sample->fenceEvent = NULL;
	for(int i =0; i < FrameCount ; ++i) sample->fenceValues[i] = 0;
	sample->constantData = NULL;
	sample->instanceData = NULL;
	sample->renderMode = LOD;
	sample->instanceLevel = 0;
	sample->instanceCount = 1;
	sample->updateInstances = true;

	LoadPipeline(sample);
	LoadAssets(sample);
}

void Sample_Destroy(DXSample* sample)
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu(sample);

	CloseHandle(sample->fenceEvent);
	ReleaseAll(sample);

	sample = NULL;
}

void Sample_AtExit(DXSample* sample)
{
	ReleaseAll(sample);
	sample = NULL;
}

void Sample_Update(DXSample* const sample) {
	Tick(&sample->timer);
	if (sample->frameCounter++ % 30 == 0)
	{
		// Update window text with FPS value.
		wchar_t fps[64];
		swprintf_s(fps, 64, L"%ufps", sample->timer.framesPerSecond);
		SetWindowTextW(G_HWND, fps);
	}

	SimpleCamera_Update(&sample->camera, TicksToSeconds(sample->timer.elapsedTicks));
	XMMATRIX viewMatrix = SimpleCamera_GetViewMatrix(sample->camera.position, sample->camera.lookDirection, sample->camera.upDirection);
	XMMATRIX projMatrix = SimpleCamera_GetProjectionMatrix(c_fovy, sample->aspectRatio, 1.0f, 1000.0f);
	XMMATRIX viewProj = XM_MAT_MULT(viewMatrix, projMatrix);
	XMVECTOR scale, rot, viewPos;
	XM_MAT_DECOMP(&scale, &rot, &viewPos, viewProj);

	XMMATRIX vp = XM_MAT_TRANSP(viewProj);
	XMVECTOR leftPlane = XM_VEC_ADD(vp.r[3], vp.r[0]);
	XMVECTOR rightPlane = XM_VEC_SUBTRACT(vp.r[3], vp.r[0]);
	XMVECTOR bottonPlane = XM_VEC_ADD(vp.r[3], vp.r[1]);
	XMVECTOR topPlane = XM_VEC_SUBTRACT(vp.r[3], vp.r[1]);
	XMVECTOR farPlane = XM_VEC_SUBTRACT(vp.r[3], vp.r[2]);
	XMVECTOR planes[6] =
	{
		XM_PLANE_NORM(leftPlane),
		XM_PLANE_NORM(rightPlane),
		XM_PLANE_NORM(bottonPlane),
		XM_PLANE_NORM(topPlane), 
		XM_PLANE_NORM(vp.r[2]),
		XM_PLANE_NORM(farPlane),
	};

	Constants* constants = &sample->constantData[sample->frameIndex];
	XMMATRIX transposedView = XM_MAT_TRANSP(viewProj);
	XM_STORE_FLOAT4X4(&constants->View, transposedView);
	viewProj = XM_MAT_TRANSP(viewProj);
	XM_STORE_FLOAT4X4(&constants->ViewProj, viewProj);
	XMMATRIX viewProjInv = XM_MAT_INV(NULL, viewProj);
	XMVECTOR baseToView = XM_VEC3_TRANSFORM(g_XMZero.v, viewProjInv);
	XM_STORE_FLOAT3(&constants->ViewPosition, baseToView);

	for (uint32_t i = 0; i < 6; ++i)
	{
		XM_STORE_FLOAT4(&constants->Planes[i], planes[i]);
	}

	constants->RenderMode = sample->renderMode;
	constants->LODCount = LodsCount;
	constants->RecipTanHalfFovy = 1.0f / tanf(c_fovy * 0.5f);
}

void Sample_Render(DXSample* const sample)
{
	PopulateCommandList(sample);

	ID3D12CommandList* asCommandList = NULL;
	CAST(sample->commandList, asCommandList);
	ID3D12CommandList* ppCommandLists[] = { asCommandList };
	ID3D12CommandQueue_ExecuteCommandLists(sample->commandQueue, _countof(ppCommandLists), ppCommandLists);
	RELEASE(asCommandList);

	HRESULT hr = IDXGISwapChain3_Present(sample->swapChain, 1, 0);
	if (FAILED(hr)) LogErrAndExit(IDXGISwapChain3_Present(sample->swapChain, 1, 0));

	MoveToNextFrame(sample);
}


void Sample_KeyDown(DXSample* const sample, UINT8 key) {
	switch (key)
	{
	case VK_OEM_PLUS:
		++sample->instanceLevel;
		RegenerateInstances(sample);
		break;

	case VK_OEM_MINUS:
		if (sample->instanceLevel != 0)
		{
			--sample->instanceLevel;
			RegenerateInstances(sample);
		}
		break;

	case VK_SPACE:
		uint32_t renderMode = 1 + sample->renderMode;
		sample->renderMode = renderMode % Count;
		break;
	}
	SimpleCamera_OnKeyDown(&sample->camera, key);
}

void Sample_KeyUp(DXSample* const sample, UINT8 key) {
	SimpleCamera_OnKeyUp(&sample->camera, key);
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
	if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**) &debugController)))
	{
		ID3D12Debug_EnableDebugLayer(debugController);
		isDebugFactory |= DXGI_CREATE_FACTORY_DEBUG;
		RELEASE(debugController);
	}
#endif

	IDXGIFactory4* factory = NULL;
	HRESULT hr = CreateDXGIFactory2(isDebugFactory, &IID_IDXGIFactory4, (void**)(&factory));
	if (FAILED(hr)) LogErrAndExit(hr);

	/* Create device */

	IDXGIAdapter1* hardwareAdapter = NULL;
	IDXGIFactory1* factoryAsFactory1 = NULL;

	hr = CAST(factory, factoryAsFactory1);
	if (FAILED(hr)) LogErrAndExit(hr);

	GetHardwareAdapter(factoryAsFactory1, &hardwareAdapter, FALSE);
	RELEASE(factoryAsFactory1);

	IUnknown* hardwareAdapterAsUnknown = NULL;

	hr = CAST(hardwareAdapter, hardwareAdapterAsUnknown);
	if (FAILED(hr)) LogErrAndExit(hr);

	hr = D3D12CreateDevice(hardwareAdapterAsUnknown, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device2, (void**)&sample->device);
	if (FAILED(hr)) LogErrAndExit(hr);

	RELEASE(hardwareAdapterAsUnknown);
	RELEASE(hardwareAdapter);

	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_5 };
	hr = ID3D12Device2_CheckFeatureSupport(sample->device, D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
	if (FAILED(hr) || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5))
	{
		OutputDebugStringA("ERROR: Shader Model 6.5 is not supported\n");
		LogErrAndExit(hr);
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {0};
	hr = ID3D12Device2_CheckFeatureSupport(sample->device, D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
	if (FAILED(hr) || (features.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED))
	{
		OutputDebugStringA("ERROR: Mesh Shaders aren't supported!\n");
		LogErrAndExit(hr);
	}
	
	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
	};
	hr = ID3D12Device2_CreateCommandQueue(sample->device, &queueDesc, &IID_ID3D12CommandQueue, (void**)&sample->commandQueue);
	if (FAILED(hr)) LogErrAndExit(hr);

	// Describe and create the swap chain.
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
	hr = CAST(sample->commandQueue, commandQueueAsIUnknown);
	if (FAILED(hr))	LogErrAndExit(hr);

	IDXGISwapChain1* swapChainAsSwapChain1 = NULL;
	hr = IDXGIFactory4_CreateSwapChainForHwnd(factory,
		commandQueueAsIUnknown,        // Swap chain needs the queue so that it can force a flush on it
		G_HWND,
		&swapChainDesc,
		NULL,
		NULL,
		&swapChainAsSwapChain1);
	if (FAILED(hr)) LogErrAndExit(hr);

	RELEASE(commandQueueAsIUnknown);

	hr = CAST(swapChainAsSwapChain1, sample->swapChain);
	if (FAILED(hr)) LogErrAndExit(hr);
	RELEASE(swapChainAsSwapChain1);

	// This sample does not support fullscreen transitions.
	hr = IDXGIFactory4_MakeWindowAssociation(factory, G_HWND, DXGI_MWA_NO_ALT_ENTER);
	if (FAILED(hr)) LogErrAndExit(hr);

	sample->frameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(sample->swapChain);

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.NumDescriptors = FrameCount,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};
		hr = ID3D12Device2_CreateDescriptorHeap(sample->device, &rtvHeapDesc, &IID_ID3D12DescriptorHeap, (void**)&sample->rtvHeap);
		if (FAILED(hr)) LogErrAndExit(hr);

		sample->rtvDescriptorSize = ID3D12Device2_GetDescriptorHandleIncrementSize(sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {
			.NumDescriptors = 1,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};
		hr = ID3D12Device2_CreateDescriptorHeap(sample->device, &dsvHeapDesc, &IID_ID3D12DescriptorHeap, (void**)&sample->dsvHeap);
		if (FAILED(hr)) LogErrAndExit(hr);

		sample->dsvDescriptorSize = ID3D12Device2_GetDescriptorHandleIncrementSize(sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		// Describe and create a shader resource view (SRV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {
			.NumDescriptors = SRV_Count,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		};
		hr = ID3D12Device2_CreateDescriptorHeap(sample->device, &srvHeapDesc, &IID_ID3D12DescriptorHeap, (void**)sample->srvHeap);
		if (FAILED(hr)) LogErrAndExit(hr);

		sample->srvDescriptorSize = ID3D12Device2_GetDescriptorHandleIncrementSize(sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Create frame resources.
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sample->rtvHeap, &rtvHandle);

		// Create a RTV and a command allocator for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			hr = IDXGISwapChain3_GetBuffer(sample->swapChain, n, &IID_ID3D12Resource, (void**)&sample->renderTargets[n]);
			if (FAILED(hr)) LogErrAndExit(hr);
			ID3D12Device2_CreateRenderTargetView(sample->device, sample->renderTargets[n], NULL, rtvHandle);

			const INT64 CurrentRtvOffset = sample->frameIndex * sample->rtvDescriptorSize;
			rtvHandle.ptr = (SIZE_T)((INT64)(rtvHandle.ptr) + CurrentRtvOffset);
			hr = ID3D12Device2_CreateCommandAllocator(sample->device, 
				D3D12_COMMAND_LIST_TYPE_DIRECT, 
				&IID_ID3D12CommandAllocator, 
				(void**)&sample->commandAllocators[n]);
			if (FAILED(hr)) LogErrAndExit(hr);
		}
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

		const D3D12_HEAP_PROPERTIES heapPropertyUpload = (D3D12_HEAP_PROPERTIES){
		.Type = D3D12_HEAP_TYPE_UPLOAD,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 1,
		.VisibleNodeMask = 1,
		};


		const D3D12_HEAP_PROPERTIES depthStencilHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		const D3D12_RESOURCE_DESC depthStencilTextureDesc = CD3DX12_TEX2D(DXGI_FORMAT_D32_FLOAT,
			sample->width, sample->height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_TEXTURE_LAYOUT_UNKNOWN, 0);

		hr = ID3D12Device2_CreateCommittedResource(sample->device,
			&depthStencilHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilTextureDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			&IID_ID3D12Resource,
			(void**)&sample->depthStencil);
		if (FAILED(hr)) LogErrAndExit(hr);
		

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sample->dsvHeap, &dsvHandle);
		ID3D12Device2_CreateDepthStencilView(sample->device, 
			sample->depthStencil, 
			&depthStencilDesc, 
			dsvHandle
		);
	}

	// Create the constant buffer.
	{
		const UINT64 constantBufferSize = sizeof(Constants) * FrameCount;

		const D3D12_HEAP_PROPERTIES constantBufferHeapProps = (D3D12_HEAP_PROPERTIES){
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1,
		};

		const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC_BUFFER(constantBufferSize, D3D12_RESOURCE_FLAG_NONE, 0);

		hr = ID3D12Device2_CreateCommittedResource(sample->device,
			&constantBufferHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&constantBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			&IID_ID3D12Resource,
			(void**)&sample->constantBuffer);
		if (FAILED(hr)) LogErrAndExit(hr);

		// Describe and create a constant buffer view.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
			.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(sample->constantBuffer),
			.SizeInBytes = constantBufferSize,
		};

		// Map and initialize the constant buffer. We don't unmap this until the
		// app closes. Keeping things mapped for the lifetime of the resource is okay.
		D3D12_RANGE readRange = { 0,0 }; // We do not intend to read from this resource on the CPU.
		hr = ID3D12Resource_Map(sample->constantBuffer, 0, &readRange, (void**) sample->constantData);
		if (FAILED(hr)) LogErrAndExit(hr);
	}
	RELEASE(factory);
}

static void LoadAssets(DXSample* const sample)
{
	// Create the pipeline state, which includes compiling and loading shaders.
	{
		HRESULT hr = ID3D12Device2_CreateCommandList(sample->device,
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			sample->commandAllocators[sample->frameIndex],
			sample->pipelineState,
			&IID_ID3D12GraphicsCommandList6,
			(void**)&sample->commandList);
		if (FAILED(hr)) LogErrAndExit(hr);

		// Command lists are created in the recording state, but there is nothing
		// to record yet. The main loop expects it to be closed, so close it now.
		hr = ID3D12GraphicsCommandList6_Close(sample->commandList);
		if (FAILED(hr)) LogErrAndExit(hr);

		struct
		{
			byte* data;
			uint32_t size;
		} ampShader, meshShader, pixelShader;

		/* Load pre-compiled shaders */
		LoadShaderData(sample->assetsPath, c_ampShaderFilename, &ampShader.data, &ampShader.size);
		LoadShaderData(sample->assetsPath, c_meshShaderFilename, &meshShader.data, &meshShader.size);
		LoadShaderData(sample->assetsPath, c_pixelShaderFilename, &pixelShader.data, &pixelShader.size);

		// Pull root signature from the precompiled mesh shader.
		hr = ID3D12Device2_CreateRootSignature(sample->device, 
			0, 
			meshShader.data, 
			meshShader.size, 
			&IID_ID3D12RootSignature,
			(void**)sample->rootSignature
		);
		if (FAILED(hr)) LogErrAndExit(hr);

		D3D12_RESOURCE_DESC rtvDesc;
		ID3D12Resource_GetDesc(sample->renderTargets[0], &rtvDesc);
		D3D12_RESOURCE_DESC depthStencilDesc;
		ID3D12Resource_GetDesc(sample->depthStencil, &depthStencilDesc);

		D3DX12_MESH_SHADER_PIPELINE_STATE_DESC  psoDesc = {
			.pRootSignature = sample->rootSignature,
			.AS = (D3D12_SHADER_BYTECODE){ 
				.pShaderBytecode = ampShader.data, 
				.BytecodeLength = ampShader.size 
			},
			.MS = (D3D12_SHADER_BYTECODE){ 
				.pShaderBytecode = meshShader.data, 
				.BytecodeLength = meshShader.size 
			},
			.PS = (D3D12_SHADER_BYTECODE){
				.pShaderBytecode = pixelShader.data,
				.BytecodeLength = pixelShader.size,
			},
			.NumRenderTargets = 1,
			.RTVFormats[0] = rtvDesc.Format,
			.DSVFormat = depthStencilDesc.Format,
			.RasterizerState = CD3DX12_DEFAULT_RASTERIZER_DESC(),      // CW front; cull back
			.BlendState = CD3DX12_DEFAULT_BLEND_DESC(),                // Opaque
			.DepthStencilState = CD3DX12_DEFAULT_DEPTH_STENCIL_DESC(), // Less-equal depth test w/ writes; no stencil
			.SampleMask = UINT_MAX,
			.SampleDesc = (DXGI_SAMPLE_DESC){.Count = 1, .Quality = 0},
		};
		
		CD3DX12_PIPELINE_MESH_STATE_STREAM meshStreamDesc = pipelineMeshStateStreamFromDesc(&psoDesc);
		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {
			.SizeInBytes = sizeof(meshStreamDesc),
			.pPipelineStateSubobjectStream = &meshStreamDesc,
		};

		hr = ID3D12Device2_CreatePipelineState(sample->device, &streamDesc, &IID_ID3D12PipelineState, (void**)&sample->pipelineState);
		if (FAILED(hr)) LogErrAndExit(hr);
	}

	for (uint32_t i = 0; i < LodsCount; ++i)
	{
		Model *lod = &sample->lods[i];
		// Load and upload model resources to the GPU
	    // Just use the D3D12_COMMAND_LIST_TYPE_DIRECT queue since it's a one-and-done operation. 
	    // For per-frame uploads consider using the D3D12_COMMAND_LIST_TYPE_COPY command queue.
		Model_LoadFromFile(lod, c_lodFilenames[i]);
		Model_UploadGpuResources(lod, sample->device, sample->commandQueue, sample->commandAllocators[sample->frameIndex], sample->commandList);
#ifdef _DEBUG
		// Mesh shader file expects a certain vertex layout; assert our mesh conforms to that layout.
		const D3D12_INPUT_ELEMENT_DESC c_elementDescs[2] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },

		};
		assert(lod->meshes[0].LayoutDesc.NumElements == 2);
		for (uint32_t i = 0; i < _countof(c_elementDescs); ++i)
			assert(memcmp(&lod->meshes[0].LayoutElems[i], &c_elementDescs[i], sizeof(D3D12_INPUT_ELEMENT_DESC)) == 0);
#endif
	}
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sample->srvHeap, &srvHandle);
	
	// Populate descriptor table with arrays of SRVs for each LOD
	for (uint32_t i = 0; i < LodsCount; ++i)
	{
		Mesh* mesh = &sample->lods[i].meshes[0];
		
		// Mesh Info Buffers
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
			.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(mesh->MeshInfoResource),
			.SizeInBytes = AlignU32(sizeof(MeshInfo)),
		};
		ID3D12Device2_CreateConstantBufferView(
			sample->device, 
			&cbvDesc, 
			OffsetDescHandle(srvHandle, SRV_MeshInfoLODs + i, sample->srvDescriptorSize)
		);
		
		// Populate common shader resource view desc with shared settings.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
			.Buffer.FirstElement = 0,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
		};


		// Vertices
		srvDesc.Buffer = (D3D12_BUFFER_SRV){
			.StructureByteStride = mesh->VertexStrides[0], // We assume we'll only use the first vertex buffer
			.NumElements = mesh->VertexCount,
		};
		ID3D12Device2_CreateShaderResourceView(
			sample->device,
			mesh->VertexResources[0],
			&srvDesc, 
			OffsetDescHandle(srvHandle, SRV_VertexLODs + i, sample->srvDescriptorSize)
		);

		// Meshlets
		srvDesc.Buffer.StructureByteStride = sizeof(Meshlet);
		srvDesc.Buffer.NumElements = mesh->Meshlets.count;
		ID3D12Device2_CreateShaderResourceView(
			sample->device, 
			mesh->MeshletResource, 
			&srvDesc, 
			OffsetDescHandle(srvHandle, SRV_MeshletLODs + i, sample->srvDescriptorSize)
		);


		// Primitive Indices
		srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
		srvDesc.Buffer.NumElements = mesh->IndexCount / 3;
		ID3D12Device2_CreateShaderResourceView(
			sample->device,
			mesh->PrimitiveIndexResource, 
			&srvDesc, 
			OffsetDescHandle(srvHandle, SRV_PrimitiveIndexLODs + i, sample->srvDescriptorSize)
		);

		// Unique Vertex Indices
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Buffer.StructureByteStride = 0;
		srvDesc.Buffer.NumElements = DivRoundUp_uint32(mesh->UniqueVertexIndices.count, 4);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		ID3D12Device2_CreateShaderResourceView(
			sample->device,
			mesh->UniqueVertexIndexResource,
			&srvDesc,
			OffsetDescHandle(srvHandle, SRV_UniqueVertexIndexLODs + i, sample->srvDescriptorSize)
		);
	}

	// Null-out remaining LOD slots in the descriptor table.
	for (uint32_t i = LodsCount; i < MAX_LOD_LEVELS; ++i)
	{
		ID3D12Device2_CreateConstantBufferView(sample->device, NULL, OffsetDescHandle(srvHandle, SRV_MeshInfoLODs + i, sample->srvDescriptorSize));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Buffer.StructureByteStride = 24,
		};
		ID3D12Device2_CreateShaderResourceView(sample->device, NULL, &srvDesc, OffsetDescHandle(srvHandle, SRV_VertexLODs + i, sample->srvDescriptorSize));

		srvDesc.Buffer.StructureByteStride = sizeof(Meshlet);
		ID3D12Device2_CreateShaderResourceView(sample->device, NULL, &srvDesc, OffsetDescHandle(srvHandle, SRV_MeshletLODs + i, sample->srvDescriptorSize));

		srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
		ID3D12Device2_CreateShaderResourceView(sample->device, NULL, &srvDesc, OffsetDescHandle(srvHandle, SRV_PrimitiveIndexLODs + i, sample->srvDescriptorSize));

		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Buffer.StructureByteStride = 0;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		ID3D12Device2_CreateShaderResourceView(sample->device, NULL, &srvDesc, OffsetDescHandle(srvHandle, SRV_UniqueVertexIndexLODs + i, sample->srvDescriptorSize));
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		HRESULT hr = ID3D12Device2_CreateFence(sample->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&sample->fence);
		if (FAILED(hr)) LogErrAndExit(hr);
		sample->fenceValues[sample->frameIndex]++;

		// Create an event handle to use for frame synchronization.
		sample->fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (sample->fenceEvent == NULL)
		{
			hr = HRESULT_FROM_WIN32(GetLastError());
			if (FAILED(hr)) LogErrAndExit(hr);
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu(sample);
	}
}

// Record all the commands we need to render the scene into the command list
static void PopulateCommandList(DXSample* const sample)
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress
	HRESULT hr = ID3D12CommandAllocator_Reset(sample->commandAllocators[sample->frameIndex]);
	if (FAILED(hr)) LogErrAndExit(hr);

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording
	hr = ID3D12GraphicsCommandList_Reset(sample->commandList, sample->commandAllocators[sample->frameIndex], sample->pipelineState);
	if (FAILED(hr)) LogErrAndExit(hr);

	// Only upload instance data if we've had a change
	if (sample->updateInstances)
	{
		const D3D12_RESOURCE_BARRIER toCopyBarrier = CD3DX12_Transition(sample->instanceBuffer,
			D3D12_RESOURCE_STATE_GENERIC_READ, 
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_NONE);
		ID3D12GraphicsCommandList_ResourceBarrier(sample->commandList, 1, &toCopyBarrier);
		ID3D12GraphicsCommandList_CopyResource(sample->commandList, sample->instanceBuffer, sample->instanceUpload);
		
		const D3D12_RESOURCE_BARRIER toGenericBarrier = CD3DX12_Transition(sample->instanceBuffer,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_NONE);
		ID3D12GraphicsCommandList_ResourceBarrier(sample->commandList, 1, &toGenericBarrier);
		sample->updateInstances = false;
	}

	// Set necessary state
	ID3D12GraphicsCommandList_SetGraphicsRootSignature(sample->commandList, sample->rootSignature);
	ID3D12DescriptorHeap* heaps[] = { sample->srvHeap };
	ID3D12GraphicsCommandList_SetDescriptorHeaps(sample->commandList, 1, heaps);
	ID3D12GraphicsCommandList_RSSetViewports(sample->commandList, 1, &sample->viewport);
	ID3D12GraphicsCommandList_RSSetScissorRects(sample->commandList, 1, &sample->scissorRect);

	// Indicate that the back buffer will be used as a render target
	const D3D12_RESOURCE_BARRIER toRenderTargetBarrier = CD3DX12_Transition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_NONE);
	ID3D12GraphicsCommandList_ResourceBarrier(sample->commandList, 1, &toRenderTargetBarrier);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sample->rtvHeap, &rtvHandle);
	const INT64 CurrentRtvOffset = sample->frameIndex * sample->rtvDescriptorSize;
	rtvHandle.ptr = (SIZE_T)((INT64)(rtvHandle.ptr) + CurrentRtvOffset);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sample->dsvHeap, &rtvHandle);
	ID3D12GraphicsCommandList_OMSetRenderTargets(sample->commandList, 1, &rtvHandle, FALSE, &dsvHandle);

	// Record commands
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	ID3D12GraphicsCommandList_ClearRenderTargetView(sample->commandList, rtvHandle, clearColor, 0, NULL);
	ID3D12GraphicsCommandList_ClearDepthStencilView(sample->commandList, dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
	ID3D12Resource_GetGPUVirtualAddress(sample->constantBuffer);
	ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(
		sample->commandList, 
		0, 
		ID3D12Resource_GetGPUVirtualAddress(sample->constantBuffer) + sizeof(Constants) * sample->frameIndex
	);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHeapGPUDescHandle;
	ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sample->srvHeap, &srvHeapGPUDescHandle);
	ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(sample->commandList, 2, srvHeapGPUDescHandle);
	D3D12_GPU_VIRTUAL_ADDRESS instanceBufferGPUVirtualAddress = ID3D12Resource_GetGPUVirtualAddress(sample->instanceBuffer);
	ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(
		sample->commandList, 
		3, 
		ID3D12Resource_GetGPUVirtualAddress(sample->instanceBuffer)
	);

	uint32_t dispatchCount = DivRoundUp_uint32(sample->instanceCount, c_maxGroupDispatchCount);

	for (uint32_t i = 0; i < dispatchCount; ++i)
	{
		uint32_t offset = dispatchCount * i;
		uint32_t count = min(sample->instanceCount - offset, c_maxGroupDispatchCount);
		ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(sample->commandList, 1, offset, 0);
		ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(sample->commandList, 1, count, 1);
		ID3D12GraphicsCommandList6_DispatchMesh(sample->commandList, count, 1, 1);
	}

	D3D12_RESOURCE_BARRIER toPresentBarrier = CD3DX12_Transition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_NONE);

	// Indicate that the back buffer will now be used to present
	ID3D12GraphicsCommandList_ResourceBarrier(sample->commandList, 1, &toPresentBarrier);
	hr = ID3D12GraphicsCommandList_Close(sample->commandList);
	if (FAILED(hr)) LogErrAndExit(hr);
}

static void LoadShaderData(const WCHAR* const base, const WCHAR* const shaderRelativePath, UINT8** shaderData, UINT* shaderDataLen) {
	size_t bufferSize = wcslen(base) + wcslen(shaderRelativePath) + 1;
	WCHAR* shaderDataPath = HeapAlloc(GetProcessHeap(), 0, bufferSize * sizeof(WCHAR));
	swprintf(shaderDataPath, bufferSize, L"%s%s", base, shaderRelativePath);
	HRESULT hr = ReadDataFromFile(shaderDataPath, shaderData, shaderDataLen);
	if (FAILED(hr)) LogErrAndExit(hr);
	HeapFree(GetProcessHeap(), 0, shaderDataPath);
}

#define OFFSET_HANDLE(i) (D3D12_CPU_DESCRIPTOR_HANDLE){ .ptr = srvHandle.ptr + ((SIZE_T)i) * srvDescriptorSize}


D3D12_CPU_DESCRIPTOR_HANDLE OffsetDescHandle(D3D12_CPU_DESCRIPTOR_HANDLE original, uint32_t index, uint32_t descSize) {
	return (D3D12_CPU_DESCRIPTOR_HANDLE){ 
		.ptr = original.ptr + ((SIZE_T)index) * descSize 
	};
};

// aligns a given UINT32 value to the D3D12 constant buffer alignment requirement,
// rounding size up to the next multiple of alignment
UINT32 AlignU32(UINT32 size) {
	const UINT32 alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	const UINT32 alignedSize = (size + alignment - 1) & ~(alignment - 1);
	return alignedSize;
}

// aligns a given UINT64 value to the D3D12 constant buffer alignment requirement,
// rounding size up to the next multiple of alignment
UINT64  AlignU64(UINT64 size) {
	const UINT64 alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	const UINT64 alignedSize = (size + alignment - 1) & ~(alignment - 1);
	return alignedSize;
}

// Wait for pending GPU work to complete.
static void WaitForGpu(DXSample *sample)
{
	// Schedule a Signal command in the queue.
	HRESULT hr = ID3D12CommandQueue_Signal(sample->commandQueue, sample->fence, sample->fenceValues[sample->frameIndex]);
	if (FAILED(hr)) LogErrAndExit(hr);

	// Wait until the fence has been processed.
	hr = ID3D12Fence_SetEventOnCompletion(sample->fence, sample->fenceValues[sample->frameIndex], sample->fenceEvent);
	if (FAILED(hr)) LogErrAndExit(hr);

	WaitForSingleObjectEx(sample->fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	sample->fenceValues[sample->frameIndex]++;
}

// Prepare to render the next frame.
void MoveToNextFrame(DXSample* sample)
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = sample->fenceValues[sample->frameIndex];
	HRESULT hr = ID3D12CommandQueue_Signal(sample->commandQueue, sample->fence, currentFenceValue);
	if (FAILED(hr)) LogErrAndExit(hr);

	// Update the frame index.
	sample->frameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(sample->swapChain);

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (ID3D12Fence_GetCompletedValue(sample->fence) < sample->fenceValues[sample->frameIndex])
	{
		hr = ID3D12Fence_SetEventOnCompletion(sample->fence, sample->fenceValues[sample->frameIndex], sample->fenceEvent);
		if (FAILED(hr)) LogErrAndExit(hr);
		WaitForSingleObjectEx(sample->fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	sample->fenceValues[sample->frameIndex] = currentFenceValue + 1;
}

void RegenerateInstances(DXSample* sample)
{
	sample->updateInstances = true;

	const float radius = sample->lods[0].boundingSphere.r;
	const float padding = 0.5f;
	const float spacing = (1.0f + padding) * radius;

	// Create the instances in a growing cube volume
	const uint32_t width = sample->instanceLevel * 2 + 1;
	const float extents = spacing * sample->instanceLevel;

	sample->instanceCount = width * width * width;

	const UINT64 instanceBufferSize = AlignU64(sample->instanceCount * sizeof(Instance));

	// Only recreate instance-sized buffers if necessary.
	D3D12_RESOURCE_DESC resourceDesc;
	ID3D12Resource_GetDesc(sample->instanceBuffer, &resourceDesc);
	if (!sample->instanceBuffer || resourceDesc.Width < instanceBufferSize)
	{
		WaitForGpu(sample);
		const D3D12_HEAP_PROPERTIES instanceBufferDefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		const D3D12_RESOURCE_DESC instanceBufferDesc = CD3DX12_RESOURCE_DESC_BUFFER(instanceBufferSize, D3D12_RESOURCE_FLAG_NONE, 0);

		// Create/re-create the instance buffer
		HRESULT hr = ID3D12Device2_CreateCommittedResource(sample->device,
			&instanceBufferDefaultHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&instanceBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			&IID_ID3D12Resource,
			(void**)(&sample->instanceBuffer)
		);
		if(FAILED(hr)) LogErrAndExit(hr);

		const D3D12_HEAP_PROPERTIES instanceBufferUploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		// Create/re-create the instance buffer
		hr = ID3D12Device2_CreateCommittedResource(sample->device,
			&instanceBufferUploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&instanceBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			&IID_ID3D12Resource,
			(void**)(&sample->instanceUpload)
		);
		if (FAILED(hr)) LogErrAndExit(hr);
		
		ID3D12Resource_Map(sample->instanceUpload, 0, NULL, (void**)&sample->instanceData);
	}

	// Regenerate the instances in our scene.
	for (uint32_t i = 0; i < sample->instanceCount; ++i)
	{
		XMVECTOR index = XMVectorSet((float)(i % width), (float)((i / width) % width), (float)(i / (width * width)), 0);
		XMVECTOR indexScaled = XM_VEC_SCALE(index, spacing);
		XMVECTOR extentsVec = XMVectorReplicate(extents);
		XMVECTOR location = XM_VEC_SUBTRACT(indexScaled, extentsVec);
		location = XM_VEC_SETW(location, radius);

		XMMATRIX world = XM_MAT_TRANSLATION_FROM_VEC(location);

		Instance *inst = &sample->instanceData[i];
		world = XM_MAT_TRANSP(world);
		XM_STORE_FLOAT4X4(&inst->World, world);

		world = XM_MAT_INV(NULL, world);
		world = XM_MAT_TRANSP(world);
		XM_STORE_FLOAT4X4(&inst->WorldInvTranspose, world);
		XM_STORE_FLOAT4(&inst->BoundingSphere, location);
	}
}


void ReleaseAll(DXSample* const sample)
{
	RELEASE(sample->swapChain);
	RELEASE(sample->device);
	for (int i = 0; i < FrameCount; ++i) {
		RELEASE(sample->renderTargets[i]);
		RELEASE(sample->commandAllocators[i]);
	}
	for (int i = 0; i < LodsCount; ++i) {
		for (int j = 0; i < sample->lods[i].nMeshes; ++j) {
			ReleaseMesh(&sample->lods[i].meshes[j]);
		}
	}
	RELEASE(sample->commandQueue);
	RELEASE(sample->rootSignature);
	RELEASE(sample->rtvHeap);
	RELEASE(sample->dsvHeap);
	RELEASE(sample->srvHeap);
	RELEASE(sample->pipelineState);
	RELEASE(sample->commandList);
	RELEASE(sample->fence);
	RELEASE(sample->constantBuffer);
	RELEASE(sample->instanceBuffer);
	RELEASE(sample->instanceUpload);
#if defined(_DEBUG)
	IDXGIDebug1* debugDev = NULL;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, &IID_IDXGIDebug1, (void**)&debugDev)))
	{
		IDXGIDebug_ReportLiveObjects(debugDev, DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
	}
#endif
}