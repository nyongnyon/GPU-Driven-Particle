#include "pch.h"
#include "ParticleWorld.h"

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
#include "FullScanPool.h"
#include "CompactionPool.h"

#include <algorithm>

using namespace Graphics;
using namespace Math;
using namespace Particles;


void ParticleWorld::Initialize(const ParticleSettings& settings)
{
	m_Settings = Sanitize(settings);

	m_ParticleRenderer.Initialize();
	ApplyRenderSettings(m_Settings);
	CreatePool(m_Settings.mode, m_Settings.maxParticles);

	m_Emitters.reserve(kMaxEmitters);
	m_Requests.reserve(kMaxEmitters);

	m_EmitterGroupBuffer.Create(L"Particle Emitter Groups", kMaxEmitters, sizeof(uint32_t), m_EmitterGroups);
	m_EmitterGroupsDirty = false;
}


void ParticleWorld::Shutdown()
{
	DestroyPool();
	m_ParticleMaterial.ClearAlbedo();
	m_ParticleRenderer.Shutdown();
	m_EmitterGroupBuffer.Destroy();

	m_Emitters.clear();
	m_FreeSlots.clear();
	m_Requests.clear();
	m_ActiveCount = 0;
}

void ParticleWorld::Update(float deltaT)
{
	ApplyPending();
	UpdateDyingSlots(deltaT);

	if (m_EmitterParamsDirty)
		RebuildEmitterParams();

	if (!m_ParticlePool)
		return;

	// 이미터마다 요청을 모은다. 한 프레임 emit 합은 MAX를 넘지 않게 (앞쪽 이미터 우선)
	m_Requests.clear();
	uint32_t budget = m_ParticlePool->GetMaxParticles();
	for (EmitterSlot& slot : m_Emitters)
	{
		if (slot.state != SlotState::Active)
			continue;

		EmitRequest request = slot.emitter.RequestEmit(deltaT);
		request.emitCount = min(request.emitCount, budget);
		budget -= request.emitCount;
		m_Requests.push_back(request);
	}

	m_ParticlePool->Update(deltaT, m_Requests.data(), static_cast<uint32_t>(m_Requests.size()));
}

void ParticleWorld::Render(GraphicsContext& gfx, const Math::Camera& cam)
{
	if (!m_ParticlePool)
		return;

	// 풀이 하나 -> 모든 이미터의 파티클이 한 DrawInput에 들어 있다 (소팅 / OIT가 이미터 경계 없이 한 번)
	Particles::DrawInput in = m_ParticlePool->PrepareDraw(gfx);

	const ParticleRenderTarget target{ g_SceneColorBuffer, g_SceneDepthBuffer };

	// 전체 강제 : 예전과 같은 경로 (측정용)
	if (m_Settings.forceTechnique)
	{
		m_ParticleRenderer.Render(gfx, cam, in, m_ParticleMaterial, target);
		return;
	}

	// 이미터별 : 같은 기법끼리 한 그룹 (그룹 안에서는 이미터 경계 없이 소팅)
	UploadEmitterGroups(gfx);
	m_ParticleRenderer.RenderGroups(gfx, cam, in, m_ParticleMaterial, target,
		UsedTechniqueMask(), m_EmitterGroupBuffer);
}

EmitterHandle ParticleWorld::CreateEmitter(const Math::Vector3& position, Particles::ParticleTechnique technique)
{
	uint32_t index = EmitterHandle::kInvalid;
	if (!m_FreeSlots.empty())
	{
		index = m_FreeSlots.back();
		m_FreeSlots.pop_back();
	}
	else if (m_Emitters.size() < kMaxEmitters)
	{
		index = static_cast<uint32_t>(m_Emitters.size());
		m_Emitters.emplace_back();
	}
	else
	{
		return EmitterHandle{};
	}

	EmitterSlot& slot = m_Emitters[index];
	slot.state = SlotState::Active;
	slot.dyingTime = 0.f;
	slot.overrideColor = false;
	slot.emitter = ParticleEmitter{};
	slot.emitter.id = index;
	slot.emitter.position = position;
	slot.technique = IsEmitterTechnique(technique) ? technique : ParticleTechnique::SortedAlpha;

	m_EmitterGroups[index] = static_cast<uint32_t>(slot.technique);
	m_EmitterGroupsDirty = true;

	++m_ActiveCount;
	m_EmitterParamsDirty = true;	// 비율을 나눌 이미터 수가 바뀜

	return EmitterHandle{ index, slot.generation };
}

