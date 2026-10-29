#include "ParticleLayeredCommon.hlsli"

StructuredBuffer<Particle> Particles : register(t0);
ByteAddressBuffer KeyList : register(t1);
ByteAddressBuffer DrawArgs : register(t2);

RWStructuredBuffer<TiledParticle> TiledOut : register(u0);
RWByteAddressBuffer SliceData : register(u1);

// 1) 화면 밖 파티클 무시
// 2) 화면 안에 있는 파티클 내에서 MIN/MAX 좌표를 갱신
// 3) 보이는 파티클 개수 기록
[numthreads(64, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint count = DrawArgs.Load(4);
    uint id = DTid.x;
    bool active = id < count;
        
    bool visible = false;
    float depth = 0.f;
    
    if (active)
    {
        TiledParticle tp = BuildTiledParticle(Particles[KeyList.Load(id * 8)]);
        
        if (IsVisibleTiled(tp))
        {
            float2 hi = tp.quad.center + tp.quad.halfExt;
            float2 low = tp.quad.center - tp.quad.halfExt;
            if (any(hi <= 0.f) || any(low >= (float2)ScreenSize) || tp.life <= 0.f)
                tp.quad.halfExt = 0.f;
      
        }
        
        visible = IsVisibleTiled(tp);
        depth = tp.viewDepth;
        tp.pad = INVALID_SLICE; // Count단계에서 채울 예정
        TiledOut[id] = tp;
        
        uint minBits = WaveActiveMin(visible ? asuint(depth) : 0xFFFFFFFFu);
        uint maxBits = WaveActiveMax(visible ? asuint(depth) : 0u);
        uint numVisible = WaveActiveCountBits(visible);
        
        if (WaveIsFirstLane() && numVisible > 0)
        {
            uint val = 0;
            SliceData.InterlockedMin(SLICE_DATA_DEPTH_MIN, minBits, val);
            SliceData.InterlockedMax(SLICE_DATA_DEPTH_MAX, maxBits, val);
            SliceData.InterlockedAdd(SLICE_DATA_VISIBLE, numVisible, val);
        }
    }
    // depth -> uint
    
}