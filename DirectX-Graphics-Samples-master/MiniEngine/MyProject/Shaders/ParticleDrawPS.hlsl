#include "ParticleShading.hlsli"

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return ShadeParticle(input.color, input.uv);
}