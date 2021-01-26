#include <d3d12.h>       // For D3D12_PIPELINE_STATE_STREAM_DESC, and ID3D12PipelineState
#include <wrl/client.h>  // For Microsoft::WRL::ComPtr

#include <dxcapi.h>

namespace dx12lib
{

class Device;
class RootSignature;

using namespace Microsoft::WRL;

class ShaderHelper
{
public:
    static ComPtr<IDxcBlob> CompileLibrary( const WCHAR* filename, const WCHAR* targetString );
};

struct DxilLibrary
{
    DxilLibrary( IDxcBlob* pBlob, const WCHAR* entryPoint[], uint32_t entryPointCount );

    DxilLibrary()
    : DxilLibrary( nullptr, nullptr, 0 )
    {}

    D3D12_DXIL_LIBRARY_DESC        dxilLibDesc = {};
    D3D12_STATE_SUBOBJECT          stateSubobject {};
    IDxcBlob*                       pShaderBlob;
    std::vector<D3D12_EXPORT_DESC> exportDesc;
    std::vector<std::wstring>      exportName;
};

struct HitProgram
{
    HitProgram( LPCWSTR ahsExport, LPCWSTR chsExport, const std::wstring& name );

    std::wstring          exportName;
    D3D12_HIT_GROUP_DESC  desc      = {};
    D3D12_STATE_SUBOBJECT subObject = {};
};

struct ExportAssociation
{
    ExportAssociation( const WCHAR* exportNames[], uint32_t exportCount,
                       const D3D12_STATE_SUBOBJECT* pSubobjectToAssociate );

    D3D12_STATE_SUBOBJECT                  subobject   = {};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association = {};
};

struct ShaderConfig
{
    ShaderConfig( uint32_t maxAttributeSizeInBytes, uint32_t maxPayloadSizeInBytes );

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    D3D12_STATE_SUBOBJECT          subobject    = {};
};

struct PipelineConfig
{
    PipelineConfig( uint32_t maxTraceRecursionDepth );

    D3D12_RAYTRACING_PIPELINE_CONFIG config    = {};
    D3D12_STATE_SUBOBJECT            subobject = {};
};


class RT_PipelineStateObject
{
public:
    ID3D12StateObject* GetD3D12PipelineState() const
    {
        return m_d3d12PipelineState;
    }
    

protected:
    RT_PipelineStateObject( Device& device, uint32_t nbrSubObjects, const D3D12_STATE_SUBOBJECT* pSubObjects );
    virtual ~RT_PipelineStateObject() = default;

private:
    Device&                                     m_Device;
    ID3D12StateObject* m_d3d12PipelineState;
};
}  // namespace dx12lib
