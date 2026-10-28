#include "ParticleTiledCommon.hlsli"

StructuredBuffer<Particle> Particles : register(t0);
ByteAddressBuffer SortedList : register(t1);
ByteAddressBuffer DrawArgs : register(t2);

RWStructuredBuffer<TiledParticle> TiledOut : register(u1);

uint NearToFarSlot(uint j, uint count)
{
    return SortedList.Load((count - 1 - j) * 8);
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint count = DrawArgs.Load(4);
    const uint id = DTid.x;
    if (id >= count)
        return;
    
    TiledOut[id] = BuildTiledParticle(Particles[NearToFarSlot(id, count)]);
    
}