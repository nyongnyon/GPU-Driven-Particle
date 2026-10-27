#include "ParticleDrawCommon.hlsli"

// uint2(slot, depthKey) : depth 내림차순(먼 것 -> 가까운 것)으로 정렬된 상태
ByteAddressBuffer SortedList : register(t1);

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    // DrawIndirect의 InstanceCount = 정렬된 개수 -> 범위 검사 불필요
    // instance 순서대로 블렌딩되므로 instanceId 순서가 곧 back-to-front 순서
    const uint slot = SortedList.Load(instanceId * 8);

    VSOutput output;
    Particle p = Particles[slot];

    float2 corner = kCorners[vertexId];
    float3 worldPos = p.position + (corner.x * CamRight.xyz + corner.y * CamUp.xyz) * p.size;

    output.position = mul(ViewProj, float4(worldPos, 1.f));
    output.color = ParticleColor(p);
    output.uv = corner;

    return output;
}
