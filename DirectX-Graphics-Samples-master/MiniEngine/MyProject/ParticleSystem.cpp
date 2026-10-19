#include "pch.h"
#include "ParticleSystem.h"

#include "GraphicsCore.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "EngineTuning.h"
#include "EngineProfiling.h"
#include "Camera.h"

#include "CompiledShaders/ParticleEmitCS.h"
#include "CompiledShaders/ParticleSimulateCS.h"
#include "CompiledShaders/ParticleDrawVS.h"
#include "CompiledShaders/ParticleDrawPS.h"

#include <vector>
#include <algorithm>

using namespace Graphics;
using namespace Math;
using namespace Particles;

namespace
{
	enum ComputeRootParams { kComputeCBVs, kComputeUAVs, kNumComputeRootParams };
	enum DrawRootParams { kDrawCBVs, kDrawSRVs, kNumDrawRootParams };

	RootSignature	s_ComputeSig;
	RootSignature	s_DrawSig;
	ComputePSO		s_EmitPSO(L"Particle Emit PSO");
	ComputePSO		s_SimulatePSO(L"Particle Simulate PSO");
	GraphicsPSO		s_DrawPSO(L"Particle Draw PSO");

	uint32_t HashU32(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
		return x;
	}

	uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
	}

	// TEMP Values
	NumVar g_TargetAliveRatio("Particles/Target alive ratio", 0.5f, 0.05f, 0.95f, 0.05f);
	NumVar g_MinLife("Particles/Life min", 1.0f, 0.1f, 10.f, 0.1f);
	NumVar g_MaxLife("Particles/Life max", 3.0f, 0.1f, 10.f, 0.1f);
	NumVar g_Size("Particles/Size", 0.03f, 0.005f, 1.f, 0.005f);

}

void ParticleSystem::InitializeShared()
{
	// ----------------------
	//		RootSignature
	// ----------------------
	
	// 1) Compute Root Sig
	s_ComputeSig.Reset(kNumComputeRootParams, 0);
	s_ComputeSig[kComputeCBVs].InitAsConstantBuffer(0);
	s_ComputeSig[kComputeUAVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 3);
	s_ComputeSig.Finalize(L"Particle Compute RootSig");

	// 2) Draw Root Sig
	s_DrawSig.Reset(kNumDrawRootParams, 0); // TEMP
	s_DrawSig[kDrawCBVs].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	s_DrawSig[kDrawSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_VERTEX);
	s_DrawSig.Finalize(L"Particle Draw RootSig");

	// ----------------------
	//			PSO
	// ----------------------
	
	s_EmitPSO.SetRootSignature(s_ComputeSig);
	s_EmitPSO.SetComputeShader(g_pParticleEmitCS, sizeof(g_pParticleEmitCS));
	s_EmitPSO.Finalize();

	s_SimulatePSO.SetRootSignature(s_ComputeSig);
	s_SimulatePSO.SetComputeShader(g_pParticleSimulateCS, sizeof(g_pParticleSimulateCS));
	s_SimulatePSO.Finalize();

	s_DrawPSO.SetRootSignature(s_DrawSig);
	s_DrawPSO.SetRasterizerState(RasterizerTwoSided);
	s_DrawPSO.SetBlendState(BlendAdditive);
	s_DrawPSO.SetDepthStencilState(DepthStateDisabled);
	s_DrawPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	s_DrawPSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
	s_DrawPSO.SetVertexShader(g_pParticleDrawVS, sizeof(g_pParticleDrawVS));
	s_DrawPSO.SetPixelShader(g_pParticleDrawPS, sizeof(g_pParticleDrawPS));
	s_DrawPSO.Finalize();
}

void ParticleSystem::Initialize(uint32_t maxParticles)
{
	m_MaxParticles = maxParticles;
	CreateBuffers();
	m_initVerified = VerifyInitialState();
}

void ParticleSystem::Shutdown()
{
	for (uint32_t i = 0; i < sReadbackLatency; ++i)
		m_CounterReadback[i].Destroy();
	m_CounterBuffer.Destroy();
	m_DeadListBuffer.Destroy();
	m_ParticleBuffer.Destroy();

}

