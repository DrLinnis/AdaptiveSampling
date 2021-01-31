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
    AccelerationBuffer* pBlasList[],
    uint64_t* pTlasSize,
    AccelerationStructure* pDes,
    MappableBuffer* pInstanceDescBuffer, 
    bool update )
{

    // First, get the size of the TLAS buffers and create them
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout  = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags                                                =  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs = 3;
    inputs.Type     = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.InstanceDescs = pInstanceDescBuffer->GetD3D12Resource()->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &info );


    if (update) {
        pCommandList->UAVBarrier( pDes->pResult );
    } else {
        pDes->pScratch = pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS 
        );
        pDes->pScratch->SetName( L"DXR TLAS Scratch" );

        pDes->pResult = pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE 
        );
        pDes->pResult->SetName( L"DXR TLAS" );

        *pTlasSize = info.ResultDataMaxSizeInBytes;
    }
        
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.DestAccelerationStructureData    = pDes->pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = pDes->pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    if (update) {
        asDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        asDesc.SourceAccelerationStructureData = pDes->pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    }

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    pCommandList->UAVBarrier( pDes->pResult );

}
