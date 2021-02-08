#include "DX12LibPCH.h"

#include <dx12lib/ShaderTable.h>

#include <vector>

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

    auto nbrTextures = pMeshes->nbrDiffuseTextures + pMeshes->nbrNormalTextures 
        + pMeshes->nbrSpecularTextures + pMeshes->nbrMaskTextures;

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
    desc.NumDescriptors = nbrRenderTargets + 2 + 2 * nbrMeshes + 1 + nbrTextures;
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
        


        // Per frame buffer - camera info etc
        d3d12Device->CreateConstantBufferView( pCbv, heapHandle );

        heapHandle.ptr += d3d12Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        d3d12Device->CreateShaderResourceView( nullptr, pRayTlasSrv, heapHandle );

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
    _diffuseTextureList.resize( pMeshes->nbrDiffuseTextures );

    std::vector<dx12lib::Texture*> _normalTextureList;
    _normalTextureList.resize( pMeshes->nbrNormalTextures );

    std::vector<dx12lib::Texture*> _specularTextureList;
    _specularTextureList.resize( pMeshes->nbrSpecularTextures );

    std::vector<dx12lib::Texture*> _maskTextureList;
    _maskTextureList.resize( pMeshes->nbrMaskTextures );

    // define CBV buffer for material list
    {
        std::vector<RayMaterialProp> matPropList;
        matPropList.resize( nbrMeshes );

        size_t matListBuffSize = nbrMeshes * sizeof( RayMaterialProp );
        m_MaterialBuffer       = m_Device.CreateMappableBuffer( matListBuffSize );
        m_MaterialBuffer->SetName( L"DXR Geometry Material Map" );

        int diffuseTexIdx = 0, normalTexIdx = 0, specularTexIdx = 0, maskTexIdx = 0;

        RayMaterialProp defaultMaterialSettings;

        for ( int i = 0; i < nbrMeshes; ++i )
        {
            auto mat = pMeshes->m_Meshes[i]->GetMaterial();

            matPropList[i]                   = defaultMaterialSettings;

            matPropList[i].Diffuse.x         = mat->GetDiffuseColor().x;
            matPropList[i].Diffuse.y         = mat->GetDiffuseColor().y;
            matPropList[i].Diffuse.z         = mat->GetDiffuseColor().z;

            matPropList[i].IndexOfReflection = mat->GetIndexOfRefraction();

            auto tex = mat->GetTexture( Material::TextureType::Diffuse );
            if ( tex )
            {
                matPropList[i].DiffuseTextureIdx     = diffuseTexIdx;
                _diffuseTextureList[diffuseTexIdx++] = tex.get();
            }

            tex = mat->GetTexture( Material::TextureType::Normal );
            if ( tex )
            {
                matPropList[i].NormalTextureIdx    = normalTexIdx;
                _normalTextureList[normalTexIdx++] = tex.get();
            }

            tex = mat->GetTexture( Material::TextureType::Specular );
            if ( tex )
            {
                matPropList[i].SpecularTextureIdx      = specularTexIdx;
                _specularTextureList[specularTexIdx++] = tex.get();
            }

            tex = mat->GetTexture( Material::TextureType::Opacity );
            if ( tex )
            {
                matPropList[i].MaskTextureIdx      = maskTexIdx;
                _maskTextureList[maskTexIdx++] = tex.get();
            }

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