#include "occcity.h"

const D3D12_INPUT_ELEMENT_DESC SampleAssets_StandardVertexDescription[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

SampleAssets_TextureResource SampleAssets_Textures[] =
{
	{ .Width = 1024, 
	  .Height = 1024, 
	  .MipLevels = 1, 
	  .Format = DXGI_FORMAT_BC1_UNORM, 
	  .Data = { { .Offset = 0, .Size = 524288, .Pitch = 2048 }, } }, // city.dds
};