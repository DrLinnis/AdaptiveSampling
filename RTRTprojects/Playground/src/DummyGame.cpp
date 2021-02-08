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

DummyGame::Colour::Colour(float r, float g, float b)
    : r(r)
    , g(g)
    , b(b)
    , padding(0)
{ }


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
, m_cam( XMFLOAT3( 50, 50, 0 ), XMFLOAT3( -500, 100, 0 ), XMFLOAT2( width / (float)height, 1 ) )
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

static const WCHAR* kShadowChs      = L"shadowChs";
static const WCHAR* kShadowMiss     = L"shadowMiss";
static const WCHAR* kShadowHitGroup = L"ShadowHitGroup";

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
    std::array<D3D12_STATE_SUBOBJECT, 16> subobjects;
    uint32_t                              index = 0;

    // Load 
    ComPtr<IDxcBlob> shaders =
        ShaderHelper::CompileLibrary( L"RTRTprojects/Playground/shaders/RayTracer.hlsl", L"lib_6_5" );
    const WCHAR* entryPoints[] = { kRayGenShader, kStandardMiss, kStandardChs, kShadowChs,
                                   kShadowMiss };  // SIZE 5
    
    DxilLibrary      dxilLib( shaders.Get(), entryPoints, 5 );
    subobjects[index++] = dxilLib.stateSubobject; 

    // Normal Hit Group and Shadow Hit Group
    HitProgram hitProgram( nullptr, kStandardChs, kStandardHitGroup );
    subobjects[index++] = hitProgram.subObject; 

    HitProgram shadowHitProgram( nullptr, kShadowChs, kShadowHitGroup );
    subobjects[index++] = shadowHitProgram.subObject;  



    // Create the ray-gen root-signature and association
    {

        CD3DX12_DESCRIPTOR_RANGE1 ranges[3] = {};
        size_t                    rangeIdx           = 0;
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0 );
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 3 );
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 4 );


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

        CD3DX12_DESCRIPTOR_RANGE1 ranges[8] = {};
        size_t                    rangeIdx = 0;
        size_t                    offset    = 4;
        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, offset );
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

        ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_TotalDiffuseTexCount, 1, 3,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += m_TotalDiffuseTexCount;

         ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_TotalNormalTexCount, 1, 4,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += m_TotalNormalTexCount;

         ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_TotalSpecularTexCount, 1, 5,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += m_TotalSpecularTexCount;

         ranges[rangeIdx++].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_TotalMaskTexCount, 1, 6,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, offset );
        offset += m_TotalMaskTexCount;

        CD3DX12_ROOT_PARAMETER1 rayRootParams[1] = {};

        rayRootParams[0].InitAsDescriptorTable( rangeIdx, ranges );

        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        CD3DX12_STATIC_SAMPLER_DESC samplers[2];
        samplers[0].Init( 0 );
        samplers[1].Init( 1, D3D12_FILTER_MIN_MAG_MIP_POINT );

        CD3DX12_STATIC_SAMPLER_DESC myTextureSampler( 0 );

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( 1, rayRootParams, 2, samplers, rootSignatureFlags );

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

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
            rootSignatureDescription.Init_1_1( 0, nullptr, 0, nullptr, rootSignatureFlags );

            m_EmptyLocalRootSig = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

            ID3D12RootSignature*  pEmptyInterface = m_EmptyLocalRootSig->GetD3D12RootSignature().Get();
            D3D12_STATE_SUBOBJECT emptySubobject  = {};
            emptySubobject.Type                   = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            emptySubobject.pDesc                  = &pEmptyInterface;

            subobjects[index] = emptySubobject;
        }

        // EMPTY associations
        uint32_t          emptyRootIndex    = index++;   
        const WCHAR*      emptyExportName[] = { kStandardMiss, kShadowMiss, kShadowChs };  
        ExportAssociation emptyRootAssociation( emptyExportName, 3, &( subobjects[emptyRootIndex] ) );
        subobjects[index++] = emptyRootAssociation.subobject;



    // Bind the payload size to the programs, SET MISS SHADER OUTPUT PAYLOAD TO 3x4=12 BYTES
    ShaderConfig shaderConfig( sizeof( float ) * 2, sizeof( float ) * (3+3+1+1) );
    subobjects[index] = shaderConfig.subobject;

    uint32_t          shaderConfigIndex = index++;  
    const WCHAR*      shaderExports[]   = { kStandardMiss, kStandardChs, kRayGenShader, kShadowMiss, kShadowChs }; 
    ExportAssociation configAssociation( shaderExports, 5, &( subobjects[shaderConfigIndex] ) );
    subobjects[index++] = configAssociation.subobject;  


        // Create the pipeline config, per bounce, one reflection ray and one depth ray
        PipelineConfig config( m_Bounces * 2 ); 
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
 

    m_nbrRayRenderTargets = 3;

    auto rayImage = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayImage->SetName( L"RayGen diffuse output texture" );

    auto rayNormals = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayNormals->SetName( L"RayGen normal output texture" );

    auto rayDepth = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    rayDepth->SetName( L"RayGen depth output texture" );

    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color0, rayImage );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color1, rayNormals );
    m_RayRenderTarget.AttachTexture( AttachmentPoint::Color2, rayDepth );

    m_RayCamCB = m_Device->CreateMappableBuffer( 256 );

    // Create an off-screen render for the compute shader
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;

    // Create SRV for TLAS after the UAV above. 
    D3D12_SHADER_RESOURCE_VIEW_DESC srvTlasDesc          = {};
    srvTlasDesc.ViewDimension                        = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvTlasDesc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvTlasDesc.RaytracingAccelerationStructure.Location =  m_TlasBuffers.pResult->GetD3D12Resource()->GetGPUVirtualAddress();


    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.SizeInBytes                     = 256;
    cbvDesc.BufferLocation                  = m_RayCamCB->GetD3D12Resource()->GetGPUVirtualAddress();

    m_RayShaderHeap = m_Device->CreateShaderTableView( m_nbrRayRenderTargets, &m_RayRenderTarget, &uavDesc, &srvTlasDesc,
                                                       &cbvDesc, m_RaySceneMesh.get() );
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
            pInstDesc[0].Transform[0][0] = pInstDesc[0].Transform[1][1] = pInstDesc[0].Transform[2][2] = 1;
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

