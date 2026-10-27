#include "ParticleShading.hlsli"

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float viewDepth : TEXCOORD1;
};

struct WBOITOutput
{
    float4 accum : SV_Target0;
    float reveal : SV_Target1;
};

float DepthWeight(float d, float alpha)
{
    float w = 10.0f / (1e-5f + pow(d / 5.0f, 2.0f) + pow(d / 200.0f, 6.0f));
    return alpha * clamp(w, 1e-2f, 3e3f);
}

WBOITOutput main(PSInput input)
{
    float4 shaded = ShadeParticle(input.color, input.uv);
    float a = shaded.a;
    
    float w = DepthWeight(input.viewDepth, a);
    
    WBOITOutput o;
    o.accum = shaded * w;
    o.reveal = a;
    
    return o;
}