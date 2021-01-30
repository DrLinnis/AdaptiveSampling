#include "DX12LibPCH.h"

#include "dx12lib/AccelerationStructure.h"

#include "dx12lib/VertexBuffer.h"
#include "dx12lib/IndexBuffer.h"
#include "dx12lib/Device.h"
#include "dx12lib/Buffer.h"
#include "dx12lib/CommandList.h"
#include "dx12lib/MappableBuffer.h"

using namespace dx12lib;


void AccelerationBuffer::CreateBottomLevelAS(   Device* pDevice,
                                                CommandList* pCommandList,
                                                VertexBuffer* pVertexBuffer,
                                                IndexBuffer* pIndexBuffer,
                                                AccelerationStructure* pDes)
{
    // Get prebuild infos
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type                           = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    // NOTE: As a vertex can contain more than just the XYZ position it thus has stride defined seperatly.

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

    std::shared_ptr<AccelerationBuffer> pScratch =
        pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    pScratch->SetName( L"DXR BLAS Scratch" );
    pCommandList->UAVBarrier( pScratch );

    std::shared_ptr<AccelerationBuffer> pResult =
        pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE );
    pResult->SetName( L"DXR BLAS" );
    pCommandList->UAVBarrier( pResult );

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.DestAccelerationStructureData    = pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    pCommandList->UAVBarrier( pResult, true );

    pDes->pResult = pResult;
    pDes->pScratch = pScratch;

}

void AccelerationBuffer::CreateTopLevelAS(Device* pDevice, CommandList* pCommandList,
    AccelerationBuffer* pBottomLevelAS,
    uint64_t* pTlasSize,
    AccelerationStructure* pDes)
{

    std::shared_ptr<MappableBuffer> pInstanceDescBuffer = pDevice->CreateMappableBuffer( 3 * sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
    pInstanceDescBuffer->SetName( L"DXR TLAS Instance Description" );

    D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
    ThrowIfFailed( pInstanceDescBuffer->Map( (void**)&pInstanceDesc ) );
    {  
        for (int i = 0; i < 3; i++) {
            pInstanceDesc[i].InstanceID                  = i;
            pInstanceDesc[i].InstanceContributionToHitGroupIndex = i;
            pInstanceDesc[i].Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            pInstanceDesc[i].Transform[0][0] = pInstanceDesc[i].Transform[1][1] = pInstanceDesc[i].Transform[2][2] = 1;
            pInstanceDesc[i].Transform[0][3]                                                                       = 4.0 * (i - 1);
            pInstanceDesc[i].AccelerationStructure = pBottomLevelAS->GetD3D12Resource()->GetGPUVirtualAddress();
            pInstanceDesc[i].InstanceMask             = 0xFF;
        }
    }
    // Unmap
    pInstanceDescBuffer->Unmap();
    
    
    
    // First, get the size of the TLAS buffers and create them
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout  = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags    = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 3;
    inputs.Type     = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.InstanceDescs = pInstanceDescBuffer->GetD3D12Resource()->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &info );


    *pTlasSize = info.ResultDataMaxSizeInBytes;


    std::shared_ptr<AccelerationBuffer> pScratch =
        pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    pScratch->SetName( L"DXR TLAS Scratch" );

    pCommandList->UAVBarrier( pScratch );

    std::shared_ptr<AccelerationBuffer> pResult =
        pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE );
    pResult->SetName( L"DXR TLAS" );

    pCommandList->UAVBarrier( pResult );

    

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.DestAccelerationStructureData    = pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    pCommandList->UAVBarrier( pResult );

    pDes->pResult  = pResult;
    pDes->pScratch = pScratch;
    pDes->pInstanceDesc = pInstanceDescBuffer;
}
