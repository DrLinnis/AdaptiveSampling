#include "DX12LibPCH.h"

#include <dx12lib/ShaderTable.h>


#include <dx12lib/Device.h>
#include <dx12lib/Resource.h>

using namespace dx12lib;

ShaderTableView::ShaderTableView( Device& device, const std::shared_ptr<Resource>& outputResource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* rayTlasSrv,
    const std::shared_ptr<Resource>& counterResource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav
    ) 
: m_Device( device )
, m_Resource( outputResource )
, m_CounterResource( counterResource )
{
    assert( m_Resource || uav || rayTlasSrv );

    auto d3d12Device          = m_Device.GetD3D12Device();
    auto d3d12Resource        = m_Resource ? m_Resource->GetD3D12Resource() : nullptr;
    auto d3d12CounterResource = m_CounterResource ? m_CounterResource->GetD3D12Resource() : nullptr;

    if ( m_Resource )
    {
        auto d3d12ResourceDesc = m_Resource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
    }

    m_Descriptor = m_Device.AllocateDescriptors( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    d3d12Device->CreateUnorderedAccessView( d3d12Resource.Get(), d3d12CounterResource.Get(), uav,
                                            m_Descriptor.GetDescriptorHandle() );

    d3d12Device->CreateShaderResourceView( nullptr, rayTlasSrv, m_Descriptor.GetDescriptorHandle( 1 ) );
}
