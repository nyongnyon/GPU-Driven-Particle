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
	if (IsSlotFamily(mode))
	{
		m_ParticlePool = std::move(std::make_unique<SlotReusePool>(mode));
	}
	else if (IsAliveFamily(mode))
	{
		m_ParticlePool = std::move(std::make_unique<AliveListPool>(mode));
	}

	m_ParticlePool->Initialize(maxParticles);
}

void ParticleSystem::InitializeEmitter(const Math::Vector3& emitterPosition, const EmitterParams& emitParams)
{
	m_ParticleEmitter.emitParams = emitParams;
	m_ParticleEmitter.position = emitterPosition;
	m_ParticleEmitter.ResetBurst();
}

void ParticleSystem::Shutdown()
{
	m_ParticlePool->Shutdown();
}

void ParticleSystem::Update(float deltaT)
{
	// if has request mode change
	ApplyPendingMode();
	ApplyPendingRecreate();

	EmitRequest request = m_ParticleEmitter.RequestEmit(deltaT);

	request.emitCount = min(request.emitCount, m_ParticlePool->GetMaxParticles());

	m_ParticlePool->Update(deltaT, request);
}

void ParticleSystem::Render(GraphicsContext& gfx, const Math::Camera& cam)
{
	m_ParticlePool->Render(gfx, cam);
}

void ParticleSystem::RequestMode(Particles::Mode mode)
{
	if (mode != m_ParticlePool->GetMode())
		m_PendingMode = mode;
}

void ParticleSystem::RequestRecreate(uint32_t maxParticles)
{
	m_RecreateRequested = true;
	m_PendingMax = maxParticles;
}

Particles::ParticleStat ParticleSystem::GetParticleStat()
{
	Particles::ParticleStat stat = {};
	stat.mode = m_ParticlePool->GetMode();
	stat.maxParticles = m_ParticlePool->GetMaxParticles();

	if (stat.mode == Particles::Mode::SlotReuse ||
		stat.mode == Particles::Mode::SlotReuseFlags )
	{
		stat.counters = static_cast<SlotReusePool*>(m_ParticlePool.get())->GetCounterSnapshot();
	}

	else if (stat.mode == Particles::Mode::AliveDirect
		|| stat.mode == Particles::Mode::AliveIndirect
		|| stat.mode == Particles::Mode::AliveIndirectWave)
	{
		stat.counters = static_cast<AliveListPool*>(m_ParticlePool.get())->GetCounterSnapshot();
	}

	return stat;
}

void ParticleSystem::ApplyPendingMode()
{
	if (m_PendingMode == Mode::NONE)
		return;

	Mode curMode = m_ParticlePool->GetMode();

	if (IsSameFamily(curMode, m_PendingMode))
	{
		m_ParticlePool->SetMode(m_PendingMode);
	}
	else
	{
		const uint32_t maxParticles = m_ParticlePool->GetMaxParticles();
		Graphics::g_CommandManager.IdleGPU(); // GPU의 사용이 끝날때까지 대기...
		m_ParticlePool->Shutdown();
		m_ParticlePool.reset();
		InitializePool(m_PendingMode, maxParticles); // 새로운 풀 생성
	}	

	m_PendingMode = Mode::NONE;
}

void ParticleSystem::ApplyPendingRecreate()
{
	if (!m_RecreateRequested)
		return;

	Mode mode = m_ParticlePool->GetMode();

	Graphics::g_CommandManager.IdleGPU(); // GPU의 사용이 끝날때까지 대기...
	m_ParticlePool->Shutdown();
	m_ParticlePool.reset();
	InitializePool(mode, m_PendingMax);

	m_RecreateRequested = false;
}

