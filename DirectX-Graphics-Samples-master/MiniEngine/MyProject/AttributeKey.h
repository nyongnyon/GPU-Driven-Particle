#pragma once
#include "ParticleNamespace.h"

#include <iostream>
#include <sstream>

// Attribute의 진짜 이름 = 네임스페이스 + 수식어 + 이름
struct AttributeKey
{
	Namespace ns = Namespace::Unknown;
	Modifier mod = Modifier::None;
	std::string name;

	// Full Key를 namespace / mod / name 으로 나눈다.
	// Particles.Previous.Position -> Particles / Previous / Position
    static AttributeKey Parse(const std::string& full);

    std::string Full() const;

    AttributeKey Base() const;

    Storage GetStorage() const
    {
        switch (ns) {
        case Namespace::Particles: return Storage::ParticleBuffer;   // 파티클마다 1개 (SoA 버퍼)
        case Namespace::Emitter:   return Storage::EmitterStore;     // Emitter당 1개
        case Namespace::System:
        case Namespace::User:      return Storage::SystemStore;      // System당 1개
        case Namespace::Engine:    return Storage::EngineProvided;   // 엔진이 줌 (DeltaTime 등)
        case Namespace::Transient:
        case Namespace::Output:
        case Namespace::Local:     return Storage::Temporary;        // 단계 안에서만 쓰는 임시값
        default:                   return Storage::Invalid;
        }
    }
};

