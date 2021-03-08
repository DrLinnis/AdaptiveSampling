#include "DX12LibPCH.h"

#include <dx12lib/ShaderTable.h>

#include <vector>
#include <algorithm>

#include <dx12lib/Device.h>
#include <dx12lib/Resource.h>
#include <dx12lib/Helpers.h>
#include <dx12lib/VertexBuffer.h>
#include <dx12lib/IndexBuffer.h>
#include <dx12lib/Scene.h>
#include <dx12lib/Mesh.h>
#include <dx12lib/Material.h>
#include <dx12lib/MappableBuffer.h>
#include <dx12lib/Texture.h>
#include <dx12lib/RenderTarget.h>

using namespace dx12lib;

ShaderTableResourceView::ShaderTableResourceView( Device& device, const std::shared_ptr<Resource>& outputResource,
                                                  const D3D12_UNORDERED_ACCESS_VIEW_DESC* pOutputUav,
                                                  const D3D12_SHADER_RESOURCE_VIEW_DESC*  pRayTlasSrv,
                                                  const D3D12_CONSTANT_BUFFER_VIEW_DESC*  pCbv )
: m_Device( device )
{
    assert( pOutputUav || pRayTlasSrv || pCbv );

    auto d3d12Device          = m_Device.GetD3D12Device();
    auto d3d12Resource = outputResource ? outputResource->GetD3D12Resource() : nullptr;

    if ( outputResource )
    {
        auto d3d12ResourceDesc = outputResource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 3;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;


    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );
    
    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    d3d12Device->CreateUnorderedAccessView( d3d12Resource.Get(), nullptr, pOutputUav, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateConstantBufferView( pCbv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );

}

ShaderTableResourceView::ShaderTableResourceView( Device& device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbv )
: m_Device( device )
{
    assert( pCbv );

    auto d3d12Device   = m_Device.GetD3D12Device();

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 1;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );

    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    d3d12Device->CreateConstantBufferView( pCbv, heapHandle );
}


ShaderTableResourceView::ShaderTableResourceView( Device& device, 
                                                  const uint32_t nbrRenderTargets,
                                                  const RenderTarget*                     pRenderTargets,
                                                  const D3D12_UNORDERED_ACCESS_VIEW_DESC* pOutputUav,
                                                  const D3D12_SHADER_RESOURCE_VIEW_DESC*  pRayTlasSrv,
                                                  const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbv, Scene* pMeshes )
