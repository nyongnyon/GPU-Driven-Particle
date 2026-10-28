#ifndef PARTICLE_TIELD_COMMON_HLSLI
#define PARTICLE_TIELD_COMMON_HLSLI

#include "ParticleCommon.hlsli"

cbuffer TileConstants : register(b0)
{
    uint2 ScreenSize;
    uint2 TileCount;
    float4x4 ViewProj;
    float2 ProjScale;
    float HeatMax;
    float NearClip;
};

struct ScreenQuad
{
    float2 center;
    float2 halfExt;
};

struct TiledParticle
{
    ScreenQuad quad;
    uint color;
    float life;
    float viewDepth;
    uint pad;
};

// 스크린 좌표상 quad 위치, 크기
bool GetScreenQuad(Particle p, out ScreenQuad quad, out float viewDepth)
{
    quad.center = 0.f;
    quad.halfExt = 0.f;

    const float4 clip = mul(ViewProj, float4(p.position, 1.f));
    viewDepth = clip.w;
    if (clip.w < NearClip)
        return false;

    const float2 ndc = clip.xy / clip.w;
    quad.center = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * ScreenSize;
    quad.halfExt = p.size * ProjScale / clip.w * 0.5f * ScreenSize;
    return true;
}

// FOR PREP
TiledParticle BuildTiledParticle(Particle p)
{
    TiledParticle tp;
    GetScreenQuad(p, tp.quad, tp.viewDepth);
    tp.color = p.color;
    tp.life = saturate(p.life / max(p.maxlife, 1e-4f));
    tp.pad = 0;
    return tp;
}

// 파티클이 타일과 겹쳐있는가
bool OverlapsTile(TiledParticle tp, float2 tileMin, float2 tileMax)
{
    const ScreenQuad quad = tp.quad;
    if (quad.halfExt.x <= 0.f)
        return false;
    return all(quad.center + quad.halfExt > tileMin) && all(quad.center - quad.halfExt < tileMax);
}

// TEMP shading
float4 ShadeAtPixel(TiledParticle tp, float2 pixelCenter)
{
    const ScreenQuad q = tp.quad;
    if (q.halfExt.x <= 0.f)
        return 0.f;

    float2 corner = (pixelCenter - q.center) / q.halfExt;
    corner.y = -corner.y;
    if (any(abs(corner) > 1.f))
        return 0.f;

    const float3 rgb = UnpackR11G11B10(tp.color);
    const float falloff = saturate(1.f - dot(corner, corner));
    const float a = tp.life * falloff * falloff;

    return float4(rgb * a, a);
}

#endif
