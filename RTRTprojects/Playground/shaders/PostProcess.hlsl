
#define BLOCK_WIDTH 16
#define BLOCK_HEIGHT 9

struct ComputeShaderInput
{
    uint3 GroupID : SV_GroupID;                    // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID : SV_GroupThreadID;        // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex : SV_GroupIndex;              // Flattened local index of the thread within a thread group.
};

Texture2D<float4> input : register( t0 );
RWTexture2D<float4> output : register( u0 );

float4 mix(float4 a, float4 b, float alpha)
{
    return a * alpha + b * (1 - alpha);
}

[numthreads(BLOCK_WIDTH, BLOCK_HEIGHT, 1)]
void main( ComputeShaderInput IN ) 
{ 
    float du = 1 / ((float) 1600 - 1);
    float dv = 1 / ((float) 900 - 1);
    
    int y = IN.DispatchThreadID.y;
    float v = y * dv;
    
    int x = IN.DispatchThreadID.x;
    float u = x * du;
    
#if 0
    output[IN.DispatchThreadID.xy] = float4(u, v, 0, 1);
#else
    output[IN.DispatchThreadID.xy] = float4(1, 0, 1, 1);
#endif
}