: m_Device( device )
{
    assert( pOutputUav || pRayTlasSrv || pCbv );

    auto d3d12Device   = m_Device.GetD3D12Device();
    auto nbrMeshes     = pMeshes->m_Meshes.size();
    auto nbrMaterials  = pMeshes->m_Materials.size();

    auto nbrTextures = pMeshes->GetDiffuseTextureCount() + pMeshes->GetNormalTextureCount() 
        + pMeshes->GetSpecularTextureCount() + pMeshes->GetMaskTextureCount();

    for (int i = 0; i < nbrRenderTargets; ++i) 
    {
        AttachmentPoint point           = static_cast<AttachmentPoint>( i );
        auto            textureResource = pRenderTargets->GetTexture( point );
        if ( textureResource )
        {
            auto d3d12ResourceDesc = textureResource->GetD3D12ResourceDesc();

            // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
            assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
        }
    }
    

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    // UAV targets, PER FRAME CBV, SRV TLAS, SRV per idxBuff & vertBuff, MaterialList, SRV textures
    desc.NumDescriptors = nbrRenderTargets + 5 + 2 * nbrMeshes + 1 + nbrTextures;
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );

    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    // ray gen root sig + TLAS
    {
        // Output targets
        for ( int i = 0; i < nbrRenderTargets; ++i )
        {
            auto textureResource = pRenderTargets->GetTexture( static_cast<AttachmentPoint>( i ) );

            d3d12Device->CreateUnorderedAccessView( textureResource->GetD3D12Resource().Get(),
                nullptr, pOutputUav, heapHandle );

            heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        }
        


        // Per frame buffer - Constant globals buffer - camera info etc
        d3d12Device->CreateConstantBufferView( &pCbv[0], heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        d3d12Device->CreateConstantBufferView( &pCbv[1], heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }

    // miss shader
    {
        // Diffuse map
        auto resource = pMeshes->skyboxDiffuse.get();

        D3D12_SHADER_RESOURCE_VIEW_DESC texCopy = {};
        texCopy.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURECUBE;
        texCopy.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texCopy.Texture2D.MipLevels             = (UINT)-1;
        texCopy.Texture2D.MostDetailedMip       = 0;
        texCopy.Format = resource ? resource->GetD3D12ResourceDesc().Format : DXGI_FORMAT_R32G32B32A32_FLOAT;

        d3d12Device->CreateShaderResourceView( resource ? resource->GetD3D12Resource().Get() : nullptr, &texCopy, heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );


        // Radiance map
        auto radianceMap = pMeshes->skyboxIntensity.get();

        D3D12_SHADER_RESOURCE_VIEW_DESC radianceDesc  = {};
        radianceDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURECUBE;
        radianceDesc.Shader4ComponentMapping          = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        radianceDesc.Texture2D.MipLevels              = (UINT)-1;
        radianceDesc.Texture2D.MostDetailedMip        = 0;
        radianceDesc.Format = radianceMap ? radianceMap->GetD3D12ResourceDesc().Format : DXGI_FORMAT_R32G32B32A32_FLOAT;

        d3d12Device->CreateShaderResourceView( radianceMap ? radianceMap->GetD3D12Resource().Get() : nullptr, &radianceDesc, heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }

    // Index and vertes buffers SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC copy = {};
        copy.Format                          = DXGI_FORMAT_R32_TYPELESS;
        copy.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;

        copy.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;
        copy.Buffer.StructureByteStride = 0;
        copy.Buffer.FirstElement        = 0;

        copy.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> srvIdxDesc;
        srvIdxDesc.resize( nbrMeshes );

        std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> srvVertDesc;
        srvVertDesc.resize( nbrMeshes );

        // define SRV for index and vert buffers
        for ( int i = 0; i < nbrMeshes; ++i )
        {
            srvIdxDesc[i] = copy;
            srvIdxDesc[i].Buffer.NumElements =
                // Frome byte size to Nbr R32 size
                pMeshes->m_Meshes[i]->GetIndexBuffer()->GetIndexBufferView().SizeInBytes / sizeof( float );

            srvVertDesc[i] = copy;
            srvVertDesc[i].Buffer.NumElements =
                // Frome byte size to Nbr R32 size
                pMeshes->m_Meshes[i]->GetVertexBuffer( 0 )->GetVertexBufferView().SizeInBytes / sizeof( float );
        }

        // Create index buffers SRV
        for ( int i = 0; i < nbrMeshes; ++i )
        {
            d3d12Device->CreateShaderResourceView( pMeshes->m_Meshes[i]->GetIndexBuffer()->GetD3D12Resource().Get(),
                                                   &srvIdxDesc[i], heapHandle );

            heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }

        // Create vertex buffers SRV
        for ( int i = 0; i < nbrMeshes; ++i )
        {
            d3d12Device->CreateShaderResourceView( pMeshes->m_Meshes[i]->GetVertexBuffer( 0 )->GetD3D12Resource().Get(),
                                                   &srvVertDesc[i], heapHandle );

            heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }
    }


    
    // define lists of diffuse, normal, and specular textures
    // used for material propert list AND texture SRV
    std::vector<dx12lib::Texture*> _diffuseTextureList;
    _diffuseTextureList.resize( pMeshes->GetDiffuseTextureCount() );
    std::copy( pMeshes->_diffuse.begin(), pMeshes->_diffuse.end(), _diffuseTextureList.begin() );

    std::vector<dx12lib::Texture*> _normalTextureList;
    _normalTextureList.resize( pMeshes->GetNormalTextureCount() );
    std::copy( pMeshes->_normal.begin(), pMeshes->_normal.end(), _normalTextureList.begin() );

    std::vector<dx12lib::Texture*> _specularTextureList;
    _specularTextureList.resize( pMeshes->GetSpecularTextureCount() );
    std::copy( pMeshes->_specular.begin(), pMeshes->_specular.end(), _specularTextureList.begin() );

    std::vector<dx12lib::Texture*> _maskTextureList;
    _maskTextureList.resize( pMeshes->GetMaskTextureCount() );
    std::copy( pMeshes->_opacity.begin(), pMeshes->_opacity.end(), _maskTextureList.begin() );

    // define CBV buffer for material list
    {
        std::vector<RayMaterialProp> matPropList;
        matPropList.resize( nbrMeshes );

        size_t matListBuffSize = nbrMeshes * sizeof( RayMaterialProp );
        m_MaterialBuffer       = m_Device.CreateMappableBuffer( matListBuffSize );
        m_MaterialBuffer->SetName( L"DXR Geometry Material Map" );

        RayMaterialProp defaultMaterialSettings;

        for ( int i = 0; i < nbrMeshes; ++i )
        {
            auto mat = pMeshes->m_Meshes[i]->GetMaterial();

            matPropList[i]                   = defaultMaterialSettings;

            matPropList[i].Diffuse.x         = mat->GetDiffuseColor().x;
            matPropList[i].Diffuse.y         = mat->GetDiffuseColor().y;
            matPropList[i].Diffuse.z         = mat->GetDiffuseColor().z;

            matPropList[i].Type = mat->GetMaterialType();

            // second
            auto tex = mat->GetTexture( Material::TextureType::Diffuse );
            if ( tex )
            {
                std::vector<dx12lib::Texture*>::iterator itr =
                    std::find( _diffuseTextureList.begin(), _diffuseTextureList.end(), tex.get() );
                matPropList[i].DiffuseTextureIdx = std::distance( _diffuseTextureList.begin(), itr );
            }

            tex = mat->GetTexture( Material::TextureType::Normal );
            if ( tex )
            {
                std::vector<dx12lib::Texture*>::iterator itr =
                    std::find( _normalTextureList.begin(), _normalTextureList.end(), tex.get() );
                matPropList[i].NormalTextureIdx = std::distance( _normalTextureList.begin(), itr );
            }

            tex = mat->GetTexture( Material::TextureType::Specular );
            if ( tex )
            {
                std::vector<dx12lib::Texture*>::iterator itr =
                    std::find( _specularTextureList.begin(), _specularTextureList.end(), tex.get() );
                matPropList[i].SpecularTextureIdx = std::distance( _specularTextureList.begin(), itr );
            }

            tex = mat->GetTexture( Material::TextureType::Opacity );
            if ( tex )
            {
                std::vector<dx12lib::Texture*>::iterator itr =
                    std::find( _maskTextureList.begin(), _maskTextureList.end(), tex.get() );
                matPropList[i].MaskTextureIdx = std::distance( _maskTextureList.begin(), itr );
            }

            // third
            matPropList[i].Emittance.x = mat->GetEmissiveColor().x;
            matPropList[i].Emittance.y = mat->GetEmissiveColor().y;
            matPropList[i].Emittance.z = mat->GetEmissiveColor().z;

            // fourth
            matPropList[i].Specular =
                ( 1 / 3.0f ) * ( mat->GetSpecularColor().x + mat->GetSpecularColor().y + mat->GetSpecularColor().z );
            matPropList[i].IndexOfRefraction = mat->GetIndexOfRefraction();

        }

        

        // transfer the data to the GPU
        void* pData;
        ThrowIfFailed( m_MaterialBuffer->Map( &pData ) );
        {
            memcpy( pData, matPropList.data(), matListBuffSize );
        }
        m_MaterialBuffer->Unmap();

        // create view
        D3D12_SHADER_RESOURCE_VIEW_DESC copy = {};
        copy.Format                          = DXGI_FORMAT_R32_TYPELESS;
        copy.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;

        copy.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;
        copy.Buffer.StructureByteStride = 0;
        copy.Buffer.FirstElement        = 0;
        // size in numbers of R32 Typeless
        copy.Buffer.NumElements         = matListBuffSize / sizeof( float );

        copy.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;



        d3d12Device->CreateShaderResourceView( m_MaterialBuffer->GetD3D12Resource().Get(), &copy, heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
    

    // texture SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC texCopy = {};
        texCopy.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
        texCopy.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texCopy.Texture2D.MipLevels             = (UINT)-1;
        texCopy.Texture2D.MostDetailedMip       = 0;

        std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> srvTextures;
        srvTextures.resize( nbrTextures );

        // Create Diffuse Texture buffers
        unsigned int texIdx = 0;
        for ( dx12lib::Texture* tex : _diffuseTextureList )
        {
            unsigned int idx = texIdx++;
            srvTextures[idx] = texCopy;

            srvTextures[idx].Format = tex->GetD3D12ResourceDesc().Format;

            d3d12Device->CreateShaderResourceView( tex->GetD3D12Resource().Get(), &srvTextures[idx], heapHandle );

            heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }

        for ( dx12lib::Texture* tex: _normalTextureList )
        {
            unsigned int idx = texIdx++;
            srvTextures[idx] = texCopy;

            srvTextures[idx].Format = tex->GetD3D12ResourceDesc().Format;

            d3d12Device->CreateShaderResourceView( tex->GetD3D12Resource().Get(), &srvTextures[idx], heapHandle );

            heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }

        for ( dx12lib::Texture* tex: _specularTextureList )
        {
            unsigned int idx = texIdx++;
            srvTextures[idx] = texCopy;

            srvTextures[idx].Format = tex->GetD3D12ResourceDesc().Format;

            d3d12Device->CreateShaderResourceView( tex->GetD3D12Resource().Get(), &srvTextures[idx], heapHandle );

            heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }

        for ( dx12lib::Texture* tex: _maskTextureList )
        {
            unsigned int idx = texIdx++;
            srvTextures[idx] = texCopy;

            srvTextures[idx].Format = tex->GetD3D12ResourceDesc().Format;

            d3d12Device->CreateShaderResourceView( tex->GetD3D12Resource().Get(), &srvTextures[idx], heapHandle );

            heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }
    }
}

/*
ShaderTableResourceView::ShaderTableResourceView( Device&                                device,
                                                    const D3D12_SHADER_RESOURCE_VIEW_DESC* pRayTlasSrv,
                                                  const std::shared_ptr<IndexBuffer>&    idxBuffRes,
                                                    const D3D12_SHADER_RESOURCE_VIEW_DESC* pIdxBuffSrv,
                                                  const std::shared_ptr<VertexBuffer>&   vertBuffRes,
                                                    const D3D12_SHADER_RESOURCE_VIEW_DESC* pVerBuffSrv )
: m_Device( device )
, m_Resource( nullptr )
, m_IndexBuff( idxBuffRes )
, m_VertBuff( vertBuffRes )
{
    assert( pIdxBuffSrv || pRayTlasSrv || pVerBuffSrv );

    auto d3d12Device   = m_Device.GetD3D12Device();
    auto d3d12ResourceVert = m_VertBuff ? m_VertBuff->GetD3D12Resource() : nullptr;
    auto d3d12ResourceIdx = m_IndexBuff ? m_IndexBuff->GetD3D12Resource() : nullptr;

    if ( m_Resource )
    {
        auto d3d12ResourceDesc = m_Resource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert( ( d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 );
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 3;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed( d3d12Device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_SrvUavHeap ) ) );
    m_SrvUavHeap->SetName( L"DXR Descriptor Heap" );

    D3D12_CPU_DESCRIPTOR_HANDLE heapHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    d3d12Device->CreateShaderResourceView( d3d12ResourceIdx.Get(), pIdxBuffSrv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateShaderResourceView( d3d12ResourceVert.Get(), pVerBuffSrv, heapHandle );

    heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );
}
*/