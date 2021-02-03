#include "DX12LibPCH.h"

#include <dx12lib/ShaderTable.h>

#include <dx12lib/Device.h>
#include <dx12lib/Resource.h>
#include <dx12lib/Helpers.h>
#include <dx12lib/VertexBuffer.h>
#include <dx12lib/IndexBuffer.h>
#include <dx12lib/Scene.h>
#include <dx12lib/Mesh.h>

using namespace dx12lib;

ShaderTableResourceView::ShaderTableResourceView( Device& device, const std::shared_ptr<Resource>& outputResource,
                                                  const D3D12_UNORDERED_ACCESS_VIEW_DESC* pOutputUav,
                                                  const D3D12_SHADER_RESOURCE_VIEW_DESC*  pRayTlasSrv,
                                                  const D3D12_CONSTANT_BUFFER_VIEW_DESC*  pCbv )
: m_Device( device )
, m_Resource( outputResource )
{
    assert( pOutputUav || pRayTlasSrv || pCbv );

    auto d3d12Device          = m_Device.GetD3D12Device();
    auto d3d12Resource = m_Resource ? m_Resource->GetD3D12Resource() : nullptr;

    if ( m_Resource )
    {
        auto d3d12ResourceDesc = m_Resource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 3;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;


    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );
    
    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    d3d12Device->CreateUnorderedAccessView( d3d12Resource.Get(), nullptr, pOutputUav, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateConstantBufferView( pCbv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );

}

ShaderTableResourceView::ShaderTableResourceView( Device& device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbv )
: m_Device( device )
{
    assert( pCbv );

    auto d3d12Device   = m_Device.GetD3D12Device();
    auto d3d12Resource = m_Resource ? m_Resource->GetD3D12Resource() : nullptr;

    if ( m_Resource )
    {
        auto d3d12ResourceDesc = m_Resource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 1;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );

    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    d3d12Device->CreateConstantBufferView( pCbv, heapHandle );
}

ShaderTableResourceView::ShaderTableResourceView( Device&                                     device,
                                                    const std::shared_ptr<Resource>&            outputResource,
                                                    const D3D12_UNORDERED_ACCESS_VIEW_DESC*     pOutputUav,
                                                    const D3D12_SHADER_RESOURCE_VIEW_DESC*      pRayTlasSrv,
                                                    const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbv ,
                                                    Scene* pMeshes)
: m_Device( device )
, m_Resource( outputResource )
{
    assert( pOutputUav || pRayTlasSrv || pCbv );

    auto d3d12Device   = m_Device.GetD3D12Device();
    auto d3d12Resource = m_Resource ? m_Resource->GetD3D12Resource() : nullptr;
    auto nbrBuffers       = pMeshes->m_Meshes.size();

    if ( m_Resource )
    {
        auto d3d12ResourceDesc = m_Resource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 3 + nbrBuffers * 2; // one buffer per index and per vertice
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );

    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    d3d12Device->CreateUnorderedAccessView( d3d12Resource.Get(), nullptr, pOutputUav, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateConstantBufferView( pCbv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    // buffer tables

    D3D12_SHADER_RESOURCE_VIEW_DESC copy = {};
    copy.Format                          = DXGI_FORMAT_R32_TYPELESS;
    copy.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;

    copy.Buffer.Flags                     = D3D12_BUFFER_SRV_FLAG_RAW;
    copy.Buffer.StructureByteStride = 0;
    copy.Buffer.FirstElement              = 0;

    copy.Shader4ComponentMapping          = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> srvIdxDesc;
    srvIdxDesc.resize( nbrBuffers );

    std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> srvVertDesc;
    srvVertDesc.resize( nbrBuffers );

    for (int i = 0; i < nbrBuffers; ++i) {
        srvIdxDesc[i] = copy;
        srvIdxDesc[i].Buffer.NumElements =
            // Frome byte size to Nbr R32 size
            pMeshes->m_Meshes[i]->GetIndexBuffer()->GetIndexBufferView().SizeInBytes / sizeof( float ); 

        srvVertDesc[i] = copy;
        srvVertDesc[i].Buffer.NumElements =
            // Frome byte size to Nbr R32 size
            pMeshes->m_Meshes[i]->GetVertexBuffer(0)->GetVertexBufferView().SizeInBytes / sizeof( float );
    }

    // indices first
    for ( int i = 0; i < nbrBuffers; ++i )
    {
        d3d12Device->CreateShaderResourceView( pMeshes->m_Meshes[i]->GetIndexBuffer()->GetD3D12Resource().Get(),
                                               &srvIdxDesc[i], heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }

    // vertices second
    for ( int i = 0; i < nbrBuffers; ++i )
    {
        d3d12Device->CreateShaderResourceView( pMeshes->m_Meshes[i]->GetVertexBuffer( 0 )->GetD3D12Resource().Get(),
                                               &srvVertDesc[i], heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }

}

/*
ShaderTableResourceView::ShaderTableResourceView( Device&                                device,
                                                    const D3D12_SHADER_RESOURCE_VIEW_DESC* pRayTlasSrv,
                                                  const std::shared_ptr<IndexBuffer>&    idxBuffRes,
                                                    const D3D12_SHADER_RESOURCE_VIEW_DESC* pIdxBuffSrv,
                                                  const std::shared_ptr<VertexBuffer>&   vertBuffRes,
                                                    const D3D12_SHADER_RESOURCE_VIEW_DESC* pVerBuffSrv )
: m_Device( device )
, m_Resource( nullptr )
, m_IndexBuff( idxBuffRes )
, m_VertBuff( vertBuffRes )
{
    assert( pIdxBuffSrv || pRayTlasSrv || pVerBuffSrv );

    auto d3d12Device   = m_Device.GetD3D12Device();
    auto d3d12ResourceVert = m_VertBuff ? m_VertBuff->GetD3D12Resource() : nullptr;
    auto d3d12ResourceIdx = m_IndexBuff ? m_IndexBuff->GetD3D12Resource() : nullptr;

    if ( m_Resource )
    {
        auto d3d12ResourceDesc = m_Resource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 3;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );

    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    d3d12Device->CreateShaderResourceView( d3d12ResourceIdx.Get(), pIdxBuffSrv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateShaderResourceView( d3d12ResourceVert.Get(), pVerBuffSrv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );
}
*/