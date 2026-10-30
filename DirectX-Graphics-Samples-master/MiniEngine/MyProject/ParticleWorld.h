#pragma once

#include "GpuBuffer.h"
#include "ReadbackBuffer.h"
#include "VectorMath.h"
#include "ParticleTypes.h"
#include "ParticlePool.h"
#include "ParticleEmitter.h"
#include "ParticleRenderer.h"
#include "ParticleMaterial.h"
#include "ParticleSettings.h"

#include <vector>

class GraphicsContext;
namespace Math { class Camera; }

// 이미터를 가리키는 핸들. 슬롯이 재사용되면 generation이 달라져 예전 핸들은 무효가 된다
struct EmitterHandle
{
	static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

	uint32_t index = kInvalid;
	uint32_t generation = 0;

	bool IsValid() const { return index != kInvalid; }
};

// 파티클 월드 : 풀 하나 + 렌더러 하나를 여러 이미터가 같이 쓴다
//  - Update : 이미터마다 EmitRequest를 모아 풀에 넘긴다 (Emit = 이미터별 dispatch, Simulate = 풀 전체에 한 번)
//  - Render : 풀이 하나이므로 소팅, WBOIT clear / composite도 프레임당 한 번
//  - steady / burst 비율은 풀 전체 기준이고, 활성 이미터 수로 나눠 각 이미터에 준다
//  - 렌더 기법 : forceTechnique면 전체가 하나, 아니면 이미터마다 (같은 기법끼리 묶어 kGroupDrawOrder 순서로)
class ParticleWorld
{
public:
	static constexpr uint32_t kMaxEmitters = 256;

	void Initialize(const ParticleSettings& settings);
	void Shutdown();

	void Update(float deltaT);
	void Render(GraphicsContext& gfx, const Math::Camera& cam);

	// -----------------------
	//		Emitter
	// -----------------------
	EmitterHandle		CreateEmitter(const Math::Vector3& position,		// 슬롯이 없으면 invalid 핸들
							Particles::ParticleTechnique technique = Particles::ParticleTechnique::SortedAlpha);
	void				DestroyEmitter(EmitterHandle handle);				// emit만 멈추고, 남은 파티클은 수명대로 산다
	ParticleEmitter*	GetEmitter(EmitterHandle handle);					// 무효 / 삭제된 핸들이면 nullptr
	void				SetEmitterColor(EmitterHandle handle, bool overrideColor, const DirectX::XMFLOAT3& color = { 1.f, 1.f, 1.f });
	uint32_t			GetActiveEmitterCount() const { return m_ActiveCount; }

	// 이미터별 기법 (IsEmitterTechnique()만, 아니면 무시). forceTechnique가 꺼져 있을 때 쓰인다
	void							SetEmitterTechnique(EmitterHandle handle, Particles::ParticleTechnique technique);
	Particles::ParticleTechnique	GetEmitterTechnique(EmitterHandle handle);

	// -----------------------
	//		Settings
	// -----------------------
	void ApplySettings(const ParticleSettings& settings, bool forceRecreate = false);

	const ParticleSettings& GetSettings() const { return m_Settings; }
	ParticleMaterial&		GetMaterial() { return m_ParticleMaterial; }
	ParticleRenderer&		GetRenderer() { return m_ParticleRenderer; }
	Particles::ParticleStat	GetParticleStat();

private:
	enum class SlotState : uint8_t { Free, Active, Dying };

	struct EmitterSlot
	{
		ParticleEmitter		emitter;
		SlotState			state = SlotState::Free;
		uint32_t			generation = 0;
		float				dyingTime = 0.f;	// Dying : 이 시간이 지나면 이 이미터의 파티클이 모두 죽었으므로 슬롯 재사용

		bool				overrideColor = false;
		DirectX::XMFLOAT3	color = { 1.f, 1.f, 1.f };

		Particles::ParticleTechnique technique = Particles::ParticleTechnique::SortedAlpha;	// Dying 동안에도 유지 (남은 파티클을 그 기법으로)
	};

	void ApplyPending();
	void ApplyRenderSettings(const ParticleSettings& settings);
	void RebuildEmitterParams();		// 설정 / 활성 이미터 수 / 색이 바뀌면
	void UpdateDyingSlots(float deltaT);
	void FreeSlot(uint32_t index);
	EmitterSlot* FindSlot(EmitterHandle handle);

	uint32_t	UsedTechniqueMask() const;		// Active + Dying 이미터가 쓰는 기법들 (TechniqueBit)
	void		UploadEmitterGroups(GraphicsContext& gfx);

	void CreatePool(Particles::Mode mode, uint32_t maxParticles);
	void DestroyPool();

	// -----------------------
	//		Particle Setting
	// -----------------------
	std::unique_ptr<ParticlePool>	m_ParticlePool;
	ParticleRenderer				m_ParticleRenderer;
	ParticleMaterial				m_ParticleMaterial;

	// -----------------------
	//		Emitters
	// -----------------------
	std::vector<EmitterSlot>		m_Emitters;		// index = Particle.emitterId
	std::vector<uint32_t>			m_FreeSlots;
	std::vector<EmitRequest>		m_Requests;		// 매 프레임 재사용
	uint32_t						m_ActiveCount = 0;
	bool							m_EmitterParamsDirty = false;

	// emitterId -> 기법 테이블 (KeyGen 필터가 읽는다). 바뀐 프레임에만 다시 올린다
	StructuredBuffer				m_EmitterGroupBuffer;
	alignas(16) uint32_t			m_EmitterGroups[kMaxEmitters] = {};
	bool							m_EmitterGroupsDirty = false;

	// -----------------------
	//		Settings
	// -----------------------
	ParticleSettings	m_Settings;
	ParticleSettings	m_PendingSettings;
	bool				m_HasPending = false;
	bool				m_ForceRecreate = false;
};
