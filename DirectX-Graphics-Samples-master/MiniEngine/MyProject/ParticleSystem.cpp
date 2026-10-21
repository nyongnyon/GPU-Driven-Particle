#include "pch.h"
#include "ParticleSystem.h"

#include "GraphicsCore.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "EngineTuning.h"
#include "EngineProfiling.h"
#include "Camera.h"

#include "ParticleUtility.h"
#include "ParticlePool.h"
#include "SlotReusePool.h"
#include "AliveListPool.h"

#include <algorithm>

using namespace Graphics;
using namespace Math;
using namespace Particles;


void ParticleSystem::InitializePool(Particles::Mode mode, uint32_t maxParticles)
{
	if (mode == Mode::SlotReuse || mode == Mode::SlotReuseFlags)
	{
		m_ParticlePool = std::move(std::make_unique<SlotReusePool>(mode));
	}
	else if (mode == Mode::AliveDirect)
	{
		m_ParticlePool = std::move(std::make_unique<AliveListPool>());
	}

	m_ParticlePool->Initialize(maxParticles);
}

void ParticleSystem::InitializeEmitter(const Math::Vector3& emitterPosition, const EmitterParams& emitParams)
{
	m_ParticleEmitter.emitParams = emitParams;
	m_ParticleEmitter.position = emitterPosition;
}

void ParticleSystem::Shutdown()
{
	m_ParticlePool->Shutdown();
}

void ParticleSystem::Update(float deltaT)
{
	m_ParticlePool->Update(deltaT, m_ParticleEmitter.RequestEmit(deltaT));
}

void ParticleSystem::Render(GraphicsContext& gfx, const Math::Camera& cam)
{
	m_ParticlePool->Render(gfx, cam);
}

void ParticleSystem::SetMode(Particles::Mode mode)
{
	Particles::Mode originMode = m_ParticlePool->GetMode();

	if (mode == originMode)
		return;

	if ((mode == Particles::Mode::SlotReuseFlags && originMode == Particles::Mode::SlotReuse)
		|| (mode == Particles::Mode::SlotReuse && originMode == Particles::Mode::SlotReuseFlags))
	{
		static_cast<SlotReusePool*>(m_ParticlePool.get())->ChangeMode();
	}

}

Particles::ParticleStat ParticleSystem::GetParticleStat()
{
	Particles::ParticleStat stat = {};
	stat.mode = m_ParticlePool->GetMode();
	stat.maxParticles = m_ParticlePool->GetMaxParticles();

	if (stat.mode == Particles::Mode::SlotReuse ||
		stat.mode == Particles::Mode::SlotReuseFlags)
	{
		stat.counters = static_cast<SlotReusePool*>(m_ParticlePool.get())->GetCounterSnapshot();
	}

	return stat;
}

