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
    uint ColorMode;
    float4 ColorA;
    float4 ColorB;    
    uint EmitterId;     // written to Particle.emitterId on emit
    uint3 SimPad;
}

#define COLOR_MODE_FIXED    0
#define COLOR_MODE_RANGE    1
#define COLOR_MODE_HUE      2

float3 RandomColor(inout uint rng)
{
    [branch]
    if (ColorMode == COLOR_MODE_RANGE)
        return lerp(ColorA.rgb, ColorB.rgb, Rand01(rng));

    [branch]
    if (ColorMode == COLOR_MODE_HUE)
        return HueToRGB(Rand01(rng)) * ColorA.w;

    return ColorA.rgb;
}

RWStructuredBuffer<Particle>    Particles : register(u0);
RWStructuredBuffer<uint>        DeadList : register(u1);
RWByteAddressBuffer             Counters : register(u2);
RWStructuredBuffer<uint>        AliveFlags : register(u3); // For SlotReuseFlags ver / 1 = alive, 0 = dead
RWStructuredBuffer<uint>        AliveIn : register(u4);
RWStructuredBuffer<uint>        AliveOut : register(u5);
RWByteAddressBuffer             IndirectArgs : register(u6); // For Indirect Version, 0~12 : dispatch args, 16~32 : draw args

#endif