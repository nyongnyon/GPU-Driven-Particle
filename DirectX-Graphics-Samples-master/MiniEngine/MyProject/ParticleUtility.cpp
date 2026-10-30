#include "pch.h"
#include "ParticleUtility.h"

namespace Particles
{
	uint32_t HashU32(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
		return x;
	}

	Simconstants BuildFrameConstants(float deltaT, uint32_t maxParticles)
	{
		Simconstants sc = {};
		sc.deltaTime = deltaT;
		sc.gravity = { 0.f, -2.f, 0.f };			// TEMP
		sc.maxParticles = maxParticles;
		return sc;
	}

	void FillEmitConstants(Simconstants& sc, const EmitRequest& request, uint32_t frameIndex)
	{
		DirectX::XMStoreFloat3(&sc.emitterPos, request.emitPosition);
		sc.emitCount = request.emitCount;
		sc.minLife = request.emitParams.minLife;
		sc.maxLife = request.emitParams.maxLife;
		sc.minSpeed = 1.f;
		sc.maxSpeed = 4.f;
		sc.particleSize = request.emitParams.minSize;	// TEMP
		sc.randomSeed = HashU32(frameIndex + 1) ^ HashU32(request.emitterId * 0x9E3779B9u);
		sc.emitterId = request.emitterId;
		FillColorConstants(sc, request.emitParams);	// colorMode, colorA, colorB
	}

	float ComputeEmitRateByAliveRatio(float aliveRatio, float minLife, float maxLife, uint32_t maxParticles)
	{
		float averageLife = max(0.5f * (minLife + maxLife), 0.01f);
		
		return aliveRatio * (float)maxParticles / averageLife;
	}

}