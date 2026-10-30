#include "pch.h"
#include "ParticleLayeredRenderer.h"

#include "GraphicsCore.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "CommandListManager.h"
#include "DepthBuffer.h"
#include "GpuBuffer.h"
#include "EngineProfiling.h"
#include "Camera.h"

#include "CompiledShaders/ParticleLayeredClearCS.h"
#include "CompiledShaders/ParticleLayeredPrepCS.h"
#include "CompiledShaders/ParticleLayeredCountCS.h"
#include "CompiledShaders/ParticleLayeredScanCS.h"
#include "CompiledShaders/ParticleLayeredWriteCS.h"
#include "CompiledShaders/ParticleLayeredRenderCS.h"
#include "CompiledShaders/ParticleTiledCompositePS.h"	// TiledAlpha와 같은 결과 포맷 -> 합성 셰이더 공유
#include "CompiledShaders/ParticleFullscreenVS.h"

using namespace Graphics;

namespace
{
	enum LayeredRootParams : uint32_t { kTileCBV = 0, kLayeredCBV, kSRVs, kUAVs, kNumRootParams };

	// t0 particles, t1 KeyList(정렬 안 됨), t2 DrawArgs(개수), t3 TiledParticles, t4 SliceList, t5 SliceData, t6 씬 깊이
	constexpr uint32_t kNumSRVs = 7;
	// u0 TiledParticles, u1 SliceData, u2 SliceList, u3 Result
	constexpr uint32_t kNumUAVs = 4;

	constexpr uint32_t kTiledParticleStride = 32;		// HLSL TiledParticle
	constexpr uint32_t kSliceDataBytes = 1024;			// 16 + 64*4 + 65*4 = 532 B 사용

	constexpr uint32_t kBinGroupSize = 256;			// Count / Write CS
	constexpr uint32_t kPrepGroupSize = 64;

	// ParticleTiledRenderer와 같은 b0 레이아웃 (ParticleTiledCommon.hlsli를 공유하므로)
	struct alignas(16) TileConstants
	{
		uint32_t screenWidth, screenHeight;
		uint32_t tileCountX, tileCountY;
		DirectX::XMFLOAT4X4 viewProj;
		float projScaleX, projScaleY;
		float heatMax;
		float nearClip;
		DirectX::XMFLOAT2 depthParams;	// 투영 행렬 (_33, _43) : 씬 깊이 -> 카메라 거리 (SceneViewDepth)
		DirectX::XMFLOAT2 pad;
	};
	static_assert(sizeof(TileConstants) == 112, "HLSL TileConstants(112 B)와 일치해야 함");

	struct alignas(16) LayeredConstants
	{
		uint32_t sliceCount;
		uint32_t sliceMode;
		uint32_t debugView;
		uint32_t pad;
	};
	static_assert(sizeof(LayeredConstants) == 16, "HLSL LayeredConstants(16 B)와 일치해야 함");

	constexpr DXGI_FORMAT kResultFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	void InitCS(ComputePSO& pso, const RootSignature& sig, const void* code, size_t size)
	{
		pso.SetRootSignature(sig);
		pso.SetComputeShader(code, size);
		pso.Finalize();
	}
}

