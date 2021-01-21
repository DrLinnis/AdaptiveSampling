#include "DX12LibPCH.h"

#include "dx12lib/RT_PipelineStateObject.h"

#include <dx12lib/Device.h>

using namespace dx12lib;

RT_PipelineStateObject::RT_PipelineStateObject( Device& device, uint32_t nbrSubObjects, const D3D12_STATE_SUBOBJECT* pSubObjects )
    : m_Device( device )
{
    auto d3d12Device = device.GetD3D12Device();

    D3D12_STATE_OBJECT_DESC desc;
    desc.NumSubobjects  = nbrSubObjects;
    desc.pSubobjects    = pSubObjects;
    desc.Type           = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    ThrowIfFailed( d3d12Device->CreateStateObject( &desc, IID_PPV_ARGS( &m_d3d12PipelineState ) ) );
}