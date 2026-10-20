#include "ParticleComputeCommon.hlsli"

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint survived = Counters.Load(COUNTER_ALIVE_AFTER_SIM);
    Counters.Store(COUNTER_ALIVE, survived);
}