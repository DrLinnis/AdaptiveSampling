

struct VertexPositionNormalTexture
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct VertexShaderOutput
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

VertexShaderOutput main( VertexPositionNormalTexture IN )
{
    VertexShaderOutput OUT;

    OUT.Position = float4( IN.Position, 1.0f );
    OUT.TexCoord = IN.TexCoord;

    return OUT;
}