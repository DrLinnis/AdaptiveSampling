#include <DummyGame.h>

#include <SceneVisitor.h>

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
#include <dx12lib/AccelerationStructure.h>

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

namespace PostProcessingRootParamaters
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

void DummyGame::CreatePostProcessor(const D3D12_STATIC_SAMPLER_DESC* sampler) 
{
    // Load compute shader
    ComPtr<ID3DBlob> cs;
    ThrowIfFailed( D3DReadFileToBlob( L"data/shaders/Playground/PostProcess.cso", &cs ) );

    // Create root signature
    CD3DX12_DESCRIPTOR_RANGE1 output( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
                                      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE );

    CD3DX12_ROOT_PARAMETER1 rootParameters[PostProcessingRootParamaters::NumRootParameters];
    // Division by 4 becase sizeof gives in number of bytes, not 32 bits.
    rootParameters[PostProcessingRootParamaters::Output].InitAsDescriptorTable( 1, &output );
    rootParameters[PostProcessingRootParamaters::Input].InitAsShaderResourceView( 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                                                                                  D3D12_SHADER_VISIBILITY_ALL );

    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1( PostProcessingRootParamaters::NumRootParameters, rootParameters, 0, sampler,
                                       rootSignatureFlags );

    m_PostProcessRootSignature = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

    // Create Pipeline State Object (PSO) for compute shader
    struct RayPipelineState
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS             CS;
    } rayPipelineStateStream;

    rayPipelineStateStream.pRootSignature = m_PostProcessRootSignature->GetD3D12RootSignature().Get();
    rayPipelineStateStream.CS             = CD3DX12_SHADER_BYTECODE( cs.Get() );

    m_PostProcessPipelineState = m_Device->CreatePipelineStateObject( rayPipelineStateStream );
}

void DummyGame::CreateDisplayPipeline( const D3D12_STATIC_SAMPLER_DESC* sampler, DXGI_FORMAT backBufferFormat )
{
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
}

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

    // Create a Cube mesh
    m_Plane = commandList->CreatePlane( 2, 2 );


    // Create a color buffer with sRGB for gamma correction.
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // Create a colour descriptor with appropriate size
    auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D( backBufferFormat, m_Width, m_Height);

    // Create an off-screen render target with a single color buffer.
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    m_RenderShaderResource = m_Device->CreateTexture( colorDesc );
    m_RenderShaderResource->SetName( L"Display Render Target" );

    //CD3DX12_RESOURCE_DESC::B

    m_RenderShaderView = m_Device->CreateShaderResourceView( m_RenderShaderResource );

    // Create an off-screen render for the compute shader
    {
        D3D12_RESOURCE_DESC stagingDesc = m_RenderShaderResource->GetD3D12ResourceDesc();
        stagingDesc.Format              = backBufferFormat;
        stagingDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;  // try or eq else

        m_StagingResource = m_Device->CreateTexture( stagingDesc );
        m_StagingResource->SetName( L"Post Processing Render Buffer" );

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                           = Texture::GetUAVCompatableFormat( stagingDesc.Format );
        uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2DArray.MipSlice          = 0;

        m_StagingUnorderedAccessView = m_Device->CreateUnorderedAccessView( m_StagingResource, nullptr, &uavDesc );
    }
    

    // Start loading resources while the rest of the resources are created.
    commandQueue.ExecuteCommandList( commandList ); 
    CD3DX12_STATIC_SAMPLER_DESC pointClampSampler( 0, D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP );

    //Post Processing Compute Shader
    CreatePostProcessor( &pointClampSampler );

    // Display Pipeline
    CreateDisplayPipeline( &pointClampSampler, backBufferFormat );

    // Make sure the command queue is finished loading resources before rendering the first frame.
    commandQueue.Flush();

#if 1
    auto& commandQueueCompute = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE );
    commandList               = commandQueueCompute.GetCommandList();

    std::shared_ptr<dx12lib::VertexBuffer> pVertexBuffer = m_Plane->GetRootNode()->GetMesh()->GetVertexBuffer( 0 );
    std::shared_ptr<dx12lib::IndexBuffer>  pIndexBuffer  = m_Plane->GetRootNode()->GetMesh()->GetIndexBuffer();

    std::shared_ptr<AccelerationStructure> bottomLevelBuffers =
        AccelerationStructure::CreateBottomLevelAS( m_Device, commandList, pVertexBuffer, pIndexBuffer );

    std::shared_ptr<AccelerationStructure> topLevelBuffers = 
        AccelerationStructure::CreateTopLevelAS( m_Device, commandList, bottomLevelBuffers->GetResult(), &mTlasSize );

    commandQueueCompute.ExecuteCommandList( commandList );
    commandQueueCompute.Flush();

    topLevelAS = topLevelBuffers->GetResult();
    bottomLevelAS = bottomLevelBuffers->GetResult();

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
    m_Plane.reset();

    m_StagingUnorderedAccessView.reset();
    m_StagingResource.reset();

    m_RenderShaderView.reset();
    m_RenderShaderResource.reset();

    m_DisplayRootSignature.reset();
    m_PostProcessRootSignature.reset();

    m_DisplayPipelineState.reset();
    m_PostProcessPipelineState.reset();

    m_RenderTarget.Reset();

    m_GUI.reset();
    m_SwapChain.reset();
    m_Device.reset();
}

static double g_FPS = 0.0;

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

    // Create a scene visitor that is used to perform the actual rendering of the meshes in the scenes.
    SceneVisitor visitor( *commandList );

    // Post process compute shader execute and transfer staging resource to display.
    {
        commandList->SetPipelineState( m_PostProcessPipelineState );
        commandList->SetComputeRootSignature( m_PostProcessRootSignature );

        // commandList->ClearTexture( m_StagingResource, clearColor );

        // set uniforms
        {
            commandList->SetUnorderedAccessView( PostProcessingRootParamaters::Output, 0,
                                                 m_StagingUnorderedAccessView );
        }

        commandList->Dispatch( Math::DivideByMultiple( m_Width, 16 ), Math::DivideByMultiple( m_Height, 9 ), 1 );

        commandList->UAVBarrier( m_StagingResource );

        if ( m_RenderShaderResource->GetD3D12Resource() != m_StagingResource->GetD3D12Resource() )
        {
            commandList->CopyResource( m_RenderShaderResource, m_StagingResource );
        }
    }
    
    /*
        Display Pipeline render!
    */

    // Clear the render targets.
    commandList->ClearTexture( m_RenderTarget.GetTexture( AttachmentPoint::Color0 ), clearColor );

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

    // Resolve the MSAA render target to the swapchain's backbuffer.
    auto& swapChainRT         = m_SwapChain->GetRenderTarget();
    auto  swapChainBackBuffer = swapChainRT.GetTexture( AttachmentPoint::Color0 );
    auto  drawRenderTarget    = m_RenderTarget.GetTexture( AttachmentPoint::Color0 );

    commandList->ResolveSubresource( swapChainBackBuffer, drawRenderTarget );

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
            m_Pitch -= e.RelY * mouseSpeed;

            m_Pitch = std::clamp( m_Pitch, -90.0f, 90.0f );

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
