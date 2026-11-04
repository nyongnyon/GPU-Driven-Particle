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

// 컴파일 결과를 사람이 읽을 수 있는 문자열로 만든다.
// (출력 방법은 호출하는 쪽이 정한다. 예: Utility::Print(DescribeCompiled(c, "Fire").c_str()))
std::string DescribeCompiled(const CompiledEmitter& c, const std::string& title);
