#pragma once
#include "LayoutBuilder.h"

// Compile된 Emitter 자료
struct CompiledEmitter
{
    Block particles;       // SoA 파티클 버퍼 레이아웃 (= "파티클 구조체")
    Block emitterParams;   // Emitter당 1개씩
    Block systemParams;    // System당 1개씩 (User 포함)

    std::vector<CopyOp> copyAtSpawnEnd;     // Initial  ← 현재
    std::vector<CopyOp> copyAtUpdateStart;  // Previous ← 현재

    std::vector<std::string> temporaries, engineValues, trimmed, warnings, errors;

    bool Ok() const { return errors.empty(); }
};

