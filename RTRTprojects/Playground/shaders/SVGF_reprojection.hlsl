
#define PI 3.1415926538
#define PI2 6.283185307
#define PI_2 1.570796327
#define PI_4 0.7853981635
#define InvPi 0.318309886
#define InvPi2 0.159154943
#define EPSILON 0.00001

#define T_HIT_MIN 0.0001
#define T_HIT_MAX 100000


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
    
    float alpha;
    float reprojectErrorLimit;
    
    float sigmaDepth;
    float sigmaNormal;
    float sigmaLuminance;
    
    int stepSize;
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

#define FILTER_SLOT_COLOUR_SOURCE 0
#define FILTER_SLOT_MOMENT_SOURCE 1
#define FILTER_SLOT_SDR_TARGET 2
#define FILTER_SLOT_COLOUR_TARGET 3
#define FILTER_SLOT_MOMENT_TARGET 4

#define SLOT_COLOUR 0
#define SLOT_NORMALS 1
#define SLOT_POS_DEPTH 2
#define SLOT_OBJECT_ID_MASK 3
#define SLOT_MOMENT_HISTORY 4

#define RAW_SAMPLES 0

#define REPROJ_DELTA 0.01

// Luminance equation from International Telecommunication Union
// https://www.itu.int/rec/R-REC-BT.601
float luminance(float3 colour)
{
    return 0.299 * colour.r + 0.587 * colour.g + 0.114 * colour.b;
}

float3 linearToSrgb(float3 c)
{
    // Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
    return srgb;
}

float4 texelFetch(RWTexture2D<float4> tex, uint2 pixelPos, float4 default_value)
{
    if (pixelPos.x >= 0 && pixelPos.x < filterData.windowResolution.x &&
            pixelPos.y >= 0 && pixelPos.y < filterData.windowResolution.y)
        return tex[pixelPos];
    return default_value;
}

bool BilienarTapFilter(float2 oldUv, float2 newUv, float3 normal, float3 worldPos, 
        float3 obj, float mask, out float4 oldIntegratedColour, out float histLength )
{
    float2 st = oldUv * filterData.windowResolution;
    int2 p0 = (int2) round(st);
    
    float4 integratedColour = 0;
    float sumSamples = 0.0;
    float lowestHistlength = 1.#INF;
    
    const int layer = 0;
    for (int dx = -layer; dx <= layer; ++dx)
    {
        for (int dy = -layer; dy <= layer; ++dy)
        {
            int2 q = p0 + int2(dx, dy);
        
            if (q.x < 0 || q.x >= filterData.windowResolution.x || q.y < 0 || q.y >= filterData.windowResolution.y)
                continue;
        
            // check obj
            if (length(historyBuffer[SLOT_OBJECT_ID_MASK][q].xyz - obj) != 0)
                continue;
        
            // check normal
            if (dot(normalize(historyBuffer[SLOT_NORMALS][q].xyz * 2.0 - 1), normal) <= 1.0 - EPSILON)
                continue;
        
            // check position
            if (length(historyBuffer[SLOT_POS_DEPTH][q].xyz - worldPos) > filterData.reprojectErrorLimit)
                continue;
        
            // check uv if material is transparent
            if ((mask != 0 || historyBuffer[SLOT_OBJECT_ID_MASK][q].w != 0) && length(oldUv - newUv) != 0)
                continue;
        
            // sample colour, histlength, and add.
            integratedColour += historyBuffer[SLOT_COLOUR][q];
        
            lowestHistlength = max(min(lowestHistlength, historyBuffer[SLOT_MOMENT_HISTORY][q].z), 0);
            sumSamples += 1.0f;
        }
            
    }
    
    histLength = lowestHistlength;
    
    if (sumSamples == 0)
    {
        oldIntegratedColour = 0;
        return false;
    }

    oldIntegratedColour = integratedColour / sumSamples;
    //oldIntegratedColour = historyBuffer[SLOT_MOMENT_HISTORY][filterData.windowResolution.xy - int2(1, 1)] ;
    return true;
    
}


// Shader toy inspired temporal reprojection := https://www.shadertoy.com/view/ldtGWl

/* 
    SUMMARY:= takes from history buffer and ray buffer, to write to filter buffer 
*/

