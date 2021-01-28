#pragma once

#include <GameFramework/GameFramework.h>

#include <vector>

#include <dx12lib/RenderTarget.h>

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
    // Tut 3
    std::shared_ptr<dx12lib::AccelerationStructure> m_topLevelBuffers;

    std::shared_ptr<dx12lib::AccelerationStructure> m_bottomLevelBuffers;

    uint64_t mTlasSize = 0;
    
    // Tut 4
    std::shared_ptr<dx12lib::RootSignature>         m_RayGenRootSig;
    std::shared_ptr<dx12lib::RootSignature>         m_HitMissRootSig;
    std::shared_ptr<dx12lib::RootSignature>         m_DummyGlobalRootSig;

    std::shared_ptr<dx12lib::RT_PipelineStateObject> m_RayPipelineState; 
    
    // Tut 5
    size_t                                          m_ShaderTableEntrySize = 0;
    std::shared_ptr<dx12lib::MappableBuffer>        m_ShaderTable;

    // Tut 6
    std::shared_ptr<dx12lib::Texture>               m_RayOutputResource;

    std::shared_ptr<dx12lib::ShaderTableResourceView>       m_RayShaderHeap;
    std::shared_ptr<dx12lib::UnorderedAccessView>   m_RayOutputUAV;
    std::shared_ptr<dx12lib::ShaderResourceView>    m_TlasSRV;



    // new helper functions
    void CreatePostProcessor( const D3D12_STATIC_SAMPLER_DESC* sampler, DXGI_FORMAT backBufferFormat );
    void CreateDisplayPipeline( const D3D12_STATIC_SAMPLER_DESC* sampler, DXGI_FORMAT backBufferFormat);
    void CreateRayTracingPipeline(  );
    void CreateShaderTable();
    void CreateShaderResource( DXGI_FORMAT backBufferFormat );



    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    std::shared_ptr<dx12lib::Device>    m_Device;
    std::shared_ptr<dx12lib::SwapChain> m_SwapChain;
    std::shared_ptr<dx12lib::GUI>       m_GUI;

    std::shared_ptr<Window> m_Window;

    // Some geometry to render.
    std::shared_ptr<dx12lib::Scene> m_Plane;

    std::shared_ptr<dx12lib::Scene> m_RayMesh;
    
    std::shared_ptr<dx12lib::Texture>             m_PostProcessOutput;
    std::shared_ptr<dx12lib::UnorderedAccessView> m_PostProcessOutputUAV;

    std::shared_ptr<dx12lib::Texture>             m_RenderShaderResource;
    std::shared_ptr<dx12lib::ShaderResourceView> m_RenderShaderView;

    // Render target
    dx12lib::RenderTarget m_RenderTarget;

    std::shared_ptr<dx12lib::RootSignature> m_DisplayRootSignature;
    std::shared_ptr<dx12lib::PipelineStateObject> m_DisplayPipelineState;

    std::shared_ptr<dx12lib::RootSignature>       m_PostProcessRootSignature;
    std::shared_ptr<dx12lib::PipelineStateObject> m_PostProcessPipelineState;

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




    int  m_Width;
    int  m_Height;
    bool m_VSync;
    bool m_Fullscreen;

    // Scale the HDR render target to a fraction of the window size.
    float m_RenderScale;

    Logger m_Logger;
};