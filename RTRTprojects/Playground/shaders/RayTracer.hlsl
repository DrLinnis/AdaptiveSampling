
// Define constants in code
#define PI 3.1415926538
#define PI2 6.283185307
#define PI_2 1.570796327
#define PI_4 0.7853981635
#define InvPi 0.318309886
#define InvPi2 0.159154943
#define EPSILON 0.00001

#define T_HIT_MIN 0.0001
#define T_HIT_MAX 100000

#define DIFFUSE         0
#define SPECULAR        1
#define TRANSMISSIVE    2

#define RAY_PRIMARY 0
#define RAY_SECONDARY 1

#define RAY_LIGHT 2

// PAYLOADS AND STRUCTS
struct RayPayload
{
    // Future Ray Specific
    float3 reflectDir;
    float3 lightDir;
    
    // Current Mat Direction Specific
    float3 colourReflect;
    float3 colourLight;
    
    // Current Mat Specific
    float3 radiance;
    float3 position;
    float3 normal;
    
    // For denoising
    int object;
    float mask;
    
    // Ray Properties
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
    
    float ambientFactor;
    
    uint exponentSamplesPerPixel;
    uint nbrBouncesPerPath;
    uint cpuGeneratedSeed;
};

struct ConstantData
{
    uint nbrActiveLights;
    
    uint hasSkybox;
    
    float2 _padding;
    
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
float3 SampleNearestLightDirection(float3 pos, float3 normal, inout uint seed)
{
    float3 result = 0;
    
#if 1
    
    int selected = (globals.nbrActiveLights - 1) * rnd(seed);
    float3 lightPos = globals.lightPositions[selected].xyz;
    lightPos = modelToWorldPosition(lightPos);
    float3 dir = lightPos - pos;
    result = normalize(dir);
    
#elif 1
    float bestDist = 1.#INF; // infinity
    for (int i = 0; i < globals.nbrActiveLights; ++i)
    {
        float3 lightPos = globals.lightPositions[i].xyz;
        lightPos = modelToWorldPosition(lightPos);
        float radius = globals.lightPositions[i].w; // Adjust for scale
        
        
        float3 dir = lightPos - pos;
        float distSqred = dot(dir, dir);
        if (distSqred < bestDist && distSqred > 0 && distSqred <= radius * radius)
        {
            bestDist = distSqred;
            result = dir;
        }
    }
    
#else
    
    float bestDist = 1.#INF; // infinity
    for (int i = 0; i < globals.nbrActiveLights; ++i)
    {
        float3 lightPos = globals.lightPositions[i].xyz;
        lightPos = modelToWorldPosition(lightPos);
        float3 dir = lightPos - pos;
        float distSqred = dot(dir, dir);
        if (distSqred < bestDist && distSqred > 0)
        {
            bestDist = distSqred;
            result = dir;
        }
    }
    
    result = normalize(result);
#endif
    
    return result;
}

// INTERPOLATION helper functions
uint3 _getIndices(uint geometryIdx, uint triangleIndex)
{
    uint indexByteStartAddress = triangleIndex * 3 * 4;
    return indices[geometryIdx].Load3(indexByteStartAddress);
}

VertexAttributes GetVertexAttributes(uint geometryIndex, uint primitiveIndex, float3 barycentrics, out float3 faceNormal)
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
    
    
    faceNormal = cross(normalize(vertPos[0] - vertPos[1]), normalize(vertPos[0] - vertPos[2]));
    
    
    // world space version
    float pA = instTrans.lodScaler * length(cross(vertPos[1] - vertPos[0], vertPos[2] - vertPos[0]));
    
    v.texCoord[2] = 0.5 * log2(tA / pA);
  
    // normalize direction vectors
    v.normal = normalize(mul(instTrans.normalModelToWorld, float4(v.normal, 0)));
    v.tangent = normalize(mul(instTrans.modelToWorld, float4(-v.tangent, 0)));
    v.bitangent = normalize(mul(instTrans.modelToWorld, float4(v.bitangent, 0)));
    
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
    ray.TMax = T_HIT_MAX;
    
    RayDesc rayLightDesc;
    rayLightDesc.TMin = T_HIT_MIN;
    rayLightDesc.TMax = T_HIT_MAX;
    
    float3 radiance = 0;
    float3 colour = 1.0f;
    RayPayload currRay;
    RayPayload lightRay;
    currRay.seed = seed;
    currRay.rayMode = RAY_PRIMARY;
    currRay.depth = 0;
    currRay.mediumIoR = 1.0f;
    
