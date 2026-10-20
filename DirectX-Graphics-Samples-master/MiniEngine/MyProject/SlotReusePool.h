#pragma once
#include "IParticlePool.h"

#include "GpuBuffer.h"
#include "ReadbackBuffer.h"

class SlotReusePool : public IParticlePool
{
public:
	SlotReusePool(Particles::Mode mode) { m_Mode = mode; }

public:
	virtual void Initialize(uint32_t maxParticles) override;
	virtual void CreateBuffers(uint32_t maxParticles) override;
	virtual void Shutdown() override;

	virtual void Update(float deltaT, const Math::Vector3& emitterPosition) override;
	virtual void Render(GraphicsContext& gfx, const Math::Camera& cam) override;

	Particles::CounterSnapshot GetCounterSnapshot() const { return m_LastCounters; }
	
	void ChangeMode();

private:
#ifdef _DEBUG
	void CollectCounterReadback();
#endif

	bool		 VerifyInitialState(); // Initialize시에 Buffer 설정이 정확한지 확인하는 함수
	uint32_t	 ComputeEmitCount(float deltaT);

	static constexpr uint32_t sThreadGroupSize = 64;
	
	float				m_EmitCarry = 0.f;

	// -----------------------
	//			buffer
	// -----------------------
	StructuredBuffer	m_ParticleBuffer;
	StructuredBuffer	m_DeadListBuffer;
	ByteAddressBuffer	m_CounterBuffer;
	StructuredBuffer	m_AliveFlagBuffer; // For Slot Reuse 'Flag' Version

	// -----------------------
	//		Readback buffer
	// -----------------------
	ReadbackBuffer		m_CounterReadback[sReadbackLatency];
	uint64_t			m_ReadbackFence[sReadbackLatency] = {};
	uint32_t			m_ReadbackSlot = 0;

	Particles::CounterSnapshot m_LastCounters = {};

	bool				m_initVerified = false;
};

