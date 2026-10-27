#include "pch.h"
#include "ParticleSettings.h"
#include "ParticleUtility.h"

#include <cstdio>

namespace
{
	template <typename T>
	T ClampValue(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

	bool SameColor(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
	{
		return a.x == b.x && a.y == b.y && a.z == b.z;
	}

	DirectX::XMFLOAT3 Clamp01(DirectX::XMFLOAT3 c)
	{
		return { ClampValue(c.x, 0.f, 1.f), ClampValue(c.y, 0.f, 1.f), ClampValue(c.z, 0.f, 1.f) };
	}
}

bool ParticleSettings::operator==(const ParticleSettings& o) const
{
	return mode == o.mode
		&& maxParticles == o.maxParticles
		&& steadyRatio == o.steadyRatio
		&& burstRatio == o.burstRatio
		&& burstInterval == o.burstInterval
		&& minLife == o.minLife
		&& maxLife == o.maxLife
		&& particleSize == o.particleSize
		&& colorMode == o.colorMode
		&& SameColor(colorA, o.colorA)
		&& SameColor(colorB, o.colorB)
		&& colorIntensity == o.colorIntensity
		&& technique == o.technique;
}

namespace Particles
{
	ParticleSettings Sanitize(ParticleSettings s)
	{
		if (!IsValidMode(s.mode) && !IsSlotFamily(s.mode) && !IsAliveFamily(s.mode))
			s.mode = Mode::AliveIndirect;

		s.maxParticles = ClampValue(s.maxParticles, 64u, kMaxDispatchParticles);
		s.steadyRatio = ClampValue(s.steadyRatio, 0.f, 1.f);
		s.burstRatio = ClampValue(s.burstRatio, 0.f, 1.f);
		s.burstInterval = ClampValue(s.burstInterval, 1.f / 60.f, 60.f);
		s.minLife = ClampValue(s.minLife, 0.01f, 60.f);
		s.maxLife = ClampValue(s.maxLife, s.minLife, 60.f);	// max >= min 보장
		s.particleSize = ClampValue(s.particleSize, 0.001f, 5.f);

		if (static_cast<uint32_t>(s.colorMode) >= static_cast<uint32_t>(ColorMode::COUNT))
			s.colorMode = ColorMode::Fixed;
		s.colorA = Clamp01(s.colorA);
		s.colorB = Clamp01(s.colorB);
		s.colorIntensity = ClampValue(s.colorIntensity, 0.f, kMaxColorIntensity);
		return s;
	}

	EmitterParams BuildEmitterParams(const ParticleSettings& s)
	{
		EmitterParams p;		// 속도, 크기는 기본값 그대로
		p.minLife = s.minLife;
		p.maxLife = s.maxLife;
		p.minSize = p.maxSize = s.particleSize;
		p.emitRate = ComputeEmitRateByAliveRatio(s.steadyRatio, s.minLife, s.maxLife, s.maxParticles);
		p.burstCount = uint32_t(s.burstRatio * float(s.maxParticles));
		p.burstInterval = s.burstInterval;
		
		p.colorMode = s.colorMode;
		p.colorA = s.colorA;
		p.colorB = s.colorB;
		p.colorIntensity = s.colorIntensity;
		return p;
	}


	const char* ColorModeName(ColorMode mode)
	{
		switch (mode)
		{
		case ColorMode::Fixed:		return "Fixed";
		case ColorMode::Range:		return "Range";
		case ColorMode::RandomHue:	return "RandomHue";
		default:					return "None";
		}
	}

	const char* ModeName(Mode mode)
	{
		switch (mode)
		{
		case Mode::Default:			return "FullScan";
		case Mode::SlotReuse:			return "SlotReuse";
		case Mode::SlotReuseFlags:		return "SlotReuseFlags";
		case Mode::AliveDirect:			return "AliveDirect";
		case Mode::AliveIndirect:		return "AliveIndirect";
		case Mode::AliveIndirectWave:	return "AliveIndirectWave";
		case Mode::Compaction:			return "Compaction";
		default:						return "None";
		}
	}


	std::string SettingsCsvHeader()
	{
		return "mode,steadyRatio,burstRatio,burstInterval,minLife,maxLife,technique,particleSize,colorMode,colorIntensity";
	}

	std::string SettingsCsvValues(const ParticleSettings& s)
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "%s,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%.3f,%s,%.2f",
			ModeName(s.mode), s.steadyRatio, s.burstRatio, s.burstInterval, s.minLife, s.maxLife,
			TechniqueName(s.technique), s.particleSize, ColorModeName(s.colorMode), s.colorIntensity);
		return buf;
	}
}