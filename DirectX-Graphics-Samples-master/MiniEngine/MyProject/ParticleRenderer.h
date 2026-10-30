#pragma once

#include "ParticleTypes.h"
#include "ParticleRenderTypes.h"
#include "RenderTechnique.h"
#include "ParticleDepthSorter.h"
#include "ParticleTiledRenderer.h"
#include "ParticleLayeredRenderer.h"
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

	// 전체 강제 모드 : 풀 전체를 SetTechnique()의 기법 하나로 (레거시 WBOIT 계열은 여기서만)
	void Render(GraphicsContext& gfx, const Math::Camera& cam, 
		const Particles::DrawInput& input, const ParticleMaterial& material,
		const ParticleRenderTarget& target);

	// 이미터별 기법 : techniqueMask에 켜진 기법마다 그 그룹의 파티클만 골라 kGroupDrawOrder 순서로 그린다
	// groupTable[emitterId] = 그 이미터의 기법. 기법이 하나뿐이면 필터 없이 Render()와 같은 경로
	void RenderGroups(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& input, const ParticleMaterial& material,
		const ParticleRenderTarget& target, uint32_t techniqueMask, StructuredBuffer& groupTable);

	void SetTechnique(Particles::ParticleTechnique technique) { m_Technique = technique; }
	Particles::ParticleTechnique GetTechnique() const { return m_Technique; }

	ParticleLayeredRenderer::Options& LayeredOptions() { return m_LayeredRenderer.GetOptions(); }
	WBOITTechnique& WBOIT();	// debug views of the WBOIT render targets


private:
	struct PipelineKey
	{
		Particles::DrawSource			source = Particles::DrawSource::FullScan;
		Particles::ParticleTechnique	output = Particles::ParticleTechnique::Additive;
		uint32_t						shaderVariant = 0;
		
		uint32_t Pack() const;
	};

	// 기법이 원하는 입력으로 바꾼다 (정렬 / compact / 그대로). filter가 있으면 그 그룹만
	Particles::DrawInput Prepare(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& input, Particles::ParticleTechnique technique, const Particles::GroupFilter* filter);
	void				Draw(GraphicsContext& gfx, const Math::Camera& cam,
		const Particles::DrawInput& prepared, Particles::ParticleTechnique technique,
		const ParticleMaterial& material, const ParticleRenderTarget& target);

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
	ParticleLayeredRenderer m_LayeredRenderer;	// TiledLayeredWBOIT

	std::unordered_map<uint32_t, std::unique_ptr<GraphicsPSO>> m_PSOCache;
	std::unique_ptr<RenderTechnique> m_Techniques[static_cast<uint32_t>(Particles::ParticleTechnique::COUNT)];

	bool				m_Initialized = false;
	Particles::ParticleTechnique		m_Technique = Particles::ParticleTechnique::Additive;
};


