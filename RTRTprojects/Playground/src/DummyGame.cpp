#include <DummyGame.h>


#include <DirectXMath.h>

#include <dx12lib/CommandList.h>
#include <dx12lib/CommandQueue.h>
#include <dx12lib/Device.h>
#include <dx12lib/GUI.h>
#include <dx12lib/Helpers.h>
#include <dx12lib/Mesh.h>
#include <dx12lib/PipelineStateObject.h>
#include <dx12lib/RootSignature.h>
#include <dx12lib/Scene.h>
#include <dx12lib/SceneNode.h>
#include <dx12lib/SwapChain.h>
#include <dx12lib/Texture.h>
#include <dx12lib/Visitor.h>
#include <dx12lib/UnorderedAccessView.h>
#include <dx12lib/ShaderResourceView.h>

#include <dx12lib/Material.h>
#include <dx12lib/Texture.h>

#include <dx12lib/AccelerationStructure.h>
#include <dx12lib/RT_PipelineStateObject.h>
#include <dx12lib/MappableBuffer.h>
#include <dx12lib/ShaderTable.h>

#include <dx12lib/IndexBuffer.h>
#include <dx12lib/VertexBuffer.h>

#include <dxcapi.h>

#include <GameFramework/Window.h>

#include <wrl/client.h>
using namespace Microsoft::WRL;

#include <DirectXColors.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>

using namespace dx12lib;
using namespace DirectX;

#include <math.h>
#include <algorithm>  // For std::min, std::max, and std::clamp.
#include <random>


DummyGame::DummyGame( const std::wstring& name, int width, int height, bool vSync )
: m_ScissorRect( CD3DX12_RECT( 0, 0, LONG_MAX, LONG_MAX ) )
, m_Forward( 0 )
, m_Backward( 0 )
, m_Left( 0 )
, m_Right( 0 )
, m_Up( 0 )
, m_Down( 0 )
, m_Pitch( 0 )
, m_Yaw( 0 )
, m_Width( width )
, m_Height( height )
, m_VSync( vSync )
, m_Fullscreen( false )
, m_RenderScale( 1.0f )
, m_CamWindow( width / (float)height, 1 )
, m_CamPos( 50, 50, 0 )
, m_frameData( XMFLOAT3( 50, 50, 0 ), XMFLOAT3( -50, 50, 0 ), XMFLOAT2(width / (float)height, 1) )
, m_Globals(10)
{
    m_Logger = GameFramework::Get().CreateLogger( "DummyGame" );
    m_Window = GameFramework::Get().CreateWindow( name, width, height );

    m_Window->Update += UpdateEvent::slot( &DummyGame::OnUpdate, this );
    m_Window->KeyPressed += KeyboardEvent::slot( &DummyGame::OnKeyPressed, this );
    m_Window->KeyReleased += KeyboardEvent::slot( &DummyGame::OnKeyReleased, this );
    m_Window->MouseMoved += MouseMotionEvent::slot( &DummyGame::OnMouseMoved, this );
    m_Window->MouseWheel += MouseWheelEvent::slot( &DummyGame::OnMouseWheel, this );
    m_Window->Resize += ResizeEvent::slot( &DummyGame::OnResize, this );
    m_Window->DPIScaleChanged += DPIScaleEvent::slot( &DummyGame::OnDPIScaleChanged, this );


    UpdateCamera( ( m_Left - m_Right ) * cam_speed , ( m_Up - m_Down ) * cam_speed ,
                  ( m_Forward - m_Backward ) * cam_speed );

}

DummyGame::~DummyGame()
{

}

uint32_t DummyGame::Run()
{
    LoadContent();

    m_Window->Show();

    uint32_t retCode = GameFramework::Get().Run();

    UnloadContent();

    return retCode;
}

#if RAY_TRACER

static const WCHAR* kRayGenShader     = L"rayGen";

static const WCHAR* kStandardChs        = L"standardChs";
static const WCHAR* kStandardMiss       = L"standardMiss";
static const WCHAR* kStandardHitGroup   = L"standardHitGroup";

