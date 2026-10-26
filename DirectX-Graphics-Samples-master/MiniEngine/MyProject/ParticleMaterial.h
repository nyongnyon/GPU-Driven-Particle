#pragma once
#include <cstdint>
#include <string>
#include <DirectXMath.h>
#include "TextureManager.h"

namespace Particles
{
	enum ShadingFeature : uint32_t
	{
		kFeatureTexture = 1u << 0,
		kFeatureDissolve = 1u << 1,
		kFeatureNoise = 1u << 2,
		// 추후 추가
	};

	struct alignas(16) MaterialConstants
	{
		uint32_t	features = 0;
		float		dissolveEdge = 0.05f;
		float		noiseScale = 1.0f;
		float		noiseSpeed = 1.0f;

		DirectX::XMFLOAT4 edgeColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	};
	static_assert(sizeof(MaterialConstants) == 32, "HLSL MaterialConstants(32 B)와 일치해야 함");
}

struct ParticleMaterial
{
	Particles::MaterialConstants	constants;
	TextureRef						albedo;
	TextureRef						noise;

	bool LoadAlbedo(const std::wstring& texturePath, bool sRGB = true);
	void ClearAlbedo();
};
