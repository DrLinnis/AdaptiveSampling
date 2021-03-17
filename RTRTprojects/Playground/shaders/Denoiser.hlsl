
#define BLOCK_SIZE 8

struct ComputeShaderInput
{
    uint3 GroupID : SV_GroupID;                    // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID : SV_GroupThreadID;        // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex : SV_GroupIndex;              // Flattened local index of the thread within a thread group.
};

RWTexture2D<float4> rayBuffer[] : register( u0, space0);
RWTexture2D<float4> historyBuffer[] : register( u0, space1 );
RWTexture2D<float4> filterBuffer[] : register(u0, space2 );

#define RAW_SAMPLES 0

[numthreads( BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( ComputeShaderInput IN ) 
{ 
    float4 result = 0;
    
#if RAW_SAMPLES
    result = rayBuffer[0][IN.DispatchThreadID.xy];
#else
    int layers = 0;
    float4 newRadiance = 0;
    
    // average new radiance
    for (int dx = -layers; dx <= layers; ++dx)
    {
        for (int dy = -layers; dy <= layers; ++dy)
        {
            newRadiance += rayBuffer[0][IN.DispatchThreadID.xy + uint2(dx, dy)];
        }
    }
    
    newRadiance /= (2 * layers + 1) * (2 * layers + 1);
    
    float4 integratedColour = historyBuffer[0][IN.DispatchThreadID.xy];
    
    uint accumulatedFrames = (uint) newRadiance.w;
    
    float4 avrRadiance = 0;
    if (accumulatedFrames == 0)
        avrRadiance = newRadiance;
    else
        avrRadiance = lerp(integratedColour, newRadiance, 1.f / (accumulatedFrames + 1.0f));
    
    historyBuffer[0][IN.DispatchThreadID.xy] = avrRadiance;
    
    result = avrRadiance;
    
#endif
    
    filterBuffer[0][IN.DispatchThreadID.xy] = clamp(result, 0, 1);

}