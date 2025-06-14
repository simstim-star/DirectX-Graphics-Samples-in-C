#pragma once
#include "DirectXMathC.h"

#define MAX(x, y) (x > y ? x : y)
#define ROUNDUP(x, y) ((x + y - 1) & ~(y - 1))

// In C, Alignas is applied to objects only, not to types
#define ALIGNED_CONSTANTS _Alignas(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) Constants

#define MAX_VERTS 64
#define MAX_PRIMS 126
#define MAX_LOD_LEVELS 8

#define THREADS_PER_WAVE 32 // Assumes availability of wave size of 32 threads

// Pre-defined threadgroup sizes for AS & MS stages
#define AS_GROUP_SIZE THREADS_PER_WAVE
#define MS_GROUP_SIZE ROUNDUP(MAX(MAX_VERTS, MAX_PRIMS), THREADS_PER_WAVE)


typedef struct Constants
{
    XMFLOAT4X4 View;
    XMFLOAT4X4 ViewProj;

    XMFLOAT4 Planes[6];
    XMFLOAT3 ViewPosition;
    float RecipTanHalfFovy;

    uint32_t RenderMode;
    uint32_t LODCount;
} Constants;

typedef struct DrawParams
{
    uint32_t InstanceOffset;
    uint32_t InstanceCount;
} DrawParams;

typedef struct Instance
{
    XMFLOAT4X4 World;
    XMFLOAT4X4 WorldInvTranspose;
    XMFLOAT4   BoundingSphere;
} Instance;