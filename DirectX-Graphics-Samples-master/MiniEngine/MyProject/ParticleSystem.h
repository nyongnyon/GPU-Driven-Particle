#pragma once

#include "GpuBuffer.h"
#include "ReadbackBuffer.h"
#include "VectorMath.h"
#include "ParticleTypes.h"
#include "IParticlePool.h"

class GraphicsContext;
namespace Math { class Camera; }

class ParticleSystem
{
public:
	void Initialize(Particles::Mode mode, uint32_t maxParticles = 0);
	void Shutdown();

	void Update(float deltaT, const Math::Vector3& emitterPosition);
	void Render(GraphicsContext& gfx, const Math::Camera& cam);

	void SetMode(Particles::Mode mode);

	Particles::ParticleStat GetParticleStat();

private:
	// -----------------------
	//		Particle Setting
	// -----------------------
	std::unique_ptr<IParticlePool> m_ParticlePool;
};

