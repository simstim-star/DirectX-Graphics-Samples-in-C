#pragma once

#include <windows.h>
#include <d3d12.h>

#define SampleAssets_STANDARD_VERTEX_DESC_NUM_ELEMENTS 4
#define SampleAssets_STANDARD_VERTEX_STRIDE 44U
#define SampleAssets_STANDARD_INDEX_FORMAT DXGI_FORMAT_R32_UINT
#define SampleAssets_VERTEX_DATA_OFFSET 524288U
#define SampleAssets_VERTEX_DATA_SIZE 820248U
#define SampleAssets_INDEX_DATA_OFFSET 1344536U
#define SampleAssets_INDEX_DATA_SIZE 74568U
#define SampleAssets_DATA_FILE_NAME L"occcity.bin"

typedef struct SampleAssets_TextureResource
{
	UINT Width;
	UINT Height;
	UINT16 MipLevels;
	DXGI_FORMAT Format;
	struct DataProperties
	{
		UINT Offset;
		UINT Size;
		UINT Pitch;
	} Data[D3D12_REQ_MIP_LEVELS];
} SampleAssets_TextureResource;

typedef struct SampleAssets_DrawParameters
{
	INT DiffuseTextureIndex;
	INT NormalTextureIndex;
	INT SpecularTextureIndex;
	UINT IndexStart;
	UINT IndexCount;
	UINT VertexBase;
} SampleAssets_DrawParameters;

extern const D3D12_INPUT_ELEMENT_DESC SampleAssets_StandardVertexDescription[SampleAssets_STANDARD_VERTEX_DESC_NUM_ELEMENTS];
extern SampleAssets_TextureResource SampleAssets_Textures[1];
