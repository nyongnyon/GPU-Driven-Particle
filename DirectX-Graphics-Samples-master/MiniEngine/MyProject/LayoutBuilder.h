#pragma once

#include "AttributeKey.h"
#include "AttributeType.h"


struct Slot
{
    AttributeKey key;
    AttrType     type;
    int          start;
};

struct Block 
{
    std::vector<Slot> slots;
    int total = 0;


    void Add(const AttributeKey& k, AttrType t)
    {
        slots.push_back({ k, t, total });
        total += ComponentCount(t);
    }
    const Slot* Find(const std::string& full) const
    {
        for (const auto& s : slots)
            if (s.key.Full() == full) return &s;
        return nullptr;
    }
};

// 엔진이 자동으로 넣는 복사, SoA 줄 단위로 통째로 복사한다.
struct CopyOp 
{
    std::string from, to;
    int src, dst, count;   // 시작 줄, 시작 줄, 줄 수
};