void ParticleLayeredRenderer::Initialize()
{
	m_Sig.Reset(kNumRootParams, 0);
	m_Sig[kTileCBV].InitAsConstantBuffer(0);
	m_Sig[kLayeredCBV].InitAsConstantBuffer(1);
	m_Sig[kSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, kNumSRVs);
	m_Sig[kUAVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, kNumUAVs);
	m_Sig.Finalize(L"Particle Layered RootSig");

	InitCS(m_ClearPSO, m_Sig, g_pParticleLayeredClearCS, sizeof(g_pParticleLayeredClearCS));
	InitCS(m_PrepPSO, m_Sig, g_pParticleLayeredPrepCS, sizeof(g_pParticleLayeredPrepCS));
	InitCS(m_CountPSO, m_Sig, g_pParticleLayeredCountCS, sizeof(g_pParticleLayeredCountCS));
	InitCS(m_ScanPSO, m_Sig, g_pParticleLayeredScanCS, sizeof(g_pParticleLayeredScanCS));
	InitCS(m_WritePSO, m_Sig, g_pParticleLayeredWriteCS, sizeof(g_pParticleLayeredWriteCS));
	InitCS(m_RenderPSO, m_Sig, g_pParticleLayeredRenderCS, sizeof(g_pParticleLayeredRenderCS));

	// 합성 : (premul rgb, 1 - T) 를 BlendPreMultiplied로 씬 위에 덮는다
	m_CompositeSig.Reset(1, 0);
	m_CompositeSig[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_CompositeSig.Finalize(L"Particle Layered Composite RootSig");

	m_CompositePSO.SetRootSignature(m_CompositeSig);
	m_CompositePSO.SetRasterizerState(RasterizerTwoSided);
	m_CompositePSO.SetBlendState(BlendPreMultiplied);
	m_CompositePSO.SetDepthStencilState(DepthStateDisabled);
	m_CompositePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_CompositePSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
	m_CompositePSO.SetVertexShader(g_pParticleFullscreenVS, sizeof(g_pParticleFullscreenVS));
	m_CompositePSO.SetPixelShader(g_pParticleTiledCompositePS, sizeof(g_pParticleTiledCompositePS));
	m_CompositePSO.Finalize();

	m_SliceData.Create(L"Particle Layered SliceData", kSliceDataBytes / 4, sizeof(uint32_t));
}

void ParticleLayeredRenderer::Shutdown()
{
	m_Result.Destroy();
	m_TiledParticles.Destroy();
	m_SliceList.Destroy();
	m_SliceData.Destroy();
	m_Capacity = 0;
}

void ParticleLayeredRenderer::EnsureSize(uint32_t width, uint32_t height)
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
	m_Result.Create(L"Particle Layered Result", width, height, 1, kResultFormat);
}

void ParticleLayeredRenderer::EnsureCapacity(uint32_t maxParticles)
{
	if (maxParticles == 0 || m_Capacity == maxParticles)
		return;

	if (m_Capacity != 0)
	{
		g_CommandManager.IdleGPU();
		m_TiledParticles.Destroy();
		m_SliceList.Destroy();
	}
	m_TiledParticles.Create(L"Layered Tiled Particles", maxParticles, kTiledParticleStride);
	m_SliceList.Create(L"Layered Slice List", maxParticles, sizeof(uint32_t));
	m_Capacity = maxParticles;
}

