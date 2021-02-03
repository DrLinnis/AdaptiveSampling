
// SRV
RaytracingAccelerationStructure gRtScene : register(t0);

ByteAddressBuffer indices[] : register(t1, space0);
ByteAddressBuffer vertices[] : register(t1, space1);

// UAV
RWTexture2D<float4> gOutput : register(u0);

// CBV
cbuffer PerFrameCameraOrigin : register(b0)
{
    float4 cameraOrigin;
    float4 cameraLookAt;
    float4 cameraLookUp;
    float2 cameraWinSize;
    
    float2 _padding;
}


// PAYLOADS AND STRUCTS
struct RayPayload
{
    float3 color;
    uint depth;
};

struct ShadowPayLoad
{
    bool hit;
};

struct VertexAttributes
{
    float3 position;
    float3 normal;
    float3 tangent;
    float3 bitangent;
    float3 texCoord;
};

struct VertexPosNormalTex
{
    float3 position;
    float3 normal;
    float3 texCoord;
};


// Helper functions
uint3 GetIndices(uint geometryIdx, uint triangleIndex)
{
    uint indexByteStartAddress = triangleIndex * 3 * 4;
    return indices[geometryIdx].Load3(indexByteStartAddress);
}


float mod(float x, float y)
{
    return x - y * floor(x / y);
}


VertexPosNormalTex GetVertexAttributes(uint geometryIndex, uint primitiveIndex, float3 barycentrics)
{
    uint3 triangleIndices = GetIndices(geometryIndex, primitiveIndex);
    uint triangleVertexIndex = 0;
    float3 position = 0;
    float3 normal = 0;
    float3 texCoord = 0;
    
    for (int i = 0; i < 3; ++i)
    {
        // get byte address 
        triangleVertexIndex = triangleIndices[i] * sizeof(VertexAttributes);
        position += asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex)) * barycentrics[i];
        triangleVertexIndex += 3 * 4; // check the next float 3, NORMAL
        normal += asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex)) * barycentrics[i];
        triangleVertexIndex += 3 * 4; // check the next float 3, TANGENT
        triangleVertexIndex += 3 * 4; // check the next float 3, BITANGENT
        triangleVertexIndex += 3 * 4; // check the next float 3, TEXCOORD
        texCoord += asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex)) * barycentrics[i];
    }
    
    VertexPosNormalTex v = { position, normal, texCoord };
    return v;
}


float3 GetDummyColour(uint index)
{
    const float3 arr[25] =
    {
        float3(0.8308, 0.5853, 0.5497), float3(0.9172, 0.2858, 0.7572), float3(0.7537, 0.3804, 0.5678)
        , float3(0.0759, 0.0540, 0.5308), float3(0.7792, 0.9340, 0.1299), float3(0.5688, 0.4694, 0.0119)
        , float3(0.3371, 0.1622, 0.7943), float3(0.3112, 0.5285, 0.1656), float3(0.6020, 0.2630, 0.6541)
        , float3(0.6892, 0.7482, 0.4505), float3(0.0838, 0.2290, 0.9133), float3(0.1524, 0.8258, 0.5383)
        , float3(0.9961, 0.0782, 0.4427), float3(0.1067, 0.9619, 0.0046), float3(0.7749, 0.8173, 0.8687)
        , float3(0.0844, 0.3998, 0.2599), float3(0.8001, 0.4314, 0.9106), float3(0.1818, 0.2638, 0.1455)
        , float3(0.1361, 0.8693, 0.5797), float3(0.5499, 0.1450, 0.8530), float3(0.6221, 0.3510, 0.5132)
        , float3(0.4018, 0.0760, 0.2399), float3(0.1233, 0.1839, 0.2400), float3(0.4173, 0.0497, 0.9027)
        , float3(0.9448, 0.4909, 0.4893)
    };
    
    return arr[mod(index, 25)];

}


ShadowPayLoad CalcShadowRay(float3 position, float3 direction)
{
    RayDesc ray;
    ray.Origin = position;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 100000;
    ShadowPayLoad shadowPayload;
    TraceRay(gRtScene,
        0 /*rayFlags*/,
        0xFF,
        1 /* ray index*/,
        0,
        1,
        ray,
        shadowPayload
    );
    return shadowPayload;
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



/* START OF REYGEN SHADER */
[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 crd = float2(launchIndex.xy);
    float2 dims = float2(launchDim.xy);

    float2 d = ((crd / dims) * 2.f - 1.f); // converts [0, 1] to [-1, 1]
    float aspectRatio = dims.x / dims.y;

    RayDesc ray;
    ray.Origin = cameraOrigin;
    
    float focus_dist = length(cameraOrigin - cameraLookAt);
    float3 w = normalize(cameraOrigin - cameraLookAt);
    float3 u = normalize(cross(cameraLookUp.xyz, w));
    float3 v = cross(w, u);
    
    float3 horizontal = focus_dist * cameraWinSize.x * u;
    float3 vertical = focus_dist * cameraWinSize.y * v;
    

    ray.Direction = -normalize(cameraLookAt + d.x * horizontal + d.y * vertical - cameraOrigin.xyz);
    
    ray.TMin = 0;
    ray.TMax = 100000;

    RayPayload payload;
    payload.depth = 5;
    TraceRay(gRtScene, 
        0 /*rayFlags*/, 
        0xFF, 
        0 /* ray index*/,
        2 /* Multiplier for Contribution to hit group index*/,
        0,
        ray,
        payload
    );
    float3 col = linearToSrgb(payload.color);
    gOutput[launchIndex.xy] = float4(payload.color, 1);
}


// Shadow rays
[shader("closesthit")]
void shadowChs(inout ShadowPayLoad payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hit = true;
}

[shader("miss")]
void shadowMiss(inout ShadowPayLoad payload)
{
    payload.hit = false;
}



// Standard rays
[shader("closesthit")] 
void standardChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    // (w,u,v)
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    VertexPosNormalTex v = GetVertexAttributes(GeometryIndex(), PrimitiveIndex(), barycentrics);
    
    // Find the world-space hit position
    float3 posW = rayOriginW + hitT * rayDirW;

    float3 L = normalize(float3(0.5, 2.5, -0.5));;
    
    // Fire a shadow ray. The direction is hard-coded here, but can be fetched from a constant-buffer
    ShadowPayLoad shadowPayload = CalcShadowRay(posW, L);

    float3 reflectedColour = 1;
    
    if (payload.depth >= 2)
    {
        RayDesc ray;
        ray.Origin = posW;
        ray.Direction = reflect(rayDirW, v.normal);
        ray.TMin = 0.001;
        ray.TMax = 100000;

        RayPayload reflPayload;
        reflPayload.depth = payload.depth - 1;
        TraceRay(gRtScene,
            0 /*rayFlags*/,
            0xFF,
            0 /* ray index*/,
            2 /* Multiplier for Contribution to hit group index*/,
            0,
            ray,
            reflPayload
        );
        reflectedColour = reflPayload.color;
    }
    
    float shadowFactor = shadowPayload.hit ? 0.1 : 1.0;
    
    float3 diffuse = GetDummyColour(GeometryIndex()) + reflectedColour * 0.1;
    
    float kD = 3.0;
    float3 phongLightFactor = diffuse * kD * max(dot(v.normal, L), 0);
    
    payload.color = diffuse * phongLightFactor * shadowFactor;
}

[shader("miss")]
void standardMiss(inout RayPayload payload)
{
    payload.color = float3(.529,.808,.922);
}
