#pragma once

#include "d3d12.h"
#include <DirectXMathC.h>
#include "span.h"
#include <DirectXCollisionC.h>

/*****************************************************************************************************************************
 >> Constants 
******************************************************************************************************************************/

enum Attribute_Type
{
    Attribute_Position,
    Attribute_Normal,
    Attribute_TexCoord,
    Attribute_Tangent,
    Attribute_Bitangent,
    Attribute_Count
};

/*****************************************************************************************************************************
 >> Mesh related types forward definitions
******************************************************************************************************************************/

// Meshlet describes a portion of mesh, a chunk to use efficiently work group/shared storage sizes.
typedef struct Meshlet
{
    uint32_t VertCount;
    uint32_t VertOffset;
    uint32_t PrimCount;
    uint32_t PrimOffset;
} Meshlet;

typedef struct __declspec(align(256)) MeshInfo
{
    uint32_t IndexSize;
    uint32_t MeshletCount;

    uint32_t LastMeshletVertCount;
    uint32_t LastMeshletPrimCount;
} MeshInfo;

// a simple pair of <Offset, Count>, representing a continuous chunk of something after an offset
// eg. { Offset = 0, Count = 300 } may represent the first 300 indices
typedef struct Subset
{
    uint32_t Offset;
    uint32_t Count;
} Subset;

typedef struct PackedTriangle
{
    uint32_t i0 : 10;
    uint32_t i1 : 10;
    uint32_t i2 : 10;
} PackedTriangle;

typedef struct CullData
{
    XMFLOAT4          BoundingSphere; // xyz = center, w = radius
    uint8_t           NormalCone[4];  // xyz = axis, w = -cos(a + 90)
    float             ApexOffset;     // apex = center - axis * offset
} CullData;


SPAN_DEFINE(uint8_t);
SPAN_DEFINE(Subset);
SPAN_DEFINE(MeshInfo);
SPAN_DEFINE(Meshlet);
SPAN_DEFINE(PackedTriangle);
SPAN_DEFINE(CullData);


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *                       ~~ The Mesh ~~                                *
 *                                                                     *
 * It has no actual data, only spans that indicate where the data is.  *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


typedef struct Mesh
{
    /********************************************************************************************************************
    *                                       Layout for IA                                                               *
    *                                                                                                                   *
    * Here we describe what will arrive for IA, like the name of the attribute ("POSITION", "NORMAL", "TANGENT", etc),  *
    * the format (DXGI_FORMAT_R32G32B32_FLOAT and the likes), alignment and etc.                                        *
    * The LayoutDesc is merely an array of D3D12_INPUT_ELEMENT_DESC. We keep then separated here for convenience when   *
    * manipulating it.                                                                                                  *
    *********************************************************************************************************************/

    D3D12_INPUT_ELEMENT_DESC   LayoutElems[Attribute_Count];
    D3D12_INPUT_LAYOUT_DESC    LayoutDesc;


    /*******************************************************************************************************************************************************************************
    *                                       Vertex Data                                                                                                                            *
    *                                                                                                                                                                              *
    * Vertices are a bunch of raw bytes (uint8) packed continuously that we access through a span (interleaved data) or many spans (non-interleaved).                              *
    * We will have a blob of vertex data which we will take a subset to be our vertices in the mesh:                                                                               *
    * Model Buffer [...,x_n, y_n, z_n, r_n, g_n, b_n, a_n, ..., x_n+1, y_n+1, z_n+1, r_n+1, g_n+1, b_n+1, a_n+1, ...] (interleaved)                                                *
    *                   ^                                                                                                                                                          *
    *                  Span                                                                                                                                                        *
    *                                                                                                                                                                              *
    * Strides = { 4 * 7 } (if we only have position and color as attributes, 4 bytes * 7)                                                                                          *
    *                                                                                                                                                                              *
    * Model Buffer [...,x_n, y_n, z_n, x_n+1, y_n+1, z_n+1, ..., r_n, g_n, b_n, a_n, r_n+1, g_n+1, b_n+1, a_n+1, ...] (non-interleaved)                                            *
    *                   ^                                         ^                                                                                                                *
    *                  Span                                      Span                                                                                                              *  
    *                                                                                                                                                                              *
    * Strides = {4 * 3, 4 * 4}  (if we only have position and color)                                                                                                               *
    *                                                                                                                                                                              *
    * The important part is to set a D3D12_VERTEX_BUFFER_VIEW correctly for DirectX12 to know how to deal with the data.                                                           *
    * For example, for interleaved, you would need a single D3D12_VERTEX_BUFFER_VIEW with BufferLocation set to the start of the span and the SizeInBytes to the size of the span. *
    * For non-interleaved, you would need a D3D12_VERTEX_BUFFER_VIEW for each attribute.                                                                                           *
    ********************************************************************************************************************************************************************************/

    // Note: the max number of spans for a model is Attribute_Count considering the above. 

    Span_uint8_t              VerticesSpans[Attribute_Count];   // spans into the model buffer, where a span is a ptr to the start + num bytes in the span
    
    uint32_t                  VertexStrides[Attribute_Count];  // the stride is necessary to know how many bytes we need "to walk" to reach the next vertex

    int                       numVerticesSpans;
    
    uint32_t                  VertexCount;    // this is the total number of vertices in the whole model, that is why it is a single value. 
                                               // in D3D12_VERTEX_BUFFER_VIEW, the SizeInBytes is calculated as VertexCount * VertexStride (if interleaved)


    // TODO create your own boundingsphere
    XMBoundingSphere    BoundingSphere;

    /********************************************************************************************************************
    *                                               Indices                                                             *
    *                                                                                                                   *
    * The indices to reuse vertices. They are also disposed in memory in a very similar way to the vertices spans,      *
    * with spans looking into a blob of indices that we can use for faster access.                                      *
    *                                                                                                                   *
    *********************************************************************************************************************/

    Span_uint8_t               Indices; 
    Span_Subset                IndexSubsets; 
    uint32_t                   IndexSize;
    uint32_t                   IndexCount;

    /*********************************************************************************************************************
    *                                               Meshlets                                                             *
    *                                                                                                                    *
    * Groupings of subsets of data of our mesh, in order to make operations in chunks of data of the mesh instead of the *
    * whole thing.                                                                                                       *
    **********************************************************************************************************************/

    Span_Subset               MeshletSubsets;
    Span_Meshlet              Meshlets;
    Span_uint8_t              UniqueVertexIndices;
    Span_PackedTriangle       PrimitiveIndices;
    Span_CullData             CullingData;

    /***************************
    *  D3D resource references *
    ****************************/

    D3D12_VERTEX_BUFFER_VIEW     VBViews[Attribute_Count];
    D3D12_INDEX_BUFFER_VIEW      IBView;

    ID3D12Resource*              VertexResources[Attribute_Count];
    ID3D12Resource*              IndexResource;
    ID3D12Resource*              MeshletResource;
    ID3D12Resource*              UniqueVertexIndexResource;
    ID3D12Resource*              PrimitiveIndexResource;
    ID3D12Resource*              CullDataResource;
    ID3D12Resource*              MeshInfoResource;
} Mesh;

