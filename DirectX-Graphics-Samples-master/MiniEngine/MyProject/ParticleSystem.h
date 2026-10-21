#pragma once

#include "GpuBuffer.h"
#include "ReadbackBuffer.h"
#include "VectorMath.h"
#include "ParticleTypes.h"
#include "ParticlePool.h"
#include "ParticleEmitter.h"

class GraphicsContext;
namespace Math { class Camera; }

class ParticleSystem
{
public:
	void InitializePool(Particles::Mode mode, uint32_t maxParticles = 0);
	void InitializeEmitter(const Math::Vector3& emitterPosition, const EmitterParams& emitParams);
	void Shutdown();

	void Update(float deltaT);
	void Render(GraphicsContext& gfx, const Math::Camera& cam);

	void RequestMode(Particles::Mode mode);

	Particles::ParticleStat GetParticleStat();

private:
	void ApplyPendingMode();

	// -----------------------
	//		Particle Setting
	// -----------------------
	std::unique_ptr<ParticlePool> m_ParticlePool;
	ParticleEmitter				  m_ParticleEmitter;
	Particles::Mode				  m_PendingMode = Particles::Mode::NONE;
};

