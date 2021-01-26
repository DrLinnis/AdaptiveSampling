#include "DX12LibPCH.h"

#include <dx12lib/MappableBuffer.h>
#include <dx12lib/Device.h>

using namespace dx12lib;

MappableBuffer::MappableBuffer( Device& device, const D3D12_RESOURCE_DESC& resDesc )
: Resource( device, resDesc, nullptr, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD )
{ }

MappableBuffer::MappableBuffer( Device& device, ComPtr<ID3D12Resource> resource )
: Resource( device, resource )
{}

HRESULT MappableBuffer::Map( void** pData )
{
    return this->GetD3D12Resource()->Map( 0, nullptr, pData );
}

void MappableBuffer::Unmap()
{
    return this->GetD3D12Resource()->Unmap( 0, nullptr );
}
