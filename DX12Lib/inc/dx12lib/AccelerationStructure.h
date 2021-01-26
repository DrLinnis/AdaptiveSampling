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

class AccelerationBuffer : public Resource
{
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

class AccelerationStructure
{
public:
    static std::shared_ptr<AccelerationStructure> CreateTopLevelAS( 
        dx12lib::Device* pDevice, dx12lib::CommandList* pCommandList,
        dx12lib::AccelerationBuffer* pBottomLevelAS, uint64_t* pTlasSize );

    static std::shared_ptr<AccelerationStructure> CreateBottomLevelAS( dx12lib::Device*       pDevice,
                                                                       dx12lib::CommandList*  pCommandList,
                                                                       dx12lib::VertexBuffer* pVertexBuffer,
                                                                       dx12lib::IndexBuffer*  pIndexBuffer );

    AccelerationStructure()
    : pScratch( nullptr )
    , pResult( nullptr )
    , pInstanceDesc( nullptr )
    {}

    AccelerationStructure( AccelerationStructure* Acc )
    : pScratch( Acc->pScratch )
    , pResult( Acc->pResult )
    , pInstanceDesc( Acc->pInstanceDesc )
    {}

    void reset()
    {
        pScratch.reset();
        pResult.reset();
        pInstanceDesc.reset();
    }

    std::shared_ptr<AccelerationBuffer> GetResult() const {
        return pResult;
    }

private: 
    

    std::shared_ptr<AccelerationBuffer>    pScratch;    // required for intermediate computation
    std::shared_ptr<AccelerationBuffer>    pResult;     // holds the acceleration data. 
    std::shared_ptr<MappableBuffer>         pInstanceDesc;  // Used only for top-level AS
};

}  // namespace dx12lib
