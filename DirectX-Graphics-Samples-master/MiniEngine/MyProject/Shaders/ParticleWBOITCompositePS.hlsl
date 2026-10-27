
Texture2D<float4> AccumTex : register(t0);
Texture2D<float> RevealTex : register(t1);

float4 main(float4 position : SV_Position) : SV_TARGET
{
    int2 pixel = int2(position.xy);
    
    float reveal = RevealTex[pixel];
    
    [branch]
    if (reveal >= 1.0f)
        discard;
    
    float4 accum = AccumTex[pixel];
    
    // Accum Format인 16bit 최대값을 넘어설 경우
    //if (isinf(max(max(abs(accum.r), abs(accum.g)), abs(accum.b))))
        //accum.rgb = accum.aaa;
    
    float3 average = accum.rgb / max(accum.a, 1e-5f);
    return float4(average, 1.0f - reveal);
    
}