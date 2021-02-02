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

namespace PostProcessingRootParameters
{
enum
{
    Input,      // Texture2D Output : register( t0 );
    Output,     // RWTexture2D Output : register( u0 );
    NumRootParameters
};
}

namespace DisplayRootParameters
{
enum
{
    Textures,   // Texture2D DiffuseTexture : register( t0 );
    NumRootParameters
};
}

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

void DummyGame::CreatePostProcessor( const D3D12_STATIC_SAMPLER_DESC* sampler, DXGI_FORMAT backBufferFormat )
{
#if POST_PROCESSOR
    // Create an off-screen render for the compute shader
    {
        D3D12_RESOURCE_DESC stagingDesc = m_RenderShaderResource->GetD3D12ResourceDesc();
        stagingDesc.Format              = backBufferFormat;
        stagingDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;  // try or eq else

        m_PostProcessOutput = m_Device->CreateTexture( stagingDesc );
        m_PostProcessOutput->SetName( L"Post Processing Render Buffer" );

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                           = Texture::GetUAVCompatableFormat( stagingDesc.Format );
        uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2DArray.MipSlice          = 0;

        m_PostProcessOutputUAV = m_Device->CreateUnorderedAccessView( m_PostProcessOutput, nullptr, &uavDesc );
    }

    // Load compute shader
    ComPtr<ID3DBlob> cs;
    ThrowIfFailed( D3DReadFileToBlob( L"data/shaders/Playground/PostProcess.cso", &cs ) );

    { 
        // Create root signature
        CD3DX12_DESCRIPTOR_RANGE1 output( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
                                          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE );

        CD3DX12_ROOT_PARAMETER1 rootParameters[PostProcessingRootParameters::NumRootParameters];
        // Division by 4 becase sizeof gives in number of bytes, not 32 bits.
        rootParameters[PostProcessingRootParameters::Output].InitAsDescriptorTable( 1, &output );
        rootParameters[PostProcessingRootParameters::Input].InitAsShaderResourceView( 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                                                                                      D3D12_SHADER_VISIBILITY_ALL );

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( PostProcessingRootParameters::NumRootParameters, rootParameters, 0, sampler,
                                           rootSignatureFlags );

        
        m_PostProcessRootSignature = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );
    }


    // Create Pipeline State Object (PSO) for compute shader
    struct RayPipelineState
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS             CS;
    } rayPipelineStateStream;

    rayPipelineStateStream.pRootSignature = m_PostProcessRootSignature->GetD3D12RootSignature().Get();
    rayPipelineStateStream.CS             = CD3DX12_SHADER_BYTECODE( cs.Get() );

    m_PostProcessPipelineState = m_Device->CreatePipelineStateObject( rayPipelineStateStream );
#endif
}