void ParticleSystem::Update(float deltaT, const Math::Vector3& emitterPosition)
{
	const float dt = 1.f / 60.f;
	const uint32_t emitCount = ComputeEmitCount(dt);
	m_LastEmitRequest = emitCount;

	// TEMP variables
	Simconstants sc = {};
	XMStoreFloat3(&sc.emitterPos, emitterPosition);
	sc.deltaTime = dt;
	sc.gravity = { 0.f, -2.f, 0.f };
	sc.emitCount = emitCount;
	sc.minLife = g_MinLife;
	sc.maxLife = g_MaxLife;
	sc.minSpeed = 1.f;
	sc.maxSpeed = 4.f;
	sc.particleSize = g_Size;
	sc.maxParticles = m_MaxParticles;
	sc.randomSeed = HashU32(m_FrameIndex + 1);
	sc.baseColor = PackColor(255, 128, 45, 255); // TEMP 주황

	// -------------------------
	//		BEGIN COMPUTE
	// -------------------------
	ComputeContext& ctx = ComputeContext::Begin(L"Particle Update");

	alignas(16) static const uint32_t kZeros[4] = { 0, 0, 0, 0 };
	ctx.WriteBuffer(m_CounterBuffer, kEmitCount, kZeros, 4);

	// 1) Buffers -> UAV
	ctx.TransitionResource(m_ParticleBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ctx.TransitionResource(m_DeadListBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ctx.TransitionResource(m_CounterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	// 2) Bind
	ctx.SetRootSignature(s_ComputeSig);
	ctx.SetDynamicConstantBufferView(kComputeCBVs, sizeof(sc), &sc);
	
	const D3D12_CPU_DESCRIPTOR_HANDLE uavs[3]
		= { m_ParticleBuffer.GetUAV(), m_DeadListBuffer.GetUAV(), m_CounterBuffer.GetUAV() };
	ctx.SetDynamicDescriptors(kComputeUAVs, 0, 3, uavs);
	
	// 3) Emit
	{
		ScopedTimer _t(L"Particle Emit", ctx); // RAII
		if (emitCount > 0)
		{
			ctx.SetPipelineState(s_EmitPSO);
			ctx.Dispatch1D(emitCount, sThreadGroupSize);
		}
	}

	ctx.InsertUAVBarrier(m_ParticleBuffer);
	ctx.InsertUAVBarrier(m_DeadListBuffer);
	ctx.InsertUAVBarrier(m_CounterBuffer, true);

	// 4) Simulate
	{
		ScopedTimer _t(L"Particle Simulate", ctx);
		ctx.SetPipelineState(s_SimulatePSO);
		ctx.Dispatch1D(m_MaxParticles, sThreadGroupSize);
	}
	
	// 5) TEMP : Readback 없음
	ctx.Finish();

	++m_FrameIndex;
}

void ParticleSystem::Render(GraphicsContext& gfx, const Math::Camera& cam)
{
	gfx.TransitionResource(m_ParticleBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	DrawConstants dc = {};
	XMStoreFloat4x4(&dc.viewProj, cam.GetViewProjMatrix());
	XMStoreFloat4(&dc.camRight, cam.GetRightVec());
	dc.camRight.w = 0.f;
	XMStoreFloat4(&dc.camUp, cam.GetUpVec());
	dc.camUp.w = 0.f;

	gfx.SetRootSignature(s_DrawSig);
	gfx.SetPipelineState(s_DrawPSO);
	gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gfx.SetDynamicConstantBufferView(kDrawCBVs, sizeof(dc), &dc);
	gfx.SetDynamicDescriptor(kDrawSRVs, 0, m_ParticleBuffer.GetSRV());

	ScopedTimer _t(L"Particle Draw", gfx);
	gfx.DrawInstanced(6, m_MaxParticles);
}

void ParticleSystem::CreateBuffers()
{
	// Particle Buffer
	std::vector<Particle> zeroParticles(m_MaxParticles);
	m_ParticleBuffer.Create(L"Particle Buffer", m_MaxParticles, sizeof(Particle), zeroParticles.data());

	// DeadList Buffer
	std::vector<uint32_t> zeroDeadList(m_MaxParticles);
	for (uint32_t i = 0; i < m_MaxParticles; ++i)
		zeroDeadList[i] = m_MaxParticles - 1 - i;
	m_DeadListBuffer.Create(L"Particle DeadList", m_MaxParticles, sizeof(uint32_t), zeroDeadList.data());
	
	// Counter Buffer
	CounterSnapshot initSnapshot{}; // deadCount = paritcle count / 나머지 = 0
	initSnapshot.deadCount = m_MaxParticles;
	m_CounterBuffer.Create(L"Particle CounterBuffer", kCounterBufferSize / 4, sizeof(uint32_t), &initSnapshot);

	// Readback Buffer
	for (uint32_t i = 0; i < sReadbackLatency; ++i)
		m_CounterReadback[i].Create(L"Particle Counter ReadBack", kCounterBufferSize / 4, sizeof(uint32_t));

}

bool ParticleSystem::VerifyInitialState()
{
	ReadbackBuffer verify;
	verify.Create(L"Particle Init Verify", 6, 4);

	// Begin Compute verify
	ComputeContext& ctx = ComputeContext::Begin(L"Particle Init Verify");

	ctx.TransitionResource(m_CounterBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
	ctx.TransitionResource(m_DeadListBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);

	const size_t lastOffset = size_t(m_MaxParticles - 1) * 4;
	ctx.CopyBufferRegion(verify, 0, m_CounterBuffer, 0, kCounterBufferSize);
	ctx.CopyBufferRegion(verify, 16, m_DeadListBuffer, lastOffset, 4); // -> 값이 0에 해당해야 함
	ctx.CopyBufferRegion(verify, 20, m_DeadListBuffer, 0, 4); // -> 값이 Max-1에 해당해야 함
	ctx.Finish(true);

	const uint32_t* data = static_cast<const uint32_t*>(verify.Map());
	bool isVerified = (data[0] == 0) && (data[1] == m_MaxParticles) && (data[2] == 0) && (data[4] == 0) && (data[5] == m_MaxParticles - 1);
	Utility::Printf("[Particle] Init verify %s | MAX=%u alive=%u dead=%u deadTop=%u deadBottom=%u\n",
		isVerified ? "OK" : "FAILED", m_MaxParticles, data[0], data[1], data[4], data[5]);
	verify.Unmap();
	verify.Destroy();
	
	return isVerified;
}

uint32_t ParticleSystem::ComputeEmitCount(float deltaT) // TEMP
{
	const float avgLife = max(0.5f * ((float)g_MinLife + (float)g_MaxLife), 0.01f);
	const float rate = (float)g_TargetAliveRatio * float(m_MaxParticles) / avgLife;

	const float want = rate * deltaT + m_EmitCarry;
	const uint32_t count = (uint32_t)want;
	m_EmitCarry = want - float(count);
	return min(count, m_MaxParticles);
}
