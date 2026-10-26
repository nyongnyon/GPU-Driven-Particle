#include "ParticleShading.hlsli"

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct WBOITOutput
{
    float4 accum : SV_Target0;
    float reveal : SV_Target1;
};

float DepthWeight(float z, float alpha)
{
    return clamp(alpha * max(1e-2f, 3e3f * z * z * z), 1e-2f, 3e3f);
}

WBOITOutput main(PSInput input)
{
    float4 shaded = ShadeParticle(input.color, input.uv);
    float a = shaded.a;
    
    float w = DepthWeight(input.position.z, a);
    
    WBOITOutput o;
    o.accum = shaded * w;
    o.reveal = a;
    
    return o;
}