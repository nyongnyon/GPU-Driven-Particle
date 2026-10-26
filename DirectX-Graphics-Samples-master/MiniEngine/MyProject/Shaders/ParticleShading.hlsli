#ifndef PARTICLE_SHADING_HLSLI
#define PARTICLE_SHADING_HLSLI

#define FEATURE_TEXTURE		(1u << 0)
#define FEATURE_DISSOLVE	(1u << 1)
#define FEATURE_NOISE		(1u << 2)
#define FEATURE_SOFT		(1u << 3)

cbuffer MaterialConstants : register(b1)
{
    uint Features;
    float DissolveEdge;
    float NoiseScale;
    float NoiseSpeed;
    float4 EdgeColor;
};

Texture2D<float4> AlbedoTex : register(t3);
Texture2D<float4> NoiseTex : register(t4);

SamplerState LinearWrap : register(s0);
SamplerState LinearClamp : register(s1);

// (-1 ~ 1) -> (0 ~ 1), 좌상 : (0, 0) / 우하 (1, 1)
float2 CornerToUV(float2 corner)
{
    return float2(corner.x * 0.5f + 0.5f, 0.5f - corner.y * 0.5f);
}

float4 ShadeParticle(float4 color, float2 corner)
{
    float3 rgb;
    float a;

	// Features는 드로우 전체에서 같은 값 -> 분기가 갈라지지 않는다
	[branch]
    if (Features & FEATURE_TEXTURE)
    {
		// 텍스처는 straight alpha(일반 PNG/DDS) 기준
        float4 tex = AlbedoTex.Sample(LinearClamp, CornerToUV(corner));
        rgb = color.rgb * tex.rgb;
        a = color.a * tex.a;
    }
    else
    {
        float falloff = saturate(1.0f - dot(corner, corner));
        rgb = color.rgb;
        a = color.a * falloff * falloff;
    }

    return float4(rgb * a, a);
}

#endif