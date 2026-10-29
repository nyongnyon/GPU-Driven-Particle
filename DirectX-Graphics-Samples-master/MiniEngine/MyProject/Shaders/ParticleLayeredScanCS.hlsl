#include "ParticleLayeredCommon.hlsli"

RWByteAddressBuffer SliceData : register(u1);

groupshared uint gs_Count[LAYERED_MAX_SLICES];

// 1) SliceData를 통해서 나는 몇번째의 index의 데이터부터 읽으면 되는가를 입력한다.
[numthreads(LAYERED_MAX_SLICES, 1, 1)]
void main( uint GI : SV_GroupIndex )
{
    gs_Count[GI] = (GI < SliceCount) ? SliceData.Load(SLICE_DATA_COUNT_BASE + GI * 4) : 0;
    GroupMemoryBarrierWithGroupSync();
    
    if (GI == 0)
    {
        uint running = 0;
        for (uint s = 0; s < LAYERED_MAX_SLICES; ++s)
        {
            SliceData.Store(SLICE_DATA_OFFSET_BASE + s * 4, running);
            running += gs_Count[s];
        }
        SliceData.Store(SLICE_DATA_OFFSET_BASE + LAYERED_MAX_SLICES * 4, running); // 마지막 기록
          
    }
    SliceData.Store(SLICE_DATA_COUNT_BASE + GI * 4, 0);
}