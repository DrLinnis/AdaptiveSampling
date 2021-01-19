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
#include <dx12lib/SwapChain.h>
#include <dx12lib/Texture.h>
#include <dx12lib/Visitor.h>

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

// An enum for root signature parameters.
// I'm not using scoped enums to avoid the explicit cast that would be required
// to use these as root indices in the root signature.
namespace RootParameters
{
enum
{
    Textures,   // Texture2D DiffuseTexture : register( t0 );
    NumRootParameters
};
}


void DummyGame::FillRandomScene() {
    materialList.clear();

    materialList.push_back( Ray::Material( MODE_MATERIAL_DIALECTIC, 1.5 ) );
    materialList.push_back( Ray::Material( MODE_MATERIAL_COLOUR, { 0.5, 0.5, 0.5 } ) );

    sphereList.clear();
    sphereList.push_back( Ray::Sphere( 0, -1000, 0, 1000, materialList.size() - 1 ));
    for ( int a = -11; a < 11; a++ )
    {
        for ( int b = -11; b < 11; b++ )
        {
            float choose_mat = Math::random_double();
            float center[]   = { a + 0.9 * Math::random_double(), 0.2, b + 0.9 * Math::random_double() };

            if ( choose_mat < 0.8 )
            {
                // diffuse
                std::array<float, 3> albedo = { Math::random_double() * Math::random_double(),
                    Math::random_double() * Math::random_double(),
                    Math::random_double() * Math::random_double()
                };
                materialList.push_back(Ray::Material( MODE_MATERIAL_COLOUR, albedo ));
                sphereList.push_back( Ray::Sphere( center, 0.2, materialList.size() - 1 ) );
            }
            else if ( choose_mat < 0.95 )
            {
                // metal
                std::array<float, 3> albedo = { Math::random_double( 0.5, 1 ) * Math::random_double( 0.5, 1 ),
                                    Math::random_double( 0.5, 1 ) * Math::random_double( 0.5, 1 ),
                                    Math::random_double( 0.5, 1 ) * Math::random_double( 0.5, 1 ) 
                };
                float fuzz       = Math::random_double( 0, 0.5 );
                materialList.push_back( Ray::Material( MODE_MATERIAL_METAL, albedo, fuzz ) );
                sphereList.push_back( Ray::Sphere( center, 0.2, materialList.size() - 1 ));
            }
            else
            {
                // glass
                sphereList.push_back( Ray::Sphere( center[0], center[1], center[2], 
                    0.2, 0 ));
            }
        }
    }

    // Big Glass
    sphereList.push_back( Ray::Sphere( 0, 1, 0, 1, 0 ));

    // Big Colour
    materialList.push_back( Ray::Material( MODE_MATERIAL_COLOUR, { 0.4, 0.2, 0.1 } ) );
    sphereList.push_back( Ray::Sphere( -4, 1, 0, 1, materialList.size() - 1 ));

    // Big Metal
    materialList.push_back( Ray::Material( MODE_MATERIAL_COLOUR, { 0.7, 0.6, 0.5 }, 0.5 ) );
    sphereList.push_back( Ray::Sphere( 4, 1, 0, 1, materialList.size() - 1 ));
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

    // Spheres
    #if 1
    materialList = { Ray::Material( MODE_MATERIAL_COLOUR, { 0.4392, 0.5020, 0.5647 } ),
                     Ray::Material( MODE_MATERIAL_COLOUR, { 0.9373, 0.5569, 0.2196 } ),
                     Ray::Material( MODE_MATERIAL_METAL, { 0.4392, 0.5020, 0.5647 } , 0.5f ) 
    };
    sphereList   = { Ray::Sphere( 0, 1, 5, 1, 0 ),
                     Ray::Sphere( 5, 2, 5, 2.5, 1 ),
                     Ray::Sphere( 0, -25, 0, 25, 2 )
    };
    #else
    this->FillRandomScene();
    #endif

    // Default ray context
    rayCB.NbrSpheres       = static_cast<uint32_t>( sphereList.size() );
    rayCB.NbrMaterials     = static_cast<uint32_t>( materialList.size() );
    rayCB.WindowResolution = { static_cast<uint32_t>( m_Width ), static_cast<uint32_t>( m_Height ) };

    rayCB.VoidColour = { 0.529, 0.808, 0.922, 1 };

    // Default camera
    camCB.CameraWindow = { 1.6, 0.9 };
    camCB.CameraPos    = { 0, 1, -10, 0};
    camCB.CameraLookAt = { 0, 0, 0, 0 };
    camCB.CameraUp     = { 0, 1, 0, 0 };
}

