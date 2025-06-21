#pragma once

/*
* This will be used by both the shaders and the regular code, so we need to add flags for DXC
* not receive invalid HLSL code.
*/

#define MAX(x, y) (x > y ? x : y)
#define ROUNDUP(x, y) ((x + y - 1) & ~(y - 1))

#define MAX_VERTS 64
#define MAX_PRIMS 126
#define MAX_LOD_LEVELS 8

#define THREADS_PER_WAVE 32 // Assumes availability of wave size of 32 threads

// Pre-defined threadgroup sizes for AS & MS stages
#define AS_GROUP_SIZE THREADS_PER_WAVE
#define MS_GROUP_SIZE ROUNDUP(MAX(MAX_VERTS, MAX_PRIMS), THREADS_PER_WAVE)

#ifndef __HLSL__
#include <DirectXMathC.h>
typedef XMFLOAT4X4 float4x4;
typedef XMFLOAT4 float4;
typedef XMFLOAT3 float3;
typedef XMFLOAT2 float2;
typedef uint32_t uint;
#endif


#ifndef __HLSL__ 
#define CBUFFER_ALIGN __declspec(align(256))
#else
#define CBUFFER_ALIGN 
#endif

struct CBUFFER_ALIGN Constants
{
    float4x4 View;
    float4x4 ViewProj;

    float4 Planes[6];
    float3 ViewPosition;
    float RecipTanHalfFovy;

    uint RenderMode;
    uint LODCount;
};

struct DrawParams
{
    uint InstanceOffset;
    uint InstanceCount;
};

struct Instance
{
    float4x4 World;
    float4x4 WorldInvTranspose;
    float4   BoundingSphere;
};