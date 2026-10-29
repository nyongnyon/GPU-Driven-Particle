#ifndef PARTICLE_LAYERED_COMMON_HLSLI
#define PARTICLE_LAYERED_COMMON_HLSLI

#include "ParticleTiledCommon.hlsli"

// Slice : 카메라 Near~Far까지 깊이 기준으로 나눈 것
// Slice 안에서는 WBOIT
// Slice끼리는 앞에서부터 합성
// 타일의 모든 픽셀이 충분히 불투명하면, 뒤 slice는 처리하지 않는다. (조기 종료)

#define LAYERED_MAX_SLICES 64
cbuffer LayeredConstants : register(b1)
{
    uint SliceCount;    // 1부터 LAYERED_MAX_SLICES까지 중
    uint SliceMode;     // linear 혹은 Log
    uint DebugView;     // debug
    uint LayeredPad;    // 
}

#define SLICE_DATA_DEPTH_MIN    0   // 보이는 파티클의 최소 depth
#define SLICE_DATA_DEPTH_MAX    4   // 
#define SLICE_DATA_VISIBLE      8   // visible count
#define SLICE_DATA_COUNT_BASE   16  // slice 개수 쓰기 커서
#define SLICE_DATA_OFFSET_BASE  (SLICE_DATA_COUNT_BASE + LAYERED_MAX_SLICES * 4) //
#define INVALID_SLICE           0xFFFFFFFFu


bool    IsVisibleTiled(TiledParticle p)
{
    return p.quad.halfExt.x > 0.f;
}

void SanitizeDepthRange(inout float zMin, inout float zMax)
{
    zMin = max(zMin, NearClip);
    zMax = max(zMax, zMin * 1.001f + 1e-4f); // zMax > zMin을 보장하기 위한 작업
}

// z 값을 기준으로 몇번 slice에 위치해 있는가?
uint SliceOf(float z, float zMin, float zMax)
{
    float t;
    if (SliceMode == 1) // log 모드
        t = log2(max(z, zMin) / zMin) / log2(zMax / zMin);
    else
        t = (z - zMin) / (zMax - zMin);
    
    return min((uint) (saturate(t) * (float)SliceCount), SliceCount - 1);
}

float SliceBoundary(uint s, float zMin, float zMax)
{
    const float t = (float) s / (float) SliceCount;
    if (SliceMode == 1)
        return zMin * exp2(log2(zMax / zMin) * t);
    return lerp(zMin, zMax, t);
}

#endif