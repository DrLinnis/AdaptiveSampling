#include "DX12LibPCH.h"

#include <dx12lib/ShaderTable.h>

#include <dx12lib/Device.h>
#include <dx12lib/Resource.h>
#include <dx12lib/Helpers.h>


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

    d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateUnorderedAccessView( d3d12Resource.Get(), nullptr, pOutputUav, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateConstantBufferView( pCbv, heapHandle );
}


ShaderTableResourceView::ShaderTableResourceView( Device& device, const D3D12_CONSTANT_BUFFER_VIEW_DESC*  pCbv )
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