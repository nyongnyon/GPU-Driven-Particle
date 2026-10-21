#include "pch.h"
#include "ParticleEmitter.h"

float ParticleEmitter::ComputeEmitCount(float deltaT)
{
	const float want = emitParams.emitRate * deltaT + m_EmitCarry;
	const uint32_t count = (uint32_t)want;
	m_EmitCarry = want - float(count);
	return max(count, 0.f);
}

EmitRequest ParticleEmitter::RequestEmit(float deltaT)
{
	EmitRequest ret = {};
	ret.emitPosition = position;
	ret.emitCount = ComputeEmitCount(deltaT);
	ret.emitParams = emitParams;

	return ret;
}
