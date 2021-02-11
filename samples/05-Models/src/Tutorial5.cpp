#include <Tutorial5.h>

#include <EffectPSO.h>
#include <SceneVisitor.h>

#include <GameFramework/Window.h>

#include <dx12lib/CommandList.h>
#include <dx12lib/CommandQueue.h>
#include <dx12lib/Device.h>
#include <dx12lib/GUI.h>
#include <dx12lib/Helpers.h>
#include <dx12lib/Material.h>
#include <dx12lib/Mesh.h>
#include <dx12lib/RootSignature.h>
#include <dx12lib/Scene.h>
#include <dx12lib/SceneNode.h>
#include <dx12lib/SwapChain.h>
#include <dx12lib/Texture.h>

#include <assimp/DefaultLogger.hpp>

#include <DirectXColors.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <d3dx12.h>

#include <ShObjIdl.h>  // For IFileOpenDialog
#include <shlwapi.h>

#include <regex>

using namespace Microsoft::WRL;
using namespace DirectX;
using namespace dx12lib;

// Builds a look-at (world) matrix from a point, up and direction vectors.
XMMATRIX XM_CALLCONV LookAtMatrix( FXMVECTOR Position, FXMVECTOR Direction, FXMVECTOR Up )
{
    assert( !XMVector3Equal( Direction, XMVectorZero() ) );
    assert( !XMVector3IsInfinite( Direction ) );
    assert( !XMVector3Equal( Up, XMVectorZero() ) );
    assert( !XMVector3IsInfinite( Up ) );

    XMVECTOR R2 = XMVector3Normalize( Direction );

    XMVECTOR R0 = XMVector3Cross( Up, R2 );
    R0          = XMVector3Normalize( R0 );

    XMVECTOR R1 = XMVector3Cross( R2, R0 );

    XMMATRIX M( R0, R1, R2, Position );

    return M;
}

// A regular express used to extract the relavent part of an Assimp log message.
static const std::regex gs_AssimpLogRegex( R"((?:Debug|Info|Warn|Error),\s*(.*)\n)" );

template<spdlog::level::level_enum lvl>
class LogStream : public Assimp::LogStream
{
public:
    LogStream( Logger logger )
    : m_Logger( logger )
    {}

    virtual void write( const char* message ) override
    {
        // Extract just the part of the message we want to log with spdlog.
        std::cmatch match;
        std::regex_search( message, match, gs_AssimpLogRegex );

        if ( match.size() > 1 )
        {
            m_Logger->log( lvl, match.str( 1 ) );
        }
    }

private:
    Logger m_Logger;
};

using DebugLogStream = LogStream<spdlog::level::debug>;
using InfoLogStream  = LogStream<spdlog::level::info>;
using WarnLogStream  = LogStream<spdlog::level::warn>;
using ErrorLogStream = LogStream<spdlog::level::err>;

Tutorial5::Tutorial5( const std::wstring& name, int width, int height, bool vSync )
: m_ScissorRect { 0, 0, LONG_MAX, LONG_MAX }
, m_Viewport( CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) ) )
, m_CameraController( m_Camera )
, m_AnimateLights( false )
, m_Fullscreen( false )
, m_AllowFullscreenToggle( true )
, m_ShowFileOpenDialog( false )
, m_CancelLoading( false )
, m_ShowControls( true )
, m_Width( width )
, m_Height( height )
, m_IsLoading( true )
, m_FPS( 0.0f )
{
#if _DEBUG
    Device::EnableDebugLayer();
#endif
    // Create a spdlog logger for the demo.
    m_Logger = GameFramework::Get().CreateLogger( "05-Models" );
    // Create logger for assimp.
    auto assimpLogger = GameFramework::Get().CreateLogger( "ASSIMP" );

    // Setup assimp logging.
#if defined( _DEBUG )
    Assimp::Logger::LogSeverity logSeverity = Assimp::Logger::VERBOSE;
#else
    Assimp::Logger::LogSeverity logSeverity = Assimp::Logger::NORMAL;
#endif
    // Create a default logger with no streams (we'll supply our own).
    auto assimpDefaultLogger = Assimp::DefaultLogger::create( "", logSeverity, 0 );
    assimpDefaultLogger->attachStream( new DebugLogStream( assimpLogger ), Assimp::Logger::Debugging );
    assimpDefaultLogger->attachStream( new InfoLogStream( assimpLogger ), Assimp::Logger::Info );
    assimpDefaultLogger->attachStream( new WarnLogStream( assimpLogger ), Assimp::Logger::Warn );
    assimpDefaultLogger->attachStream( new ErrorLogStream( assimpLogger ), Assimp::Logger::Err );

    // Create  window for rendering to.
    m_Window = GameFramework::Get().CreateWindow( name, width, height );

    // Hookup Window callbacks.
    m_Window->Update += UpdateEvent::slot( &Tutorial5::OnUpdate, this );
    m_Window->Resize += ResizeEvent::slot( &Tutorial5::OnResize, this );
    m_Window->DPIScaleChanged += DPIScaleEvent::slot( &Tutorial5::OnDPIScaleChanged, this );
    m_Window->KeyPressed += KeyboardEvent::slot( &Tutorial5::OnKeyPressed, this );
    m_Window->KeyReleased += KeyboardEvent::slot( &Tutorial5::OnKeyReleased, this );
    m_Window->MouseMoved += MouseMotionEvent::slot( &Tutorial5::OnMouseMoved, this );
}

