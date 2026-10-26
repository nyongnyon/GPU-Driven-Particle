#pragma once

#include <cstdint>

class ByteAddressBuffer;

namespace Particles
{
	enum class DrawSource : uint32_t
	{
		FullScan = 0,
		SlotReuse,
		SlotReuseFlags,
		AliveList,
		COUNT
	};

	enum class ParticleTechnique : uint32_t
	{
		Additive = 0,
		PremultipliedAlpha,
		WeightedOIT,
		COUNT
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
		default: return "Unknown";
		}
	}
}