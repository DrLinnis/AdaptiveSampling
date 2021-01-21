#include "DX12LibPCH.h"

#include "dx12lib/AccelerationStructure.h"

#include "dx12lib/VertexBuffer.h"
#include "dx12lib/IndexBuffer.h"
#include "dx12lib/Device.h"
#include "dx12lib/Buffer.h"
#include "dx12lib/CommandList.h"

using namespace dx12lib;

std::shared_ptr<AccelerationBuffer> AccelerationStructure::GetResult() {
    return this->pResult;
}

std::shared_ptr<AccelerationStructure> AccelerationStructure::CreateTopLevelAS(
    std::shared_ptr<dx12lib::Device> pDevice, std::shared_ptr<dx12lib::CommandList> pCommandList,
    std::shared_ptr<dx12lib::AccelerationBuffer> pBottomLevelAS, uint64_t* pTlasSize )
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

    return pCommandList->BuildTopLevelAccelerationStructure( pBottomLevelAS, inputs, info );
}

std::shared_ptr<AccelerationStructure> AccelerationStructure::CreateBottomLevelAS(
    std::shared_ptr<dx12lib::Device> pDevice, std::shared_ptr<dx12lib::CommandList> pCommandList,
    std::shared_ptr<dx12lib::VertexBuffer> pVertexBuffer, std::shared_ptr<dx12lib::IndexBuffer> pIndexBuffer )
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

    return pCommandList->BuildBottomLevelAccelerationStructure( inputs, info );
}