Tutorial5::~Tutorial5()
{
    Assimp::DefaultLogger::kill();
}

uint32_t Tutorial5::Run()
{

    LoadContent();

    // Only show the window after content has been loaded.
    m_Window->Show();

    auto retCode = GameFramework::Get().Run();

    // Make sure the loading task is finished
    m_LoadingTask.get();

    UnloadContent();

    return retCode;
}

bool Tutorial5::LoadingProgress( float loadingProgress )
{
    m_LoadingProgress = loadingProgress;

    // This function should return false to cancel the loading process.
    return !m_CancelLoading;
}

bool Tutorial5::LoadScene( const std::wstring& sceneFile )
{
    using namespace std::placeholders;  // For _1 used to denote a placeholder argument for std::bind.

    m_IsLoading     = true;
    m_CancelLoading = false;

    auto& commandQueue = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_COPY );
    auto  commandList  = commandQueue.GetCommandList();

    // Load a scene, passing an optional function object for receiving loading progress events.
    m_LoadingText = std::string( "Loading " ) + ConvertString( sceneFile ) + "...";
    auto scene    = commandList->LoadSceneFromFile( sceneFile, 1.0, std::bind( &Tutorial5::LoadingProgress, this, _1 ));

    if ( scene )
    {
        // Scale the scene so it fits in the camera frustum.
        DirectX::BoundingSphere s;
        BoundingSphere::CreateFromBoundingBox( s, scene->GetAABB() );
        auto scale = 50.0f / ( s.Radius * 2.0f );
        s.Radius *= scale;

        scene->GetRootNode()->SetLocalTransform( XMMatrixScaling( scale, scale, scale ) );

        // Position the camera so that it is looking at the loaded scene.
        auto cameraRotation   = m_Camera.get_Rotation();
        auto cameraFoV        = m_Camera.get_FoV();
        auto distanceToObject = s.Radius / std::tanf( XMConvertToRadians( cameraFoV ) / 2.0f );

        auto cameraPosition = XMVectorSet( 0, 0, -distanceToObject, 1 );
        //        cameraPosition      = XMVector3Rotate( cameraPosition, cameraRotation );
        auto focusPoint = XMVectorSet( s.Center.x * scale, s.Center.y * scale, s.Center.z * scale, 1.0f );
        cameraPosition  = cameraPosition + focusPoint;

        m_Camera.set_Translation( cameraPosition );
        m_Camera.set_FocalPoint( focusPoint );

        m_Scene = scene;
    }

    commandQueue.ExecuteCommandList( commandList );

    // Ensure that the scene is completely loaded before rendering.
    commandQueue.Flush();

    // Loading is finished.
    m_IsLoading = false;

    return scene != nullptr;
}

