
#define BLOCK_SIZE 8

struct ComputeShaderInput
{
    uint3 GroupID : SV_GroupID;                    // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID : SV_GroupThreadID;        // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex : SV_GroupIndex;              // Flattened local index of the thread within a thread group.
};

RWTexture2D<float4> input[] : register( u0, space0);
RWTexture2D<float4> output : register( u0, space1 );

[numthreads( BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( ComputeShaderInput IN ) 
{ 
    output[IN.DispatchThreadID.xy] = float4(1, 0, 1, 1);
}