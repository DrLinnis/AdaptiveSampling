
// Define constants in code
#define PI 3.1415926538
#define PI2 6.283185307
#define PI_2 1.570796327
#define PI_4 0.7853981635
#define InvPi 0.318309886
#define InvPi2 0.159154943
#define EPSILON 0.00001

#define T_HIT_MIN 0.0001

#define LAMBERTIAN  0
#define METALIC       1
#define PLASTIC     2
#define DIALECTIC   3

#define RAY_PRIMARY 0
#define RAY_SECONDARY 1

#define RAY_LIGHT 2

// PAYLOADS AND STRUCTS
struct RayPayload
{
    // Vectors
    float3 colour;
    float3 normal;
    float3 reflectDir;
    float3 position;
    float3 radiance;
    
    // For denoising
    float prob;
    int object;
    
    // Ray Properties
    float mask;
    uint depth;
    uint rayMode;
    uint seed;
    bool mediumIoR;
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
    
    float Reflectivity;
    float3 Emittance;
    // 16 bytes: Material properties
    
    float IndexOfRefraction;
    float Roughness;
    float2 _padding;
};


struct MaterialInfoBDRF
{
    // Basics
    float3 view;
    float3 normal;
    float3 pos;
    
    // Material Type
    uint type;
    uint depth;
    
    // Material props
    float3 colour;
    float reflectivity;
    float roughness;
    float ior; //index of refraction
};

struct InstanceTransforms
{
    row_major matrix<float, 3, 4> modelToWorld;
    row_major matrix<float, 3, 4> normalModelToWorld;
    
    float lodScaler;
};

struct PerFrameData
{
    float4 atmosphere;
    
    row_major matrix<float, 3, 4> cameraPixelToWorld;
    
    uint exponentSamplesPerPixel;
    uint nbrBouncesPerPath;
    uint cpuGeneratedSeed;
};

struct ConstantData
{
    uint nbrActiveLights;
    
    uint hasSkybox;
    
    float4 lightPositions[10];
};

// SRV
RaytracingAccelerationStructure gRtScene : register(t0);

TextureCube<float4> skyboxDiffuse : register(t1, space0);
TextureCube<float4> skyboxRadiance : register(t1, space1);

ByteAddressBuffer indices[]     : register(t2, space0);
ByteAddressBuffer vertices[]    : register(t2, space1);

ByteAddressBuffer GeometryMaterialMap : register(t2, space2);


Texture2D<float4> diffuseTex[]  : register(t2, space3);
Texture2D<float4> normalsTex[]  : register(t2, space4);
Texture2D<float4> specularTex[] : register(t2, space5);
Texture2D<float4> maskTex[]     : register(t2, space6);


// UAV
RWTexture2D<float4> gOutput[] : register(u0);

// CBV
ConstantBuffer<PerFrameData> frame : register(b0);
ConstantBuffer<ConstantData> globals : register(b1);
ConstantBuffer<InstanceTransforms> instTrans : register(b2);

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
    return mul(instTrans.modelToWorld, float4(pos, 1)).xyz;
}


