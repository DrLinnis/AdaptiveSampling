#pragma once

#include "Buffer.h"

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

class AccelerationBuffer : public Resource
{
protected:
    AccelerationBuffer( Device& device, const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_RESOURCE_STATES initState,
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
    MakeAccelerationBuffer( Device& device, const D3D12_RESOURCE_DESC& resDesc, const D3D12_RESOURCE_STATES initState,
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
        std::shared_ptr<dx12lib::Device> pDevice, std::shared_ptr<dx12lib::CommandList> pCommandList,
        std::shared_ptr<dx12lib::AccelerationBuffer> pBottomLevelAS, uint64_t* pTlasSize );

    static std::shared_ptr<AccelerationStructure> CreateBottomLevelAS( 
        std::shared_ptr<dx12lib::Device> pDevice, std::shared_ptr<dx12lib::CommandList> pCommandList,
        std::shared_ptr<dx12lib::VertexBuffer> pVertexBuffer, std::shared_ptr<dx12lib::IndexBuffer> pIndexBuffer );
    
    AccelerationStructure( std::shared_ptr<AccelerationBuffer> pScratch, std::shared_ptr<AccelerationBuffer> pResult)
    : pScratch( pScratch )
    , pResult( pResult )
    , pInstanceDesc( nullptr )
    {}

    AccelerationStructure( std::shared_ptr<AccelerationBuffer> pScratch, std::shared_ptr<AccelerationBuffer> pResult,
                           std::shared_ptr<AccelerationBuffer> pInstanceDesc )
        : pScratch( pScratch )
        , pResult( pResult )
        , pInstanceDesc( pInstanceDesc )
    {}

    void reset()
    {
        pScratch.reset();
        pResult.reset();
        pInstanceDesc.reset();
    }

    std::shared_ptr<AccelerationBuffer> GetResult() const;

private: 
    std::shared_ptr<AccelerationBuffer>    pScratch;    // required for intermediate computation
    std::shared_ptr<AccelerationBuffer>    pResult;     // holds the acceleration data. 
    std::shared_ptr<AccelerationBuffer>    pInstanceDesc;  // Used only for top-level AS
};

}  // namespace dx12lib