void ParticleWorld::DestroyEmitter(EmitterHandle handle)
{
	EmitterSlot* slot = FindSlot(handle);
	if (slot == nullptr)
		return;

	// 바로 비우지 않는다 : 이 id를 가진 파티클이 아직 살아 있으므로 최대 수명만큼 기다린 뒤 재사용
	slot->state = SlotState::Dying;
	slot->dyingTime = m_Settings.maxLife;

	--m_ActiveCount;
	m_EmitterParamsDirty = true;
}

ParticleEmitter* ParticleWorld::GetEmitter(EmitterHandle handle)
{
	EmitterSlot* slot = FindSlot(handle);
	return slot ? &slot->emitter : nullptr;
}

void ParticleWorld::SetEmitterColor(EmitterHandle handle, bool overrideColor, const DirectX::XMFLOAT3& color)
{
	EmitterSlot* slot = FindSlot(handle);
	if (slot == nullptr)
		return;

	slot->overrideColor = overrideColor;
	slot->color = color;
	m_EmitterParamsDirty = true;
}

void ParticleWorld::SetEmitterTechnique(EmitterHandle handle, Particles::ParticleTechnique technique)
{
	EmitterSlot* slot = FindSlot(handle);
	if (slot == nullptr || !IsEmitterTechnique(technique) || slot->technique == technique)
		return;

	slot->technique = technique;
	m_EmitterGroups[handle.index] = static_cast<uint32_t>(technique);
	m_EmitterGroupsDirty = true;
}

Particles::ParticleTechnique ParticleWorld::GetEmitterTechnique(EmitterHandle handle)
{
	EmitterSlot* slot = FindSlot(handle);
	return slot ? slot->technique : ParticleTechnique::SortedAlpha;
}

void ParticleWorld::ApplySettings(const ParticleSettings& settings, bool forceRecreate)
{
	m_PendingSettings = Sanitize(settings);
	m_HasPending = true;
	m_ForceRecreate = m_ForceRecreate || forceRecreate;
}

Particles::ParticleStat ParticleWorld::GetParticleStat()
{
	Particles::ParticleStat stat = {};
	if (!m_ParticlePool)
		return stat;

	stat.mode = m_ParticlePool->GetMode();
	stat.maxParticles = m_ParticlePool->GetMaxParticles();

	if (IsDefault(stat.mode))
	{
		stat.counters = static_cast<FullScanPool*>(m_ParticlePool.get())->GetCounterSnapshot();
	}

	else if (IsSlotFamily(stat.mode))
	{
		stat.counters = static_cast<SlotReusePool*>(m_ParticlePool.get())->GetCounterSnapshot();
	}

	else if (IsAliveFamily(stat.mode))
	{
		stat.counters = static_cast<AliveListPool*>(m_ParticlePool.get())->GetCounterSnapshot();
	}

	return stat;
}

void ParticleWorld::ApplyPending()
{
	if (!m_HasPending)
		return;

	const ParticleSettings& cur = m_Settings;
	const ParticleSettings& next = m_PendingSettings;

	// 1) Pool : [reset 요청, MAX 변경, 계열 변경] 중 하나라면 새로 만든다
	const bool recreate = m_ForceRecreate
		|| next.maxParticles != cur.maxParticles
		|| !IsSameFamily(next.mode, cur.mode);

	if (recreate)
	{
		DestroyPool();
		CreatePool(next.mode, next.maxParticles);

		// 풀이 비었으므로 Dying 이미터의 파티클도 사라졌다 -> 바로 재사용 가능
		for (uint32_t i = 0; i < m_Emitters.size(); ++i)
		{
			if (m_Emitters[i].state == SlotState::Dying)
				FreeSlot(i);
		}
	}
	else if (next.mode != cur.mode)
	{
		m_ParticlePool->SetMode(next.mode);	// 같은 계열: 버퍼는 그대로, 모드만
	}

	ApplyRenderSettings(next);

	m_Settings = next;
	m_HasPending = false;
	m_ForceRecreate = false;

	// 2) Emitter : 비율 -> 개수 변환이 MAX에 따라 달라지므로 항상 다시 계산
	m_EmitterParamsDirty = true;
}