void DummyGame::CreateDisplayPipeline( const D3D12_STATIC_SAMPLER_DESC* sampler, DXGI_FORMAT backBufferFormat )
{
#if RASTER_DISPLAY
    {
        // Create a colour descriptor with appropriate size
        auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D( backBufferFormat, m_Width, m_Height, 1, 1 );

        // Create an off-screen render target with a single color buffer.
        colorDesc.Flags        = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        m_RenderShaderResource = m_Device->CreateTexture( colorDesc );
        m_RenderShaderResource->SetName( L"Display Render Target" );

        m_RenderShaderView = m_Device->CreateShaderResourceView( m_RenderShaderResource );
    }


    // Load the shaders.
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ThrowIfFailed( D3DReadFileToBlob( L"data/shaders/Playground/Vertex.cso", &vs ) );
    ThrowIfFailed( D3DReadFileToBlob( L"data/shaders/Playground/Pixel.cso", &ps ) );

    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 );

    CD3DX12_ROOT_PARAMETER1 rootParameters[DisplayRootParameters::NumRootParameters];
    rootParameters[DisplayRootParameters::Textures].InitAsDescriptorTable( 1, &descriptorRange,
                                                                           D3D12_SHADER_VISIBILITY_PIXEL );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1( DisplayRootParameters::NumRootParameters, rootParameters, 1, sampler,
                                       rootSignatureFlags );

    m_DisplayRootSignature = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

    // Setup the HDR pipeline state.
    struct DisplayPipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT          InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC           SampleDesc;
    } displayPipelineStateStream;

    DXGI_SAMPLE_DESC sampleDesc = m_Device->GetMultisampleQualityLevels( backBufferFormat );

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets      = 1;
    rtvFormats.RTFormats[0]          = backBufferFormat;

    displayPipelineStateStream.pRootSignature        = m_DisplayRootSignature->GetD3D12RootSignature().Get();
    displayPipelineStateStream.InputLayout           = VertexPositionNormalTangentBitangentTexture::InputLayout;
    displayPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    displayPipelineStateStream.VS                    = CD3DX12_SHADER_BYTECODE( vs.Get() );
    displayPipelineStateStream.PS                    = CD3DX12_SHADER_BYTECODE( ps.Get() );

    displayPipelineStateStream.RTVFormats = rtvFormats;
    displayPipelineStateStream.SampleDesc = sampleDesc;

    m_DisplayPipelineState = m_Device->CreatePipelineStateObject( displayPipelineStateStream );

    auto renderDesc = CD3DX12_RESOURCE_DESC::Tex2D( backBufferFormat, m_Width, m_Height, 1, 1, sampleDesc.Count,
                                                    sampleDesc.Quality, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET );

    // Create render target image
    D3D12_CLEAR_VALUE colorClearValue;
    colorClearValue.Format = renderDesc.Format;
    std::memcpy( colorClearValue.Color, this->clearColor, sizeof( float ) * 4 );

    // render target
    auto renderedImage = m_Device->CreateTexture( renderDesc, &colorClearValue );
    renderedImage->SetName( L"Color Render Target" );

    // Attach the textures to the render target.
    m_RenderTarget.AttachTexture( AttachmentPoint::Color0, renderedImage );
#endif
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
        ShaderHelper::CompileLibrary( L"RTRTprojects/Playground/shaders/RayTracer.hlsl", L"lib_6_3" );
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
        CD3DX12_DESCRIPTOR_RANGE1 output( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                          0 );

        CD3DX12_DESCRIPTOR_RANGE1 camera( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                           1 );

        CD3DX12_DESCRIPTOR_RANGE1 TlvlAcc( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                           2 );

        const CD3DX12_DESCRIPTOR_RANGE1 tables[3] = { output, TlvlAcc, camera };

        CD3DX12_ROOT_PARAMETER1 rayRootParams[1] = {};

        rayRootParams[0].InitAsDescriptorTable( 3, tables );

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
        CD3DX12_DESCRIPTOR_RANGE1 TlvlAcc( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 2 );

        CD3DX12_ROOT_PARAMETER1 rayRootParams[1] = {};

        rayRootParams[0].InitAsDescriptorTable( 1, &TlvlAcc );

        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( 1, rayRootParams, 0, nullptr, rootSignatureFlags );

        m_PlaneHitRootSig = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

        ID3D12RootSignature*  pHitInterface = m_PlaneHitRootSig->GetD3D12RootSignature().Get();
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
    ShaderConfig shaderConfig( sizeof( float ) * 2, sizeof( float ) * 3 );
    subobjects[index] = shaderConfig.subobject;

    uint32_t          shaderConfigIndex = index++;  
    const WCHAR*      shaderExports[]   = { kStandardMiss, kStandardChs, kRayGenShader, kShadowMiss, kShadowChs }; 
    ExportAssociation configAssociation( shaderExports, 5, &( subobjects[shaderConfigIndex] ) );
    subobjects[index++] = configAssociation.subobject;  

        // Create the pipeline config
        PipelineConfig config( 2 ); 
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

    m_RayOutputResource = m_Device->CreateTexture( renderDesc, nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE );
    m_RayOutputResource->SetName( L"RayGen output texture" );

    m_RayCamCB = m_Device->CreateMappableBuffer( 256 );

    // Create an off-screen render for the compute shader
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;

    // Create SRV for TLAS after the UAV above. 
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc          = {};
    srvDesc.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_TlasBuffers.pResult->GetD3D12Resource()->GetGPUVirtualAddress();


    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.SizeInBytes                     = 256;
    cbvDesc.BufferLocation                  = m_RayCamCB->GetD3D12Resource()->GetGPUVirtualAddress();

    m_RayShaderHeap = m_Device->CreateShaderTableView( m_RayOutputResource, &uavDesc, &srvDesc, &cbvDesc );

}

