
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
    uint3 GroupID : SV_GroupID; // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID : SV_GroupThreadID; // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID : SV_DispatchThreadID; // 3D index of global thread ID in the dispatch.
    uint GroupIndex : SV_GroupIndex; // Flattened local index of the thread within a thread group.
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
    
    int gridSize;
    
    float as_posDiffLimit;
    float as_normalDotLimit;
    float as_depthDiffLimit;
    float as_colourLimit;
};

struct CB
{
    int iteration;
};

ConstantBuffer<DenoiserFilterData> filterData : register(b0);

ConstantBuffer<CB> data : register(b1);

/*
    colour,
    normal,
    posDepth,
    objectMask,
*/
RWTexture2D<float4> rayBuffer[] : register(u0, space0);


#define SLOT_COLOUR 0
#define SLOT_NORMALS 1
#define SLOT_POS_DEPTH 2
#define SLOT_OBJECT_ID_MASK 3

#define RAW_SAMPLES 0

#define AS_DEFAULT 0
#define AS_CAST 1
#define AS_CASTED 2
#define AS_INTERPOLATED 3

// Shader toy inspired temporal reprojection := https://www.shadertoy.com/view/ldtGWl

/* 
    SUMMARY:= Schedules 
*/

struct Triangle
{
    uint2 one;
    uint2 two;
    uint2 three;
    
    float3 barycentrics;
};

Triangle buildTriangle(uint2 pos, uint side)
{
    Triangle result;
 
    uint2 upperLeft = side * floor(pos / side);
    uint2 lowerRight = upperLeft + uint2(side, side);
    uint2 lowerLeft = uint2(upperLeft.x, lowerRight.y);
    uint2 upperRight = uint2(lowerRight.x, upperLeft.y);
    
    
    
    return result;
}

bool tryInterpolateFromTriangle(uint2 pos, in Triangle tri)
{
    float4 colOne = rayBuffer[SLOT_COLOUR][tri.one];
    float4 colTwo = rayBuffer[SLOT_COLOUR][tri.two];
    float4 colThree = rayBuffer[SLOT_COLOUR][tri.three];
    
    if ( colOne.w > 0 && colTwo.w > 0 && colThree.w > 0 )
    {
        //float
    }
    return false;
}