void DummyGame::CreateConstantBuffer() 
{
    // nothing done here atm :(
}

void DummyGame::CreateShaderTable()
{
    uint32_t shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    m_ShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    m_ShaderTableEntrySize += sizeof( UINT64 );  // extra 8 bytes (64 bits) for heap pointer to viewers.
    m_ShaderTableEntrySize = align_to( D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, m_ShaderTableEntrySize );

    uint32_t shaderBufferSize = m_ShaderTableEntrySize;
    shaderBufferSize          = align_to( D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, m_ShaderTableEntrySize );
    m_ShadersEntriesPerGeometry = 2;

    uint32_t perGeomShaderBuffSize = m_ShadersEntriesPerGeometry * shaderBufferSize;


    m_RaygenShaderTable = m_Device->CreateMappableBuffer( shaderBufferSize );
    m_RaygenShaderTable->SetName( L"DXR Raygen Shader Table" );

    m_MissShaderTable = m_Device->CreateMappableBuffer( m_ShadersEntriesPerGeometry *
                                                        shaderBufferSize );  // 2, one per primary and one per shadow
    m_MissShaderTable->SetName( L"DXR Miss Shader Table" );

    m_HitShaderTable = m_Device->CreateMappableBuffer(
        m_TotalGeometryCount * perGeomShaderBuffSize );  // 25, one per primary and one per shadow
    m_HitShaderTable->SetName( L"DXR Hit Shader Table" );

    ComPtr<ID3D12StateObjectProperties> pRtsoProps;
    m_RayPipelineState->GetD3D12PipelineState()->QueryInterface( IID_PPV_ARGS( &pRtsoProps ) );

    UINT64 descriptorHeapStart = m_RayShaderHeap->GetTableHeap()->GetGPUDescriptorHandleForHeapStart().ptr;

    // ray gen shader
    void* pRayGenShdr = pRtsoProps->GetShaderIdentifier( kRayGenShader );

    // miss for regular and shadow
    void* pStandardMiss   = pRtsoProps->GetShaderIdentifier( kStandardMiss );
    void* pShadowMiss = pRtsoProps->GetShaderIdentifier( kShadowMiss );

    // hit for regular and shadow
    void* pStandardHitGrp = pRtsoProps->GetShaderIdentifier( kStandardHitGroup );
    void* pShadowHitGrp = pRtsoProps->GetShaderIdentifier( kShadowHitGroup );

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

        memcpy( pData + shaderBufferSize, pShadowMiss, shaderIdSize );
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

            // Shadow - secondary
            memcpy( pData + ( m_ShadersEntriesPerGeometry * i + 1 ) * shaderBufferSize, pShadowHitGrp,
                    shaderIdSize );  // shadow hit
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
    //m_RaySceneMesh = commandList->CreatePlane( 15,15 );
    //m_RaySceneMesh = commandList->CreateSphere( 15, 16);
    m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/crytek-sponza/sponza_nobanner.obj" );
    //m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/AmazonLumberyard/interior.obj" );
    //m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/AmazonLumberyard/exterior.obj" );
    //m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/San_Miguel/san-miguel.obj" );
    //m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/San_Miguel/san-miguel-low-poly.obj" );

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

    CreateShaderResource( backBufferFormat );  // Tutorial 6

    CreateConstantBuffer();                     // Tutorial 9

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

    //m_RayOutputResource->Resize( m_Width, m_Height );
    //m_RayShaderHeap.recrate();

}

