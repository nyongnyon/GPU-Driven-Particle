#include "ParticleLayeredCommon.hlsli"

ByteAddressBuffer DrawArgs : register(t2);

RWStructuredBuffer<TiledParticle> TiledOut : register(u0);
RWByteAddressBuffer SliceData : register(u1);
RWStructuredBuffer<uint> SliceListOut : register(u2);

#define GROUP_SIZE 256

groupshared uint gs_Hist[LAYERED_MAX_SLICES];
groupshared uint gs_Base[LAYERED_MAX_SLICES];

[numthreads(GROUP_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    if (GI < LAYERED_MAX_SLICES)
        gs_Hist[GI] = 0;
    GroupMemoryBarrierWithGroupSync();
    
    uint count = DrawArgs.Load(4);
    uint id = DTid.x;
    
    uint s = INVALID_SLICE;
    uint localIdx = 0;
    if (id < count)
    {
        s = TiledOut[id].pad; // 몇번째 Slice인가?
        if (s != INVALID_SLICE)
            InterlockedAdd(gs_Hist[s], 1, localIdx);
    }
    GroupMemoryBarrierWithGroupSync();
    
    if (GI < SliceCount && gs_Hist[GI] > 0)
    {
        uint base;
        SliceData.InterlockedAdd(SLICE_DATA_COUNT_BASE + GI * 4, gs_Hist[GI], base);
        gs_Base[GI] = base;
    }
    GroupMemoryBarrierWithGroupSync();
    
    if (s != INVALID_SLICE)
    {
        const uint offset = SliceData.Load(SLICE_DATA_OFFSET_BASE + s * 4);
        SliceListOut[offset + gs_Base[s] + localIdx] = id;
    }

}