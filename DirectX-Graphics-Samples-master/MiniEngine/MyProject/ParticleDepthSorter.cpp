#include "pch.h"
#include "ParticleDepthSorter.h"
#include "ParticleTypes.h"

#include "GraphicsCore.h"
#include "CommandContext.h"
#include "CommandListManager.h"
#include "BitonicSort.h"
#include "Camera.h"
#include "EngineProfiling.h"

#include "CompiledShaders/ParticleSortKeyReuseCS.h"
#include "CompiledShaders/ParticleSortKeyReuseFlagsCS.h"
#include "CompiledShaders/ParticleSortKeyAliveCS.h"

using namespace Graphics;
using namespace Particles;

namespace
{
	enum SortRootParams : uint32_t
	{
		kSortCBV = 0,
		kSortSRV,
		kSortUAV,
		kNumSortRootParams,
	};

	constexpr uint32_t kNumSortSRVs = 4;	// DrawInput의 t0 ~ t2 그대로 + t3 : EmitterGroups
	constexpr uint32_t kNoFilter = 0xFFFFFFFFu;	// HLSL SORT_NO_FILTER
	constexpr uint32_t kNumSortUAVs = 2;	// u0 : SortList, u1 : DrawArgs

	// D3D12_DRAW_ARGUMENTS 내 InstanceCount 위치 (BitonicSort의 counter offset)
	constexpr uint32_t kInstanceCountOffset = 4;

	struct alignas(16) SortConstants
	{
		DirectX::XMFLOAT3	camPosition;	uint32_t maxParticles;
		DirectX::XMFLOAT3	camForward;		uint32_t filterGroup;
	};
	static_assert(sizeof(SortConstants) == 32, "HLSL cbuffer SortConstants(32 B)와 일치해야 함");
}

void ParticleDepthSorter::Initialize()
{
	m_KeyGenSignature.Reset(kNumSortRootParams, 0);
	m_KeyGenSignature[kSortCBV].InitAsConstantBuffer(0);
	m_KeyGenSignature[kSortSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, kNumSortSRVs);
	m_KeyGenSignature[kSortUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, kNumSortUAVs);
	m_KeyGenSignature.Finalize(L"Particle Sort KeyGen RootSignature");

	const D3D12_SHADER_BYTECODE shaders[kNumKeyGenVariants] =
	{
		{ g_pParticleSortKeyReuseCS, sizeof(g_pParticleSortKeyReuseCS) },
		{ g_pParticleSortKeyReuseFlagsCS, sizeof(g_pParticleSortKeyReuseFlagsCS) },
		{ g_pParticleSortKeyAliveCS, sizeof(g_pParticleSortKeyAliveCS) },
	};

	for (uint32_t i = 0; i < kNumKeyGenVariants; ++i)
	{
		m_KeyGenPSO[i].SetRootSignature(m_KeyGenSignature);
		m_KeyGenPSO[i].SetComputeShader(shaders[i]);
		m_KeyGenPSO[i].Finalize();
	}

	m_DrawArgs.Create(L"Particle Sorted Draw Args", 4, sizeof(uint32_t));

	const uint32_t zero = 0;
	m_NoFilterTable.Create(L"Particle Sort No Filter Table", 1, sizeof(uint32_t), &zero);
}

void ParticleDepthSorter::Shutdown()
{
	m_SortList.Destroy();
	m_DrawArgs.Destroy();
	m_NoFilterTable.Destroy();
	m_Capacity = 0;
}

Particles::DrawInput ParticleDepthSorter::Sort(GraphicsContext& gfx, const Math::Camera& cam, const Particles::DrawInput& input, const Particles::GroupFilter* filter)
{
	return Build(gfx, cam, input, true, filter);
}

Particles::DrawInput ParticleDepthSorter::Compact(GraphicsContext& gfx, const Math::Camera& cam, const Particles::DrawInput& input, const Particles::GroupFilter* filter)
{
	return Build(gfx, cam, input, false, filter);
}