void DummyGame::CreateAccelerationStructure() 
{

    auto& commandQueueDirect = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_DIRECT );
    auto  commandList        = commandQueueDirect.GetCommandList();

    AccelerationStructure blasBuffers = {};
    m_RaySceneMesh->BuildBottomLevelAccelerationStructure( m_Device.get(), commandList.get(), &blasBuffers );

    // Create instances
    {
        m_InstanceDescBuffer = m_Device->CreateMappableBuffer( sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
        m_InstanceDescBuffer->SetName( L"DXR TLAS Instance Description" );

        D3D12_RAYTRACING_INSTANCE_DESC* pInstDesc;
        ThrowIfFailed( m_InstanceDescBuffer->Map( (void**)&pInstDesc ) );
        {
            // instance 0 - sphere AND plane
            pInstDesc[0].InstanceID                              = 0;
            pInstDesc[0].InstanceContributionToHitGroupIndex     = 0;
            pInstDesc[0].Flags                                   = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            pInstDesc[0].Transform[0][0] = pInstDesc[0].Transform[1][1] = pInstDesc[0].Transform[2][2] = 1;
            pInstDesc[0].AccelerationStructure = blasBuffers.pResult->GetD3D12Resource()->GetGPUVirtualAddress();
            pInstDesc[0].InstanceMask          = 0xFF;
        }
        m_InstanceDescBuffer->Unmap();

    }


    AccelerationBuffer* pBuffer[] = { blasBuffers.pResult.get() };
    AccelerationBuffer::CreateTopLevelAS( m_Device.get(), commandList.get(), 1, pBuffer, 
        &mTlasSize, &m_TlasBuffers, m_InstanceDescBuffer.get() 
    );

    commandQueueDirect.ExecuteCommandList( commandList );
    commandQueueDirect.Flush();

    m_BLAS = blasBuffers.pResult;
}

void DummyGame::CreateConstantBuffer() 
{
    for ( int i = 0; i < 3; ++i ) 
    {
        m_MissSdrCBs[i] = m_Device->CreateMappableBuffer( sizeof( Colour ) );
        void* pData;
        ThrowIfFailed( m_MissSdrCBs[i]->Map( &pData ) );
        {
            memcpy( pData, &m_SphereHintedColours[i], sizeof( Colour ) );
        }
        m_MissSdrCBs[i]->Unmap();
    }

    
}

