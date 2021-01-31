#pragma once

#include <GameFramework/GameFramework.h>

#include <vector>

#include <dx12lib/RenderTarget.h>
#include <dx12lib/AccelerationStructure.h>

#include <DirectXMath.h>

#include <string>

#include <memory>

namespace dx12lib
{
class CommandList;
class Device;
class GUI;
class Mesh;
class PipelineStateObject;
class RT_PipelineStateObject;
class Scene;
class RootSignature;
class ShaderResourceView;
class UnorderedAccessView;
class SwapChain;
class Texture;
class AccelerationBuffer;
class AccelerationStructure;
class MappableBuffer;
class ShaderTableResourceView;
} 

class Window;  // From GameFramework.


#define RAY_TRACER     1
#define POST_PROCESSOR 0
#define RASTER_DISPLAY 0


struct CameraCB
{
    CameraCB( float x, float y, float z )
    : x( x )
    , y( y )
    , z( z )
    {}

    float x, y, z;
};


class DummyGame
{
public:
    DummyGame( const std::wstring& name, int width, int height, bool vSync = false );
    virtual ~DummyGame();

    /**
     * Start the main game loop.
     */
    uint32_t Run();

    /**
     *  Load content required for the demo.
     */
    bool LoadContent();

    /**
     *  Unload demo specific content that was loaded in LoadContent.
     */
    void UnloadContent();

protected:
    /**
     *  Update the game logic.
     */
    void OnUpdate( UpdateEventArgs& e );

    /**
     *  Render stuff.
     */
    void OnRender();

    /**
     * Invoked by the registered window when a key is pressed
     * while the window has focus.
     */
    void OnKeyPressed( KeyEventArgs& e );

    /**
     * Invoked when a key on the keyboard is released.
     */
    void OnKeyReleased( KeyEventArgs& e );

    /**
     * Invoked when the mouse is moved over the registered window.
     */
    void OnMouseMoved( MouseMotionEventArgs& e );

    /**
     * Invoked when the mouse wheel is scrolled while the registered window has focus.
     */
    void OnMouseWheel( MouseWheelEventArgs& e );

    void OnResize( ResizeEventArgs& e );

    void OnDPIScaleChanged( DPIScaleEventArgs& e );

    void OnGUI( const std::shared_ptr<dx12lib::CommandList>& commandList, const dx12lib::RenderTarget& renderTarget );

private:
    // Added tutorial member:
#if RAY_TRACER
    
    std::shared_ptr<dx12lib::AccelerationBuffer> m_BLAS_sphere;
    std::shared_ptr<dx12lib::AccelerationBuffer> m_BLAS_plane;

    uint64_t mTlasSize = 0;
    
    std::shared_ptr<dx12lib::RootSignature>             m_RayGenRootSig;
    std::shared_ptr<dx12lib::RootSignature>             m_SphereHitRootSig;
    std::shared_ptr<dx12lib::RootSignature>             m_PlaneHitRootSig;
    std::shared_ptr<dx12lib::RootSignature>             m_EmptyLocalRootSig;
    std::shared_ptr<dx12lib::RootSignature>             m_GlobalRootSig;

    std::shared_ptr<dx12lib::RT_PipelineStateObject>    m_RayPipelineState; 
    
    std::shared_ptr<dx12lib::MappableBuffer>            m_InstanceDescBuffer;

    size_t                                              m_ShaderTableEntrySize = 0;
    std::shared_ptr<dx12lib::MappableBuffer>            m_RaygenShaderTable;
    std::shared_ptr<dx12lib::MappableBuffer>            m_MissShaderTable;
    std::shared_ptr<dx12lib::MappableBuffer>            m_HitShaderTable;

    std::shared_ptr<dx12lib::MappableBuffer> m_RayCamCB;
    std::shared_ptr<dx12lib::MappableBuffer> m_MissSdrCBs[3];

    dx12lib::AccelerationStructure m_TlasBuffers = {};

    struct Colour
    {
        Colour( float r, float g, float b );

        float r, g, b, padding;
    };

    Colour m_SphereHintedColours[3] = { 
        Colour( 0, 1, 0 ), 
        Colour( 0.2, 0.8, 0.6 ),
        Colour( 0.3, 0.2, 0.69 ) 
    };

    // Tut 6
    std::shared_ptr<dx12lib::Texture>                   m_RayOutputResource;

    std::shared_ptr<dx12lib::ShaderTableResourceView>   m_RayShaderHeap;

    std::shared_ptr<dx12lib::UnorderedAccessView>       m_RayOutputUAV;
    std::shared_ptr<dx12lib::ShaderResourceView>        m_TlasSRV;

    // new helper functions
    
    /*
        Create the ray tracing pipeline with settings as local root signatures, 
            ray depth, payload, etc etc.
    */
    void CreateRayTracingPipeline(  );

    /*
        Upload shader programs and point at relative resources needed per function
    */
    void CreateShaderTable( );
    
    /*
        Create Output image and CBV_SRV_UAV heap
    */
    void CreateShaderResource( DXGI_FORMAT backBufferFormat );
    
    /*
        Create Accelleration structures needed for the geometry
    */
    void CreateAccelerationStructure();

    /*
        Create the constant buffer we use for sphere colouring
    */
    void CreateConstantBuffer();

#endif

    // refactored helper functions
    void CreatePostProcessor( const D3D12_STATIC_SAMPLER_DESC* sampler, DXGI_FORMAT backBufferFormat );
    void CreateDisplayPipeline( const D3D12_STATIC_SAMPLER_DESC* sampler, DXGI_FORMAT backBufferFormat );
    


    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    // General
    std::shared_ptr<dx12lib::Device>    m_Device;
    std::shared_ptr<dx12lib::SwapChain> m_SwapChain;
    std::shared_ptr<dx12lib::GUI>       m_GUI;

    std::shared_ptr<Window> m_Window;

    // Some geometry to render.
    std::shared_ptr<dx12lib::Scene> m_RayPlane;
    std::shared_ptr<dx12lib::Scene> m_RaySphere;
    
    std::shared_ptr<dx12lib::Texture> m_DummyTexture;

    // Render target
    dx12lib::RenderTarget m_RenderTarget;

#if RASTER_DISPLAY
    std::shared_ptr<dx12lib::RootSignature> m_DisplayRootSignature;
    std::shared_ptr<dx12lib::PipelineStateObject> m_DisplayPipelineState;

    std::shared_ptr<dx12lib::Texture>            m_RenderShaderResource;
    std::shared_ptr<dx12lib::ShaderResourceView> m_RenderShaderView;
#endif

#if POST_PROCESSOR
    std::shared_ptr<dx12lib::Texture>             m_PostProcessOutput;
    std::shared_ptr<dx12lib::UnorderedAccessView> m_PostProcessOutputUAV;

    std::shared_ptr<dx12lib::RootSignature>       m_PostProcessRootSignature;
    std::shared_ptr<dx12lib::PipelineStateObject> m_PostProcessPipelineState;
#endif


    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    float lookat_dist = 10.0;
    float speed       = 2.0f;
    double theta       = 0;

    // Camera controller
    float m_Forward;
    float m_Backward;
    float m_Left;
    float m_Right;
    float m_Up;
    float m_Down;

    float m_Pitch;
    float m_Yaw;

    CameraCB m_cam;


    int  m_Width;
    int  m_Height;
    bool m_VSync;
    bool m_Fullscreen;

    // Scale the HDR render target to a fraction of the window size.
    float m_RenderScale;

    Logger m_Logger;
};