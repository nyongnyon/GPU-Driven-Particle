#include "pch.h"
#include "ParticleRenderer.h"
#include "ParticleMaterial.h"
#include "ParticleTypes.h"

#include "GraphicsCore.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "GpuBuffer.h"
#include "Camera.h"
#include "EngineProfiling.h"
#include "GpuBuffer.h"

#include "CompiledShaders/ParticleDrawReuseVS.h"
#include "CompiledShaders/ParticleDrawReuseFlagsVS.h"
#include "CompiledShaders/ParticleDrawAliveVS.h"

using namespace Graphics;
using namespace Particles;

namespace
{
	enum DrawRootParams : uint32_t
	{
		kDrawCBV = 0,
		kMaterialCBV,
		kParticleSRV,
		kMaterialSRV,
		kNumDrawRootParams, 
	};

	constexpr uint32_t kNumParticleSRVs = 3;	// VS에서 사용되는 파티클 SRV (particles, deadlist 등)
	constexpr uint32_t kNumMaterialSRVs = 4;	// material 관련 SRV
	constexpr uint32_t kMaterialSRVBase = kNumParticleSRVs;

	D3D12_SHADER_BYTECODE SelectVS(DrawSource source)
	{
		switch (source)
		{
		case DrawSource::FullScan:
		case DrawSource::SlotReuse:
			return { g_pParticleDrawReuseVS, sizeof(g_pParticleDrawReuseVS) };
		case DrawSource::SlotReuseFlags:
			return { g_pParticleDrawReuseFlagsVS, sizeof(g_pParticleDrawReuseFlagsVS) };
		case DrawSource::AliveList:
			return { g_pParticleDrawAliveVS, sizeof(g_pParticleDrawAliveVS) };
		default:
			break;
		}
		ASSERT(false, "Unknown DrawSource");
		return { g_pParticleDrawReuseVS, sizeof(g_pParticleDrawReuseVS) };
	}
}

void ParticleRenderer::Initialize()
{
	if (m_Initialized)
		return;
	
	BuildRootSignature();
	CreateTechniques();
	PrewarmPSOs();

	m_Initialized = true;
}

void ParticleRenderer::Shutdown()
{
	for (auto& tech : m_Techniques)
	{
		if (tech)
		{
			tech->Shutdown();
			tech.reset();
		}
	}
	m_PSOCache.clear();
	m_Initialized = false;
}

void ParticleRenderer::Render(GraphicsContext& gfx, const Math::Camera& cam,
	const Particles::DrawInput& input, const ParticleMaterial& material, 
	const ParticleRenderTarget& target)
{
	if (!m_Initialized)
		return;

	if (input.numSrvs == 0 || input.maxParticle == 0)
		return;

	RenderTechnique& tech = GetTechniqueImpl(m_Technique);

	tech.BeginDraw(gfx, target);

	// PSO
	gfx.SetRootSignature(m_DrawSignature);
	gfx.SetPipelineState(GetPSO(MakeKey(input.source, m_Technique)));
	gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// b0 : Camera
	DrawConstants dc = {};
	XMStoreFloat4x4(&dc.viewProj, cam.GetViewProjMatrix());
	XMStoreFloat4(&dc.camRight, cam.GetRightVec());	dc.camRight.w = 0.f;
	XMStoreFloat4(&dc.camUp, cam.GetUpVec());		dc.camUp.w = 0.f;
	
	// b1 : material
	gfx.SetDynamicConstantBufferView(kMaterialCBV, sizeof(material.constants), &material.constants);

	// t0 - t2 : 파티클 데이터
	D3D12_CPU_DESCRIPTOR_HANDLE particleSrvs[kNumParticleSRVs];
	for (uint32_t i = 0; i < kNumParticleSRVs; ++i)
		particleSrvs[i] = (i < input.numSrvs) ? input.srvs[i] : input.srvs[0]; // 0 = null
	gfx.SetDynamicDescriptors(kParticleSRV, 0, kNumParticleSRVs, particleSrvs);

	// t3 - t6 : 머티리얼 텍스처
	const D3D12_CPU_DESCRIPTOR_HANDLE materialSrvs[kNumMaterialSRVs] =
	{
		material.albedo.IsValid() ? material.albedo.GetSRV() : GetDefaultTexture(kWhiteOpaque2D),		// t3
		material.noise.IsValid() ? material.noise.GetSRV() : GetDefaultTexture(kBlackTransparent2D),	// t4
		GetDefaultTexture(kBlackTransparent2D),	// t5 : SceneDepth (Soft particle 구현 시 교체)
		GetDefaultTexture(kBlackTransparent2D),	// t6 : 예비
	};
	gfx.SetDynamicDescriptors(kMaterialSRV, 0, kNumMaterialSRVs, materialSrvs);

	// Draw
	{
		ScopedTimer _t(L"Particle Draw", gfx);

		if (input.indirectBuffer != nullptr)
			gfx.DrawIndirect(*input.indirectBuffer, input.argsOffset);
		else
			gfx.DrawInstanced(6, input.maxParticle);
	}


	tech.EndDraw(gfx, target);
}


