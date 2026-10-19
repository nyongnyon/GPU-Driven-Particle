#ifndef PARTICLE_COMPUTE_COMMON_HLSLI
#define PARTICLE_COMPUTE_COMMON_HLSLI

#include "ParticleCommon.hlsli"

cbuffer SimConstants : register(b0)
{
    float3 EmitterPos;
    float DeltaTime;
    float3 Gravity;
    uint EmitCount;
    float MinLife;
    float MaxLife;
    float MinSpeed;
    float MaxSpeed;
    float ParticleSize;
    uint MaxParticles;
    uint RandomSeed;
    uint BaseColor;
}

RWStructuredBuffer<Particle>    Particles : register(u0);
RWStructuredBuffer<uint>        DeadList : register(u1);
RWByteAddressBuffer             Counters : register(u2);

#endif