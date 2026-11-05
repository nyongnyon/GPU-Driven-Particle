#pragma once
#include "LayoutBuilder.h"

#include <string>
#include <vector>

// Compile 된 Emitter 자료
struct CompiledEmitter
{
    Block particles;       // 파티클 버퍼 레이아웃 (= "파티클 구조체")
    Block emitterParams;   // Emitter 당 1개씩
    Block systemParams;    // System 당 1개씩

    std::vector<CopyOp> copyAtSpawnEnd;     // Initial  <- 현재 (새로 태어난 파티클만)
    std::vector<CopyOp> copyAtUpdateStart;  // Previous <- 현재 (살아 있는 파티클 전부)

    std::vector<std::string> temporaries;
    std::vector<std::string> engineValues;
    std::vector<std::string> trimmed;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    bool Ok() const { return errors.empty(); }
};

std::string DescribeCompiled(const CompiledEmitter& c, const std::string& title);
