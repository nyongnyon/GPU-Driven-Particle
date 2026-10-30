#include "ParticleTiledCommon.hlsli"

#ifndef TILED_DEBUG_HEATMAP
#define TILED_DEBUG_HEATMAP 0
#endif


ByteAddressBuffer DrawArgs : register(t2); // offset 4 = 정렬된 개수
StructuredBuffer<TiledParticle> TiledParticles : register(t5); //

Texture2D<float> SceneDepth : register(t3);   // 씬 깊이 버퍼 (reverse-Z) : 직접 깊이 테스트

RWTexture2D<float4> Result : register(u0);

#define TILE_SIZE   16
#define GROUP_THREADS (TILE_SIZE * TILE_SIZE)            // 256
#define MASK_WORDS   (GROUP_THREADS / 32)                // 8
#define MIN_TRANSMITTANCE (1.0f / 255.0f)

groupshared uint gs_HitMask[MASK_WORDS];
groupshared uint gs_TileMaxDepth;   // 타일에서 가장 먼 씬 거리 (asuint, 양수 float라 uint 비교 = float 비교)


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

    // 깊이 테스트 : 이 픽셀의 씬까지 거리. 파티클이 이보다 멀면 가려진 것
    const float sceneDepth = inScreen ? SceneViewDepth(SceneDepth[DTid.xy]) : 0.f;

    // 타일 단위 컬링 : 타일에서 가장 먼 씬보다도 뒤에 있는 파티클은 타일의 모든 픽셀에서 가려진다
    if (GI == 0)
        gs_TileMaxDepth = 0;
    GroupMemoryBarrierWithGroupSync();
    InterlockedMax(gs_TileMaxDepth, asuint(sceneDepth));
    GroupMemoryBarrierWithGroupSync();
    const float tileMaxDepth = asfloat(gs_TileMaxDepth);
    
    float3 color = 0.f;
    float T = 1.f;
    uint hits = 0; // for hitmap

    for (uint base = 0; base < count; base += GROUP_THREADS)
    {
        if (GI < MASK_WORDS)
            gs_HitMask[GI] = 0;
        GroupMemoryBarrierWithGroupSync();

        const uint j = base + GI;
        if (j < count)
        {
            const TiledParticle tp = TiledParticles[j];
            if (tp.viewDepth <= tileMaxDepth && OverlapsTile(tp, tileMin, tileMax))
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

                    const TiledParticle tp = TiledParticles[base + w * 32 + k];
                    if (tp.viewDepth > sceneDepth)
                        continue;   // 픽셀 단위 깊이 테스트 (하드웨어 depth test 대신)

                    const float4 c = ShadeAtPixel(tp, pixelCenter);

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