#include "pch.h"
#include "ParticleTiledRenderer.h"

#include "GraphicsCore.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "CommandListManager.h"
#include "DepthBuffer.h"
#include "GpuBuffer.h"
#include "EngineProfiling.h"

#include "CompiledShaders/ParticleTiledRenderCS.h"
#include "CompiledShaders/ParticleTiledCompositePS.h"
#include "CompiledShaders/ParticleFullscreenVS.h"

#include "Camera.h"

using namespace Graphics;

namespace
{
	enum TileRootParams : uint32_t { kTileCBV = 0, kTileSRVs, kTileUAVs, kNumTileRootParams };
	constexpr uint32_t kNumTileSRVs = 4;	// t0 particles, t1 정렬 목록, t2 개수(DrawArgs), t3 예비

	struct alignas(16) TileConstants
	{
		uint32_t screenWidth, screenHeight;
		uint32_t tileCountX, tileCountY;
		DirectX::XMFLOAT4X4 viewProj;
		float projScaleX, projScaleY;
		float heatMax; // 히트맵 최대값 (디버그용)
		float nearClip;		
	};
	static_assert(sizeof(TileConstants) == 96, "HLSL TileConstants(96 B)와 일치해야 함");


	constexpr DXGI_FORMAT kResultFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
}

void ParticleTiledRenderer::Initialize()
{
	// --- 타일 CS ---
	m_TileSig.Reset(kNumTileRootParams, 0);
	m_TileSig[kTileCBV].InitAsConstantBuffer(0);
	m_TileSig[kTileSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, kNumTileSRVs);
	m_TileSig[kTileUAVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_TileSig.Finalize(L"Particle Tiled RootSig");

	m_TilePSO.SetRootSignature(m_TileSig);
	m_TilePSO.SetComputeShader(g_pParticleTiledRenderCS, sizeof(g_pParticleTiledRenderCS));
	m_TilePSO.Finalize();

	// --- 합성 (WBOIT 합성과 같은 구조) ---
	m_CompositeSig.Reset(1, 0);
	m_CompositeSig[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_CompositeSig.Finalize(L"Particle Tiled Composite RootSig");

	m_CompositePSO.SetRootSignature(m_CompositeSig);
	m_CompositePSO.SetRasterizerState(RasterizerTwoSided);
	m_CompositePSO.SetBlendState(BlendPreMultiplied);		// 결과가 (premul rgb, 1 - T)
	m_CompositePSO.SetDepthStencilState(DepthStateDisabled);
	m_CompositePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_CompositePSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);	// 깊이 끔 -> DSV 없음
	m_CompositePSO.SetVertexShader(g_pParticleFullscreenVS, sizeof(g_pParticleFullscreenVS));
	m_CompositePSO.SetPixelShader(g_pParticleTiledCompositePS, sizeof(g_pParticleTiledCompositePS));
	m_CompositePSO.Finalize();
}

void ParticleTiledRenderer::Shutdown()
{
	m_Result.Destroy();
}

void ParticleTiledRenderer::EnsureSize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return;
	if (m_Result.GetWidth() == width && m_Result.GetHeight() == height)
		return;
	if (m_Result.GetWidth() != 0)
	{
		g_CommandManager.IdleGPU();
		m_Result.Destroy();
	}
	m_Result.Create(L"Particle Tiled Result", width, height, 1, kResultFormat);
}

void ParticleTiledRenderer::Render(GraphicsContext& gfx, const Math::Camera& cam,
	const Particles::DrawInput& sorted, const ParticleMaterial& /*material*/,
	const ParticleRenderTarget& target)
{
	const uint32_t w = target.color.GetWidth();
	const uint32_t h = target.color.GetHeight();
	EnsureSize(w, h);

	ComputeContext& cc = gfx.GetComputeContext();
	{
		ScopedTimer _t(L"Particle Tiled", cc);

		cc.TransitionResource(*sorted.indirectBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cc.TransitionResource(m_Result, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

		cc.SetRootSignature(m_TileSig);
		cc.SetPipelineState(m_TilePSO);

		TileConstants tc = {};
		tc.screenWidth = w;
		tc.screenHeight = h;
		tc.tileCountX = (w + kTileSize - 1) / kTileSize;
		tc.tileCountY = (h + kTileSize - 1) / kTileSize;
		
		XMStoreFloat4x4(&tc.viewProj, cam.GetViewProjMatrix());

		DirectX::XMFLOAT4X4 proj;
		XMStoreFloat4x4(&proj, cam.GetProjMatrix());
		tc.projScaleX = proj._11;
		tc.projScaleY = proj._22;
		tc.heatMax = 1024.f;
		tc.nearClip = cam.GetNearClip();
		
		cc.SetDynamicConstantBufferView(kTileCBV, sizeof(tc), &tc);

		const D3D12_CPU_DESCRIPTOR_HANDLE srvs[kNumTileSRVs] =
		{
			sorted.srvs[0],					
			sorted.srvs[1],					
			sorted.indirectBuffer->GetSRV(),
			sorted.srvs[0],					
		};
		cc.SetDynamicDescriptors(kTileSRVs, 0, kNumTileSRVs, srvs);
		cc.SetDynamicDescriptor(kTileUAVs, 0, m_Result.GetUAV());

		cc.Dispatch2D(w, h, kTileSize, kTileSize);
	}

	gfx.TransitionResource(m_Result, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
	gfx.SetRenderTarget(target.color.GetRTV());
	{
		ScopedTimer _t(L"Particle Composite", gfx);
		gfx.SetRootSignature(m_CompositeSig);
		gfx.SetPipelineState(m_CompositePSO);
		gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfx.SetDynamicDescriptor(0, 0, m_Result.GetSRV());
		gfx.Draw(3);
	}


	gfx.SetRenderTarget(target.color.GetRTV(), target.depth.GetDSV());
}