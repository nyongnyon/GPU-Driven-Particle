#include "ParticleLayeredCommon.hlsli"

ByteAddressBuffer DrawArgs : register(t2);

RWStructuredBuffer<TiledParticle> TiledOut : register(u0);
RWByteAddressBuffer SliceData : register(u1);

#define GROUP_SIZE 256

groupshared uint gs_Hist[LAYERED_MAX_SLICES];

// 1) 파티클 하나씩 담당, 파티클이 어느 depth에 있는가
// 2) depth -> Slice 위치(층)를 얻고, Pad에 기록한다.
// 3) 각 Slice에는 몇개의 파티클이 그곳에 있는지 기록한다.
[numthreads(GROUP_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID, uint GI :SV_GroupIndex )
{
    if (GI < LAYERED_MAX_SLICES)
        gs_Hist[GI] = 0;
    GroupMemoryBarrierWithGroupSync();
    
    uint count = DrawArgs.Load(4);
    uint id = DTid.x;
    
    if (id < count)
    {
        const TiledParticle p = TiledOut[id];
        if (IsVisibleTiled(p))
        {
            float zMin = asfloat(SliceData.Load(SLICE_DATA_DEPTH_MIN));
            float zMax = asfloat(SliceData.Load(SLICE_DATA_DEPTH_MAX));
            SanitizeDepthRange(zMin, zMax);
            
            uint s = SliceOf(p.viewDepth, zMin, zMax);
            TiledOut[id].pad = s; // pad에 몇번째 슬라이스인지 기록한다.
            InterlockedAdd(gs_Hist[s], 1);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    
    uint tmp;
    if (GI < SliceCount&& gs_Hist[GI] > 0)
        SliceData.InterlockedAdd(SLICE_DATA_COUNT_BASE + GI * 4, gs_Hist[GI], tmp);

}