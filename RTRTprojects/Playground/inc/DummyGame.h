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

struct FrameData
{
    void UpdateCamera(DirectX::XMFLOAT3 cameraPos, DirectX::XMFLOAT3 cameraLookAt ) { 
        auto camPos = 
            DirectX::XMLoadFloat3( &cameraPos );
        auto lookAt =
            DirectX::XMLoadFloat3( &cameraLookAt );
        static auto up = 
            DirectX::XMVectorSet( 0, -1, 0, 0 );
        auto LookDirection =
            DirectX::XMVectorSubtract( lookAt, camPos );

#if 0
        auto focal_length = 
            DirectX::XMVector3Length( LookDirection );
        auto w =
            DirectX::XMVector3Normalize( LookDirection );
        auto u =
            DirectX::XMVector3Normalize( DirectX::XMVector3Cross( w, up ) );
        auto v =
            DirectX::XMVector3Cross( u, w );


        auto Horizontal = 
            DirectX::XMVectorScale( DirectX::XMVectorMultiply( focal_length, u ), cameraWinSize.x );

        auto Vertical = 
            DirectX::XMVectorScale( DirectX::XMVectorMultiply( focal_length, v ), -cameraWinSize.y );

        auto FinalMatrix =
            DirectX::XMMATRIX( Horizontal, Vertical, LookDirection, camPos );

        DirectX::XMStoreFloat3x4( &this->camPixelToWorld, FinalMatrix );
#else

        auto worldToView = DirectX::XMMatrixLookToLH( camPos, LookDirection, up );

        auto detView = DirectX::XMMatrixDeterminant( worldToView );

        auto viewToWorld = DirectX::XMMatrixInverse( &detView, worldToView );

        DirectX::XMStoreFloat3x4( &this->camPixelToWorld, viewToWorld );


#endif


    }

    FrameData( uint32_t defaultBouncesPerPath)
        : nbrBouncesPerPath( defaultBouncesPerPath )
        , atmosphere( { 0, 0, 0, 1} ) // { .529, .808, .922, 1 }
        , exponentSamplesPerPixel( 0 )
        , ambientLight(0)
    {
        UpdateCamera( DirectX::XMFLOAT3( 0, 2, 0 ), DirectX::XMFLOAT3( 10, 2, 0 ) );
    }

    bool Equal( FrameData* pOld ) 
    { 
        DirectX::XMFLOAT3X4* curr = &this->camPixelToWorld;
        DirectX::XMFLOAT3X4* old  = &pOld->camPixelToWorld;
        for ( int i = 0; i < 3; ++i )
        {
            for ( int j = 0; j < 4; ++j )
            {
                if ( curr->m[i][j] != old->m[i][j] )
                    return false;
            }
        }

        DirectX::XMFLOAT4 arr[1];
        DirectX::XMStoreFloat4( &arr[0], DirectX::XMVectorEqual( DirectX::XMLoadFloat4( &atmosphere ),
                                                                 DirectX::XMLoadFloat4( &pOld->atmosphere ) ) );
        for ( DirectX::XMFLOAT4 a : arr) {
            bool elements = a.x && a.y && a.z; 
            if ( !elements ) // If not all channels are equal
                return false;
        }

        if ( pOld->nbrBouncesPerPath != this->nbrBouncesPerPath )
            return false;

        return true;
    }

    DirectX::XMFLOAT4 atmosphere;

    DirectX::XMFLOAT3X4 camPixelToWorld;

    float ambientLight;

    uint32_t exponentSamplesPerPixel;
    uint32_t nbrBouncesPerPath;
    uint32_t cpuGeneratedSeed;
};

struct GlobalConstantData
{
    GlobalConstantData( )
    : nbrActiveLights( 0 )
    , hasSkybox( false )
    { }

    uint32_t nbrActiveLights;

    uint32_t hasSkybox;

    DirectX::XMFLOAT2 _padding;

    // Fill all elements with empty positions
    DirectX::XMFLOAT4 lightPositions[10] = { DirectX::XMFLOAT4(0,0,0,0) };
};

struct InstanceTransforms
{

    void CalculateNormalInverse() 
    {
        auto A         = DirectX::XMLoadFloat3x4( &matrix );
        auto detA      = DirectX::XMMatrixDeterminant( A );
        auto Ainv      = DirectX::XMMatrixInverse( &detA, A );
        auto AinvTrans = DirectX::XMMatrixTranspose( Ainv );
        DirectX::XMStoreFloat3x4( &normal_matrix, AinvTrans );
    }

    InstanceTransforms()
    : matrix( 1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0)
    , lodScaler( 1 )
    {
        CalculateNormalInverse();
    }

