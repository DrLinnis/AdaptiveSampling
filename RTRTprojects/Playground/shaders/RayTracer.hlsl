
// Define constants in code
#define PI 3.1415926538
#define PI2 6.283185307
#define PI_2 1.570796327
#define PI_4 0.7853981635
#define InvPi 0.318309886
#define InvPi2 0.159154943
#define EPSILON 0.00001

#define LAMBERTIAN 0
#define METAL 1

// PAYLOADS AND STRUCTS
struct RayPayload
{
    float3 colour;
    float3 normal;
    float3 position;
    
    float specular;
    int object;
    uint rayBudget;
    uint seed;
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
    uint Type;
    // 16 bytes: Material Colour and Type
    
    int DiffuseTextureIdx;
    int NormalTextureIdx;
    int SpecularTextureIdx;
    int MaskTextureIdx;
    // 16 bytes: Material Texture Indices
    
    float Roughness;
    float3 Emittance;
    // 16 bytes: Material properties
};

struct InstanceTransforms
{
    matrix<float, 4, 4> rotScale;
    matrix<float, 4, 4> normalRotScale;
    float4 translate;
    
    // 4x4x4 Bytes == 16 aligned
};

struct PerFrameData
{
    float4 atmopshere;
    
    float4 camOrigin;
    float4 camLookAt;
    float4 camLookUp;
    float2 camWinSize;
    
    uint accumulatedFrames;
    
