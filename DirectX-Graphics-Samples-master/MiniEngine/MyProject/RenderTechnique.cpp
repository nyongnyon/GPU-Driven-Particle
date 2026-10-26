#include "pch.h"
#include "RenderTechnique.h"

#include "GraphicsCore.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "CommandListManager.h"
#include "DepthBuffer.h"
#include "EngineProfiling.h"

#include "CompiledShaders/ParticleDrawPS.h"
#include "CompiledShaders/ParticleWBOITPS.h"
#include "CompiledShaders/ParticleFullscreenVS.h"
#include "CompiledShaders/ParticleWBOITCompositePS.h"


using namespace Graphics;

void SinglePassTechnique::ConfigurePSO(GraphicsPSO& pso) const
{
	pso.SetBlendState(m_Blend);
	pso.SetDepthStencilState(DepthStateReadOnly);
	pso.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat());
	pso.SetPixelShader(g_pParticleDrawPS, sizeof(g_pParticleDrawPS));
}

namespace
{
	enum CompositeRootParams : uint32_t
	{
		kCompositeSRVs = 0, // t0 : Accumulate, t1 : Revealage
		kNumCompositeRootParams = 1
	};

	D3D12_BLEND_DESC MakeWBOITBlend()
	{
		D3D12_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;	// alpha to coverage를 다중 샘플링 기술로 사용할지
		desc.IndependentBlendEnable = TRUE; // RTV마다 다른 blend 상태

		// RT0 : Accum Target
		D3D12_RENDER_TARGET_BLEND_DESC& accum = desc.RenderTarget[0];
		accum.BlendEnable = TRUE;
		accum.SrcBlend = D3D12_BLEND_ONE;
		accum.DestBlend = D3D12_BLEND_ONE;
		accum.BlendOp = D3D12_BLEND_OP_ADD;
		accum.SrcBlendAlpha = D3D12_BLEND_ONE;
		accum.DestBlendAlpha = D3D12_BLEND_ONE;
		accum.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		accum.LogicOp = D3D12_LOGIC_OP_NOOP;
		accum.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		// RT1 : Revealage
		D3D12_RENDER_TARGET_BLEND_DESC& reveal = desc.RenderTarget[1];
		reveal.BlendEnable = TRUE;
		reveal.SrcBlend = D3D12_BLEND_ZERO;
		reveal.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
		reveal.BlendOp = D3D12_BLEND_OP_ADD;
		reveal.SrcBlendAlpha = D3D12_BLEND_ZERO;
		reveal.DestBlendAlpha = D3D12_BLEND_ONE;
		reveal.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		reveal.LogicOp = D3D12_LOGIC_OP_NOOP;
		reveal.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED; // R value만 사용
	
		return desc;
	}
}

void WBOITTechnique::Initialize()
{
	m_CompositeSig.Reset(kNumCompositeRootParams, 0);
	m_CompositeSig[kCompositeSRVs].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2, D3D12_SHADER_VISIBILITY_PIXEL);
	m_CompositeSig.Finalize(L"Particle WBOIT Composite RootSig");

	m_CompositePSO.SetRootSignature(m_CompositeSig);
	m_CompositePSO.SetRasterizerState(RasterizerTwoSided);
	m_CompositePSO.SetBlendState(BlendTraditional);
	m_CompositePSO.SetDepthStencilState(DepthStateDisabled);
	m_CompositePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_CompositePSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat());
	m_CompositePSO.SetVertexShader(g_pParticleFullscreenVS, sizeof(g_pParticleFullscreenVS));
	m_CompositePSO.SetPixelShader(g_pParticleWBOITCompositePS, sizeof(g_pParticleWBOITCompositePS));
	m_CompositePSO.Finalize();

	m_Accum.SetClearColor(Color(0.f, 0.f, 0.f, 0.f));
	m_Reveal.SetClearColor(Color(1.f, 1.f, 1.f, 1.f));

}

void WBOITTechnique::Shutdown()
{
	m_Accum.Destroy();
	m_Reveal.Destroy();
}

void WBOITTechnique::ConfigurePSO(GraphicsPSO& pso) const
{
	static const DXGI_FORMAT kFormats[2] = { kAccumFormat,  kRevealFormat };

	pso.SetBlendState(MakeWBOITBlend());
	pso.SetDepthStencilState(DepthStateReadOnly);
	pso.SetRenderTargetFormats(2, kFormats, g_SceneDepthBuffer.GetFormat());
	
	pso.SetPixelShader(g_pParticleWBOITPS, sizeof(g_pParticleWBOITPS));
}

void WBOITTechnique::BeginDraw(GraphicsContext& gfx, const ParticleRenderTarget& target)
{
	EnsureSize(target.color.GetWidth(), target.color.GetHeight());

	gfx.TransitionResource(m_Accum, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfx.TransitionResource(m_Reveal, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfx.ClearColor(m_Accum);
	gfx.ClearColor(m_Reveal);

	const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { m_Accum.GetRTV(), m_Reveal.GetRTV() };

	gfx.SetRenderTargets(2, rtvs, target.depth.GetDSV());
}

void WBOITTechnique::EndDraw(GraphicsContext& gfx, const ParticleRenderTarget& target)
{
	gfx.TransitionResource(m_Accum, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	gfx.TransitionResource(m_Reveal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);

	gfx.SetRenderTarget(target.color.GetRTV(), target.depth.GetDSV());

	ScopedTimer _t(L"Particle Composite", gfx);

	gfx.SetRootSignature(m_CompositeSig);
	gfx.SetPipelineState(m_CompositePSO);
	gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const D3D12_CPU_DESCRIPTOR_HANDLE srvs[2] = { m_Accum.GetSRV(), m_Reveal.GetSRV() };
	gfx.SetDynamicDescriptors(kCompositeSRVs, 0, 2, srvs);

	gfx.Draw(3);	// 전체 화면 삼각형
}


void WBOITTechnique::EnsureSize(uint32_t width, uint32_t height)
{
	if (m_Accum.GetWidth() == width && m_Accum.GetHeight() == height)
		return;

	if (m_Accum.GetWidth() != 0)
	{
		g_CommandManager.IdleGPU();
		m_Accum.Destroy();
		m_Reveal.Destroy();
	}

	m_Accum.Create(L"Particle WBOIT Accum", width, height, 1, kAccumFormat);
	m_Reveal.Create(L"Particle WBOIT Revealage", width, height, 1, kRevealFormat);
}