void DummyGame::CreateRayTracingPipeline() {
    // Need 10 subobjects:
    //  1 for the DXIL library
    //  2 for hit-group
    //  2 for RayGen root-signature (root-signature and the subobject association)
    //  2 for the miss root-signature (signature and association)
    //  2 for the SPHERE hit root-signature (signature and association)
    //  2 for the PLANE hit root-signature (signature and association)
    //  2 for shader config (shared between all programs. 1 for the config, 1 for association)
    //  1 for pipeline config
    //  1 for the global root signature
    std::array<D3D12_STATE_SUBOBJECT, 15> subobjects;
    uint32_t                              index = 0;

    // Load 
    ComPtr<IDxcBlob> shaders =
        ShaderHelper::CompileLibrary( L"RTRTprojects/Playground/shaders/RayTracer.hlsl", L"lib_6_5" );
    const WCHAR* entryPoints[] = { kRayGenShader, kStandardMiss, kStandardChs };  // SIZE 3
    
    DxilLibrary      dxilLib( shaders.Get(), entryPoints, 3 );
    subobjects[index++] = dxilLib.stateSubobject; 

    // Normal Hit Group 
    HitProgram hitProgram( nullptr, kStandardChs, kStandardHitGroup );
    subobjects[index++] = hitProgram.subObject; 


    // Create the ray-gen root-signature and association
    {

        CD3DX12_DESCRIPTOR_RANGE1 ranges[4] = {};
        size_t                    rangeIdx           = 0;
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, m_nbrRayRenderTargets, 0, 0,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0 );
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                 m_nbrRayRenderTargets );
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                 m_nbrRayRenderTargets + 1 );
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                 m_nbrRayRenderTargets + 2 );


        CD3DX12_ROOT_PARAMETER1 rayRootParams[1] = {};

        rayRootParams[0].InitAsDescriptorTable( rangeIdx, ranges );

        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( 1, rayRootParams, 0, nullptr, rootSignatureFlags );

        m_RayGenRootSig = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

        
        ID3D12RootSignature* pRayInterface = m_RayGenRootSig->GetD3D12RootSignature().Get();

        D3D12_STATE_SUBOBJECT rayGenSubobject = {};
        rayGenSubobject.Type                  = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
        rayGenSubobject.pDesc                 = &pRayInterface;

        subobjects[index] = rayGenSubobject;  // 2 RayGen Root Sig
    }

    uint32_t          rgsRootIndex = index++;  // 2
    ExportAssociation rgsRootAssociation( &kRayGenShader, 1, &( subobjects[rgsRootIndex] ) );
    subobjects[index++] = rgsRootAssociation.subobject;  // 3 Associate Root Sig to RGS


    // Create the HIT-programs root-signature      
    {
        std::vector<CD3DX12_DESCRIPTOR_RANGE1> ranges;
        // TLAS + Idx + Vert + MatProp + Diffuse
        size_t rangeSize = 9;

        ranges.resize( rangeSize );

        size_t                    rangeIdx = 0;
        size_t                    offset    = m_nbrRayRenderTargets + 1;
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_NONE, offset );
        offset += 1;

        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_NONE, offset );
        offset += 1;

        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_TotalGeometryCount, 1, 0,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += m_TotalGeometryCount;

        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_TotalGeometryCount, 1, 1,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += m_TotalGeometryCount;

        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 2,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += 1;

        // temp values
        unsigned int nDiffuseDesc = m_TotalDiffuseTexCount > 0 ? m_TotalDiffuseTexCount : 1;
        unsigned int nNormalDesc   = m_TotalNormalTexCount > 0 ? m_TotalNormalTexCount : 1;
        unsigned int nSpecularDesc = m_TotalSpecularTexCount > 0 ? m_TotalSpecularTexCount : 1;
        unsigned int nMaskDesc     = m_TotalMaskTexCount > 0 ? m_TotalMaskTexCount : 1;

        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nDiffuseDesc, 1, 3,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += nDiffuseDesc;

        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nNormalDesc, 1, 4,
                                    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += nNormalDesc;
         
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nSpecularDesc, 1, 5,
                                    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += nSpecularDesc;
         
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nMaskDesc, 1, 6,
                                    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += nMaskDesc;
         

        CD3DX12_ROOT_PARAMETER1 rayRootParams[2] = {};
        rayRootParams[0].InitAsDescriptorTable( rangeIdx, ranges.data() );
        rayRootParams[1].InitAsConstantBufferView( 2 ); 

        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        CD3DX12_STATIC_SAMPLER_DESC samplers[2];
        samplers[0].Init( 0 );
        samplers[1].Init( 1, D3D12_FILTER_MIN_MAG_MIP_POINT );

        CD3DX12_STATIC_SAMPLER_DESC myTextureSampler( 0 );

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( 2, rayRootParams, 2, samplers, rootSignatureFlags );

        m_StdHitRootSig = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

        ID3D12RootSignature*  pHitInterface = m_StdHitRootSig->GetD3D12RootSignature().Get();
        D3D12_STATE_SUBOBJECT hitSubobject  = {};
        hitSubobject.Type                   = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
        hitSubobject.pDesc                  = &pHitInterface;
        subobjects[index]                   = hitSubobject;
        // END HIT                                      PLANE
    }

    // Hit associations
    uint32_t          planeHitRootIndex    = index++;                // 4
    const WCHAR*      planeHitExportName[] = { kStandardChs };  
    ExportAssociation planeHitRootAssociation( planeHitExportName, 1, &( subobjects[planeHitRootIndex] ) );
    subobjects[index++] = planeHitRootAssociation.subobject;  // 5 Associate Root Sig to Miss and CHS


        // Create the EMPTY-programs root-signature
        {
            D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

            CD3DX12_ROOT_PARAMETER1 rayRootParams[1] = {};

            rayRootParams[0].InitAsConstantBufferView( 0 );

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
            rootSignatureDescription.Init_1_1( 1, rayRootParams, 0, nullptr, rootSignatureFlags );

            m_EmptyLocalRootSig = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

            ID3D12RootSignature*  pEmptyInterface = m_EmptyLocalRootSig->GetD3D12RootSignature().Get();
            D3D12_STATE_SUBOBJECT emptySubobject  = {};
            emptySubobject.Type                   = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            emptySubobject.pDesc                  = &pEmptyInterface;

            subobjects[index] = emptySubobject;
        }

        // EMPTY associations
        uint32_t          emptyRootIndex    = index++;   
        const WCHAR*      emptyExportName[] = { kStandardMiss };  
        ExportAssociation emptyRootAssociation( emptyExportName, 1, &( subobjects[emptyRootIndex] ) );
        subobjects[index++] = emptyRootAssociation.subobject;



    // Bind the payload size to the programs
    ShaderConfig shaderConfig( sizeof( float ) * 2, sizeof( float ) * (3*5 + 3 + 3) );
    subobjects[index] = shaderConfig.subobject;

    uint32_t          shaderConfigIndex = index++;  
    const WCHAR*      shaderExports[]   = { kStandardMiss, kStandardChs, kRayGenShader }; 
    ExportAssociation configAssociation( shaderExports, 3, &( subobjects[shaderConfigIndex] ) );
    subobjects[index++] = configAssociation.subobject;  


        // Create the pipeline config, per bounce, 1 reflection, 1 refraction
    PipelineConfig config( m_Globals.nbrBouncesPerPath * m_Globals.nbrRaysPerBounce ); 
        subobjects[index++] = config.subobject;  // 8

    // Create the GLOBAL root signature and store the empty signature
    {
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( 0, nullptr, 0, nullptr, rootSignatureFlags );

        m_GlobalRootSig = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

        ID3D12RootSignature*  pGlobalInterface     = m_GlobalRootSig->GetD3D12RootSignature().Get();
        D3D12_STATE_SUBOBJECT emptyGlobalSubobject = {};
        emptyGlobalSubobject.Type                  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        emptyGlobalSubobject.pDesc                 = &pGlobalInterface;

        subobjects[index++] = emptyGlobalSubobject;  // 9
    }
    
    // END GLOBAL

    m_RayPipelineState = m_Device->CreateRayPipelineState( index, subobjects.data() );

}

