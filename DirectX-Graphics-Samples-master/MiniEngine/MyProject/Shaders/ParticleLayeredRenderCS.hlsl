#include "ParticleLayeredCommon.hlsli"

StructuredBuffer<TiledParticle> TiledParticles : register(t3);
StructuredBuffer<uint> SliceList : register(t4);
ByteAddressBuffer SliceData : register(t5);
Texture2D<float> SceneDepth : register(t6);   // 씬 깊이 버퍼 (reverse-Z) : 직접 깊이 테스트

RWTexture2D<float4> Result : register(u3);

#define TILE_SIZE           16
#define GROUP_THREADS       (TILE_SIZE * TILE_SIZE)     // 256
#define MASK_WORDS          (GROUP_THREADS / 32)        // 8
#define MIN_TRANSMITTANCE   (10.0f / 255.0f)

// slice 안의 깊이 가중치 : slice가 얇으므로 약하게만 (앞쪽일수록 최대 4배)
//   0 이면 alpha만으로 평균 (순수 평균)
#ifndef LAYERED_DEPTH_WEIGHT
#define LAYERED_DEPTH_WEIGHT 1
#endif

groupshared uint gs_HitMask[MASK_WORDS];
groupshared TiledParticle gs_Batch[GROUP_THREADS]; // 32 B x 256 = 8 KB
groupshared uint gs_AnyAlive;
groupshared uint gs_TileMaxDepth;   // 타일에서 가장 먼 씬 거리 (asuint, 양수 float라 uint 비교 = float 비교)

float3 HeatColor(float t)   // 0 = 파랑 -> 초록 -> 노랑 -> 1 = 빨강
{
    return saturate(float3(1.5f - abs(4.f * t - 3.f),
                           1.5f - abs(4.f * t - 2.f),
                           1.5f - abs(4.f * t - 1.f)));
}

float SliceWeight(float z, float sliceNear, float sliceFar)
{
#if LAYERED_DEPTH_WEIGHT
    const float t = saturate((z - sliceNear) / max(sliceFar - sliceNear, 1e-5f));
    const float f = 1.f - t;
    return 1.f + 3.f * f * f;
#else
    return 1.f;
#endif
}

