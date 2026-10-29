// WBOIT render target debug views : Accum / Revealage -> 3 RGBA8 targets shown with ImGui::Image
Texture2D<float4> AccumTex : register(t0);
Texture2D<float> RevealTex : register(t1);

cbuffer DebugConstants : register(b0)
{
    float WeightRange;  // accum.a shown as white (log scale)
};

struct DebugOutput
{
    float4 color : SV_Target0;   // accum.rgb / accum.a : average color (same as composite)
    float4 weight : SV_Target1;  // accum.a : sum of alpha * weight
    float4 reveal : SV_Target2;  // revealage : 1 = nothing drawn, 0 = fully covered
};

DebugOutput main(float4 position : SV_Position)
{
    int2 pixel = int2(position.xy);

    float4 accum = AccumTex[pixel];
    float reveal = RevealTex[pixel];

    // same overflow handling as ParticleWBOITCompositePS
    if (isinf(max(max(abs(accum.r), abs(accum.g)), abs(accum.b))))
        accum.rgb = accum.aaa;

    float w = saturate(log2(1.0f + accum.a) / log2(1.0f + WeightRange));

    DebugOutput o;
    o.color = float4(saturate(accum.rgb / max(accum.a, 1e-5f)), 1.0f);
    o.weight = float4(w.xxx, 1.0f);
    o.reveal = float4(reveal.xxx, 1.0f);
    return o;
}