void DummyGame::CreateShaderResource( DXGI_FORMAT backBufferFormat )
{
    D3D12_RESOURCE_DESC renderDesc = CD3DX12_RESOURCE_DESC::Tex2D( backBufferFormat, m_Width, m_Height, 1, 1 );
    renderDesc.Format              = backBufferFormat;
    renderDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; 

    auto rayImage = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayImage->SetName( L"RayGen diffuse output texture" );

    auto rayAlbedo = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayAlbedo->SetName( L"RayGen albedo output texture" );

    auto rayNormals = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayNormals->SetName( L"RayGen normal output texture" );

    auto rayDepth = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayDepth->SetName( L"RayGen depth output texture" );

    auto rayPos = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayPos->SetName( L"RayGen position output texture" );

    auto rayObjID = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayObjID->SetName( L"RayGen object ID output texture" );

    auto rayMetal = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayMetal->SetName( L"RayGen metalness output texture" );

    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color0, rayImage );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color1, rayAlbedo );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color2, rayNormals );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color3, rayDepth );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color4, rayPos );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color5, rayObjID );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color6, rayMetal );

    // Create an off-screen render for the compute shader
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;

    // Create SRV for TLAS after the UAV above. 
    D3D12_SHADER_RESOURCE_VIEW_DESC srvTlasDesc          = {};
    srvTlasDesc.ViewDimension                        = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvTlasDesc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvTlasDesc.RaytracingAccelerationStructure.Location =  m_TlasBuffers.pResult->GetD3D12Resource()->GetGPUVirtualAddress();


    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc[2] = {};
    cbvDesc[0].SizeInBytes                     = 256;
    cbvDesc[0].BufferLocation                  = m_FrameDataCB->GetD3D12Resource()->GetGPUVirtualAddress();

    cbvDesc[1].SizeInBytes                   = align_to( 256, sizeof( GlobalConstantData ) );
    cbvDesc[1].BufferLocation                = m_GlobalCB->GetD3D12Resource()->GetGPUVirtualAddress();

    m_RayShaderHeap = m_Device->CreateShaderTableView( m_nbrRayRenderTargets, &m_RayRenderTarget, &uavDesc, &srvTlasDesc, cbvDesc, m_RaySceneMesh.get() );
}

void DummyGame::CreateAccelerationStructure() 
{

    auto& commandQueueDirect = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_DIRECT );
    auto  commandList        = commandQueueDirect.GetCommandList();

    AccelerationStructure blasBuffers = {};
    m_RaySceneMesh->BuildBottomLevelAccelerationStructure( m_Device.get(), commandList.get(), &blasBuffers );

    // Init based on acceleration structure
    {
        m_Instances = 1;

        m_InstanceTransforms.resize( m_Instances );

        m_GeometryCountPerInstance.resize( m_Instances );
        m_DiffuseTexCountPerInstance.resize( m_Instances );
        m_NormalTexCountPerInstance.resize( m_Instances );
        m_SpecularTexCountPerInstance.resize( m_Instances );
        m_MaskTexCountPerInstance.resize( m_Instances );

        m_GeometryCountPerInstance[0] = m_RaySceneMesh->GetGeometryCount();
        m_DiffuseTexCountPerInstance[0] = m_RaySceneMesh->GetDiffuseTextureCount();
        m_NormalTexCountPerInstance[0]  = m_RaySceneMesh->GetNormalTextureCount();
        m_SpecularTexCountPerInstance[0] = m_RaySceneMesh->GetSpecularTextureCount();
        m_MaskTexCountPerInstance[0]     = m_RaySceneMesh->GetMaskTextureCount();

        m_TotalGeometryCount = 0;
        m_TotalDiffuseTexCount = 0;
        m_TotalNormalTexCount  = 0;
        m_TotalSpecularTexCount = 0;
        m_TotalMaskTexCount     = 0;

        for ( int i = 0; i < m_Instances; ++i )
        {
            m_TotalGeometryCount += m_GeometryCountPerInstance[i];
            m_TotalDiffuseTexCount += m_DiffuseTexCountPerInstance[i];
            m_TotalNormalTexCount += m_NormalTexCountPerInstance[i];
            m_TotalSpecularTexCount += m_SpecularTexCountPerInstance[i];
            m_TotalMaskTexCount += m_MaskTexCountPerInstance[i];

            float scale             = m_RaySceneMesh->GetSceneScale();

            m_InstanceTransforms[0] = InstanceTransforms( XMFLOAT3( scale, scale, scale ) );
        }

    }

    // Create instances
    {
        m_InstanceDescBuffer = m_Device->CreateMappableBuffer( m_Instances * sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
        m_InstanceDescBuffer->SetName( L"DXR TLAS Instance Description" );

        D3D12_RAYTRACING_INSTANCE_DESC* pInstDesc;
        // Map INSTANCES, their TRANSFORMS, and respective BLAS.
        ThrowIfFailed( m_InstanceDescBuffer->Map( (void**)&pInstDesc ) ); 
        {
            // instance 0 - sphere AND plane
            pInstDesc[0].InstanceID                              = 0;
            pInstDesc[0].InstanceContributionToHitGroupIndex     = 0;
            pInstDesc[0].Flags                                   = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

            memcpy( &pInstDesc[0].Transform, &m_InstanceTransforms[0].matrix, sizeof( XMFLOAT3X4 ) );

            // select the BLAS we build on.
            pInstDesc[0].AccelerationStructure = blasBuffers.pResult->GetD3D12Resource()->GetGPUVirtualAddress();
            pInstDesc[0].InstanceMask          = 0xFF;
        }
        m_InstanceDescBuffer->Unmap();

    }

    AccelerationBuffer::CreateTopLevelAS( m_Device.get(), commandList.get(), 
        &mTlasSize, &m_TlasBuffers, m_Instances, m_InstanceDescBuffer.get() 
    );

    commandQueueDirect.ExecuteCommandList( commandList );
    commandQueueDirect.Flush();

    m_BLAS = blasBuffers.pResult;
}


