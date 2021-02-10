

// PAYLOADS AND STRUCTS
struct RayPayload
{
    float3 colour;
    float3 normal;
    float3 position;
    float3 albedo;
    
    float metalness;
    int object;
    
    uint bounces;
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

struct RayMaterialProp
{
    float3 Diffuse;
    float IndexOfReflection;
    int DiffuseTextureIdx;
    int NormalTextureIdx;
    int SpecularTextureIdx;
    int MaskTextureIdx;
};


// SRV
RaytracingAccelerationStructure gRtScene : register(t0);

ByteAddressBuffer indices[]     : register(t1, space0);
ByteAddressBuffer vertices[]    : register(t1, space1);

ByteAddressBuffer GeometryMaterialMap : register(t1, space2);

Texture2D<float4> diffuseTex[]  : register(t1, space3);
Texture2D<float4> normalsTex[]  : register(t1, space4);
Texture2D<float4> specularTex[] : register(t1, space5);
Texture2D<float4> maskTex[]     : register(t1, space6);

// UAV
RWTexture2D<float4> gOutput[] : register(u0);

// CBV
cbuffer PerFrameCameraOrigin : register(b0)
{
    float4 cameraOrigin;
    float4 cameraLookAt;
    float4 cameraLookUp;
    float2 cameraWinSize;
    
    float2 _padding;
}



// Helper functions
uint3 GetIndices(uint geometryIdx, uint triangleIndex)
{
    uint indexByteStartAddress = triangleIndex * 3 * 4;
    return indices[geometryIdx].Load3(indexByteStartAddress);
}


RayMaterialProp GetMaterialProp(uint geometryIndex)
{
    RayMaterialProp result;
    // From Geometry index to int32 location to byte location.
    uint indexByteStartAddress = geometryIndex * sizeof(RayMaterialProp);
    
        result.Diffuse = asfloat(GeometryMaterialMap.Load3(indexByteStartAddress));
        indexByteStartAddress += 3 * 4; // add 4 floats to byte counter
        result.IndexOfReflection = asfloat(GeometryMaterialMap.Load(indexByteStartAddress));
        indexByteStartAddress += 4; // add one float
        result.DiffuseTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
        indexByteStartAddress += 4; // add one int
        result.NormalTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
        indexByteStartAddress += 4; // add one int
        result.SpecularTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
        indexByteStartAddress += 4; // add one int
        result.MaskTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
    
    return result;
}


float mod(float x, float y)
{
    return x - y * floor(x / y);
}


VertexAttributes GetVertexAttributes(uint geometryIndex, uint primitiveIndex, float3 barycentrics)
{
    uint3 triangleIndices = GetIndices(geometryIndex, primitiveIndex);
    uint triangleVertexIndex = 0;
    
    VertexAttributes v;
    v.position = 0;
    v.normal = 0;
    v.texCoord = 0;
    v.tangent = 0;
    v.bitangent = 0;
    
    float3 vertPos[3];
    float3 texels[3];
    
    for (int i = 0; i < 3; ++i)
    {
        // get byte address 
        triangleVertexIndex = triangleIndices[i] * sizeof(VertexAttributes);
        
        // position
        vertPos[i] = asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex));
        v.position += vertPos[i] * barycentrics[i];
        triangleVertexIndex += 3 * 4; // check the next float 3
        // normal
        v.normal += asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex)) * barycentrics[i];
        triangleVertexIndex += 3 * 4; // check the next float 3
        // tangent
        v.tangent += asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex)) * barycentrics[i];
        triangleVertexIndex += 3 * 4; // check the next float 3
        // bitangent
        v.bitangent += asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex)) * barycentrics[i];
        triangleVertexIndex += 3 * 4; // check the next float 3
        // tex coordinate
        texels[i] = asfloat(vertices[GeometryIndex()].Load3(triangleVertexIndex));
        v.texCoord += texels[i] * barycentrics[i];
    }
    
    // DEPTH CALCULATION BASED ON:
    // https://media.contentapi.ea.com/content/dam/ea/seed/presentations/2019-ray-tracing-gems-chapter-20-akenine-moller-et-al.pdf
    
    
    float tA = abs((texels[1].x - texels[0].x) * (texels[2].y - texels[0].y) -
            (texels[2].x - texels[0].x) * (texels[1].y - texels[0].y)
        );
    
    // world space version
    float pA = length(cross(vertPos[1] - vertPos[0], vertPos[2] - vertPos[0]));
    
    v.texCoord[2] = 0.5 * log2(tA / pA);
    
    // normalize direction vectors
    v.normal = dot(v.normal, v.normal) == 0 ? float3(0, 0, 0) : normalize(v.normal);
    v.tangent = dot(v.tangent, v.tangent) == 0 ? float3(0, 0, 0) : normalize(v.tangent);
    v.bitangent = dot(v.bitangent, v.bitangent) == 0 ? float3(0, 0, 0) : normalize(v.bitangent);
    
    return v;
}



