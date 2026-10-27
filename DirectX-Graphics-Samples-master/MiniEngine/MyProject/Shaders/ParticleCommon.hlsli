#ifndef PARTICLE_COMMON_HLSLI
#define PARTICLE_COMMON_HLSLI

struct Particle
{
    float3 position; float life;
    float3 velocity; float maxlife;
    uint color;
    float size;
    uint flags;
    uint pad;
};

#define COUNTER_ALIVE 0
#define COUNTER_DEAD 4
#define COUNTER_EMIT 8
#define COUNTER_ALIVE_AFTER_SIM 12

#define PARTICLE_FLAG_ALIVE 0x1u
#define PARTICLE_GROUP_SIZE 64

#define PARTICLE_ARGS_SIMULATE_DISPATCH 0
#define PARTICLE_ARGS_DRAW 16

// uint to float4(Color)
float4 UnpackColor(uint c)
{
    return float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF) / 255.0f;
}

// HDR색 저장
uint PackR11G11B10(float3 c)
{
    c = clamp(c, 0.0f, 65000.0f); // 음수 / half 최대값 근처 inf 방지
    uint3 h = f32tof16(c);
    return ((h.r >> 4) & 0x7FF)
          | (((h.g >> 4) & 0x7FF) << 11)
          | (((h.b >> 5) & 0x3FF) << 22);
}

float3 UnpackR11G11B10(uint p)
{
    return f16tof32(uint3((p & 0x7FF) << 4,
                          ((p >> 11) & 0x7FF) << 4,
                          ((p >> 22) & 0x3FF) << 5));
}

float3 HueToRGB(float h)
{
    return saturate(abs(frac(h + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f);
}


// hash
uint PcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Rand01(inout uint state)
{
    state = PcgHash(state);
    return float(state) / 4294967296.0f;
}

float3 RandDirection(inout uint state)
{
    float z = Rand01(state) * 2.0 - 1.0;
    float phi = Rand01(state) * 6.28318530718;
    float r = sqrt(saturate(1.0 - z * z));
    return float3(r * cos(phi), r * sin(phi), z);
}

#endif