bool tryInterpolateFromNeighbour(uint2 pos)
{
    int2 pUp = pos + int2(0, 1);
    int2 pDown = pos - int2(0, 1);
    int2 pLeft = pos + uint2(-1, 0);
    int2 pRight = pos + uint2(0, 1);
    
    float4 up = rayBuffer[SLOT_COLOUR][pUp];
    float4 down = rayBuffer[SLOT_COLOUR][pDown];
    float4 left = rayBuffer[SLOT_COLOUR][pLeft];
    float4 right = rayBuffer[SLOT_COLOUR][pRight];
    
    // Check if all are sampled.
    if (up.w > AS_CAST && down.w > AS_CAST && left.w > AS_CAST && right.w > AS_CAST)
    {
        float4 intColour = (up + down + left + right) * 0.25f;
        // check if colour is ok
        if (length(intColour.xyz - up.xyz) < filterData.as_colourLimit &&
            length(intColour.xyz - down.xyz) < filterData.as_colourLimit &&
            length(intColour.xyz - left.xyz) < filterData.as_colourLimit &&
            length(intColour.xyz - right.xyz) < filterData.as_colourLimit)
        { 
            // check if same object (with mask)
            up = rayBuffer[SLOT_OBJECT_ID_MASK][pUp];
            down = rayBuffer[SLOT_OBJECT_ID_MASK][pDown];
            left = rayBuffer[SLOT_OBJECT_ID_MASK][pLeft];
            right = rayBuffer[SLOT_OBJECT_ID_MASK][pRight];
        
            if (length(up - down) == 0 &&
                length(left - right) == 0 &&
                length(up - left) == 0)
            {
                float4 intObj = up;
                // from [0,1] to [-1,1]
                up.xyz = normalize(rayBuffer[SLOT_NORMALS][pUp].xyz * 2 - 1);
                down.xyz = normalize(rayBuffer[SLOT_NORMALS][pDown].xyz * 2 - 1);
                left.xyz = normalize(rayBuffer[SLOT_NORMALS][pLeft].xyz * 2 - 1);
                right.xyz = normalize(rayBuffer[SLOT_NORMALS][pRight].xyz * 2 - 1);
            
                // check normals pointing in somewhat same direction
                if (dot(up.xyz, down.xyz) > filterData.as_normalDotLimit &&
                    dot(left.xyz, right.xyz) > filterData.as_normalDotLimit &&
                    dot(up.xyz, left.xyz) > filterData.as_normalDotLimit)
                {
                    float3 intNorm = normalize(up.xyz + down.xyz + left.xyz + right.xyz);
                    up = rayBuffer[SLOT_POS_DEPTH][pUp];
                    down = rayBuffer[SLOT_POS_DEPTH][pDown];
                    left = rayBuffer[SLOT_POS_DEPTH][pLeft];
                    right = rayBuffer[SLOT_POS_DEPTH][pRight];
                    
                    // check world position is within limit
                    if (length(up.xyz - down.xyz) < 2 * filterData.as_posDiffLimit &&
                        length(left.xyz - right.xyz) < 2 * filterData.as_posDiffLimit &&
                        length(up.xyz - left.xyz) < 2 * filterData.as_posDiffLimit)
                    {
                        float4 intPosition = (up + down + left + right) * 0.25f;
                        
                        // Write interpolated value
                        rayBuffer[SLOT_COLOUR][pos] = intColour;
                        rayBuffer[SLOT_NORMALS][pos] = float4(intNorm * 0.5 + 0.5, 1); // [-1,1] to [0,1]
                        rayBuffer[SLOT_POS_DEPTH][pos] = intPosition;
                        rayBuffer[SLOT_OBJECT_ID_MASK][pos] = intObj;
                        
                        // mark this pixel as interpolated and not traced.
                        rayBuffer[SLOT_COLOUR][pos].w = AS_INTERPOLATED;
                        
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// 3, 5, 9, 17, etc etc
int calcWidth(int widthIndex)
{
    int result = 3;
    for (int i = 1; i < widthIndex; ++i)
        result += (1 << i);
    return result;
}

bool shootNextRay(uint2 pos)
{
    return false;
}

#define BLOCK_SIZE 16
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main(ComputeShaderInput IN)
{
    // cleans out those outside of the image
    if (IN.DispatchThreadID.x >= filterData.windowResolution.x || IN.DispatchThreadID.y >= filterData.windowResolution.y)
        return;
    
    uint2 launchIndex = IN.DispatchThreadID.xy;
    
    // cleans out those that have already been traced.
    if (rayBuffer[SLOT_COLOUR][launchIndex.xy].w > 0)
        return;
    
    int maxStep = filterData.gridSize * 2;   

    uint squareSize = calcWidth(filterData.gridSize);
    uint tileSize = squareSize - 1;
    
    // initial step
    if (data.iteration == 1)
    {
        uint2 upperTileLimit = tileSize * floor((filterData.windowResolution.xy - 1) / tileSize);

        if (launchIndex.x > 0 && launchIndex.x < upperTileLimit.x &&
            launchIndex.y > 0 && launchIndex.y < upperTileLimit.y)
        {
            if ((launchIndex.x % tileSize != 0 || launchIndex.y % tileSize != 0) &&
                ((launchIndex.x + (tileSize >> 1)) % tileSize != 0 || (launchIndex.y + (tileSize >> 1)) % tileSize != 0))
            {
                return;
            }
        }
    }
    else if (data.iteration == maxStep) // final step.
    {
        if (tryInterpolateFromNeighbour(launchIndex.xy))
            return;
    }
    else if (false) // smart cast between pixels. 
    {
        Triangle tri = buildTriangle(launchIndex.xy, tileSize);
        
        //
        if (tryInterpolateFromTriangle(launchIndex.xy, tri))
            return;
        
        // 
        if (!shootNextRay(launchIndex.xy))
            return;
    }
    else
    {
        return;
    }
    
    // default is always to sample the pixel.
    rayBuffer[SLOT_COLOUR][launchIndex.xy].w = AS_CAST;
}