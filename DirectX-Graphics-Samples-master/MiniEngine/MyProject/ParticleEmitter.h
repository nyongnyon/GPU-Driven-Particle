#pragma once
#include "VectorMath.h"

struct EmitterParams
{
	EmitterParams() {}

	float minLife = 1.f;
	float maxLife = 3.f;
	float minSpeed = 1.f;
	float maxSpeed = 4.f;
	float minSize = 0.03f;
	float maxSize = 0.05f;
	float emitRate = 30000.f;
};

struct EmitRequest
{
	Math::Vector3 emitPosition;
	uint32_t	  emitCount;
	EmitterParams emitParams;
};

class ParticleEmitter
{
public:
	float			ComputeEmitCount(float deltaT);
	EmitRequest		RequestEmit(float deltaT);


	Math::Vector3	position;		// TEMP
	EmitterParams	emitParams;


private:
	float			m_EmitCarry = 0.f;
};