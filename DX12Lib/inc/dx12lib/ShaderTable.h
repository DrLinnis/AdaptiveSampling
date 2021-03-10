#pragma once


#include <dx12lib/DescriptorAllocation.h>

#include <dx12lib/DescriptorAllocatorPage.h>

#include <d3d12.h>  // For D3D12_UNORDERED_ACCESS_VIEW_DESC and D3D12_CPU_DESCRIPTOR_HANDLE
#include <memory>   // For std::shared_ptr


namespace dx12lib
{

class Device;
class Resource;
class IndexBuffer;
class VertexBuffer;
class Scene;
class MappableBuffer;
class RenderTarget;

class ShaderTableResourceView
{
public:
    
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle() const
    {
        return m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    }

    ID3D12DescriptorHeap* GetTableHeap() const {
        return m_SrvUavHeap.Get();
    }

    void UpdateShaderTableUAV( const uint32_t nbrRenderTargets, const RenderTarget* pRenderTargets );

protected:
    ShaderTableResourceView( Device& device, const std::shared_ptr<Resource>& outputResource, 
                             const D3D12_SHADER_RESOURCE_VIEW_DESC*  pRayTlasSrv,
                             const D3D12_CONSTANT_BUFFER_VIEW_DESC*  pCbv );

    ShaderTableResourceView( Device& device, 
                             const uint32_t nbrRenderTargets, 
                             const RenderTarget* pRenderTargets, 
                             const D3D12_SHADER_RESOURCE_VIEW_DESC*  pRayTlasSrv,
                             const D3D12_CONSTANT_BUFFER_VIEW_DESC*  pCbv ,
                             Scene* pMeshes);

    ShaderTableResourceView( Device& device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbv );

    virtual ~ShaderTableResourceView() = default;

private:


    Device&                                         m_Device;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    m_SrvUavHeap;
    std::shared_ptr<MappableBuffer>                 m_MaterialBuffer;
};


class MakeShaderTableView : public ShaderTableResourceView
{
public:
    MakeShaderTableView( Device& device, const std::shared_ptr<Resource>& outputResource, 
                         const D3D12_SHADER_RESOURCE_VIEW_DESC*  pRayTlasSrv,
                         const D3D12_CONSTANT_BUFFER_VIEW_DESC*  pCbv )
    : ShaderTableResourceView( device, outputResource, pRayTlasSrv, pCbv )
    {}

    MakeShaderTableView( Device& device, 
                         const uint32_t nbrRenderTargets,
                         const RenderTarget* pRenderTargets, 
                         const D3D12_SHADER_RESOURCE_VIEW_DESC*  pRayTlasSrv,
                         const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbv, Scene* pMeshes )
    : ShaderTableResourceView( device, nbrRenderTargets, pRenderTargets, pRayTlasSrv, pCbv, pMeshes )
    {}

    MakeShaderTableView( Device& device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbv )
    : ShaderTableResourceView( device, pCbv )
    {}

    virtual ~MakeShaderTableView() {}
};

}  // namespace dx12lib
