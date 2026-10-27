#pragma once

#include "ParticleTypes.h"
#include "ParticleRenderTypes.h"
#include "RenderTechnique.h"
#include "ParticleDepthSorter.h"
#include "ParticleTiledRenderer.h"
#include "VectorMath.h"
#include "RootSignature.h"
#include "PipelineState.h"

#include <unordered_map>
#include <memory>

class GraphicsContext;
struct ParticleMaterial;
namespace Math { class Camera; }

class ParticleRenderer
{
public:
	void Initialize();
	void Shutdown();

	void Render(GraphicsContext& gfx, const Math::Camera& cam, 
		const Particles::DrawInput& input, const ParticleMaterial& material,
		const ParticleRenderTarget& target);

	void SetTechnique(Particles::ParticleTechnique technique) { m_Technique = technique; }
	Particles::ParticleTechnique GetTechnique() const { return m_Technique; }

private:
	struct PipelineKey
	{
		Particles::DrawSource			source = Particles::DrawSource::FullScan;
		Particles::ParticleTechnique		output = Particles::ParticleTechnique::Additive;
		uint32_t						shaderVariant = 0;
		
		uint32_t Pack() const;
	};

	void				BuildRootSignature();
	void				CreateTechniques();
	void				PrewarmPSOs();

	PipelineKey			MakeKey(Particles::DrawSource source, Particles::ParticleTechnique output) const;
	GraphicsPSO&		GetPSO(const PipelineKey& key);

	RenderTechnique&	GetTechniqueImpl(Particles::ParticleTechnique t);
	const RenderTechnique& GetTechniqueImpl(Particles::ParticleTechnique t) const;

	RootSignature		m_DrawSignature;
	ParticleDepthSorter	m_DepthSorter;		// SortedAlpha 등 RequiresDepthSort() 기법용
	ParticleTiledRenderer m_TiledRenderer;
	
	std::unordered_map<uint32_t, std::unique_ptr<GraphicsPSO>> m_PSOCache;
	std::unique_ptr<RenderTechnique> m_Techniques[static_cast<uint32_t>(Particles::ParticleTechnique::COUNT)];

	bool				m_Initialized = false;
	Particles::ParticleTechnique		m_Technique = Particles::ParticleTechnique::Additive;
};


