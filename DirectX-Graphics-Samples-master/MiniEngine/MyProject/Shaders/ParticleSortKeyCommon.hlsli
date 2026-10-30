#ifndef PARTICLE_SORT_KEY_COMMON_HLSLI
#define PARTICLE_SORT_KEY_COMMON_HLSLI

#include "ParticleCommon.hlsli"

// Pool이 넘겨준 DrawInput(t0 ~ t2)을 그대로 읽어 정렬 키를 만든다 -> Pool 쪽 변경 없음
#define SORT_SOURCE_REUSE       0   // FullScan / SlotReuse : Particle.flags로 생존 판정
#define SORT_SOURCE_REUSE_FLAGS 1   // SlotReuseFlags : AliveFlags 배열로 생존 판정
#define SORT_SOURCE_ALIVE       2   // AliveList : AliveList[0, aliveCount)가 곧 생존 slot

#ifndef SORT_SOURCE
#define SORT_SOURCE SORT_SOURCE_REUSE
#endif

// D3D12_DRAW_ARGUMENTS { VertexCountPerInstance, InstanceCount, StartVertex, StartInstance }
#define DRAW_ARGS_INSTANCE_COUNT 4

cbuffer SortConstants : register(b0)
{
    float3 CamPosition; uint MaxParticles;
    float3 CamForward;  uint FilterGroup;   // SORT_NO_FILTER = every particle
};

#define SORT_NO_FILTER 0xFFFFFFFFu

// emitterId -> technique group. Only read when FilterGroup != SORT_NO_FILTER
StructuredBuffer<uint> EmitterGroups : register(t3);

StructuredBuffer<Particle> Particles : register(t0);
#if SORT_SOURCE == SORT_SOURCE_REUSE_FLAGS
StructuredBuffer<uint> AliveFlags : register(t1);
#elif SORT_SOURCE == SORT_SOURCE_ALIVE
StructuredBuffer<uint> AliveList : register(t1);
ByteAddressBuffer Counters : register(t2);
#endif

RWByteAddressBuffer SortList : register(u0);    // uint2(slot, depthKey) : BitonicSort 64bit 포맷 (key = .y)
RWByteAddressBuffer DrawArgs : register(u1);    // InstanceCount = 정렬 대상 수 (BitonicSort의 counter로도 사용)



[numthreads(PARTICLE_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint i = DTid.x;

    bool visible = false;
    uint slot = 0;
    float depth = 0.f;

    if (i < MaxParticles)
    {
#if SORT_SOURCE == SORT_SOURCE_ALIVE
        bool alive = false;
        if (i < Counters.Load(COUNTER_ALIVE))
        {
            slot = AliveList[i];
            alive = true;
        }
#elif SORT_SOURCE == SORT_SOURCE_REUSE_FLAGS
        slot = i;
        const bool alive = AliveFlags[i] != 0;
#else
        slot = i;
        const bool alive = (Particles[i].flags & PARTICLE_FLAG_ALIVE) != 0;
#endif
        if (alive)
        {
            const Particle p = Particles[slot];
            const bool inGroup = (FilterGroup == SORT_NO_FILTER) || (EmitterGroups[p.emitterId] == FilterGroup);
            depth = dot(p.position - CamPosition, CamForward);

            // 쿼드가 일부라도 카메라 앞에 걸치면 그린다 (완전히 뒤쪽이면 정렬/드로우 대상에서 제외)
            visible = inGroup && (depth + p.size) > 0.f;
        }
    }

    // compact append : atomic은 wave당 한 번
    const uint waveCount = WaveActiveCountBits(visible);
    const uint rank = WavePrefixCountBits(visible);
    uint base = 0;
    if (waveCount > 0 && WaveIsFirstLane())
        DrawArgs.InterlockedAdd(DRAW_ARGS_INSTANCE_COUNT, waveCount, base);
    base = WaveReadLaneFirst(base);

    if (visible)
    {
        // 양수 float의 비트 패턴은 uint 대소 순서와 같다.
        // 0은 내림차순 BitonicSort의 NullItem(패딩)이므로 최소값을 양수로 clamp
        const uint key = asuint(max(depth, 1e-6f));
        SortList.Store2((base + rank) * 8, uint2(slot, key));
    }
}

#endif
