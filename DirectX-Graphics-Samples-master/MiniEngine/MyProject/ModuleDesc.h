#pragma once

#include "AttributeType.h"
#include <vector>
#include <string>

enum class Stage 
{ 
    SystemSpawn, 
    SystemUpdate, 
    EmitterSpawn, 
    EmitterUpdate, 
    ParticleSpawn, 
    ParticleUpdate 
};

inline int StageLevel(Stage s)   // 0 = System, 1 = Emitter, 2 = Particle
{
    switch (s) 
    {
    case Stage::SystemSpawn:
    case Stage::SystemUpdate:   return 0;
    case Stage::EmitterSpawn:
    case Stage::EmitterUpdate:  return 1;
    default:                    return 2;
    }
}

inline const char* StageName(Stage s)
{
    switch (s) 
    {
    case Stage::SystemSpawn:    return "SystemSpawn";
    case Stage::SystemUpdate:   return "SystemUpdate";
    case Stage::EmitterSpawn:   return "EmitterSpawn";
    case Stage::EmitterUpdate:  return "EmitterUpdate";
    case Stage::ParticleSpawn:  return "ParticleSpawn";
    case Stage::ParticleUpdate: return "ParticleUpdate";
    }
    return "?";
}

// 이름 + 타입
struct Usage 
{
    std::string attr;   // 전체 이름: "Particles.Previous.Position"
    AttrType    type;
};

// 실제 모듈이 사용할 구조체
struct ModuleDesc
{
public:
	virtual ~ModuleDesc() = default;
	virtual std::string Name() const = 0;

	virtual std::vector<Usage> GetReads() { return {}; }
	virtual std::vector<Usage> GetWrites() { return {}; }
};

struct RendererDesc 
{
    std::string        name;
    std::vector<Usage> reads;
};

