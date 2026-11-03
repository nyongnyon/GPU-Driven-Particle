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

#include "CompiledShaders/ParticleTiledPrepCS.h"
#include "CompiledShaders/ParticleTiledRenderCS.h"
#include "CompiledShaders/ParticleTiledCompositePS.h"
#include "CompiledShaders/ParticleFullscreenVS.h"
#include "CompiledShaders/ParticleTiledCullOnly.h"

#include "Camera.h"

using namespace Graphics;

namespace
{
	enum TileRootParams : uint32_t { kTileCBV = 0, kTileSRVs, kTileUAVs, kNumTileRootParams };
	// t0 particles, t1 정렬 목록, t2 개수(DrawArgs), t3/t4 예비, t5 TiledParticles
	constexpr uint32_t kNumTileSRVs = 6;

	// u0 : Result, u1 : TiledParticles, u2 : , u3 : 
	constexpr uint32_t kNumTileUAVs = 4; 
	
	constexpr uint32_t kNumParticleStride = 32;

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
	m_TileSig[kTileUAVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, kNumTileUAVs);
	m_TileSig.Finalize(L"Particle Tiled RootSig");

	m_PrepPSO.SetRootSignature(m_TileSig);
	m_PrepPSO.SetComputeShader(g_pParticleTiledPrepCS, sizeof(g_pParticleTiledPrepCS));
	m_PrepPSO.Finalize();

	m_TilePSO.SetRootSignature(m_TileSig);
	m_TilePSO.SetComputeShader(g_pParticleTiledRenderCS, sizeof(g_pParticleTiledRenderCS));
	m_TilePSO.Finalize();

	m_CullOnlyPSO.SetRootSignature(m_TileSig);
	m_CullOnlyPSO.SetComputeShader(g_pParticleTiledCullOnly, sizeof(g_pParticleTiledCullOnly));
	m_CullOnlyPSO.Finalize();

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
	m_TiledParticles.Destroy();
	m_Capacity = 0;

	m_TiledBatchMask.Destroy();
	m_BatchMaskWords = 0;

	m_TileHits.Destroy();
	for (uint32_t i = 0; i < kLatency; ++i)
		m_StatsReadback[i].Destroy();
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

	const uint32_t tiles = ((width + kTileSize - 1) / kTileSize) * ((height + kTileSize - 1) / kTileSize);
	m_TileHits.Create(L"Tiled Hits", tiles + 1, sizeof(uint32_t));
	for (uint32_t i = 0; i < kLatency; ++i)
	{
		m_StatsReadback[i].Create(L"Tiled Hits Readback", tiles + 1, sizeof(uint32_t));
		m_ReadbackFence[i] = 0;
	}

	m_Result.Create(L"Particle Tiled Result", width, height, 1, kResultFormat);
	m_PendingSlot = -1;   // for문 뒤에 추가
}

void ParticleTiledRenderer::EnsureCapacity(uint32_t maxParticles)
{
	if (maxParticles == 0 || m_Capacity == maxParticles)
		return;

	if (m_Capacity != 0)
	{
		g_CommandManager.IdleGPU();
		m_TiledParticles.Destroy();
	}
	m_TiledParticles.Create(L"Tiled Particles", maxParticles, kNumParticleStride);

	m_Capacity = maxParticles;
}

void ParticleTiledRenderer::EnsureBatchMask(uint32_t maxParticles, uint32_t tiles)
{
	if (maxParticles == 0 || tiles == 0)
		return;

	const uint32_t wordsPerTile = (maxParticles + 256 * 32 - 1) / (256 * 32);
	const uint32_t needed = tiles * wordsPerTile;

	if (needed == 0 || m_BatchMaskWords == needed)
		return;

	if (m_BatchMaskWords != 0)
		g_CommandManager.IdleGPU();

	m_TiledBatchMask.Create(L"Tiled Particle Batch Mask", needed, sizeof(uint32_t));
	m_BatchMaskWords = needed;
}

void ParticleTiledRenderer::CollectStats()
{
	const uint32_t s = m_StatsSlot;
	const uint64_t fence = m_ReadbackFence[s];
	if (fence == 0 || !g_CommandManager.IsFenceComplete(fence))
		return;

	const uint32_t* data = static_cast<const uint32_t*>(m_StatsReadback[s].Map());

	TileStats st;
	st.tiles = m_ReadbackTiles[s];
	for (uint32_t i = 0; i < st.tiles; ++i)
	{
		st.pairs += data[i];
		st.maxPerTile = max(st.maxPerTile, data[i]);
		st.emptyTiles += (data[i] == 0);
	}
	st.particles = data[st.tiles];

	m_StatsReadback[s].Unmap();
	m_ReadbackFence[s] = 0;
	st.valid = true;
	m_LastStats = st;
}