void ParticleRenderer::BuildRootSignature()
{
	m_DrawSignature.Reset(kNumDrawRootParams, 1); // sampler 1개
	m_DrawSignature[kDrawCBV].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_DrawSignature[kMaterialCBV].InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_ALL);
	m_DrawSignature[kParticleSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 
		kNumParticleSRVs, D3D12_SHADER_VISIBILITY_VERTEX);
	m_DrawSignature[kMaterialSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kMaterialSRVBase,
		kNumMaterialSRVs, D3D12_SHADER_VISIBILITY_PIXEL);
	m_DrawSignature.InitStaticSampler(0, SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_DrawSignature.Finalize(L"Particle Draw RootSignature00");
}

void ParticleRenderer::CreateTechniques()
{
	m_Techniques[static_cast<uint32_t>(ParticleTechnique::Additive)] =
		std::make_unique<SinglePassTechnique>(BlendAdditive);

	m_Techniques[static_cast<uint32_t>(ParticleTechnique::PremultipliedAlpha)] =
		std::make_unique<SinglePassTechnique>(BlendPreMultiplied);

	m_Techniques[static_cast<uint32_t>(ParticleTechnique::WeightedOIT)] =
		std::make_unique<WBOITTechnique>();

	for (auto& tech : m_Techniques)
	{
		tech->Initialize();
	}
}

void ParticleRenderer::PrewarmPSOs()
{
	for (uint32_t s = 0; s < static_cast<uint32_t>(DrawSource::COUNT); ++s)
	{
		for (uint32_t t = 0; t < static_cast<uint32_t>(ParticleTechnique::COUNT); ++t)
		{
			GetPSO(MakeKey(static_cast<DrawSource>(s), static_cast<ParticleTechnique>(t)));
		}
	}
}

ParticleRenderer::PipelineKey ParticleRenderer::MakeKey(Particles::DrawSource source, Particles::ParticleTechnique output) const
{
	PipelineKey key;
	key.source = static_cast<DrawSource>(source);
	key.output = static_cast<ParticleTechnique>(output);
	
	return key;
}


GraphicsPSO& ParticleRenderer::GetPSO(const ParticleRenderer::PipelineKey& key)
{
	const uint32_t packed = key.Pack();

	auto it = m_PSOCache.find(packed);
	if (it != m_PSOCache.end())
		return *it->second;

	auto pso = std::make_unique<GraphicsPSO>(L"Particle Draw PSO");

	pso->SetRootSignature(m_DrawSignature);
	pso->SetRasterizerState(RasterizerTwoSided);
	pso->SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	pso->SetVertexShader(SelectVS(key.source));

	GetTechniqueImpl(key.output).ConfigurePSO(*pso);

	pso->Finalize();

	GraphicsPSO& ref = *pso;
	m_PSOCache.emplace(packed, std::move(pso));
	return ref;
}


uint32_t ParticleRenderer::PipelineKey::Pack() const
{ 
	// [0:3] source | [4:7] output | [8:variant]
	return  (static_cast<uint32_t>(source) & 0xF)
		| ((static_cast<uint32_t>(output) & 0xF) << 4)
		| (shaderVariant << 8);
}

RenderTechnique& ParticleRenderer::GetTechniqueImpl(ParticleTechnique t)
{
	return *m_Techniques[static_cast<uint32_t>(t)];
}

const RenderTechnique& ParticleRenderer::GetTechniqueImpl(ParticleTechnique t) const
{
	return *m_Techniques[static_cast<uint32_t>(t)];
}