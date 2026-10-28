#include "ParticleTiledCommon.hlsli"

#ifndef TILED_DEBUG_HEATMAP
#define TILED_DEBUG_HEATMAP 0
#endif


ByteAddressBuffer DrawArgs : register(t2); // offset 4 = 정렬된 개수
StructuredBuffer<TiledParticle> TiledParticles : register(t5); //

RWTexture2D<float4> Result : register(u0);

#define TILE_SIZE   16
#define GROUP_THREADS (TILE_SIZE * TILE_SIZE)            // 256
#define MASK_WORDS   (GROUP_THREADS / 32)                // 8
#define MIN_TRANSMITTANCE (1.0f / 255.0f)

groupshared uint gs_HitMask[MASK_WORDS];


#if TILED_DEBUG_HEATMAP
float3 HeatColor(float t)   // 0 = 파랑 -> 초록 -> 노랑 -> 1 = 빨강
{
    return saturate(float3(1.5f - abs(4.f * t - 3.f),
                           1.5f - abs(4.f * t - 2.f),
                           1.5f - abs(4.f * t - 1.f)));
}
#endif

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
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

        const uint j = base + GI;
        if (j < count && OverlapsTile(TiledParticles[j], tileMin, tileMax))
        {
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

                    const float4 c = ShadeAtPixel(TiledParticles[base + w * 32 + k], pixelCenter);

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