void Tutorial5::LoadContent()
{
    m_Device = Device::Create();
    m_Logger->info( L"Device created: {}", m_Device->GetDescription() );

    m_SwapChain = m_Device->CreateSwapChain( m_Window->GetWindowHandle(), DXGI_FORMAT_R8G8B8A8_UNORM );
    m_GUI       = m_Device->CreateGUI( m_Window->GetWindowHandle(), m_SwapChain->GetRenderTarget() );

    // This magic here allows ImGui to process window messages.
    GameFramework::Get().WndProcHandler += WndProcEvent::slot( &GUI::WndProcHandler, m_GUI );

    // Start the loading task to perform async loading of the scene file.
    m_LoadingTask = std::async( std::launch::async, std::bind( &Tutorial5::LoadScene, this,
                                                               L"Assets/Models/crytek-sponza/sponza_nobanner.obj" ) );

    // Load a few (procedural) models to represent the light sources in the scene.
    auto& commandQueue = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_COPY );
    auto  commandList  = commandQueue.GetCommandList();

    m_Sphere = commandList->CreateSphere( 0.1f );
    m_Cone   = commandList->CreateCone( 0.1f, 0.2f );
    m_Axis   = commandList->LoadSceneFromFile( L"Assets/Models/axis_of_evil.nff" );

    auto fence = commandQueue.ExecuteCommandList( commandList );

    // Create a PSOs
    m_LightingPSO = std::make_shared<EffectPSO>( m_Device, true, false );
    m_DecalPSO    = std::make_shared<EffectPSO>( m_Device, true, true );
    m_UnlitPSO    = std::make_shared<EffectPSO>( m_Device, false, false );

    // Create a color buffer with sRGB for gamma correction.
    DXGI_FORMAT backBufferFormat  = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

    // Check the best multisample quality level that can be used for the given back buffer format.
    DXGI_SAMPLE_DESC sampleDesc = m_Device->GetMultisampleQualityLevels( backBufferFormat );

    // Create an off-screen render target with a single color buffer and a depth buffer.
    auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D( backBufferFormat, m_Width, m_Height, 1, 1, sampleDesc.Count,
                                                   sampleDesc.Quality, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET );
    D3D12_CLEAR_VALUE colorClearValue;
    colorClearValue.Format   = colorDesc.Format;
    colorClearValue.Color[0] = 0.4f;
    colorClearValue.Color[1] = 0.6f;
    colorClearValue.Color[2] = 0.9f;
    colorClearValue.Color[3] = 1.0f;

    auto colorTexture = m_Device->CreateTexture( colorDesc, &colorClearValue );
    colorTexture->SetName( L"Color Render Target" );

    // Create a depth buffer.
    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D( depthBufferFormat, m_Width, m_Height, 1, 1, sampleDesc.Count,
                                                   sampleDesc.Quality, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
    D3D12_CLEAR_VALUE depthClearValue;
    depthClearValue.Format       = depthDesc.Format;
    depthClearValue.DepthStencil = { 1.0f, 0 };

    auto depthTexture = m_Device->CreateTexture( depthDesc, &depthClearValue );
    depthTexture->SetName( L"Depth Render Target" );

    // Attach the textures to the render target.
    m_RenderTarget.AttachTexture( AttachmentPoint::Color0, colorTexture );
    m_RenderTarget.AttachTexture( AttachmentPoint::DepthStencil, depthTexture );

    // Make sure the copy command queue is finished before leaving this function.
    commandQueue.WaitForFenceValue( fence );
}

void Tutorial5::UnloadContent() {}

