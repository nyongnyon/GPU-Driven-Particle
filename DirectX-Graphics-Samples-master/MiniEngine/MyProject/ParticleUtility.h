#pragma once
#include "ParticleTypes.h"
#include "ParticleEmitter.h"

namespace Particles
{
	inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
	}

	inline void FillColorConstants(Simconstants& sc, const EmitterParams& p)
	{
		const float i = p.colorIntensity;
		sc.colorMode = static_cast<uint32_t>(p.colorMode);
		sc.colorA = { p.colorA.x * i, p.colorA.y * i, p.colorA.z * i, i };
		sc.colorB = { p.colorB.x * i, p.colorB.y * i, p.colorB.z * i, i };
	}

	uint32_t HashU32(uint32_t x);

	// Emit / Simulate dispatch에 공통으로 들어가는 값 (emitCount = 0 : 이미터 값 없음)
	Simconstants BuildFrameConstants(float deltaT, uint32_t maxParticles);

	// 이미터 하나의 Emit dispatch용 값을 frame 값 위에 채운다
	// seed는 이미터마다 다르게 (id 0 = 이미터 하나일 때와 같은 seed)
	void FillEmitConstants(Simconstants& sc, const EmitRequest& request, uint32_t frameIndex);

	float ComputeEmitRateByAliveRatio(float aliveRatio, float minLife, float maxLife, uint32_t maxParticles);
}
