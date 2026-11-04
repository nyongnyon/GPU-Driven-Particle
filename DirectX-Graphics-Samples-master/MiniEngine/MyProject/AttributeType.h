#pragma once
enum class AttrType 
{ 
    Float, 
    Float2, 
    Float3, 
    Float4,
    Int
};

inline int ComponentCount(AttrType t)
{
    switch (t) 
    {
    case AttrType::Float:  return 1;
    case AttrType::Float2: return 2;
    case AttrType::Float3: return 3;
    case AttrType::Float4: return 4;
    case AttrType::Int:    return 4;
    }
    return 0;
}

inline const char* TypeName(AttrType t)
{
    switch (t) {
    case AttrType::Float:  return "float";
    case AttrType::Float2: return "float2";
    case AttrType::Float3: return "float3";
    case AttrType::Float4: return "float4";
    case AttrType::Int: return "Int";
    }
    return "?";
}