#define BLOCK_SIZE 16
[numthreads( BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( ComputeShaderInput IN ) 
{ 
    if (IN.DispatchThreadID.x >= filterData.windowResolution.x || IN.DispatchThreadID.y >= filterData.windowResolution.y)
        return;
    
    // current pixel worldPos
    float4 newPosDepth = rayBuffer[SLOT_POS_DEPTH][IN.DispatchThreadID.xy];
    
    // Old UV and curr UV
    float aspectRatio = filterData.cameraWindowSize.x / filterData.cameraWindowSize.y;
    
    float3 oldClipPos = mul(filterData.oldCameraWorldToClip, float4(newPosDepth.xyz, 1));
    float oldDiv = oldClipPos.z;
    float3 oldPos = float3(oldClipPos.x / aspectRatio, oldClipPos.y, 0) / oldDiv;
    
    // [-1,1] -> [0,1]
    oldPos.xy = oldPos.xy * 0.5 + 0.5;
    uint2 oldRayPixelPos = (uint2) round((clamp(oldPos.xy, 0, 1) * filterData.windowResolution));
    
    float3 newClipPos = mul(filterData.newCameraWorldToClip, float4(newPosDepth.xyz, 1));
    float newDiv = newClipPos.z;
    float3 newPos = float3(newClipPos.x / aspectRatio, newClipPos.y, 0) / newDiv;
    
    // [-1,1] -> [0,1]
    newPos.xy = newPos.xy * 0.5 + 0.5;
    uint2 newRayPixelPos = (uint2) round((clamp(newPos.xy, 0, 1) * filterData.windowResolution));
    
    
    float4 newRadiance = rayBuffer[SLOT_COLOUR][newRayPixelPos];
    float3 newNormals = normalize(rayBuffer[SLOT_NORMALS][newRayPixelPos].xyz * 2 - 1);
    float4 newObjMask = rayBuffer[SLOT_OBJECT_ID_MASK][newRayPixelPos];
    
    
    // Reprojection
    float4 oldIntegratedColour = 0;
    float oldHistLength = 0;
    
    
    bool reuseSample = false;
    // If current position is visible in integrated history buffer.
    if (oldPos.x >= 0 && oldPos.x < 1 && oldPos.y >= 0 && oldPos.y < 1)
    {
        // assume true until we multiple with false
        reuseSample = BilienarTapFilter(oldPos.xy, newPos.xy, newNormals, newPosDepth.xyz,
            newObjMask.xyz, newObjMask.w, oldIntegratedColour, oldHistLength);
        
    }
    
    float4 momentHistlenStepsize = 0;
    float4 reprojectedColour = 0;
    if (reuseSample)
    {
        momentHistlenStepsize.z = floor(oldHistLength) + 1;
#if 1
        reprojectedColour.xyz = filterData.alpha * newRadiance.xyz + (1 - filterData.alpha) * oldIntegratedColour.xyz;
#else
        reprojectedColour.xyz = (newRadiance.xyz + (momentHistlenStepsize.z - 1) * oldIntegratedColour.xyz) / momentHistlenStepsize.z;
#endif
        
    }
    else
    {
        momentHistlenStepsize.z = 1;
        reprojectedColour.xyz = newRadiance.xyz;
    }
    
    momentHistlenStepsize.x = luminance(reprojectedColour.rgb);
    momentHistlenStepsize.y = momentHistlenStepsize.x * momentHistlenStepsize.x;
    
    
    filterBuffer[FILTER_SLOT_MOMENT_TARGET][IN.DispatchThreadID.xy] = momentHistlenStepsize;
    filterBuffer[FILTER_SLOT_COLOUR_TARGET][IN.DispatchThreadID.xy] = reprojectedColour;
    
    
    
    
    // remove in future
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(rayBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy], 0, 1);
    filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(linearToSrgb(reprojectedColour.rgb), 1), 0, 1);
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(newPos.xy, IN.DispatchThreadID.y >= filterData.windowResolution.y ? 1 : 0, 0), 0, 1);
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(length(oldRayPixelPos - IN.DispatchThreadID.xy) > 1, 0, 0, 0), 0, 1);
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(linearToSrgb(newRadiance.rgb), 1), 0, 1);
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(historyBuffer[SLOT_NORMALS][oldRayPixelPos].xyz - rayBuffer[SLOT_NORMALS][IN.DispatchThreadID.xy].xyz, 1), 0, 1);
}