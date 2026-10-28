#include "ParticleCommon.hlsli"

cbuffer TileConstants : register(b0)
{
    uint2 ScreenSize;
    uint2 TileCount;
    float4x4 ViewProj;
    float2 ProjScale;
    float HeatMax;
    float _pad;
};

StructuredBuffer<Particle> Particles : register(t0);
ByteAddressBuffer SortedList : register(t1); // uint2(slot, depthKey), 먼 것 -> 가까운 것
ByteAddressBuffer DrawArgs : register(t2); // offset 4 = 정렬된 개수

RWTexture2D<float4> Result : register(u0);

#define TILE_SIZE   16
#define GROUP_THREADS (TILE_SIZE * TILE_SIZE)            // 256
#define MASK_WORDS   (GROUP_THREADS / 32)                // 8

groupshared uint gs_HitMask[MASK_WORDS];

// 정렬 목록은 먼 것부터라서, 가까운 것부터 읽으려면 뒤에서부터 읽는다 (4-3에서 사용)
uint NearToFarSlot(uint j, uint count)
{
    return SortedList.Load((count - 1 - j) * 8);
}

// 파티클의 화면 사각형 (픽셀 좌표). 빌보드가 화면과 평행해서 축 정렬 사각형이 된다
bool ParticleScreenRect(Particle p, out float2 rmin, out float2 rmax)
{
    const float4 clip = mul(ViewProj, float4(p.position, 1.f));
    if (clip.w <= 1e-4f)
    {
        // 카메라 평면에 걸친 경우 -> 보수적으로 화면 전체
        rmin = 0;
        rmax = (float2) ScreenSize;
        return true;
    }
    const float2 ndc = clip.xy / clip.w;
    const float2 center = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * ScreenSize;
    const float2 halfExt = p.size * ProjScale / clip.w * 0.5f * ScreenSize;
    rmin = center - halfExt;
    rmax = center + halfExt;
    return true;
}

bool OverlapsTile(Particle p, float2 tileMin, float2 tileMax)
{
    float2 rmin, rmax;
    ParticleScreenRect(p, rmin, rmax);
    return all(rmax > tileMin) && all(rmin < tileMax);
}

float3 HeatColor(float t)   // 0 = 파랑 -> 초록 -> 노랑 -> 1 = 빨강
{
    return saturate(float3(1.5f - abs(4.f * t - 3.f),
                           1.5f - abs(4.f * t - 2.f),
                           1.5f - abs(4.f * t - 1.f)));
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
    // 주의 : 아래에 GroupMemoryBarrier가 있으므로 화면 밖 스레드도 return 하면 안 된다
    const bool inScreen = all(DTid.xy < ScreenSize);

    const float2 tileMin = (float2) (Gid.xy * TILE_SIZE);
    const float2 tileMax = tileMin + TILE_SIZE;

    const uint count = DrawArgs.Load(4);
    uint hits = 0;

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

        [unroll]
        for (uint w = 0; w < MASK_WORDS; ++w)
            hits += countbits(gs_HitMask[w]);

        GroupMemoryBarrierWithGroupSync();
    }

    if (!inScreen)
        return;

    if (hits == 0)
    {
        Result[DTid.xy] = 0;
        return;
    }

    // 개수 차이가 크니 로그 스케일로
    const float t = saturate(log2(1.f + hits) / log2(1.f + HeatMax));
    const float a = 0.6f;
    Result[DTid.xy] = float4(HeatColor(t) * a, a);
}