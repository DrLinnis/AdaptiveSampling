
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


float CalcDepthGradient(RWTexture2D<float4> tex, int2 p)
{
    float centre = tex[p].w;
    
    float left = texelFetch(tex, p + int2(-1, 0), centre).w;
    float right = texelFetch(tex, p + int2(1, 0), centre).w;
    float up = texelFetch(tex, p + int2(0, -1), centre).w;
    float down = texelFetch(tex, p + int2(0, 1), centre).w;
    
    float maxVert = max(abs(centre - left), abs(centre - right));
    float maxHori = max(abs(centre - up), abs(centre - down));
    return max(maxVert, maxHori);
}

/* Guassian 3x3 filter*/
float computeVarianceCenter(int2 center, RWTexture2D<float4> texInS)
{
    float sum = 0;

    const float kernel[2][2] =
    {
        { 1.0 / 4.0, 1.0 / 8.0 },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };

    const int radius = 1;
    for (int yy = -radius; yy <= radius; yy++)
    {
        for (int xx = -radius; xx <= radius; xx++)
        {
            int2 p = center + int2(xx, yy);

            float k = kernel[abs(xx)][abs(yy)];

            sum += texInS[p].w * k; // we sample the variance from the moments here
        }
    }

    return sum;
}


#if 1

/* 
    SUMMARY:= takes from (history, ray, filter) buffer, to write to WAVELET TARGET FILTER buffer 
*/

#define BLOCK_SIZE 16
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main(ComputeShaderInput IN)
{
    const float kernel[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };
    uint2 p = IN.DispatchThreadID.xy;
    
    float4 momentHistlenExtra = filterBuffer[FILTER_SLOT_MOMENT_SOURCE][p];
    uint histLength = momentHistlenExtra.z;
    
    float4 centreColour = filterBuffer[FILTER_SLOT_COLOUR_SOURCE][p];
    
    float lumP = luminance(centreColour.rgb);
    
    float3 centreNormal = normalize(rayBuffer[SLOT_NORMALS][p].xyz * 2.0 - 1.0);
    float3 centreObj = rayBuffer[SLOT_OBJECT_ID_MASK][p].xyz;
    float centreDepth = rayBuffer[SLOT_POS_DEPTH][p].w / T_HIT_MAX;
        
    float depthGradient = CalcDepthGradient(rayBuffer[SLOT_POS_DEPTH], p) / T_HIT_MAX;
        
    float1 sumWeight = 1.0;
    float4 sumColourVar = centreColour;
        
    float varGauss = computeVarianceCenter(p, filterBuffer[FILTER_SLOT_COLOUR_SOURCE]);
    float weightLumDenominator = filterData.sigmaLuminance * sqrt(max(0, EPSILON + varGauss));
    
// ignore filter
#if 1
    // 5x5 � trous wavelet filter
    for (int yOffset = -2; yOffset <= 2; yOffset++)
    {
        for (int xOffset = -2; xOffset <= 2; xOffset++)
        {
            if (xOffset != 0 || yOffset != 0)
            {
                int2 q = p + 2 * int2(xOffset, yOffset);
                
                if (q.x < 0 || q.x >= filterData.windowResolution.x || q.y < 0 || q.y >= filterData.windowResolution.y)
                    continue;
                    
                float4 currColourVar = texelFetch(filterBuffer[FILTER_SLOT_COLOUR_SOURCE], q, 0);
                float3 currNormal = normalize(texelFetch(rayBuffer[SLOT_NORMALS], q, 0).xyz * 2.0 - 1.0);
                float3 currObj = texelFetch(rayBuffer[SLOT_OBJECT_ID_MASK], q, 0).xyz;
                float1 currDepth = texelFetch(rayBuffer[SLOT_POS_DEPTH], q, 0).w / T_HIT_MAX;
                
                float lumQ = luminance(currColourVar.rgb);
                    
                // For estimating the variance spatially, we use normals, depth, and luminace
                float kernelWeight = kernel[abs(xOffset)] * kernel[abs(yOffset)];
                
                // w_l
                float weightLuminace = abs(lumP - lumQ) / weightLumDenominator;
                // w_n
                float weightNormal = pow(max(0.0, dot(centreNormal, currNormal)), filterData.sigmaNormal);
                // w_z
                float weightDepth = abs(centreDepth - currDepth) / (abs(depthGradient * length(float2(xOffset, yOffset))) * filterData.sigmaDepth + EPSILON);

                // W_i = h(q) * w(p,q)
                float w_i = exp(0.0 - max(weightDepth, 0.0) - max(weightLuminace, 0)) * weightNormal * kernelWeight;
                    
                if (isnan(w_i))
                    w_i = 0;
                    
                sumColourVar += float4(w_i.xxx, w_i * w_i ) * currColourVar;
                sumWeight += w_i;
            }
        }
    }
        
    sumWeight = max(sumWeight, EPSILON);
    sumColourVar /= float4(sumWeight.xxx, sumWeight * sumWeight);
    
#endif
    filterBuffer[FILTER_SLOT_COLOUR_TARGET][p] = sumColourVar;
    
    
    // remove in future
    filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(linearToSrgb(sumColourVar.rgb), 1), 0, 1);
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(1, 0, 1);
}

