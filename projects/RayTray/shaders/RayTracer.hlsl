
#define BLOCK_WIDTH 16
#define BLOCK_HEIGHT 9

struct ComputeShaderInput
{
    uint3 GroupID : SV_GroupID;                    // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID : SV_GroupThreadID;        // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex : SV_GroupIndex;              // Flattened local index of the thread within a thread group.
};

struct Ray
{
    float3 pos;
    float3 dir;
    float newSeed;
};

struct HitRecord
{
    bool hit;
    float3 pos;
    float3 refDir;
    float distance;
    uint matIndex;
    float newSeed;
};

struct ScatterRecord
{
    bool isAbsorbed;
    float3 attenuation;
    float3 newDirection;
    float newSeed;
};

struct Sphere
{
    float3 Position;
    float Radius;
    uint MatIdx;
    
    float3 padding;
};

#define MODE_MATERIAL_COLOUR    0
#define MODE_MATERIAL_METAL     1
#define MODE_MATERIAL_DIALECTIC 2

//#define MODE_MATERIAL_CHECKER   3
//#define MODE_MATERIAL_TEXTURE   4

struct Material
{
    uint Mode;
    float3 Colour;
    
    float2 uvBottomLeft;
    float2 uvTopRight;
    uint TexIdx;
    
    float Scale;
    float Fuzz;
    float RefractionIndex;
    
    // Total 3*16 = 48 bytes
};

cbuffer RayCB : register( b0 )
{
    float4 VoidColour;
    
    uint2 WindowResolution;
    uint NbrSpheres;
    uint NbrMaterials;
    
    float TimeSeed;
    
    float3 padding;
    // Total 2*16 = 32 bytes
}

/* Return beteen [0,1) */
float random_float(float2 uv, float seed)
{
    float fixedSeed = abs(seed) + 1.0;
    float x = dot(uv, float2(12.9898, 78.233) * fixedSeed);
    return frac(sin(x) * 43758.5453);
}

float random_float(float2 uv, float seed, float min, float max)
{
    return random_float(uv, seed) * (max - min) + min;
}

float3 random_colour(float2 uv, float seed)
{
    float a = random_float(uv, seed);
    float b = random_float(uv, a);
    float c = random_float(uv, b);
    return float3(a, b, c);
}

float3 random_in_unit_sphere(float2 uv, float seed)
{
    return normalize(random_colour(uv, seed) - 0.5);
}

float3 random_in_unit_disk(float2 uv, float seed)
{
    float a = random_float(uv, seed, -1, 1);
    float b = random_float(uv, a, -1, 1);
    float3 result = float3(a, b, 0);
    for (int i = 0; i < 10000 && dot(result, result) >= 1; i++)
    {
        a = random_float(uv, b, -1, 1);
        b = random_float(uv, a, -1, 1);
        result = float3(a, b, 0);
    }
    return result;

}

cbuffer CamCB : register( b1 )
{
    float4 CameraPos;
    float4 CameraLookAt;
    float4 CameraUp;
    float4 CameraWindow;
    // Total 4*16 = 64 bytes
}

StructuredBuffer<Sphere> SphereList : register(t0);

StructuredBuffer<Material> MaterialList : register(t1);

RWTexture2D<float4> output : register( u0 );

/*
    Fill scatter record and attenuation
*/
ScatterRecord build_scatter(HitRecord record, Material material, float2 uv, float seed)
{
    ScatterRecord result;
    float3 rnd;
    result.newSeed = seed;
    switch (material.Mode)
    {
        case MODE_MATERIAL_COLOUR:
            result.isAbsorbed = true;
            result.attenuation = material.Colour;
            result.newDirection = record.refDir;
            break;
        case MODE_MATERIAL_METAL:
            rnd = 0.5 * random_in_unit_sphere(uv, seed);
            result.newSeed = rnd.z;
            result.newDirection = normalize(record.refDir + material.Fuzz * rnd);
            result.isAbsorbed = dot(result.newDirection, record.refDir) < 0;
            result.attenuation = material.Colour;
            break;
        case MODE_MATERIAL_DIALECTIC:
            result.isAbsorbed = true;
            result.attenuation = float3(1.0, 1.0, 1.0);
            result.newDirection = record.refDir;
            break;
        default:
            result.isAbsorbed = true;
            break;
    }
    return result;
}

/*
*   Returns the hitrecord where distance, position, 
    reflection direction are stored.
*/
HitRecord sphere_hit(Sphere sphere, float3 rayOrigin, float3 rayDir)
{
    HitRecord hitResult;
    hitResult.hit = false;
    hitResult.refDir = float3(0, 0, 0);
    hitResult.pos = float3(0, 0, 0);
    hitResult.distance = 10e100;
    hitResult.matIndex = 0;
    
    rayDir = normalize(rayDir);
    float3 oc = rayOrigin - sphere.Position;
    float a = dot(rayDir, rayDir);
    float half_b = dot(oc, rayDir);
    float c = dot(oc, oc) - sphere.Radius * sphere.Radius;
    float discriminant = half_b * half_b - a * c;

    if (discriminant > 0)
    {
        float root = sqrt(discriminant);

        float temp = (-half_b - root) / a;
        /*
            SOMETHING WRONG WIH THE WE DID MIN MAX
        */
        
        // Min temp to remove the issue that we are already hitting a sphere
        //max to see that we are not reaching infinity
        if (temp > 0.0001 && temp < 10e100)
        {
            float3 hitPos = rayOrigin + rayDir * temp;
            float3 normal = normalize(hitPos - sphere.Position);
            if (dot(normal, rayDir) < 0)
            {
                hitResult.distance = dot(rayDir * temp, rayDir * temp);
                hitResult.refDir = reflect(rayDir, normal);
                hitResult.pos = hitPos;
                hitResult.hit = true;
                return hitResult;
            }
        }

        temp = (-half_b + root) / a;
        if (temp > 0.0001 && temp < 10e100)
        {
            float3 hitPos = rayOrigin + rayDir * temp;
            float3 normal = normalize(hitPos - sphere.Position);
            if (dot(normal, rayDir) < 0)
            {
                hitResult.distance = dot(rayDir * temp, rayDir * temp);
                hitResult.refDir = reflect(rayDir, normal);
                hitResult.pos = hitPos;
                hitResult.hit = true;
                return hitResult;
            }
        }
    }

    return hitResult;
}