    InstanceTransforms( DirectX::XMFLOAT3 scale, DirectX::XMFLOAT3 pos = DirectX::XMFLOAT3( 0, 0, 0 ) )
    : matrix( scale.x, 0, 0, pos.x, 
        0, scale.y, 0, pos.y, 
        0, 0, scale.z, pos.z )
    , lodScaler(1)
    {
        CalculateNormalInverse();
    }

    InstanceTransforms( DirectX::XMFLOAT3X3 rotScale, DirectX::XMFLOAT3 pos = DirectX::XMFLOAT3( 0, 0, 0 ) )
    : matrix( rotScale._11, rotScale._12, rotScale._12, pos.x, 
        rotScale._21, rotScale._22, rotScale._22, pos.y,
          rotScale._31, rotScale._32, rotScale._32, pos.z )
    , lodScaler( 1 )
    {
        CalculateNormalInverse();
    }

    DirectX::XMFLOAT3X4 matrix;
    DirectX::XMFLOAT3X4 normal_matrix;

    float lodScaler;

    // 4x4x4 Bytes == 16 aligned

    bool Equal( InstanceTransforms* pOld )
    {
        DirectX::XMFLOAT3X4* thisMatrices[2] = { &this->matrix, &this->normal_matrix };
        DirectX::XMFLOAT3X4* theyMatrices[2] = { &pOld->matrix, &pOld->normal_matrix };
            
        // Checking matrices
        for (int idx = 0; idx < 2; ++idx) 
        {
            DirectX::XMFLOAT3X4* curr = thisMatrices[idx];
            DirectX::XMFLOAT3X4* old  = theyMatrices[idx];
            for ( int i = 0; i < 3; ++i )
            {
                for ( int j = 0; j < 4; ++j )
                {
                    if ( curr->m[i][j] != old->m[i][j] )
                        return false;
                }
            }
        }

        if ( this->lodScaler != pOld->lodScaler )
            return false;

        return true;
    }
};

struct DenoiserFilterData
{
    DirectX::XMFLOAT3X4 oldCameraWorldToClip;
    DirectX::XMFLOAT3X4 newCameraWorldToClip;

    DirectX::XMFLOAT2 m_CameraWinSize;
    DirectX::XMFLOAT2 m_WindowResolution;

    float m_alpha_new         = 0.1;
    float m_ReprojectErrorLimit = 1;

    float sigmaDepth = 1;
    float sigmaNormal = 128;
    float sigmaLuminance = 4;

    int stepSize = 1;

    void BuildOldAndNewDenoiser( FrameData* pOld, FrameData* pNew, 
        DirectX::XMFLOAT2 cameraWinSize, int width, int height )
    {
        if (pOld && pNew) {
            auto oldCam = DirectX::XMLoadFloat3x4( &pOld->camPixelToWorld );
            auto newCam = DirectX::XMLoadFloat3x4( &pNew->camPixelToWorld );

            auto camPosFloat = DirectX::XMFLOAT4( 0, 0, 0, 1 );
            auto centerFloat = DirectX::XMFLOAT4( 0, 0, 1, 1 );
            auto upFloat     = DirectX::XMFLOAT4( 0, -1, 0, 0 );

            auto centre = DirectX::XMLoadFloat4( &centerFloat );
            auto camPos = DirectX::XMLoadFloat4( &camPosFloat );
            auto up     = DirectX::XMLoadFloat4( &upFloat );

            auto oldPos = DirectX::XMVector4Transform( camPos, oldCam );
            auto newPos = DirectX::XMVector4Transform( camPos, newCam );

            auto oldLookAt = DirectX::XMVector4Transform( centre, oldCam );
            auto newLookAt = DirectX::XMVector4Transform( centre, newCam );

            auto oldCamLength = DirectX::XMVectorSubtract( oldLookAt, oldPos );
            auto newCamLength = DirectX::XMVectorSubtract( newLookAt, newPos );

            float oldFocalLength, newFocalLength;
            DirectX::XMStoreFloat( &oldFocalLength, DirectX::XMVector3Length( oldCamLength ) );
            DirectX::XMStoreFloat( &newFocalLength, DirectX::XMVector3Length( newCamLength ) );

            auto oldDir = DirectX::XMVector3Normalize( oldCamLength );
            auto newDir = DirectX::XMVector3Normalize( newCamLength );

            auto oldView = DirectX::XMMatrixLookToLH( oldPos, oldDir, up );
            auto newView = DirectX::XMMatrixLookToLH( newPos, newDir, up );

            DirectX::XMStoreFloat3x4( &oldCameraWorldToClip, oldView );
            DirectX::XMStoreFloat3x4( &newCameraWorldToClip, newView );
        }
        
        m_CameraWinSize = cameraWinSize;
        m_WindowResolution = DirectX::XMFLOAT2( static_cast<float>( width ), static_cast<float>( height ) );
    }
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

    
    alignas( 16 ) DenoiserFilterData m_FilterData;
    alignas( 16 ) FrameData m_frameData;
    alignas( 16 ) GlobalConstantData m_Globals;


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