void Mesh_Release(Mesh* m);


/*************************************************************************************************************
 >> Mesh Functions Definitions
*************************************************************************************************************/

/****************************************************************************************************************************
 * Calculates the number of instances of the last meshlet which can be packed into a single threadgroup.                    *
 * It is important to consider the last meshlet because it is possibly smaller, so we can perform optimizations             *
 * to group these irregular meshlets into packs and perform operations on them together when we can group enough of them.   *
 ***************************************************************************************************************************/
uint32_t Mesh_GetLastMeshletPackCount (Span_Meshlet meshlets, Span_Subset mehsletSubsets, uint32_t subsetIndex, uint32_t maxGroupVerts, uint32_t maxGroupPrims);


/**********************************************************************************
* Extracts references to the vertex indices of the triangle at index in the span. *
***********************************************************************************/
void     Mesh_GetPrimitive            (Span_PackedTriangle PrimitiveIndices, uint32_t index, uint32_t* i0, uint32_t* i1, uint32_t* i2);


uint32_t Mesh_GetVertexIndex          (Span_uint8_t UniqueVertexIndices, uint32_t index, uint32_t indexSize);



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                             ~~ The Model ~~                                 *
 *                                                                             *
 * Meshes + bounding sphere + buffer for the raw data the spans in the Meshes  *
 * will access.                                                                *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

typedef struct Model {
    Mesh* meshes;
    int nMeshes;
    XMBoundingSphere boundingSphere;
    uint8_t* buffer;
} Model;


/*****************************************************************************************************************************
 * Model_LoadFromFile function loads a 3D model from a binary file specified by `filename`.                                  *
 *                                                                                                                           *
 * The file format starts with the `FileHeader`, which contains:                                                             *
 *   - Prolog: A magic number used to identify the file as a valid model file.                                               *
 *   - Version: The version of the file format.                                                                              *
 *   - MeshCount: The number of meshes in the model.                                                                         *
 *   - AccessorCount: The number of accessors describing how to read vertex data.                                            *
 *   - BufferViewCount: The number of buffer views that describe the memory layout of model data.                            *
 *   - BufferSize: The size of the entire model data buffer, including all mesh, index, vertex, and other attributes.        *
 *                                                                                                                           *
 * Following the header, the file contains an array of `MeshHeader` structures, each of which describes a mesh's attributes: *
 *   - Indices: The index accessor for the mesh.                                                                             *
 *   - IndexSubsets: The index subsets accessor for the mesh.                                                                *
 *   - Attributes: An array of accessors for vertex attributes (such as position, color, etc.).                              *
 *   - Meshlets: The meshlets accessor for the mesh.                                                                         *
 *   - MeshletSubsets: The meshlet subsets accessor for the mesh.                                                            *
 *   - UniqueVertexIndices: The accessor describing unique vertex indices for the mesh.                                      *
 *   - PrimitiveIndices: The primitive indices accessor for the mesh.                                                        *
 *   - CullData: The culling data accessor for the mesh.                                                                     *
 *                                                                                                                           *
 * The accessors describe how to access data in the buffer through `BufferView` structures, which specify the offset,        *
 * size, and stride for the data, enabling efficient reading of model data such as vertices, indices, and other attributes.  *
 *                                                                                                                           *
 * The function reads and validates the file, ensuring it matches the expected format and version. It then allocates memory  *
 * for the model data and reads the mesh, accessor, and buffer view information into the model's internal structures.        *
 * The model’s mesh data is parsed, and bounding spheres for each mesh are calculated.                                       *
 *****************************************************************************************************************************/
HRESULT Model_LoadFromFile(Model* const m, const wchar_t* const basepath, const wchar_t* const assetpath);

HRESULT Model_UploadGpuResources(Model *model, ID3D12Device2* device, ID3D12CommandQueue* cmdQueue, ID3D12CommandAllocator* cmdAlloc, ID3D12GraphicsCommandList6* cmdList);