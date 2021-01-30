#include "DX12LibPCH.h"

#include "dx12lib/RT_PipelineStateObject.h"
#include <dx12lib/RootSignature.h>

#include <dx12lib/Device.h>

#include <dxcapi.h>

using namespace dx12lib;
using namespace Microsoft::WRL;

ComPtr<IDxcBlob> ShaderHelper::CompileLibrary( const WCHAR* filename, const WCHAR* targetString )
{
    // Initialize the helper
    ComPtr<IDxcLibrary> pLibrary;
    ThrowIfFailed( DxcCreateInstance( CLSID_DxcLibrary, IID_PPV_ARGS( &pLibrary ) ) );

    ComPtr<IDxcCompiler> pCompiler;
    ThrowIfFailed( DxcCreateInstance( CLSID_DxcCompiler, IID_PPV_ARGS( &pCompiler ) ) );

    uint32_t                 codePage = CP_UTF8;
    ComPtr<IDxcBlobEncoding> sourceBlob;
    ThrowIfFailed( pLibrary->CreateBlobFromFile( filename, &codePage, &sourceBlob ) );

    ComPtr<IDxcOperationResult> pResult;
    ThrowIfFailed( pCompiler->Compile( sourceBlob.Get(), filename, L"", targetString, nullptr, 0, nullptr, 0, nullptr,
                                       &pResult ) );

    // Verify the result
    HRESULT resultCode;
    ThrowIfFailed( pResult->GetStatus( &resultCode ) );

    if ( FAILED( resultCode ) )
    {
        ComPtr<IDxcBlobEncoding> pError;
        ThrowIfFailed( pResult->GetErrorBuffer( &pError ) );
        std::vector<char> infoLog( pError->GetBufferSize() + 1 );
        memcpy( infoLog.data(), pError->GetBufferPointer(), pError->GetBufferSize() );
        infoLog[pError->GetBufferSize()] = 0;
        std::string errorStr             = std::string( "Compile error:\n" ) + std::string( infoLog.data() );
        throw std::exception( errorStr.c_str() );
    }

    ComPtr<IDxcBlob> pBlob;
    ThrowIfFailed( pResult->GetResult( &pBlob ) );

    return pBlob;
}

DxilLibrary::DxilLibrary( IDxcBlob* pBlob, const WCHAR* entryPoint[], uint32_t entryPointCount )
: pShaderBlob( pBlob )
{
    stateSubobject.Type  = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    stateSubobject.pDesc = &dxilLibDesc;

    dxilLibDesc = {};
    exportDesc.resize( entryPointCount );
    exportName.resize( entryPointCount );
    if ( pBlob )
    {
        dxilLibDesc.DXILLibrary.pShaderBytecode = pBlob->GetBufferPointer();
        dxilLibDesc.DXILLibrary.BytecodeLength  = pBlob->GetBufferSize();
        dxilLibDesc.NumExports                  = entryPointCount;
        dxilLibDesc.pExports                    = exportDesc.data();

        for ( uint32_t i = 0; i < entryPointCount; i++ )
        {
            exportName[i]                = entryPoint[i];
            exportDesc[i].Name           = exportName[i].c_str();
            exportDesc[i].Flags          = D3D12_EXPORT_FLAG_NONE;
            exportDesc[i].ExportToRename = nullptr;
        }
    }
};

HitProgram::HitProgram( LPCWSTR ahsExport, LPCWSTR chsExport, const std::wstring& name )
: exportName( name )
{
    desc.AnyHitShaderImport     = ahsExport; // any hit
    desc.ClosestHitShaderImport = chsExport; // closest hit
    desc.HitGroupExport         = exportName.c_str();

    subObject.Type  = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subObject.pDesc = &desc;
}

ExportAssociation::ExportAssociation( const WCHAR* exportNames[], uint32_t exportCount,
                   const D3D12_STATE_SUBOBJECT* pSubobjectToAssociate )
{
    association.NumExports            = exportCount;
    association.pExports              = exportNames;
    association.pSubobjectToAssociate = pSubobjectToAssociate;

    subobject.Type  = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    subobject.pDesc = &association;
}

ShaderConfig::ShaderConfig( uint32_t maxAttributeSizeInBytes, uint32_t maxPayloadSizeInBytes )
{
    shaderConfig.MaxAttributeSizeInBytes = maxAttributeSizeInBytes;
    shaderConfig.MaxPayloadSizeInBytes   = maxPayloadSizeInBytes;

    subobject.Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobject.pDesc = &shaderConfig;
}

PipelineConfig::PipelineConfig( uint32_t maxTraceRecursionDepth )
{
    config.MaxTraceRecursionDepth = maxTraceRecursionDepth;

    subobject.Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobject.pDesc = &config;
}




RT_PipelineStateObject::RT_PipelineStateObject( Device& device, uint32_t nbrSubObjects, const D3D12_STATE_SUBOBJECT* pSubObjects )
    : m_Device( device )
{
    auto d3d12Device = device.GetD3D12Device();

    D3D12_STATE_OBJECT_DESC desc = {};
    desc.NumSubobjects  = nbrSubObjects;
    desc.pSubobjects    = pSubObjects;
    desc.Type           = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    // Store pointer on GPU
    ThrowIfFailed( d3d12Device->CreateStateObject( &desc, IID_PPV_ARGS( &m_d3d12PipelineState ) ) );
}
