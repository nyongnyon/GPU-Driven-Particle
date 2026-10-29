#pragma once

#include <string>

class ColorBuffer;

// GPU texture -> PNG file (debug / documentation captures, not for measurement)
namespace ImageCapture
{
	// "Captures/<YYYYMMDD_HHMMSS_mmm>_<name>.png". Creates the Captures folder.
	// Use the same stamp for files that belong to one moment (Save all).
	std::string MakeStamp();
	std::string MakePath(const std::string& stamp, const char* name);

	// Copies the texture to the CPU (waits for the GPU) and writes an 8-bit PNG.
	//   srgbEncode   : apply the sRGB curve (linear scene color -> what the screen shows)
	//   restoreState : state the texture is put back to after the copy
	// Supported formats : R8G8B8A8_UNORM, R11G11B10_FLOAT, R16G16B16A16_FLOAT
	bool SavePng(ColorBuffer& buffer, const std::string& path, bool srgbEncode, D3D12_RESOURCE_STATES restoreState);
}
