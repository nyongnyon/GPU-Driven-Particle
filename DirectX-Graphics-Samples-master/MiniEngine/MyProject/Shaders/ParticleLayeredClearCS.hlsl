#include "ParticleLayeredCommon.hlsli"

RWByteAddressBuffer SliceData : register(u1);

[numthreads(LAYERED_MAX_SLICES, 1, 1)] // (64, 1, 1)
void main( uint GI : SV_GroupIndex)
{
    SliceData.Store(SLICE_DATA_COUNT_BASE + GI * 4, 0); // Clear
    
    if (GI == 0)
    {
        SliceData.Store(SLICE_DATA_DEPTH_MIN, asuint(3.0e38f)); // 최대값으로 초기화
        SliceData.Store(SLICE_DATA_DEPTH_MAX, 0); // 최소값으로 초기화
        SliceData.Store(SLICE_DATA_VISIBLE, 0);
    }
}