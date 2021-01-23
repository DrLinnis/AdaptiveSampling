#include "DX12LibPCH.h"

#include <dx12lib/ShaderTableBuffer.h>
#include <dx12lib/Device.h>

using namespace dx12lib;

ShaderTableBuffer::ShaderTableBuffer( Device& device, const D3D12_RESOURCE_DESC& resDesc )
: Resource( device, resDesc, nullptr, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD )
{ }

ShaderTableBuffer::ShaderTableBuffer( Device& device, ComPtr<ID3D12Resource> resource )
: Resource( device, resource )
{}

HRESULT ShaderTableBuffer::Map( void** pData )
{
    return this->GetD3D12Resource()->Map( 0, nullptr, pData );
}

void ShaderTableBuffer::Unmap()
{
    return this->GetD3D12Resource()->Unmap( 0, nullptr );
}
