#pragma once

struct float2 { float x, y; };
struct float3 { float x, y, z; };
struct float4 { float x, y, z, w; };

struct NBool
{
    int32_t value = 0;

    NBool() = default;
    NBool(bool b) : value(b ? 1 : 0) {}
    explicit operator bool() const { return value != 0; }
};

struct TypeInfo
{
    std::string name;
    uint32_t    size;    // 바이트 수
    uint32_t    align;   // 정렬 (바이트)
};


enum class AttrType 
{ 
    Float, 
    Float2, 
    Float3, 
    Float4,
    Int
};

namespace Types
{
    extern const TypeInfo Float;
    extern const TypeInfo Float2;
    extern const TypeInfo Float3;
    extern const TypeInfo Float4;
    extern const TypeInfo Int32;
    extern const TypeInfo UInt32;
    extern const TypeInfo Bool;   // NBool

    // GPU 버퍼로 그대로 옮길 수 있는지 (크기 4의 배수, 정렬 4)
    bool IsGpuCompatible(const TypeInfo& t, std::string* whyNot = nullptr);
}

// ---------------------------------------------------------------------
// C++ 타입 -> TypeInfo 연결
//   버퍼에서 값을 꺼낼 때 레이아웃의 타입과 같은지 확인하는 데 쓴다.
//   새 타입을 추가하려면 여기에 선언하고 AttributeType.cpp 에 정의를 하나 더 쓴다.
// ---------------------------------------------------------------------
template <typename T> const TypeInfo& TypeOf();

template <> const TypeInfo& TypeOf<float>();
template <> const TypeInfo& TypeOf<float2>();
template <> const TypeInfo& TypeOf<float3>();
template <> const TypeInfo& TypeOf<float4>();
template <> const TypeInfo& TypeOf<int32_t>();
template <> const TypeInfo& TypeOf<uint32_t>();
template <> const TypeInfo& TypeOf<NBool>();
