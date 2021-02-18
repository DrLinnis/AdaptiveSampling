
// Define constants in code
#define PI 3.1415926538
#define EPSILON 0.00001

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
    bool hitObject;
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
    float IdxOfRef;
    int DiffuseTextureIdx;
    int NormalTextureIdx;
    int SpecularTextureIdx;
    int MaskTextureIdx;
};

struct InstanceTransforms
{
    matrix<float, 4, 4> rotScale;
    matrix<float, 4, 4> normalRotScale;
    float4 translate;
    
    // 4x4x4 Bytes == 16 aligned
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

ConstantBuffer<InstanceTransforms> instTrans : register(b1);


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
        result.IdxOfRef = asfloat(GeometryMaterialMap.Load(indexByteStartAddress));
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
    
    
    v.position = mul(instTrans.rotScale, float4(v.position, 0)).xyz;
    
    v.position += instTrans.translate.xyz;
    
    for (int i = 0; i < 3; ++i)
    {
        vertPos[i] = mul(instTrans.rotScale, float4(vertPos[i], 0)).xyz;
        vertPos[i] += instTrans.translate.xyz;
    }
    
    // world space version
    float pA = length(cross(vertPos[1] - vertPos[0], vertPos[2] - vertPos[0]));
    
    v.texCoord[2] = 0.5 * log2(tA / pA);
  
    
    // normalize direction vectors
    v.normal = normalize(v.normal);
    v.tangent = normalize(-v.tangent);
    v.bitangent = normalize(v.bitangent);
    
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
    payload.hitObject = true;
}

[shader("miss")]
void shadowMiss(inout ShadowPayLoad payload)
{
    payload.hitObject = false;
}

// Trowbridge-Reitx GGX - Normal Distribution Functions
float DistributionGGX(float cosTheta, float alpha)
{
    float a2 = alpha * alpha;
    float NdotH = cosTheta;
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    
    return nom / denom;
}

// Geometry functions
float GeometrySchlickGGX(float cosTheta, float k)
{
    float nom = cosTheta;
    float denom = max(cosTheta * (1.0 - k) + k, EPSILON);
	
    return nom / denom;
}
  
float GeometrySmith(float cosLight, float cosView, float k)
{
    float ggx1 = GeometrySchlickGGX(cosView, k);
    float ggx2 = GeometrySchlickGGX(cosLight, k);
	
    return ggx1 * ggx2;
}

//Fresnel function
float3 FresnelSchlick(float3 F0, float cosOmega)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosOmega, 5.0);
}

float3 CookTorranceBRDF(float3 l, float3 n, float3 v, float3 albedo,
                        float metalness, float IdxOfRef, float roughness)
{
    float3 F0 = float3(0.04, 0.04, 0.04); // Fdialectric
    F0 = lerp(F0, albedo, metalness);
    
    
    float alpha = 0.5;
    float k_direct = (alpha + 1) * (alpha + 1) / 8;
   
    // Build halfway vector
    float3 h = normalize(l + v); 
    
    float cosTheta = max(dot(n, h), 0); // angle between normal and halfway vector
    float cosOmega = max(dot(v, h), 0); // angle between view and halfway vector
    
    float cosLight = max(dot(n, l), 0); // angle between light and normal
    float cosView = max(dot(n, v), 0); // angle between view and normal
    
    float D = DistributionGGX(cosTheta, alpha);
    
    float3 F = FresnelSchlick(F0, cosOmega); 
    
    float G = GeometrySmith(cosLight, cosView, k_direct);    
    
    // Calc diffuse comp (REFRACT)
    float3 kD = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metalness);
    
    float denom = max(4 * cosView * cosLight, EPSILON);
    
    return kD * albedo / PI + D * F * G / denom;
    //return D;
}