void DummyGame::UpdateConstantBuffer()
{
    void* pData;
    ThrowIfFailed( m_InstanceTransformResources->Map( &pData ) );
    {
        memcpy( pData, &m_InstanceTransforms[0], sizeof( InstanceTransforms ) );
    }
    m_InstanceTransformResources->Unmap();
}

void DummyGame::CreateConstantBuffer() 
{
    m_FrameDataCB = m_Device->CreateMappableBuffer( 256 );

    m_GlobalCB = m_Device->CreateMappableBuffer( align_to( 256, sizeof( GlobalConstantData ) ) );

    // Upload data
    void* pData;
    ThrowIfFailed( m_GlobalCB->Map( &pData ) );
    {
        memcpy( pData, &m_Globals, sizeof( GlobalConstantData ) );
    }
    m_GlobalCB->Unmap();

    m_InstanceTransformResources = m_Device->CreateMappableBuffer( sizeof( InstanceTransforms ) );
    UpdateConstantBuffer();
}

void DummyGame::CreateShaderTable()
{
    uint32_t shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    m_ShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    m_ShaderTableEntrySize += 2 * sizeof( UINT64 );  // extra 8 bytes (64 bits) for heap pointer to viewers.
    m_ShaderTableEntrySize = align_to( D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, m_ShaderTableEntrySize );

    uint32_t shaderBufferSize = m_ShaderTableEntrySize;
    shaderBufferSize          = align_to( D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, m_ShaderTableEntrySize );
    m_ShadersEntriesPerGeometry = 1;

    uint32_t perGeomShaderBuffSize = m_ShadersEntriesPerGeometry * shaderBufferSize;


    m_RaygenShaderTable = m_Device->CreateMappableBuffer( shaderBufferSize );
    m_RaygenShaderTable->SetName( L"DXR Raygen Shader Table" );

    m_MissShaderTable = m_Device->CreateMappableBuffer( m_ShadersEntriesPerGeometry *
                                                        shaderBufferSize );  // 2, one per primary ray
    m_MissShaderTable->SetName( L"DXR Miss Shader Table" );

    m_HitShaderTable = m_Device->CreateMappableBuffer(
        m_TotalGeometryCount * perGeomShaderBuffSize );  // 25, one per primary ray
    m_HitShaderTable->SetName( L"DXR Hit Shader Table" );

    ComPtr<ID3D12StateObjectProperties> pRtsoProps;
    m_RayPipelineState->GetD3D12PipelineState()->QueryInterface( IID_PPV_ARGS( &pRtsoProps ) );

    UINT64 descriptorHeapStart = m_RayShaderHeap->GetTableHeap()->GetGPUDescriptorHandleForHeapStart().ptr;
    UINT64 instanceTransformHeapStart = m_InstanceTransformResources->GetD3D12Resource()->GetGPUVirtualAddress();
    UINT64 perFrameDataHeapStart      = m_FrameDataCB->GetD3D12Resource()->GetGPUVirtualAddress();

    // ray gen shader
    void* pRayGenShdr = pRtsoProps->GetShaderIdentifier( kRayGenShader );

    // miss for regular
    void* pStandardMiss   = pRtsoProps->GetShaderIdentifier( kStandardMiss );

    // hit for regular
    void* pStandardHitGrp = pRtsoProps->GetShaderIdentifier( kStandardHitGroup );

    uint8_t* pData;
    ThrowIfFailed( m_RaygenShaderTable->GetD3D12Resource()->Map( 0, nullptr, (void**)&pData ) );  // Map
    {
        // Entry 0 - ray-gen program ID and descriptor data
        memcpy( pData, pRayGenShdr, shaderIdSize );

        // Entry 0.1 Parameter Heap pointer
        memcpy( pData + shaderIdSize, &descriptorHeapStart, sizeof( UINT64 ) );
    }
    m_RaygenShaderTable->Unmap();  // Unmap

    ThrowIfFailed( m_MissShaderTable->GetD3D12Resource()->Map( 0, nullptr, (void**)&pData ) );  // Map
    {
        // Entry 1 - miss program PRIMARY
        memcpy( pData, pStandardMiss, shaderIdSize );

        // Entry 1.1 Parameter Constant Buffer pointer
        memcpy( pData + shaderIdSize, &perFrameDataHeapStart, sizeof( UINT64 ) );  

    }
    m_MissShaderTable->Unmap();  // Unmap


    ThrowIfFailed( m_HitShaderTable->GetD3D12Resource()->Map( 0, nullptr, (void**)&pData ) );  // Map
    {
        // Instance 0, geometry I
        for ( int i = 0; i < m_TotalGeometryCount; ++i )
        {
            // Primary
            memcpy( pData + ( m_ShadersEntriesPerGeometry * i + 0 ) * shaderBufferSize, pStandardHitGrp,
                    shaderIdSize );  // regular closest hit
            memcpy( pData + ( m_ShadersEntriesPerGeometry * i + 0 ) * shaderBufferSize + shaderIdSize, 
                &descriptorHeapStart, sizeof( UINT64 ) );  
            memcpy( pData + ( m_ShadersEntriesPerGeometry * i + 0 ) * shaderBufferSize + shaderIdSize + sizeof( UINT64 ),
                &instanceTransformHeapStart, sizeof( UINT64 ) );  

        }
        
    }
    m_HitShaderTable->Unmap();  // Unmap
}

#endif