Particles::DrawInput ParticleDepthSorter::Build(GraphicsContext& gfx, const Math::Camera& cam, const Particles::DrawInput& input, bool sort, const Particles::GroupFilter* filter)
{
	EnsureCapacity(input.maxParticle);

	ComputeContext& cc = gfx.GetComputeContext();
	// 정렬 비용을 따로 비교할 수 있도록 타이머 이름을 나눈다
	ScopedTimer _t(sort ? L"Particle Sort" : L"Particle KeyGen", cc);

	// 1) Draw args 초기화 : 6 vertex(quad), InstanceCount = 0
	alignas(16) static const uint32_t kInitArgs[4] = { 6, 0, 0, 0 };
	cc.TransitionResource(m_DrawArgs, D3D12_RESOURCE_STATE_COPY_DEST, true);
	cc.WriteBuffer(m_DrawArgs, 0, kInitArgs, sizeof(kInitArgs));

	// 2) KeyGen : (slot, depth) compact append + InstanceCount 누적
	cc.TransitionResource(m_DrawArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cc.TransitionResource(m_SortList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// t3 : 필터가 있으면 이미터 -> 기법 테이블, 없으면 빈 테이블 (FilterGroup = kNoFilter라 읽지 않음)
	StructuredBuffer& groupTable = (filter && filter->groupTable) ? *filter->groupTable : m_NoFilterTable;
	cc.TransitionResource(groupTable, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);

	cc.SetRootSignature(m_KeyGenSignature);
	cc.SetPipelineState(m_KeyGenPSO[SelectKeyGen(input.source)]);

	SortConstants sc = {};
	XMStoreFloat3(&sc.camPosition, cam.GetPosition());
	XMStoreFloat3(&sc.camForward, cam.GetForwardVec());
	sc.maxParticles = input.maxParticle;
	sc.filterGroup = (filter && filter->groupTable) ? filter->group : kNoFilter;
	cc.SetDynamicConstantBufferView(kSortCBV, sizeof(sc), &sc);

	D3D12_CPU_DESCRIPTOR_HANDLE srvs[kNumSortSRVs];
	for (uint32_t i = 0; i < 3; ++i)
		srvs[i] = (i < input.numSrvs) ? input.srvs[i] : input.srvs[0];	// 안 쓰는 슬롯은 t0로 채움
	srvs[3] = groupTable.GetSRV();
	cc.SetDynamicDescriptors(kSortSRV, 0, kNumSortSRVs, srvs);

	const D3D12_CPU_DESCRIPTOR_HANDLE uavs[kNumSortUAVs] = { m_SortList.GetUAV(), m_DrawArgs.GetUAV() };
	cc.SetDynamicDescriptors(kSortUAV, 0, kNumSortUAVs, uavs);

	cc.Dispatch1D(input.maxParticle, 64);	// PARTICLE_GROUP_SIZE

	// 3) Sort : key(depth) 내림차순 -> 먼 것부터 그린다. 개수는 GPU의 InstanceCount를 그대로 사용
	if (sort)
		BitonicSort::Sort(cc, m_SortList, m_DrawArgs, kInstanceCountOffset, false, false);

	// 4) Draw 준비
	cc.TransitionResource(m_SortList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cc.TransitionResource(m_DrawArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, true);

	DrawInput out;
	out.source = DrawSource::Sorted;
	out.srvs[0] = input.srvs[0];		// t0 : particles
	out.srvs[1] = m_SortList.GetSRV();	// t1 : sorted (slot, depth)
	out.numSrvs = 2;
	out.maxParticle = input.maxParticle;
	out.indirectBuffer = &m_DrawArgs;
	out.argsOffset = 0;
	return out;
}

ParticleDepthSorter::KeyGenVariant ParticleDepthSorter::SelectKeyGen(Particles::DrawSource source)
{
	switch (source)
	{
	case DrawSource::FullScan:
	case DrawSource::SlotReuse:
		return kKeyGenReuse;
	case DrawSource::SlotReuseFlags:
		return kKeyGenReuseFlags;
	case DrawSource::AliveList:
		return kKeyGenAlive;
	default:
		break;
	}
	ASSERT(false, "DrawSource not sortable");
	return kKeyGenReuse;
}

void ParticleDepthSorter::EnsureCapacity(uint32_t maxParticles)
{
	if (m_Capacity == maxParticles)
		return;

	if (m_Capacity != 0)
	{
		g_CommandManager.IdleGPU();
		m_SortList.Destroy();
	}

	// BitonicSort가 count 이후 범위는 건드리지 않으므로 2의 거듭제곱 정렬 불필요
	m_SortList.Create(L"Particle Sort List", maxParticles, sizeof(uint32_t) * 2);
	m_Capacity = maxParticles;
}
