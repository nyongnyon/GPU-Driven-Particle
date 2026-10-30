#pragma once

#include <cstdint>
#include <string>
#include "ParticleTypes.h"
#include "ParticleEmitter.h"
#include "ParticleREnderTypes.h"

// ImGui에서 바꾸는 측정 조건을 전부 한곳에 모은 것
struct ParticleSettings
{
	// --- Pool: MAX나 계열(Slot <-> Alive)이 바뀌면 버퍼를 새로 만든다 ---
	Particles::Mode	mode = Particles::Mode::AliveIndirect;
	uint32_t		maxParticles = 1000000;

	// --- Emit: 바뀌면 Emitter만 다시 설정한다 ---
	float	steadyRatio = 0.5f;		// steady로 유지할 alive 비율 (MAX 대비)
	float	burstRatio = 0.0f;		// burst 한 번에 만들 양 (MAX 대비). 0이면 burst 끔
	float	burstInterval = 2.0f;	// 초 (시뮬레이션 시간)
	float	minLife = 1.0f;
	float	maxLife = 3.0f;
	float	particleSize = 0.03f;	// world units, quad half-size

	Particles::ColorMode	colorMode = Particles::ColorMode::Fixed;
	DirectX::XMFLOAT3		colorA = { 1.0f, 0.502f, 0.176f };
	DirectX::XMFLOAT3		colorB = { 1.0f, 0.85f, 0.3f };
	float					colorIntensity = 1.0f;

	// --- Render ---
	// forceTechnique = true : 모든 이미터를 technique 하나로 (측정용, 레거시 WBOIT 계열은 여기서만)
	// forceTechnique = false : 이미터마다 자기 기법 (ParticleWorld::SetEmitterTechnique)
	bool						 forceTechnique = true;
	Particles::ParticleTechnique technique = Particles::ParticleTechnique::Additive;

	// --- TiledLayeredWBOIT 전용 (다른 기법에선 무시) ---
	uint32_t	layeredSlices = 16;		// 깊이 slice 수 (1 ~ 64). 1 = 순수 WBOIT, 클수록 정렬에 가까움
	uint32_t	layeredSliceMode = 1;	// 0 = linear, 1 = log


	bool operator==(const ParticleSettings& o) const;
	bool operator!=(const ParticleSettings& o) const { return !(*this == o); }
};

namespace Particles
{
	// Dispatch1D(MAX, 64)가 넘으면 안 되는 한도 (그룹 수 65535 x 64)
	static const uint32_t kMaxDispatchParticles = 65535u * 64u;

	ParticleSettings	Sanitize(ParticleSettings s);					// 잘못된 값 정리
	EmitterParams		BuildEmitterParams(const ParticleSettings& s);	// 비율 -> 실제 개수 (변환은 여기서만)

	static const float kMaxColorIntensity = 16.0f;

	const char* ModeName(Mode mode);
	const char* ColorModeName(ColorMode mode);
	std::string	SettingsCsvHeader();
	std::string	SettingsCsvValues(const ParticleSettings& s);
}