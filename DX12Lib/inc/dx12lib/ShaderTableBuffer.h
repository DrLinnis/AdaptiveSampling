#pragma once

#include "Buffer.h"
#include "DescriptorAllocation.h"

#include <d3dx12.h>

namespace dx12lib
{

class Device;

class ShaderTableBuffer : public Resource
{
public:
    size_t GetBufferSize() const
    {
        return m_BufferSize;
    }

    HRESULT Map( void** pData );
    void    Unmap();

protected:
    ShaderTableBuffer( Device& device, const D3D12_RESOURCE_DESC& resDesc );
    ShaderTableBuffer( Device& device, Microsoft::WRL::ComPtr<ID3D12Resource> resource );
    virtual ~ShaderTableBuffer() = default;

private:
    size_t m_BufferSize;
};
}  // namespace dx12lib