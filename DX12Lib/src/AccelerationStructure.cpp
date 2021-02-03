#include "DX12LibPCH.h"

#include "dx12lib/AccelerationStructure.h"

#include "dx12lib/VertexBuffer.h"
#include "dx12lib/IndexBuffer.h"
#include "dx12lib/Device.h"
#include "dx12lib/Buffer.h"
#include "dx12lib/CommandList.h"
#include "dx12lib/MappableBuffer.h"
#include "dx12lib/Scene.h"
#include <dx12lib/Mesh.h>

using namespace dx12lib;


void AccelerationBuffer::CreateBottomLevelAS(Device* pDevice,
    CommandList* pCommandList, Scene* pScene, AccelerationStructure* pDes) 
{
    int idx           = 0;
    int geometryCount = pScene->m_Meshes.size();

    // Get prebuild infos
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDesc;
    geomDesc.resize( geometryCount );

    for (std::shared_ptr<Mesh> m : pScene->m_Meshes) {
        geomDesc[idx].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        // NOTE: As a vertex can contain more than just the XYZ position it thus has stride defined seperatly.
        
        geomDesc[idx].Triangles.VertexBuffer.StartAddress =
            m->GetVertexBuffer( 0 )->GetD3D12Resource()->GetGPUVirtualAddress();
        geomDesc[idx].Triangles.VertexBuffer.StrideInBytes = m->GetVertexBuffer( 0 )->GetVertexStride();
        geomDesc[idx].Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;  // XYZ per vertex.
        geomDesc[idx].Triangles.VertexCount                = m->GetVertexBuffer( 0 )->GetNumVertices();

        geomDesc[idx].Triangles.IndexBuffer = m->GetIndexBuffer()->GetD3D12Resource()->GetGPUVirtualAddress();
        geomDesc[idx].Triangles.IndexCount  = m->GetIndexBuffer()->GetNumIndicies();
        geomDesc[idx].Triangles.IndexFormat = m->GetIndexBuffer()->GetIndexFormat();

        geomDesc[idx].Triangles.Transform3x4 = 0;

        // number of bytes in buffer divided by bytes of a vertex
        geomDesc[idx].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        ++idx;
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

    pDes->pScratch =
        pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    pDes->pScratch->SetName( L"DXR BLAS Scratch" );
    pCommandList->UAVBarrier( pDes->pScratch );

    pDes->pResult =
        pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE );
    pDes->pResult->SetName( L"DXR BLAS" );
    pCommandList->UAVBarrier( pDes->pResult );

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs                                             = inputs;
    asDesc.DestAccelerationStructureData                      = pDes->pResult->GetD3D12Resource()->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = pDes->pScratch->GetD3D12Resource()->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure( &asDesc );

    pCommandList->UAVBarrier( pDes->pResult, true );


}

void AccelerationBuffer::CreateTopLevelAS(
    Device* pDevice,
    CommandList* pCommandList,
    uint64_t* pTlasSize,
    AccelerationStructure* pDes,
    size_t numInstances,
    MappableBuffer* pInstanceDescBuffer, 
    bool update )
{

    // First, get the size of the TLAS buffers and create them
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout  = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs      = numInstances;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.InstanceDescs = pInstanceDescBuffer->GetD3D12Resource()->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &info );


    if (update) {
        pCommandList->UAVBarrier( pDes->pResult );
    } else {
        pDes->pScratch = pDevice->CreateAccelerationBuffer( info.ScratchDataSizeInBytes, 
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS 
        );
        pDes->pScratch->SetName( L"DXR TLAS Scratch" );

        pDes->pResult = pDevice->CreateAccelerationBuffer( info.ResultDataMaxSizeInBytes, 
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE 
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

    pCommandList->UAVBarrier( pDes->pResult, true);

}