void DummyGame::CreateShaderTable()
{
    uint32_t shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    m_ShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    m_ShaderTableEntrySize += 2*sizeof( UINT64 );  // extra 8 bytes (64 bits) for heap pointer to viewers.
    m_ShaderTableEntrySize = align_to( D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, m_ShaderTableEntrySize );

    uint32_t shaderBufferSize = m_ShaderTableEntrySize;
    shaderBufferSize          = align_to( D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, m_ShaderTableEntrySize );
    uint32_t perInstShaderBuffSize = 2 * shaderBufferSize;

    m_RaygenShaderTable = m_Device->CreateMappableBuffer( shaderBufferSize );
    m_RaygenShaderTable->SetName( L"DXR Raygen Shader Table" );

    m_MissShaderTable =
        m_Device->CreateMappableBuffer( perInstShaderBuffSize );  // 2, one per primary and one per shadow
    m_MissShaderTable->SetName( L"DXR Miss Shader Table" );

    m_HitShaderTable =
        m_Device->CreateMappableBuffer( 25*perInstShaderBuffSize );  // 25, one per primary and one per shadow
    m_HitShaderTable->SetName( L"DXR Hit Shader Table" );

    ComPtr<ID3D12StateObjectProperties> pRtsoProps;
    m_RayPipelineState->GetD3D12PipelineState()->QueryInterface( IID_PPV_ARGS( &pRtsoProps ) );

    UINT64 heapUavSrvPtr = m_RayShaderHeap->GetTableHeap()->GetGPUDescriptorHandleForHeapStart().ptr;
    UINT64 cbvDest = m_TlasBuffers.pResult->GetD3D12Resource()->GetGPUVirtualAddress();

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
        memcpy( pData + shaderIdSize, &heapUavSrvPtr, sizeof( UINT64 ) );
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
        for (int i = 0; i < 25; ++i) {
            // Primary
            memcpy( pData + ( 2 * i + 0 ) * shaderBufferSize, pStandardHitGrp, shaderIdSize );  // regular closest hit
            memcpy( pData + ( 2 * i + 0 ) * shaderBufferSize + shaderIdSize, &heapUavSrvPtr,
                    sizeof( UINT64 ) );  // point to TLAS SRV

            // Shadow - secondary
            memcpy( pData + ( 2 * i + 1 ) * shaderBufferSize, pShadowHitGrp, shaderIdSize );  // shadow hit
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
    m_RaySphere = commandList->CreateTorus( 1.5, 1 / 2.0f, 32, DirectX::XMFLOAT3( 0, 0, 0 ) );
    m_RaySceneMesh = commandList->LoadSceneFromFile( L"Assets/Models/crytek-sponza/sponza_nobanner.obj" );



    // Create a color buffer with sRGB for gamma correction.
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    backBufferFormat = Texture::GetUAVCompatableFormat( backBufferFormat );

    // Start loading resources while the rest of the resources are created.
    commandQueue.ExecuteCommandList( commandList ); 

    CD3DX12_STATIC_SAMPLER_DESC pointClampSampler( 0, D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP );

    //Post Processing Compute Shader
#if POST_PROCESSOR
    CreatePostProcessor( &pointClampSampler, backBufferFormat );
#endif

    // Display Pipeline
#if RASTER_DISPLAY
    CreateDisplayPipeline( &pointClampSampler, backBufferFormat );
#endif 


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
    m_SphereHitRootSig.reset();
    m_GlobalRootSig.reset();

    m_RayPipelineState->GetD3D12PipelineState()->Release(); // not released properly when reseting variable.
    m_RayPipelineState.reset();

    m_RaygenShaderTable.reset();
    m_MissShaderTable.reset();
    m_HitShaderTable.reset();

    m_RayOutputResource.reset();
    m_RayShaderHeap.reset();

    m_RayOutputUAV.reset();
    m_TlasSRV.reset();

#endif

    // old resets
#if POST_PROCESSOR
    m_PostProcessOutputUAV.reset();
    m_PostProcessOutput.reset();

    m_PostProcessRootSignature.reset();
    m_PostProcessPipelineState.reset();
#endif

#if RASTER_DISPLAY
    m_RenderShaderView.reset();
    m_RenderShaderResource.reset();

    m_DisplayRootSignature.reset();
    m_DisplayPipelineState.reset();
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
        commandList->TransitionBarrier( m_RayOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

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
            raytraceDesc.RayGenerationShaderRecord.SizeInBytes  = m_ShaderTableEntrySize;

            // Miss is the second entry in the shader-table
            raytraceDesc.MissShaderTable.StartAddress  = 
                m_MissShaderTable->GetD3D12Resource()->GetGPUVirtualAddress();
            raytraceDesc.MissShaderTable.StrideInBytes = m_ShaderTableEntrySize;
            raytraceDesc.MissShaderTable.SizeInBytes   = 2*m_ShaderTableEntrySize;  

            // Hit is the third entry in the shader-table
            raytraceDesc.HitGroupTable.StartAddress = 
                m_HitShaderTable->GetD3D12Resource()->GetGPUVirtualAddress();
            raytraceDesc.HitGroupTable.StrideInBytes = m_ShaderTableEntrySize;
            raytraceDesc.HitGroupTable.SizeInBytes   = m_ShaderTableEntrySize * 25;
        }

        // Set global root signature
        commandList->SetComputeRootSignature( m_GlobalRootSig );

        // Set pipeline and heaps for shader table
        commandList->SetPipelineState1( m_RayPipelineState, m_RayShaderHeap );

        // Dispatch Rays
        commandList->DispatchRays( &raytraceDesc );

        commandList->TransitionBarrier( m_RayOutputResource, D3D12_RESOURCE_STATE_COPY_SOURCE );

    }
    #endif
    
    
    /*
       Post process compute shader execute and transfer staging resource to display.
    */
#if POST_PROCESSOR
    {
        commandList->SetPipelineState( m_PostProcessPipelineState );
        commandList->SetComputeRootSignature( m_PostProcessRootSignature );

        // set uniforms
        {
            commandList->SetUnorderedAccessView( PostProcessingRootParameters::Output, 0,
                                                 m_PostProcessOutputUAV );
        }

        commandList->Dispatch( Math::DivideByMultiple( m_Width, 16 ), Math::DivideByMultiple( m_Height, 9 ), 1 );

        commandList->UAVBarrier( m_PostProcessOutput );

        if ( m_RenderShaderResource->GetD3D12Resource() != m_PostProcessOutput->GetD3D12Resource() )
        {
            //commandList->CopyResource( m_RenderShaderResource, m_StagingResource );
        }
    }
#endif
    

    /*
        Display Pipeline render!
    */
#if RASTER_DISPLAY 
    
    // Clear the render targets.
    // commandList->ClearTexture( m_RenderTarget.GetTexture( AttachmentPoint::Color0 ), clearColor ); 

    // Create a scene visitor that is used to perform the actual rendering of the meshes in the scenes.
    SceneVisitor visitor( *commandList );

    commandList->SetPipelineState( m_DisplayPipelineState );
    commandList->SetGraphicsRootSignature( m_DisplayRootSignature );

    commandList->SetViewport( m_Viewport );
    commandList->SetScissorRect( m_ScissorRect );

    commandList->SetRenderTarget( m_RenderTarget );
    

     // Floor plane.
    float scalePlane      = 20.0f;
    float translateOffset = scalePlane / 2.0f;

    commandList->SetShaderResourceView( DisplayRootParameters::Textures, 0,
        m_RenderShaderView, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

    m_Plane->Accept( visitor );

    //m_MiniPlane->Accept( visitor );

    // Resolve the MSAA render target to the swapchain's backbuffer.
    auto& swapChainRT         = m_SwapChain->GetRenderTarget();
    auto  swapChainBackBuffer = swapChainRT.GetTexture( AttachmentPoint::Color0 );
    auto  drawRenderTarget    = m_RenderTarget.GetTexture( AttachmentPoint::Color0 );

    commandList->ResolveSubresource( swapChainBackBuffer, drawRenderTarget );
#else

    auto& swapChainRT         = m_SwapChain->GetRenderTarget();
    auto swapChainBackBuffer = swapChainRT.GetTexture( AttachmentPoint::Color0 );

    #if RAY_TRACER
    commandList->CopyResource( swapChainBackBuffer, m_RayOutputResource );
    #else
    commandList->CopyResource( swapChainBackBuffer, m_DummyTexture );
    #endif

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
