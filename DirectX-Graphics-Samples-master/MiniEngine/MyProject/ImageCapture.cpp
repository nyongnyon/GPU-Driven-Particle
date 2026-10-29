#include "pch.h"
#include "ImageCapture.h"

#include "ColorBuffer.h"
#include "CommandContext.h"
#include "ReadbackBuffer.h"

#include <DirectXPackedVector.h>
#include <wincodec.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

namespace
{
	const char* kCaptureDir = "Captures";

	uint8_t ToByte(float v, bool srgbEncode)
	{
		v = min(max(v, 0.f), 1.f);	// also turns NaN into 0
		if (srgbEncode)
			v = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
		return uint8_t(v * 255.f + 0.5f);
	}

	// One texel -> linear rgb. false = format not supported
	bool LoadTexel(DXGI_FORMAT format, const uint8_t* src, uint32_t x, float rgb[3])
	{
		switch (format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		{
			const uint8_t* p = src + x * 4;
			rgb[0] = p[0] / 255.f; rgb[1] = p[1] / 255.f; rgb[2] = p[2] / 255.f;
			return true;
		}
		case DXGI_FORMAT_R11G11B10_FLOAT:
		{
			XMFLOAT3 c;
			XMStoreFloat3(&c, XMLoadFloat3PK(reinterpret_cast<const XMFLOAT3PK*>(src + x * 4)));
			rgb[0] = c.x; rgb[1] = c.y; rgb[2] = c.z;
			return true;
		}
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		{
			const HALF* p = reinterpret_cast<const HALF*>(src + x * 8);
			rgb[0] = XMConvertHalfToFloat(p[0]); rgb[1] = XMConvertHalfToFloat(p[1]); rgb[2] = XMConvertHalfToFloat(p[2]);
			return true;
		}
		default:
			return false;
		}
	}

	std::wstring ToWide(const std::string& s)
	{
		return std::wstring(s.begin(), s.end());	// paths here are ASCII only
	}

	bool WritePng(const std::string& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& bgra)
	{
		ComPtr<IWICImagingFactory> factory;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			return false;

		ComPtr<IWICStream> stream;
		if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(ToWide(path).c_str(), GENERIC_WRITE)))
			return false;

		ComPtr<IWICBitmapEncoder> encoder;
		if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
			|| FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
			return false;

		ComPtr<IWICBitmapFrameEncode> frame;
		if (FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr))
			|| FAILED(frame->SetSize(width, height)))
			return false;

		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
		if (FAILED(frame->SetPixelFormat(&pixelFormat)) || pixelFormat != GUID_WICPixelFormat32bppBGRA)
			return false;

		const UINT stride = width * 4;
		if (FAILED(frame->WritePixels(height, stride, stride * height, const_cast<BYTE*>(bgra.data()))))
			return false;

		return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
	}
}

namespace ImageCapture
{
	std::string MakeStamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);

		std::tm local = {};
		localtime_s(&local, &t);

		char buf[32];
		snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d_%03d",
			local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, ms);
		return buf;
	}

	std::string MakePath(const std::string& stamp, const char* name)
	{
		CreateDirectoryA(kCaptureDir, nullptr);	// fails harmlessly if it already exists
		return std::string(kCaptureDir) + "/" + stamp + "_" + name + ".png";
	}

	bool SavePng(ColorBuffer& buffer, const std::string& path, bool srgbEncode, D3D12_RESOURCE_STATES restoreState)
	{
		const uint32_t width = buffer.GetWidth();
		const uint32_t height = buffer.GetHeight();
		const DXGI_FORMAT format = buffer.GetFormat();
		if (width == 0 || height == 0)
			return false;

		// Copy to a readback buffer and wait, so the data can be read right away
		ReadbackBuffer readback;
		CommandContext& ctx = CommandContext::Begin(L"Image Capture");
		const uint32_t rowPitch = ctx.ReadbackTexture(readback, buffer);
		ctx.TransitionResource(buffer, restoreState, true);
		ctx.Finish(true);

		std::vector<uint8_t> bgra(size_t(width) * height * 4);
		const uint8_t* src = static_cast<const uint8_t*>(readback.Map());
		bool ok = true;
		for (uint32_t y = 0; y < height && ok; ++y)
		{
			const uint8_t* row = src + size_t(y) * rowPitch;
			uint8_t* dst = bgra.data() + size_t(y) * width * 4;
			for (uint32_t x = 0; x < width; ++x)
			{
				float rgb[3];
				if (!LoadTexel(format, row, x, rgb))
				{
					ok = false;
					break;
				}
				dst[x * 4 + 0] = ToByte(rgb[2], srgbEncode);
				dst[x * 4 + 1] = ToByte(rgb[1], srgbEncode);
				dst[x * 4 + 2] = ToByte(rgb[0], srgbEncode);
				dst[x * 4 + 3] = 255;
			}
		}
		readback.Unmap();

		return ok && WritePng(path, width, height, bgra);
	}
}
