#include "ParticleCommon.hlsli"

#ifndef TILED_DEBUG_HEATMAP
#define TILED_DEBUG_HEATMAP 0
#endif

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

StructuredBuffer<Particle> Particles : register(t0);
ByteAddressBuffer SortedList : register(t1); // uint2(slot, depthKey), 먼 것 -> 가까운 것
ByteAddressBuffer DrawArgs : register(t2); // offset 4 = 정렬된 개수

RWTexture2D<float4> Result : register(u0);

#define TILE_SIZE   16
#define GROUP_THREADS (TILE_SIZE * TILE_SIZE)            // 256
#define MASK_WORDS   (GROUP_THREADS / 32)                // 8
#define MIN_TRANSMITTANCE (1.0f / 255.0f)

groupshared uint gs_HitMask[MASK_WORDS];

// 정렬 목록은 먼 것부터라서, 가까운 것부터 읽으려면 뒤에서부터 읽는다 (4-3에서 사용)
uint NearToFarSlot(uint j, uint count)
{
    return SortedList.Load((count - 1 - j) * 8);
}

bool GetScreenQuad(Particle p, out ScreenQuad quad)
{
    quad.center = 0.f;
    quad.halfExt = 0.f;
    
    const float4 clip = mul(ViewProj, float4(p.position, 1.f));
    if (clip.w < NearClip)
        return false;

    const float2 ndc = clip.xy / clip.w;
    quad.center = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * ScreenSize;
    quad.halfExt = p.size * ProjScale / clip.w * 0.5f * ScreenSize;
    return true;
}


bool OverlapsTile(Particle p, float2 tileMin, float2 tileMax)
{
    ScreenQuad quad;
    if (!GetScreenQuad(p, quad))
        return false;
    return all(quad.center + quad.halfExt > tileMin) && all(quad.center - quad.halfExt < tileMax);
}

#if TILED_DEBUG_HEATMAP
float3 HeatColor(float t)   // 0 = 파랑 -> 초록 -> 노랑 -> 1 = 빨강
{
    return saturate(float3(1.5f - abs(4.f * t - 3.f),
                           1.5f - abs(4.f * t - 2.f),
                           1.5f - abs(4.f * t - 1.f)));
}
#endif

float4 ShadeAtPixel(Particle p, float2 pixelCenter)
{
    ScreenQuad q;
    if (!GetScreenQuad(p, q))
        return 0.f;

    float2 corner = (pixelCenter - q.center) / q.halfExt;
    corner.y = -corner.y;
    if (any(abs(corner) > 1.f)) // 타일에는 걸쳤으나 이 픽셀을 덮지는 않는다.
        return 0.f; 

    const float3 rgb = UnpackR11G11B10(p.color); // ParticleColor()와 동일
    const float life = saturate(p.life / max(p.maxlife, 1e-4f));
    const float falloff = saturate(1.f - dot(corner, corner));
    const float a = life * falloff * falloff;

    return float4(rgb * a, a);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
    // 주의 : 아래에 GroupMemoryBarrier가 있으므로 화면 밖 스레드도 return 하면 안 된다
    const bool inScreen = all(DTid.xy < ScreenSize);
    const float2 pixelCenter = (float2) DTid.xy + 0.5f;

    const float2 tileMin = (float2) (Gid.xy * TILE_SIZE);
    const float2 tileMax = tileMin + TILE_SIZE;

    const uint count = DrawArgs.Load(4);
    
    float3 color = 0.f;
    float T = 1.f;
    uint hits = 0; // for hitmap

    for (uint base = 0; base < count; base += GROUP_THREADS)
    {
        if (GI < MASK_WORDS)
            gs_HitMask[GI] = 0;
        GroupMemoryBarrierWithGroupSync();

        // 스레드마다 파티클 하나씩 검사해서 비트로 표시
        const uint j = base + GI;
        if (j < count)
        {
            const Particle p = Particles[NearToFarSlot(j, count)];
            if (OverlapsTile(p, tileMin, tileMax))
                InterlockedOr(gs_HitMask[GI >> 5], 1u << (GI & 31));
        }
        GroupMemoryBarrierWithGroupSync();

#if TILED_DEBUG_HEATMAP
        [unroll]
        for (uint w = 0; w < MASK_WORDS; ++w)
            hits += countbits(gs_HitMask[w]);
#else
        if (inScreen)
        {
            for (uint w = 0; w < MASK_WORDS && T > MIN_TRANSMITTANCE; ++w)
            {
                uint bits = gs_HitMask[w];
                while (bits != 0 && T > MIN_TRANSMITTANCE)
                {
                    const uint k = firstbitlow(bits); 
                    bits &= bits - 1;

                    const Particle p = Particles[NearToFarSlot(base + w * 32 + k, count)];
                    const float4 c = ShadeAtPixel(p, pixelCenter);

                    color += T * c.rgb;
                    T *= 1.f - c.a;
                }
            }
        }        
#endif
        GroupMemoryBarrierWithGroupSync();
    }

    if (!inScreen)
        return;
#if TILED_DEBUG_HEATMAP
    if (hits == 0)
    {
        Result[DTid.xy] = 0;
        return;
    }

    // 개수 차이가 크니 로그 스케일로
    const float t = saturate(log2(1.f + hits) / log2(1.f + HeatMax));
    const float a = 0.6f;
    Result[DTid.xy] = float4(HeatColor(t) * a, a);
#else
    Result[DTid.xy] = float4(color, 1.f - T);
    
#endif
}