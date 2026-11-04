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

int         StageLevel(Stage s);   // 0 = System, 1 = Emitter, 2 = Particle
const char* StageName(Stage s);

// 값 하나 사용: 전체 이름 + 자료형
struct Usage
{
    std::string     attr;   // 전체 이름: "Particles.Previous.Position"
    const TypeInfo* type;   // 예: &Types::Float3
};

// 모듈 선언 (값으로 복사해서 저장할 수 있도록 일반 구조체로 둔다)
struct ModuleDesc
{
    std::string        name;
    Stage              stage;
    std::vector<Usage> reads;
    std::vector<Usage> writes;
};

// 렌더러(또는 엔진 단계)는 읽기만 한다
struct RendererDesc
{
    std::string        name;
    std::vector<Usage> reads;
};