#include "ParticleCommon.hlsli"

StructuredBuffer<Particle> Particles : register(t0);

cbuffer DrawConstants : register(b0)
{
    float4x4 ViewProj;
    float4 CamRight;
    float4 CamUp;
};

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

float4 ParticleColor(Particle p)
{
    return float4(UnpackR11G11B10(p.color), saturate(p.life / max(p.maxlife, 1e-4)));
}

VSOutput Culled()
{
    VSOutput output;
    output.position = float4(2.0f, 2.0f, 2.0f, 1.f);
    output.color = 0;
    output.uv = 0;
    return output;
}