#pragma once

/*
 *  Copyright(c) 2020 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

/**
 *  @file Material.h
 *  @date October 30, 2020
 *  @author Jeremiah van Oosten
 *
 *  @brief Material class for scene loading.
 */

#include <DirectXMath.h>  // For vector types.

#include <map>     // For std::map
#include <memory>  // For std::unique_ptr and std::shared_ptr

namespace dx12lib
{

class Texture;


#define DIFFUSE         0
#define SPECULAR        1
#define TRANSMISSIVE    2

// clang-format off
struct alignas( 16 ) MaterialProperties
{
    // The Material properties must be aligned to a 16-byte boundary.
    // To guarantee alignment, the MaterialProperties structure will be allocated in aligned memory.
    MaterialProperties( 
        const DirectX::XMFLOAT3 diffuse  = { 1, 1, 1 },
        const DirectX::XMFLOAT4 specular = { 1, 1, 1, 1 }, 
        const float specularPower = 128.0f,
        const DirectX::XMFLOAT4 ambient  = { 0, 0, 0, 1 },
        const DirectX::XMFLOAT4 emissive = { 0, 0, 0, 1 },
        const DirectX::XMFLOAT4 reflectance = { 0, 0, 0, 0 }, const float opacity = 1.0f,
        const float indexOfRefraction = 0.0f, const float bumpIntensity = 1.0f,
        const float alphaThreshold = 0.1f, uint32_t type = DIFFUSE, float roughness = 1.0f
    )
    : Diffuse( diffuse )
    , Specular( specular )
    , Emissive( emissive )
    , Ambient( ambient )
    , Reflectance( reflectance )
    , Opacity( opacity )
    , SpecularPower( specularPower )
    , IndexOfRefraction( indexOfRefraction )
    , BumpIntensity( bumpIntensity )
    , Type(type)
    , Roughness( roughness )
    , HasAmbientTexture( false )
    , HasEmissiveTexture( false )
    , HasDiffuseTexture( false )
    , HasSpecularTexture( false )
    , HasSpecularPowerTexture( false )
    , HasNormalTexture( false )
    , HasBumpTexture( false )
    , HasOpacityTexture( false )
    {}

    float Roughness;
    uint32_t Type;

    DirectX::XMFLOAT3 Diffuse;
    //------------------------------------ ( 16 bytes )
    DirectX::XMFLOAT4 Specular;
    //------------------------------------ ( 16 bytes )
    DirectX::XMFLOAT4 Emissive;
    //------------------------------------ ( 16 bytes )
    DirectX::XMFLOAT4 Ambient;
    //------------------------------------ ( 16 bytes )
    DirectX::XMFLOAT4 Reflectance;
    //------------------------------------ ( 16 bytes )
    float Opacity;                       // If Opacity < 1, then the material is DIALECTIC.
    float SpecularPower;
    float IndexOfRefraction;             // For DIALECTIC materials, IOR > 0.
    float BumpIntensity;                 // When using bump textures (height maps) we need
                                         // to scale the height values so the normals are visible.
    //------------------------------------ ( 16 bytes )
    uint32_t HasAmbientTexture;
    uint32_t HasEmissiveTexture;
    uint32_t HasDiffuseTexture;
    uint32_t HasSpecularTexture;
    //------------------------------------ ( 16 bytes )
    uint32_t HasSpecularPowerTexture;
    uint32_t HasNormalTexture;
    uint32_t HasBumpTexture;
    uint32_t HasOpacityTexture;
    //------------------------------------ ( 16 bytes )
    // Total:                              ( 16 * 8 = 128 bytes )
};


struct alignas( 16 ) RayMaterialProp
{
    RayMaterialProp(
        const DirectX::XMFLOAT3 diffuse = { 1, 1, 1 },
        const int type = DIFFUSE,
        const DirectX::XMFLOAT3 emittance = {0, 0, 0},
        const float reflectivity = 0.0f,
        const float indexOfRefraction = 1.0f
    )
        : Diffuse( diffuse )
        , Type( type )

        , Reflectivity( reflectivity )
        , Emittance( emittance )

        , IndexOfRefraction( indexOfRefraction )
        , Roughness( 1.0f )
        , _padding( 0, 0 )

        , DiffuseTextureIdx(-1)
        , NormalTextureIdx(-1)
        , SpecularTextureIdx(-1)
        , MaskTextureIdx(-1)

    { }

    DirectX::XMFLOAT3 Diffuse;  
    unsigned int Type;
    // ------------------------------------ ( 16 bytes )

    int DiffuseTextureIdx;
    int NormalTextureIdx;
    int SpecularTextureIdx;
    int MaskTextureIdx;
    // ------------------------------------ ( 16 bytes )
    
