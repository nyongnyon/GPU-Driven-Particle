#include "pch.h"
#include "AliveListPool.h"

#include "GraphicsCore.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Camera.h"

#include "ParticleUtility.h"

#include "CompiledShaders/ParticleEmitAliveCS.h"
#include "CompiledShaders/ParticleSimulateAliveCS.h"
#include "CompiledShaders/ParticleAfterSimAliveCS.h"
#include "CompiledShaders/ParticleDrawAliveVS.h"
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

	bool			s_Initialized = false;
	RootSignature	s_ComputeSig;
	RootSignature	s_DrawSig;
	ComputePSO		s_EmitPSO(L"Particle Alive Emit PSO");
	ComputePSO		s_SimulatePSO(L"Particle Alive Simulate PSO");
	ComputePSO		s_AfterSimPSO(L"Particle Alive After Sim PSO");
	
	GraphicsPSO		s_DrawPSO(L"Particle Alive Draw PSO");


	uint32_t HashU32(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
		return x;
	}

	// TEMP Values
	NumVar g_TargetAliveRatio("Particles/Target alive ratio", 0.5f, 0.05f, 0.95f, 0.05f);
	NumVar g_MinLife("Particles/Life min", 1.0f, 0.1f, 10.f, 0.1f);
	NumVar g_MaxLife("Particles/Life max", 3.0f, 0.1f, 10.f, 0.1f);
	NumVar g_Size("Particles/Size", 0.03f, 0.005f, 1.f, 0.005f);

	void InitializeSetting()
	{
		if (s_Initialized)
			return;

		// ----------------------
		//		RootSignature
		// ----------------------

		// 1) Compute RootSig
		s_ComputeSig.Reset(kNumComputeRootParams, 0);
		s_ComputeSig[kComputeCBVs].InitAsConstantBuffer(0);
		s_ComputeSig[kComputeUAVs].InitAsDescriptorTable(2);
		s_ComputeSig[kComputeUAVs].SetTableRange(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 3);
		s_ComputeSig[kComputeUAVs].SetTableRange(1, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 2);
		s_ComputeSig.Finalize(L"Particle Alive List Compute RoogSig");

		// 2) Draw RootSig
		s_DrawSig.Reset(kNumDrawRootParams, 0);
		s_DrawSig[kDrawCBVs].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
		s_DrawSig[kDrawSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 3, D3D12_SHADER_VISIBILITY_VERTEX);
		s_DrawSig.Finalize(L"Particle Alive List Draw RootSig");

		// ----------------------
		//			PSO
		// ----------------------
		// 1) Compute PSO
		s_EmitPSO.SetRootSignature(s_ComputeSig);
		s_EmitPSO.SetComputeShader(g_pParticleEmitAliveCS, sizeof(g_pParticleEmitAliveCS));
		s_EmitPSO.Finalize();

		s_SimulatePSO.SetRootSignature(s_ComputeSig);
		s_SimulatePSO.SetComputeShader(g_pParticleSimulateAliveCS, sizeof(g_pParticleSimulateAliveCS));
		s_SimulatePSO.Finalize();

		s_AfterSimPSO.SetRootSignature(s_ComputeSig);
		s_AfterSimPSO.SetComputeShader(g_pParticleAfterSimAliveCS, sizeof(g_pParticleAfterSimAliveCS));
		s_AfterSimPSO.Finalize();
		
		// 2) Draw PSO
		s_DrawPSO.SetRootSignature(s_DrawSig);
		s_DrawPSO.SetRasterizerState(RasterizerShadowTwoSided);
		s_DrawPSO.SetBlendState(BlendAdditive);
		s_DrawPSO.SetDepthStencilState(DepthStateDisabled);
		s_DrawPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		s_DrawPSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
		s_DrawPSO.SetVertexShader(g_pParticleDrawAliveVS, sizeof(g_pParticleDrawAliveVS));
		s_DrawPSO.SetPixelShader(g_pParticleDrawPS, sizeof(g_pParticleDrawPS));
		s_DrawPSO.Finalize();
	}
};


void AliveListPool::Initialize(uint32_t maxParticles)
{
	if (!s_Initialized)
		InitializeSetting();

	CreateBuffers(maxParticles);

	m_initVerified = VerifyInitialState();

}

void AliveListPool::CreateBuffers(uint32_t maxParticles)
{
	m_MaxParticles = maxParticles;

	// 1) Particle Buffer;
	std::vector<Particle> zeroParticles(m_MaxParticles);
	m_ParticleBuffer.Create(L"Particle Buffer", m_MaxParticles, sizeof(Particle), zeroParticles.data());

	// 2) DeadList Buffer
	std::vector<uint32_t> zeroDeadList(m_MaxParticles);
	for (uint32_t i = 0; i < m_MaxParticles; ++i)
		zeroDeadList[i] = m_MaxParticles - 1 - i;
	m_DeadListBuffer.Create(L"Particle DeadList", m_MaxParticles, sizeof(uint32_t), zeroDeadList.data());

	// 3) AliveList Buffer
	std::vector<uint32_t> zeroAliveList(m_MaxParticles, 0u);
	m_AliveListBuffer[0].Create(L"Particle AliveList 0", m_MaxParticles, sizeof(uint32_t), zeroAliveList.data());
	m_AliveListBuffer[1].Create(L"Particle AliveList 1", m_MaxParticles, sizeof(uint32_t), zeroAliveList.data());

	// 4) Counter Buffer
	CounterSnapshot initSnapshot = {};
	initSnapshot.deadCount = m_MaxParticles;
	m_CounterBuffer.Create(L"Particle CounterBuffer", kCounterBufferSize / 4, sizeof(uint32_t), &initSnapshot);
	
	// Readback Buffer
	for (uint32_t i = 0; i < sReadbackLatency; ++i)
		m_CounterReadback[i].Create(L"Particle Counter ReadBack", kCounterBufferSize / 4, sizeof(uint32_t));

}

void AliveListPool::Shutdown()
{
	for (uint32_t i = 0; i < sReadbackLatency; ++i)
		m_CounterReadback[i].Destroy();
	m_CounterBuffer.Destroy();
	m_AliveListBuffer[0].Destroy();
	m_AliveListBuffer[1].Destroy();
	m_DeadListBuffer.Destroy();
	m_ParticleBuffer.Destroy();
}

void AliveListPool::Update(float deltaT, const Math::Vector3& emitterPosition)
{

}

void AliveListPool::Render(GraphicsContext& gfx, const Math::Camera& cam)
{
}

bool AliveListPool::VerifyInitialState()
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