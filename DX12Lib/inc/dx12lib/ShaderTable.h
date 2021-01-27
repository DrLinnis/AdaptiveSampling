#pragma once


#include <dx12lib/DescriptorAllocation.h>

#include <dx12lib/DescriptorAllocatorPage.h>

#include <d3d12.h>  // For D3D12_UNORDERED_ACCESS_VIEW_DESC and D3D12_CPU_DESCRIPTOR_HANDLE
#include <memory>   // For std::shared_ptr

namespace dx12lib
{

class Device;
class Resource;


class ShaderTableView
{
public:
    std::shared_ptr<Resource> GetResource() const
    {
        return m_Resource;
    }

    std::shared_ptr<Resource> GetCounterResource() const
    {
        return m_CounterResource;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle() const
    {
        return m_Descriptor.GetDescriptorHandle();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle() const
    {
        return m_Descriptor.GetDescriptorAllocatorPage()->GetGpuDescriptorHandle();
    }

protected:
    ShaderTableView( Device& device, const std::shared_ptr<Resource>& outputResource,
                         const D3D12_SHADER_RESOURCE_VIEW_DESC* rayTlasSrv, 
                         const std::shared_ptr<Resource>&        counterResource = nullptr,
                         const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav             = nullptr );

    virtual ~ShaderTableView() = default;

private:
    Device&                   m_Device;
    std::shared_ptr<Resource> m_Resource;
    std::shared_ptr<Resource> m_CounterResource;
    DescriptorAllocation      m_Descriptor;
};


class MakeShaderTableView : public ShaderTableView
{
public:
    MakeShaderTableView( Device& device, const std::shared_ptr<Resource>& outputResource,
                         const D3D12_SHADER_RESOURCE_VIEW_DESC* rayTlasSrv,
                         const std::shared_ptr<Resource>& counterResource, 
                         const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav )
    : ShaderTableView( device, outputResource, rayTlasSrv, counterResource, uav )
    {}

    virtual ~MakeShaderTableView() {}
};

}  // namespace dx12lib
