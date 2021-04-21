
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

struct perFrame
{
    int iteration;
};

ConstantBuffer<DenoiserFilterData> filterData : register(b0);

ConstantBuffer<perFrame> data : register(b1);
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

#define AS_EMPTY 0
#define AS_CAST 1
#define AS_CASTED 2
#define AS_INTERPOLATED 3

// Shader toy inspired temporal reprojection := https://www.shadertoy.com/view/ldtGWl

/* 
    SUMMARY:= Schedules 
*/

struct Square
{
    int2 one;
    int2 two;
    int2 three;
    int2 four;
    
    float4 barycentrics;
};

struct Triangle
{
    int2 one;
    int2 two;
    int2 three;
    
    float3 barycentrics;
};

// source: https://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates
float3 calcBaryCentrics(Triangle tri, int2 p)
{

    int2 a = tri.one, b = tri.two, c = tri.three;
    float2 v0 = b - a, v1 = c - a, v2 = p - a;
    
    float d00 = dot(v0, v0);
    float d01 = dot(v0, v1);
    float d11 = dot(v1, v1);
    float d20 = dot(v2, v0);
    float d21 = dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0f - v - w;
    
    return float3(u, v, w);
}

bool shootNextRay(int2 pos, int tileSize)
{
    // check if corner or centre of tileSize grid sytem.
    return ((pos.x % tileSize) == 0 && (pos.y % tileSize) == 0) ||
        (pos.x % tileSize == (tileSize >> 1) && pos.y % tileSize == (tileSize >> 1));

}

// itr := { 1, 2, 3, 4 }
// out := { 3, 5, 9, 17 }
int calcWidth(int widthIndex)
{
    int result = 3;
    for (int i = 1; i < widthIndex; ++i)
        result += (1 << i);
    return result;
}

// for side = 17
// itr := {0, 1, 2, 3, 4}
// adj := {17, 17, 9, 5, 3 }

// for side = 9
// itr := {0, 1, 2, 3}
// adj := {9, 9, 5, 3}
int calcAdjustedSide(int side, int itr)
{
    int result = side;
    for (int i = 1; i < itr; ++i)
        result = ceil(result / 2.0);
    return result;
}

Triangle buildTriangle(int2 pos, int side)
{
    Triangle result;
    int2 upperLeft = (side - 1) * floor(pos / (side - 1));
    int2 lowerRight = upperLeft + int2(side - 1, side - 1);
    int2 lowerLeft = int2(upperLeft.x, lowerRight.y);
    int2 upperRight = int2(lowerRight.x, upperLeft.y);
    int2 centre = (upperLeft + lowerRight) / 2;
    
    float2 dirCentrePos = normalize(pos - centre); // end - start
    int2 quadrant = int2(dirCentrePos.x >= 0 ? 1 : -1, dirCentrePos.y > 0 ? 1 : -1);
    
    // corner A. 
    int2 corner = centre + quadrant * ((side >> 1));
    
    float2 dirCornerCentre = normalize(centre - corner); // end - start
    
    // [-PI, PI]
    float theta = atan2(dirCentrePos.y, dirCentrePos.x);
    
    float2 reflNorm;
    if (abs(theta) <= PI_4)
        reflNorm = float2(1, 0);
    else if (theta > -3 * PI_4 && theta < PI_4)
        reflNorm = float2(0, -1);
    else if (theta < 3 * PI_4 && theta > PI_4)
        reflNorm = float2(0, 1);
    else
        reflNorm = float2(-1, 0);
    
    float2 dirCentreReflPos = reflect(-dirCentrePos, reflNorm);
    int2 reflQuadrant = int2(dirCentreReflPos.x > 0 ? 1 : -1, dirCentreReflPos.y >= 0 ? 1 : -1);
    int2 cornerRefl = centre + reflQuadrant * ((side >> 1));
    
    result.one = corner;
    result.two = centre;
    result.three = cornerRefl;
    
    result.barycentrics = calcBaryCentrics(result, pos);
    return result;
}

