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

#define UPDATE_TRANSFORMS 1


struct CameraCB
{
    CameraCB( DirectX::XMFLOAT3 pos, DirectX::XMFLOAT3 lookat, DirectX::XMFLOAT2 winSize )
    : pos( pos.x, pos.y, pos.z, 0 )
    , lookAt( lookat.x, lookat.y, lookat.z, 0 )
    , lookUp( 0, 1, 0, 0 )
    , windowSize( winSize.x, winSize.y)
    {}

    DirectX::XMFLOAT4 pos;
    DirectX::XMFLOAT4 lookAt;
    DirectX::XMFLOAT4 lookUp;
    DirectX::XMFLOAT2 windowSize;

    DirectX::XMFLOAT2 _padding;
};

struct InstanceTransforms
{

    void CalculateNormalInverse() 
    {
        auto A         = DirectX::XMLoadFloat4x4( &RS );
        auto detA      = DirectX::XMMatrixDeterminant( A );
        auto Ainv      = DirectX::XMMatrixInverse( &detA, A );
        auto AinvTrans = DirectX::XMMatrixTranspose( Ainv );
        DirectX::XMStoreFloat4x4( &normal_RS, AinvTrans );
    }

    InstanceTransforms()
    : RS(   1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1)
    , translate(0,0,0,0)
    {
        CalculateNormalInverse();
    }

    #if 1
    InstanceTransforms( DirectX::XMFLOAT3 scale, DirectX::XMFLOAT3 pos = DirectX::XMFLOAT3( 0, 0, 0 ) )
    : RS( scale.x, 0, 0, 0, 
        0, scale.y, 0, 0, 
        0, 0, scale.z, 0,
            0, 0,       0, 1)
    , translate(pos.x, pos.y, pos.z, 0)
    {
        CalculateNormalInverse();
    }
    #else
    InstanceTransforms( DirectX::XMFLOAT3 scale, DirectX::XMFLOAT3 pos = DirectX::XMFLOAT3( 0, 0, 0 ) )
    : RS( scale.x, 0, 0, 0, 
        0, 0, -scale.y, 0, 
        0, scale.z, 0, 0,
            0, 0,       0, 1)
    , translate(pos.x, pos.y, pos.z, 0)
    {
        CalculateNormalInverse();
    }
    #endif

    InstanceTransforms( DirectX::XMFLOAT3X3 rotScale, DirectX::XMFLOAT3 pos = DirectX::XMFLOAT3( 0, 0, 0 ) )
    : RS( rotScale._11, rotScale._12, rotScale._12, 0, 
        rotScale._21, rotScale._22, rotScale._22, 0,
        rotScale._31, rotScale._32, rotScale._32, 0,
        0, 0, 0, 1 )
    , translate( pos.x, pos.y, pos.z, 0 )
    {
        CalculateNormalInverse();
    }

    DirectX::XMFLOAT4X4 RS;
    DirectX::XMFLOAT4X4 normal_RS;
    DirectX::XMFLOAT4   translate;

    // 4x4x4 Bytes == 16 aligned
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
    
    const unsigned int m_Bounces = 5;

    std::shared_ptr<dx12lib::AccelerationBuffer> m_BLAS;

    // mesh count and instance count
    std::vector<size_t>  m_GeometryCountPerInstance;
    std::vector<size_t>  m_DiffuseTexCountPerInstance;
    std::vector<size_t>  m_NormalTexCountPerInstance;
    std::vector<size_t>  m_SpecularTexCountPerInstance;
    std::vector<size_t>  m_MaskTexCountPerInstance;

    size_t   m_Instances;

    size_t   m_TotalGeometryCount;
    size_t   m_TotalDiffuseTexCount;
    size_t   m_TotalNormalTexCount;
    size_t   m_TotalSpecularTexCount;
    size_t   m_TotalMaskTexCount;

    size_t m_ShaderTableEntrySize;
    size_t m_ShadersEntriesPerGeometry;
    
    uint64_t mTlasSize = 0;
    
    std::shared_ptr<dx12lib::RootSignature>             m_RayGenRootSig;
    std::shared_ptr<dx12lib::RootSignature>             m_StdHitRootSig;
    std::shared_ptr<dx12lib::RootSignature>             m_EmptyLocalRootSig;
    std::shared_ptr<dx12lib::RootSignature>             m_GlobalRootSig;

    std::shared_ptr<dx12lib::RT_PipelineStateObject>    m_RayPipelineState; 
    
    std::shared_ptr<dx12lib::MappableBuffer>            m_InstanceDescBuffer;

    std::shared_ptr<dx12lib::MappableBuffer>            m_RaygenShaderTable;
    std::shared_ptr<dx12lib::MappableBuffer>            m_MissShaderTable;
    std::shared_ptr<dx12lib::MappableBuffer>            m_HitShaderTable;

    std::shared_ptr<dx12lib::MappableBuffer> m_RayCamCB;
    std::shared_ptr<dx12lib::MappableBuffer> m_InstanceTransformResources;

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
    const uint32_t        m_nbrRayRenderTargets = 7;
    dx12lib::RenderTarget m_RayRenderTarget;

    std::shared_ptr<dx12lib::ShaderTableResourceView>   m_RayShaderHeap;
    std::vector<InstanceTransforms>                   m_InstanceTransforms;

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

    /*
        Update values from creation
    */
    void UpdateConstantBuffer();

#endif

    void UpdateCamera( float moveVertically, float moveUp, float moveForward );

    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    // General
    std::shared_ptr<dx12lib::Device>    m_Device;
    std::shared_ptr<dx12lib::SwapChain> m_SwapChain;
    std::shared_ptr<dx12lib::GUI>       m_GUI;

    std::shared_ptr<Window> m_Window;

    // Some geometry to render.
    std::shared_ptr<dx12lib::Scene> m_RaySceneMesh;
    
    std::shared_ptr<dx12lib::Texture> m_DummyTexture;

    // Render target
    dx12lib::RenderTarget m_RenderTarget;

    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    float lookat_dist = 10.0;
    float speed       = 300.0f;
    float  thetaSpeed  = 0.0f;
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