bool DummyGame::LoadContent()
{
    m_Device    = Device::Create(true);

    m_SwapChain = m_Device->CreateSwapChain( m_Window->GetWindowHandle(), DXGI_FORMAT_R8G8B8A8_UNORM );
    m_SwapChain->SetVSync( m_VSync );

    m_GUI = m_Device->CreateGUI( m_Window->GetWindowHandle(), m_SwapChain->GetRenderTarget() );

    // This magic here allows ImGui to process window messages.
    GameFramework::Get().WndProcHandler += WndProcEvent::slot( &GUI::WndProcHandler, m_GUI );

    auto& commandQueue = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_COPY );
    auto  commandList  = commandQueue.GetCommandList();

    m_DummyTexture = commandList->LoadTextureFromFile( L"Assets/Textures/Tree.png", true, false );
    

    // DISPLAY MESHES IN RAY TRACING
    #if 1
    
    //sphereMat->SetEmissiveColor( DirectX::XMFLOAT4( 1,1,1, 0 ) );

    //sphereMat->SetTexture( Material::TextureType::Diffuse, m_DummyTexture );

    #if 1

    // m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/AmazonLumberyard/interior.obj" );
    // m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/AmazonLumberyard/exterior.obj" );
    // m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/San_Miguel/san-miguel.obj" ); scene_scale = 30;
    // m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/San_Miguel/san-miguel-low-poly.obj" ); scene_scale = 30;
    m_RaySceneMesh   = commandList->LoadSceneFromFile( L"Assets/Models/CornellBox/CornellBox-Original.obj" ); scene_scale = 100;
    m_Globals.lightPositions[0] = DirectX::XMFLOAT4( 0, 1.980, 0, 0 );
    m_Globals.nbrActiveLights   = 1;
    #else
    m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/crytek-sponza/sponza_nobanner.obj" );
    // merge scenes

    m_frameData.atmosphere = DirectX::XMFLOAT4( .529, .808, .922, 1 );

    m_Globals.nbrActiveLights   = 5;
    m_Globals.lightPositions[0] = DirectX::XMFLOAT4( 1120, 200, 445, 0 );
    m_Globals.lightPositions[1] = DirectX::XMFLOAT4( 1120, 200, -405, 0 );
    m_Globals.lightPositions[2] = DirectX::XMFLOAT4( -1190, 200, 445, 0 );
    m_Globals.lightPositions[3] = DirectX::XMFLOAT4( -1190, 200, -405, 0 );
    m_Globals.lightPositions[4] = DirectX::XMFLOAT4( 0, 2000, 0, 0 ); // sun, up?

    for (int i = 0; i < 4; ++i) {

        auto pos         = DirectX::XMFLOAT3(
            m_Globals.lightPositions[i].x, 
            m_Globals.lightPositions[i].y,
            m_Globals.lightPositions[i].z 
        );
        
        auto m_RaySphere = commandList->CreateSphere( 45, 16u, pos );

        auto sphereMat = m_RaySphere->GetRootNode()->GetMesh( 0 )->GetMaterial();
        sphereMat->SetEmissiveColor( DirectX::XMFLOAT4( 0.5 + Math::random_double() * 0.5,
                                                        0.5 + Math::random_double() * 0.5,
                                                        0.5 + Math::random_double() * 0.5, 
                                                        0 ) 
        );

        m_RaySceneMesh->MergeScene( m_RaySphere );
    }
    #endif

    // Debug scene
    #else   // Debug Scene

    auto earth = commandList->LoadTextureFromFile( L"Assets/Textures/earth.dds" );
    auto UV    = commandList->LoadTextureFromFile( L"Assets/Textures/UV_Test_Pattern.png" );

    auto brick_diff = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/brick_diff.png" );
    auto brick_norm = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/brick_norm.png" );
    auto brick_spec = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/brick_spec.png" );

    auto cobble_diff = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/cobble_diff.png" );
    auto cobble_norm = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/cobble_norm.png" );
    auto cobble_spec = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/cobble_spec.png" );

    auto concrete_diff = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/concrete_diff.png" );
    auto concrete_norm = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/concrete_norm.png" );
    auto concrete_spec = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/concrete_spec.png" );

    auto wood_diff = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/wood_diff.png" );
    auto wood_norm = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/wood_norm.png" );

    auto trans_alpha_diff = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/alpha_diffuse.png" );
    auto trans_mask_diff  = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/plant_diff.tga" );
    auto trans_mask_mask  = commandList->LoadTextureFromFile( L"Assets/Textures/Selected_Textures/plant_mask.tga" );

    m_CamPos = DirectX::XMFLOAT3( 0, 200, -200 );
    m_frameData.atmosphere = DirectX::XMFLOAT4( .529, .808, .922, 1 );
    m_frameData.UpdateCamera( m_CamPos, DirectX::XMFLOAT3( 0, 0, 0 ), m_CamWindow );
    m_Yaw     = 90;
    m_Pitch   = -45;

    // "Empty scene"
    m_RaySceneMesh = commandList->CreateSphere( 1 );

    // Centre Sphere
    auto centreSphere = commandList->CreateSphere( 30 );
    auto tmpMatCentre = centreSphere->GetRootNode()->GetMesh( 0 )->GetMaterial();
    tmpMatCentre->SetTexture( Material::TextureType::Diffuse, earth );
    m_RaySceneMesh->MergeScene( centreSphere );

    // Playboard
    auto playboard = commandList->CreatePlane( 400, 400, XMFLOAT3( 0, -30, 0 ) );
    auto tmpMatPlayboard    = playboard->GetRootNode()->GetMesh( 0 )->GetMaterial();
    tmpMatPlayboard->SetTexture( Material::TextureType::Diffuse, wood_diff );
    tmpMatPlayboard->SetTexture( Material::TextureType::Normal, wood_norm );
    m_RaySceneMesh->MergeScene( playboard );

    // Added material spheres
    const int subspheres = 10;
    for ( int i = 0; i <= subspheres - 1; ++i ) 
    {
        float alpha     = i * Math::_2PI / ( (float) subspheres ) ;
        float radius    = 100;
        std::shared_ptr<dx12lib::Scene> tmpScene;

        bool isSphere = Math::random_double() > 0.5;

        if ( isSphere )
            tmpScene = commandList->CreateSphere( 10, 32u, XMFLOAT3( radius * std::cos( alpha ), 0, radius * std::sin( alpha ) ) );
        else
            tmpScene = commandList->CreateCube( 20, XMFLOAT3( radius * std::cos( alpha ), 0, radius * std::sin( alpha ) ) );

        auto tmpMat = tmpScene->GetRootNode()->GetMesh( 0 )->GetMaterial();

        int texture = (int) std::round( Math::random_double() * 4 );
        
        switch ( texture )
        {
        case 0:
            tmpMat->SetTexture( Material::TextureType::Diffuse, brick_diff );
            tmpMat->SetTexture( Material::TextureType::Normal, brick_norm );
            tmpMat->SetTexture( Material::TextureType::Specular, brick_spec );
            break;
        case 1:
            tmpMat->SetTexture( Material::TextureType::Diffuse,     cobble_diff );
            tmpMat->SetTexture( Material::TextureType::Normal,      cobble_norm );
            tmpMat->SetTexture( Material::TextureType::Specular,    cobble_spec );
            break;
        case 2:
            tmpMat->SetTexture( Material::TextureType::Diffuse,     concrete_diff );
            tmpMat->SetTexture( Material::TextureType::Normal,      concrete_norm );
            tmpMat->SetTexture( Material::TextureType::Specular,    concrete_spec );
            break;
        case 3:
            tmpMat->SetTexture( Material::TextureType::Diffuse, UV );
            break;
        default:
        case 4:
            tmpMat->SetDiffuseColor( XMFLOAT4( Math::random_double(), Math::random_double(), Math::random_double(), 0 ) );
            break;
        }

        m_RaySceneMesh->MergeScene( tmpScene );
    }

    // Added transparent objects
    auto maskPlane       = commandList->CreatePlane( 100, 100, XMFLOAT3( -150, 70, 0 ) );
    auto tmpMatMaskPlane = maskPlane->GetRootNode()->GetMesh( 0 )->GetMaterial();
    tmpMatMaskPlane->SetTexture( Material::TextureType::Diffuse, trans_mask_diff );
    tmpMatMaskPlane->SetTexture( Material::TextureType::Opacity, trans_mask_mask );
    m_RaySceneMesh->MergeScene( maskPlane );

    auto alphaPlane       = commandList->CreatePlane( 100, 100, XMFLOAT3( 150, 70, 0 ) );
    auto tmpMatAlphaPlane = alphaPlane->GetRootNode()->GetMesh( 0 )->GetMaterial();
    tmpMatAlphaPlane->SetTexture( Material::TextureType::Diffuse, trans_alpha_diff );
    m_RaySceneMesh->MergeScene( alphaPlane );
    
    #endif

    
    backgroundColour[0] = m_frameData.atmosphere.x;
    backgroundColour[1] = m_frameData.atmosphere.y;
    backgroundColour[2] = m_frameData.atmosphere.z;


    // Create a color buffer with sRGB for gamma correction.
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    backBufferFormat = Texture::GetUAVCompatableFormat( backBufferFormat );

    // Start loading resources while the rest of the resources are created.
    commandQueue.ExecuteCommandList( commandList ); 

    CD3DX12_STATIC_SAMPLER_DESC pointClampSampler( 0, D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP );


    // Make sure the command queue is finished loading resources before rendering the first frame.
    commandQueue.Flush();

