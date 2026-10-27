Texture2D<float4> TiledResult : register(t0);

float4 main(float4 position : SV_Position) : SV_Target
{
    return TiledResult[int2(position.xy)]; // (premul rgb, 1 - T) -> BlendPreMultiplied
}