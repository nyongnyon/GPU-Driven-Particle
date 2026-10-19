#include "ParticleComputeCommon.hlsli"

[numthreads(PARTICLE_GROUP_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint i = DTid.x;
    if (i >= MaxParticles)
        return;
    
    if ((Particles[i].flags & PARTICLE_FLAG_ALIVE) == 0)
        return;
    
    Particle p = Particles[i];
    p.life -= DeltaTime;
    
    // ¼ö¸í ³¡
    if (p.life <= 0.0)
    {
        p.life = 0.f;
        p.flags &= ~PARTICLE_FLAG_ALIVE;
        Particles[i] = p;
        
        uint prevDead, prevAlive;
        Counters.InterlockedAdd(COUNTER_DEAD, 1, prevDead);
        DeadList[prevDead] = i;
        Counters.InterlockedAdd(COUNTER_ALIVE, 0xFFFFFFFF, prevAlive);
        return;
    }    
    
    p.velocity += Gravity * DeltaTime;
    p.position += p.velocity * DeltaTime;
    Particles[i] = p;

}