    uint prevType;
    float3 prevPos;
    while (currRay.depth < frame.nbrBouncesPerPath)
    {
        currRay.radiance = lightRay.radiance = 0;
        currRay.colourReflect = lightRay.colourReflect = 0;
        prevType = currRay.rayMode;
        prevPos = currRay.position;
        
        TraceRay( gRtScene, 0, 0xFF, 0, 1, 0, ray, currRay );
        float3 dist = prevPos - currRay.position;
        float distSqredLight = 1;
        
        // Trace light if given direction
        rayLightDesc.Origin = currRay.position;
        rayLightDesc.Direction = currRay.lightDir;
        lightRay.radiance = 0;
        const float wantedMaxRadiance = 10;
        
        if (length(currRay.lightDir) != 0 && currRay.depth <= 3)
        {
            TraceRay(gRtScene, 0, 0xFF, 0, 1, 0, rayLightDesc, lightRay);
            float currentRadiance = length(lightRay.radiance);
            // adjust
            lightRay.radiance *= currentRadiance > 0 ? wantedMaxRadiance / currentRadiance : 0;
            
            float3 distToLight = currRay.position - lightRay.position;
            distSqredLight = length(distToLight);
            distSqredLight = max(distSqredLight, 1);

        }
        
        // Sampling from the skybox doesn't invovle distance.
        if (currRay.object == -1)
            dist = 1;
        
        // Adjust rnd light hit
        float rndRadiance = length(currRay.radiance);
        currRay.radiance *= rndRadiance > 0 ? sqrt(3) / rndRadiance : 0;
        
        const float shadowRayBounceAlpha = 0.5;
        radiance += shadowRayBounceAlpha * colour * currRay.radiance / length(dist) 
            + (1 - shadowRayBounceAlpha) * colour * currRay.colourLight * lightRay.radiance / distSqredLight;
        colour *= currRay.colourReflect;
        
        // Store the first Primary Ray normal/specular/object/position
        if (currRay.rayMode == RAY_SECONDARY && prevType == RAY_PRIMARY)
        {
            result.normal = currRay.normal;
            result.mask = currRay.mask;
            result.object = currRay.object;
            result.position = currRay.position;
            
            if (length(currRay.radiance) > 0)
                radiance = currRay.object == -1 ? currRay.colourReflect : currRay.radiance;
        }
        
        ray.Origin = currRay.position;
        ray.Direction = currRay.reflectDir;
        
        if (length(currRay.radiance) > 0)
            break;
        if (length(colour) < 0.01) {
            //radiance += colour * frame.ambientFactor;
            break;
        }
            
        
        // Only shoot from 0 on camera.
        ray.TMin = T_HIT_MIN;
    }
    
    result.colourReflect = radiance;
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
    cos2 = max(cos2, EPSILON);
    float x = alpha2 + (1 - cos2) / cos2;
    return alpha2 / (PI * cos2 * cos2 * x * x);
}

float _Smith_TrowbridgeReitz(in float3 wi, in float3 wo, in float3 wm, in float3 wn, in float alpha2)
{
    if (dot(wo, wm) < 0 || dot(wi, wm) < 0)
        return 0.0f;

    float cos2 = max(dot(wn, wo), EPSILON);
    cos2 *= cos2;
    float lambda1 = 0.5 * (-1 + sqrt(1 + alpha2 * (1 - cos2) / cos2));
    cos2 = max(dot(wn, wi), EPSILON);
    cos2 *= cos2;
    float lambda2 = 0.5 * (-1 + sqrt(1 + alpha2 * (1 - cos2) / cos2));
    return 1 / (1 + lambda1 + lambda2);
}


