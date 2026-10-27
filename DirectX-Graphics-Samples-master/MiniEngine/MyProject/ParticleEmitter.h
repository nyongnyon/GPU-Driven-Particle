#pragma once
#include "VectorMath.h"
#include "ParticleTypes.h"

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

	uint32_t burstCount = 0;
	float	 burstInterval = 5.f;

	Particles::ColorMode	colorMode = Particles::ColorMode::Fixed;
	DirectX::XMFLOAT3		colorA = { 1.0f, 0.502f, 0.176f };
	DirectX::XMFLOAT3		colorB = { 1.0f, 0.85f, 0.3f };
	float					colorIntensity = 1.0f;
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
	uint32_t		ComputeEmitCount(float deltaT);
	EmitRequest		RequestEmit(float deltaT);

	uint32_t		ComputeBurstCount(float deltaT);
	void			ResetBurst();

	Math::Vector3	position;		// TEMP
	EmitterParams	emitParams;


private:
	float			m_EmitCarry = 0.f;
	double			m_BurstTimer = 0;
};