    float _padding;
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
ConstantBuffer<PerFrameData> frame : register(b0);
ConstantBuffer<InstanceTransforms> instTrans : register(b1);

// Sampling
SamplerState trilinearFilter : register(s0);
SamplerState pointFilter : register(s1);



// Generic Helper functions
float mod(float x, float y)
{
    return x - y * floor(x / y);
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

float3 modelToWorldPosition(float3 pos)
{
    float3 result = mul(instTrans.rotScale, float4(pos, 0)).xyz;
    result += instTrans.translate.xyz;
    return result;
}

/*
    Random Generator 
    ( CODE FROM https://github.com/phgphg777/DXR-PathTracer )
*/
uint getNewSeed(uint param1, uint param2, uint numPermutation)
{
    uint s0 = 0;
    uint v0 = param1;
    uint v1 = param2;
	
    for (uint perm = 0; perm < numPermutation; perm++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }

    return v0;
}

float rnd(inout uint seed)
{
    seed = (1664525u * seed + 1013904223u);
    return ((float) (seed & 0x00FFFFFF) / (float) 0x01000000);
}

// --> https://math.stackexchange.com/questions/180418/calculate-rotation-matrix-to-align-vector-a-to-vector-b-in-3d
float3 applyRotationMappingZToN(in float3 N, in float3 v)	
{
    float s = (N.z >= 0.0f) ? 1.0f : -1.0f;
    v.z *= s;

    float3 h = float3(N.x, N.y, N.z + s);
    float k = dot(v, h) / (1.0f + abs(N.z));

    return k * h - v;
}

float3 sample_hemisphere_cos(inout uint seed)
{
    float3 sampleDir;

    float param1 = rnd(seed);
    float param2 = rnd(seed);

	// Uniformly sample disk.
    float r = sqrt(param1);
    float phi = PI2 * param2;
    sampleDir.x = r * cos(phi);
    sampleDir.y = r * sin(phi);

	// Project up to hemisphere.
    sampleDir.z = sqrt(max(0.0f, 1.0f - r * r));

    return sampleDir;
}


// INTERPOLATION helper functions
uint3 _getIndices(uint geometryIdx, uint triangleIndex)
{
    uint indexByteStartAddress = triangleIndex * 3 * 4;
    return indices[geometryIdx].Load3(indexByteStartAddress);
}

VertexAttributes GetVertexAttributes(uint geometryIndex, uint primitiveIndex, float3 barycentrics)
{
    uint3 triangleIndices = _getIndices(geometryIndex, primitiveIndex);
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
    
    
    v.position = modelToWorldPosition(v.position);
    
    for (int i = 0; i < 3; ++i)
    {
        vertPos[i] = modelToWorldPosition(vertPos[i]);
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

// TEXTURE and MATERIAL related helper functions
float4 TriSampleTex(Texture2D<float4> texArr[], uint index, float3 uvd)
{
    float depth = uvd[2];
    return texArr[index].SampleLevel(trilinearFilter, uvd.xy, floor(depth));
}


RayMaterialProp GetMaterialProp(uint geometryIndex)
{
    RayMaterialProp result;
    // From Geometry index to int32 location to byte location.
    uint indexByteStartAddress = geometryIndex * sizeof(RayMaterialProp);
    
    result.Diffuse = asfloat(GeometryMaterialMap.Load3(indexByteStartAddress));
    indexByteStartAddress += 3 * 4; // add 4 floats to byte counter
    result.Type = GeometryMaterialMap.Load(indexByteStartAddress);
    indexByteStartAddress += 4; // add one float
    
    result.DiffuseTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
    indexByteStartAddress += 4; // add one int
    result.NormalTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
    indexByteStartAddress += 4; // add one int
    result.SpecularTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
    indexByteStartAddress += 4; // add one int
    result.MaskTextureIdx = GeometryMaterialMap.Load(indexByteStartAddress);
    indexByteStartAddress += 4; // add one int
    
    result.Roughness = asfloat(GeometryMaterialMap.Load(indexByteStartAddress));
    indexByteStartAddress += 4; // add one float
    result.Emittance = asfloat(GeometryMaterialMap.Load3(indexByteStartAddress));
    indexByteStartAddress += 3 * 4; // add 4 floats to byte counter
    
    
    return result;
}

RayPayload TracePath(float3 origin, float3 direction, uint rayBudget, uint seed)
{
    RayPayload reflPayload;
    // return black if we are too far away
    if (!rayBudget)
    {
        reflPayload.colour = 0;
        reflPayload.normal = 0;
        return reflPayload;
    }
    reflPayload.rayBudget = rayBudget;
    reflPayload.seed = seed;
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 100000;
    
    TraceRay(gRtScene, 0 /*rayFlags*/, 0xFF /*No culling*/, 0 /* ray index*/,
        1 /* Multiplier for Contribution to hit group index*/, 0, ray, reflPayload
    );
    return reflPayload;
}


/*
    GENERATE COLOUR based on index helper functions
*/
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


/*
    PBR/BDRF helper functions
*/
// Trowbridge-Reitx GGX - Normal Distribution Functions
float _distributionGGX(float cosTheta, float alpha)
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
float _geometrySchlickGGX(float cosTheta, float k)
{
    float nom = cosTheta;
    float denom = max(cosTheta * (1.0 - k) + k, EPSILON);
	
    return nom / denom;
}
  
float _geometrySmith(float cosLight, float cosView, float k)
{
    float ggx1 = _geometrySchlickGGX(cosView, k);
    float ggx2 = _geometrySchlickGGX(cosLight, k);
	
    return ggx1 * ggx2;
}

//Fresnel function
float3 _fresnelSchlick(float3 F0, float cosOmega)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosOmega, 5.0);
}

float3 CookTorranceBRDF(float3 n, float3 v, float3 pos, float3 albedo, uint rayBudget, float seed,
                        float metalness, float roughness, float specular, uint type)
{
    // Path reflected ray
    float3 l = sample_hemisphere_cos(seed); // ASSUME every material is diffuse
    l = applyRotationMappingZToN(n, l);
        
    RayPayload Li = TracePath(pos, l, rayBudget - 1, seed);
    
    float3 F0 = float3(0.04, 0.04, 0.04); // Fdialectric
    F0 = lerp(F0, albedo, metalness);
    
    float k_direct = (roughness + 1) * (roughness + 1) / 8;
   
    // Build halfway vector
    float3 h = normalize(l + v);
    
    float cosTheta = max(dot(n, h), 0); // angle between normal and halfway vector
    float cosOmega = max(dot(v, h), 0); // angle between view and halfway vector
    
    float cosLight = max(dot(n, l), 0); // angle between light and normal
    float cosView = max(dot(n, v), 0); // angle between view and normal
    
    float D = _distributionGGX(cosTheta, roughness);
    
    float3 F = specular * _fresnelSchlick(F0, cosOmega);
    
    float G = _geometrySmith(cosLight, cosView, k_direct);
    
    // Calc diffuse comp (REFRACT)
    float3 kD = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metalness);
    
    float denom = max(4 * cosView * cosLight, EPSILON);
    
    return (kD * albedo + D * F * G / denom) * Li.colour * cosLight;
}


/* 
    START OF REYGEN SHADER 
*/
[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    uint bufferOffset = launchDim.x * launchIndex.y + launchIndex.x;
    uint seed = getNewSeed(bufferOffset, frame.accumulatedFrames, 8);
    
    float2 crd = float2(launchIndex.xy) + float2(rnd(seed), rnd(seed));
    float2 dims = float2(launchDim.xy);

    float2 d = ((crd / dims) * 2.f - 1.f); // converts [0, 1] to [-1, 1]
    float aspectRatio = dims.x / dims.y;

    RayDesc ray;
    ray.Origin = frame.camOrigin;
    
    float focus_dist = length(frame.camOrigin - frame.camLookAt);
    float3 w = normalize(frame.camOrigin - frame.camLookAt);
    float3 u = normalize(cross(frame.camLookUp.xyz, w));
    float3 v = cross(w, u);
    
    float3 horizontal = focus_dist * frame.camWinSize.x * u;
    float3 vertical = focus_dist * frame.camWinSize.y * v;
    

    ray.Direction = -normalize(frame.camLookAt + d.x * horizontal + d.y * vertical - frame.camOrigin.xyz);
    
    ray.TMin = 0;
    ray.TMax = 100000;

    RayPayload payload;
    payload.rayBudget = 5;
    payload.seed = seed;
    TraceRay(gRtScene, 
        0 /*rayFlags*/, 
        0xFF, 
        0 /* ray index*/,
        1 /* Multiplier for Contribution to hit group index*/,
        0,
        ray,
        payload
    );
    
    float depth = length(frame.camOrigin - payload.position);
    
    /*
        Rendered Image
        Normal
        WorldPos + Depth
        Objects
        Specular
    */
    
    float3 newRadiance = linearToSrgb(payload.colour);
    
    float3 avrRadiance; 
    if (frame.accumulatedFrames == 0)
        avrRadiance = newRadiance;
    else
        avrRadiance = lerp(gOutput[0][launchIndex.xy].xyz, newRadiance, 1.f / (frame.accumulatedFrames + 1.0f));
    
    gOutput[0][launchIndex.xy] = float4(avrRadiance, 1);
    gOutput[1][launchIndex.xy] = float4((payload.normal + 1) * 0.5, 1);
    gOutput[2][launchIndex.xy] = float4(payload.position, depth);
    gOutput[3][launchIndex.xy] = float4(GenColour(payload.object + 1), 1);
    gOutput[4][launchIndex.xy] = float4(payload.specular, payload.specular, payload.specular, 1);

}

/*
    STANDARD ray shader
*/
[shader("closesthit")] 
void standardChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    float3 L = normalize(float3(0.5, 2.5, -0.5));
    
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
    
