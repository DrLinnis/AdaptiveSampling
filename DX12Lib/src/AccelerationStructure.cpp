#include "DX12LibPCH.h"

#include "dx12lib/AccelerationStructure.h"

#include "dx12lib/VertexBuffer.h"
#include "dx12lib/IndexBuffer.h"
#include "dx12lib/Device.h"
#include "dx12lib/Buffer.h"
#include "dx12lib/CommandList.h"
#include "dx12lib/MappableBuffer.h"

using namespace dx12lib;


std::shared_ptr<AccelerationStructure> AccelerationStructure::CreateBottomLevelAS( Device*       pDevice,
                                                                                   CommandList*  pCommandList,
                                                                                   VertexBuffer* pVertexBuffer,
                                                                                   IndexBuffer*  pIndexBuffer )
{

    // Get prebuild infos
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type                           = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    // NOTE: As a vertex can contain more than just the XYZ position it thus has stride defined seperatly.
    /**/
    geomDesc.Triangles.VertexBuffer.StartAddress  = pVertexBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
    geomDesc.Triangles.VertexBuffer.StrideInBytes = pVertexBuffer->GetVertexStride();
    geomDesc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;  // XYZ per vertex.
    geomDesc.Triangles.VertexCount                = pVertexBuffer->GetNumVertices();

    geomDesc.Triangles.IndexBuffer = pIndexBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
    geomDesc.Triangles.IndexCount  = pIndexBuffer->GetNumIndicies();
    geomDesc.Triangles.IndexFormat = pIndexBuffer->GetIndexFormat();

    geomDesc.Triangles.Transform3x4 = 0;

    // number of bytes in buffer divided by bytes of a vertex
    geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // Get the size requirements for the scratch and AS buffers
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout                                          = D3D12_ELEMENTS_LAYOUT_ARRAY;
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
    structure->pScratch->SetName( L"DXR BLAS Scratch" );

    structure->pResult =
        pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE );
    structure->pResult->SetName( L"DXR BLAS" );

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.DestAccelerationStructureData    = structure->pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = structure->pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    pCommandList->UAVBarrier( structure->pResult );

    return structure;
}

std::shared_ptr<AccelerationStructure>
    AccelerationStructure::CreateTopLevelAS( Device* pDevice, CommandList* pCommandList, AccelerationBuffer* pBottomLevelAS, uint64_t* pTlasSize )
{
    // create target structure
    std::shared_ptr<AccelerationStructure> structure = std::make_shared<AccelerationStructure>();

    structure->pInstanceDesc = pDevice->CreateMappableBuffer( sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
    structure->pInstanceDesc->SetName( L"DXR TLAS Instance Description" );

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};

    D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
    ThrowIfFailed( structure->pInstanceDesc->Map( (void**)&pInstanceDesc ) );
    {  // Initialize the instance desc. We only have a single instance

        instanceDesc.InstanceID = 0;  // This value will be exposed to the shader via InstanceID()
        instanceDesc.InstanceContributionToHitGroupIndex =
            0;  // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
        instanceDesc.Flags           = D3D12_RAYTRACING_INSTANCE_FLAG_NONE; 
            // OR D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE
        instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
        instanceDesc.AccelerationStructure = pBottomLevelAS->GetD3D12Resource()->GetGPUVirtualAddress();
        instanceDesc.InstanceMask          = 0xFF;

        memcpy( pInstanceDesc, &instanceDesc, sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
    }
    // Unmap
    structure->pInstanceDesc->Unmap();
    
    
    
    // First, get the size of the TLAS buffers and create them
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout  = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags    = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 1;
    inputs.Type     = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.InstanceDescs = structure->pInstanceDesc->GetD3D12Resource()->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &info );

    *pTlasSize = info.ResultDataMaxSizeInBytes;


    structure->pScratch =
        pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    structure->pScratch->SetName( L"DXR TLAS Scratch" );


    structure->pResult =
        pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE );
    structure->pResult->SetName( L"DXR TLAS" );

    

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.DestAccelerationStructureData    = structure->pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = structure->pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    pCommandList->UAVBarrier( structure->pResult );

    return structure;
}
