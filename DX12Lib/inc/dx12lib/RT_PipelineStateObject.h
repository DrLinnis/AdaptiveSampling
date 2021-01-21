#include <d3d12.h>       // For D3D12_PIPELINE_STATE_STREAM_DESC, and ID3D12PipelineState
#include <wrl/client.h>  // For Microsoft::WRL::ComPtr

namespace dx12lib
{

class Device;

class RT_PipelineStateObject
{
public:
    Microsoft::WRL::ComPtr<ID3D12StateObject> GetD3D12PipelineState() const
    {
        return m_d3d12PipelineState;
    }

protected:
    RT_PipelineStateObject( Device& device, uint32_t nbrSubObjects, const D3D12_STATE_SUBOBJECT* pSubObjects );
    virtual ~RT_PipelineStateObject() = default;

private:
    Device&                                     m_Device;
    Microsoft::WRL::ComPtr<ID3D12StateObject>   m_d3d12PipelineState;
};
}  // namespace dx12lib
