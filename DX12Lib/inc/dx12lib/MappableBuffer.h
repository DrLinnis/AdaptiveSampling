#pragma once

#include "Buffer.h"
#include "DescriptorAllocation.h"

#include <d3dx12.h>

namespace dx12lib
{

class Device;

class MappableBuffer : public Resource
{
public:

    HRESULT Map( void** pData );
    void    Unmap();

protected:
    MappableBuffer( Device& device, const D3D12_RESOURCE_DESC& resDesc );
    MappableBuffer( Device& device, Microsoft::WRL::ComPtr<ID3D12Resource> resource );
    virtual ~MappableBuffer() = default;

};

class MakeMappableBuffer : public MappableBuffer
{
public:
    MakeMappableBuffer( Device& device, const D3D12_RESOURCE_DESC& desc )
    : MappableBuffer( device, desc )
    {}

    MakeMappableBuffer( Device& device, Microsoft::WRL::ComPtr<ID3D12Resource> resoruce )
    : MappableBuffer( device, resoruce )
    {}

    virtual ~MakeMappableBuffer() {}
};

}  // namespace dx12lib