#if RAY_TRACER

    CreateAccelerationStructure(); // Tutorial 3

    CreateRayTracingPipeline(); // Tutorial 4

    CreateConstantBuffer();  // Tutorial 9

    CreateShaderResource( backBufferFormat );  // Tutorial 6

    CreateShaderTable();  // Tutorial 5


#endif

    return true;
}

void DummyGame::OnResize( ResizeEventArgs& e )
{
    m_Width  = std::max( 1, e.Width );
    m_Height = std::max( 1, e.Height );


    m_SwapChain->Resize( m_Width, m_Height );

    float aspectRatio = m_Width / (float)m_Height;

    m_Viewport = CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>( m_Width ), static_cast<float>( m_Height ) );

    m_RenderTarget.Resize( m_Width, m_Height );
    //m_RayRenderTarget.Resize( m_Width, m_Height );

}

void DummyGame::OnDPIScaleChanged( DPIScaleEventArgs& e )
{
    m_GUI->SetScaling( e.DPIScale );
}

void DummyGame::UnloadContent()
{
    m_RaySceneMesh.reset();
    m_DummyTexture.reset();

    // ray tracing 
#if RAY_TRACER

    m_BLAS.reset();
    m_TlasBuffers.pScratch.reset();
    m_TlasBuffers.pResult.reset();
    m_TlasBuffers.pInstanceDesc.reset();
    
    m_RayGenRootSig.reset();
    m_EmptyLocalRootSig.reset();
    m_StdHitRootSig.reset();
    m_GlobalRootSig.reset();

    m_RayPipelineState->GetD3D12PipelineState()->Release(); // not released properly when reseting variable.
    m_RayPipelineState.reset();

    m_RaygenShaderTable.reset();
    m_MissShaderTable.reset();
    m_HitShaderTable.reset();

    m_RayRenderTarget.Reset();
    m_RayShaderHeap.reset();

#endif

    m_RenderTarget.Reset();

    m_GUI.reset();
    m_SwapChain.reset();

    m_Device.reset();
}

static double g_FPS = 0.0;

XMFLOAT3 CalculateDirectionVector(float yaw, float pitch) {
    float dx = std::cos( Math::Radians( yaw ) ) * std::cos( Math::Radians( pitch ) );
    float dy = std::sin( Math::Radians( pitch ) );
    float dz = std::sin( Math::Radians( yaw ) ) * std::cos( Math::Radians( pitch ) );
    return XMFLOAT3( dx, dy, dz );
}

void DummyGame::UpdateCamera( float moveVertically, float moveUp, float moveForward )
{
    const float cam_dist = 1.0;

    XMFLOAT3 forward = CalculateDirectionVector( m_Yaw, m_Pitch );
    XMFLOAT3 right   = CalculateDirectionVector( m_Yaw + 90, 0 );

    m_CamPos.x += moveForward * forward.x;
    m_CamPos.y += moveForward * forward.y;
    m_CamPos.z += moveForward * forward.z;

    m_CamPos.x += moveVertically * right.x;
    m_CamPos.y += moveVertically * right.y;
    m_CamPos.z += moveVertically * right.z;

    m_CamPos.y += moveUp;

    XMFLOAT3          camDir = CalculateDirectionVector( m_Yaw, m_Pitch );

    auto              lookAt = DirectX::XMLoadFloat3( &m_CamPos ) + cam_dist * DirectX::XMLoadFloat3( &camDir );
    DirectX::XMFLOAT3 camLookAt;
    DirectX::XMStoreFloat3( &camLookAt, lookAt );

    m_frameData.UpdateCamera( m_CamPos, camLookAt, m_CamWindow );

}