void Tutorial5::OnUpdate( UpdateEventArgs& e )
{
    static uint64_t frameCount = 0;
    static double   totalTime  = 0.0;

    totalTime += e.DeltaTime;
    frameCount++;

    if ( totalTime > 1.0 )
    {
        m_FPS = frameCount / totalTime;

        wchar_t buffer[512];
        ::swprintf_s( buffer, L"Models [FPS: %f]", m_FPS );
        m_Window->SetWindowTitle( buffer );

        frameCount = 0;
        totalTime  = 0.0;
    }

    if ( m_ShowFileOpenDialog )
    {
        m_ShowFileOpenDialog = false;
        OpenFile();
    }

    m_SwapChain->WaitForSwapChain();

    // Process keyboard, mouse, and pad input.
    GameFramework::Get().ProcessInput();
    m_CameraController.Update( e );

    // Move the Axis model to the focal point of the camera.
    XMVECTOR cameraPoint = m_Camera.get_FocalPoint();
    XMMATRIX translationMatrix = XMMatrixTranslationFromVector( cameraPoint );
    XMMATRIX scaleMatrix = XMMatrixScaling( 0.01f, 0.01f, 0.01f );
    m_Axis->GetRootNode()->SetLocalTransform( scaleMatrix * translationMatrix );

    XMMATRIX viewMatrix = m_Camera.get_ViewMatrix();

    const int numDirectionalLights = 3;

    static const XMVECTORF32 LightColors[] = { Colors::White, Colors::OrangeRed, Colors::Blue };

    static float lightAnimTime = 0.0f;
    if ( m_AnimateLights )
    {
        lightAnimTime += static_cast<float>( e.DeltaTime ) * 0.5f * XM_PI;
    }

    const float radius                 = 1.0f;
    float       directionalLightOffset = numDirectionalLights > 0 ? 2.0f * XM_PI / numDirectionalLights : 0;

    m_DirectionalLights.resize( numDirectionalLights );
    for ( int i = 0; i < numDirectionalLights; ++i )
    {
        DirectionalLight& l = m_DirectionalLights[i];

        float angle = lightAnimTime + directionalLightOffset * i;

        XMVECTORF32 positionWS = { static_cast<float>( std::cos( angle ) ) * radius,
                                   static_cast<float>( std::sin( angle ) ) * radius, radius, 1.0f };

        XMVECTOR directionWS = XMVector3Normalize( XMVectorNegate( positionWS ) );
        XMVECTOR directionVS = XMVector3TransformNormal( directionWS, viewMatrix );

        XMStoreFloat4( &l.DirectionWS, directionWS );
        XMStoreFloat4( &l.DirectionVS, directionVS );

        l.Color = XMFLOAT4( LightColors[i] );
    }

    m_LightingPSO->SetDirectionalLights( m_DirectionalLights );
    m_DecalPSO->SetDirectionalLights( m_DirectionalLights );

    OnRender();
}

void Tutorial5::OnResize( ResizeEventArgs& e )
{
    m_Logger->info( "Resize: {}, {}", e.Width, e.Height );

    m_Width  = std::max( 1, e.Width );
    m_Height = std::max( 1, e.Height );

    m_Camera.set_Projection( 45.0f, m_Width / (float)m_Height, 0.1f, 100.0f );
    m_Viewport = CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>( m_Width ), static_cast<float>( m_Height ) );

    m_RenderTarget.Resize( m_Width, m_Height );

    m_SwapChain->Resize( m_Width, m_Height );
}

void Tutorial5::OnRender()
{
    // This is done here to prevent the window switching to fullscreen while rendering the GUI.
    m_Window->SetFullscreen( m_Fullscreen );

    auto& commandQueue = m_Device->GetCommandQueue( D3D12_COMMAND_LIST_TYPE_DIRECT );
    auto  commandList  = commandQueue.GetCommandList();

    const auto& renderTarget = m_IsLoading ? m_SwapChain->GetRenderTarget() : m_RenderTarget;

    if ( m_IsLoading )
    {
        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
        commandList->ClearTexture( renderTarget.GetTexture( AttachmentPoint::Color0 ), clearColor );

        // TODO: Render a loading screen.
    }
    else
    {
        SceneVisitor opaquePass( *commandList, m_Camera, *m_LightingPSO, false );
        SceneVisitor transparentPass( *commandList, m_Camera, *m_DecalPSO, true );
        SceneVisitor unlitPass( *commandList, m_Camera, *m_UnlitPSO, false );

        // Clear the render targets.
        {
            FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

            commandList->ClearTexture( renderTarget.GetTexture( AttachmentPoint::Color0 ), clearColor );
            commandList->ClearDepthStencilTexture( renderTarget.GetTexture( AttachmentPoint::DepthStencil ),
                                                   D3D12_CLEAR_FLAG_DEPTH );
        }

        commandList->SetViewport( m_Viewport );
        commandList->SetScissorRect( m_ScissorRect );
        commandList->SetRenderTarget( m_RenderTarget );

        // Render the scene.
        // Opaque pass.
        m_Scene->Accept( opaquePass );
        m_Axis->Accept( unlitPass );

        // Transparent pass.
        m_Scene->Accept( transparentPass );

        MaterialProperties lightMaterial = Material::Black;
        for ( const auto& l: m_PointLights )
        {
            lightMaterial.Emissive = l.Color;
            auto lightPos          = XMLoadFloat4( &l.PositionWS );
            auto worldMatrix       = XMMatrixTranslationFromVector( lightPos );

            m_Sphere->GetRootNode()->SetLocalTransform( worldMatrix );
            m_Sphere->GetRootNode()->GetMesh()->GetMaterial()->SetMaterialProperties( lightMaterial );
            m_Sphere->Accept( unlitPass );
        }

        for ( const auto& l: m_SpotLights )
        {
            lightMaterial.Emissive = l.Color;
            XMVECTOR lightPos      = XMLoadFloat4( &l.PositionWS );
            XMVECTOR lightDir      = XMLoadFloat4( &l.DirectionWS );
            XMVECTOR up            = XMVectorSet( 0, 1, 0, 0 );

            // Rotate the cone so it is facing the Z axis.
            auto rotationMatrix = XMMatrixRotationX( XMConvertToRadians( -90.0f ) );
            auto worldMatrix    = rotationMatrix * LookAtMatrix( lightPos, lightDir, up );

            m_Cone->GetRootNode()->SetLocalTransform( worldMatrix );
            m_Cone->GetRootNode()->GetMesh()->GetMaterial()->SetMaterialProperties( lightMaterial );
            m_Cone->Accept( unlitPass );
        }

        // Resolve the MSAA render target to the swapchain's backbuffer.
        auto swapChainBackBuffer = m_SwapChain->GetRenderTarget().GetTexture( AttachmentPoint::Color0 );
        auto msaaRenderTarget    = m_RenderTarget.GetTexture( AttachmentPoint::Color0 );

        commandList->ResolveSubresource( swapChainBackBuffer, msaaRenderTarget );
    }

    OnGUI( commandList, m_SwapChain->GetRenderTarget() );

    commandQueue.ExecuteCommandList( commandList );

    m_SwapChain->Present();
}

