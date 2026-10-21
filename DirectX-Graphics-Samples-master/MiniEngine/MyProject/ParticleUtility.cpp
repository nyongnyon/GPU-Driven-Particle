#include "pch.h"
#include "ParticleUtility.h"

namespace Particles
{
	uint32_t HashU32(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
		return x;
	}
}