#else

// 7x7 bilateral filter
float calcSpatialVariance(inout float2 moment, float histlen, uint2 pixelPos, float linearGradientPixel)
{
    
    float weightSum = 1.0;
    int radius = 3;
    
    float3 c = filterBuffer[FILTER_SLOT_INTEGRATED_COLOUR][pixelPos].rgb;
    
    float3 normalPixel = normalize(texelFetch(rayBuffer[SLOT_NORMALS], pixelPos, 0).xyz * 2 - 1);
    float3 meshPixel = texelFetch(rayBuffer[SLOT_OBJECT_ID_MASK], pixelPos, 0).xyz;
    float depthPixel = texelFetch(rayBuffer[SLOT_POS_DEPTH], pixelPos, 0).w;
    
    for (int yy = -radius; yy <= radius; ++yy)
    {
        for (int xx = -radius; xx <= radius; ++xx)
        {
            if (xx == 0 && yy == 0)
            {
                continue;
            }
            
            uint2 q = int2(pixelPos) + int2(xx, yy);
            float3 curColor = texelFetch(filterBuffer[FILTER_SLOT_INTEGRATED_COLOUR], q, 0).rgb;
            float currDepth = texelFetch(rayBuffer[SLOT_POS_DEPTH], q, 0).w;
            float3 curNormal = normalize(texelFetch(rayBuffer[SLOT_NORMALS], q, 0).xyz * 2 - 1);
            float3 curMeshID = texelFetch(rayBuffer[SLOT_OBJECT_ID_MASK], q, 0).xyz;
            
            
            float lumQ = luminance(curColor.xyz);
            
            // x in w_z = exp(-x)
            float weightDepth = abs(depthPixel - currDepth) / (linearGradientPixel * length(float2(xx, yy)) + EPSILON);
            // w_n 
            float weightNormal = pow(max(0, dot(curNormal, normalPixel)), 128);
            
            // w_i(p,q) = w_z * w_n * w_l * mask
            float w = exp(-weightDepth) * weightNormal * (length(curMeshID - meshPixel) == 0);

            if (isnan(w))
                w = 0.0;
            
            weightSum += w;
            
            moment += float2(lumQ, lumQ * lumQ) * w;
            
        }
    }
    
    moment /= weightSum;
    
    return (1.0 + 2.0 * (1 - histlen)) * max(0.0, moment.y - moment.x * moment.x);
}

float4 WaveletFilter(inout float variance, in RWTexture2D<float4> colour, in uint2 currUv, float linearGradientPixel, int step_size )
{
    
    // 3/8, 1/4, 1/16
    const float hList[] = { 0.375, 0.25, 0.0625 };
    const int radius = 2;
    
    float4 colourNominator = 0;
    float colourDenominator = 0;
    
    float varNominator = 0;
    float varDenominator = 0;
    
    float3 normalPixel = normalize(texelFetch(rayBuffer[SLOT_NORMALS], currUv, 0).xyz * 2 - 1);
    float luminancePixel = luminance(colour[currUv].xyz);
    float depthPixel = texelFetch(rayBuffer[SLOT_POS_DEPTH], currUv, 0).w;
    float3 meshPixel = texelFetch(rayBuffer[SLOT_OBJECT_ID_MASK], currUv, 0).xyz;
    
    for (int yy = -radius; yy <= radius; ++yy)
    {
        for (int xx = -radius; xx <= radius; ++xx)
        {
            // sample which filter kernal stage we are in.
            float h = hList[max(radius - abs(xx), radius - abs(yy))];
            
            uint2 q = currUv + int2(xx, yy);
            
            float currDepth = texelFetch(rayBuffer[SLOT_POS_DEPTH], q, 0).w;
            float3 currNormal = normalize(texelFetch(rayBuffer[SLOT_NORMALS], q, 0).xyz * 2 - 1);
            float currLuminance = luminance(colour[q].xyz);
            float3 currMeshID = texelFetch(rayBuffer[SLOT_OBJECT_ID_MASK], q, 0).xyz;
            
            // LOOK AT THIS := https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/asvgf_atrous.comp
            float weightDepth = abs(depthPixel - currDepth) / (filterData.sigmaDepth * linearGradientPixel * length(float2(xx, yy)) + EPSILON);
            float weightNormal = pow(max(0, dot(currNormal, normalPixel)), filterData.sigmaNormal);
            float weightLuminance = abs(luminancePixel - currLuminance) / (filterData.sigmaLuminance * sqrt(variance) + EPSILON);
            
            float w_i = exp(-weightDepth) * weightNormal * exp(-weightLuminance) * (length(meshPixel - currMeshID) == 0 ? 1 : 0);
            
            if (isnan(w_i))
                w_i = 0.0;
            
            colourNominator += h * w_i * colour[q];
            colourDenominator += h * w_i;
            
            // h(q)^2 * w(p,q)^2 * Var(C_i(q))
            varNominator += h * h * w_i * w_i * texelFetch(historyBuffer[SLOT_MOMENT_HISTORY], q, 0).w;
            varDenominator += h * w_i;
        }
    }
    
    variance = varNominator / max(varDenominator * varDenominator, EPSILON);
    return colourNominator / max(colourDenominator, EPSILON);
    //return weightNormal;

}

