#pragma once

#include <GameFramework/GameFramework.h>

#include <vector>

#include <dx12lib/RenderTarget.h>

#include <DirectXMath.h>

#include <string>

namespace dx12lib
{
class CommandList;
class Device;
class GUI;
class Mesh;
class PipelineStateObject;
class Scene;
class RootSignature;
class ShaderResourceView;
class UnorderedAccessView;
class SwapChain;
class Texture;
} 

class Window;  // From GameFramework.


namespace Ray
{
const int BLOCK_WIDTH  = 16;
const int BLOCK_HEIGHT = 9;

struct Sphere
{
    DirectX::XMFLOAT3 Position;
    float             Radius;
    uint32_t          MatIdx;

    float padding1;
    float padding2;
    float padding3;
    // 2 * 16 bytes
    Sphere() {};
    Sphere(float x, float y, float z, float radius, int matIdx) 
        : Position( x, y, z ) , Radius( radius ) , MatIdx( matIdx ) {}
    Sphere( float pos[], float radius, int matIdx )
        : Position( pos[0], pos[1], pos[2] ) , Radius( radius ) , MatIdx( matIdx ) {}
};


#define MODE_MATERIAL_COLOUR    0
#define MODE_MATERIAL_METAL     1
#define MODE_MATERIAL_DIALECTIC 2
//#define MODE_MATERIAL_CHECKER   3
//#define MODE_MATERIAL_TEXTURE   4

struct Material
{
    uint32_t Mode;
    DirectX::XMFLOAT3 Colour;

    DirectX::XMFLOAT2 uvBottomLeft;
    DirectX::XMFLOAT2 uvTopRight;
    uint32_t          TexIdx;

    float Scale;
    float Fuzz;
    float RefractionIndex;
    // Total 3*16 = 48 bytes

    // default
    Material() {}
    // Colour and metal
    Material( uint32_t mode, std::array<float,3> rgb, float fuzz = 1.0f) 
        : Mode(mode), Colour(rgb[0], rgb[1], rgb[2]), Fuzz(fuzz) { }
    // Dialectic
    Material( uint32_t mode, float refractIdx = 1.0f ) 
        : Mode(mode), RefractionIndex(refractIdx) { }
    // Texture
    Material( uint32_t mode, uint32_t texIdx, std::array<float, 2> uv1, std::array<float, 2> uv2 ) 
        : Mode(mode), TexIdx(texIdx), uvBottomLeft(uv1[0], uv1[1]), uvTopRight(uv2[0], uv2[1]) { }
    // RGB and white checkers with scale 
    //Material( uint32_t mode, std::array<float, 3> non_white_rgb, float scale = 1.0f )
    //    : Mode( mode ) , Colour( non_white_rgb[0], non_white_rgb[1], non_white_rgb[2] ) , Scale(scale) {}
};

struct RayCB
{
    DirectX::XMFLOAT4 VoidColour;

    DirectX::XMUINT2 WindowResolution;
    uint32_t         NbrSpheres;
    uint32_t         NbrMaterials;

    float TimeSeed;

    DirectX::XMFLOAT3 padding;
};

struct CamCB
{
    DirectX::XMFLOAT4 CameraPos;
    DirectX::XMFLOAT4 CameraLookAt;
    DirectX::XMFLOAT4 CameraUp;

    DirectX::XMFLOAT2 CameraWindow;
};

enum
{
    RayContext,     // b0
    CameraContext,  // b1
    SphereList,     // t0
    MaterialList,   // t1
    Output,         // u0
    NumRootParameters
};
}  // namespace RayRootParameters

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

    void FillRandomScene();

    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    std::shared_ptr<dx12lib::Device>    m_Device;
    std::shared_ptr<dx12lib::SwapChain> m_SwapChain;
    std::shared_ptr<dx12lib::GUI>       m_GUI;

    std::shared_ptr<Window> m_Window;

    // Some geometry to render.
    std::shared_ptr<dx12lib::Scene> m_Plane;
    
    std::shared_ptr<dx12lib::Texture>             m_StagingResource;
    std::shared_ptr<dx12lib::UnorderedAccessView> m_StagingUnorderedAccessView;

    std::shared_ptr<dx12lib::Texture>             m_RenderShaderResource;
    std::shared_ptr<dx12lib::ShaderResourceView> m_RenderShaderView;

    // Render target
    dx12lib::RenderTarget m_RenderTarget;

    std::shared_ptr<dx12lib::RootSignature> m_DisplayRootSignature;
    std::shared_ptr<dx12lib::PipelineStateObject> m_DisplayPipelineState;

    std::shared_ptr<dx12lib::RootSignature>       m_RayRootSignature;
    std::shared_ptr<dx12lib::PipelineStateObject> m_RayPipelineState;

    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    alignas( 16 ) Ray::RayCB rayCB;
    alignas( 16 ) Ray::CamCB camCB;
    std::vector<Ray::Sphere> sphereList;
    std::vector<Ray::Material> materialList;

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