SamplerState trilinearFilter : register(s0);
SamplerState pointFilter : register(s1);

float4 TriSampleTex(Texture2D<float4> texArr[], uint index, float3 uvd)
{
    float depth = uvd[2];
    return texArr[index].SampleLevel(trilinearFilter, uvd.xy, floor(depth));
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


RayPayload TracePath(float3 origin, float3 direction, uint bounce)
{
    RayPayload reflPayload;
    // return black if we are too far away
    if (!bounce)
    {
        reflPayload.colour = 0;
        reflPayload.normal = 0;
        return reflPayload;
    }
    reflPayload.bounces = bounce;
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 100000;
    
    TraceRay(gRtScene, 0 /*rayFlags*/, 0xFF /*No culling*/, 0 /* ray index*/,
        2 /* Multiplier for Contribution to hit group index*/, 0, ray, reflPayload
    );
    return reflPayload;
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


/* Following three functions generates pattern colour from single index*/
int3 _getPattern( int index) {
    int n = (int) pow(index, 1 / 3); // cubic root
    index -= (n*n*n);
    int3 p = { n, n, n };
    if (index == 0) {
        return p;
    }
    index--;
    int v = index % 3;
    index = index / 3;
    if (index < n) {
        p[v] = index % n;
        return p;
    }
    index -= n;
    p[v] = index / n;
    p[++v % 3] = index % n;
    return p;
}

int _getElement(int index) {
    int value = index - 1;
    int v = 0;
    for (int i = 0; i < 8; i++)
    {
        v = v | (value & 1);
        v <<= 1;
        value >>= 1;
    }
    v >>= 1;
    return v & 0xFF;
}

float3 GenColour(int id)
{
    int3 pattern = _getPattern(id);
    
    float3 rgb = 0;
    // Convert from integer [0,255] to float [0,1].
    rgb.r = (float) _getElement(pattern[0]) / 255; 
    rgb.g = (float) _getElement(pattern[1]) / 255;
    rgb.b = (float) _getElement(pattern[2]) / 255;
    
    return rgb;
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
    payload.bounces = 5;
    TraceRay(gRtScene, 
        0 /*rayFlags*/, 
        0xFF, 
        0 /* ray index*/,
        2 /* Multiplier for Contribution to hit group index*/,
        0,
        ray,
        payload
    );
    
    //float3 col = linearToSrgb(payload.colour);
    float3 depth = length(cameraOrigin - payload.position) / 10000;
    float3 metal = payload.metalness;
    
    gOutput[0][launchIndex.xy] = float4(linearToSrgb(payload.colour), 1);
    gOutput[1][launchIndex.xy] = float4(payload.albedo, 1);
    gOutput[2][launchIndex.xy] = float4((payload.normal + 1) * 0.5, 1);
    gOutput[3][launchIndex.xy] = float4(depth, 1);
    gOutput[4][launchIndex.xy] = float4(payload.position / 1000, 1);
    gOutput[5][launchIndex.xy] = float4(GenColour(payload.object + 1), 1);
    gOutput[6][launchIndex.xy] = float4(metal, 1);

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
    
    
    float3 L = normalize(float3(0.5, 2.5, -0.5));;
    
    // (w,u,v)
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    RayMaterialProp mat = GetMaterialProp(GeometryIndex());
    VertexAttributes v = GetVertexAttributes(GeometryIndex(), PrimitiveIndex(), barycentrics);
    
    // calculate depth
    uint width, height, nbrLevels;
    // assumes every texture follows the same size and that each object at least has diffuse texture!
    diffuseTex[mat.DiffuseTextureIdx].GetDimensions(0, width, height, nbrLevels);
    
    // Adjust for lack of w*h in tA term.
    v.texCoord[2] += 0.5 * log2(width * height); 
    
    float3 finalColour = 0;
    float3 finalNormal = 0;
    float3 finalPos = 0;
    float3 finalAlbedo = 0;
    
    float finalMetalness = 0;
    int finalObject = 0;
    
    // Find the world-space hit position
    float3 posW = rayOriginW + hitT * rayDirW;
    
    // Transparent pixel hit!
    if (mat.MaskTextureIdx >= 0 && maskTex[mat.MaskTextureIdx].SampleLevel(pointFilter, v.texCoord.xy, 0).x != 1 ) 
    {
        // unfortunetly we must continue the ray and reduce depth or we will crash :(
        RayPayload continouedRay = TracePath(posW, rayDirW, payload.bounces - 1);
        finalColour = continouedRay.colour;
        finalNormal = continouedRay.normal;
        finalPos = continouedRay.position;
        finalAlbedo = continouedRay.albedo;
        finalObject = continouedRay.object;
        finalMetalness = continouedRay.metalness;

    }
    else  // We have hit a normal object/pixel
    {
        
        float3 materialColour = TriSampleTex(diffuseTex, mat.DiffuseTextureIdx, v.texCoord).rgb;
        
        // Fire a shadow ray. The direction is hard-coded here, but can be fetched from a constant-buffer
        ShadowPayLoad shadowPayload = CalcShadowRay(v.position, L);

        float3 normal = v.normal;
        if (mat.NormalTextureIdx >= 0)
        {
            // from [0,1] to [-1, 1]
            normal = TriSampleTex(normalsTex, mat.NormalTextureIdx, v.texCoord).rgb * 2 - 1;
            float3x3 TBN = float3x3(v.tangent, v.bitangent, v.normal);
            normal = mul(TBN, normal);
        }
        float spec = 0;
        if (mat.SpecularTextureIdx >= 0)
        {
            float4 sampledValue = TriSampleTex(specularTex, mat.SpecularTextureIdx, v.texCoord);
            spec = sampledValue.r;
        }
        
        // Path reflected ray
        RayPayload reflectedRay = TracePath(posW, reflect(rayDirW, normal), payload.bounces - 1);
    
        float3 reflectedColour = reflectedRay.colour;
    
        float shadowFactor = shadowPayload.hit ? 0.5 : 1.0;
        
        finalColour = materialColour * shadowFactor + reflectedColour * 0.1;
        finalNormal = normal;
        finalPos = posW;
        finalAlbedo = materialColour;
        
        finalObject = GeometryIndex();
        finalMetalness = spec;
    }
    
    payload.colour = finalColour;
    payload.normal = finalNormal;
    payload.position = finalPos;
    payload.albedo = finalAlbedo;
    
    payload.metalness = finalMetalness;
    payload.object = finalObject;
}

[shader("miss")]
void standardMiss(inout RayPayload payload)
{
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    // sky normal, depth, and colour
    payload.colour = float3(.529, .808, .922); 
    payload.albedo = float3(.529, .808, .922);
    payload.normal = 0.5;
    payload.position = rayOriginW + 100000 * rayDirW;
    
    payload.metalness = 0;
    payload.object = -1;

}
