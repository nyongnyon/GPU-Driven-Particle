#include "pch.h"
#include "ParticleMaterial.h"

#include "Utility.h"
#include "TextureConvert.h"

using namespace Particles;

bool ParticleMaterial::LoadAlbedo(const std::wstring& texturePath, bool sRGB)
{
	CompileTextureOnDemand(texturePath, TextureOptions(sRGB, true, false));

	const std::wstring ddsPath = Utility::RemoveExtension(texturePath) + L".dds";
	albedo = TextureManager::LoadDDSFromFile(ddsPath, Graphics::kMagenta2D, sRGB);

	if (!albedo.IsValid())
	{
		Utility::Printf(L"[ParticleMaterial] Albedo load failed : %ws\n", ddsPath.c_str());
		ClearAlbedo();
		return false;
	}

	constants.features |= kFeatureTexture;
	return true;
}

void ParticleMaterial::ClearAlbedo()
{
	albedo = nullptr;
	constants.features &= ~static_cast<uint32_t>(kFeatureTexture);
}