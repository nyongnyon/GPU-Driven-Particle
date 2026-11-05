#pragma once

#include "AttributeType.h"
#include "CompiledEmitter.h"

struct EngineValues // Temp
{
	float  DeltaTime = 0.f;
	float3 OwnerPosition = { 0.f, 0.f, 0.f };
	uint32_t RandomSeed = 0; // 프레임마다 달라지는 랜덤시드
};

template<typename T>
struct StridedView 
{
	uint8_t* base; // start + offset
	uint32_t stride; // 파티클 하나의 크기
	T& operator[](uint32_t i)
	{ 
		return *reinterpret_cast<T*>(base + i * stride);
	}
};

class ModuleContext
{
public:
	ModuleContext(const CompiledEmitter& compiled,
		uint8_t* particleData,   // 파티클 버퍼 시작 (Stride x 최대 개수)
		uint8_t* emitterData,    // Emitter 저장소 시작
		const EngineValues& engine,
		uint32_t               begin,          // 이번에 처리할 파티클 범위 [begin, end)
		uint32_t               end);

	uint32_t Begin() const { return m_Begin; } // 시작 지점
	uint32_t End() const { return m_End; }

	template<typename T> StridedView<T> Particles(const std::string& name);
	template<typename T> T& Emitter(const std::string& name);

	float         DeltaTime()     const { return m_Engine.DeltaTime; }
	const float3& OwnerPosition() const { return m_Engine.OwnerPosition; }

	float			Random01(uint32_t particleIndex, uint32_t salt) const;

private:
	static const Slot* FindChecked(const Block& block, const std::string& fullName, const TypeInfo& expected);

	const CompiledEmitter& m_Compiled;
	uint8_t* m_ParticleData;
	uint8_t* m_EmitterData;
	const EngineValues&		m_Engine;
	uint32_t               m_Begin;
	uint32_t               m_End;
};

template<typename T>
inline StridedView<T> ModuleContext::Particles(const std::string& name)
{
	const Slot* slot = FindChecked(m_Compiled.particles, name, Typeof<T>());

	StridedView<T> ret;
	ret.base = m_ParticleData + slot->offset;
	ret.stride = m_Compiled.particles.Stride();
	return ret;
}

template<typename T>
inline T& ModuleContext::Emitter(const std::string& name)
{
	const Slot* slot = FindChecked(m_Compiled.emitterParams, name, TypeOf<T>());
	return *reinterpret_cast<T*>(m_EmitterData + slot->offset);

}
