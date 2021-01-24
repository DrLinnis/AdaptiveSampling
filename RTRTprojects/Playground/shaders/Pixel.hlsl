
struct PixelShaderOutput
{
    float2 TexCoord : TEXCOORD;
};

const SamplerState MeshTextureSampler
{
    Filter = ANISOTROPIC;
    AddressU = Wrap;
    AddressV = Wrap;
};

Texture2D Texture : register( t0 );

static const float2 texture_step = { 1.0 / 1600.0, 1.0 / 900.0 };

float4 main( PixelShaderOutput IN ) : SV_TARGET
{
#if 1 
    return Texture.Sample(MeshTextureSampler, IN.TexCoord);// + float4(1.0, 0.0, 0.0, 0.0);
#else
    float4 result;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            result += Texture.Sample(MeshTextureSampler, IN.TexCoord + float2(x, y) * texture_step);
        }
    }
    return result / 9;
#endif

}