DummyGame::~DummyGame()
{
    sphereList.clear();
    materialList.clear();
}

uint32_t DummyGame::Run()
{
    LoadContent();

    m_Window->Show();

    uint32_t retCode = GameFramework::Get().Run();

    UnloadContent();

    return retCode;
}

#define RAY_PIPELINE 1

bool DummyGame::LoadContent()
{
    m_Device    = Device::Create();

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

    #if RAY_PIPELINE
    // Create an off-screen render for the compute shader
    D3D12_RESOURCE_DESC stagingDesc = m_RenderShaderResource->GetD3D12ResourceDesc();
    stagingDesc.Format              = backBufferFormat;
    stagingDesc.Flags               |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // try or eq else
    
    m_StagingResource = m_Device->CreateTexture( stagingDesc);
    m_StagingResource->SetName( L"Ray Render Buffer" );

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = Texture::GetUAVCompatableFormat( stagingDesc.Format );
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2DArray.MipSlice        = 0;

    m_StagingUnorderedAccessView = m_Device->CreateUnorderedAccessView( m_StagingResource, nullptr, &uavDesc );

    #endif
    // Start loading resources while the rest of the resources are created.
    commandQueue.ExecuteCommandList( commandList ); 
    CD3DX12_STATIC_SAMPLER_DESC linearClampSampler( 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                                                    D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                                    D3D12_TEXTURE_ADDRESS_MODE_CLAMP );
    // Ray tracer pipeline
#if RAY_PIPELINE
    {
        // Load compute shader
        ComPtr<ID3DBlob> cs;
        ThrowIfFailed( D3DReadFileToBlob( L"data/shaders/RayTray/RayTracer.cso", &cs ) );
    
        // Create root signature
        CD3DX12_DESCRIPTOR_RANGE1 output( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
                                          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE );

        CD3DX12_ROOT_PARAMETER1 rootParameters[Ray::NumRootParameters];
        // Division by 4 becase sizeof gives in number of bytes, not 32 bits.
        rootParameters[Ray::RayContext].InitAsConstants( sizeof( Ray::RayCB ) / 4, 0 );
        rootParameters[Ray::CameraContext].InitAsConstants( sizeof( Ray::CamCB ) / 4, 1 );
        rootParameters[Ray::Output].InitAsDescriptorTable( 1, &output);
        rootParameters[Ray::SphereList].InitAsShaderResourceView( 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                                                                    D3D12_SHADER_VISIBILITY_ALL );
        rootParameters[Ray::MaterialList].InitAsShaderResourceView( 1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                                                                    D3D12_SHADER_VISIBILITY_ALL );

        

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( Ray::NumRootParameters, 
                            rootParameters, 1, &linearClampSampler, rootSignatureFlags );

        m_RayRootSignature = m_Device->CreateRootSignature( rootSignatureDescription.Desc_1_1 );

        // Create Pipeline State Object (PSO) for compute shader
        struct RayPipelineState
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_CS             CS;
        } rayPipelineStateStream;

        rayPipelineStateStream.pRootSignature = m_RayRootSignature->GetD3D12RootSignature().Get();
        rayPipelineStateStream.CS             = CD3DX12_SHADER_BYTECODE( cs.Get() );

        m_RayPipelineState = m_Device->CreatePipelineStateObject( rayPipelineStateStream );
    }
    #endif

    // Display Pipeline
    {
        // Load the shaders.
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed( D3DReadFileToBlob( L"data/shaders/RayTray/Vertex.cso", &vs ) );
        ThrowIfFailed( D3DReadFileToBlob( L"data/shaders/RayTray/Pixel.cso", &ps ) );

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 );

        CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
        rootParameters[RootParameters::Textures].InitAsDescriptorTable( 1, &descriptorRange,
                                                                               D3D12_SHADER_VISIBILITY_PIXEL );

        CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler( 0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR );
        //CD3DX12_STATIC_SAMPLER_DESC anisotropicSampler( 0, D3D12_FILTER_ANISOTROPIC );

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1( RootParameters::NumRootParameters, rootParameters, 1, &linearClampSampler,
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

    // Make sure the command queue is finished loading resources before rendering the first frame.
    commandQueue.Flush();

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

    rayCB.WindowResolution = { static_cast<uint32_t>( m_Width ), static_cast<uint32_t>( m_Height ) };
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
    m_RayRootSignature.reset();

    m_DisplayPipelineState.reset();
    m_RayPipelineState.reset();

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

        float dx_vert = cos( m_Pitch / 10.0f ) * sin( m_Yaw / 10.0f + Math::PI / 2 );
        float dz_vert = cos( m_Pitch / 10.0f ) * cos( m_Yaw / 10.0f + Math::PI / 2 );

        float dx = cos(m_Pitch / 10.0f) * sin( m_Yaw / 10.0f );
        float dy = sin( m_Pitch / 10.0f );
        float dz = cos( m_Pitch / 10.0f ) * cos( m_Yaw / 10.0f );

        camCB.CameraPos.x += speed * e.DeltaTime * ( dx * ( m_Forward - m_Backward ) + dx_vert * ( m_Left - m_Right ) );

        camCB.CameraPos.y += speed * e.DeltaTime * ( m_Up - m_Down + dy * ( m_Forward - m_Backward ) );

        camCB.CameraPos.z += speed * e.DeltaTime * ( dz * ( m_Forward - m_Backward ) + dz_vert * ( m_Left - m_Right ) );

        camCB.CameraLookAt.x = camCB.CameraPos.x + sin( m_Yaw / 10.0f) * lookat_dist ;
        camCB.CameraLookAt.y = camCB.CameraPos.y + sin( m_Pitch / 10.0f ) * lookat_dist;
        camCB.CameraLookAt.z = camCB.CameraPos.z + cos( m_Yaw / 10.0f ) * lookat_dist;
    
        //rayCB.TimeSeed = Math::random_double();
        rayCB.TimeSeed = theta;

        //sphereList[1].Position.x = 0;
        //sphereList[1].Position.y = 65 * cos( theta / 2.0f );
        //sphereList[1].Position.z = 65 * sin( theta / 2.0f );

        //sphereList[0].Position.x = 0;
        //sphereList[0].Position.y = 60 * cos( theta / 3.0f );
        //sphereList[0].Position.z = 60 * sin( theta / 3.0f );

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

#if RAY_PIPELINE
    commandList->SetPipelineState( m_RayPipelineState );
    commandList->SetComputeRootSignature( m_RayRootSignature );

    //commandList->ClearTexture( m_StagingResource, clearColor );
    
    commandList->SetCompute32BitConstants( Ray::RayContext, rayCB );

    commandList->SetCompute32BitConstants( Ray::CameraContext, camCB );

    commandList->SetUnorderedAccessView( Ray::Output, 0, m_StagingUnorderedAccessView);
    
    commandList->SetComputeDynamicStructuredBuffer( Ray::SphereList, sphereList );

    commandList->SetComputeDynamicStructuredBuffer( Ray::MaterialList, materialList );

    commandList->Dispatch( Math::DivideByMultiple( m_Width, Ray::BLOCK_WIDTH),
                           Math::DivideByMultiple( m_Height, Ray::BLOCK_HEIGHT ), 1 
    );  

    commandList->UAVBarrier( m_StagingResource );

    if ( m_RenderShaderResource->GetD3D12Resource() != m_StagingResource->GetD3D12Resource() )
    {
        commandList->CopyResource( m_RenderShaderResource, m_StagingResource );
    }
    #endif
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

    commandList->SetShaderResourceView( RootParameters::Textures, 0,
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