// Shader toy inspired temporal reprojection := https://www.shadertoy.com/view/ldtGWl


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
    float alpha = 0.2;
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
        float3 oldNormals = normalize(BilienarFilter(historyBuffer[SLOT_NORMALS], oldPos.xy * filterData.windowResolution).xyz * 2 - 1);
        reuseSample &= dot(newNormals, oldNormals) >= 0.95;
        
        
        // Decide alpha
        oldIntegratedColour = BilienarFilter(historyBuffer[SLOT_COLOUR], oldPos.xy * filterData.windowResolution);
        
        //oldIntegratedColour = historyBuffer[SLOT_COLOUR][oldRayPixelPos];

    }
    
    
    
    float4 newIntegratedColour = 0;
    if (reuseSample)
        newIntegratedColour = alpha * newRadiance + (1 - alpha) * oldIntegratedColour;
    else
        newIntegratedColour = newRadiance;
    
    // temporarly write the reprojected value to filter integrated colour buffer.
    filterBuffer[FILTER_SLOT_INTEGRATED_COLOUR][IN.DispatchThreadID.xy] = newIntegratedColour;
    float4 backup = newIntegratedColour;
    
    float4 oldIntegratedMoment = texelFetch(historyBuffer[SLOT_MOMENT_HISTORY], oldPos.xy, 0);
    
    float2 moment = oldIntegratedMoment.xy; // moment
    float histlen = oldIntegratedMoment.z;
    float temporalVariance = max(0, moment.y - moment.x * moment.x);
    
    // Sync temporal reprojection. Everything is stored in the current buffer now.
    AllMemoryBarrierWithGroupSync();
    
    /*
        ESTIMATE VARIANCE AND WAVELETS
    */
    float centre = texelFetch(rayBuffer[SLOT_POS_DEPTH], IN.DispatchThreadID.xy, 0).w;
    float up = texelFetch(rayBuffer[SLOT_POS_DEPTH], IN.DispatchThreadID.xy + int2(0, 1), 0).w;
    float right = texelFetch(rayBuffer[SLOT_POS_DEPTH], IN.DispatchThreadID.xy + int2(1,0), 0).w;
    
    float linearGradient = max(abs(centre - right), abs(centre - up));
    
    // only use the variance once the moment has been accumulated for a while.
    float variance = histlen > 4 ? temporalVariance : calcSpatialVariance(moment, histlen, IN.DispatchThreadID.xy, linearGradient);
    
    newIntegratedColour = WaveletFilter(variance, filterBuffer[FILTER_SLOT_INTEGRATED_COLOUR], IN.DispatchThreadID.xy, linearGradient, 1);
    
    filterBuffer[FILTER_SLOT_MOMENT][IN.DispatchThreadID.xy] = float4(reuseSample ? moment : 0, reuseSample ? histlen + 1 : 0, variance);
    
    int n = 5;
    for (int i = 0; i < n; ++i)
    {
        // sync everyone finished reading in wavelet before writing to texture
        AllMemoryBarrierWithGroupSync();
        filterBuffer[FILTER_SLOT_WAVELET][IN.DispatchThreadID.xy] = newIntegratedColour;
        filterBuffer[FILTER_SLOT_MOMENT][IN.DispatchThreadID.xy].w = variance;
        
        // ONLY write to integrated colour after first wavelet filtering, the rest is just extra processing for a pretty image.
        if (i == 0)
        {
            filterBuffer[FILTER_SLOT_INTEGRATED_COLOUR][IN.DispatchThreadID.xy] = newIntegratedColour;
            ++i;
        }
        
        // sync everyong finish writing before we read in the wavelet.
        AllMemoryBarrierWithGroupSync();
        newIntegratedColour = WaveletFilter(variance, filterBuffer[FILTER_SLOT_WAVELET], IN.DispatchThreadID.xy, linearGradient, (1 << i));
        
    }
    
    
    
    
    
}

#endif