void ParticleLayeredRenderer::Render(GraphicsContext& gfx, const Math::Camera& cam,
	const Particles::DrawInput& compact, const ParticleMaterial& /*material*/,
	const ParticleRenderTarget& target)
{
	const uint32_t w = target.color.GetWidth();
	const uint32_t h = target.color.GetHeight();
	EnsureSize(w, h);
	EnsureCapacity(compact.maxParticle);

	ComputeContext& cc = gfx.GetComputeContext();

	cc.TransitionResource(*compact.indirectBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cc.TransitionResource(m_TiledParticles, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cc.TransitionResource(m_SliceList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cc.TransitionResource(m_SliceData, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cc.TransitionResource(m_Result, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	cc.SetRootSignature(m_Sig);

	// b0 : 화면 / 카메라
	{
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
		tc.depthParams = { proj._33, proj._43 };
		cc.SetDynamicConstantBufferView(kTileCBV, sizeof(tc), &tc);
	}

	// b1 : slice 설정
	{
		LayeredConstants lc = {};
		const uint32_t maxSlices = kMaxSlices;
		lc.sliceCount = max(1u, min(m_Options.sliceCount, maxSlices));
		lc.sliceMode = static_cast<uint32_t>(m_Options.sliceMode);
		lc.debugView = m_Options.debugView ? 1u : 0u;
		cc.SetDynamicConstantBufferView(kLayeredCBV, sizeof(lc), &lc);
	}

	// ---------------- Binning : 정렬 대신 slice별 counting sort ----------------
	{
		ScopedTimer _t(L"Particle Layered Bin", cc);

		// 이 구간의 셰이더는 t0 ~ t2만 읽는다. t3 ~ t6은 자리만 채움
		const D3D12_CPU_DESCRIPTOR_HANDLE srvs[kNumSRVs] =
		{
			compact.srvs[0],					// t0 particles
			compact.srvs[1],					// t1 (slot, depth) 정렬 안 됨
			compact.indirectBuffer->GetSRV(),	// t2 개수
			compact.srvs[0], compact.srvs[0], compact.srvs[0], compact.srvs[0],
		};
		const D3D12_CPU_DESCRIPTOR_HANDLE uavs[kNumUAVs] =
		{
			m_TiledParticles.GetUAV(), m_SliceData.GetUAV(), m_SliceList.GetUAV(), m_Result.GetUAV(),
		};
		cc.SetDynamicDescriptors(kSRVs, 0, kNumSRVs, srvs);
		cc.SetDynamicDescriptors(kUAVs, 0, kNumUAVs, uavs);

		// 0) depth min/max, slice 카운터 초기화
		cc.SetPipelineState(m_ClearPSO);
		cc.Dispatch(1, 1, 1);
		cc.InsertUAVBarrier(m_SliceData);

		// 1) 화면 quad + 깊이 범위
		cc.SetPipelineState(m_PrepPSO);
		cc.Dispatch1D(compact.maxParticle, kPrepGroupSize);
		cc.InsertUAVBarrier(m_TiledParticles);
		cc.InsertUAVBarrier(m_SliceData);

		// 2) slice별 개수
		cc.SetPipelineState(m_CountPSO);
		cc.Dispatch1D(compact.maxParticle, kBinGroupSize);
		cc.InsertUAVBarrier(m_TiledParticles);
		cc.InsertUAVBarrier(m_SliceData);

		// 3) prefix sum -> slice 시작 위치
		cc.SetPipelineState(m_ScanPSO);
		cc.Dispatch(1, 1, 1);
		cc.InsertUAVBarrier(m_SliceData);

		// 4) slice 구간에 인덱스 기록
		cc.SetPipelineState(m_WritePSO);
		cc.Dispatch1D(compact.maxParticle, kBinGroupSize);
	}

	cc.TransitionResource(m_TiledParticles, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cc.TransitionResource(m_SliceList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cc.TransitionResource(m_SliceData, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cc.TransitionResource(target.depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);	// Render CS가 깊이 테스트로 읽는다

	// ---------------- 타일 렌더 : slice 단위 WBOIT + under + 조기 종료 ----------------
	{
		ScopedTimer _t(L"Particle Layered Render", cc);
		cc.SetPipelineState(m_RenderPSO);

		const D3D12_CPU_DESCRIPTOR_HANDLE srvs[kNumSRVs] =
		{
			compact.srvs[0], compact.srvs[1], compact.indirectBuffer->GetSRV(),
			m_TiledParticles.GetSRV(),			// t3
			m_SliceList.GetSRV(),				// t4
			m_SliceData.GetSRV(),				// t5
			target.depth.GetDepthSRV(),			// t6 씬 깊이
		};
		// 이 pass는 u3(Result)만 쓴다. 나머지는 SRV 상태인 버퍼 대신 Result로 자리만 채움
		const D3D12_CPU_DESCRIPTOR_HANDLE uavs[kNumUAVs] =
		{
			m_Result.GetUAV(), m_Result.GetUAV(), m_Result.GetUAV(), m_Result.GetUAV(),
		};
		cc.SetDynamicDescriptors(kSRVs, 0, kNumSRVs, srvs);
		cc.SetDynamicDescriptors(kUAVs, 0, kNumUAVs, uavs);

		cc.Dispatch2D(w, h, kTileSize, kTileSize);
	}

	// ---------------- 합성 ----------------
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

	gfx.TransitionResource(target.depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	gfx.SetRenderTarget(target.color.GetRTV(), target.depth.GetDSV());
}
