#include "DX12LibPCH.h"

#include "dx12lib/AccelerationStructure.h"

#include "dx12lib/VertexBuffer.h"
#include "dx12lib/IndexBuffer.h"
#include "dx12lib/Device.h"
#include "dx12lib/Buffer.h"
#include "dx12lib/CommandList.h"
#include "dx12lib/MappableBuffer.h"

using namespace dx12lib;


void AccelerationBuffer::CreateBottomLevelAS(Device* pDevice,
    CommandList* pCommandList,
    VertexBuffer* pVertexBuffer[],
    IndexBuffer* pIndexBuffer[],
    size_t geometryCount,
    AccelerationStructure* pDes) 
{
    // Get prebuild infos
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDesc;
    geomDesc.resize( geometryCount );
    for (int i = 0; i < geometryCount; ++i) {
        geomDesc[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        // NOTE: As a vertex can contain more than just the XYZ position it thus has stride defined seperatly.

        geomDesc[i].Triangles.VertexBuffer.StartAddress = pVertexBuffer[i]->GetD3D12Resource()->GetGPUVirtualAddress();
        geomDesc[i].Triangles.VertexBuffer.StrideInBytes = pVertexBuffer[i]->GetVertexStride();
        geomDesc[i].Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;  // XYZ per vertex.
        geomDesc[i].Triangles.VertexCount                = pVertexBuffer[i]->GetNumVertices();

        geomDesc[i].Triangles.IndexBuffer = pIndexBuffer[i]->GetD3D12Resource()->GetGPUVirtualAddress();
        geomDesc[i].Triangles.IndexCount  = pIndexBuffer[i]->GetNumIndicies();
        geomDesc[i].Triangles.IndexFormat = pIndexBuffer[i]->GetIndexFormat();

        geomDesc[i].Triangles.Transform3x4 = 0;

        // number of bytes in buffer divided by bytes of a vertex
        geomDesc[i].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    }
    

    // Get the size requirements for the scratch and AS buffers
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout                                          = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs       = geometryCount;
    inputs.pGeometryDescs = geomDesc.data();
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

void AccelerationBuffer::CreateTopLevelAS(Device* pDevice, CommandList* pCommandList, size_t nbrBlas,
    dx12lib::AccelerationBuffer* pBlasList[],
    uint64_t* pTlasSize,
    AccelerationStructure* pDes)
{

    std::shared_ptr<MappableBuffer> pInstanceDescBuffer = pDevice->CreateMappableBuffer( 3 * sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
    pInstanceDescBuffer->SetName( L"DXR TLAS Instance Description" );

    D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
    ThrowIfFailed( pInstanceDescBuffer->Map( (void**)&pInstanceDesc ) );
    {  
        for (int i = 1; i < 3; i++) {
            pInstanceDesc[i].InstanceID                  = i;
            pInstanceDesc[i].InstanceContributionToHitGroupIndex = i;
            pInstanceDesc[i].Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            pInstanceDesc[i].Transform[0][0] = pInstanceDesc[i].Transform[1][1] = pInstanceDesc[i].Transform[2][2] = 1;
            pInstanceDesc[i].Transform[0][3]          = 4.0 * ( i - 1 ) + 4.0 * ( i - 2 );
            pInstanceDesc[i].AccelerationStructure = pBlasList[0]->GetD3D12Resource()->GetGPUVirtualAddress();
            pInstanceDesc[i].InstanceMask             = 0xFF;
        }

        pInstanceDesc[0].InstanceID                          = 0;
        pInstanceDesc[0].InstanceContributionToHitGroupIndex = 0;
        pInstanceDesc[0].Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        pInstanceDesc[0].Transform[0][0]                     = 1;
        pInstanceDesc[0].Transform[1][1] = pInstanceDesc[3].Transform[2][2] = 0; // rotation 90 deg around x
        pInstanceDesc[0].Transform[1][2]                                    = -1;
        pInstanceDesc[0].Transform[2][1]                                    = 1;
        pInstanceDesc[0].Transform[1][3]                                    = -1.5;
        pInstanceDesc[0].AccelerationStructure = pBlasList[1]->GetD3D12Resource()->GetGPUVirtualAddress();
        pInstanceDesc[0].InstanceMask          = 0xFF;
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
