cbuffer TileConstants : register(b0)
{
    uint2 ScreenSize;
    uint2 TileCount;
};

RWTexture2D<float4> Result : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID)
{
    if (any(DTid.xy >= ScreenSize))
        return;

    // 타일마다 번갈아 칠하기. premultiplied라서 rgb에 알파가 이미 곱해진 값
    const float a = 0.25f;
    const bool odd = ((Gid.x + Gid.y) & 1) != 0;
    Result[DTid.xy] = odd ? float4(a, 0.f, 0.f, a) : float4(0.f, 0.f, a, a);
}

