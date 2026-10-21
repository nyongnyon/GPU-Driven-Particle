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

	void Update(float deltaT, const EmitRequest& emitRequest) override;
	void Render(GraphicsContext& gfx, const Math::Camera& cam) override;

private:
#ifdef _DEBUG
	void	CollectCounterReadback();
#endif
	bool	VerifyInitialState();
	void	InsertUAVBarriers(ComputeContext& ctx);

	StructuredBuffer& AliveIn() { return m_AliveListBuffer[m_AliveCurrent]; }
	StructuredBuffer& AliveOut() { return m_AliveListBuffer[1 - m_AliveCurrent]; }

	static constexpr uint32_t sThreadGroupSize = 64;

	// -----------------------
	//			buffer
	// -----------------------
	StructuredBuffer	m_ParticleBuffer;
	StructuredBuffer	m_DeadListBuffer;
	StructuredBuffer	m_AliveListBuffer[2]; // In / Out (ping-pong)
	ByteAddressBuffer	m_CounterBuffer;

	uint32_t			m_AliveCurrent = 0;

	// -----------------------
	//		Readback buffer
	// -----------------------
	ReadbackBuffer		m_CounterReadback[sReadbackLatency];
	uint64_t			m_ReadbackFence[sReadbackLatency] = {};
	uint32_t			m_ReadbackSlot = 0;

	Particles::CounterSnapshot m_LastCounters = {};

	bool				m_initVerified = false;
};