// Cook Torrance BRDF
void sampleBRDF(out float3 reflectDir, out float3 lightDir, 
                out float3 brdfCosReflect, out float3 brdfCosLight,
                in MaterialInfoBDRF mat, inout uint seed )
{
    float3 brdfEvalReflect, brdfEvalLight;
    float sampleProbReflect, sampleProbLight;
    
    // Reflection dir, view vector, normal, half-vector
    float3 R, V = mat.view, N = mat.normal, H;
    
    float cosNH, cosVH, cosNR, cosNV, cosRH, cosNL;
    
    //float k_direct = (mat.roughness + 1) * (mat.roughness + 1) / 8;
    float alpha2 = mat.roughness * mat.roughness;
    
    
    // Light Sample Ray scatter cone
    float3 L = SampleNearestLightDirection(mat.pos, mat.normal, seed);
    cosNL = dot(N, L);
    float3 lightRandomScatterDir = sample_hemisphere_TrowbridgeReitzCos(0.01, seed);
    L = normalize(applyRotationMappingZToN(L, lightRandomScatterDir));
    
    // Can't sample in negative hemisphere
    if (cosNL <= 0) 
    {
        sampleProbLight = 1;
        brdfEvalLight = 0;
        L = 0;
    }
    else
    {
        sampleProbLight = cosNR;
        brdfEvalLight = mat.colour;
    }
        
    if (mat.type == DIFFUSE)
    {
        R = sample_hemisphere_TrowbridgeReitzCos(alpha2, seed);
        R = normalize(applyRotationMappingZToN(N, R));
        
        H = normalize(R + V);
        
        cosNH = dot(N, H);
        cosNV = dot(N, V);
        cosVH = dot(V, H);
        cosNR = dot(N, R);
        
        float D = _TrowbridgeReitz(cosNH * cosNH, alpha2);
        
        if (cosNR < 0) // Can't sample in negative hemisphere
        {
            sampleProbReflect = 1;
            brdfEvalReflect = 0;
        }
        else
        {
            float G = _Smith_TrowbridgeReitz(R, V, H, N, alpha2);
            float3 F = mat.colour + (1 - mat.colour) * pow(max(0, 1 - cosVH), 5);
    
            // Can't divide by zero
            float denomBRDF = 4 * cosNV * cosNR;
            float denomProb = 4 * cosNV;
            
            sampleProbReflect = D * cosNR / denomProb;
            brdfEvalReflect = (D * G / denomBRDF) * F;
        }
        
        
    }
    
    else if (mat.type == SPECULAR)
    {
        float r = mat.reflectivity;
        if (rnd(seed) < r)
        {
            R = reflect(-V, N);
            H = normalize(R + V);
            
            cosNH = dot(N, H);
            cosNV = dot(N, V);
            cosVH = dot(V, H);
        
            cosNR = dot(N, R);
        }
        else
        {
            R = applyRotationMappingZToN(N, sample_hemisphere_cos(seed));
        
            R = normalize(R);
        
            cosNR = dot(N, R);
            H = normalize(R + V);
            
            cosNH = dot(N, H);
            cosNV = dot(N, V);
            cosVH = dot(V, H);
        }
        
        
        
        if (cosNR < 0)
        {
            brdfEvalReflect = 0;
            sampleProbReflect = 1; //sampleProb = r * (D*HN / (4*abs(OH)));  if allowing sample negative hemisphere
        }
        else
        {
            float denomBRDF = max(4 * cosNV * cosNR, EPSILON);
            float denomProb = max(4 * cosNV, EPSILON);
            
            float D = _TrowbridgeReitz(cosNH * cosNH, alpha2);
            float G = _Smith_TrowbridgeReitz(R, V, H, N, alpha2);
            float3 spec = ((D * G) / denomBRDF);
            brdfEvalReflect = r * spec + (1 - r) * InvPi * mat.colour;
            sampleProbReflect = r * (D * cosNH / denomProb) + (1 - r) * (InvPi * cosNR);
        }

    }
    
    else if (mat.type == TRANSMISSIVE)
    {
        R = refract(V, N, mat.ior);
        
        if (length(R) == 0)
            R = reflect(-V, N);
        
        R = normalize(R);
        
        cosNR = dot(N, R);
            
        sampleProbReflect = cosNR;
        brdfEvalReflect = (float3(1, 1, 1));
    }
    
    else
    {
        // NOT SUPPOSE TO HAPPEN
        R = N;
        brdfEvalReflect = 0;
        sampleProbReflect = 1;
        cosNR = 0;
    }
    
    
    
    reflectDir = R;
    lightDir = L;
    
    brdfCosReflect = brdfEvalReflect * cosNR / sampleProbReflect;
    brdfCosLight = brdfEvalLight * cosNL / sampleProbLight;

}

/* 
    START OF REYGEN SHADER 
*/

#define SLOT_COLOUR 0
#define SLOT_NORMALS 1
#define SLOT_POS_DEPTH 2
#define SLOT_OBJECT_ID_MASK 3