void ParticleTiledRenderer::Render(GraphicsContext& gfx, const Math::Camera& cam,
	const Particles::DrawInput& sorted, const ParticleMaterial& /*material*/,
	const ParticleRenderTarget& target)
{
	
	const uint32_t w = target.color.GetWidth();
	const uint32_t h = target.color.GetHeight();

	const uint32_t tileCountX = (w + kTileSize - 1) / kTileSize;
	const uint32_t tileCountY = (h + kTileSize - 1) / kTileSize;
	const uint32_t tileCount = tileCountX * tileCountY;

	EnsureSize(w, h);
	EnsureCapacity(sorted.maxParticle);
	EnsureBatchMask(sorted.maxParticle,  tileCount);

	// readback
	if (m_PendingSlot >= 0)
	{
		m_ReadbackFence[m_PendingSlot] = g_CommandManager.GetGraphicsQueue().GetNextFenceValue() - 1;
		m_PendingSlot = -1;
	}
	CollectStats();
	
	// temp: imgui collect
	if (ImGui::GetCurrentContext() != nullptr)   // ImGui를 끈 빌드에서 크래시 방지
	{
		ImGui::Begin("Tiled Stats");
		ImGui::Checkbox("Cull only (no shading)", &m_CullOnly);

		if (m_CullOnly && m_LastStats.valid)
		{
			const TileStats& s = m_LastStats;
			const uint64_t tests = uint64_t(s.tiles) * s.particles;
			const uint32_t usedTiles = s.tiles - s.emptyTiles;

			ImGui::Text("Particles            : %u", s.particles);
			ImGui::Text("Overlap tests (TxP)  : %llu", tests);
			ImGui::Text("Overlaps found       : %llu  (%.3f%% of tests)", s.pairs,
				tests ? 100.0 * double(s.pairs) / double(tests) : 0.0);
			ImGui::Separator();
			ImGui::Text("Particles / tile     : avg %.1f, max %u",
				usedTiles ? double(s.pairs) / usedTiles : 0.0, s.maxPerTile);
			ImGui::Text("Empty tiles          : %u / %u", s.emptyTiles, s.tiles);
		}
		else if (m_CullOnly)
		{
			ImGui::TextDisabled("collecting...");
		}
		ImGui::End();
	}

	// 
	ComputeContext& cc = gfx.GetComputeContext();

	cc.TransitionResource(*sorted.indirectBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cc.TransitionResource(m_Result, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cc.TransitionResource(m_TiledParticles, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cc.TransitionResource(m_TiledBatchMask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	cc.ClearUAV(m_TiledBatchMask);                                                       
	cc.InsertUAVBarrier(m_TiledBatchMask, true);                                         

	cc.SetRootSignature(m_TileSig);

	{
		TileConstants tc = {};
		tc.screenWidth = w;
		tc.screenHeight = h;
		tc.tileCountX = tileCountX;
		tc.tileCountY = tileCountY;

		XMStoreFloat4x4(&tc.viewProj, cam.GetViewProjMatrix());

		DirectX::XMFLOAT4X4 proj;
		XMStoreFloat4x4(&proj, cam.GetProjMatrix());
		tc.projScaleX = proj._11;
		tc.projScaleY = proj._22;
		tc.heatMax = 1024.f;
		tc.nearClip = cam.GetNearClip();

		cc.SetDynamicConstantBufferView(kTileCBV, sizeof(tc), &tc);
	}

	//----------------------
	//		Prep
	//----------------------
	{
		ScopedTimer _t(L"Particle Tiled Prep", cc);
		cc.SetPipelineState(m_PrepPSO);		

		const D3D12_CPU_DESCRIPTOR_HANDLE srvs[kNumTileSRVs] =
		{
			sorted.srvs[0],
			sorted.srvs[1],
			sorted.indirectBuffer->GetSRV(),
			sorted.srvs[0],						// t3
			sorted.srvs[0],						// t4
			sorted.srvs[0],						// t5 (이 패스에선 미사용)
		};

		const D3D12_CPU_DESCRIPTOR_HANDLE uavs[kNumTileUAVs] = { m_Result.GetUAV(), m_TiledParticles.GetUAV(), m_TileHits.GetUAV(), m_TiledBatchMask.GetUAV()};

		cc.SetDynamicDescriptors(kTileSRVs, 0, kNumTileSRVs, srvs);
		cc.SetDynamicDescriptors(kTileUAVs, 0, kNumTileUAVs, uavs);
		cc.Dispatch1D(sorted.maxParticle, 64);
	}
	
	cc.InsertUAVBarrier(m_TiledBatchMask);                    
	cc.TransitionResource(m_TiledParticles, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cc.TransitionResource(m_TileHits, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);  

	// tiled (rendering)
	{
		ScopedTimer _t(L"Particle Tiled", cc);
		// CullOnly or Default
		cc.SetPipelineState(m_CullOnly ? m_CullOnlyPSO : m_TilePSO);

		const D3D12_CPU_DESCRIPTOR_HANDLE srvs[kNumTileSRVs] =
		{
			sorted.srvs[0],					
			sorted.srvs[1],					
			sorted.indirectBuffer->GetSRV(),
			sorted.srvs[0],					
			sorted.srvs[0],					
			m_TiledParticles.GetSRV(),			// t5 TiledParticles
		};

		const D3D12_CPU_DESCRIPTOR_HANDLE uavs[kNumTileUAVs] = { m_Result.GetUAV(), m_Result.GetUAV(), m_TileHits.GetUAV(), m_TiledBatchMask.GetUAV()};
		cc.SetDynamicDescriptors(kTileSRVs, 0, kNumTileSRVs, srvs);
		cc.SetDynamicDescriptors(kTileUAVs, 0, kNumTileUAVs, uavs);

		cc.Dispatch2D(w, h, kTileSize, kTileSize);
	}

	// readback when cullonly
	if (m_CullOnly)
	{
		cc.TransitionResource(m_TileHits, D3D12_RESOURCE_STATE_COPY_SOURCE, true);
		cc.CopyBufferRegion(m_StatsReadback[m_StatsSlot], 0, m_TileHits, 0, (tileCount + 1) * sizeof(uint32_t));

		m_ReadbackTiles[m_StatsSlot] = tileCount;
		m_PendingSlot = static_cast<int32_t>(m_StatsSlot);
		m_StatsSlot = (m_StatsSlot + 1) % kLatency;
	}



	// composite
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