// Standard rays
[shader("closesthit")] 
void standardChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    const float p = 1 / (2 * PI);
    
    float3 L = normalize(float3(0.5, 2.5, -0.5));
    //float3 L = normalize(float3(0.5, 1, 0.5));;
    
    // (w,u,v)
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    RayMaterialProp mat = GetMaterialProp(GeometryIndex());
    VertexAttributes v = GetVertexAttributes(GeometryIndex(), PrimitiveIndex(), barycentrics);
    
    
    // Alpha can be set by either mask or diffuse texture
    
    float4 tex_rgba = float4(mat.Diffuse, 1);
    
    
    if (mat.MaskTextureIdx >= 0) {
        float alpha = maskTex[mat.MaskTextureIdx].SampleLevel(pointFilter, v.texCoord.xy, 0).x;
        tex_rgba.w *= alpha;
    }
    
    if (mat.DiffuseTextureIdx >= 0)
    {
        // Point filter sample from diffuse texture
        float alpha = diffuseTex[mat.DiffuseTextureIdx].SampleLevel(pointFilter, v.texCoord.xy, 0).w;
        tex_rgba.w *= alpha;
        
        // calculate depth
        uint width, height, nbrLevels;
        diffuseTex[mat.DiffuseTextureIdx].GetDimensions(0, width, height, nbrLevels);
    
        // Adjust for lack of w*h in tA term.
        v.texCoord[2] += 0.5 * log2(width * height);
        
        tex_rgba.rgb *= TriSampleTex(diffuseTex, mat.DiffuseTextureIdx, v.texCoord).rgb;

    }
    
    float3 finalColour = 0;
    float3 finalNormal = 0;
    float3 finalPos = 0;
    float3 finalAlbedo = 0;
    
    float finalMetalness = 0;
    int finalObject = 0;
    
    
    // Transparent pixel hit!
    if (tex_rgba.w == 0) 
    {
        // unfortunetly we must continue the ray and reduce depth or we will crash :(
        RayPayload continouedRay = TracePath(v.position, rayDirW, payload.bounces - 1);
        finalColour = continouedRay.colour;
        finalNormal = continouedRay.normal;
        finalPos = continouedRay.position;
        finalAlbedo = continouedRay.albedo;
        finalObject = continouedRay.object;
        finalMetalness = continouedRay.metalness;

    }
    else  // We have hit a normal object/pixel
    {
        // sample diffuse texture value
        float3 albedo = tex_rgba.rgb;
        
        // Build normal
        float3 normal = v.normal;
        if (mat.NormalTextureIdx >= 0)
        {
            // from [0,1] to [-1, 1]
            normal = TriSampleTex(normalsTex, mat.NormalTextureIdx, v.texCoord).rgb * 2 - 1;
            float3x3 TBN = float3x3(v.tangent, v.bitangent, v.normal);
            normal = mul(normal, TBN);
        }
        normal = normalize(mul(instTrans.normalRotScale, float4(normal, 0)).xyz);
        
        // Reflection vector
        float3 R = reflect(rayDirW, normal);
        // View Vector
        float3 V = -rayDirW;
        
        float specIntensity = 0;
        if (mat.SpecularTextureIdx >= 0)
        {
            specIntensity = TriSampleTex(specularTex, mat.SpecularTextureIdx, v.texCoord).r;
        }
        
        // Path reflected ray
        RayPayload reflectedRay = TracePath(v.position, R, payload.bounces - 1);
        
        
        float3 L0 = 0.5 * CookTorranceBRDF(R, normal, V, albedo, 0.3, mat.IdxOfRef, specIntensity) * reflectedRay.colour * dot(normal, R);
        
        //float3 L0 = CookTorranceBRDF(L, normal, V, albedo, 0.3, mat.IdxOfRef, specIntensity) * float3(.529, .808, .922) * dot(normal, L);
        
        //float3 L0 = 0;
        
        // Fire a shadow ray. 
        ShadowPayLoad shadowPayload = CalcShadowRay(v.position, L);
        
        if (!shadowPayload.hitObject)
            L0 = L0 + 0.5 * CookTorranceBRDF(L, normal, V, albedo, 0.3, mat.IdxOfRef, specIntensity) * float3(.529, .808, .922) * dot(normal, L);

        
        //finalColour = CookTorranceBRDF(L, normal, V, albedo, 0.5, mat.IdxOfRef, specIntensity) * dot(normal, L);
        finalColour = L0;
        
        
        finalNormal = normal;
        finalPos = v.position;
        finalAlbedo = albedo;
        
        finalObject = GeometryIndex();
        finalMetalness = specIntensity;
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
