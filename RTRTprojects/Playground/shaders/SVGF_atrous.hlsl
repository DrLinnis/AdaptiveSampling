
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

/* 
    SUMMARY:= takes from (history, ray, filter) buffer, to write to WAVELET TARGET FILTER buffer 
*/

#define BLOCK_SIZE 16
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main(ComputeShaderInput IN)
{
    if (IN.DispatchThreadID.x >= filterData.windowResolution.x || IN.DispatchThreadID.y >= filterData.windowResolution.y)
        return;
    
    const float kernel[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };
    uint2 p = IN.DispatchThreadID.xy;
    
    float4 momentHistlenStepsize = filterBuffer[FILTER_SLOT_MOMENT_SOURCE][p];
    uint histLength = momentHistlenStepsize.z;
    int stepsize = (int) momentHistlenStepsize.w;
    
    float4 centreColour = filterBuffer[FILTER_SLOT_COLOUR_SOURCE][p];
    
    float lumP = luminance(clamp(centreColour.rgb, 0, 1));
    
    float3 centreNormal = normalize(rayBuffer[SLOT_NORMALS][p].xyz * 2.0 - 1.0);
    float3 centreObj = rayBuffer[SLOT_OBJECT_ID_MASK][p].xyz;
    float centreDepth = rayBuffer[SLOT_POS_DEPTH][p].w;
        
    float depthGradient = CalcDepthGradient(rayBuffer[SLOT_POS_DEPTH], p);
        
    float1 sumWeight = 1.0;
    float4 sumColourVar = centreColour;
        
    float varGauss = computeVarianceCenter(p, filterBuffer[FILTER_SLOT_COLOUR_SOURCE]);
    float weightLumDenominator = filterData.sigmaLuminance * sqrt(max(0, EPSILON + varGauss));
    
// ignore filter
#if 1
    // 5x5 à trous wavelet filter
    for (int yOffset = -2; yOffset <= 2; yOffset++)
    {
        for (int xOffset = -2; xOffset <= 2; xOffset++)
        {
            int2 q = p + pow(2, stepsize) * int2(xOffset, yOffset);
                
            if ((xOffset == 0 && yOffset == 0)
                    || q.x < 0 || q.x >= filterData.windowResolution.x 
                    || q.y < 0 || q.y >= filterData.windowResolution.y)
                continue;
                
            float4 currColourVar = filterBuffer[FILTER_SLOT_COLOUR_SOURCE][q];
            float3 currNormal = normalize(rayBuffer[SLOT_NORMALS][q].xyz * 2.0 - 1.0);
            float3 currObj = rayBuffer[SLOT_OBJECT_ID_MASK][q].xyz;
            float1 currDepth = rayBuffer[SLOT_POS_DEPTH][q].w;
                
            if (length(currObj - centreObj))
                continue;
            
            float lumQ = luminance(clamp(currColourVar.rgb, 0, 1));
                    
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
        
    //sumWeight = max(sumWeight, EPSILON);
    sumColourVar /= float4(sumWeight.xxx, sumWeight * sumWeight);
    momentHistlenStepsize.w = stepsize + 1;
#endif
    
    
    filterBuffer[FILTER_SLOT_COLOUR_TARGET][p] = sumColourVar;
    filterBuffer[FILTER_SLOT_MOMENT_TARGET][p] = momentHistlenStepsize;
    
#define PROPER 1
#if PROPER
    filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(linearToSrgb(sumColourVar.rgb), 1), 0, 1);
    
#else
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(centreNormal * 0.5 + 0.5, 1), 0, 1);
    //if (stepsize == 3)
        //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(float4(linearToSrgb(sumColourVar.rgb), 1), 0, 1);
    //if (stepsize == 2)
    //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = clamp(sumColourVar.w * 100000, 0, 1);
    // Aim
    //if (filterData.windowResolution.x / 2 == IN.DispatchThreadID.x || filterData.windowResolution.y / 2 == IN.DispatchThreadID.y )
        //filterBuffer[FILTER_SLOT_SDR_TARGET][IN.DispatchThreadID.xy] = float4(1, 0, 0, 0);
#endif
    
}
