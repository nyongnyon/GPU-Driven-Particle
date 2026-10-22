#pragma once
#include "ParticlePool.h"
#include "GpuBuffer.h"
#include "ReadbackBuffer.h"

class CompactionPool : public ParticlePool
{
public:
	CompactionPool(Particles::Mode mode = Particles::Mode::Compaction)
		: ParticlePool(mode) {	}
public:
	void Initialize(uint32_t maxParticles) override;
	void CreateBuffers(uint32_t maxParticles) override;
	void Shutdown() override;

	void Update(float deltaT, const EmitRequest& emitRequest) override;
	void Render(GraphicsContext& gfx, const Math::Camera& cam) override;

	static constexpr uint32_t	sThreadGroupSize = 64;

	// -----------------------
	//			buffer
	// -----------------------
	StructuredBuffer	m_ParticleBuffer;
	StructuredBuffer	m_DeadListBuffer;
	StructuredBuffer	m_AliveListBuffer;
	ByteAddressBuffer	m_CountBuffer;
	ByteAddressBuffer	m_IndirectArgsBuffer;
};