uint SliceOffset(uint s)
{
    return SliceData.Load(SLICE_DATA_OFFSET_BASE + s * 4);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
    const bool inScreen = all(DTid.xy < ScreenSize);
    const float2 pixelCenter = (float2) DTid.xy + 0.5f;

    const float2 tileMin = (float2) (Gid.xy * TILE_SIZE);
    const float2 tileMax = tileMin + TILE_SIZE;

    // 깊이 테스트 : 이 픽셀의 씬까지 거리. 파티클이 이보다 멀면 가려진 것
    const float sceneDepth = inScreen ? SceneViewDepth(SceneDepth[DTid.xy]) : 0.f;

    // 타일 단위 컬링 : 타일에서 가장 먼 씬보다 뒤의 파티클 / slice는 타일 전체에서 가려진다
    if (GI == 0)
        gs_TileMaxDepth = 0;
    GroupMemoryBarrierWithGroupSync();
    InterlockedMax(gs_TileMaxDepth, asuint(sceneDepth));
    GroupMemoryBarrierWithGroupSync();
    const float tileMaxDepth = asfloat(gs_TileMaxDepth);

    float zMin = asfloat(SliceData.Load(SLICE_DATA_DEPTH_MIN));
    float zMax = asfloat(SliceData.Load(SLICE_DATA_DEPTH_MAX));
    SanitizeDepthRange(zMin, zMax);

    float3 color = 0.f; // premultiplied 누적
    float T = 1.f; // 지금까지의 투과율

    uint processedSlices = 0; // 디버그 : 실제로 처리한 (비어있지 않은) slice 수
    uint nonEmptySlices = 0;

    uint sliceBegin = SliceOffset(0);
    bool tileDone = false;

    for (uint s = 0; s < SliceCount; ++s)
    {
        const uint sliceEnd = SliceOffset(s + 1);
        const uint begin = sliceBegin;
        sliceBegin = sliceEnd;

        // 그룹 전체가 같은 값을 읽으므로 분기가 갈라지지 않는다 (barrier 안전)
        if (begin == sliceEnd)
            continue;

        ++nonEmptySlices;

        const float sliceNear = SliceBoundary(s, zMin, zMax);
        const float sliceFar = SliceBoundary(s + 1, zMin, zMax);

        // slice는 앞에서 뒤 순서 -> 이 slice가 타일의 가장 먼 씬보다 뒤라면 남은 slice도 모두 가려짐 (그룹 전체 같은 값)
        if (sliceNear > tileMaxDepth)
            tileDone = true;

        if (tileDone)
            continue; // 남은 slice는 개수만 세고(디버그) 처리하지 않음

        ++processedSlices;

        float3 accum = 0.f;
        float wsum = 0.f;
        float reveal = 1.f;

        for (uint base = begin; base < sliceEnd; base += GROUP_THREADS)
        {
            if (GI < MASK_WORDS)
                gs_HitMask[GI] = 0;
            GroupMemoryBarrierWithGroupSync();

            const uint j = base + GI;
            if (j < sliceEnd)
            {
                const TiledParticle tp = TiledParticles[SliceList[j]];
                gs_Batch[GI] = tp;
                if (tp.viewDepth <= tileMaxDepth && OverlapsTile(tp, tileMin, tileMax))
                    InterlockedOr(gs_HitMask[GI >> 5], 1u << (GI & 31));
            }
            GroupMemoryBarrierWithGroupSync();

            // 이미 꽉 막힌 픽셀은 셰이딩 생략 (타일의 다른 픽셀 때문에 루프에 남아 있을 뿐)
            if (inScreen && T > MIN_TRANSMITTANCE)
            {
                [loop]
                for (uint w = 0; w < MASK_WORDS; ++w)
                {
                    uint bits = gs_HitMask[w];
                    while (bits != 0)
                    {
                        const uint k = firstbitlow(bits);
                        bits &= bits - 1;

                        const TiledParticle tp = gs_Batch[w * 32 + k];
                        if (tp.viewDepth > sceneDepth)
                            continue;   // 픽셀 단위 깊이 테스트 (하드웨어 depth test 대신)

                        const float4 c = ShadeAtPixel(tp, pixelCenter); // (rgb * a, a)
                        if (c.a <= 0.f)
                            continue;

                        const float wgt = SliceWeight(tp.viewDepth, sliceNear, sliceFar);
                        accum += c.rgb * wgt;
                        wsum += c.a * wgt;
                        reveal *= 1.f - c.a;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }

        // slice 결과 = 가중 평균색(straight) x slice 불투명도, 앞에서부터 under 합성
        if (wsum > 1e-6f)
        {
            const float sliceAlpha = 1.f - reveal;
            const float3 sliceColor = accum / wsum;
            color += T * sliceColor * sliceAlpha;
            T *= reveal;
        }

        // 조기 종료 판정 : 타일 안에 아직 비치는 픽셀이 하나라도 있는가
        if (GI == 0)
            gs_AnyAlive = 0;
        GroupMemoryBarrierWithGroupSync();
        if (inScreen && T > MIN_TRANSMITTANCE)
            gs_AnyAlive = 1;
        GroupMemoryBarrierWithGroupSync();
        tileDone = (gs_AnyAlive == 0);
        GroupMemoryBarrierWithGroupSync(); // 다음 slice에서 GI 0이 덮어쓰기 전에 모두 읽도록
    }

    if (!inScreen)
        return;

    if (DebugView != 0)
    {
        if (nonEmptySlices == 0)
        {
            Result[DTid.xy] = 0.f;
            return;
        }
        // 파랑 = 앞쪽 몇 장만 보고 끝남, 빨강 = 끝까지 다 봄
        const float t = (float) processedSlices / (float) nonEmptySlices;
        const float a = 0.6f;
        Result[DTid.xy] = float4(HeatColor(t) * a, a);
        return;
    }

    Result[DTid.xy] = float4(color, 1.f - T);
}
