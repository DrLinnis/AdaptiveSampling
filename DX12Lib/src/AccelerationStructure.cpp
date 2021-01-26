#include "DX12LibPCH.h"

#include "dx12lib/AccelerationStructure.h"

#include "dx12lib/VertexBuffer.h"
#include "dx12lib/IndexBuffer.h"
#include "dx12lib/Device.h"
#include "dx12lib/Buffer.h"
#include "dx12lib/CommandList.h"
#include "dx12lib/MappableBuffer.h"

using namespace dx12lib;

std::shared_ptr<AccelerationStructure>
    AccelerationStructure::CreateTopLevelAS( Device* pDevice, CommandList* pCommandList, AccelerationBuffer* pBottomLevelAS, uint64_t* pTlasSize )
{
    // First, get the size of the TLAS buffers and create them
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout  = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags    = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 1;
    inputs.Type     = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &info );

    *pTlasSize = info.ResultDataMaxSizeInBytes;

    AccelerationStructure                  tmp;
    std::shared_ptr<AccelerationStructure> structure = std::make_shared<AccelerationStructure>(&tmp);

    structure->pScratch =
        pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    structure->pResult =
        pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE );

    structure->pInstanceDesc = pDevice->CreateMappableBuffer( sizeof( D3D12_RAYTRACING_INSTANCE_DESC ));

    float m[] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };  // Identity matrix for 3x4

    D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
    ThrowIfFailed( structure->pInstanceDesc->Map( (void**)&pInstanceDesc ) );
    { // Initialize the instance desc. We only have a single instance
        
        pInstanceDesc->InstanceID = 0;  // This value will be exposed to the shader via InstanceID()
        pInstanceDesc->InstanceContributionToHitGroupIndex =
            0;  // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
        pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        memcpy( pInstanceDesc->Transform, &m, sizeof( pInstanceDesc->Transform ) );
        pInstanceDesc->AccelerationStructure = pBottomLevelAS->GetD3D12Resource()->GetGPUVirtualAddress();
        pInstanceDesc->InstanceMask          = 0xFF;
    }
    // Unmap
    structure->pInstanceDesc->Unmap();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.Inputs.InstanceDescs             = structure->pInstanceDesc->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData    = structure->pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = structure->pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    return structure;
}

std::shared_ptr<AccelerationStructure>
AccelerationStructure::CreateBottomLevelAS(Device* pDevice, CommandList* pCommandList,
    VertexBuffer* pVertexBuffer,
    IndexBuffer* pIndexBuffer)
{
    // get location, stride, and size of buffer.
    D3D12_VERTEX_BUFFER_VIEW vsBufferView  = pVertexBuffer->GetVertexBufferView();
    D3D12_INDEX_BUFFER_VIEW  idxBufferView = pIndexBuffer->GetIndexBufferView();

    // Get prebuild infos
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type                           = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    // NOTE: As a vertex can contain more than just the XYZ position it thus has stride defined seperatly.
    geomDesc.Triangles.VertexBuffer.StartAddress  = vsBufferView.BufferLocation;
    geomDesc.Triangles.VertexBuffer.StrideInBytes = vsBufferView.StrideInBytes;
    geomDesc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;  // XYZ per vertex.
    geomDesc.Triangles.VertexCount                = vsBufferView.SizeInBytes / vsBufferView.StrideInBytes;

    geomDesc.Triangles.IndexBuffer = idxBufferView.BufferLocation;
    geomDesc.Triangles.IndexCount  = idxBufferView.SizeInBytes / idxBufferView.Format;
    geomDesc.Triangles.IndexFormat = idxBufferView.Format;

    // number of bytes in buffer divided by bytes of a vertex
    geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // Get the size requirements for the scratch and AS buffers
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY; 
    inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs       = 1;
    inputs.pGeometryDescs = &geomDesc;
    inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &info );

    std::shared_ptr<AccelerationStructure> structure = std::make_shared<AccelerationStructure>();

    structure->pScratch =
        pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

    structure->pResult =
        pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE );

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.DestAccelerationStructureData    = structure->pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = structure->pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    return structure;
}