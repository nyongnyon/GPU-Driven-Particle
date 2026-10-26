#pragma once

#include "ParticleTypes.h"
#include "ParticleRenderTypes.h"
#include "GpuBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"

class GraphicsContext;
namespace Math { class Camera; }

// Alpha blending 직전에 살아있는 파티클을 카메라 깊이 기준으로 GPU에서 정렬한다.
// Pool이 넘겨준 DrawInput(t0 ~ t2)만 읽으므로 Pool 쪽은 정렬의 존재를 모른다.
//  1) KeyGen : 살아있고 카메라 앞에 있는 파티클을 (slot, depth)로 SortList에 compact append
//  2) Sort   : Core BitonicSort로 depth 내림차순 정렬 (먼 것 -> 가까운 것)
//  3) Draw   : DrawSource::Sorted VS가 SortList[instanceId].slot을 읽어 DrawIndirect
class ParticleDepthSorter
{
public:
	void Initialize();
	void Shutdown();

	// 정렬 결과를 읽는 DrawInput을 반환 (source = Sorted, indirect)
	Particles::DrawInput Sort(GraphicsContext& gfx, const Math::Camera& cam, const Particles::DrawInput& input);

private:
	enum KeyGenVariant : uint32_t
	{
		kKeyGenReuse = 0,	// FullScan / SlotReuse
		kKeyGenReuseFlags,	// SlotReuseFlags
		kKeyGenAlive,		// AliveList
		kNumKeyGenVariants
	};

	static KeyGenVariant SelectKeyGen(Particles::DrawSource source);
	void EnsureCapacity(uint32_t maxParticles);

	RootSignature		m_KeyGenSignature;
	ComputePSO			m_KeyGenPSO[kNumKeyGenVariants];

	ByteAddressBuffer	m_SortList;		// uint2(slot, depthKey) x maxParticles
	IndirectArgsBuffer	m_DrawArgs;		// D3D12_DRAW_ARGUMENTS, InstanceCount = 정렬 대상 수
	uint32_t			m_Capacity = 0;
};