MaterialInfoBDRF MaterialInfo(in float3 view, in float3 normal, in float3 pos, in uint matType,
        in float3 colour, in float reflectivity, in float roughness, in float ior, in uint depth)
{
    MaterialInfoBDRF result;
    
    result.view = view;
    result.normal = normal;
    result.pos = pos;
    result.type = matType;
    result.colour = colour;
    result.roughness = roughness;
    result.reflectivity = reflectivity;
    result.ior = ior;
    result.depth = depth;
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

float3 sample_hemisphere_TrowbridgeReitzCos(in float alpha2, inout uint seed)
{
    float3 sampleDir;

    float u = rnd(seed);
    float v = rnd(seed);

    float tan2theta = alpha2 * (u / (1 - u));
    float cos2theta = 1 / (1 + tan2theta);
    float sinTheta = sqrt(1 - cos2theta);
    float phi = PI2 * v;

    sampleDir.x = sinTheta * cos(phi);
    sampleDir.y = sinTheta * sin(phi);
    sampleDir.z = sqrt(cos2theta);

    return sampleDir;
}

// My functions
float3 SampleNearestLightDirection(float3 pos, float3 normal)
{
    float bestDist = pow(1 / EPSILON, 2);
    float3 result = normal;
    
    for (int i = 0; i < globals.nbrActiveLights; ++i)
    {
        float3 lightPos = globals.lightPositions[i].xyz;
        lightPos = modelToWorldPosition(lightPos);
        float3 dir = lightPos - pos;
        float distSqred = dot(dir, dir);
        if (distSqred < bestDist)
        {
            bestDist = distSqred;
            result = dir;
        }
    }
    
    //result = mul(instTrans.normalModelToWorld, float4(result, 0)).xyz;
    
    return normalize(result);
}

// INTERPOLATION helper functions
uint3 _getIndices(uint geometryIdx, uint triangleIndex)
{
    uint indexByteStartAddress = triangleIndex * 3 * 4;
    return indices[geometryIdx].Load3(indexByteStartAddress);
}

VertexAttributes GetVertexAttributes(uint geometryIndex, uint primitiveIndex, float3 barycentrics)
{
    int i;
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
    
    for (i = 0; i < 3; ++i)
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
    
    for (i = 0; i < 3; ++i)
    {
        vertPos[i] = modelToWorldPosition(vertPos[i]);
    }
    
    // world space version
    float pA = instTrans.lodScaler * length(cross(vertPos[1] - vertPos[0], vertPos[2] - vertPos[0]));
    
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
    
    result.Reflectivity = asfloat(GeometryMaterialMap.Load(indexByteStartAddress));
    indexByteStartAddress += 4; // add one float
    result.Emittance = asfloat(GeometryMaterialMap.Load3(indexByteStartAddress));
    indexByteStartAddress += 3 * 4; // add 4 floats to byte counter
    
    result.IndexOfRefraction = asfloat(GeometryMaterialMap.Load(indexByteStartAddress));
    indexByteStartAddress += 4; // add one float
    result.Roughness = asfloat(GeometryMaterialMap.Load(indexByteStartAddress));
    indexByteStartAddress += 4; // add one float
    
    return result;
}


// TRACE helper functions
RayPayload TraceFullPath(float3 origin, float3 direction, uint seed)
{
    RayPayload result;
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = 100000;
    
    float3 radiance = 0;
    float3 colour = 1.0f;
    RayPayload currRay;
    currRay.seed = seed;
    currRay.rayMode = RAY_PRIMARY;
    currRay.depth = frame.nbrBouncesPerPath;
    currRay.mediumIoR = 1.0f;
    
    uint prevType;
    while (currRay.depth > 0)
    {
        currRay.radiance = 0;
        currRay.colour = 0;
        prevType = currRay.rayMode;
        
        TraceRay( gRtScene, 0, 0xFF, 0, 1, 0, ray, currRay );
        
        radiance += colour * currRay.radiance;
        colour *= currRay.colour;
        
        // Store the first Primary Ray normal/specular/object/position
        if (currRay.rayMode == RAY_SECONDARY && prevType == RAY_PRIMARY)
        {
            result.normal = currRay.normal;
            result.mask = currRay.mask;
            result.object = currRay.object;
            result.position = currRay.position;
            
            if (length(currRay.radiance) > 0 && currRay.object == -1)
                radiance = currRay.colour;
        }
        
        ray.Origin = currRay.position;
        ray.Direction = currRay.reflectDir;
        
        if (length(currRay.radiance) > 0 || length(colour) < 0.05)
        {
            break;
        }
        
        // Only shoot from 0 on camera.
        ray.TMin = T_HIT_MIN;
    }
    
    result.colour = radiance;
    result.seed = currRay.seed;
    
    return result;
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

float _TrowbridgeReitz(in float cos2, in float alpha2)
{
    float x = alpha2 + (1 - cos2) / cos2;
    return alpha2 / (PI * cos2 * cos2 * x * x);
}

float _Smith_TrowbridgeReitz(in float3 wi, in float3 wo, in float3 wm, in float3 wn, in float alpha2)
{
    if (dot(wo, wm) < 0 || dot(wi, wm) < 0)
        return 0.0f;

    float cos2 = dot(wn, wo);
    cos2 *= cos2;
    float lambda1 = 0.5 * (-1 + sqrt(1 + alpha2 * (1 - cos2) / cos2));
    cos2 = dot(wn, wi);
    cos2 *= cos2;
    float lambda2 = 0.5 * (-1 + sqrt(1 + alpha2 * (1 - cos2) / cos2));
    return 1 / (1 + lambda1 + lambda2);
}


// Cook Torrance BRDF
void sampleBRDF(out float3 sampleDir, out float sampleProb, out float3 brdfCos,
                in MaterialInfoBDRF mat, in bool sampleLight, inout uint seed)
{
    float3 brdfEval;
    
    // Reflection dir, view vector, normal, half-vector
    float3 R, V = mat.view, N = mat.normal, H;
    
    float cosNH, cosVH, cosNR, cosNV, cosRH;
    
    //float k_direct = (mat.roughness + 1) * (mat.roughness + 1) / 8;
    float alpha2 = mat.roughness * mat.roughness;
    
    if (mat.type == LAMBERTIAN)
    {
        
#if 0
        const float mix = 0.0;
        
        float3 L = SampleNearestLightDirection(mat.pos, mat.normal);
        
        if (sampleLight && dot(N, L) >= 0 && rnd(seed) < 0.5)
            R = L;
#endif
        
        R = applyRotationMappingZToN(N, sample_hemisphere_cos(seed));
        
        
        R = normalize(R);
        
        // Can't divide by zero
        cosNR = max(dot(N, R), EPSILON);
            
        sampleProb = cosNR * InvPi;
        brdfEval = mat.colour * InvPi;

    }
    
    else if (mat.type == METALIC)
    {
        
        H = sample_hemisphere_TrowbridgeReitzCos(alpha2, seed);
        H = normalize(applyRotationMappingZToN(N, H));
        
        cosNH = max(dot(N, H), 0);
        cosNV = max(dot(N, V), 0);
        cosVH = max(dot(V, H), 0);
        
        R = 2 * cosVH * H - V;
        
        // Can't divide by zero
        cosNR = max(dot(N, R), EPSILON);
        
    
        float D = _TrowbridgeReitz(cosNH * cosNH, alpha2);
        float G = _Smith_TrowbridgeReitz(R, V, H, N, alpha2);
        float3 F = mat.colour + (1 - mat.colour) * pow(max(0, 1 - cosVH), 5);
    
        // Can't divide by zero
        float denomBRDF = max(4 * cosNV * cosNR, EPSILON);
        float denomProb = max(4 * cosNV, EPSILON);
            
        sampleProb = D * cosNH / denomProb;
        brdfEval = (D * G / denomBRDF) * F;
        
    }
    
    else if (mat.type == PLASTIC)
    {
        float r = mat.reflectivity;
        if (rnd(seed) < r)
        {
            R = reflect(-V, N);
            H = normalize(R + V);
            
            cosNH = max(dot(N, H), 0);
            cosNV = max(dot(N, V), 0);
            cosVH = max(dot(V, H), 0);
        
            // Can't divide by zero
            cosNR = dot(N, R);
        }
        else
        {
            R = applyRotationMappingZToN(N, sample_hemisphere_cos(seed));
        
            R = normalize(R);
        
            // Can't divide by zero
            cosNR = dot(N, R);
            H = normalize(R + V);
            
            cosNH = max(dot(N, H), 0);
            cosNV = max(dot(N, V), 0);
            cosVH = max(dot(V, H), 0);
        }
        
        if (cosNR < 0)
        {
            brdfEval = 0;
            sampleProb = 1; //sampleProb = r * (D*HN / (4*abs(OH)));  if allowing sample negative hemisphere
        }
        else
        {
            float denomBRDF = max(4 * cosNV * cosNR, EPSILON);
            float denomProb = max(4 * cosNV, EPSILON);
            
            float D = _TrowbridgeReitz(cosNH * cosNH, alpha2);
            float G = _Smith_TrowbridgeReitz(R, V, H, N, alpha2);
            float3 spec = ((D * G) / denomBRDF);
            brdfEval = r * spec + (1 - r) * InvPi * mat.colour;
            sampleProb = r * (D * cosNH / denomProb) + (1 - r) * (InvPi * cosNR);
        }

    }
    
    else if (mat.type == DIALECTIC)
    {
        R = refract(V, N, mat.ior);
        
        if (length(R) == 0)
            R = reflect(-V, N);
        
        R = normalize(R);
        
        // Can't divide by zero
        cosNR = max(dot(N, R), EPSILON);
            
        sampleProb = cosNR * InvPi;
        brdfEval = (float3(1, 1, 1) - mat.colour) * InvPi;
    }
    
    else
    {
        // NOT SUPPOSE TO HAPPEN
        R = N;
        brdfCos = 0;
        sampleProb = 1;
        cosNR = 0;
        brdfEval = 0;

    }
    
    sampleDir = R;
    brdfCos = brdfEval * cosNR;
}

/* 
    START OF REYGEN SHADER 
*/

#define SLOT_COLOUR 0
#define SLOT_NORMALS 1
#define SLOT_POS_DEPTH 2
#define SLOT_OBJECT_ID 3

[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    uint bufferOffset = launchDim.x * launchIndex.y + launchIndex.x;
    uint seed = getNewSeed(bufferOffset, frame.cpuGeneratedSeed, 8);
    
    float2 dims = float2(launchDim.xy);
    float2 pixel = float2(launchIndex.xy);
    
    float2 d = ((pixel / dims) * 2.f - 1.f); // converts [0, 1] to [-1, 1]
    float4 pixelRay = float4(d.x, d.y, 1, 0);
    
    float3 camOrigin = mul(frame.cameraPixelToWorld, float4(0, 0, 0, 1));
    
    
    float3 newRadiance = 0;
    
    RayPayload payload;
    payload.seed = seed;
    
    
    int nbrSamples = 1 << frame.exponentSamplesPerPixel;
    
    for (int i = 0; i < nbrSamples; ++i)
    {
        // Add random seed there
        float dx = rnd(payload.seed) - 0.5;
        float dy = rnd(payload.seed) - 0.5;
        
        float4 pixelRayRnd = pixelRay + float4(dx, dy, 0, 0);
        float3 direction = normalize(mul(frame.cameraPixelToWorld, pixelRay));
        
        payload = TraceFullPath(camOrigin, direction, payload.seed);
        
        newRadiance += payload.colour;
    }
    
    newRadiance /= (float) nbrSamples;
    
    
    float depth = length(camOrigin - payload.position);

    gOutput[SLOT_COLOUR][launchIndex.xy] = float4(newRadiance, 0);
    gOutput[SLOT_NORMALS][launchIndex.xy] = float4((payload.normal + 1) * 0.5, 1);
    gOutput[SLOT_POS_DEPTH][launchIndex.xy] = float4(payload.position, depth);
    gOutput[SLOT_OBJECT_ID][launchIndex.xy] = float4(GenColour(payload.object + 2), payload.mask);

}

/*
    STANDARD ray shader
*/
[shader("closesthit")] 
void standardChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Iterate depths
    --payload.depth;
    
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    float3 posW = rayOriginW + hitT * rayDirW;
    
    // (w,u,v)
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    RayMaterialProp mat = GetMaterialProp(GeometryIndex());
    VertexAttributes v = GetVertexAttributes(GeometryIndex(), PrimitiveIndex(), barycentrics);
    
    // Alpha can be set by either mask or diffuse texture
    float4 tex_rgba = float4(mat.Diffuse, 1);
    
    if (mat.MaskTextureIdx >= 0)
    {
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
    
    
    bool frontFace = false;
    
#if 0 
    float3 tmpNormal = normalize(mul(instTrans.normalModelToWorld, float4(v.normal, 0)).xyz);
    frontFace = dot(tmpNormal, rayDirW) > 0;
#endif
    
    // DIALECTIC pixel hit!
    if ((tex_rgba.w == 0 || frontFace) && payload.depth > 0)
    {
        payload.reflectDir = rayDirW;
        payload.position = posW;
        payload.prob = 1.0f;
        payload.colour = 1.0f;
        payload.radiance = 0;
    }
    else // We have hit a normal object/pixel
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
        normal = normalize(mul(instTrans.normalModelToWorld, float4(normal, 0)).xyz);
        // Flip normals on two sided faces
        if (dot(rayDirW, normal) > 0)
            normal = -normal;
        
        
        // View Vector
        float3 V = -rayDirW;
        
        float specVal = mat.Reflectivity;
        if (mat.SpecularTextureIdx >= 0)
        {
            specVal = TriSampleTex(specularTex, mat.SpecularTextureIdx, v.texCoord).r;
        }
        
        
        // Create material and sample BDRF to payload output
        if (payload.mediumIoR == mat.IndexOfRefraction)
            mat.IndexOfRefraction = 1.0f; // in X, and hit X again, assume try enter air. UGLY hack
        
        float divIoR = payload.mediumIoR / mat.IndexOfRefraction;
        
        MaterialInfoBDRF matBDRF = MaterialInfo(V, normal, posW, mat.Type, albedo,
                        specVal, mat.Roughness, divIoR, payload.depth);
        sampleBRDF(payload.reflectDir, payload.prob, payload.colour, matBDRF, length(mat.Emittance) == 0, payload.seed);
        
        if (mat.Type == DIALECTIC && dot(normal, payload.reflectDir) < 0)
            payload.mediumIoR = mat.IndexOfRefraction;
        
        
        payload.position = posW;
        payload.radiance = mat.Emittance;
        payload.colour /= payload.prob;
        
        if (payload.rayMode == RAY_PRIMARY)
        {
            payload.normal = normal;
            payload.object = GeometryIndex();
            payload.mask = mat.Type;
            payload.rayMode = RAY_SECONDARY;
        }
    }
}

[shader("miss")]
void standardMiss(inout RayPayload payload)
{
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    float3 radiance = 0;
    float3 colour = 0;
    
    //skyboxDiffuse && skyboxRadiance
    
    if (globals.hasSkybox)
    {
        float sinTheta = sin(frame.atmosphere.x);
        float cosTheta = cos(frame.atmosphere.x);
        
        float3x3 rotationMatrix = float3x3(cosTheta, 0, sinTheta, 0, 1, 0, -sinTheta, 0, cosTheta);
        
        float3 sampleDir = mul(rotationMatrix, rayDirW);
        
        colour = skyboxDiffuse.SampleLevel(trilinearFilter, sampleDir, 0).xyz;
        radiance = frame.atmosphere.w * skyboxRadiance.SampleLevel(trilinearFilter, sampleDir, 0).xyz;
    }
    else
    {
        radiance = frame.atmosphere.w * frame.atmosphere.xyz;
        colour = frame.atmosphere.xyz;
    }
        
    
    // sky normal, depth, and colour
    payload.radiance = radiance;
    payload.colour = colour;
    
    payload.reflectDir = 0.0;
    
    payload.normal = 0;
    payload.position = rayOriginW + 100000 * rayDirW;
    
    payload.mask = 0;
    payload.object = -1;
    
    payload.rayMode = RAY_SECONDARY;
}
