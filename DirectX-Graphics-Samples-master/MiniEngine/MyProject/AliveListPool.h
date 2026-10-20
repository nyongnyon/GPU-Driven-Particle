#pragma once
#include "ParticlePool.h"

#include "GpuBuffer.h"
#include "ReadbackBuffer.h"

class AliveListPool : public ParticlePool
{
public:
	AliveListPool() : ParticlePool(Particles::Mode::AliveDirect) {}

public:
	// IParticlePool을(를) 통해 상속됨
	void Initialize(uint32_t maxParticles) override;
	void CreateBuffers(uint32_t maxParticles) override;
	void Shutdown() override;

	void Update(float deltaT, const Math::Vector3& emitterPosition) override;
	void Render(GraphicsContext& gfx, const Math::Camera& cam) override;

private:
	bool	VerifyInitialState();
	
	static constexpr uint32_t sThreadGroupSize = 64;

	float	m_EmitCarry = 0.f;

	// -----------------------
	//			buffer
	// -----------------------
	StructuredBuffer	m_ParticleBuffer;
	StructuredBuffer	m_DeadListBuffer;
	StructuredBuffer	m_AliveListBuffer[2]; // In / Out (Double Buffering ?)
	ByteAddressBuffer	m_CounterBuffer;

	// -----------------------
	//		Readback buffer
	// -----------------------
	ReadbackBuffer		m_CounterReadback[sReadbackLatency];
	uint64_t			m_ReadbackFence[sReadbackLatency] = {};
	uint32_t			m_ReadbackSlot = 0;

	Particles::CounterSnapshot m_LastCounters = {};

	bool				m_initVerified = false;
};

