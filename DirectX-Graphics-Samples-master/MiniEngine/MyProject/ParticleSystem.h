#pragma once

#include "GpuBuffer.h"
#include "ReadbackBuffer.h"
#include "VectorMath.h"
#include "ParticleTypes.h"

class GraphicsContext;
namespace Math { class Camera; }

class ParticleSystem
{
public:
	static void InitializeShared();

	void Initialize(uint32_t maxParticles = 0);
	void Shutdown();

	void Update(float deltaT, const Math::Vector3& emitterPosition);
	void Render(GraphicsContext& gfx, const Math::Camera& cam);

	Particles::ParticleStat GetStat()
	{
		Particles::ParticleStat stat = {};
		stat.maxParticles = m_MaxParticles;
		stat.initVerified = m_initVerified;
		stat.counters = m_LastCounters;

		return stat;
	}

private:
	void CreateBuffers();
	bool VerifyInitialState();

	uint32_t ComputeEmitCount(float deltaT);
	void CollectCounterReadback();

private:
	static constexpr uint32_t sReadbackLatency = 3;
	static constexpr uint32_t sThreadGroupSize = 64;

	// -----------------------
	//		Particle Setting
	// -----------------------
	uint32_t			m_MaxParticles = 0;
	Particles::Mode		m_Mode = Particles::Mode::SlotReuse;

	float				m_EmitCarry = 0.f;
	uint32_t			m_FrameIndex = 0;
	uint32_t			m_LastEmitRequest = 0;

	// -----------------------
	//			buffer
	// -----------------------
	StructuredBuffer	m_ParticleBuffer;
	StructuredBuffer	m_DeadListBuffer;
	ByteAddressBuffer	m_CounterBuffer;

	// -----------------------
	//		Readback buffer
	// -----------------------
	ReadbackBuffer		m_CounterReadback[sReadbackLatency];
	uint64_t			m_ReadbackFence[sReadbackLatency] = {};
	uint32_t			m_ReadbackSlot = 0;

	Particles::CounterSnapshot		m_LastCounters = {};
	bool				m_HasCounter = false;
	bool				m_initVerified = false;

};