void Tutorial5::OnKeyPressed( KeyEventArgs& e )
{
    if ( !ImGui::GetIO().WantCaptureKeyboard )
    {
        switch ( e.Key )
        {
        case KeyCode::Escape:
            GameFramework::Get().Stop();
            break;
        case KeyCode::Space:
            m_AnimateLights = !m_AnimateLights;
            break;
        case KeyCode::Enter:
            if ( e.Alt )
            {
            case KeyCode::F11:
                if ( m_AllowFullscreenToggle )
                {
                    m_Fullscreen = !m_Fullscreen;  // Defer window resizing until OnUpdate();
                    // Prevent the key repeat to cause multiple resizes.
                    m_AllowFullscreenToggle = false;
                }
                break;
            }
        case KeyCode::V:
            m_SwapChain->ToggleVSync();
            break;
        case KeyCode::R:
            // Reset camera transform
            m_CameraController.ResetView();
            break;
        case KeyCode::O:
            if ( e.Control )
            {
                OpenFile();
            }
            break;
        }
    }
}

void Tutorial5::OnKeyReleased( KeyEventArgs& e )
{
    if ( !ImGui::GetIO().WantCaptureKeyboard )
    {
        switch ( e.Key )
        {
        case KeyCode::Enter:
            if ( e.Alt )
            {
            case KeyCode::F11:
                m_AllowFullscreenToggle = true;
            }
            break;
        }
    }
}

void Tutorial5::OnMouseMoved( MouseMotionEventArgs& e )
{
    if ( !ImGui::GetIO().WantCaptureMouse ) {}
}

void Tutorial5::OnDPIScaleChanged( DPIScaleEventArgs& e )
{
    m_GUI->SetScaling( e.DPIScale );
}