void DummyGame::OnDPIScaleChanged( DPIScaleEventArgs& e )
{
    m_GUI->SetScaling( e.DPIScale );
}

void DummyGame::UnloadContent()
{
    m_RaySceneMesh.reset();
    m_RaySphere.reset();
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
    XMFLOAT3 forward = CalculateDirectionVector( m_Yaw, m_Pitch );
    XMFLOAT3 right   = CalculateDirectionVector( m_Yaw + 90, 0 );

    m_cam.pos.x += moveForward * forward.x;
    m_cam.pos.y += moveForward * forward.y;
    m_cam.pos.z += moveForward * forward.z;

    m_cam.pos.x += moveVertically * right.x;
    m_cam.pos.y += moveVertically * right.y;
    m_cam.pos.z += moveVertically * right.z;

    m_cam.pos.y += moveUp;
}

void DummyGame::OnUpdate( UpdateEventArgs& e )
{
    static uint64_t frameCount = 0;
    static double   totalTime  = 0.0;

    totalTime += e.DeltaTime;
    theta += e.DeltaTime;
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
    }

    // Defacto update
    {
        const float cam_dist = 1.0;
        XMFLOAT3    camDir   = CalculateDirectionVector( m_Yaw, m_Pitch );

        UpdateCamera( 
            ( m_Left - m_Right ) * speed * e.DeltaTime, 
            ( m_Up - m_Down ) * speed * e.DeltaTime,
            ( m_Backward - m_Forward ) * speed * e.DeltaTime 
        );

        m_cam.lookAt = XMFLOAT4( 
            m_cam.pos.x + cam_dist * camDir.x, 
            m_cam.pos.y + cam_dist * camDir.y,
            m_cam.pos.z + cam_dist * camDir.z,
            0 
        );

        void* pData;
        ThrowIfFailed( m_RayCamCB->Map( &pData ) );
        {
            memcpy( pData, &m_cam, sizeof( CameraCB ) );
        }
        m_RayCamCB->Unmap();

#if UPDATE_TRANSFORMS
        D3D12_RAYTRACING_INSTANCE_DESC* pInstDesc;
        ThrowIfFailed( m_InstanceDescBuffer->Map( (void**)&pInstDesc ) );
        {
            pInstDesc[1].Transform[1][1] = pInstDesc[1].Transform[2][2] = std::cos( theta );
            pInstDesc[1].Transform[1][2]                                = -std::sin( theta );
            pInstDesc[1].Transform[2][1]                                = std::sin( theta );

            pInstDesc[2].Transform[0][0] = pInstDesc[2].Transform[1][1] = std::cos( theta );
            pInstDesc[2].Transform[0][1]                                = -std::sin( theta );
            pInstDesc[2].Transform[1][0]                                = std::sin( theta );

        }
        m_InstanceDescBuffer->Unmap();
#endif
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
        dx12lib::AccelerationBuffer* blasList[] = { m_BLAS_plane.get(), m_BLAS_sphere.get() };

        AccelerationBuffer::CreateTopLevelAS( m_Device.get(), commandList.get(), 2, blasList, &mTlasSize,
                                              &m_TlasBuffers, m_InstanceDescBuffer.get(), true );
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
    auto outputImage = m_RayRenderTarget.GetTexture( static_cast<AttachmentPoint>( 0 ) );
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
        case KeyCode::ControlKey:
            speed = 300.0f;
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
        case KeyCode::ControlKey:
            speed = 100.0f;
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
            m_Pitch -= e.RelY * mouseSpeed;

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