    float Reflectivity;
    DirectX::XMFLOAT3 Emittance;
    // ------------------------------------ ( 16 bytes )

    float IndexOfRefraction;
    float Roughness;
    DirectX::XMFLOAT2 _padding;
    // ------------------------------------ ( 16 bytes )

    // Total:                              ( 16 * 4 = 48 bytes )
};

// clang-format on

class Material
{
public:
    // These are the texture slots that can be bound to the material.
    enum class TextureType
    {
        Ambient,
        Emissive,
        Diffuse,
        Specular,
        SpecularPower,
        Normal,
        Bump,
        Opacity,
        NumTypes,
    };

    Material( const MaterialProperties& materialProperties = MaterialProperties() );
    Material( const Material& copy );

    ~Material() = default;

    const DirectX::XMFLOAT4& GetAmbientColor() const;
    void                     SetAmbientColor( const DirectX::XMFLOAT4& ambient );

    const DirectX::XMFLOAT3& GetDiffuseColor() const;
    void                     SetDiffuseColor( const DirectX::XMFLOAT3& diffuse );

    uint32_t GetMaterialType() const;
    void     SetMaterialType( uint32_t type );

    const DirectX::XMFLOAT4& GetEmissiveColor() const;
    void                     SetEmissiveColor( const DirectX::XMFLOAT4& emissive );

    const DirectX::XMFLOAT4& GetSpecularColor() const;
    void                     SetSpecularColor( const DirectX::XMFLOAT4& specular );

    float GetRoughness() const;
    void  SetRoughness( float roughness );

    float GetSpecularPower() const;
    void  SetSpecularPower( float specularPower );

    const DirectX::XMFLOAT4& GetReflectance() const;
    void                     SetReflectance( const DirectX::XMFLOAT4& reflectance );

    const float GetOpacity() const;
    void        SetOpacity( float opacity );

    float GetIndexOfRefraction() const;
    void  SetIndexOfRefraction( float indexOfRefraction );

    // When using bump maps, we can adjust the "intensity" of the normals generated
    // from the bump maps. We can even inverse the normals by using a negative intensity.
    // Default bump intensity is 1.0 and a value of 0 will remove the bump effect altogether.
    float GetBumpIntensity() const;
    void  SetBumpIntensity( float bumpIntensity );

    std::shared_ptr<Texture> GetTexture( TextureType ID ) const;
    void                     SetTexture( TextureType type, std::shared_ptr<Texture> texture );

    // This material defines a DIALECTIC material
    // if the opacity value is < 1, or there is an opacity map, or the diffuse texture has an alpha channel.
    bool IsDIALECTIC() const;

    const MaterialProperties& GetMaterialProperties() const;
    void                      SetMaterialProperties( const MaterialProperties& materialProperties );

    // Define some interesting materials.
    static const MaterialProperties Zero;
    static const MaterialProperties Red;
    static const MaterialProperties Green;
    static const MaterialProperties Blue;
    static const MaterialProperties Cyan;
    static const MaterialProperties Magenta;
    static const MaterialProperties Yellow;
    static const MaterialProperties White;
    static const MaterialProperties WhiteDiffuse;
    static const MaterialProperties Black;
    static const MaterialProperties Emerald;
    static const MaterialProperties Jade;
    static const MaterialProperties Obsidian;
    static const MaterialProperties Pearl;
    static const MaterialProperties Ruby;
    static const MaterialProperties Turquoise;
    static const MaterialProperties Brass;
    static const MaterialProperties Bronze;
    static const MaterialProperties Chrome;
    static const MaterialProperties Copper;
    static const MaterialProperties Gold;
    static const MaterialProperties Silver;
    static const MaterialProperties BlackPlastic;
    static const MaterialProperties CyanPlastic;
    static const MaterialProperties GreenPlastic;
    static const MaterialProperties RedPlastic;
    static const MaterialProperties WhitePlastic;
    static const MaterialProperties YellowPlastic;
    static const MaterialProperties BlackRubber;
    static const MaterialProperties CyanRubber;
    static const MaterialProperties GreenRubber;
    static const MaterialProperties RedRubber;
    static const MaterialProperties WhiteRubber;
    static const MaterialProperties YellowRubber;

protected:
private:
    using TextureMap = std::map<TextureType, std::shared_ptr<Texture>>;
    // A unique pointer with a custom allocator/deallocator to ensure alignment.
    using MaterialPropertiesPtr = std::unique_ptr<MaterialProperties, void ( * )( MaterialProperties* )>;

    MaterialPropertiesPtr m_MaterialProperties;
    TextureMap            m_Textures;
};
}  // namespace dx12lib