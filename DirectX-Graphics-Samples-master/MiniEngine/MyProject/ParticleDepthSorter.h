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
	// filter != nullptr : 그 그룹(이미터 기법)에 속한 파티클만 남긴다
	Particles::DrawInput Sort(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& input, const Particles::GroupFilter* filter = nullptr);

	// KeyGen만 수행 (BitonicSort 생략) : 살아있고 카메라 앞에 있는 파티클의 (slot, depth)를 순서 없이 모은다
	// TiledLayeredWBOIT처럼 전역 정렬이 필요 없는 기법용. 반환 형식은 Sort()와 같다
	Particles::DrawInput Compact(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& input, const Particles::GroupFilter* filter = nullptr);
private:
	enum KeyGenVariant : uint32_t
	{
		kKeyGenReuse = 0,	// FullScan / SlotReuse
		kKeyGenReuseFlags,	// SlotReuseFlags
		kKeyGenAlive,		// AliveList
		kNumKeyGenVariants
	};

	Particles::DrawInput Build(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& input, bool sort, const Particles::GroupFilter* filter);

	static KeyGenVariant SelectKeyGen(Particles::DrawSource source);
	void EnsureCapacity(uint32_t maxParticles);

	RootSignature		m_KeyGenSignature;
	ComputePSO			m_KeyGenPSO[kNumKeyGenVariants];

	ByteAddressBuffer	m_SortList;		// uint2(slot, depthKey) x maxParticles
	StructuredBuffer	m_NoFilterTable;	// 필터 없을 때 t3에 묶는 빈 테이블 (셰이더는 읽지 않음)
	IndirectArgsBuffer	m_DrawArgs;		// D3D12_DRAW_ARGUMENTS, InstanceCount = 정렬 대상 수
	uint32_t			m_Capacity = 0;
};
