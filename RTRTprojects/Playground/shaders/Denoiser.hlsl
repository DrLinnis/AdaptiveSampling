
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
    
    float2 cameraWindowSize;
    float2 windowResolution;
};

ConstantBuffer<DenoiserFilterData> filterData : register(b0);

/*
    colour,
    normal,
    posDepth,
    objectMask,
*/
RWTexture2D<float4> rayBuffer[] : register( u0, space0);

/*
    oldIntegratedColour,
    prevNormal,
    prevPosDepth,
    prevObject,
*/
RWTexture2D<float4> historyBuffer[] : register( u0, space1 );

/*
    newIntegeratedColour

    outputImage
*/
RWTexture2D<float4> filterBuffer[] : register(u0, space2 );

#define FILTER_SLOT_SDR 1

#define SLOT_COLOUR 0
#define SLOT_NORMALS 1
#define SLOT_POS_DEPTH 2
#define SLOT_OBJECT_ID 3
#define SLOT_MASK 4

#define RAW_SAMPLES 0

#define REPROJ_DELTA 0.01

float4 BilienarFilter(RWTexture2D<float4> tex, float2 floatingPixelPos)
{
    float2 AB = frac(floatingPixelPos - 0.5);
    uint2 uv00 = (uint2) clamp(floor(floatingPixelPos - 0.5), 0, filterData.windowResolution);
    uint2 uv10 = (uint2) clamp(uv00 + uint2(1, 0), 0, filterData.windowResolution);
    uint2 uv11 = (uint2) clamp(uv00 + uint2(1, 1), 0, filterData.windowResolution);
    uint2 uv01 = (uint2) clamp(uv00 + uint2(0, 1), 0, filterData.windowResolution);
    
    float4 r1 = AB.x * tex[uv00] + (1 - AB.x) * tex[uv10];
    float4 r2 = AB.x * tex[uv01] + (1 - AB.x) * tex[uv11];
    return AB.y * r1 + (1 - AB.y) * r2;
}

// Shader toy inspired temporal reprojection := https://www.shadertoy.com/view/ldtGWl
[numthreads( BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( ComputeShaderInput IN ) 
{ 
    float4 result = 0;
    
#if RAW_SAMPLES
    result = rayBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy];
#else
    
    
#if 0
    float4 newRadiance = 0;
    int layers = 0;
    // average new radiance
    for (int dx = -layers; dx <= layers; ++dx)
    {
        for (int dy = -layers; dy <= layers; ++dy)
        {
            newRadiance += rayBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy + uint2(dx, dy)];
        }
    }
    
    newRadiance /= (2 * layers + 1) * (2 * layers + 1);

#else
    float4 newRadiance = rayBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy];
#endif
    
    float4 newPosDepth = rayBuffer[SLOT_POS_DEPTH][IN.DispatchThreadID.xy];
    float aspectRatio = filterData.cameraWindowSize.x / filterData.cameraWindowSize.y;
    
    
    float3 oldPos = mul(filterData.oldCameraWorldToClip, float4(newPosDepth.xyz, 1));
    oldPos = float3(-oldPos.x / aspectRatio, -oldPos.y, 0) / oldPos.z;
    
    // [-1,1] -> [0,1]
    oldPos.xy = oldPos.xy * 0.5 + 0.5;
    
    float3 newPos = mul(filterData.newCameraWorldToClip, float4(newPosDepth.xyz, 1));
    newPos = float3(-newPos.x / aspectRatio, -newPos.y, 0) / newPos.z;
    
    // [-1,1] -> [0,1]
    newPos.xy = newPos.xy * 0.5 + 0.5;
    
    // Reprojection
    float alpha = 0.2;
    float4 oldIntegratedColour = 0;
    float4 newIntegratedColour = 0;
    
    bool reuseSample = false;
    // If current position is visible in integrated history buffer.
    if (oldPos.x > 0 && oldPos.x < 1 && oldPos.y > 0 && oldPos.y < 1 )
    {
        uint2 oldRayPixelPos = (uint2) (oldPos.xy * filterData.windowResolution);
    
        // If the old object is the same object, then we can reuse
        float4 newObjMask = rayBuffer[SLOT_OBJECT_ID][IN.DispatchThreadID.xy];
        float4 oldObjMask = historyBuffer[SLOT_OBJECT_ID][oldRayPixelPos];
        reuseSample = length(newObjMask.xyz - oldObjMask.xyz) == 0;
    
        // Check that the material is lambertian, if not, then just accumulate on same position
        if (newObjMask.w == 0 && oldObjMask.w == 0)
            reuseSample &= length(oldPos.xy - newPos.xy) == 0;
        
        // if the old position is about the same as the new position
        //float3 oldRayPos = historyBuffer[SLOT_POS_DEPTH][oldRayPixelPos].xyz;
        float3 oldRayPos = BilienarFilter(historyBuffer[SLOT_POS_DEPTH], oldPos.xy * filterData.windowResolution);
        reuseSample &= length(oldRayPos - newPosDepth.xyz) < 10;

        // check normals?
        float3 newNormals = normalize(rayBuffer[SLOT_NORMALS][oldRayPixelPos].xyz * 2 - 1);
        float3 oldNormals = normalize(historyBuffer[SLOT_NORMALS][oldRayPixelPos].xyz * 2 - 1);
        reuseSample &= dot(newNormals, oldNormals) >= 0.99;
        
        
        // Decide alpha
        //oldIntegratedColour = BilienarFilter(historyBuffer[SLOT_COLOUR], oldPos.xy * filterData.windowResolution);
        oldIntegratedColour = historyBuffer[SLOT_COLOUR][oldRayPixelPos];

    }
    
    
    
    
    #if 0
    if (reuseSample)
        newIntegratedColour = alpha * newRadiance + (1 - alpha) * oldIntegratedColour;
    else
        newIntegratedColour = newRadiance;
    #elif 1
    if (reuseSample)
        newIntegratedColour = float4((newRadiance.xyz + oldIntegratedColour.w * oldIntegratedColour.xyz) / (oldIntegratedColour.w + 1), oldIntegratedColour.w + 1);
    else
        newIntegratedColour = float4(newRadiance.xyz, 1);
    #else
    if (reuseSample)
        newIntegratedColour = float4(1, 0, 0, 0);
    else
        newIntegratedColour = float4(0, 1, 0, 0);
    #endif
    
    filterBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy] = newIntegratedColour;
    
    result = newIntegratedColour;
    
#if 0
    float3 diff = historyBuffer[1][IN.DispatchThreadID.xy].xyz - rayBuffer[1][IN.DispatchThreadID.xy].xyz;
    if (dot(diff, diff) != 0)
        result = float4(1, 0, 0, 0);
#endif
    
    
#endif
    
    filterBuffer[FILTER_SLOT_SDR][IN.DispatchThreadID.xy] = clamp(result, 0, 1);

}