void DummyGame::OnUpdate( UpdateEventArgs& e )
{
    static uint64_t frameCount = 0;
    static double   totalTime  = 0.0;
    static double   accumalatedRotation = 0.0;

    totalTime += e.DeltaTime;
    accumalatedRotation += scene_rot_speed * e.DeltaTime;
    frameCount++;

    if ( totalTime > 1.0 )
    {
        g_FPS = frameCount / totalTime;

        m_Logger->info( "FPS: {:.7}", g_FPS );

        wchar_t buffer[512];
        ::swprintf_s( buffer, L"HDR [FPS: %f]", g_FPS );
        m_Window->SetWindowTitle( buffer );

        frameCount = 0;
        totalTime  = 0.0;

        if (m_Print) {
            m_Print = false;
            m_Logger->info( "Pos: ({:.7},{:.7},{:.7})", m_CamPos.x, m_CamPos.y, m_CamPos.z );
        }

    }

    // Defacto update
    {
        bool isAccumelatingFrames = true;

#if UPDATE_TRANSFORMS

        InstanceTransforms oldTransforms = m_InstanceTransforms[0];

        auto S  = DirectX::XMMatrixScaling( scene_scale, scene_scale, scene_scale );
        auto R  = DirectX::XMMatrixRotationY( accumalatedRotation + Math::Radians( scene_rot_offset ) );
        auto RS = DirectX::XMMatrixMultiply( R, S );

        DirectX::XMStoreFloat3x4( &m_InstanceTransforms[0].matrix, RS );
        m_InstanceTransforms[0].CalculateNormalInverse();

        isAccumelatingFrames &= m_InstanceTransforms[0].Equal( &oldTransforms );

#endif


        FrameData old = m_frameData;

        m_frameData.atmosphere.x = backgroundColour[0];
        m_frameData.atmosphere.y = backgroundColour[1];
        m_frameData.atmosphere.z = backgroundColour[2];

        UpdateCamera( 
            ( m_Right - m_Left ) * cam_speed * e.DeltaTime,
            ( m_Up - m_Down ) * cam_speed * e.DeltaTime, 
            ( m_Forward - m_Backward ) * cam_speed * e.DeltaTime 
        );


        isAccumelatingFrames &= m_frameData.Equal( &old );


        if ( !isAccumelatingFrames )
        {
            m_frameData.accumulatedFrames = 0;
        }
        else
        {
            m_frameData.accumulatedFrames += 1;
        }



        // update buffers
        void* pData;
        ThrowIfFailed( m_FrameDataCB->Map( &pData ) );
        {
            memcpy( pData, &m_frameData, sizeof( FrameData ) );
        }
        m_FrameDataCB->Unmap();

        
        D3D12_RAYTRACING_INSTANCE_DESC* pInstDesc;
        ThrowIfFailed( m_InstanceDescBuffer->Map( (void**)&pInstDesc ) );
        {
            memcpy( pInstDesc[0].Transform, &m_InstanceTransforms[0].matrix, sizeof( XMFLOAT3X4 ) );
        }
        m_InstanceDescBuffer->Unmap();


        UpdateConstantBuffer();

        

    }


    m_Window->SetFullscreen( m_Fullscreen );

    OnRender();
}

void DummyGame::OnGUI( const std::shared_ptr<dx12lib::CommandList>& commandList,
                       const dx12lib::RenderTarget&                 renderTarget )
{
    m_GUI->NewFrame();

    static bool showDemoWindow = false;
    if ( showDemoWindow )
    {
        ImGui::ShowDemoWindow( &showDemoWindow );
    }
    
    if (ImGui::Begin( "Camera and Transform Sliders" ))// not demo window
    {
        ImGui::SliderFloat( "Camera Speed", &cam_speed, 1, 1000 );
        ImGui::SliderFloat( "Rotation Speed", &scene_rot_speed, -1, 1 );

        ImGui::SliderFloat( "Scene Rotation Offset", &scene_rot_offset, -180, 180 );
        ImGui::SliderFloat( "Scene Scale", &scene_scale, 1, 1000 );

        int tmp = static_cast<int>( m_frameData.nbrSamplesPerPixel );
        ImGui::SliderInt( "Samples Per Pixel", &tmp, 1, 10 );
        m_frameData.nbrSamplesPerPixel = static_cast<uint32_t>( tmp );

        ImGui::End();
    }

    static bool editAtmosphere = true;
    if ( editAtmosphere && ImGui::Begin( "Atmosphere Sliders" ) )  // not demo window
    {

        ImGui::ColorPicker3( "Atmosphere Colour", backgroundColour );

        float atmosphereIntensity = m_frameData.atmosphere.w;
        ImGui::SliderFloat( "Atmosphere Intensity", &atmosphereIntensity, 1, 100 );
        m_frameData.atmosphere.w = atmosphereIntensity;

        ImGui::End();
    }


    m_GUI->Render( commandList, renderTarget );
}


void DummyGame::OnRender()
{

    auto& commandQueue = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_DIRECT );
    auto  commandList  = commandQueue.GetCommandList();

    /*
       Ray tracing calling.
    */