static void HelpMarker( const char* desc )
{
    ImGui::TextDisabled( "(?)" );
    if ( ImGui::IsItemHovered() )
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos( ImGui::GetFontSize() * 35.0f );
        ImGui::TextUnformatted( desc );
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Tutorial5::OnGUI( const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget )
{
    m_GUI->NewFrame();

    if ( m_IsLoading )
    {
        // Show a progress bar.
        ImGui::SetNextWindowPos( ImVec2( m_Window->GetClientWidth() / 2.0f, m_Window->GetClientHeight() / 2.0f ), 0,
                                 ImVec2( 0.5, 0.5 ) );
        ImGui::SetNextWindowSize( ImVec2( m_Window->GetClientWidth() / 2.0f, 0 ) );

        ImGui::Begin( "Loading", nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoScrollbar );
        ImGui::ProgressBar( m_LoadingProgress );
        ImGui::Text( m_LoadingText.c_str() );
        if ( !m_CancelLoading )
        {
            if ( ImGui::Button( "Cancel" ) )
            {
                m_CancelLoading = true;
            }
        }
        else
        {
            ImGui::Text( "Cancel Loading..." );
        }

        ImGui::End();
    }

    if ( ImGui::BeginMainMenuBar() )
    {
        if ( ImGui::BeginMenu( "File" ) )
        {
            if ( ImGui::MenuItem( "Open file...", "Ctrl+O", nullptr, !m_IsLoading ) )
            {
                m_ShowFileOpenDialog = true;
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Exit", "Esc" ) )
            {
                GameFramework::Get().Stop();
            }
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "View" ) )
        {
            ImGui::MenuItem( "Controls", nullptr, &m_ShowControls );

            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Options" ) )
        {
            bool vSync = m_SwapChain->GetVSync();
            if ( ImGui::MenuItem( "V-Sync", "V", &vSync ) )
            {
                m_SwapChain->SetVSync( vSync );
            }

            bool fullscreen = m_Window->IsFullscreen();
            if ( ImGui::MenuItem( "Full screen", "Alt+Enter", &fullscreen ) )
            {
                // m_Window->SetFullscreen( fullscreen );
                // Defer the window resizing until the reference to the render target is released.
                m_Fullscreen = fullscreen;
            }

            ImGui::MenuItem( "Animate Lights", "Space", &m_AnimateLights );

            bool invertY = m_CameraController.IsInverseY();
            if ( ImGui::MenuItem( "Inverse Y", nullptr, &invertY ) )
            {
                m_CameraController.SetInverseY( invertY );
            }
            if ( ImGui::MenuItem( "Reset view", "R" ) )
            {
                m_CameraController.ResetView();
            }

            ImGui::EndMenu();
        }

        char buffer[256];
        {
            sprintf_s( buffer, _countof( buffer ), "FPS: %.2f (%.2f ms)  ", m_FPS, 1.0 / m_FPS * 1000.0 );
            auto fpsTextSize = ImGui::CalcTextSize( buffer );
            ImGui::SameLine( ImGui::GetWindowWidth() - fpsTextSize.x );
            ImGui::Text( buffer );
        }

        ImGui::EndMainMenuBar();
    }

    if ( m_ShowControls && ImGui::Begin( "Controls", &m_ShowControls ) )
    {
        ImGui::Text( "KEYBOARD CONTROLS" );
        ImGui::BulletText( "ESC: Terminate application" );
        ImGui::BulletText( "Alt+Enter: Toggle fullscreen" );
        ImGui::BulletText( "F11: Toggle fullscreen" );
        ImGui::BulletText( "W: Move camera forward" );
        ImGui::BulletText( "A: Move camera left" );
        ImGui::BulletText( "S: Move camera backward" );
        ImGui::BulletText( "D: Move camera right" );
        ImGui::BulletText( "Q: Move camera down" );
        ImGui::BulletText( "E: Move camera up" );
        ImGui::BulletText( "R: Reset view" );
        ImGui::BulletText( "Shift: Boost move/rotate speed" );
        ImGui::BulletText( "Space: Animate lights" );
        ImGui::Separator();

        ImGui::Text( "MOUSE CONTROLS" );
        ImGui::BulletText( "LMB: Rotate camera" );
        ImGui::BulletText( "Mouse wheel: Zoom in/out on focal point" );
        ImGui::Separator();

        ImGui::Text( "GAMEPAD CONTROLS" );
        ImGui::BulletText( "Left analog stick: Move camera" );
        ImGui::BulletText( "Right analog stick: Rotate camera around the focal point" );
        ImGui::BulletText( "Left trigger: Move camera down" );
        ImGui::BulletText( "Right trigger: Move camera up" );
        ImGui::BulletText( "Hold left or right stick: Boost move/rotate speed" );
        ImGui::BulletText( "D-Pad up/down: Zoom in/out on focal point" );

        ImGui::End();
    }
    m_GUI->Render( commandList, renderTarget );
}

// Open a file dialog for the user to select a scene to load.
// @see https://docs.microsoft.com/en-us/windows/win32/learnwin32/example--the-open-dialog-box
// @see
// https://github.com/microsoft/Windows-classic-samples/blob/master/Samples/Win7Samples/winui/shell/appplatform/commonfiledialog/CommonFileDialogApp.cpp
// @see https://www.codeproject.com/Articles/16678/Vista-Goodies-in-C-Using-the-New-Vista-File-Dialog
void Tutorial5::OpenFile()
{
    // clang-format off
    static const COMDLG_FILTERSPEC g_FileFilters[] = { 
        { L"Autodesk", L"*.fbx" }, 
        { L"Collada", L"*.dae" },
        { L"glTF", L"*.gltf;*.glb" },
        { L"Blender 3D", L"*.blend" },
        { L"3ds Max 3DS", L"*.3ds" },
        { L"3ds Max ASE", L"*.ase" },
        { L"Wavefront Object", L"*.obj" },
        { L"Industry Foundation Classes (IFC/Step)", L"*.ifc" },
        { L"XGL", L"*.xgl;*.zgl" },
        { L"Stanford Polygon Library", L"*.ply" },
        { L"AutoCAD DXF", L"*.dxf" },
        { L"LightWave", L"*.lws" },
        { L"LightWave Scene", L"*.lws" },
        { L"Modo", L"*.lxo" },
        { L"Stereolithography", L"*.stl" },
        { L"DirectX X", L"*.x" },
        { L"AC3D", L"*.ac" },
        { L"Milkshape 3D", L"*.ms3d" },
        { L"TrueSpace", L"*.cob;*.scn" },
        { L"Ogre XML", L"*.xml" },
        { L"Irrlicht Mesh", L"*.irrmesh" },
        { L"Irrlicht Scene", L"*.irr" },
        { L"Quake I", L"*.mdl" },
        { L"Quake II", L"*.md2" },
        { L"Quake III", L"*.md3" },
        { L"Quake III Map/BSP", L"*.pk3" },
        { L"Return to Castle Wolfenstein", L"*.mdc" },
        { L"Doom 3", L"*.md5*" },
        { L"Valve Model", L"*.smd;*.vta" },
        { L"Open Game Engine Exchange", L"*.ogx" },
        { L"Unreal", L"*.3d" },
        { L"BlitzBasic 3D", L"*.b3d" },
        { L"Quick3D", L"*.q3d;*.q3s" },
        { L"Neutral File Format", L"*.nff" },
        { L"Sense8 WorldToolKit", L"*.nff" },
        { L"Object File Format", L"*.off" },
        { L"PovRAY Raw", L"*.raw" },
        { L"Terragen Terrain", L"*.ter" },
        { L"Izware Nendo", L"*.ndo" },
        { L"All Files", L"*.*" }
    };
    // clang-format on

    ComPtr<IFileOpenDialog> pFileOpen;
    HRESULT                 hr = CoCreateInstance( CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS( &pFileOpen ) );

    if ( SUCCEEDED( hr ) )
    {
        // Create an event handling object, and hook it up to the dialog.
        // ComPtr<IFileDialogEvents> pDialogEvents;
        // hr = DialogEventHandler_CreateInstance( IID_PPV_ARGS( &pDialogEvents ) );

        if ( SUCCEEDED( hr ) )
        {
            // Setup filters.
            hr = pFileOpen->SetFileTypes( _countof( g_FileFilters ), g_FileFilters );
            pFileOpen->SetFileTypeIndex( 40 );  // All Files (*.*)

            // Show the open dialog box.
            if ( SUCCEEDED( pFileOpen->Show( m_Window->GetWindowHandle() ) ) )
            {
                ComPtr<IShellItem> pItem;
                if ( SUCCEEDED( pFileOpen->GetResult( &pItem ) ) )
                {
                    PWSTR pszFilePath;
                    if ( SUCCEEDED( pItem->GetDisplayName( SIGDN_FILESYSPATH, &pszFilePath ) ) )
                    {
                        // try to load the scene file (asynchronously).
                        m_LoadingTask =
                            std::async( std::launch::async, std::bind( &Tutorial5::LoadScene, this, pszFilePath ) );

                        CoTaskMemFree( pszFilePath );
                    }
                }
            }
        }
    }
}