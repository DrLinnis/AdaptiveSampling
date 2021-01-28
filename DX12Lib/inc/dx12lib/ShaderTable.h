#pragma once


#include <dx12lib/DescriptorAllocation.h>

#include <dx12lib/DescriptorAllocatorPage.h>

#include <d3d12.h>  // For D3D12_UNORDERED_ACCESS_VIEW_DESC and D3D12_CPU_DESCRIPTOR_HANDLE
#include <memory>   // For std::shared_ptr

namespace dx12lib
{

class Device;
class Resource;


class ShaderTableResourceView
{
public:
    

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle() const
    {
        return m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    }

    ID3D12DescriptorHeap* GetTableHeap() const {
        return m_SrvUavHeap;
    }

protected:
    ShaderTableResourceView( Device& device, const std::shared_ptr<Resource>& outputResource,
                             const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav,
                             const D3D12_SHADER_RESOURCE_VIEW_DESC*  rayTlasSrv );

    virtual ~ShaderTableResourceView() = default;

private:
    Device&                     m_Device;
    ID3D12DescriptorHeap*       m_SrvUavHeap;
};


class MakeShaderTableView : public ShaderTableResourceView
{
public:
    MakeShaderTableView( Device& device, const std::shared_ptr<Resource>& outputResource,
                         const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav,
                         const D3D12_SHADER_RESOURCE_VIEW_DESC*  rayTlasSrv )
    : ShaderTableResourceView( device, outputResource, uav, rayTlasSrv )
    {}

    virtual ~MakeShaderTableView() {}
};

}  // namespace dx12lib
