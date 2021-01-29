#pragma once

#include "Buffer.h"
#include "MappableBuffer.h"

#include "d3dx12.h"
#include <memory>

using namespace Microsoft::WRL;

namespace dx12lib
{
class Device;
class CommandList;
class VertexBuffer;
class IndexBuffer;
class Buffer;
class MappableBuffer;
class AccelerationBuffer;

struct AccelerationStructure
{
    std::shared_ptr<AccelerationBuffer> pScratch;
    std::shared_ptr<AccelerationBuffer> pResult;
    std::shared_ptr<MappableBuffer> pInstanceDesc;
};

class AccelerationBuffer : public Resource
{
public:
    static void CreateTopLevelAS( dx12lib::Device*             pDevice,
                                    dx12lib::CommandList*        pCommandList,
                                    dx12lib::AccelerationBuffer* pBottomLevelAS,
                                    uint64_t* pTlasSize, AccelerationStructure* pDes );

    static void CreateBottomLevelAS( dx12lib::Device*       pDevice,
                                    dx12lib::CommandList*  pCommandList,
                                    dx12lib::VertexBuffer* pVertexBuffer,
                                    dx12lib::IndexBuffer*  pIndexBuffer,
                                    AccelerationStructure* pDes );

protected:
    AccelerationBuffer( Device& device, 
                        const D3D12_RESOURCE_DESC& resourceDesc, 
                        const D3D12_RESOURCE_STATES initState,
                        const D3D12_HEAP_TYPE heapType )
    : Resource( device, resourceDesc, nullptr, initState, heapType )
    {}
    AccelerationBuffer( Device& device, Microsoft::WRL::ComPtr<ID3D12Resource> resource )
    : Resource( device, resource, nullptr )
    {}
};

class MakeAccelerationBuffer : public AccelerationBuffer
{
public:
    MakeAccelerationBuffer( Device& device,
                            const D3D12_RESOURCE_DESC& resDesc, 
                            const D3D12_RESOURCE_STATES initState,
                            const D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT )
    : AccelerationBuffer( device, resDesc, initState, heapType )
    {}

    MakeAccelerationBuffer( Device& device, Microsoft::WRL::ComPtr<ID3D12Resource> resource )
    : AccelerationBuffer( device, resource )
    {}

    virtual ~MakeAccelerationBuffer() {}
};
}  // namespace dx12lib