[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    uint bufferOffset = launchDim.x * launchIndex.y + launchIndex.x;
    uint seed = getNewSeed(bufferOffset, frame.cpuGeneratedSeed, 8);
    
    float2 dims = float2(launchDim.xy);
    float2 pixel = float2(launchIndex.xy);
    
    
    float2 d = ((pixel / (dims)) * 2.f - 1.f); // converts [0, 1] to [-1, 1]
    
    
    float aspectRatio = dims.x / dims.y;
    d.x *= aspectRatio;
    
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
        
        
        // Opting for no random in initial pixel to not confuse
        float3 direction = normalize(mul(frame.cameraPixelToWorld, pixelRay));
        
        payload = TraceFullPath(camOrigin, direction, payload.seed);
        
        newRadiance += payload.colourReflect;
    }
    
    newRadiance /= (float) nbrSamples;
    
    
    float depth = length(camOrigin - payload.position) / T_HIT_MAX;

    gOutput[SLOT_COLOUR][launchIndex.xy] = float4(newRadiance, 0);
    gOutput[SLOT_NORMALS][launchIndex.xy] = float4((payload.normal + 1) * 0.5, 1);
    gOutput[SLOT_POS_DEPTH][launchIndex.xy] = float4(payload.position, depth);
    gOutput[SLOT_OBJECT_ID_MASK][launchIndex.xy] = float4(GenColour(payload.object + 2), payload.mask);

}

/*
    STANDARD ray shader
*/
[shader("closesthit")] 
void standardChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Iterate depths
    ++payload.depth;
    
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    
    float3 posW = rayOriginW + hitT * rayDirW;
    
    // (w,u,v)
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    float3 faceNormal;
    RayMaterialProp mat = GetMaterialProp(GeometryIndex());
    VertexAttributes v = GetVertexAttributes(GeometryIndex(), PrimitiveIndex(), barycentrics, faceNormal);
    
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
    
    
    
    // transparent pixel hit!
    if (tex_rgba.w == 0)
    {
        payload.reflectDir = rayDirW;
        payload.position = posW;
        payload.colourReflect = 1.0f;
        payload.radiance = 0;
    }
    else // We have hit a normal object/pixel
    {
        // sample diffuse texture value
        float3 albedo = tex_rgba.rgb;
        
        // View Vector
        float3 V = -rayDirW;
        
        // Build normal
        float3 objNormal = v.normal;
        float3 normal = v.normal;
        
        if (mat.NormalTextureIdx >= 0)
        {
            // from [0,1] to [-1, 1]
            normal = TriSampleTex(normalsTex, mat.NormalTextureIdx, v.texCoord).rgb * 2 - 1;
            float3x3 TBN = float3x3(v.tangent, v.bitangent, v.normal);
            normal = mul(normal, TBN);
        }
        
        normal = normalize(normal);
        objNormal = normalize(objNormal);
        
        bool isLight = length(mat.Emittance) != 0;
        
        // If the primary ray is hiting an object that is in the wrong direction, or hitting the light.
        if ((mat.Type != TRANSMISSIVE && dot(normal, V) <= 0 && payload.rayMode == RAY_PRIMARY) || isLight)
        {
            payload.position = posW;
            payload.colourReflect = 0;
            payload.reflectDir = -normal;
            
            payload.radiance = mat.Emittance;
            
            payload.lightDir = 0;
            
            payload.normal = isLight ? -normal : 0;
            //payload.object = length(mat.Emittance) != 0 ? GeometryIndex() : -1;
            payload.object = GeometryIndex();
            payload.mask = mat.Type;
            payload.rayMode = RAY_SECONDARY;
            return;
        }
        
        
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
        sampleBRDF( payload.reflectDir, payload.lightDir, 
                    payload.colourReflect, payload.colourLight, 
                    matBDRF, payload.seed
            );
        
        if (mat.Type == TRANSMISSIVE && dot(normal, payload.reflectDir) < 0)
            payload.mediumIoR = mat.IndexOfRefraction;
        
        payload.position = posW;
        payload.radiance = 0;
        
        if (payload.rayMode == RAY_PRIMARY)
        {
            payload.normal = objNormal;
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
    payload.colourReflect = colour;
    
    payload.reflectDir = 0.0;
    
    payload.normal = -rayDirW;
    payload.position = rayOriginW + 100000 * rayDirW;
    
    payload.mask = 0;
    payload.object = -1;
    
    payload.rayMode = RAY_SECONDARY;
}