void ParticleWorld::ApplyRenderSettings(const ParticleSettings& settings)
{
	m_ParticleRenderer.SetTechnique(settings.technique);

	// Layer Option의 Log/Linear 설정
	ParticleLayeredRenderer::Options& layered = m_ParticleRenderer.LayeredOptions();
	layered.sliceCount = settings.layeredSlices;
	layered.sliceMode = (settings.layeredSliceMode == 0)
		? ParticleLayeredRenderer::SliceMode::Linear : ParticleLayeredRenderer::SliceMode::Log;
}

void ParticleWorld::RebuildEmitterParams()
{
	m_EmitterParamsDirty = false;

	// 풀 전체 기준 비율을 활성 이미터 수로 나눈다 -> 이미터가 1개면 예전과 같은 값
	const EmitterParams base = BuildEmitterParams(m_Settings);
	const float share = 1.f / float(max(m_ActiveCount, 1u));

	for (EmitterSlot& slot : m_Emitters)
	{
		if (slot.state != SlotState::Active)
			continue;

		EmitterParams p = base;
		p.emitRate *= share;
		p.burstCount = uint32_t(float(base.burstCount) * share);
		if (slot.overrideColor)
		{
			p.colorMode = ColorMode::Fixed;
			p.colorA = slot.color;
		}

		slot.emitter.emitParams = p;
		slot.emitter.ResetBurst();	// 모든 이미터의 burst 주기를 맞춘다
	}
}

void ParticleWorld::UpdateDyingSlots(float deltaT)
{
	for (uint32_t i = 0; i < m_Emitters.size(); ++i)
	{
		EmitterSlot& slot = m_Emitters[i];
		if (slot.state != SlotState::Dying)
			continue;

		slot.dyingTime -= deltaT;
		if (slot.dyingTime <= 0.f)
			FreeSlot(i);
	}
}

void ParticleWorld::FreeSlot(uint32_t index)
{
	EmitterSlot& slot = m_Emitters[index];
	slot.state = SlotState::Free;
	++slot.generation;			// 예전 핸들 무효화
	m_FreeSlots.push_back(index);
}

ParticleWorld::EmitterSlot* ParticleWorld::FindSlot(EmitterHandle handle)
{
	if (!handle.IsValid() || handle.index >= m_Emitters.size())
		return nullptr;

	EmitterSlot& slot = m_Emitters[handle.index];
	if (slot.generation != handle.generation || slot.state != SlotState::Active)
		return nullptr;

	return &slot;
}

uint32_t ParticleWorld::UsedTechniqueMask() const
{
	uint32_t mask = 0;
	for (const EmitterSlot& slot : m_Emitters)
	{
		if (slot.state != SlotState::Free)
			mask |= TechniqueBit(slot.technique);
	}
	return mask;
}

void ParticleWorld::UploadEmitterGroups(GraphicsContext& gfx)
{
	if (!m_EmitterGroupsDirty)
		return;

	gfx.TransitionResource(m_EmitterGroupBuffer, D3D12_RESOURCE_STATE_COPY_DEST, true);
	gfx.WriteBuffer(m_EmitterGroupBuffer, 0, m_EmitterGroups, sizeof(m_EmitterGroups));
	gfx.TransitionResource(m_EmitterGroupBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
	m_EmitterGroupsDirty = false;
}

void ParticleWorld::CreatePool(Particles::Mode mode, uint32_t maxParticles)
{
	if (IsDefault(mode))
		m_ParticlePool = std::make_unique<FullScanPool>();
	else if (IsSlotFamily(mode))
		m_ParticlePool = std::make_unique<SlotReusePool>(mode);
	else
		m_ParticlePool = std::make_unique<AliveListPool>(mode);

	m_ParticlePool->Initialize(maxParticles);
}

void ParticleWorld::DestroyPool()
{
	if (!m_ParticlePool)
		return;

	Graphics::g_CommandManager.IdleGPU();	// 이전 프레임들이 아직 버퍼를 쓰고 있을 수 있음
	m_ParticlePool->Shutdown();
	m_ParticlePool.reset();					// 새 풀을 만들기 전에 먼저 해제 (VRAM 두 배 방지)
}