    float finalSpecular = 0;
    int finalObject = 0;
    
    // Transparent pixel hit!
    if (tex_rgba.w == 0) 
    {
        // unfortunetly we must continue the ray and reduce depth or we will crash :(
        RayPayload contRay = TracePath(v.position, rayDirW, payload.rayBudget - 1, payload.seed);
        
        finalColour = contRay.colour;
        finalNormal = contRay.normal;
        finalPos = contRay.position;
        
        finalObject = contRay.object;
        finalSpecular = contRay.specular;
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
        
        // View Vector
        float3 V = -rayDirW;
        
        float specVal = 0;
        if (mat.SpecularTextureIdx >= 0)
        {
            specVal = TriSampleTex(specularTex, mat.SpecularTextureIdx, v.texCoord).w;
        }
        
        
        float3 BRDF = CookTorranceBRDF(normal, V, v.position, albedo,
                payload.rayBudget, payload.seed, 0, mat.Roughness, specVal, mat.Type);
        
        finalColour = BRDF;
        finalNormal = normal;
        finalPos = v.position;
        
        finalObject = GeometryIndex();
        finalSpecular = specVal;
        
    }
    
    payload.colour = mat.Emittance + finalColour;
    payload.normal = finalNormal;
    payload.position = finalPos;
    
    payload.specular = finalSpecular;
    payload.object = finalObject;
}

[shader("miss")]
void standardMiss(inout RayPayload payload)
{
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    // sky normal, depth, and colour
    payload.colour = float3(.529, .808, .922);
    payload.normal = 0.5;
    payload.position = rayOriginW + 100000 * rayDirW;
    
    payload.specular = 0;
    payload.object = -1;

}
