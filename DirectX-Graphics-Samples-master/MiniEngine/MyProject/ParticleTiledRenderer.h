#pragma once
#include "ColorBuffer.h"
#include "GpuBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ParticleRenderTypes.h"
#include "RenderTechnique.h"

class GraphicsContext;
struct ParticleMaterial;
namespace Math { class Camera; }

class ParticleTiledRenderer
{
public:
	static constexpr uint32_t kTileSize = 16;

	void Initialize();
	void Shutdown();

	// sorted : ParticleDepthSorter::Sort()의 결과 (t0 = particles, t1 = 정렬 목록, indirectBuffer = 개수)
	void Render(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& sorted, const ParticleMaterial& material,
		const ParticleRenderTarget& target);

private:
	void EnsureSize(uint32_t width, uint32_t height);
	void EnsureCapacity(uint32_t maxParticles);

	RootSignature	m_TileSig;
	ComputePSO		m_PrepPSO{ L"Particle Tile Prep CS" };
	ComputePSO		m_TilePSO{ L"Particle Tiled Render CS" };

	StructuredBuffer	m_TiledParticles;
	uint32_t			m_Capacity = 0;

	RootSignature	m_CompositeSig;
	GraphicsPSO		m_CompositePSO{ L"Particle Tiled Composite PSO" };

	ColorBuffer		m_Result;	// rgb = premultiplied color / a =  1 - 투과
};

