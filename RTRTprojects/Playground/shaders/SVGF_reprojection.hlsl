
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

#define FILTER_SLOT_INTEGRATED_COLOUR 0
#define FILTER_SLOT_MOMENT 1
#define FILTER_SLOT_SDR 2
#define FILTER_SLOT_WAVELET_TARGET 3

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

float4 BilienarFilter(RWTexture2D<float4> tex, float2 st)
{
    float2 AB = frac(st);
    uint2 uv00 = (uint2) clamp(st - AB, 0, filterData.windowResolution);
    uint2 uv10 = (uint2) clamp(uv00 + uint2(1, 0), 0, filterData.windowResolution);
    uint2 uv11 = (uint2) clamp(uv00 + uint2(1, 1), 0, filterData.windowResolution);
    uint2 uv01 = (uint2) clamp(uv00 + uint2(0, 1), 0, filterData.windowResolution);
    
    float4 r1 = AB.x * tex[uv10] + (1 - AB.x) * tex[uv00];
    float4 r2 = AB.x * tex[uv11] + (1 - AB.x) * tex[uv01];
    return AB.y * r2 + (1 - AB.y) * r1;
}

float4 texelFetch(RWTexture2D<float4> tex, uint2 pixelPos, float4 default_value)
{
    if (pixelPos.x >= 0 && pixelPos.x < filterData.windowResolution.x &&
            pixelPos.y >= 0 && pixelPos.y < filterData.windowResolution.y)
        return tex[pixelPos];
    return default_value;
}

// Shader toy inspired temporal reprojection := https://www.shadertoy.com/view/ldtGWl

/* 
    SUMMARY:= takes from history buffer and ray buffer, to write to filter buffer 
*/

#define BLOCK_SIZE 16
[numthreads( BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( ComputeShaderInput IN ) 
{ 
    
    float4 newRadiance = rayBuffer[SLOT_COLOUR][IN.DispatchThreadID.xy];
    float4 newPosDepth = rayBuffer[SLOT_POS_DEPTH][IN.DispatchThreadID.xy];
    float3 newNormals = normalize(rayBuffer[SLOT_NORMALS][IN.DispatchThreadID.xy].xyz * 2 - 1);
    float4 newObjMask = rayBuffer[SLOT_OBJECT_ID_MASK][IN.DispatchThreadID.xy];
    
    float aspectRatio = filterData.cameraWindowSize.x / filterData.cameraWindowSize.y;
    
    
    float3 oldClipPos = mul(filterData.oldCameraWorldToClip, float4(newPosDepth.xyz, 1));
    float oldDiv = oldClipPos.z;
    float3 oldPos = float3(-oldClipPos.x / aspectRatio, -oldClipPos.y, 0) / oldDiv;
    
    // [-1,1] -> [0,1]
    oldPos.xy = oldPos.xy * 0.5 + 0.5;
    uint2 oldRayPixelPos = (uint2) (clamp(oldPos.xy, 0, 1) * filterData.windowResolution);
    
    float3 newClipPos = mul(filterData.newCameraWorldToClip, float4(newPosDepth.xyz, 1));
    float newDiv = newClipPos.z;
    float3 newPos = float3(-newClipPos.x / aspectRatio, -newClipPos.y, 0) / newDiv;
    
    // [-1,1] -> [0,1]
    newPos.xy = newPos.xy * 0.5 + 0.5;
    
    // Reprojection
    float4 oldIntegratedColour = 0;
    
    bool reuseSample = false;
    // If current position is visible in integrated history buffer.
    if (oldPos.x > 0 && oldPos.x < 1 && oldPos.y > 0 && oldPos.y < 1 && oldPos.z >= 0)
    {
        // If the old object is the same object, then we can reuse
        float4 oldObjMask = BilienarFilter(historyBuffer[SLOT_OBJECT_ID_MASK], oldPos.xy * filterData.windowResolution);
        reuseSample = length(newObjMask.xyz - oldObjMask.xyz) == 0;
    
        // Check that the material is not lambertian, then just accumulate on same position
        if (newObjMask.w != 0 || oldObjMask.w != 0)
            reuseSample &= length(oldPos.xy - newPos.xy) == 0;
        
        // if the old position is about the same as the new position
        float3 oldRayPos = BilienarFilter(historyBuffer[SLOT_POS_DEPTH], oldPos.xy * filterData.windowResolution).xyz;
        //float3 oldRayPos = historyBuffer[SLOT_POS_DEPTH][oldRayPixelPos].xyz;
        reuseSample &= length(oldRayPos - newPosDepth.xyz) < filterData.reprojectErrorLimit;

        // check normals?
        //float3 oldNormals = normalize(BilienarFilter(historyBuffer[SLOT_NORMALS], oldPos.xy * filterData.windowResolution).xyz * 2 - 1);
        float3 oldNormals = normalize(historyBuffer[SLOT_POS_DEPTH][oldRayPixelPos].xyz * 2 - 1);
        reuseSample &= length(newNormals -  oldNormals) == 0.0;
        
        
        // Decide alpha
        oldIntegratedColour = BilienarFilter(historyBuffer[SLOT_COLOUR], oldPos.xy * filterData.windowResolution);
        
        //oldIntegratedColour = historyBuffer[SLOT_COLOUR][oldRayPixelPos];

    }
    
    float4 momentsHistlenExtra = 0;
    float4 reprojectedColour = 0;
    if (reuseSample)
    {
        momentsHistlenExtra = BilienarFilter(historyBuffer[SLOT_COLOUR], oldPos.xy * filterData.windowResolution);
        momentsHistlenExtra.z = floor(momentsHistlenExtra.z + 1);
        reprojectedColour = filterData.alpha * newRadiance + (1 - filterData.alpha) * oldIntegratedColour;
    }
    else
    {
        momentsHistlenExtra.z = 1;
        reprojectedColour = newRadiance;
    }
    
    momentsHistlenExtra.x = luminance(reprojectedColour.rgb);
    momentsHistlenExtra.y = momentsHistlenExtra.x * momentsHistlenExtra.x;
    
    filterBuffer[FILTER_SLOT_MOMENT][IN.DispatchThreadID.xy] = momentsHistlenExtra;
    filterBuffer[FILTER_SLOT_INTEGRATED_COLOUR][IN.DispatchThreadID.xy] = reprojectedColour;
    
    // remove in future
    filterBuffer[FILTER_SLOT_SDR][IN.DispatchThreadID.xy] = clamp(float4(linearToSrgb(reprojectedColour.rgb), 1), 0, 1);
    //filterBuffer[FILTER_SLOT_SDR][IN.DispatchThreadID.xy] = clamp(float4(linearToSrgb(newRadiance.rgb), 1), 0, 1);
    //filterBuffer[FILTER_SLOT_SDR][IN.DispatchThreadID.xy] = clamp(float4(reuseSample ? float3(1, 0, 0) : 0, 1), 0, 1);
}