#if RAY_TRACER
    {
        for (uint32_t i = 0; i < m_nbrRayRenderTargets; ++i) 
        {
            AttachmentPoint point    = static_cast<AttachmentPoint>( i );
            auto            resource = m_RayRenderTarget.GetTexture( point );
            commandList->TransitionBarrier( resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        }


#if UPDATE_TRANSFORMS

        AccelerationBuffer::CreateTopLevelAS( m_Device.get(), commandList.get(), &mTlasSize, &m_TlasBuffers,
                                              m_Instances, m_InstanceDescBuffer.get(), true );

    #endif
        /*
            Here we declare where the hitshader is, where the miss shader is, and where the raygen shader
        */
        D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
        {
            raytraceDesc.Width  = m_Width;
            raytraceDesc.Height = m_Height;
            raytraceDesc.Depth  = 1;

            // RayGen is the first entry in the shader-table
            raytraceDesc.RayGenerationShaderRecord.StartAddress =
                m_RaygenShaderTable->GetD3D12Resource()->GetGPUVirtualAddress();
            raytraceDesc.RayGenerationShaderRecord.SizeInBytes = 
                m_ShaderTableEntrySize;

            // Miss is the second entry in the shader-table
            raytraceDesc.MissShaderTable.StartAddress  = 
                m_MissShaderTable->GetD3D12Resource()->GetGPUVirtualAddress();
            raytraceDesc.MissShaderTable.StrideInBytes = m_ShaderTableEntrySize;
            raytraceDesc.MissShaderTable.SizeInBytes =
                m_ShadersEntriesPerGeometry * m_ShaderTableEntrySize;  

            // Hit is the third entry in the shader-table
            raytraceDesc.HitGroupTable.StartAddress = 
                m_HitShaderTable->GetD3D12Resource()->GetGPUVirtualAddress();
            raytraceDesc.HitGroupTable.StrideInBytes = m_ShaderTableEntrySize;
            raytraceDesc.HitGroupTable.SizeInBytes =
                m_TotalGeometryCount * m_ShadersEntriesPerGeometry * m_ShaderTableEntrySize;
        }

        // Set global root signature
        commandList->SetComputeRootSignature( m_GlobalRootSig );

        // Set pipeline and heaps for shader table
        commandList->SetPipelineState1( m_RayPipelineState, m_RayShaderHeap );

        // Dispatch Rays
        commandList->DispatchRays( &raytraceDesc );

        for ( uint32_t i = 0; i < m_nbrRayRenderTargets; ++i )
        {
            auto resource = m_RayRenderTarget.GetTexture( static_cast<AttachmentPoint>( i ) );
            commandList->TransitionBarrier( resource, D3D12_RESOURCE_STATE_COPY_SOURCE );
        }

    }
    #endif
    
    auto& swapChainRT         = m_SwapChain->GetRenderTarget();
    auto  swapChainBackBuffer = swapChainRT.GetTexture( AttachmentPoint::Color0 );

#if RAY_TRACER
    auto outputImage = m_RayRenderTarget.GetTexture( AttachmentPoint::Color0 );
    commandList->CopyResource( swapChainBackBuffer, outputImage );
#else
    commandList->CopyResource( swapChainBackBuffer, m_DummyTexture );
#endif

    // Render GUI.
    OnGUI( commandList, m_SwapChain->GetRenderTarget() );

    commandQueue.ExecuteCommandList( commandList );

    // Present
    m_SwapChain->Present();
}

static bool g_AllowFullscreenToggle = true;

void DummyGame::OnKeyPressed( KeyEventArgs& e )
{
    if ( !ImGui::GetIO().WantCaptureKeyboard )
    {
        switch ( e.Key )
        {
        case KeyCode::Escape:
            GameFramework::Get().Stop();
            break;
        case KeyCode::Enter:
            if ( e.Alt )
            {
            case KeyCode::F11:
                if ( g_AllowFullscreenToggle )
                {
                    m_Fullscreen = !m_Fullscreen; // Defer window resizing until OnUpdate();
                    // Prevent the key repeat to cause multiple resizes.
                    g_AllowFullscreenToggle = false;
                }
                break;
            }
        case KeyCode::V:
            m_SwapChain->ToggleVSync();
            break;
        case KeyCode::R:
            // Reset camera transform
            m_Pitch = 0.0f;
            m_Yaw   = 0.0f;
            break;
        case KeyCode::Up:
        case KeyCode::W:
            m_Forward = 1.0f;
            break;
        case KeyCode::Left:
        case KeyCode::A:
            m_Left = 1.0f;
            break;
        case KeyCode::Down:
        case KeyCode::S:
            m_Backward = 1.0f;
            break;
        case KeyCode::Right:
        case KeyCode::D:
            m_Right = 1.0f;
            break;
        case KeyCode::ShiftKey:
            m_Down = 1.0f;
            break;
        case KeyCode::Space:
            m_Up = 1.0f;
            break;
        case KeyCode::P:
            m_Print = true;
            break;
        }
    }
}

void DummyGame::OnKeyReleased( KeyEventArgs& e )
{
    if ( !ImGui::GetIO().WantCaptureKeyboard )
    {
        switch ( e.Key )
        {
        case KeyCode::Enter:
            if ( e.Alt )
            {
            case KeyCode::F11:
                g_AllowFullscreenToggle = true;
            }
            break;
        case KeyCode::Up:
        case KeyCode::W:
            m_Forward = 0.0f;
            break;
        case KeyCode::Left:
        case KeyCode::A:
            m_Left = 0.0f;
            break;
        case KeyCode::Down:
        case KeyCode::S:
            m_Backward = 0.0f;
            break;
        case KeyCode::Right:
        case KeyCode::D:
            m_Right = 0.0f;
            break;
        case KeyCode::ShiftKey:
            m_Down = 0.0f;
            break;
        case KeyCode::Space:
            m_Up = 0.0f;
            break;
        }
    }
}

void DummyGame::OnMouseMoved( MouseMotionEventArgs& e )
{
    const float mouseSpeed = 0.1f;
    if ( !ImGui::GetIO().WantCaptureMouse )
    {
        if ( e.LeftButton )
        {
            m_Pitch += e.RelY * mouseSpeed;

            m_Pitch = std::clamp( m_Pitch, -89.0f, 89.0f );

            m_Yaw -= e.RelX * mouseSpeed;
        }
    }
}

void DummyGame::OnMouseWheel( MouseWheelEventArgs& e )
{
    if ( !ImGui::GetIO().WantCaptureMouse )
    {

    }
}