bool tryInterpolateFromSquare(int2 pos, Square quad)
{
    int2 pUp = quad.one;
    int2 pDown = quad.two;
    int2 pLeft = quad.three;
    int2 pRight = quad.four;
    
    float4 up = rayBuffer[SLOT_COLOUR][pUp];
    float4 down = rayBuffer[SLOT_COLOUR][pDown];
    float4 left = rayBuffer[SLOT_COLOUR][pLeft];
    float4 right = rayBuffer[SLOT_COLOUR][pRight];
    
    // Check if all are sampled.
    if (up.w > AS_CAST && down.w > AS_CAST && left.w > AS_CAST && right.w > AS_CAST)
    {
        float4 intColour = up * quad.barycentrics.x + down * quad.barycentrics.y 
            + left * quad.barycentrics.z + right * quad.barycentrics.w;
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
                    float3 intNorm = normalize(up.xyz * quad.barycentrics.x + down.xyz * quad.barycentrics.y 
                                        + left.xyz * quad.barycentrics.z + right.xyz * quad.barycentrics.w);
                    up = rayBuffer[SLOT_POS_DEPTH][pUp];
                    down = rayBuffer[SLOT_POS_DEPTH][pDown];
                    left = rayBuffer[SLOT_POS_DEPTH][pLeft];
                    right = rayBuffer[SLOT_POS_DEPTH][pRight];
                    
                    // check world position is within limit
                    if (length(up.xyz - down.xyz) < 2 * filterData.as_posDiffLimit &&
                        length(left.xyz - right.xyz) < 2 * filterData.as_posDiffLimit &&
                        length(up.xyz - left.xyz) < 2 * filterData.as_posDiffLimit)
                    {
                        float4 intPosition = up * quad.barycentrics.x + down * quad.barycentrics.y 
                                    + left * quad.barycentrics.z + right * quad.barycentrics.w;
                        
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

bool tryInterpolateFromTriangle(int2 pos, in Triangle tri)
{
    float4 one = rayBuffer[SLOT_COLOUR][tri.one];
    float4 two = rayBuffer[SLOT_COLOUR][tri.two];
    float4 three = rayBuffer[SLOT_COLOUR][tri.three];
    
    if (one.w > AS_CAST && two.w > AS_CAST && three.w > AS_CAST)
    {
        float4 inteColour = one * tri.barycentrics.x + two * tri.barycentrics.y + three * tri.barycentrics.z;
        
        // check if colour is ok
        if (length(inteColour.xyz - one.xyz) < filterData.as_colourLimit &&
            length(inteColour.xyz - two.xyz) < filterData.as_colourLimit &&
            length(inteColour.xyz - three.xyz) < filterData.as_colourLimit )
        {
            // check if same object (with mask)
            one = rayBuffer[SLOT_OBJECT_ID_MASK][tri.one];
            two = rayBuffer[SLOT_OBJECT_ID_MASK][tri.two];
            three = rayBuffer[SLOT_OBJECT_ID_MASK][tri.three];
        
            if (length(one - two) == 0 &&
                length(one - three) == 0)
            {
                float4 intObjMask = one;
                
                // from [0,1] to [-1,1]
                one.xyz = normalize(rayBuffer[SLOT_NORMALS][tri.one].xyz * 2 - 1);
                two.xyz = normalize(rayBuffer[SLOT_NORMALS][tri.two].xyz * 2 - 1);
                three.xyz = normalize(rayBuffer[SLOT_NORMALS][tri.three].xyz * 2 - 1);
                
                // check normals pointing in somewhat same direction
                if (dot(one.xyz, two.xyz) > filterData.as_normalDotLimit &&
                    dot(one.xyz, three.xyz) > filterData.as_normalDotLimit &&
                    dot(one.xyz, two.xyz) > filterData.as_normalDotLimit)
                {
                    float3 intNorm = normalize(one.xyz * tri.barycentrics.x + two.xyz * tri.barycentrics.y + three.xyz * tri.barycentrics.z);
                    
                    // positions
                    one = rayBuffer[SLOT_POS_DEPTH][tri.one];
                    two = rayBuffer[SLOT_POS_DEPTH][tri.two];
                    three = rayBuffer[SLOT_POS_DEPTH][tri.three];
                    
                    // check world position is within limit
                    if (length(one.xyz - two.xyz) < length(tri.one - pos) * filterData.as_posDiffLimit &&
                        length(one.xyz - three.xyz) < length(tri.two - pos) * filterData.as_posDiffLimit &&
                        length(two.xyz - three.xyz) < length(tri.three - pos) * filterData.as_posDiffLimit)
                    {
                        float4 intPosition = one * tri.barycentrics.x + two * tri.barycentrics.y + three * tri.barycentrics.z;
                        
                        // Write interpolated value
                        rayBuffer[SLOT_COLOUR][pos] = inteColour;
                        rayBuffer[SLOT_NORMALS][pos] = float4(intNorm * 0.5 + 0.5, 1); // [-1,1] to [0,1]
                        rayBuffer[SLOT_POS_DEPTH][pos] = intPosition;
                        rayBuffer[SLOT_OBJECT_ID_MASK][pos] = intObjMask;
                        
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

#define BLOCK_SIZE 16
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main(ComputeShaderInput IN)
{
    // cleans out those outside of the image
    if (IN.DispatchThreadID.x >= filterData.windowResolution.x || IN.DispatchThreadID.y >= filterData.windowResolution.y)
        return;
    
    uint2 launchIndex = IN.DispatchThreadID.xy;
    
    // cleans out those that have already been traced.
    if (rayBuffer[SLOT_COLOUR][launchIndex.xy].w != AS_EMPTY)
        return;
    
    int maxStep = filterData.gridSize;   

    uint gridSize = calcWidth(filterData.gridSize);
    uint tileSize = gridSize - 1;
    
    int itr = data.iteration;
    
    // initial step
    if (itr == 0)
    {
        uint2 upperTileLimit = tileSize * floor((filterData.windowResolution.xy - 1) / tileSize);

        // I outside of tiles
        if (launchIndex.x <= 0 || launchIndex.x >= upperTileLimit.x ||
                launchIndex.y <= 0 || launchIndex.y >= upperTileLimit.y) {
            rayBuffer[SLOT_COLOUR][launchIndex.xy].w = AS_CAST;
        }
        // if inside tiles and shoot ray
        else if (shootNextRay(launchIndex.xy, tileSize)) {
            rayBuffer[SLOT_COLOUR][launchIndex.xy].w = AS_CAST;
        }
            
    }
    else if (itr == maxStep) // final step.
    {
        // Try interpolate, if we cant, send all rays
        Square neighbour;
        neighbour.barycentrics = 0.25;
        neighbour.one = launchIndex.xy + int2(0, 1);
        neighbour.two = launchIndex.xy + int2(0, -1);
        neighbour.three = launchIndex.xy + int2(1, 0);
        neighbour.four = launchIndex.xy + int2(-1, 0);
        
        if (!tryInterpolateFromSquare(launchIndex.xy, neighbour))
            rayBuffer[SLOT_COLOUR][launchIndex.xy].w = AS_CAST;
    }
    else if (true) // smart cast between pixels. 
    {
        int adjustedGridSize = calcAdjustedSide(gridSize, itr);
        
        // Build Geometry and see if we can interpolate
        Triangle tri = buildTriangle(launchIndex.xy, adjustedGridSize);
        
        // see if we can interpolate the value.
        if (tryInterpolateFromTriangle(launchIndex.xy, tri))
            return;
        
        // if we cant interpolate, check if we should send the next one.
        if (shootNextRay(launchIndex.xy, calcAdjustedSide(gridSize, itr + 1) - 1))
            rayBuffer[SLOT_COLOUR][launchIndex.xy].w = AS_CAST;
    }
    
}