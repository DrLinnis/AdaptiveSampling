
#define BLOCK_SIZE 8

struct ComputeShaderInput
{
    uint3 GroupID : SV_GroupID;                    // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID : SV_GroupThreadID;        // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex : SV_GroupIndex;              // Flattened local index of the thread within a thread group.
};

struct DenoiserFilterData
{
    row_major matrix<float, 3, 4> oldCameraWorldToClip;
    row_major matrix<float, 3, 4> newCameraWorldToClip;
};

ConstantBuffer<DenoiserFilterData> filterData : register(b0);

/*
    colour,
    normal,
    posDepth,
    object,
    spec
*/
RWTexture2D<float4> rayBuffer[] : register( u0, space0);

/*
    integratedColour,
    prevNormal,
    prevPosDepth,
    prevObject,
*/
RWTexture2D<float4> historyBuffer[] : register( u0, space1 );

/*
    outputImage
*/
RWTexture2D<float4> filterBuffer[] : register(u0, space2 );

#define SLOT_COLOUR 0
#define SLOT_NORMALS 1
#define SLOT_POS_DEPTH 2
#define SLOT_OBJECT_ID 3

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
            newRadiance += rayBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy + uint2(dx, dy)];
        }
    }
    
    newRadiance /= (2 * layers + 1) * (2 * layers + 1);
    
    float4 integratedColour = historyBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy];
    
    uint accumulatedFrames = (uint) newRadiance.w;
    
    float4 avrRadiance = 0;
    if (accumulatedFrames == 0)
        avrRadiance = newRadiance;
    else
        avrRadiance = lerp(integratedColour, newRadiance, 1.f / (accumulatedFrames + 1.0f));
    
    historyBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy] = avrRadiance;
    
    result = avrRadiance;
    
    float4 worldPos = float4(rayBuffer[SLOT_POS_DEPTH][IN.DispatchThreadID.xy].xyz, 1);
    float3 vel = mul(filterData.newCameraWorldToClip, worldPos) - mul(filterData.oldCameraWorldToClip, worldPos);
    //result = float4(vel, 0);
    
    
    
#if 0
    float3 diff = historyBuffer[1][IN.DispatchThreadID.xy].xyz - rayBuffer[1][IN.DispatchThreadID.xy].xyz;
    if (dot(diff, diff) != 0)
        result = float4(1, 0, 0, 0);
#endif
    
    
#endif
    
    filterBuffer[0][IN.DispatchThreadID.xy] = clamp(result, 0, 1);

}