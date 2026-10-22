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
	void RequestRecreate(uint32_t maxParticles);

	Particles::ParticleStat GetParticleStat();

private:
	void ApplyPendingMode();
	void ApplyPendingRecreate();

	// -----------------------
	//		Particle Setting
	// -----------------------
	std::unique_ptr<ParticlePool> m_ParticlePool;
	ParticleEmitter				  m_ParticleEmitter;

	// -----------------------
	//	For Pending Request
	// -----------------------
	bool						  m_RecreateRequested = false;
	uint32_t					  m_PendingMax = 0;
	Particles::Mode				  m_PendingMode = Particles::Mode::NONE;
};

