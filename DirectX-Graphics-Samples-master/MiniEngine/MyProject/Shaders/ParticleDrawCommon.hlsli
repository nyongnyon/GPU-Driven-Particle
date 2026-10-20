#include "ParticleCommon.hlsli"

#ifndef USE_FLAG_ARRAY
#define USE_FLAG_ARRAY 0
#endif

cbuffer DrawConstants : register(b0)
{
    float4x4 ViewProj;
    float4 CamRight;
    float4 CamUp;
};

StructuredBuffer<Particle> Particles : register(t0);
StructuredBuffer<uint> AliveFlags : register(t1);

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

static const float2 kCorners[6] =
{
    float2(-1.f, 1.f), float2(1.f, 1.f), float2(1.f, -1.f),
    float2(-1.f, 1.f), float2(1.f, -1.f), float2(-1.f, -1.f)
};


VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;

#if USE_FLAG_ARRAY
    const bool alive = AliveFlags[instanceId] != 0;
#else    
    const bool alive = (Particles[instanceId].flags & PARTICLE_FLAG_ALIVE) != 0;
#endif  
    // Particle이 살아있지 않은 경우 -> Rasterizer에서 버리도록한다.
    if (!alive)
    {
        output.position = float4(2.0f, 2.0f, 2.0f, 1.f);
        output.color = 0;
        output.uv = 0;
        
        return output;
    }
    
    Particle p = Particles[instanceId];
    
    float2 corner = kCorners[vertexId];
    float3 worldPos = p.position + (corner.x * CamRight.xyz + corner.y * CamUp.xyz) * p.size;
    
    output.position = mul(ViewProj, float4(worldPos, 1.f));
    output.color = UnpackColor(p.color);
    output.color.a = saturate(p.life / max(p.maxlife, 1e-4));
    output.uv = corner;
    
    return output;
}