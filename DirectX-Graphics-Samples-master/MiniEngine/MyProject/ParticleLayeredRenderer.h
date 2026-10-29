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


// Clear -> Prep(quad + depth min/max) -> Count -> Scan -> Write -> Render(Å¸ÀÏ) -> Composite
class ParticleLayeredRenderer
{
public:
	static constexpr uint32_t kTileSize = 16;
	static constexpr uint32_t kMaxSlices = 64;

	enum class SliceMode : uint32_t { Linear = 0, Log = 1 };

	struct Options
	{
		uint32_t	sliceCount = 16;
		SliceMode	sliceMode = SliceMode::Log;
		bool		debugView = false;
	};

	void Initialize();
	void Shutdown();

	void Render(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& compact, const ParticleMaterial& material,
		const ParticleRenderTarget& target);

	Options& GetOptions() { return m_Options; }
	const Options& GetOptions() const { return m_Options; }

private:
	void EnsureSize(uint32_t width, uint32_t height);
	void EnsureCapacity(uint32_t maxParticles);

	Options			m_Options;

	RootSignature	m_Sig;
	ComputePSO		m_ClearPSO{ L"Particle Layered Clear CS" };
	ComputePSO		m_PrepPSO{ L"Particle Layered Prep CS" };
	ComputePSO		m_CountPSO{ L"Particle Layered Count CS" };
	ComputePSO		m_ScanPSO{ L"Particle Layered Scan CS" };
	ComputePSO		m_WritePSO{ L"Particle Layered Write CS" };
	ComputePSO		m_RenderPSO{ L"Particle Layered Render CS" };

	StructuredBuffer	m_TiledParticles;	
	StructuredBuffer	m_SliceList;		
	ByteAddressBuffer	m_SliceData;		
	uint32_t			m_Capacity = 0;

	RootSignature	m_CompositeSig;
	GraphicsPSO		m_CompositePSO{ L"Particle Layered Composite PSO" };

	ColorBuffer		m_Result;	// rgb = premultiplied color / a = 1 - T
};
