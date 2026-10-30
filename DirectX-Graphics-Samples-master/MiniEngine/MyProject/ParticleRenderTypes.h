#pragma once
#include "ParticleTypes.h"
#include <cstdint>

class ByteAddressBuffer;
class StructuredBuffer;

namespace Particles
{
	enum class DrawSource : uint32_t
	{
		FullScan = 0,
		SlotReuse,
		SlotReuseFlags,
		AliveList,
		Sorted,
		COUNT
	};

	enum class ParticleTechnique : uint32_t
	{
		Additive = 0,
		PremultipliedAlpha,
		WeightedOIT,		// 레거시 : 전체 강제 모드에서만 (이미터별 선택 불가)
		SortedAlpha,	// draw 직전 GPU 깊이 정렬(back-to-front) + alpha blending
		TiledAlpha,
		TiledLayeredWBOIT,	// 레거시 : 전체 강제 모드에서만 (이미터별 선택 불가)
		COUNT
	};

	// 이미터마다 고를 수 있는 기법 (WBOIT 계열은 레거시라 제외)
	inline bool IsEmitterTechnique(ParticleTechnique t)
	{
		return t == ParticleTechnique::Additive || t == ParticleTechnique::PremultipliedAlpha
			|| t == ParticleTechnique::SortedAlpha || t == ParticleTechnique::TiledAlpha;
	}

	// 이미터별 기법 그룹을 그리는 고정 순서. 그룹 사이의 앞뒤는 섞이지 않으므로 순서라도 매번 같게
	static const ParticleTechnique kGroupDrawOrder[] =
	{
		ParticleTechnique::Additive,
		ParticleTechnique::PremultipliedAlpha,
		ParticleTechnique::SortedAlpha,
		ParticleTechnique::TiledAlpha,
	};

	inline uint32_t TechniqueBit(ParticleTechnique t) { return 1u << static_cast<uint32_t>(t); }

	// KeyGen 필터 : groupTable[Particle.emitterId] == group 인 파티클만 남긴다
	struct GroupFilter
	{
		StructuredBuffer*	groupTable = nullptr;	// uint x ParticleWorld::kMaxEmitters (값 = ParticleTechnique)
		uint32_t			group = 0;
	};

	struct DrawInput
	{
		DrawSource source = DrawSource::FullScan;
		ByteAddressBuffer* indirectBuffer = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE srvs[8];
		uint32_t					numSrvs = 0;
		uint32_t					maxParticle = 0;
		uint32_t					argsOffset = kDrawArgs;
	};

	inline const char* TechniqueName(ParticleTechnique technique)
	{
		switch (technique)
		{
		case ParticleTechnique::Additive: return "Additive";
		case ParticleTechnique::PremultipliedAlpha: return "PremultipliedAlpha";
		case ParticleTechnique::WeightedOIT: return "WeightedOIT";
		case ParticleTechnique::SortedAlpha: return "SortedAlpha";
		case ParticleTechnique::TiledAlpha: return "TiledAlpha";
		case ParticleTechnique::TiledLayeredWBOIT: return "TiledLayeredWBOIT";
		default: return "Unknown";
		}
	}
}