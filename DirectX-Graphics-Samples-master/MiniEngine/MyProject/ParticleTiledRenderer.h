#pragma once
#include "ColorBuffer.h"
#include "GpuBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ParticleRenderTypes.h"
#include "RenderTechnique.h"
#include "ReadbackBuffer.h"

class GraphicsContext;
struct ParticleMaterial;
namespace Math { class Camera; }

class ParticleTiledRenderer
{
public:
	struct TileStats
	{
		uint64_t pairs = 0;     
		uint32_t maxPerTile = 0;
		uint32_t emptyTiles = 0;
		uint32_t tiles = 0;
		uint32_t particles = 0;
		bool     valid = false;
	};

	static constexpr uint32_t kTileSize = 16;

	void Initialize();
	void Shutdown();

	void Render(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& sorted, const ParticleMaterial& material,
		const ParticleRenderTarget& target);

private:
	void EnsureSize(uint32_t width, uint32_t height);
	void EnsureCapacity(uint32_t maxParticles);
	void EnsureBatchMask(uint32_t maxParticles, uint32_t tiles);

	void CollectStats();

	RootSignature	m_TileSig;
	ComputePSO		m_PrepPSO{ L"Particle Tile Prep CS" };
	ComputePSO		m_TilePSO{ L"Particle Tiled Render CS" };
	ComputePSO		m_CullOnlyPSO{ L"Particle Tiled CullOnly CS" };

	RootSignature	m_CompositeSig;
	GraphicsPSO		m_CompositePSO{ L"Particle Tiled Composite PSO" };
	
	StructuredBuffer	m_TiledParticles;
	ByteAddressBuffer	m_TiledBatchMask;
	ColorBuffer			m_Result;	// rgb = premultiplied color / a =  1 - Åõ°ú

	bool				m_CullOnly = false;
	uint32_t			m_Capacity = 0;
	uint32_t			m_BatchMaskWords = 0;
	float				m_SoftDistance = 0.f;
	
	//----------------------------
	//			Read back
	//----------------------------
	static constexpr uint32_t kLatency = 3;
	ByteAddressBuffer	m_TileHits;
	ReadbackBuffer		m_StatsReadback[kLatency];
	uint64_t          m_ReadbackFence[kLatency] = {};
	uint32_t          m_ReadbackTiles[kLatency] = {};
	uint32_t          m_StatsSlot = 0;
	int32_t           m_PendingSlot = -1;
	TileStats         m_LastStats;
};