    std::shared_ptr<dx12lib::MappableBuffer> m_FilterCB;
    std::shared_ptr<dx12lib::MappableBuffer> m_FrameDataCB;
    std::shared_ptr<dx12lib::MappableBuffer> m_GlobalCB;
    std::shared_ptr<dx12lib::MappableBuffer> m_InstanceTransformResources;

    dx12lib::AccelerationStructure m_TlasBuffers = {};

    D3D12_DISPATCH_RAYS_DESC m_RaytraceDesc = {};

    // Tut 6
    const uint32_t m_nbrRayRenderTargets = 4;
    dx12lib::RenderTarget m_RayRenderTarget;

    const uint32_t        m_nbrHistoryRenderTargets = 5;
    dx12lib::RenderTarget m_HistoryRenderTarget;

    const uint32_t        m_nbrFilterRenderTargets = 5;
    dx12lib::RenderTarget m_FilterRenderTarget;
    #define FILTER_SLOT_COLOUR_SOURCE 0
    #define FILTER_SLOT_MOMENT_SOURCE 1
    #define FILTER_SLOT_SDR_TARGET    2
    #define FILTER_SLOT_COLOUR_TARGET 3
    #define FILTER_SLOT_MOMENT_TARGET 4

    dx12lib::AttachmentPoint m_FilterMomentSource = dx12lib::AttachmentPoint::Color1;
    dx12lib::AttachmentPoint m_FilterOutputSDR = dx12lib::AttachmentPoint::Color2;
    dx12lib::AttachmentPoint m_FilterColourTarget     = dx12lib::AttachmentPoint::Color3;
    dx12lib::AttachmentPoint m_FilterMomentTarget  = dx12lib::AttachmentPoint::Color4;

    dx12lib::AttachmentPoint m_ColourSlot  = dx12lib::AttachmentPoint::Color0;
    dx12lib::AttachmentPoint m_NormalsSlot = dx12lib::AttachmentPoint::Color1;
    dx12lib::AttachmentPoint m_PosDepth    = dx12lib::AttachmentPoint::Color2;
    dx12lib::AttachmentPoint m_ObjectMask  = dx12lib::AttachmentPoint::Color3;
    dx12lib::AttachmentPoint m_MomentHistory  = dx12lib::AttachmentPoint::Color4;

    std::shared_ptr<dx12lib::ShaderTableResourceView>   m_RayShaderHeap;
    std::vector<InstanceTransforms>                   m_InstanceTransforms;

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

    /*
        Update dispatch ray description
    */
    void UpdateDispatchRaysDesc();


    // denoiser compute shader
    std::shared_ptr<dx12lib::RootSignature> m_DenoiserRootSig;
    std::shared_ptr<dx12lib::PipelineStateObject> m_SVGF_AtrousPipelineState;
    std::shared_ptr<dx12lib::PipelineStateObject> m_SVGF_ReprojectionPipelineState; 
    std::shared_ptr<dx12lib::PipelineStateObject> m_SVGF_MomentsPipelineState; 

#endif

    void UpdateCamera( float moveVertically, float moveUp, float moveForward );

    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    FLOAT backgroundColour[3];
    int lodScaleExp = 1;

    // General
    std::shared_ptr<dx12lib::Device>    m_Device;
    std::shared_ptr<dx12lib::SwapChain> m_SwapChain;
    std::shared_ptr<dx12lib::GUI>       m_GUI;

    std::shared_ptr<Window> m_Window;

    // Some geometry to render.
    std::shared_ptr<dx12lib::Scene> m_RaySceneMesh;
    
    std::shared_ptr<dx12lib::Texture> m_DummyTexture;

    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    float lookat_dist = 10.0;

    DirectX::XMFLOAT3 m_CamPos;
    DirectX::XMFLOAT2 m_CamWindow;

    float cam_speed       = 30.0f;

    float scene_rot_speed   = 0.0f;
    float scene_rot_offset  = 0;
    float scene_scale       = 1;

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

    bool m_Print;

    // Scale the HDR render target to a fraction of the window size.
    float m_RenderScale;

    bool m_IsLoading;

    Logger m_Logger;
};