/*
    Goes through all geometry and returns the hit record 
    of the hit on the closest target.
*/
HitRecord get_closet_target(float3 rayOrigin, float3 rayDir)
{
    uint selected = 0;
    HitRecord current;
    HitRecord record = sphere_hit(SphereList[0], rayOrigin, rayDir);;
    for (uint i = 1; i < NbrSpheres; i++)
    {
        current = sphere_hit(SphereList[i], rayOrigin, rayDir);
        if (current.hit && current.distance < record.distance)
        {
            record = current;
            selected = i;
        }
    }
    
    record.matIndex = SphereList[selected].MatIdx;
    
    return record;
}


#define RECURSIVE_DEPTH 5
/*
    Get the ray colour of the ray
*/
float4 ray_colour(float3 origin, float3 dir, float2 uv, float seed) 
{ 
    float oldY = normalize(dir).y;
    
    HitRecord curRecord;
    curRecord.pos = origin;
    curRecord.refDir = dir;
    
    Material curMaterial;
    float4 result = float4(1, 1, 1, 1);
    for (int i = 0; i < RECURSIVE_DEPTH; i++)
    {
        curRecord = get_closet_target(curRecord.pos, curRecord.refDir);
        if (curRecord.hit)
        {
            curMaterial = MaterialList[curRecord.matIndex];
            ScatterRecord scattered = build_scatter(curRecord, curMaterial, uv, seed);
            seed = scattered.newSeed;
            oldY = curRecord.refDir.y;
            
            curRecord.refDir = scattered.newDirection;
            result *= float4(scattered.attenuation, 1.0);
            oldY = curRecord.refDir.y;
            // If we absorbe the ray, stop reflecting
         
            if (scattered.isAbsorbed) 
                return result;
        }
        else // background
        {
            float t = 0.5 * (oldY + 1.0);
            result *= (1.0 - t) * float4(1.0, 1.0, 1.0, 1.0) + t * VoidColour;
            return result;
        }  
    }
    // if i >= RECURSIVE_DEPTH or scatteredIsAbsorbed
    return result * float4(0.0, 0.0, 0.0, 1.0);
}

/*
    Using the camera, we build build the ray from UV coordinates
*/
Ray getRay(float3 lookfrom, float3 lookat, float3 vup, float lens_radius, float2 uv, float seed)
{
    Ray ray;
    float focus_dist = length(lookfrom - lookat);
    float3 w = normalize(lookfrom - lookat);
    float3 u = normalize(cross(vup, w));
    float3 v = cross(w, u);
    
    float3 horizontal = focus_dist * CameraWindow.x * u;
    float3 vertical = focus_dist * CameraWindow.y * v;
    float3 upper_left_corner = lookfrom - horizontal / 2 - vertical / 2 - focus_dist * w;
    
    float3 rd = lens_radius * random_in_unit_disk(uv, seed);
#if 1
    float3 offset = u * rd.x + v * rd.y;
#else
    float3 offset = float3(0, 0, 0);
#endif
    ray.pos = lookfrom + offset;
    ray.dir = upper_left_corner + uv.x * horizontal + uv.y * vertical - lookfrom - offset;
    ray.newSeed = rd.z;
    return ray;
}


float4 mix(float4 a, float4 b, float alpha)
{
    return a * alpha + b * (1 - alpha);
}

[numthreads(BLOCK_WIDTH, BLOCK_HEIGHT, 1)]
void main( ComputeShaderInput IN ) 
{ 
    float dv = 1 / ((float) WindowResolution.y - 1);
    float du = 1 / ((float) WindowResolution.x - 1);
    
    int y = IN.DispatchThreadID.y;
    float v = y * dv;
    
    int x = IN.DispatchThreadID.x;
    float u = x * du;
    const float2 uv = float2(u, 1 - v);
    
    // Flip image vertically to ease calculations
    Ray camRay = getRay(CameraPos.xyz, CameraLookAt.xyz, CameraUp.xyz, 0.05, uv, TimeSeed);
    
#if 1
    output[IN.DispatchThreadID.xy] = ray_colour(camRay.pos, camRay.dir, uv, camRay.newSeed);
#else
#if 1
    float3 colour = MaterialList[SphereList[0].MatIdx].Colour;
    colour = random_colour(uv, TimeSeed);
    output[IN.DispatchThreadID.xy] = float4(colour, 1.0f);
#else
    float val = random_float(uv, TimeSeed);
    output[IN.DispatchThreadID.xy] = float4(val, val, val, 1.0f);
#endif
#endif

    }